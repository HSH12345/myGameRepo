#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldCameraOcclusion.h"
#include "World/WorldCombatEffect.h"
#include "World/WorldControlHUD.h"
#include "GameClientWorld.h"
#include "SharedWorld/Dungeon/DungeonMaze.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// The subsystem uses a core ticker, which need not observe PlayerController's one-frame
// WasInputKeyJustPressed window. Track level transitions once per control frame instead.
void UGameSessionSubsystem::PollActionInput()
{
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!pc)
    {
        return;
    }

    const FKey keys[] = {EKeys::LeftMouseButton,
                         EKeys::RightMouseButton,
                         EKeys::One,
                         EKeys::Two,
                         EKeys::Q,
                         EKeys::Escape,
                         EKeys::Tab,
                         EKeys::F1,
                         EKeys::F2,
                         EKeys::CapsLock,
                         EKeys::Three,
                         EKeys::Four,
                         EKeys::K,
                         EKeys::Five,
                         EKeys::Six,
                         EKeys::Seven,
                         EKeys::Eight,
                         EKeys::Nine,
                         EKeys::Zero};
    uint32 next = 0;
    for (uint32 i = 0; i < UE_ARRAY_COUNT(keys); ++i)
    {
        if (pc->IsInputKeyDown(keys[i]))
        {
            next |= 1U << i;
        }

    }

    this->actionPressedBits = next & ~this->actionInputBits;
#if PLATFORM_ANDROID
    if (this->mobileTouch.tap)
    {
        this->actionPressedBits |= PRIMARY_INPUT;
    }

#endif
    this->actionReleasedBits = this->actionInputBits & ~next;
    this->actionInputBits = next;
}

bool UGameSessionSubsystem::WorldPointerPosition(const APlayerController& controller, FVector2D& position) const
{
#if PLATFORM_ANDROID
    if (this->mobileTouch.tap)
    {
        position = this->mobileTouch.position;
        return true;
    }

#endif
    float x = 0, y = 0;
    const bool HAS_MOUSE = controller.GetMousePosition(x, y);
    position = FVector2D(x, y);
    return HAS_MOUSE;
}

bool UGameSessionSubsystem::CursorGround(FVector& point) const
{
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    FHitResult hit;
    FVector start, direction;
    auto* own = this->players.FindRef(this->prepared.epoch).Get();
    FVector2D position;
    if (!pc || !own || !WorldPointerPosition(*pc, position) ||
        !pc->DeprojectScreenPositionToWorld(position.X, position.Y, start, direction) ||
        !own->cameraOcclusion->TraceGround(hit, start, start + direction * 50000))
    {
        return false;
    }

    point = hit.ImpactPoint;
    return true;
}

bool UGameSessionSubsystem::TracePointerScene(FHitResult& hit, const FVector& start, const FVector& end) const
{
    if (auto* own = this->players.FindRef(this->prepared.epoch).Get())
    {
        return own->cameraOcclusion->TraceVisibleScene(hit, start, end);
    }

    return GetWorld()->LineTraceSingleByChannel(hit, start, end, ECC_Pawn);
}

unsigned UGameSessionSubsystem::CombatRadius(uint64 entity) const
{
    if (!(entity & shared_world::npc::ENTITY_BIT))
    {
        return shared_world::combat::PLAYER_RADIUS;
    }

    const auto* pawn = this->npcs.FindRef(entity).Get();
    if (this->map && this->map->content->combat && pawn)
    {
        const auto IT = this->map->content->combat->monsters.find(pawn->npcIndex);
        if (IT != this->map->content->combat->monsters.end())
        {
            return IT->second.bodyRadius;
        }

    }

    return shared_world::combat::DEFAULT_NPC_RADIUS;
}

