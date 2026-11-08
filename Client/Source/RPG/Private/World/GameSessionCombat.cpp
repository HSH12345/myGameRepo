#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldCombatEffect.h"
#include "GameClientWorld.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

double UGameSessionSubsystem::CombatServerNow() const
{
    return this->combatClock ? FPlatformTime::Seconds() + this->combatOffset : 0;
}

void UGameSessionSubsystem::ClearCombat()
{
    for (const auto& effect : this->combatEffects)
    {
        if (IsValid(effect))
        {
            effect->Destroy();
        }

    }

    ClearAttackHighlight();
    this->activeAttackTarget = 0;
    this->selectedQuickSlot = -1;
    this->savedBossCameraDistance = 0;
    this->combatEffects.Empty();
    this->combatLives.Empty();
    this->combatTarget = 0;
    this->ownCast = 0;
    this->combatSequence = 0;
    this->aimingArea = false;
    this->combatClock = false;
    if (IsValid(this->combatAimMarker))
    {
        this->combatAimMarker->Destroy();
    }

    this->combatAimMarker = nullptr;
    if (IsValid(this->combatTargetMarker))
    {
        this->combatTargetMarker->Destroy();
    }

    this->combatTargetMarker = nullptr;
    this->actionInputBits = 0;
    this->actionPressedBits = 0;
    this->actionReleasedBits = 0;
    this->intentSkill = 0;
    this->intentTarget = 0;
    this->intentLife = 0;
    this->intentRepeatKey = 0;
    this->intentSent = false;
    this->attackRequestPending = false;
    this->ownSkill = 0;
    this->ownRecoveryEnd = 0;
    this->ownCastEnd = 0;
    this->abilityReadyAt.Empty();
    this->nextCombatSubmit = 0;
    this->nextPointerRefresh = 0;
    this->nextApproachPlan = 0;
    this->approachProgressAt = 0;
    this->combatMoveOverride = false;
    this->primaryAttackGesture = false;
    this->suppressPrimaryUntilRelease = false;
    this->primaryManaFallback = false;
    this->combatActionText = TEXT("Ready");
    this->rightGestureActive = false;
    this->rightCameraDrag = false;
    this->rightSkillTap = false;
    this->rightClickConsumed = false;
    this->rightDragDistance = 0;
    this->rightGestureX = 0;
    this->rightGestureY = 0;
#if PLATFORM_ANDROID
    this->mobileTouch = {};
#endif
}

AWorldCombatEffect* UGameSessionSubsystem::CombatEffect(const shared_world::combat::Event& e, int32 kind)
{
    if (!this->map || !this->map->content->combat)
    {
        return nullptr;
    }

    AWorldCombatEffect* effect = nullptr;
    for (const auto& candidate : this->combatEffects)
    {
        if (IsValid(candidate) && !candidate->Active())
        {
            effect = candidate;
            break;
        }

    }

    if (!effect && this->combatEffects.Num() < 192)
    {
        effect = GetWorld()->SpawnActor<AWorldCombatEffect>();
        if (effect)
        {
            this->combatEffects.Add(effect);
        }

    }

    if (!effect)
    {
        return nullptr;
    }

    const auto ABILITY = this->map->content->combat->abilities.find(e.skill);
    auto display = e;
    const auto HEIGHT = [&](uint64 id)
    {
        if (!(id & shared_world::npc::ENTITY_BIT))
        {
            return 160U;
        }

        const auto* pawn = this->npcs.FindRef(id).Get();
        if (!pawn)
        {
            return 160U;
        }

        const auto FOUND = this->map->content->combat->monsters.find(pawn->npcIndex);
        return FOUND == this->map->content->combat->monsters.end() ? 160U : FOUND->second.bodyHeight;
    };

    // Lift only cosmetic copies to hands/torso; authoritative feet and hit
    // geometry remain exactly as received from the map owner.
    if (kind == 1)
    {
        display.origin.z += FMath::RoundToInt(HEIGHT(e.source) * .6);
        display.point.z += FMath::RoundToInt(HEIGHT(e.target) * .6);
    }

    if (kind == 3 || kind == 5)
    {
        display.point.z += FMath::RoundToInt(HEIGHT(e.target) * (kind == 3 ? .95 : .65) + (kind == 3 ? 20 : 0));
    }

    effect->Activate(display, ABILITY == this->map->content->combat->abilities.end() ? nullptr : &ABILITY->second,
                     CombatServerNow(), (e.source & shared_world::npc::ENTITY_BIT) != 0, kind);
    return effect;
}

