#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "GameClientWorld.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldControlHUD.h"
#include "World/WorldInventoryUI.h"
#include "Components/MeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

void UGameSessionSubsystem::TickQuickSlotSmoke()
{
    if (this->state != 5 || !this->map)
    {
        return;
    }

    auto* own = this->players.FindRef(this->prepared.epoch).Get();
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!own || !pc || !own->AppearanceReady())
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto next = [&](int32 stage)
    {
        this->controlSmokeStage = stage;
        this->controlSmokeTime = NOW;
    };

    auto checkResult = [&](bool pass, const TCHAR* name)
    {
        if (!pass)
        {
            UE_LOG(LogTemp, Error, TEXT("QUICK_SMOKE failed=%s stage=%d selected=%d mp=%d intent=%u cast=%llu"), name,
                   this->controlSmokeStage, this->selectedQuickSlot, own->mp, this->intentSkill, this->ownCast);
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
        if (!pc->ProjectWorldLocationToScreen(point, screen))
        {
            return false;
        }

        pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        return true;
    };

    auto shot = [&](const TCHAR* name)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(file), FPaths::GetBaseFilename(file) + name), true, false);
    };

    static bool warmed = false;
    static uint64 sequence = 0;
    static int32 releasedHp = 0;
    static float yaw = 0;
    if (this->controlSmokeStage == 0)
    {
        if (UsePortal())
        {
            next(1);
        }

        return;
    }

    if (this->prepared.map.id != 301)
    {
        return;
    }

    if (this->controlSmokeStage == 1)
    {
        own->arm->TargetArmLength = 2000;
        own->arm->SetWorldRotation(FRotator(-55, 0, 0));
        if (RequestMoveTo(FVector(-450, -8400, 0)))
        {
            next(2);
        }

        return;
    }

    AWorldPlayerPawn* boss = nullptr;
    for (const auto& entry : this->npcs)
    {
        if (entry.Value->npcIndex == 2201)
        {
            boss = entry.Value;
        }

    }

    if (!boss || !boss->AppearanceReady() || !boss->CombatAssetsReady())
    {
        return;
    }

    if (this->controlSmokeStage == 2 && this->map->activeRoom == 6)
    {
        if (RequestMoveTo(FVector(1280, -8400, 0)))
        {
            next(3);
        }

        return;
    }

    if (this->controlSmokeStage == 3 && this->predicted.x == 1280 && this->pendingInputs.empty() &&
        NOW - this->controlSmokeTime > 1)
    {
        if (!warmed)
        {
            boss->SetCombatHighlighted(true);
            warmed = true;
            this->controlSmokeTime = NOW;
            return;
        }

#if WITH_EDITOR
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return;
        }