shared_world::WorldPosition UGameSessionSubsystem::PredictBodyMove(shared_world::WorldPosition from, int dx, int dy,
                                                                   const shared_world::WorldPosition* candidate) const
{
    using namespace shared_world;
    auto clear = [&](WorldPosition to)
    {
        if (!dungeon::DoorClear(from, to, this->map->activeRoom))
        {
            return false;
        }

        if (this->map->content->combat)
        {
            for (const auto& n : this->npcs)
            {
                if (IsValid(n.Value) && !n.Value->dead)
                {
                    const auto IT = this->map->content->combat->monsters.find(n.Value->npcIndex);
                    if (IT == this->map->content->combat->monsters.end() || !IT->second.solidRadius)
                    {
                        continue;
                    }

                    const FVector P = (this->remoteTargets.Contains(n.Key) ? this->remoteTargets[n.Key]
                                                                           : n.Value->GetActorLocation()) +
                                      FVector(GetWorld()->OriginLocation);
                    const WorldPosition BODY{FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y),
                                             FMath::RoundToInt32(P.Z)};
                    if (!combat::BodyMoveClear(from, to, BODY, IT->second.solidRadius + combat::PLAYER_RADIUS,
                                               IT->second.bodyHeight))
                    {
                        return false;
                    }

                }

            }

        }

        return true;
    };

    auto tryValue = [&](int x, int y)
    {
        const auto NEXT =
            candidate && x == dx && y == dy ? *candidate : this->map->residency->Move(from, x, y).position;
        return clear(NEXT) ? NEXT : from;
    };

    const auto FULL = tryValue(dx, dy);
    if (FULL != from || !dx || !dy)
    {
        return FULL;
    }

    const auto X_VALUE = tryValue(dx, 0), Y = tryValue(0, dy);
    return DistanceSquared2D(from, X_VALUE) >= DistanceSquared2D(from, Y) ? X_VALUE : Y;
}

uint64 UGameSessionSubsystem::PickCombatTarget() const
{
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    FVector2D cursor;
    if (!pc || !WorldPointerPosition(*pc, cursor))
    {
        return 0;
    }

    const FVector2D CURSOR = cursor;
    double best = 1;
    uint64 pick = 0;
    FVector rayStart, rayDirection;
    const bool RAY_READY = pc->DeprojectScreenPositionToWorld(CURSOR.X, CURSOR.Y, rayStart, rayDirection);
    double largeDistance = 2;
    uint64 largePick = 0;
    FHitResult terrain;
    const bool HAS_TERRAIN = RAY_READY && TracePointerScene(terrain, rayStart, rayStart + rayDirection * 50000);
    auto position = [&](FVector p)
    {
        p += FVector(GetWorld()->OriginLocation);
        return shared_world::WorldPosition{FMath::RoundToInt32(p.X), FMath::RoundToInt32(p.Y),
                                           FMath::RoundToInt32(p.Z)};
    };

    auto consider = [&](uint64 entity, AWorldPlayerPawn* pawn)
    {
        if (IsHostileEntity(entity))
        {
            if (CombatRadius(entity) > 100 && RAY_READY)
            {
                const auto& body = this->map->content->combat->monsters.at(pawn->npcIndex);
                const double T = shared_world::combat::SweepBodyTarget(
                    position(rayStart), position(rayStart + rayDirection * 50000), position(pawn->GetActorLocation()),
                    body.bodyRadius, body.bodyHeight, 0);
                if (T < largeDistance && T <= 1 && (!HAS_TERRAIN || T * 50000 <= terrain.Distance + 10))
                {
                    largePick = entity;
                    largeDistance = T;
                }

                return;
            }

            FVector2D feet, head;
            if (!pc->ProjectWorldLocationToScreen(pawn->GetActorLocation() + FVector(0, 0, 20), feet) ||
                !pc->ProjectWorldLocationToScreen(pawn->GetActorLocation() + FVector(0, 0, 165), head))
            {
                return;
            }

            const FVector2D AXIS = head - feet;
            const double T =
                FMath::Clamp(FVector2D::DotProduct(CURSOR - feet, AXIS) / FMath::Max(1., AXIS.SizeSquared()), 0., 1.);
            const double WIDTH = FMath::Clamp(AXIS.Size() * .3, 12., 36.);
            const double D = FVector2D::DistSquared(CURSOR, feet + AXIS * T) / (WIDTH * WIDTH);
            if (D < best)
            {
                best = D;
                pick = entity;
            }

        }

    };

    for (const auto& entry : this->npcs)
    {
        consider(entry.Key, entry.Value.Get());
    }

    for (const auto& entry : this->players)
    {
        consider(entry.Key, entry.Value.Get());
    }

    return largePick ? largePick : pick;
}