bool UGameSessionSubsystem::ReceiveCombat(const shared_world::net::Message& m)
{
    using namespace shared_world;
    using namespace combat;
    if (m.kind != net::Kind::COMBAT_EVENT)
    {
        return false;
    }

    const auto& e = m.combatEvent;
    const double CANDIDATE = double(e.serverMs) / 1000 - FPlatformTime::Seconds();
    this->combatOffset = this->combatClock ? FMath::Max(this->combatOffset, CANDIDATE) : CANDIDATE;
    this->combatClock = true;
    if (e.phase == Phase::CLOCK)
    {
        if (!this->map->content->combat || e.action != this->map->content->combat->checksum)
        {
            Fail(TEXT("Combat data build mismatch"));
        }

        return true;
    }

    auto find = [&](uint64 id) -> AWorldPlayerPawn*
    {
        auto* entry = (id & npc::ENTITY_BIT) ? this->npcs.Find(id) : this->players.Find(id);
        return entry && IsValid(*entry) ? *entry : nullptr;
    };

    const auto ABILITY = this->map->content->combat->abilities.find(e.skill);
    const auto RANKED_ABILITY = GetAbility(e.skill);
    const Ability* a = ABILITY == this->map->content->combat->abilities.end() ? nullptr
                       : e.source == this->prepared.epoch                     ? &RANKED_ABILITY
                                                                              : &ABILITY->second;
    if (e.phase == Phase::REJECTED)
    {
        static const TCHAR* reasons[] = {TEXT(""),
                                         TEXT("Combat unavailable"),
                                         TEXT("You are dead"),
                                         TEXT("Action in progress"),
                                         TEXT("Select a living enemy"),
                                         TEXT("Target is out of range"),
                                         TEXT("Not enough MP"),
                                         TEXT("Ability is cooling down"),
                                         TEXT("Too many requests"),
                                         TEXT("Terrain blocks the attack"),
                                         TEXT("Cast cancelled")};
        this->attackRequestPending = false;
        this->nextCombatSubmit = FPlatformTime::Seconds() + .2;
        // Only a still-held input may retry transient owner timing/CD rejection, at most 5 Hz.
        // A click, invalid target, MP failure or released key never becomes a persistent retry.
        if (this->intentSkill && this->intentRepeatKey &&
            (e.reason == unsigned(Reject::BUSY) || e.reason == unsigned(Reject::COOLDOWN) ||
             e.reason == unsigned(Reject::RATE_LIMITED)))
        {
            this->combatActionText = TEXT("Waiting for server cooldown");
        }
        else
        {
            CancelCombatIntent();
        }

        if (e.reason == unsigned(Reject::MP))
        {
            // The owner can reject MP before the matching stats snapshot arrives.
            if (GetQuickSlotBinding(this->selectedQuickSlot) == e.skill)
            {
                this->selectedQuickSlot = -1;
                this->aimingArea = false;
                if (IsValid(this->combatAimMarker))
                {
                    this->combatAimMarker->Release();
                }

            }

            this->suppressPrimaryUntilRelease = false;
            this->primaryAttackGesture = false;
            this->primaryManaFallback = true;
        }

        UE_LOG(LogTemp, Display, TEXT("COMBAT rejection reason=%u skill=%u"), e.reason, e.skill);
        this->onStatusChanged.Broadcast(reasons[FMath::Min(e.reason, 10U)]);
        return true;
    }

    if (e.phase == Phase::STARTED)
    {
        if (e.source == this->prepared.epoch)
        {
            this->ownCast = e.action;
            this->ownSkill = e.skill;
            this->attackRequestPending = false;
            this->ownCastEnd = double(e.endMs) / 1000;
            this->ownRecoveryEnd = this->ownCastEnd + (a ? a->recoveryMs / 1000. : 0);
            if (this->combatMoveOverride)
            {
                net::Message cancel;
                cancel.kind = net::Kind::COMBAT_CANCEL;
                cancel.epoch = this->prepared.epoch;
                cancel.request = this->prepared.request;
                cancel.sequence = ++this->combatSequence;
                cancel.combatEvent.action = this->ownCast;
                Send(cancel);
            }
            else
            {
                CancelClickMove();
            }

        }

        if (auto* pawn = find(e.source))
        {
            pawn->PlayCombat(e, a, CombatServerNow());
        }

        if (a && (a->shape == Shape::CIRCLE || a->shape == Shape::CONE || a->shape == Shape::LINE))
        {
            CombatEffect(e, 0);
        }
        else if (a && a->shape == Shape::PROJECTILE && (e.source & npc::ENTITY_BIT))
        {
            // NPC projectile aim is readable before launch. The boss commits this direction.
            Ability warning = *a;
            warning.shape = Shape::CONE;
            warning.radius = a->range;
            warning.angle = 8;
            auto telegraph = e;
            telegraph.point = e.origin;
            if (auto* effect = CombatEffect(telegraph, 0))
            {
                effect->Activate(telegraph, &warning, CombatServerNow(), true, 0);
            }

        }

    }

    if (e.phase == Phase::FIRED || e.phase == Phase::CANCELLED)
    {
        for (const auto& effect : this->combatEffects)
        {
            if (IsValid(effect) && effect->Active() && effect->Action() == e.action && effect->EffectKind() == 0)
            {
                effect->Release();
            }

        }

        if (e.source == this->prepared.epoch && this->ownCast == e.action)
        {
            this->ownCast = 0;
            this->attackRequestPending = false;
            this->ownCastEnd = 0;
            this->ownRecoveryEnd = e.phase == Phase::FIRED && a ? double(e.serverMs + a->recoveryMs) / 1000 : 0;
            if (e.phase == Phase::FIRED && a)
            {
                this->abilityReadyAt.Add(e.skill, double(e.serverMs + a->cooldownMs) / 1000);
            }

            if (e.phase == Phase::CANCELLED && this->intentSkill && this->intentSent)
            {
                const bool RETRY_MOVING_TARGET = this->intentTarget && this->intentRepeatKey &&
                                                 !this->combatMoveOverride && e.reason == unsigned(Reject::TARGET);
                if (RETRY_MOVING_TARGET)
                {
                    this->nextCombatSubmit = FPlatformTime::Seconds() + .2;
                    this->combatActionText = TEXT("Reapproaching enemy");
                }
                else
                {
                    this->intentSkill = 0;
                    this->intentRepeatKey = 0;
                }

            }

        }

        if (auto* pawn = find(e.source))
        {
            pawn->PlayCombat(e, a, CombatServerNow());
        }

        if (e.phase == Phase::FIRED && a &&
            (a->shape == Shape::CIRCLE || a->shape == Shape::CONE || a->shape == Shape::LINE))
        {
            CombatEffect(e, 2);
        }
        else if (e.phase == Phase::FIRED && a && (e.skill == 11 || e.skill == 12))
        {
            CombatEffect(e, 2);
        }

    }

    if (e.phase == Phase::PROJECTILE)
    {
        CombatEffect(e, 1);
    }

    if (e.phase == Phase::PROJECTILE_END)
    {
        for (const auto& effect : this->combatEffects)
        {
            if (IsValid(effect) && effect->Active() && effect->Action() == e.action && effect->EffectKind() == 1)
            {
                effect->Release();
            }

        }

        CombatEffect(e, 2);
    }

    if (e.phase == Phase::HIT)
    {
        if (this->combatLives.FindRef(e.target) == e.life)
        {
            if (auto* pawn = find(e.target))
            {
                pawn->PlayCombat(e, a, CombatServerNow());
            }

            if (e.value)
            {
                CombatEffect(e, 5);
            }

            CombatEffect(e, 3);
        }

    }

    if (e.phase == Phase::DEATH || e.phase == Phase::RESPAWN)
    {
        this->combatLives.Add(e.source, e.life);
        if (auto* pawn = find(e.source))
        {
            pawn->PlayCombat(e, a, CombatServerNow());
            if (e.phase == Phase::RESPAWN)
            {
                const FVector LOCATION = FVector(e.point.x, e.point.y, e.point.z) - FVector(GetWorld()->OriginLocation);
                pawn->SetActorLocation(LOCATION);
                this->remoteTargets.Remove(e.source);
                if (e.source == this->prepared.epoch)
                {
                    this->predicted = e.point;
                    CancelClickMove();
                    this->pendingInputs.clear();
                    this->pendingPositions.clear();
                }

            }

        }

        if (e.phase == Phase::DEATH && e.source == this->prepared.epoch)
        {
            this->ownCast = 0;
            CancelCombatIntent();
            CancelClickMove();
            this->onStatusChanged.Broadcast(TEXT("You died. Waiting for server respawn."));
        }

        if (e.phase == Phase::DEATH && e.source == this->intentTarget)
        {
            CancelCombatIntent();
            CancelClickMove();
        }

        if (e.phase == Phase::DEATH && e.source == this->combatTarget)
        {
            this->combatTarget = 0;
        }

        if (e.phase == Phase::RESPAWN && e.source == this->prepared.epoch)
        {
            CancelCombatIntent();
            this->attackRequestPending = false;
            this->ownRecoveryEnd = 0;
            this->ownCastEnd = 0;
        }

    }

    UE_LOG(LogTemp, Display,
           TEXT("COMBAT event phase=%u source=%llu target=%llu action=%llu skill=%u damage=%u life=%llu"),
           unsigned(e.phase), e.source, e.target, e.action, e.skill, e.value, e.life);
    return true;
}

