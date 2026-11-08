#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldControlHUD.h"
#include "World/WorldCombatEffect.h"
#include "GameClientWorld.h"

const shared_world::skills::Catalog* UGameSessionSubsystem::GetSkillCatalog() const
{
    return this->map && this->map->content->combat ? &this->map->content->combat->skills : nullptr;
}

shared_world::skills::State UGameSessionSubsystem::GetSkillState() const
{
    if (this->economyReady)
    {
        return this->economyState.skills;
    }

    const auto* catalog = GetSkillCatalog();
    return catalog ? catalog->Initial() : shared_world::skills::State{};
}

uint32 UGameSessionSubsystem::GetSkillRank(uint32 skill) const
{
    return skill <= 2 ? 1 : skill <= 64 ? GetSkillState().ranks[skill] : 0;
}

shared_world::combat::Ability UGameSessionSubsystem::GetAbility(uint32 skill) const
{
    if (!this->map || !this->map->content->combat || !this->map->content->combat->abilities.count(skill))
    {
        return {};
    }

    auto ability = this->map->content->combat->abilities.at(skill);
    const auto* catalog = GetSkillCatalog();
    const uint32 RANK = GetSkillRank(skill);
    if (catalog && RANK && catalog->ranks.count({skill, RANK}))
    {
        ability = shared_world::combat::Ranked(ability, catalog->ranks.at({skill, RANK}));
    }

    return ability;
}

bool UGameSessionSubsystem::CanAffordSkill(uint32 skill) const
{
    const auto* own = GetInventoryCharacter();
    return own && !own->dead && this->map && this->map->content->combat &&
           this->map->content->combat->abilities.count(skill) && GetSkillRank(skill) &&
           own->mp >= int32(GetAbility(skill).mp);
}

void UGameSessionSubsystem::CancelUnaffordableSkill()
{
    bool cancelled = false;
    const uint32 SELECTED = GetQuickSlotBinding(this->selectedQuickSlot);
    if (SELECTED && !CanAffordSkill(SELECTED))
    {
        this->selectedQuickSlot = -1;
        this->aimingArea = false;
        cancelled = true;
        if (IsValid(this->combatAimMarker))
        {
            this->combatAimMarker->Release();
        }

    }

    // A server-accepted cast owns its MP already. Do not cancel it when the
    // following stats snapshot reports that cost; only discard unsent input.
    if (this->intentSkill > 2 && !this->intentSent && !CanAffordSkill(this->intentSkill))
    {
        CancelCombatIntent();
        cancelled = true;
    }

    if (cancelled)
    {
        // The held/next primary input can now use the normal basic-attack path.
        this->primaryManaFallback = this->primaryManaFallback || this->suppressPrimaryUntilRelease;
        this->suppressPrimaryUntilRelease = false;
        this->primaryAttackGesture = false;
    }

}

bool UGameSessionSubsystem::TrainSkill(uint32 skill)
{
    const auto* catalog = GetSkillCatalog();
    if (!catalog || !this->economyReady || !catalog->definitions.count(skill))
    {
        return false;
    }

    auto progression = GetSkillState();
    const uint32 NEXT = progression.ranks[skill] + 1;
    if (!catalog->Train(progression, skill, NEXT))
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::TRAIN_SKILL;
    query.instance = skill;
    query.quantity = NEXT;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::RegisterPotionType(int32 index, uint32 type)
{
    if (index < 0 || index > 1)
    {
        return false;
    }

    if (type)
    {
        if (!this->itemCatalog)
        {
            return false;
        }

        const auto IT = this->itemCatalog->consumables.find(type);
        if (IT == this->itemCatalog->consumables.end() || bool(IT->second.mana) != (index == 1))
        {
            return false;
        }

        bool owned = false;
        for (const auto& item : this->economyState.items)
        {
            if (item.type == type && item.quantity)
            {
                owned = true;
            }

        }

        if (!owned)
        {
            return false;
        }

    }

    if (!this->economyReady || EconomyBusy())
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::REGISTER_POTION;
    query.instance = type;
    query.destination = index;
    if (!RequestEconomy(query))
    {
        return false;
    }

    this->registeredPotions[index] = type;
    return true;
}

TArray<FWorldQuickSlotState> UGameSessionSubsystem::GetPotionStates() const
{
    TArray<FWorldQuickSlotState> result;
    const auto* own = GetInventoryCharacter();
    for (int32 i = 0; i < 2; ++i)
    {
        FWorldQuickSlotState value;
        value.binding = this->registeredPotions[i];
        value.icon = i ? TEXT("Mana") : TEXT("Health");
        uint32 count = 0;
        for (const auto& item : this->economyState.items)
        {
            if (item.type == value.binding)
            {
                count += item.quantity;
            }

        }

        value.name = i ? TEXT("MP <50%") : TEXT("HP ≤50%");
        value.detail = value.binding ? FString::Printf(TEXT("×%u"), count) : TEXT("등록");
        value.enabled = own && !own->dead;
        value.selected = value.binding != 0;
        value.usable = value.selected && count > 0;
        value.cooldown = FMath::Max(0., this->potionHudReadyAt - FPlatformTime::Seconds());
        result.Add(value);
    }

    return result;
}

void UGameSessionSubsystem::TickAutoPotions()
{
    const double NOW = FPlatformTime::Seconds();
    const auto* own = GetInventoryCharacter();
    if (!own || own->dead || !this->economyReady || EconomyBusy() || NOW < this->potionHudReadyAt ||
        NOW < this->nextAutoPotion)
    {
        return;
    }

    for (int32 i = 0; i < 2; ++i)
    {
        const bool LOW = i ? own->mp * 2 < own->maxMp : own->hp * 2 <= own->maxHp;
        if (!LOW || !this->registeredPotions[i])
        {
            continue;
        }

        bool owned = false;
        for (const auto& item : this->economyState.items)
        {
            if (item.type == this->registeredPotions[i] && item.quantity)
            {
                owned = true;
            }

        }

        if (!owned)
        {
            continue;
        }

        this->nextAutoPotion = NOW + .3;
        if (UseRegisteredPotion(i))
        {
            return;
        }

    }

}