void UGameSessionSubsystem::CancelCombatIntent(bool cancelCast)
{
    if (cancelCast)
    {
        this->activeAttackTarget = 0;
        ClearAttackHighlight();
    }

    this->intentSkill = 0;
    this->intentTarget = 0;
    this->intentLife = 0;
    this->intentRepeatKey = 0;
    this->intentSent = false;
    this->aimingArea = false;
    this->primaryAttackGesture = false;
    this->suppressPrimaryUntilRelease = true;
    this->primaryManaFallback = false;
    this->combatActionText = TEXT("Ready");
    if (IsValid(this->combatAimMarker))
    {
        this->combatAimMarker->Release();
    }

    if (cancelCast && !this->combatMoveOverride && (this->ownCast || this->attackRequestPending))
    {
        shared_world::net::Message m;
        m.kind = shared_world::net::Kind::COMBAT_CANCEL;
        m.epoch = this->prepared.epoch;
        m.request = this->prepared.request;
        m.sequence = ++this->combatSequence;
        m.combatEvent.action = this->ownCast;
        Send(m);
    }

    if (cancelCast)
    {
        this->combatMoveOverride = true;
    }

}

void UGameSessionSubsystem::QueueCombatIntent(uint32 skill, uint64 target, FVector point, int32 repeatKey,
                                              bool stationary)
{
    if (!this->map || !this->map->content->combat)
    {
        return;
    }

    const auto FOUND = this->map->content->combat->abilities.find(skill);
    if (FOUND == this->map->content->combat->abilities.end())
    {
        return;
    }

    const auto* own = this->players.FindRef(this->prepared.epoch).Get();
    if (!own || own->dead)
    {
        return;
    }

    const double SERVER_NOW = CombatServerNow();
    if (this->abilityReadyAt.FindRef(skill) - SERVER_NOW > .3 && repeatKey == 0)
    {
        this->onStatusChanged.Broadcast(TEXT("Ability is cooling down."));
        return;
    }

    if (!CanAffordSkill(skill))
    {
        CancelUnaffordableSkill();
        return;
    }

    if ((this->ownCast || this->attackRequestPending || this->ownRecoveryEnd > SERVER_NOW) &&
        this->ownRecoveryEnd - SERVER_NOW > .3 && repeatKey == 0)
    {
        this->onStatusChanged.Broadcast(TEXT("Finish the current cast, or move / Esc to cancel."));
        return;
    }

    CancelClickMove();
    this->intentSkill = skill;
    this->intentTarget = target;
    this->intentLife = this->combatLives.FindRef(target);
    this->intentPoint = point;
    this->intentRepeatKey = repeatKey;
    this->intentStationary = stationary;
    this->intentSent = false;
    const double NOW = FPlatformTime::Seconds();
    this->intentExpires = NOW + (target && skill == this->basicAbility ? 10 : .4);
    this->nextApproachPlan = 0;
    this->approachLastFeet = this->predicted;
    this->approachProgressAt = NOW;
    if (target)
    {
        this->combatTarget = target;
    }

}

