#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldCombatEffect.h"
#include "UIs/RpgUITheme.h"
#include "GameClientWorld.h"
#include "Engine/World.h"

void UGameSessionSubsystem::ActivateQuickSlot(int32 index, bool fromHotkey)
{
    if (this->state != 5 || InventoryVisible() || this->currentNpc || this->shopOpening || this->exitWarping ||
        index < 0 || index >= 10)
    {
        return;
    }

    auto* own = GetInventoryCharacter();
    const uint32 SKILL = this->quickSlotBindings[index];
    const auto* catalog = GetSkillCatalog();
    if (!own || own->dead || !catalog || !catalog->definitions.count(SKILL) || !GetSkillRank(SKILL))
    {
        return;
    }

    if (!CanAffordSkill(SKILL))
    {
        return;
    }

    const auto& definition = catalog->definitions.at(SKILL);
    if (fromHotkey && definition.aim == shared_world::skills::Aim::TARGET)
    {
        uint64 target = this->combatTarget;
        auto* enemy = this->npcs.FindRef(target).Get();
        if (!IsValid(enemy) || enemy->dead || !enemy->attackable)
        {
            target = PickCombatTarget();
            enemy = this->npcs.FindRef(target).Get();
        }

        if (IsValid(enemy) && !enemy->dead && enemy->attackable)
        {
            this->selectedQuickSlot = -1;
            QueueCombatIntent(SKILL, target, enemy->GetActorLocation(), 0, true);
            return;
        }

    }

    this->selectedQuickSlot = this->selectedQuickSlot == index ? -1 : index;
    this->aimingArea = this->selectedQuickSlot >= 0 && definition.aim != shared_world::skills::Aim::TARGET;
    if (IsValid(this->combatAimMarker))
    {
        this->combatAimMarker->Release();
    }

}

bool UGameSessionSubsystem::BindQuickSlot(int32 index, uint32 binding)
{
    const auto* catalog = GetSkillCatalog();
    if (index < 0 || index >= 10 ||
        (binding && (!catalog || !catalog->definitions.count(binding) || !GetSkillRank(binding))))
    {
        return false;
    }

    if (!this->economyReady || EconomyBusy())
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::BIND_SKILL;
    query.instance = binding;
    query.destination = index;
    if (!RequestEconomy(query))
    {
        return false;
    }

    this->quickSlotBindings[index] = binding;
    this->selectedQuickSlot = -1;
    this->aimingArea = false;
    if (IsValid(this->combatAimMarker))
    {
        this->combatAimMarker->Release();
    }

    return true;
}

TArray<FWorldQuickSlotState> UGameSessionSubsystem::GetQuickSlotStates() const
{
    TArray<FWorldQuickSlotState> result;
    const auto* own = GetInventoryCharacter();
    const auto* catalog = GetSkillCatalog();
    const bool ALIVE = this->state == 5 && own && !own->dead;
    for (int32 i = 0; i < 10; ++i)
    {
        FWorldQuickSlotState value;
        value.key = FString::FromInt((i + 1) % 10);
        value.binding = this->quickSlotBindings[i];
        value.selected = this->selectedQuickSlot == i;
        value.enabled = ALIVE && !InventoryVisible() && !this->currentNpc && !this->shopOpening && !this->exitWarping;
        if (catalog && catalog->definitions.count(value.binding) && GetSkillRank(value.binding))
        {
            const auto& definition = catalog->definitions.at(value.binding);
            const auto ABILITY = GetAbility(value.binding);
            value.enabled = value.enabled && own->mp >= int32(ABILITY.mp);
            value.name = UTF8_TO_TCHAR(definition.name.c_str());
            value.icon = RpgUI::SkillIcon(value.binding);
            value.cooldown = FMath::Max(0., this->abilityReadyAt.FindRef(value.binding) - CombatServerNow());
            value.usable = ALIVE && !this->ownCast && !this->attackRequestPending && value.cooldown <= 0 &&
                           own->mp >= int32(ABILITY.mp);
            value.detail = FString::Printf(TEXT("%u MP"), ABILITY.mp);
        }

        result.Add(value);
    }

    return result;
}

