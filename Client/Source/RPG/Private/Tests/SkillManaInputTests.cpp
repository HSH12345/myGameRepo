#include "World/GameSessionSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "World/WorldPlayerPawn.h"
#include "World/WorldCombatEffect.h"
#include "../World/GameClientWorld.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillManaInputTest, "RPG.World.SkillManaInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillManaInputTest::RunTest(const FString&)
{
    using namespace shared_world;
    auto* world = UWorld::CreateWorld(EWorldType::Game, false);
    world->AddToRoot();
    ON_SCOPE_EXIT
    {
        world->DestroyWorld(false);
        world->RemoveFromRoot();
    };

    TStrongObjectPtr<UGameInstance> game(NewObject<UGameInstance>());
    TStrongObjectPtr<UGameSessionSubsystem> session(NewObject<UGameSessionSubsystem>(game.Get()));
    auto catalog = std::make_shared<combat::Catalog>(combat::Catalog::Load(std::filesystem::u8path(
        TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Combat"))))));
    session->map = std::make_shared<FGameClientWorld>();
    session->map->content = std::make_shared<WorldContent>();
    session->map->content->combat = catalog;
    session->state = 5;
    session->prepared.epoch = 42;
    session->prepared.request = 1;
    auto* own = world->SpawnActor<AWorldPlayerPawn>();
    session->players.Add(42, own);
    own->ApplyServerStats(EntityStats::PlayerDefaults());
    auto* enemy = world->SpawnActor<AWorldPlayerPawn>();
    enemy->attackable = true;
    session->combatTarget = npc::ENTITY_BIT | 101;
    session->npcs.Add(session->combatTarget, enemy);
    session->economyReady = true;
    session->economyState.skills = catalog->skills.Initial();
    int32 slot = 0;
    for (const auto& row : catalog->skills.definitions)
    {
        const uint32 SKILL = row.first;
        session->economyState.skills.ranks[SKILL] = 1;
        session->quickSlotBindings[slot] = SKILL;
        const int32 COST = session->GetAbility(SKILL).mp;
        own->mp = COST - 1;
        session->selectedQuickSlot = -1;
        session->ActivateQuickSlot(slot);
        TestEqual(TEXT("Insufficient MP blocks slot click"), session->selectedQuickSlot, -1);
        session->ActivateQuickSlot(slot, true);
        TestEqual(TEXT("Insufficient MP blocks number key"), session->selectedQuickSlot, -1);
        TestEqual(TEXT("Insufficient MP never queues a skill"), session->intentSkill, 0U);
        TestFalse(TEXT("Insufficient MP disables slot"), session->GetQuickSlotStates()[slot].enabled);
        own->mp = COST;
        session->ActivateQuickSlot(slot);
        TestEqual(TEXT("Exact MP cost allows selection"), session->selectedQuickSlot, slot);
        TestTrue(TEXT("Exact MP cost enables slot"), session->GetQuickSlotStates()[slot].enabled);
        session->combatAimMarker = world->SpawnActor<AWorldCombatEffect>();
        session->suppressPrimaryUntilRelease = true;
        // Exercise the actual server stats receive path, including MP depletion.
        net::Message stats;
        stats.kind = net::Kind::ENTITY_STATS;
        stats.epoch = 42;
        stats.request = 1;
        stats.entity = 42;
        stats.stats = own->GetServerStats();
        stats.stats.mp = COST - 1;
        if (!TestTrue(TEXT("Stats fixture satisfies the wire contract"), stats.Valid()))
        {
            return false;
        }

        const auto BYTES = stats.Encode();
        session->Receive(BYTES.data(), int32(BYTES.size()));
        TestEqual(TEXT("Server MP update clears selection"), session->selectedQuickSlot, -1);
        TestFalse(TEXT("Server MP update clears aiming"), session->aimingArea);
        TestFalse(TEXT("MP fallback releases skill gesture suppression"), session->suppressPrimaryUntilRelease);
        TestTrue(TEXT("Held fallback cannot become ground movement"), session->primaryManaFallback);
        TestFalse(TEXT("Aim marker is released"), session->combatAimMarker->Active());
        session->QueueCombatIntent(1, session->combatTarget, enemy->GetActorLocation(), 1);
        TestEqual(TEXT("Melee basic can queue at insufficient MP"), session->intentSkill, 1U);
        session->CancelCombatIntent();
        session->QueueCombatIntent(2, session->combatTarget, enemy->GetActorLocation(), 1);
        TestEqual(TEXT("Ranged basic can queue at insufficient MP"), session->intentSkill, 2U);
        session->CancelCombatIntent();
        ++slot;
    }

    TestEqual(TEXT("All ten skill types checked"), slot, 10);
    session->quickSlotBindings[0] = 3;
    session->quickSlotBindings[1] = 4;
    // Selection, HUD state and input must all use the current rank's cost.
    catalog->skills.ranks.at({3, 2}).mp = 17;
    session->economyState.skills.ranks[3] = 2;
    own->mp = 16;
    session->ActivateQuickSlot(0);
    TestEqual(TEXT("Rank-specific MP cost blocks selection"), session->selectedQuickSlot, -1);
    own->mp = 17;
    session->ActivateQuickSlot(0);
    TestEqual(TEXT("Rank-specific exact cost allows selection"), session->selectedQuickSlot, 0);
    session->intentSkill = 3;
    session->intentSent = false;
    own->mp = 0;
    session->CancelUnaffordableSkill();
    TestEqual(TEXT("Unsent skill is discarded when MP drops"), session->intentSkill, 0U);
    session->intentSkill = 3;
    session->intentSent = true;
    session->ownCast = 99;
    session->CancelUnaffordableSkill();
    TestEqual(TEXT("Already submitted skill remains intact"), session->intentSkill, 3U);
    TestEqual(TEXT("Last affordable cast is not cancelled by its MP cost"), session->ownCast, uint64(99));
    session->ownCast = 0;
    session->CancelCombatIntent();
    // An owner MP rejection can arrive before its MP snapshot.
    own->mp = 50;
    session->ActivateQuickSlot(0);
    session->aimingArea = true;
    session->attackRequestPending = true;
    net::Message rejected;
    rejected.kind = net::Kind::COMBAT_EVENT;
    rejected.combatEvent.phase = combat::Phase::REJECTED;
    rejected.combatEvent.skill = 3;
    rejected.combatEvent.reason = unsigned(combat::Reject::MP);
    session->ReceiveCombat(rejected);
    TestEqual(TEXT("Owner MP rejection clears stale affordable selection"), session->selectedQuickSlot, -1);
    TestFalse(TEXT("Owner MP rejection releases suppression"), session->suppressPrimaryUntilRelease);
    TestFalse(TEXT("Owner MP rejection clears pending request"), session->attackRequestPending);
    session->ActivateQuickSlot(1);
    session->ReceiveCombat(rejected);
    TestEqual(TEXT("Late rejection keeps a different affordable selection"), session->selectedQuickSlot, 1);
    session->selectedQuickSlot = -1;
    session->ActivateQuickSlot(0);
    rejected.combatEvent.reason = unsigned(combat::Reject::COOLDOWN);
    session->ReceiveCombat(rejected);
    TestEqual(TEXT("Cooldown does not clear affordable selection"), session->selectedQuickSlot, 0);
    session->players.Empty();
    session->npcs.Empty();
    session->ClearCombat();
    return true;
}

#endif