void UGameSessionSubsystem::TickCombatIntent()
{
    using namespace shared_world;
    using namespace combat;
    if (!this->intentSkill || !this->map->content->combat)
    {
        return;
    }

    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    auto* own = this->players.FindRef(this->prepared.epoch).Get();
    if (!pc || !own)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds(), SERVER_NOW = CombatServerNow();
    const bool HELD = this->intentRepeatKey == 1 && pc->IsInputKeyDown(EKeys::LeftMouseButton);
    if (HELD)
    {
        this->intentExpires = NOW + 10;
    }

    if (this->intentRepeatKey == 1 && !HELD && !(this->actionPressedBits & PRIMARY_INPUT))
    {
        CancelCombatIntent();
        CancelClickMove();
        return;
    }

    if ((this->intentSent && !HELD) || NOW > this->intentExpires)
    {
        this->intentSkill = 0;
        this->intentRepeatKey = 0;
        return;
    }

    if (this->attackRequestPending && NOW > this->combatRequestDeadline)
    {
        CancelCombatIntent();
        this->onStatusChanged.Broadcast(TEXT("Attack acknowledgement timed out. Choose the action again."));
        return;
    }

    if (this->ownCast || this->attackRequestPending || this->ownRecoveryEnd > SERVER_NOW ||
        NOW < this->nextCombatSubmit)
    {
        return;
    }

    const auto A = GetAbility(this->intentSkill);
    if (this->abilityReadyAt.FindRef(this->intentSkill) > SERVER_NOW)
    {
        this->combatActionText = TEXT("Waiting for cooldown");
        return;
    }

    if (!CanAffordSkill(this->intentSkill))
    {
        CancelUnaffordableSkill();
        return;
    }

    if (this->intentTarget)
    {
        auto* target = CombatPawn(this->intentTarget);
        if (!IsHostileEntity(this->intentTarget) || this->combatLives.FindRef(this->intentTarget) != this->intentLife)
        {
            CancelCombatIntent();
            CancelClickMove();
            this->combatTarget = 0;
            return;
        }

        const FVector FEET = target->GetActorLocation() + FVector(GetWorld()->OriginLocation);
        const WorldPosition GOAL{FMath::RoundToInt32(FEET.X), FMath::RoundToInt32(FEET.Y), FMath::RoundToInt32(FEET.Z)};
        const unsigned REACH =
            A.shape == Shape::MELEE
                ? MeleeCenterRange(A.range, PLAYER_RADIUS, CombatRadius(this->intentTarget))
                : A.range + std::max(0, int(CombatRadius(this->intentTarget)) - int(DEFAULT_NPC_RADIUS));
        if (!InRange(this->predicted, GOAL, this->intentStationary ? REACH : REACH > 30 ? REACH - 20 : REACH))
        {
            if (this->intentStationary)
            {
                CancelCombatIntent();
                this->onStatusChanged.Broadcast(TEXT("Target is out of range."));
                return;
            }

            if (this->predicted != this->approachLastFeet)
            {
                this->approachLastFeet = this->predicted;
                this->approachProgressAt = NOW;
            }

            if (NOW - this->approachProgressAt > 3)
            {
                CancelCombatIntent();
                CancelClickMove();
                this->onStatusChanged.Broadcast(TEXT("Cannot reach the enemy."));
                return;
            }

            if (NOW >= this->nextApproachPlan && (!this->clickPending && this->clickPath.empty() ||
                                                  DistanceSquared2D(this->approachLastTarget, GOAL) > 10000))
            {
                const FVector FROM(this->predicted.x, this->predicted.y, this->predicted.z);
                FVector destination = FEET - (FEET - FROM).GetSafeNormal2D() * FMath::Max(40.f, float(REACH) - 40);
                destination.Z = FEET.Z;
                if (!RequestMoveToInternal(destination - FVector(GetWorld()->OriginLocation), true))
                {
                    CancelCombatIntent();
                    this->onStatusChanged.Broadcast(TEXT("Enemy is too far away or unreachable."));
                    return;
                }

                this->approachLastTarget = GOAL;
                this->nextApproachPlan = NOW + .35;
            }

            this->combatActionText = TEXT("Approaching enemy");
            return;
        }

        this->intentPoint = target->GetActorLocation();
    }
    else if (HELD && !CursorGround(this->intentPoint))
    {
        return;
    }

    CancelClickMove();
    // Drain already sent moves before asking for a stationary cast. A late move would cancel it on the owner.
    if (!this->pendingInputs.empty())
    {
        this->combatActionText = TEXT("Stopping to attack");
        return;
    }

    if (RequestAttack(this->intentSkill, this->intentPoint, this->intentTarget))
    {
        this->intentSent = true;
        this->nextCombatSubmit = NOW + .12;
        this->combatActionText = TEXT("Requesting attack");
    }

}

