#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "World/WorldPlayerPawn.h"
#include "World/WorldInventoryUI.h"
#include "World/WorldControlHUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif
void UGameSessionSubsystem::TickEconomySmoke()
{
    const bool POTION_SMOKE = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokePotion"));
    const bool HUD_SMOKE = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeHud"));
    if (this->state != 5 || (!this->smokeRemote && !POTION_SMOKE) || !this->economyReady)
    {
        return;
    }

    auto* own = this->players.Find(this->prepared.epoch);
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!own || !IsValid(*own) || !(*own)->AppearanceReady() || !pc)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    const bool SECOND = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeOffset"));
    const int32 OUTFIT_OFFER = SECOND ? 2 : 3;
    const uint32 OUTFIT_ID = SECOND ? 2002 : 2003;
    const int32 DEFENSE = SECOND ? 2 : 5;
    auto stop = [&](const TCHAR* why)
    {
        UE_LOG(LogTemp, Error, TEXT("ECONOMY_SMOKE failed=%s stage=%d feet=%d,%d,%d"), why, this->controlSmokeStage,
               this->predicted.x, this->predicted.y, this->predicted.z);
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };

    auto advance = [&]
    {
        ++this->controlSmokeStage;
        this->controlSmokeTime = NOW;
    };

    auto reached = [&](int32 x, int32 y)
    {
        return !this->clickPending && this->clickPath.empty() && this->pendingInputs.empty() &&
               FMath::Abs(this->predicted.x - x) <= 2 && FMath::Abs(this->predicted.y - y) <= 2;
    };

    auto shot = [&](const TCHAR* suffix)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(file), FPaths::GetBaseFilename(file) + suffix), true, false);
    };

    auto stableShot = [&](const TCHAR* suffix)
    {
        if (!this->economySmokeCaptureTime)
        {
            shot(suffix);
            this->economySmokeCaptureTime = NOW;
            return false;
        }

        if (FScreenshotRequest::IsScreenshotRequested() || NOW - this->economySmokeCaptureTime < .35)
        {
            return false;
        }

        this->economySmokeCaptureTime = 0;
        return true;
    };

    if (POTION_SMOKE)
    {
        static int32 beforeHp = 0;
        auto key = [&](FKey which, bool press)
        {
            pc->InputKey(
                FInputKeyEventArgs::CreateSimulated(which, press ? IE_Pressed : IE_Released, press ? 1.f : 0.f));
        };

        if (this->controlSmokeStage == 0)
        {
            if ((*own)->hp != 5000 || (*own)->maxHp != 5000 || (*own)->mp != 50)
            {
                stop(TEXT("potion_initial_stats"));
                return;
            }

            if (RequestMoveTo(FVector(-3500, 0, 112)))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 1 && reached(-3500, 0))
        {
            if (RequestMoveTo(FVector(-6000, 0, 507)))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 2 && reached(-6000, 0))
        {
            if (RequestMoveTo(FVector(-6000, -800, 505)))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 3 && reached(-6000, -800))
        {
            if (RequestMoveTo(FVector(-6200, -800, 516)))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 4 && reached(-6200, -800))
        {
            if (TalkToNearestNpc())
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 5 && this->shopToken && InventoryVisible())
        {
            if (!this->inventoryUi->ClickOfferForTest(5))
            {
                stop(TEXT("potion_buy_click"));
                return;
            }

            advance();
            return;
        }

        if (this->controlSmokeStage == 6 && !EconomyBusy() && NOW - this->controlSmokeTime > .5)
        {
            if (BagItem(0) && BagItem(0)->quantity < 3)
            {
                if (!this->inventoryUi->ClickOfferForTest(5))
                {
                    stop(TEXT("potion_repeat_buy"));
                    return;
                }

                this->controlSmokeTime = NOW;
                return;
            }

            if (HUD_SMOKE && (!BagItem(1) || BagItem(1)->quantity < 2))
            {
                if (!this->inventoryUi->ClickOfferForTest(6))
                {
                    stop(TEXT("mana_buy_click"));
                    return;
                }

                this->controlSmokeTime = NOW;
                return;
            }

            if (!BagItem(0) || BagItem(0)->type != 1201 || BagItem(0)->quantity != 3 ||
                this->economyState.gold != (HUD_SMOKE ? 875ULL : 925ULL))
            {
                stop(TEXT("potion_purchase"));
                return;
            }

            if (HUD_SMOKE &&
                (!this->inventoryUi->ClickActionForTest(0, 5) || !this->inventoryUi->ClickActionForTest(1, 5)))
            {
                stop(TEXT("register_potions"));
                return;
            }

            if (!stableShot(TEXT("-shop.png")))
            {
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("POTION_SMOKE shop_visible qty=3 gold=%llu HP=%d"), this->economyState.gold,
                   (*own)->hp);
            CloseShop();
            if (RequestMoveTo(FVector(-6000, -800, 505)))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 7 && reached(-6000, -800))
        {
            if (TalkToNearestNpc() && this->currentNpc)
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 8 && this->dialogueId)
        {
            if (ChooseNpcWarp())
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 9 && this->prepared.map.id == 301)
        {
            if (RequestMoveTo(FVector(-1500, 0, 0)))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 10 && (*own)->hp < 5000)
        {
            CancelClickMove();
            beforeHp = (*own)->hp;
            ToggleInventory();
            advance();
            return;
        }

        if (this->controlSmokeStage == 11 && InventoryVisible() && NOW - this->controlSmokeTime > .5)
        {
            if (!this->inventoryUi->ClickActionForTest(0, 3))
            {
                stop(TEXT("potion_use_button"));
                return;
            }

            advance();
            return;
        }

        if (this->controlSmokeStage == 12 && !EconomyBusy() && BagItem(0) && BagItem(0)->quantity == 2)
        {
            // New monster damage may share the same receive batch; the report checks each authoritative heal message.
            if (!stableShot(TEXT("-used.png")))
            {
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("POTION_SMOKE used_button before=%d after=%d qty=2"), beforeHp, (*own)->hp);
            CloseShop();
            advance();
            return;
        }

        if (this->controlSmokeStage == 13 && NOW - this->controlSmokeTime > 3.3 && (*own)->hp < (*own)->maxHp)
        {
            key(EKeys::Three, true);
            advance();
            return;
        }

        if (this->controlSmokeStage == 14 && NOW - this->controlSmokeTime > .15)
        {
            key(EKeys::Three, false);
            key(EKeys::RightMouseButton, true);
            this->controlSmokeStage = 140;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 140 && NOW - this->controlSmokeTime > .15)
        {
            key(EKeys::RightMouseButton, false);
            this->controlSmokeStage = 15;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 15 && !EconomyBusy() && BagItem(0) && BagItem(0)->quantity == 1)
        {
            UE_LOG(LogTemp, Display, TEXT("POTION_SMOKE used_key HP=%d qty=1"), (*own)->hp);
            if (HUD_SMOKE)
            {
                this->controlSmokeStage = 30;
                this->controlSmokeTime = NOW;
                return;
            }

            ToggleInventory();
            advance();
            return;
        }

        if (this->controlSmokeStage == 16 && NOW - this->controlSmokeTime > 1)
        {
            if (stableShot(TEXT("-key.png")))
            {
                advance();
            }

            return;
        }

        if (this->controlSmokeStage == 17 && NOW - this->controlSmokeTime > 1)
        {
            UE_LOG(LogTemp, Display, TEXT("POTION_SMOKE passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        if (this->controlSmokeStage == 30 && NOW - this->controlSmokeTime > .5)
        {
            for (const auto& entry : this->npcs)
            {
                if (IsValid(entry.Value) && entry.Value->attackable && !entry.Value->dead &&
                    FVector::DistSquared2D(entry.Value->GetActorLocation(), (*own)->GetActorLocation()) < 900 * 900)
                {
                    this->combatTarget = entry.Key;
                    break;
                }

            }

            if (!this->combatTarget || !this->controlHud || !this->controlHud->ClickQuickSlotForTest(0))
            {
                stop(TEXT("hud_bolt_click"));
                return;
            }

            auto* target = this->npcs.FindRef(this->combatTarget).Get();
            FVector2D screen;
            pc->ProjectWorldLocationToScreen(target->GetActorLocation(), screen);
            pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
            UE_LOG(LogTemp, Display, TEXT("HUD_SMOKE bolt_click"));
            this->controlSmokeStage = 300;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 300 && NOW - this->controlSmokeTime > .2)
        {
            key(EKeys::RightMouseButton, true);
            this->controlSmokeStage = 301;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 301 && NOW - this->controlSmokeTime > .15)
        {
            key(EKeys::RightMouseButton, false);
            this->controlSmokeStage = 31;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 31 && (*own)->mp < 50 && !this->ownCast && !this->attackRequestPending &&
            NOW > this->potionHudReadyAt + .3)
        {
            if (!this->controlHud->CombatHudReadyForTest())
            {
                stop(TEXT("hud_minimap_target"));
                return;
            }

            if (!stableShot(TEXT("-hud.png")))
            {
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("HUD_SMOKE minimap_enemy_quickslots"));
            if (!this->controlHud->ClickQuickSlotForTest(3))
            {
                stop(TEXT("hud_mana_click"));
                return;
            }

            this->controlSmokeStage = 310;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 310 && NOW - this->controlSmokeTime > .2)
        {
            key(EKeys::RightMouseButton, true);
            this->controlSmokeStage = 311;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 311 && NOW - this->controlSmokeTime > .15)
        {
            key(EKeys::RightMouseButton, false);
            this->controlSmokeStage = 32;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 32 && !EconomyBusy() && (*own)->mp == 50 && BagItem(1) &&
            BagItem(1)->quantity == 1)
        {
            UE_LOG(LogTemp, Display, TEXT("HUD_SMOKE mana_click_restored"));
            ToggleInventory();
            advance();
            return;
        }

        if (this->controlSmokeStage == 33 && NOW - this->controlSmokeTime > .5)
        {
            if (!this->inventoryUi->ClickActionForTest(1, 5))
            {
                stop(TEXT("hud_register_mana"));
                return;
            }

            if (!stableShot(TEXT("-key.png")))
            {
                return;
            }

            CloseShop();
            advance();
            return;
        }

        if (this->controlSmokeStage == 34 && NOW - this->controlSmokeTime > .5)
        {
            if (!this->controlHud->ClickQuickSlotForTest(1) || !this->aimingArea)
            {
                stop(TEXT("hud_area_aim"));
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("HUD_SMOKE area_aim"));
            advance();
            return;
        }

        if (this->controlSmokeStage == 35 && NOW - this->controlSmokeTime > .2)
        {
            auto* target = this->npcs.FindRef(this->combatTarget).Get();
            FVector2D screen;
            const FVector POINT =
                IsValid(target) ? target->GetActorLocation() : (*own)->GetActorLocation() + FVector(150, 0, 0);
            if (!pc->ProjectWorldLocationToScreen(POINT, screen))
            {
                stop(TEXT("hud_ground_project"));
                return;
            }

            pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
            key(EKeys::RightMouseButton, true);
            advance();
            return;
        }

        if (this->controlSmokeStage == 36 && NOW - this->controlSmokeTime > .2)
        {
            key(EKeys::RightMouseButton, false);
            advance();
            return;
        }

        if (this->controlSmokeStage == 37 && (*own)->mp < 50 && !this->ownCast && !this->attackRequestPending &&
            NOW > this->potionHudReadyAt + .3)
        {
            key(EKeys::Four, true);
            advance();
            return;
        }

        if (this->controlSmokeStage == 38 && NOW - this->controlSmokeTime > .15)
        {
            key(EKeys::Four, false);
            key(EKeys::RightMouseButton, true);
            this->controlSmokeStage = 380;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 380 && NOW - this->controlSmokeTime > .15)
        {
            key(EKeys::RightMouseButton, false);
            this->controlSmokeStage = 39;
            this->controlSmokeTime = NOW;
            return;
        }

        if (this->controlSmokeStage == 39 && !EconomyBusy() && (*own)->mp == 50 && !BagItem(1))
        {
            if (!stableShot(TEXT("-hud-final.png")))
            {
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("HUD_SMOKE mana_key_restored"));
            advance();
            return;
        }

        if (this->controlSmokeStage == 40 && NOW - this->controlSmokeTime > 1)
        {
            UE_LOG(LogTemp, Display, TEXT("POTION_SMOKE passed"));
            UE_LOG(LogTemp, Display, TEXT("HUD_SMOKE passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        if (this->controlSmokeStage >= 30 && NOW - this->controlSmokeTime > 25)
        {
            stop(TEXT("hud_stage_timeout"));
        }

        return;
    }

    if (this->controlSmokeStage == 0)
    {
        if (this->economyState.gold != 1000 || !this->economyState.items.empty())
        {
            stop(TEXT("initial_snapshot"));
            return;
        }

        if (!RequestMoveTo(FVector(-3500, 0, 112)))
        {
            stop(TEXT("route1"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 1 && reached(-3500, 0))
    {
        if (!RequestMoveTo(FVector(-6000, 0, 507)))
        {
            stop(TEXT("route2"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 2 && reached(-6000, 0))
    {
        if (!RequestMoveTo(FVector(-6000, -800, 505)))
        {
            stop(TEXT("route3"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 3 && reached(-6000, -800))
    {
        if (!RequestMoveTo(FVector(-6200, SECOND ? -880 : -800, 516)))
        {
            stop(TEXT("route4"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 4 && reached(-6200, SECOND ? -880 : -800))
    {
        if (!TalkToNearestNpc() || this->shopNpc != (shared_world::npc::ENTITY_BIT | 2))
        {
            stop(TEXT("merchant_interaction"));
            return;
        }

        pc->SetViewTarget(*own);
        (*own)->UpdateCamera(0, 26.66f, 2.5f);
        advance();
    }
    else if (this->controlSmokeStage == 5 && this->shopToken && InventoryVisible())
    {
        if (!this->inventoryUi->ClickOfferForTest(1))
        {
            stop(TEXT("buy_button"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE shop_visible buy_button"));
        advance();
    }
    else if (this->controlSmokeStage == 6 && !EconomyBusy() && NOW - this->controlSmokeTime > .25)
    {
        if (this->economyState.gold != 900 || this->economyState.items.size() != 1 ||
            !this->inventoryUi->ClickActionForTest(0, 0))
        {
            stop(TEXT("equip_sword_button"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 7 && !EconomyBusy() && (*own)->EquipmentReady() &&
             NOW - this->controlSmokeTime > .25)
    {
        if ((*own)->GetEquipmentIds()[0] != 2001 || (*own)->attack != 15)
        {
            stop(TEXT("sword_state"));
            return;
        }

        if (!this->inventoryUi->ClickOfferForTest(OUTFIT_OFFER))
        {
            stop(TEXT("buy_outfit_button"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 8 && !EconomyBusy() && NOW - this->controlSmokeTime > .25)
    {
        if (this->economyState.items.size() != 2 || !this->inventoryUi->ClickActionForTest(1, 0))
        {
            stop(TEXT("equip_outfit_button"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 9 && !EconomyBusy() && (*own)->EquipmentReady() &&
             NOW - this->controlSmokeTime > 1)
    {
#if WITH_EDITOR
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return;
        }

#endif
        if ((*own)->GetEquipmentIds()[1] != OUTFIT_ID || (*own)->defense != DEFENSE)
        {
            stop(TEXT("outfit_state"));
            return;
        }

        if (!stableShot(TEXT("-equipped.png")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE equipped outfit=%u defense=%d"), OUTFIT_ID, DEFENSE);
        if (!BuyOffer(4, 10))
        {
            stop(TEXT("buy_stack"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 10 && !EconomyBusy() && NOW - this->controlSmokeTime > .25)
    {
        if (this->economyState.items.size() != 3 || !this->inventoryUi->ClickActionForTest(2, 2))
        {
            stop(TEXT("split_button"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 11 && !EconomyBusy() && NOW - this->controlSmokeTime > .25)
    {
        if (this->economyState.items.size() != 4 || !this->inventoryUi->ClickMoveForTest(3, 2))
        {
            stop(TEXT("merge_slots"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 12 && !EconomyBusy() && NOW - this->controlSmokeTime > .25)
    {
        if (this->economyState.items.size() != 3 || !BagItem(2) || BagItem(2)->quantity != 10)
        {
            stop(TEXT("stack_conservation"));
            return;
        }

        if (!stableShot(TEXT("-bag.png")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE inventory_split_merge"));
        if (!this->inventoryUi->ClickActionForTest(1, 1))
        {
            stop(TEXT("unequip_button"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 13 && !EconomyBusy() && (*own)->EquipmentReady() &&
             NOW - this->controlSmokeTime > .5)
    {
        if ((*own)->GetEquipmentIds()[1] != 0 || (*own)->defense != 0)
        {
            stop(TEXT("unequip_restore"));
            return;
        }

        if (!stableShot(TEXT("-unequipped.png")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE unequipped_restored"));
        if (!this->inventoryUi->ClickActionForTest(1, 0))
        {
            stop(TEXT("reequip_button"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 14 && !EconomyBusy() && (*own)->EquipmentReady() &&
             NOW - this->controlSmokeTime > .5)
    {
        bool remote = false;
        for (const auto& entry : this->players)
        {
            if (entry.Key != this->prepared.epoch && entry.Value->EquipmentReady() &&
                entry.Value->GetEquipmentIds()[0] == 2001 && entry.Value->GetEquipmentIds()[1])
            {
                remote = true;
            }

        }

        if (!remote)
        {
            return;
        }

        if (this->economyState.gold != (SECOND ? 790ULL : 640ULL))
        {
            stop(TEXT("final_gold"));
            return;
        }

        CloseShop();
        UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE remote_equipment gold=%llu"), this->economyState.gold);
        if (!RequestMoveTo(FVector(-6200, SECOND ? -480 : -400, 515)))
        {
            stop(TEXT("equipped_move"));
            return;
        }

        advance();
    }
    else if (this->controlSmokeStage == 15)
    {
        if ((*own)->IsMovementAnimationPlaying() && !this->controlMovingShot)
        {
            shot(TEXT("-moving.png"));
            this->controlMovingShot = true;
            UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE moving_equipment"));
        }

        if (reached(-6200, SECOND ? -480 : -400) && this->controlMovingShot && NOW - this->controlSmokeTime > 1)
        {
            shot(TEXT("-world.png"));
            advance();
        }

    }
    else if (this->controlSmokeStage == 16 && NOW - this->controlSmokeTime > 3)
    {
        UE_LOG(LogTemp, Display, TEXT("ECONOMY_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

    if (this->controlSmokeStage > 0 && NOW - this->controlSmokeTime > 45)
    {
        stop(TEXT("stage_timeout"));
    }

}

#endif