#endif
        boss->SetCombatHighlighted(false);
        if (!checkResult(this->controlHud && this->controlHud->QuickIconsInsideForTest() &&
                             GetQuickSlotStates().Num() == 4 && !BindQuickSlot(0, 1),
                         TEXT("four slots/no basic/layout")))
        {
            return;
        }

        sequence = this->combatSequence;
        if (!checkResult(this->controlHud->ClickQuickSlotForTest(0), TEXT("slot click")))
        {
            return;
        }

        next(4);
        return;
    }

    if (this->controlSmokeStage == 4 && NOW - this->controlSmokeTime > .25)
    {
        if (!checkResult(this->controlHud->QuickSelectionCoversSlotForTest(0), TEXT("selection covers whole slot")))
        {
            return;
        }

        if (!checkResult(this->selectedQuickSlot == 0 && own->mp == 50 && !this->ownCast &&
                             this->combatSequence == sequence,
                         TEXT("selection never casts")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE select_only"));
        shot(TEXT("-selected.png"));
        next(40);
        return;
    }

    if (this->controlSmokeStage == 40 && NOW - this->controlSmokeTime > .2)
    {
        if (!checkResult(this->controlHud->ClickQuickSlotForTest(0) && this->selectedQuickSlot == -1,
                         TEXT("click deselect")))
        {
            return;
        }

        next(5);
        return;
    }

    if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::One, true);
        next(6);
        return;
    }

    if (this->controlSmokeStage == 6 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::One, false);
        if (!checkResult(this->selectedQuickSlot == 0, TEXT("number selects")))
        {
            return;
        }

        next(7);
        return;
    }

    if (this->controlSmokeStage == 7 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::One, true);
        next(8);
        return;
    }

    if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::One, false);
        if (!checkResult(this->selectedQuickSlot == -1 && own->mp == 50 && this->combatSequence == sequence,
                         TEXT("number deselects")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE click_number_toggle"));
        cursor(boss->GetActorLocation());
        next(9);
        return;
    }

    if (this->controlSmokeStage == 9 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::RightMouseButton, true);
        next(10);
        return;
    }

    if (this->controlSmokeStage == 10 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::RightMouseButton, false);
        next(11);
        return;
    }

    if (this->controlSmokeStage == 11 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(this->combatSequence == sequence && own->mp == 50, TEXT("unselected right click no cast")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE empty_right_click"));
        if (!checkResult(this->controlHud->OpenQuickSlotPickerForTest(0), TEXT("open registration")))
        {
            return;
        }

        next(12);
        return;
    }

    if (this->controlSmokeStage == 12 && NOW - this->controlSmokeTime > .25)
    {
        shot(TEXT("-registration.png"));
        next(13);
        return;
    }

    if (this->controlSmokeStage == 13 && NOW - this->controlSmokeTime > .2)
    {
        if (!checkResult(this->controlHud->ClickBindingForTest(4) && GetQuickSlotBinding(0) == 4,
                         TEXT("register burst")))
        {
            return;
        }

        this->controlHud->ClickQuickSlotForTest(0);
        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE registration"));
        cursor(boss->GetActorLocation());
        yaw = own->CameraYaw();
        next(14);
        return;
    }

    if (this->controlSmokeStage == 14 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::RightMouseButton, true);
        next(15);
        return;
    }

    if (this->controlSmokeStage == 15 && NOW - this->controlSmokeTime < .4)
    {
        pc->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 10.f));
        return;
    }

    if (this->controlSmokeStage == 15)
    {
        key(EKeys::RightMouseButton, false);
        next(16);
        return;
    }

    if (this->controlSmokeStage == 16 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(own->mp == 50 && this->combatSequence == sequence &&
                             FMath::Abs(FMath::FindDeltaAngleDegrees(yaw, own->CameraYaw())) > 5,
                         TEXT("drag camera no skill")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE camera_drag"));
        own->arm->SetWorldRotation(FRotator(-40, 0, 0));
        cursor(boss->GetActorLocation() + FVector(0, 0, 300));
        next(17);
        return;
    }

    if (this->controlSmokeStage == 17)
    {
        cursor(boss->GetActorLocation() + FVector(0, 0, 300));
        if (NOW - this->controlSmokeTime <= .3)
        {
            return;
        }

        if (!checkResult(PickCombatTarget() != 0, TEXT("boss cursor target")))
        {
            return;
        }

        key(EKeys::LeftMouseButton, true);
        next(18);
        return;
    }

    if (this->controlSmokeStage == 18)
    {
        cursor(boss->GetActorLocation() + FVector(0, 0, 300));
        if (boss->hp > 1186)
        {
            return;
        }

        if (!checkResult(own->mp == 50 && this->selectedQuickSlot == 0 && boss->IsCombatHighlighted(),
                         TEXT("held basic while skill selected")))
        {
            return;
        }

        CollectGarbage(RF_NoFlags);
        TInlineComponentArray<UMeshComponent*> meshes;
        boss->GetComponents(meshes);
        int32 outlines = 0;
        for (auto* mesh : meshes)
        {
            if (mesh->ComponentHasTag(TEXT("RPG.AttackOutline")))
            {
                ++outlines;
            }

        }

        if (!checkResult(outlines > 0, TEXT("outline meshes/GC")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE held_basic_outline hp=%d meshes=%d"), boss->hp, outlines);
        shot(TEXT("-outline.png"));
        next(19);
        return;
    }

    if (this->controlSmokeStage == 19 && NOW - this->controlSmokeTime > .15)
    {
        key(EKeys::LeftMouseButton, false);
        releasedHp = boss->hp;
        next(20);
        return;
    }

    if (this->controlSmokeStage == 20 && NOW - this->controlSmokeTime > 1.3)
    {
        if (!checkResult(boss->hp == releasedHp && !boss->IsCombatHighlighted(),
                         TEXT("release stops basic and outline")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE release_stops"));
        shot(TEXT("-plain.png"));
        cursor(boss->GetActorLocation());
        next(21);
        return;
    }

    if (this->controlSmokeStage == 21 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::RightMouseButton, true);
        next(22);
        return;
    }

    if (this->controlSmokeStage == 22 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::RightMouseButton, false);
        next(23);
        return;
    }

    if (this->controlSmokeStage == 23 && this->ownCast && this->ownSkill == 4)
    {
        shot(TEXT("-burst-cast.png"));
        next(24);
        return;
    }

    if (this->controlSmokeStage == 24 && own->mp == 38 && !this->ownCast && NOW - this->controlSmokeTime > .6)
    {
        if (!checkResult(this->controlHud->ClickQuickSlotForTest(0) && this->selectedQuickSlot == -1,
                         TEXT("cooldown deselect")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE right_selected_burst"));
        if (!checkResult(this->controlHud->OpenQuickSlotPickerForTest(1), TEXT("second slot registration")))
        {
            return;
        }

        next(25);
        return;
    }

    if (this->controlSmokeStage == 25 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(this->controlHud->ClickBindingForTest(3), TEXT("register bolt")))
        {
            return;
        }

        next(26);
        return;
    }

    if (this->controlSmokeStage == 26 && NOW - this->controlSmokeTime > .25)
    {
        key(EKeys::Two, true);
        next(27);
        return;
    }

    if (this->controlSmokeStage == 27 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::Two, false);
        if (!checkResult(this->selectedQuickSlot == 1 && GetQuickSlotBinding(1) == 3, TEXT("select registered bolt")))
        {
            return;
        }

        cursor(boss->GetActorLocation());
        next(28);
        return;
    }

    if (this->controlSmokeStage == 28 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::RightMouseButton, true);
        next(29);
        return;
    }

    if (this->controlSmokeStage == 29 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::RightMouseButton, false);
        next(30);
        return;
    }

    if (this->controlSmokeStage == 30 && own->mp == 30 && !this->ownCast && boss->hp < 100 && boss->hp > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE right_registered_bolt hp=%d"), boss->hp);
        next(31);
        return;
    }

    if (this->controlSmokeStage == 31 && NOW - this->controlSmokeTime > 2.8)
    {
        cursor(boss->GetActorLocation());
        key(EKeys::RightMouseButton, true);
        next(32);
        return;
    }

    if (this->controlSmokeStage == 32 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::RightMouseButton, false);
        next(33);
        return;
    }

    if (this->controlSmokeStage == 33 && boss->dead && !boss->IsCombatHighlighted())
    {
        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE death_clears_outline"));
        key(EKeys::Escape, true);
        next(34);
        return;
    }

    if (this->controlSmokeStage == 34 && NOW - this->controlSmokeTime > .1)
    {
        key(EKeys::Escape, false);
        if (!checkResult(this->selectedQuickSlot == -1 && !this->highlightedEnemy.IsValid(), TEXT("escape clears")))
        {
            return;
        }

        if (!this->itemCatalog)
        {
            this->itemCatalog = std::make_shared<const shared_world::economy::Catalog>(
                shared_world::economy::Catalog::Load(std::filesystem::u8path(
                    TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Items"))))));
        }

        this->inventoryUi = CreateWidget<UWorldInventoryUI>(pc, UWorldInventoryUI::StaticClass());
        this->inventoryUi->AddToViewport(100);
        next(35);
        return;
    }

    if (this->controlSmokeStage == 35 && NOW - this->controlSmokeTime > .6)
    {
        this->inventoryUi->LongTextForTest();
        next(36);
        return;
    }

    if (this->controlSmokeStage == 36 && NOW - this->controlSmokeTime > .3)
    {
        if (!checkResult(this->controlHud->QuickIconsInsideForTest() && this->inventoryUi->FramedTextInsideForTest(),
                         TEXT("text stays in frames")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE text_bounds"));
        shot(TEXT("-inventory-long.png"));
        next(37);
        return;
    }

    if (this->controlSmokeStage == 37 && NOW - this->controlSmokeTime > .3)
    {
        this->inventoryUi->RemoveFromParent();
        this->inventoryUi = nullptr;
        if (!checkResult(BindQuickSlot(1, 0) && GetQuickSlotBinding(1) == 0, TEXT("clear registered slot")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("QUICK_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