void UGameSessionSubsystem::UpdateCombatFeedback()
{
    if (!this->map || !this->map->content->combat)
    {
        return;
    }

    auto* own = this->players.FindRef(this->prepared.epoch).Get();
    if (!own)
    {
        return;
    }

    const double NOW = CombatServerNow();
    FString targetText;
    const uint64 ATTACK_ID = this->intentTarget ? this->intentTarget
                             : (this->ownCast || this->attackRequestPending || this->ownRecoveryEnd > NOW)
                                 ? this->activeAttackTarget
                                 : 0;
    auto* attacking = CombatPawn(ATTACK_ID);
    if (own->dead || !IsHostileEntity(ATTACK_ID))
    {
        attacking = nullptr;
    }

    if (this->highlightedEnemy.Get() != attacking)
    {
        ClearAttackHighlight();
        this->highlightedEnemy = attacking;
        if (attacking)
        {
            attacking->SetCombatHighlighted(true);
        }

    }

    auto* target = CombatPawn(this->combatTarget);
    if (IsHostileEntity(this->combatTarget))
    {
        targetText = FString::Printf(TEXT("%s  HP %d / %d"), *target->GetDisplayName(), target->hp, target->maxHp);
        if (IsValid(this->combatTargetMarker))
        {
            this->combatTargetMarker->Release();
        }

    }
    else if (IsValid(this->combatTargetMarker))
    {
        this->combatTargetMarker->Release();
    }

    if (this->controlHud)
    {
        this->controlHud->ShowEnemy(targetText.IsEmpty() ? FString() : target->GetDisplayName(),
                                    targetText.IsEmpty() ? 0 : target->hp, targetText.IsEmpty() ? 0 : target->maxHp);
    }

    const auto& abilities = this->map->content->combat->abilities;
    FString action;
    float progress = 0;
    if (own->dead)
    {
        action = TEXT("Defeated");
    }
    else if (this->aimingArea)
    {
        action = TEXT("스킬 조준 · 좌클릭 사용 · 같은 슬롯 / Esc 해제");
    }
    else if (this->ownCast)
    {
        const double DURATION = abilities.count(this->ownSkill) ? abilities.at(this->ownSkill).castMs / 1000. : 1;
        progress = FMath::Clamp(float(1 - (this->ownCastEnd - NOW) / FMath::Max(.001, DURATION)), 0.f, 1.f);
        action = FString::Printf(
            TEXT("Casting %s  %.1fs"),
            abilities.count(this->ownSkill) ? UTF8_TO_TCHAR(abilities.at(this->ownSkill).name.c_str()) : TEXT(""),
            FMath::Max(0., this->ownCastEnd - NOW));
    }
    else if (this->attackRequestPending)
    {
        action = TEXT("Requesting attack");
    }
    else if (this->ownRecoveryEnd > NOW)
    {
        action = TEXT("Recovering");
    }
    else if (this->intentSkill)
    {
        action = this->combatActionText;
    }
    else if (this->clickPending || !this->clickPath.empty())
    {
        action = this->walkMode ? TEXT("Walking") : TEXT("Running");
    }
    else if (this->selectedQuickSlot >= 0)
    {
        action = FString::Printf(TEXT("%d번 슬롯 선택 · 좌클릭 사용 · 다시 누르면 해제"),
                                 (this->selectedQuickSlot + 1) % 10);
    }
    else
    {
        action = TEXT("Ready");
    }

    if (this->controlHud)
    {
        this->controlHud->ShowCombat(targetText, action, FString(), progress);
    }

}