bool UGameSessionSubsystem::RequestAttack(uint32 skill, FVector point, uint64 targetOverride)
{
    if (this->state != 5 || !this->map || !this->combatClock || this->currentNpc || this->exitWarping ||
        InventoryVisible() || this->dungeonMatch.room)
    {
        return false;
    }

    auto* own = this->players.Find(this->prepared.epoch);
    if (!own || !IsValid(*own) || (*own)->dead)
    {
        return false;
    }

    shared_world::net::Message m;
    m.kind = shared_world::net::Kind::COMBAT_REQUEST;
    m.epoch = this->prepared.epoch;
    m.request = this->prepared.request;
    m.sequence = ++this->combatSequence;
    m.combatEvent.skill = skill;
    m.combatEvent.target = targetOverride == UINT64_MAX ? this->combatTarget : targetOverride;
    m.combatEvent.life = m.combatEvent.target ? this->combatLives.FindRef(m.combatEvent.target) : 1;
    point += FVector(GetWorld()->OriginLocation);
    if (point.ContainsNaN() || FMath::Abs(point.X) > shared_world::COORDINATE_LIMIT ||
        FMath::Abs(point.Y) > shared_world::COORDINATE_LIMIT || FMath::Abs(point.Z) > shared_world::COORDINATE_LIMIT)
    {
        return false;
    }

    m.combatEvent.point = {FMath::RoundToInt32(point.X), FMath::RoundToInt32(point.Y), FMath::RoundToInt32(point.Z)};
    CancelClickMove();
    if (!Send(m))
    {
        return false;
    }

    this->activeAttackTarget = m.combatEvent.target ? m.combatEvent.target : PickCombatTarget();
    this->attackRequestPending = true;
    this->combatRequestDeadline = FPlatformTime::Seconds() + 2;
    this->ownSkill = skill;
    this->combatMoveOverride = false;
    return true;
}