void UGameSessionSubsystem::BuildMinimap(FWorldMinimapSnapshot& snapshot) const
{
    snapshot = {};
    if (this->state != 5 || !this->map || !this->map->content || !this->map->residency)
    {
        return;
    }

    constexpr float RANGE = 5000;
    auto project = [&](shared_world::WorldPosition p)
    {
        return FVector2D((double(p.y) - this->predicted.y) / RANGE, (double(this->predicted.x) - p.x) / RANGE);
    };

    auto actorPoint = [&](const AActor* actor)
    {
        const auto P = actor->GetActorLocation() + FVector(GetWorld()->OriginLocation);
        return FVector2D((P.Y - this->predicted.y) / RANGE, (this->predicted.x - P.X) / RANGE);
    };

    snapshot.title = this->prepared.map.id == 301   ? TEXT("Dungeon")
                     : this->prepared.map.id == 201 ? TEXT("Peace World")
                                                    : FString::Printf(TEXT("Map %u"), this->prepared.map.id);
    if (auto* own = GetInventoryCharacter())
    {
        snapshot.heading = own->GetActorRotation().Yaw;
    }

    if (auto collision = this->map->residency->Get(this->predicted))
    {
        const auto BOUNDS = collision->Map().Definition().bounds;
        shared_world::WorldBounds area{{FMath::Max(BOUNDS.min.x, this->predicted.x - 2500),
                                        FMath::Max(BOUNDS.min.y, this->predicted.y - 2500), BOUNDS.min.z},
                                       {FMath::Min(BOUNDS.max.x, this->predicted.x + 2500),
                                        FMath::Min(BOUNDS.max.y, this->predicted.y + 2500), BOUNDS.max.z}};
        const auto CACHE = collision->CacheGeometry(area);
        collision->VisitGeometry(
            area, 2'000'000,
            [&](const shared_world::Triangle& t)
            {
                if (snapshot.lines.Num() + 6 > 3072)
                {
                    return;
                }

                const auto NORMAL =
                    shared_world::Cross(shared_world::Difference(t.b, t.a), shared_world::Difference(t.c, t.a));
                if (std::abs(NORMAL.z) > std::max(std::abs(NORMAL.x), std::abs(NORMAL.y)))
                {
                    return;
                }

                const auto A = project(t.a), B = project(t.b), C = project(t.c);
                snapshot.lines.Append({A, B, B, C, C, A});
            },

            [&](const shared_world::WorldBounds& volume)
            {
                if (snapshot.lines.Num() + 8 > 3072)
                {
                    return;
                }

                const auto A = project(volume.min), C = project(volume.max);
                const FVector2D B(C.X, A.Y), D(A.X, C.Y);
                snapshot.lines.Append({A, B, B, C, C, D, D, A});
            },

            &CACHE);
    }

    for (const auto& entry : this->npcs)
    {
        if (IsValid(entry.Value) && !entry.Value->dead)
        {
            if (entry.Value->attackable)
            {
                snapshot.enemies.Add(actorPoint(entry.Value));
            }
            else
            {
                snapshot.npcs.Add(actorPoint(entry.Value));
            }

        }

    }

    for (const auto& entry : this->players)
    {
        if (entry.Key != this->prepared.epoch && IsValid(entry.Value))
        {
            snapshot.players.Add(actorPoint(entry.Value));
        }

    }

    for (uint32 i = 0; i < this->dungeonExits.count; ++i)
    {
        snapshot.exits.Add(project(this->dungeonExits.zones[i].feet));
    }

    if (this->prepared.map.id != 301 && this->portalActor)
    {
        snapshot.exits.Add(actorPoint(this->portalActor));
    }

}

void UGameSessionSubsystem::ClearAttackHighlight()
{
    if (auto* enemy = this->highlightedEnemy.Get())
    {
        enemy->SetCombatHighlighted(false);
    }

    this->highlightedEnemy.Reset();
}