bool UGameSessionSubsystem::TickCombatControl(float)
{
    using namespace shared_world;
    using namespace combat;
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!pc || !this->map || !this->map->content->combat)
    {
        return false;
    }

    const FVector CAMERA = pc->PlayerCameraManager ? pc->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    for (const auto& effect : this->combatEffects)
    {
        if (IsValid(effect))
        {
            effect->Update(CombatServerNow(), CAMERA);
        }

    }

    if (this->attackRequestPending && FPlatformTime::Seconds() > this->combatRequestDeadline)
    {
        this->attackRequestPending = false;
        CancelCombatIntent();
        this->onStatusChanged.Broadcast(TEXT("Attack acknowledgement timed out. Choose the action again."));
    }

    if (this->currentNpc || this->exitWarping || InventoryVisible() || this->shopOpening)
    {
        if (this->intentSkill || this->aimingArea || this->ownCast || this->attackRequestPending)
        {
            CancelCombatIntent(true);
        }

        CancelClickMove();
        UpdateCombatFeedback();
        return false;
    }

    auto* own = this->players.FindRef(this->prepared.epoch).Get();
    if (!IsValid(own))
    {
        return false;
    }

    if (own->dead)
    {
        this->selectedQuickSlot = -1;
        ClearAttackHighlight();
        UpdateCombatFeedback();
        return true;
    }

    CancelUnaffordableSkill();
    const double NOW = FPlatformTime::Seconds();
    const bool POINTER_BLOCKED = this->controlHud && this->controlHud->IsPointerOverHud();
    const bool LEFT_DOWN = !POINTER_BLOCKED && pc->IsInputKeyDown(EKeys::LeftMouseButton),
               LEFT_PRESS = !POINTER_BLOCKED && ((this->actionPressedBits & PRIMARY_INPUT) != 0);
    if (!LEFT_DOWN && !LEFT_PRESS)
    {
        this->suppressPrimaryUntilRelease = false;
        this->primaryAttackGesture = false;
        this->primaryManaFallback = false;
    }

    if (LEFT_PRESS)
    {
        this->suppressPrimaryUntilRelease = false;
        this->primaryManaFallback = false;
    } // A new press is a new movement/attack decision.

    const uint32 selectBits[] = {BOLT_INPUT,   AREA_INPUT,   POTION_INPUT, MANA_POTION_INPUT, SLOT_5_INPUT,
                                 SLOT_6_INPUT, SLOT_7_INPUT, SLOT_8_INPUT, SLOT_9_INPUT,      SLOT_0_INPUT};
    for (int32 i = 0; i < 10; ++i)
    {
        if (this->actionPressedBits & selectBits[i])
        {
            ActivateQuickSlot(i, true);
        }

    }

    if ((this->actionPressedBits & SKILL_BOOK_INPUT) && this->controlHud)
    {
        this->controlHud->ToggleQuickSlotPicker(this->selectedQuickSlot >= 0 ? this->selectedQuickSlot : 0);
    }

    if (this->controlHud && this->controlHud->IsSkillBookVisible())
    {
        if (this->actionPressedBits & CANCEL_INPUT)
        {
            this->controlHud->HideQuickSlotPicker();
        }

        CancelCombatIntent(true);
        CancelClickMove();
        return true;
    }

    if (((this->actionPressedBits & CANCEL_INPUT) != 0))
    {
        this->selectedQuickSlot = -1;
        if (this->controlHud)
        {
            this->controlHud->HideQuickSlotPicker();
        }

        CancelCombatIntent(true);
        CancelClickMove();
        UpdateCombatFeedback();
        return true;
    }

    if (this->intentRepeatKey == 1 && !LEFT_DOWN && !LEFT_PRESS)
    {
        CancelCombatIntent();
        CancelClickMove();
    }

    if (((this->actionPressedBits & MELEE_MODE_INPUT) != 0))
    {
        this->basicAbility = 1;
        CancelCombatIntent();
        this->onStatusChanged.Broadcast(TEXT("Melee basic selected."));
    }

    if (((this->actionPressedBits & RANGED_MODE_INPUT) != 0))
    {
        this->basicAbility = 2;
        CancelCombatIntent();
        this->onStatusChanged.Broadcast(TEXT("Ranged basic selected."));
    }

    const bool KEYBOARD_MOVE = pc->IsInputKeyDown(EKeys::W) || pc->IsInputKeyDown(EKeys::A) ||
                               pc->IsInputKeyDown(EKeys::S) || pc->IsInputKeyDown(EKeys::D);
    if (KEYBOARD_MOVE)
    {
        if (this->intentSkill || this->aimingArea || this->ownCast || this->attackRequestPending)
        {
            CancelCombatIntent(true);
        }

        UpdateCombatFeedback();
        return false;
    }

    if (((this->actionPressedBits & TARGET_INPUT) != 0))
    {
        TArray<uint64> candidates;
        for (const auto& n : this->npcs)
        {
            if (IsValid(n.Value) && n.Value->attackable && !n.Value->dead)
            {
                candidates.Add(n.Key);
            }

        }

        for (const auto& entry : this->players)
        {
            if (IsHostileEntity(entry.Key))
            {
                candidates.Add(entry.Key);
            }

        }

        candidates.Sort();
        if (candidates.Num())
        {
            this->combatTarget = candidates[(candidates.Find(this->combatTarget) + 1) % candidates.Num()];
        }

    }

    FVector point;
    const bool GROUND = CursorGround(point);
    const uint32 BINDING = GetQuickSlotBinding(this->selectedQuickSlot);
    const auto* skillCatalog = GetSkillCatalog();
    const auto* definition =
        skillCatalog && skillCatalog->definitions.count(BINDING) ? &skillCatalog->definitions.at(BINDING) : nullptr;
    const bool HAS_SKILL = definition && GetSkillRank(BINDING);
    this->aimingArea = HAS_SKILL && definition->aim != skills::Aim::TARGET;
    if (this->aimingArea && (GROUND || definition->aim == skills::Aim::SELF) && !LEFT_DOWN && !this->ownCast)
    {
        const auto ABILITY = GetAbility(BINDING);
        const auto AIM = definition->aim;
        const FVector DESTINATION = AIM == skills::Aim::SELF ? own->GetActorLocation() : point;
        if (!IsValid(this->combatAimMarker))
        {
            this->combatAimMarker = GetWorld()->SpawnActor<AWorldCombatEffect>();
        }

        const bool VALID =
            this->abilityReadyAt.FindRef(BINDING) <= CombatServerNow() && own->mp >= int32(ABILITY.mp) &&
            (AIM != skills::Aim::GROUND ||
             FVector::DistSquared2D(DESTINATION, own->GetActorLocation()) <= double(ABILITY.range) * ABILITY.range);
        if (this->combatAimMarker)
        {
            this->combatAimMarker->ShowAbilityMarker(own->GetActorLocation(), DESTINATION, ABILITY, AIM, VALID);
        }

    }
    else if (IsValid(this->combatAimMarker))
    {
        this->combatAimMarker->Release();
    }

    if (LEFT_PRESS && !POINTER_BLOCKED && HAS_SKILL)
    {
        // Consume the complete primary gesture: a selected skill must never also
        // move, perform a basic attack, or start one while the button stays held.
        this->suppressPrimaryUntilRelease = true;
        const auto AIM = definition->aim;
        if (AIM == skills::Aim::TARGET)
        {
            uint64 target = PickCombatTarget();
            if (!target)
            {
                target = this->combatTarget;
            }

            auto* enemy = CombatPawn(target);
            if (IsHostileEntity(target))
            {
                QueueCombatIntent(BINDING, target, enemy->GetActorLocation(), 0, true);
            }

        }
        else if (GROUND || AIM == skills::Aim::SELF)
        {
            QueueCombatIntent(BINDING, 0, AIM == skills::Aim::SELF ? own->GetActorLocation() : point, 0, true);
            this->selectedQuickSlot = -1;
            this->aimingArea = false;
        }

    }

    if (!this->suppressPrimaryUntilRelease &&
        (LEFT_PRESS || LEFT_DOWN && !this->primaryAttackGesture && NOW >= this->nextPointerRefresh))
    {
        const bool STATIONARY = pc->IsInputKeyDown(EKeys::LeftShift) || pc->IsInputKeyDown(EKeys::RightShift);
        const uint64 PICK = PickCombatTarget();
        if (PICK)
        {
            QueueCombatIntent(this->basicAbility, PICK, GROUND ? point : CombatPawn(PICK)->GetActorLocation(),
                              LEFT_DOWN ? 1 : 0,
                              STATIONARY);
            this->primaryAttackGesture = true;
        }
        else if (!this->primaryManaFallback && GROUND &&
                 (LEFT_PRESS ||
                  FVector::DistSquared2D(point, FVector(this->clickGoal.x, this->clickGoal.y, this->clickGoal.z) -
                                                    FVector(GetWorld()->OriginLocation)) > 10000))
        {
            if (RequestMoveTo(point))
            {
                this->suppressPrimaryUntilRelease = false;
                UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL cursor_destination x=%d y=%d"), this->clickGoal.x,
                       this->clickGoal.y);
            }

        }

        this->nextPointerRefresh = NOW + .15;
    }

    TickCombatIntent();
    UpdateCombatFeedback();
    return false;
}

