#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "World/WorldPlayerPawn.h"
#include "World/WorldControlHUD.h"
#include "World/WorldCombatEffect.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void UGameSessionSubsystem::TickSkillSmoke()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgActionBarRestore")) ||
        (FParse::Param(FCommandLine::Get(), TEXT("RpgActionBarSmoke")) && this->controlSmokeStage >= 61 &&
         this->controlSmokeStage < 100))
    {
        TickActionBarSmoke();
        return;
    }

    if (this->state != 5 || !this->controlHud || !this->economyReady)
    {
        return;
    }

    auto* own = GetInventoryCharacter();
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!own || !pc)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto next = [&](int32 stage)
    {
        this->controlSmokeStage = stage;
        this->controlSmokeTime = NOW;
        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE stage=%d selected=%d mp=%d"), stage, this->selectedQuickSlot,
               own->mp);
    };

    auto checkResult = [&](bool pass, const TCHAR* what)
    {
        if (!pass)
        {
            UE_LOG(LogTemp, Error, TEXT("SKILL_SMOKE failed=%s stage=%d hp=%d mp=%d"), what, this->controlSmokeStage,
                   own->hp, own->mp);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return pass;
    };

    auto key = [&](FKey k, bool down)
    {
        pc->InputKey(FInputKeyEventArgs::CreateSimulated(k, down ? IE_Pressed : IE_Released, down ? 1.f : 0.f));
    };

    auto cursor = [&](FVector point)
    {
        FVector2D screen;
        if (pc->ProjectWorldLocationToScreen(point, screen))
        {
            pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        }

    };

    auto shot = [&](const TCHAR* name)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        if (!file.IsEmpty())
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(file), FString(name) + TEXT(".png")),
                                                  true, false);
        }

    };

    auto count = [&](uint32 type)
    {
        uint32 value = 0;
        for (const auto& item : this->economyState.items)
        {
            if (item.type == type)
            {
                value += item.quantity;
            }

        }

        return value;
    };

    static int32 learn = 10, castIndex = 0;
    static const uint32 casts[] = {11, 10, 12, 13, 14, 15, 16, 17};
    if (this->controlSmokeStage && NOW - this->controlSmokeTime > 20)
    {
        FVector aim;
        UE_LOG(LogTemp, Error,
               TEXT("SKILL_SMOKE timeout_context selected=%d ground=%d pointer_ui=%d camera_drag=%d intent=%u"),
               this->selectedQuickSlot, CursorGround(aim), this->controlHud->IsPointerOverHud(), this->rightCameraDrag,
               this->intentSkill);
        checkResult(false, TEXT("stage timeout"));
        return;
    }

    if (own->dead)
    {
        checkResult(false, TEXT("unexpected death"));
        return;
    }

    if (this->controlSmokeStage == 0)
    {
        if (!checkResult(this->quickSlotBindings[2] == 0 && this->quickSlotBindings[3] == 0,
                         TEXT("potion-free skill slots")))
        {
            return;
        }

        next(1);
    }
    else if (this->controlSmokeStage == 1 && NOW - this->controlSmokeTime > .3)
    {
        if (this->controlHud->ClickPotionRegisterForTest(0, 1201))
        {
            UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE hp_registered"));
            next(2);
        }

    }
    else if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > .3)
    {
        if (this->controlHud->ClickPotionRegisterForTest(1, 1202))
        {
            shot(TEXT("auto-potions"));
            UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE mp_registered"));
            this->combatTarget = shared_world::npc::ENTITY_BIT | 103;
            key(EKeys::One, true);
            next(3);
        }

    }
    else if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::One, false);
        next(4);
    }
    else if (this->controlSmokeStage == 4 && this->abilityReadyAt.FindRef(3) > CombatServerNow())
    {
        if (!checkResult(this->selectedQuickSlot == -1, TEXT("target hotkey casts directly")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE target_hotkey_fired"));
        next(5);
    }
    else if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > .5)
    {
        key(EKeys::K, true);
        next(6);
    }
    else if (this->controlSmokeStage == 6 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::K, false);
        next(7);
    }
    else if (this->controlSmokeStage == 7 && this->controlHud->IsSkillBookVisible() &&
             NOW - this->controlSmokeTime > .2)
    {
        if (learn <= 17)
        {
            if (GetSkillRank(learn))
            {
                ++learn;
                this->controlSmokeTime = NOW;
                return;
            }

            if (!EconomyBusy())
            {
                this->controlHud->ClickTrainForTest(learn);
            }

            this->controlSmokeTime = NOW;
            return;
        }

        if (GetSkillRank(11) == 1)
        {
            if (!EconomyBusy())
            {
                this->controlHud->ClickTrainForTest(11);
            }

            this->controlSmokeTime = NOW;
            return;
        }

        if (!checkResult(GetSkillRank(11) == 2 && this->economyState.skills.points == 3,
                         TEXT("documented learn/upgrade costs")))
        {
            return;
        }

        shot(TEXT("skill-book"));
        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE learned_upgraded points=%u"), this->economyState.skills.points);
        next(8);
    }
    else if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(this->controlHud->ClickBindingForTest(10) && this->controlHud->ClickQuickSlotForTest(2) &&
                             this->quickSlotBindings[2] == 10,
                         TEXT("book then slot registration")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE book_slot_binding"));
        key(EKeys::K, true);
        next(9);
    }
    else if (this->controlSmokeStage == 9 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::K, false);
        next(10);
    }
    else if (this->controlSmokeStage == 10 && NOW - this->controlSmokeTime > .2)
    {
        cursor(own->GetActorLocation() + FVector(500, 0, 0));
        key(EKeys::Two, true);
        next(11);
    }
    else if (this->controlSmokeStage == 11 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::Two, false);
        next(12);
    }
    else if (this->controlSmokeStage == 12 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(this->selectedQuickSlot == 1 && IsValid(this->combatAimMarker) &&
                             this->combatAimMarker->Active() && !this->ownCast,
                         TEXT("number arms ground area without casting")))
        {
            return;
        }

        key(EKeys::RightMouseButton, true);
        next(120);
    }
    else if (this->controlSmokeStage == 120 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::RightMouseButton, false);
        next(121);
    }
    else if (this->controlSmokeStage == 121 && NOW - this->controlSmokeTime > .2)
    {
        if (!checkResult(this->selectedQuickSlot == 1 && !this->ownCast && !this->abilityReadyAt.FindRef(4),
                         TEXT("right click cannot activate selected skill")))
        {
            return;
        }

        cursor(own->GetActorLocation() + FVector(500, 0, 0));
        next(122);
    }
    else if (this->controlSmokeStage == 122 && NOW - this->controlSmokeTime > .2)
    {
        shot(TEXT("area-aim"));
        key(EKeys::LeftMouseButton, true);
        next(13);
    }
    else if (this->controlSmokeStage == 13 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::LeftMouseButton, false);
        next(14);
    }
    else if (this->controlSmokeStage == 14 && this->abilityReadyAt.FindRef(4) > CombatServerNow())
    {
        if (!checkResult(!this->clickPending && this->clickPath.empty() && this->pendingInputs.empty() &&
                             this->predicted == shared_world::WorldPosition{-3000, -3000, 0},
                         TEXT("skill left click never submits movement/basic")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE ground_left_fired"));
        next(40);
    }
    else if (this->controlSmokeStage == 40 && NOW - this->controlSmokeTime > .5)
    {
        if (castIndex >= UE_ARRAY_COUNT(casts))
        {
            next(60);
            return;
        }

        this->controlHud->OpenQuickSlotPickerForTest(0);
        next(41);
    }
    else if (this->controlSmokeStage == 41 && NOW - this->controlSmokeTime > .25)
    {
        const uint32 SKILL = casts[castIndex];
        if (!checkResult(this->controlHud->ClickBindingForTest(SKILL) && this->controlHud->ClickQuickSlotForTest(2) &&
                             this->quickSlotBindings[2] == SKILL,
                         TEXT("register each learned type")))
        {
            return;
        }

        this->controlHud->HideQuickSlotPicker();
        this->combatTarget = shared_world::npc::ENTITY_BIT | 101;
        cursor(own->GetActorLocation() + FVector(550, 0, 0));
        next(42);
    }
    else if (this->controlSmokeStage == 42 && NOW - this->controlSmokeTime > .2)
    {
        if (EconomyBusy() || own->mp < int32(GetAbility(casts[castIndex]).mp) || this->potionHudReadyAt > NOW)
        {
            return;
        }

        key(EKeys::Three, true);
        next(43);
    }
    else if (this->controlSmokeStage == 43 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::Three, false);
        next(GetSkillCatalog()->definitions.at(casts[castIndex]).aim == shared_world::skills::Aim::TARGET ? 46 : 44);
    }
    else if (this->controlSmokeStage == 44 && NOW - this->controlSmokeTime > .25)
    {
        if (!checkResult(this->selectedQuickSlot == 2, TEXT("non-target hotkey arms only")))
        {
            return;
        }

        if (casts[castIndex] == 16)
        {
            shot(TEXT("line-aim"));
        }

        key(EKeys::LeftMouseButton, true);
        next(45);
    }
    else if (this->controlSmokeStage == 45 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::LeftMouseButton, false);
        next(46);
    }
    else if (this->controlSmokeStage == 46 && this->abilityReadyAt.FindRef(casts[castIndex]) > CombatServerNow())
    {
        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE type_fired id=%u feet=%d,%d,%d"), casts[castIndex],
               this->predicted.x, this->predicted.y, this->predicted.z);
        ++castIndex;
        next(40);
    }
    else if (this->controlSmokeStage == 60 && NOW - this->controlSmokeTime > 3)
    {
        if (!checkResult(count(1201) <= 8 && count(1202) < 10 && own->hp * 2 > own->maxHp &&
                             this->economyState.skills.ranks[11] == 2,
                         TEXT("authoritative automatic HP/MP consumption")))
        {
            return;
        }

        shot(TEXT("skills-final"));
        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE auto_potions hp=%d mp=%d hp_count=%u mp_count=%u"), own->hp, own->mp,
               count(1201), count(1202));
        next(FParse::Param(FCommandLine::Get(), TEXT("RpgSkillManaSmoke")) ? 130 : 61);
    }
    else if (this->controlSmokeStage == 130 && !EconomyBusy())
    {
        if (RegisterPotionType(1, 0))
        {
            next(131);
        }

    }
    else if (this->controlSmokeStage == 131 && !EconomyBusy() && !this->ownCast && !this->attackRequestPending &&
             this->ownRecoveryEnd <= CombatServerNow() && this->abilityReadyAt.FindRef(3) <= CombatServerNow())
    {
        if (!checkResult(own->mp >= 8, TEXT("mana fallback starts with an affordable cast")))
        {
            return;
        }

        this->combatTarget = shared_world::npc::ENTITY_BIT | 101;
        if (own->mp >= 16)
        {
            key(EKeys::One, true);
            next(132);
        }
        else
        {
            if (!checkResult(this->controlHud->ClickQuickSlotForTest(0) && this->selectedQuickSlot == 0,
                             TEXT("select last affordable target skill")))
            {
                return;
            }

            cursor(this->npcs.FindRef(this->combatTarget)->GetActorLocation() + FVector(0, 0, 80));
            next(133);
        }

    }
    else if (this->controlSmokeStage == 132 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::One, false);
        next(131);
    }
    else if (this->controlSmokeStage == 133 && NOW - this->controlSmokeTime > .25)
    {
        key(EKeys::LeftMouseButton, true);
        next(134);
    }
    else if (this->controlSmokeStage == 134 && own->mp < 8 &&
             this->abilityReadyAt.FindRef(this->basicAbility) > CombatServerNow())
    {
        if (!checkResult(this->selectedQuickSlot == -1 && !this->aimingArea && !this->clickPending &&
                             this->clickPath.empty(),
                         TEXT("MP depletion clears skill and held click fires basic")))
        {
            return;
        }

        key(EKeys::LeftMouseButton, false);
        shot(TEXT("mana-basic-fallback"));
        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE mana_basic_fallback mp=%d basic=%u"), own->mp, this->basicAbility);
        next(135);
    }
    else if (this->controlSmokeStage == 135 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(!this->controlHud->ClickQuickSlotForTest(0),
                         TEXT("insufficient MP disables actual Slate slot")))
        {
            return;
        }

        key(EKeys::One, true);
        next(136);
    }
    else if (this->controlSmokeStage == 136 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::One, false);
        if (!checkResult(this->selectedQuickSlot == -1 && !this->intentSkill && !this->ownCast,
                         TEXT("insufficient MP hotkey cannot select or cast")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE mana_selection_blocked"));
        next(61);
    }
    else if (this->controlSmokeStage == 61 && NOW - this->controlSmokeTime > 1)
    {
        UE_LOG(LogTemp, Display, TEXT("SKILL_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

void UGameSessionSubsystem::TickActionBarSmoke()
{
    if (this->state != 5 || !this->controlHud || !this->economyReady)
    {
        return;
    }

    auto* own = GetInventoryCharacter();
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!own || !pc)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    const bool RESTORE = FParse::Param(FCommandLine::Get(), TEXT("RpgActionBarRestore"));
    static const uint32 layout[] = {3, 4, 10, 11, 12, 13, 14, 15, 16, 17};
    static int32 slot = 0;
    auto checkResult = [&](bool pass, const TCHAR* what)
    {
        if (!pass)
        {
            UE_LOG(LogTemp, Error, TEXT("ACTION_BAR_SMOKE failed=%s stage=%d"), what, this->controlSmokeStage);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return pass;
    };

    auto key = [&](FKey k, bool down)
    {
        pc->InputKey(FInputKeyEventArgs::CreateSimulated(k, down ? IE_Pressed : IE_Released, down ? 1.f : 0.f));
    };

    auto next = [&](int32 stage)
    {
        this->controlSmokeStage = stage;
        this->controlSmokeTime = NOW;
    };

    auto matches = [&]()
    {
        for (int32 i = 0; i < 10; ++i)
        {
            if (this->quickSlotBindings[i] != layout[i] || this->economyState.quickSlots[i] != layout[i])
            {
                return false;
            }

        }

        return this->registeredPotions[0] == 1201 && this->registeredPotions[1] == 1202 &&
               this->economyState.potions[0] == 1201 && this->economyState.potions[1] == 1202;
    };

    auto shot = [&](const TCHAR* name)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        if (!file.IsEmpty())
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(file), FString(name) + TEXT(".png")),
                                                  true, false);
        }

    };

    if (this->controlSmokeStage && NOW - this->controlSmokeTime > 20)
    {
        checkResult(false, TEXT("timeout"));
        return;
    }

    if (RESTORE && this->controlSmokeStage == 0)
    {
        if (!checkResult(matches() && this->controlHud->QuickGridForTest(),
                         TEXT("fresh process DB restore and 5x2 layout")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ACTION_BAR_SMOKE restored"));
        key(EKeys::Six, true);
        next(80);
    }
    else if (this->controlSmokeStage == 61)
    {
        if (EconomyBusy())
        {
            return;
        }

        this->controlHud->OpenQuickSlotPickerForTest(0);
        next(70);
    }
    else if (this->controlSmokeStage == 70 && NOW - this->controlSmokeTime > .25 && !EconomyBusy())
    {
        if (slot == 10)
        {
            this->controlHud->HideQuickSlotPicker();
            next(71);
            return;
        }

        if (!checkResult(this->controlHud->ClickBindingForTest(layout[slot]) &&
                             this->controlHud->ClickQuickSlotForTest(slot) &&
                             this->quickSlotBindings[slot] == layout[slot],
                         TEXT("bind all ten slots through UI")))
        {
            return;
        }

        ++slot;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 71 && NOW - this->controlSmokeTime > .3 && !EconomyBusy())
    {
        if (!checkResult(matches() && this->controlHud->QuickGridForTest(), TEXT("durable ten-slot layout")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ACTION_BAR_SMOKE saved"));
        key(EKeys::Zero, true);
        next(81);
    }
    else if (this->controlSmokeStage == 80 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::Six, false);
        if (!checkResult(this->selectedQuickSlot == 5, TEXT("key6 selects lower row")))
        {
            return;
        }

        key(EKeys::Zero, true);
        next(81);
    }
    else if (this->controlSmokeStage == 81 && NOW - this->controlSmokeTime > .12)
    {
        key(EKeys::Zero, false);
        if (!checkResult(this->selectedQuickSlot == 9, TEXT("key0 selects slot10")))
        {
            return;
        }

        shot(RESTORE ? TEXT("action-bar-restored") : TEXT("action-bar-layout"));
        next(82);
    }
    else if (this->controlSmokeStage == 82 && NOW - this->controlSmokeTime > 1)
    {
        UE_LOG(LogTemp, Display, TEXT("ACTION_BAR_SMOKE passed restore=%d"), RESTORE);
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
