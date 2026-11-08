#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "World/WorldEntityPool.h"
#include "World/WorldPlayerPawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcSystemTest, "RPG.World.NpcPoolAndIdle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNpcSystemTest::RunTest(const FString&)
{
    const auto ROOT = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Npcs"));
    const auto CATALOG = shared_world::npc::Catalog::Load(std::filesystem::u8path(TCHAR_TO_UTF8(*ROOT)));
    const auto LOOK = CATALOG.appearances.at(1);
    auto* world = UWorld::CreateWorld(EWorldType::Game, false);
    world->AddToRoot();
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
    auto* pool = world->GetSubsystem<UWorldEntityPool>();
    if (!TestNotNull(TEXT("World pool"), pool))
    {
        world->DestroyWorld(false);
        GEngine->DestroyWorldContext(world);
        world->RemoveFromRoot();
        return false;
    }

    auto* pawn = pool->Acquire(TEXT("Npc:1"));
    pawn->DispatchBeginPlay();
    pawn->LoadNpcAppearance(LOOK);
    // Cancel a different lease while assets are still pending, then reacquire it.
    auto* cancelled = pool->Acquire(TEXT("Npc:1"));
    cancelled->DispatchBeginPlay();
    cancelled->LoadNpcAppearance(LOOK);
    pool->Release(cancelled);
    const double STARTED = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, world, pool, pawn, cancelled, LOOK, STARTED]()
        {
            if (!pawn->AppearanceReady() && FPlatformTime::Seconds() - STARTED < 30)
            {
                return false;
            }

            TestTrue(TEXT("Elf asynchronous appearance ready"), pawn->AppearanceReady());
            TestFalse(TEXT("Cancelled lease stays hidden"), cancelled->IsPoolActive());
            auto* mesh = pawn->FindComponentByClass<USkeletalMeshComponent>();
            TestNotNull(TEXT("Elf skeletal body"), mesh);
            if (mesh)
            {
                TestEqual(TEXT("Elves source mesh"), mesh->GetSkeletalMeshAsset()->GetPathName(),
                          FString(UTF8_TO_TCHAR(LOOK.mesh.c_str())));
                auto* animation = mesh->GetSingleNodeInstance();
                TestTrue(TEXT("Idle looping"), animation && animation->IsLooping());
                mesh->TickAnimation(.5f, false);
                mesh->RefreshBoneTransforms();
                const auto POSE = mesh->GetBoneSpaceTransforms();
                const auto& reference = mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
                bool animated = false;
                for (int32 bone = 1; bone < FMath::Min(POSE.Num(), reference.Num()); ++bone)
                {
                    animated |= !POSE[bone].GetRotation().Equals(reference[bone].GetRotation(), .001);
                }

                TestTrue(TEXT("Elf Idle differs from reference pose"), animated);
                for (int32 i = 0; i < 100; ++i)
                {
                    pawn->npcIndex = 1001;
                    pawn->npcRole = 4;
                    pawn->npcRoleData = 99;
                    auto stats = shared_world::EntityStats::PlayerDefaults();
                    stats.ApplyDamage(13);
                    stats.SpendMp(7);
                    pawn->ApplyServerStats(stats);
                    TestEqual(TEXT("Server HP applied"), pawn->hp, 4987);
                    TestEqual(TEXT("Server MP applied"), pawn->mp, 43);
                    pawn->SetCombatHighlighted(true);
                    CollectGarbage(RF_NoFlags);
                    TestTrue(TEXT("Attacking outline state survives GC"), pawn->IsCombatHighlighted());
                    pawn->SetLocalVisibility(true);
                    CollectGarbage(RF_NoFlags);
                    TestTrue(TEXT("Local visibility material survives GC"),
                             pawn->HasLocalVisibility() && IsValid(pawn->GetVisibilityMaterial()));
                    TestTrue(TEXT("Own mesh writes reserved stencil"),
                             mesh->bRenderCustomDepth && mesh->CustomDepthStencilValue == 241);
                    pool->Release(pawn);
                    TestFalse(TEXT("Pooled actor loses attack outline"), pawn->IsCombatHighlighted());
                    TestFalse(TEXT("Pooled actor loses local visibility"), pawn->HasLocalVisibility());
                    TestFalse(TEXT("Pooled camera performs no collision ticks"), pawn->arm->IsComponentTickEnabled());
                    TestFalse(TEXT("Released mesh cannot reveal a future remote/NPC lease"), mesh->bRenderCustomDepth);
                    TestEqual(TEXT("Released stencil cleared"), mesh->CustomDepthStencilValue, 0);
                    TestTrue(TEXT("All stats and combat policy reset on release"),
                             pawn->GetServerStats() == shared_world::EntityStats{} && !pawn->dead);
                    TestFalse(TEXT("Pooled animation tick disabled"), mesh->IsComponentTickEnabled());
                    TestTrue(TEXT("Pooled actor hidden"), pawn->IsHidden());
                    TestEqual(TEXT("Role reset on release"), pawn->npcRoleData, 0);
                    auto* reused = pool->Acquire(TEXT("Npc:1"));
                    TestTrue(TEXT("Matching appearance actor reused"), reused == pawn);
                    reused->ApplyServerStats({});
                    TestFalse(TEXT("Service zero HP is not dead"), reused->dead);
                    reused->LoadNpcAppearance(LOOK);
                    TestTrue(TEXT("Skeletal components reused"),
                             reused->FindComponentByClass<USkeletalMeshComponent>() == mesh);
                }

                CollectGarbage(RF_NoFlags);
                TestTrue(TEXT("Pool and mesh survive GC"), IsValid(pawn) && IsValid(mesh) && pawn->AppearanceReady());
                TestEqual(TEXT("No allocations on repeated leases"), pool->AllocatedCount(), 2);
            }

            pool->Release(pawn);
            // Bound the pool under saturation and prove release recovers one lease.
            TArray<AWorldPlayerPawn*> leases;
            for (int32 i = 0; i < 288; ++i)
            {
                leases.Add(pool->Acquire(TEXT("Player:0:0")));
            }

            TestEqual(TEXT("Pool capacity"), pool->AllocatedCount(), 288);
            TestNull(TEXT("Pool saturation bounded"), pool->Acquire(TEXT("Player:0:0")));
            if (leases[0])
            {
                pool->Release(leases[0]);
                TestNotNull(TEXT("Pool recovers after saturation"), pool->Acquire(TEXT("Player:0:0")));
            }

            world->DestroyWorld(false);
            GEngine->DestroyWorldContext(world);
            world->RemoveFromRoot();
            return true;
        }));

    return true;
}

#endif
