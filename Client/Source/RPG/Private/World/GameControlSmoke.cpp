#include "World/GameSessionSubsystem.h"
#include "GameClientWorld.h"
#include "SharedWorld/Dungeon/DungeonMaze.h"
#if !UE_BUILD_SHIPPING
#include "World/WorldPlayerPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "World/WorldEntityPool.h"
#include "World/WorldControlHUD.h"
#include "World/WorldExitZones.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

void UGameSessionSubsystem::TickControlSmoke()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgCombatPresentationSmoke")))
    {
        TickPresentationSmoke();
        return;
    }

    if (this->state != 5)
    {
        return;
    }

    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    auto* own = this->players.Find(this->prepared.epoch);
    if (!pc || !own || !IsValid(*own) || !(*own)->AppearanceReady())
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    const bool OBSERVER = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeOffset"));
    FString screenshot;
    FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
    auto key = [pc](FKey which, bool press)
    {
        pc->InputKey(FInputKeyEventArgs::CreateSimulated(which, press ? IE_Pressed : IE_Released, press ? 1.f : 0.f));
    };

    auto hasAnimation = [](AWorldPlayerPawn* pawn, const TCHAR* name)
    {
        auto* mesh = pawn->FindComponentByClass<USkeletalMeshComponent>();
        auto* instance = mesh ? mesh->GetSingleNodeInstance() : nullptr;
        return instance && instance->GetCurrentAsset() && instance->GetCurrentAsset()->GetName().Contains(name);
    };

    auto shot = [&](const TCHAR* suffix)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(screenshot), FPaths::GetBaseFilename(screenshot) + suffix), true, false);
    };

    if (OBSERVER)
    {
        for (const auto& pair : this->players)
        {
            if (pair.Key != this->prepared.epoch && IsValid(pair.Value) && pair.Value->AppearanceReady())
            {
                pc->SetViewTarget(pair.Value);
                if (this->smokeMoved && !this->controlMovingShot && pair.Value->IsRunningAnimationPlaying() &&
                    hasAnimation(pair.Value, TEXT("Sprint_Loop")))
                {
                    this->controlMovingShot = true;
                    FScreenshotRequest::RequestScreenshot(screenshot, true, false);
                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_remote_animation"));
                }

                if (this->controlMovingShot && !this->controlWalkingShot && pair.Value->IsWalkingAnimationPlaying() &&
                    hasAnimation(pair.Value, TEXT("Walk_Loop")))
                {
                    if (this->controlSmokeTime == 0)
                    {
                        this->controlSmokeTime = NOW;
                    }

                    if (NOW - this->controlSmokeTime > .75)
                    {
                        this->controlWalkingShot = true;
                        shot(TEXT("-walk.png"));
                        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_remote_walk"));
                    }

                }
                else
                {
                    this->controlSmokeTime = 0;
                }

            }

        }

        if (this->smokeDespawn && this->controlMovingShot && this->controlWalkingShot)
        {
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        return;
    }

    if (this->controlSmokeStage == 0 && this->smokeRemote && NOW - this->smokeReady > 2)
    {
        key(EKeys::W, true);
        this->controlSmokeStage = 1;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 1)
    {
        if (this->predicted.x > -1800 && !this->controlMovingShot && (*own)->IsRunningAnimationPlaying() &&
            hasAnimation(*own, TEXT("Sprint_Loop")))
        {
            this->controlMovingShot = true;
            FScreenshotRequest::RequestScreenshot(
                FPaths::Combine(FPaths::GetPath(screenshot), FPaths::GetBaseFilename(screenshot) + TEXT("-moving.png")),
                true, false);
        }

        if (this->predicted.x >= -600)
        {
            key(EKeys::W, false);
            if (!this->controlMovingShot)
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_wasd"));
            this->controlSmokeStage = 2;
            this->controlSmokeTime = NOW;
        }

    }
    else if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > .3)
    {
        FVector2D screen;
        if (!pc->ProjectWorldLocationToScreen(FVector(300, -3350, 0) - FVector(GetWorld()->OriginLocation), screen))
        {
            return;
        }

        pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        key(EKeys::LeftMouseButton, true);
        this->controlSmokeStage = 3;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::LeftMouseButton, false);
        this->controlSmokeStage = 4;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 4 && !this->clickPath.empty())
    {
        key(EKeys::W, true);
        this->controlSmokeStage = 5;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > .2)
    {
        key(EKeys::W, false);
        if (!this->clickPath.empty() || this->clickPending)
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_wasd_cancel"));
        this->controlSmokeStage = 50;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 50 && NOW - this->controlSmokeTime > .2)
    {
        // Rapid retarget uses the same API as cursor picking. Only the latest goal may publish.
        RequestMoveTo(FVector(-600, -3500, 0) - FVector(GetWorld()->OriginLocation));
        StartClickPath();
        RequestMoveTo(FVector(300, -3350, 0) - FVector(GetWorld()->OriginLocation));
        this->controlSmokeStage = 6;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 6 && this->predicted.x == 300 && this->predicted.y == -3350 &&
             this->clickPath.empty() && !this->clickPending)
    {
        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_click_arrived"));
        this->controlSmokeStage = 7;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 7 && NOW - this->controlSmokeTime > 1 && !(*own)->IsMovementAnimationPlaying())
    {
        FScreenshotRequest::RequestScreenshot(screenshot, true, false);
        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_idle"));
        this->controlSmokeStage = 8;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > 2)
    {
        key(EKeys::MiddleMouseButton, true);
        key(EKeys::MouseScrollUp, true);
        this->controlSmokeStage = 9;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 9 && NOW - this->controlSmokeTime > .2)
    {
        pc->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 20.f));
        if ((*own)->CameraYaw() >= 25)
        {
            key(EKeys::MiddleMouseButton, false);
            key(EKeys::MouseScrollUp, false);
            const auto STEP = ControlStep(1, 0);
            if (STEP.x <= 0 || STEP.y <= 0 || STEP.x * STEP.x + STEP.y * STEP.y > 400 ||
                !FMath::IsNearlyEqual((*own)->arm->TargetArmLength, 570.f))
            {
                UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_camera_failed"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            key(EKeys::W, true);
            this->controlSmokeStage = 10;
            this->controlSmokeTime = NOW;
        }

    }
    else if (this->controlSmokeStage == 10 && NOW - this->controlSmokeTime > .5)
    {
        key(EKeys::W, false);
        if (this->predicted.x <= 300 || this->predicted.y <= -3350)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_camera_move_failed"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_camera"));
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(screenshot), FPaths::GetBaseFilename(screenshot) + TEXT("-camera.png")),
            true, false);
        this->controlSmokeStage = 11;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 11 && NOW - this->controlSmokeTime > 2 && this->pendingInputs.empty())
    {
        (*own)->arm->SetWorldRotation(FRotator(-40, 0, 0));
        key(EKeys::CapsLock, true);
        this->controlSmokeStage = 100;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 100 && NOW - this->controlSmokeTime > .2 && this->walkMode)
    {
        key(EKeys::CapsLock, false);
        const auto AXIS = ControlStep(1, 0), DIAGONAL = ControlStep(1, 1);
        if (AXIS.x != 4 || AXIS.y != 0 || DIAGONAL.x != 3 || DIAGONAL.y != 3)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_walk_quantization_failed"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        this->paceSmokeOrigin = this->predicted;
        this->paceSmokeSequence = this->nextSequence;
        key(EKeys::W, true);
        this->controlSmokeStage = 101;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 101 && NOW - this->controlSmokeTime > 2)
    {
        key(EKeys::W, false);
        const uint64 STEPS = this->nextSequence - this->paceSmokeSequence;
        if (STEPS < 30 || this->predicted.x - this->paceSmokeOrigin.x != 4 * STEPS ||
            this->predicted.y != this->paceSmokeOrigin.y || !(*own)->IsWalkingAnimationPlaying() ||
            !hasAnimation(*own, TEXT("Walk_Loop")))
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_walk_failed steps=%llu delta=%d"), STEPS,
                   this->predicted.x - this->paceSmokeOrigin.x);
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        UE_LOG(LogTemp, Display,
               TEXT("WORLD_SMOKE controls_walk steps=%llu distance=%d cm_per_tick=4 run_cm_per_tick=20"), STEPS,
               this->predicted.x - this->paceSmokeOrigin.x);
        shot(TEXT("-walk.png"));
        this->controlSmokeStage = 102;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 102 && NOW - this->controlSmokeTime > 1 && this->pendingInputs.empty())
    {
        this->paceSmokeOrigin = this->predicted;
        if (!RequestMoveTo(FVector(this->predicted.x + 120, this->predicted.y + 120, this->predicted.z) -
                           FVector(GetWorld()->OriginLocation)))
        {
            return;
        }

        this->controlSmokeStage = 103;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 103 && !this->clickPending && this->clickPath.empty() &&
             this->pendingInputs.empty())
    {
        if (this->predicted.x != this->paceSmokeOrigin.x + 120 || this->predicted.y != this->paceSmokeOrigin.y + 120)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_walk_click_failed"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_walk_click server_acknowledged=1"));
        key(EKeys::CapsLock, true);
        this->controlSmokeStage = 104;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 104 && NOW - this->controlSmokeTime > .2 && !this->walkMode)
    {
        key(EKeys::CapsLock, false);
        if (ControlStep(1, 0).x != 20)
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        key(EKeys::W, true);
        this->controlSmokeStage = 105;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 105 && NOW - this->controlSmokeTime > 1)
    {
        key(EKeys::W, false);
        if (!(*own)->IsRunningAnimationPlaying() || !hasAnimation(*own, TEXT("Sprint_Loop")))
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_run_restore_failed"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_run_restore"));
        this->controlSmokeStage = 106;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 106 && NOW - this->controlSmokeTime > 1 && this->pendingInputs.empty() &&
             !(*own)->IsMovementAnimationPlaying())
    {
        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE controls_passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

    if (NOW - this->controlSmokeTime > 35 && this->controlSmokeStage > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE controls_timeout stage=%d feet=%d,%d"), this->controlSmokeStage,
               this->predicted.x, this->predicted.y);
        FPlatformMisc::RequestExitWithStatus(false, 1);
    }

}

void UGameSessionSubsystem::TickNpcSmoke()
{
    if (this->state != 5 || !this->smokeRemote || this->npcs.Num() != 2)
    {
        return;
    }

    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    auto* pawn = this->npcs.FindRef(shared_world::npc::ENTITY_BIT | 1).Get();
    if (!pc || !IsValid(pawn) || !pawn->AppearanceReady())
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    if (this->controlSmokeStage == 0)
    {
        const FVector FEET = pawn->GetActorLocation() + FVector(GetWorld()->OriginLocation);
        if (this->prepared.map.id != 201 || !FEET.Equals(FVector(-6030, -775, 507), .01) || pawn->npcIndex != 1001 ||
            pawn->npcRole != 5)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_NPC invalid_actual_spawn"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        auto* mesh = pawn->FindComponentByClass<USkeletalMeshComponent>();
        if (!mesh || !mesh->GetSingleNodeInstance() || !mesh->GetSingleNodeInstance()->IsLooping())
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_NPC idle_not_looping"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        FHitResult hit;
        const FVector LOCAL = pawn->GetActorLocation();
        const bool GROUND =
            GetWorld()->LineTraceSingleByChannel(hit, LOCAL + FVector(0, 0, 30), LOCAL - FVector(0, 0, 100), ECC_Pawn);
        UE_LOG(LogTemp, Display,
               TEXT("WORLD_NPC actual_spawn feet=%.0f,%.0f,%.0f ground=%d ground_z=%.3f idle_time=%.3f"), FEET.X,
               FEET.Y, FEET.Z, GROUND, GROUND ? hit.ImpactPoint.Z + GetWorld()->OriginLocation.Z : -9999,
               mesh->GetSingleNodeInstance()->GetCurrentTime());
        if (!GROUND || FMath::Abs(hit.ImpactPoint.Z - LOCAL.Z) > 5)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_NPC ground_mismatch"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        pc->SetViewTarget(pawn);
        pawn->UpdateCamera(240, 26.66f, 3.5f);
        this->controlSmokeStage = 1;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 1 && NOW - this->controlSmokeTime > 5)
    {
#if WITH_EDITOR
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return;
        }

#endif
        auto* mesh = pawn->FindComponentByClass<USkeletalMeshComponent>();
        if (!mesh || !mesh->GetSingleNodeInstance() || mesh->GetSingleNodeInstance()->GetCurrentTime() <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_NPC idle_not_advancing"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        FString screenshot;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
        FScreenshotRequest::RequestScreenshot(screenshot, true, false);
        UE_LOG(LogTemp, Display, TEXT("WORLD_NPC rendered_idle pool_active=%d idle_time=%.3f"),
               GetWorld()->GetSubsystem<UWorldEntityPool>()->ActiveCount(),
               mesh->GetSingleNodeInstance()->GetCurrentTime());
        this->controlSmokeStage = 2;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > 2)
    {
        UE_LOG(LogTemp, Display, TEXT("WORLD_NPC passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

void UGameSessionSubsystem::TickNpcWarpSmoke()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgLoadingSmoke")))
    {
        TickLoadingSmoke();
        return;
    }

    if (this->state != 5)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto* own = this->players.Find(this->prepared.epoch);
    if (!own || !IsValid(*own) || !(*own)->AppearanceReady())
    {
        return;
    }

    auto shot = [&](const TCHAR* suffix)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(file), FPaths::GetBaseFilename(file) + suffix), true, false);
    };

    auto reached = [&](int32 x, int32 y)
    {
        return !this->clickPending && this->clickPath.empty() && this->pendingInputs.empty() &&
               FMath::Abs(this->predicted.x - x) <= 2 && FMath::Abs(this->predicted.y - y) <= 2;
    };

    auto stop = [&](const TCHAR* why)
    {
        UE_LOG(LogTemp, Error, TEXT("NPC_WARP_SMOKE failed=%s stage=%d feet=%d,%d,%d"), why, this->controlSmokeStage,
               this->predicted.x, this->predicted.y, this->predicted.z);
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };

    if (this->controlSmokeStage == 0 && this->smokeRemote)
    {
        if ((*own)->GetServerStats() != shared_world::EntityStats::PlayerDefaults() || this->npcs.Num() != 2 ||
            !this->npcs.Contains(shared_world::npc::ENTITY_BIT | 1) ||
            !this->npcs.Contains(shared_world::npc::ENTITY_BIT | 2) ||
            this->npcs.FindRef(shared_world::npc::ENTITY_BIT | 1)->GetServerStats() != shared_world::EntityStats{} ||
            this->npcs.FindRef(shared_world::npc::ENTITY_BIT | 2)->GetServerStats() != shared_world::EntityStats{})
        {
            stop(TEXT("initial_stats"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ENTITY_STATS_SMOKE player_defaults service_invulnerable"));
        if (!RequestMoveTo(FVector(-3500, 0, 112)))
        {
            stop(TEXT("first_route"));
            return;
        }

        this->controlSmokeStage = 1;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 1 && reached(-3500, 0))
    {
        if (!RequestMoveTo(FVector(-6000, 0, 507)))
        {
            stop(TEXT("stairs_route"));
            return;
        }

        this->controlSmokeStage = 2;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 2 && reached(-6000, 0))
    {
        if (!RequestMoveTo(FVector(-6000, -600, 507)))
        {
            stop(TEXT("npc_route"));
            return;
        }

        this->controlSmokeStage = 3;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 3 && reached(-6000, -600))
    {
        (*own)->UpdateCamera(-120, 20, 2);
        if (!TalkToNearestNpc())
        {
            stop(TEXT("talk"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE approached"));
        this->controlSmokeStage = 4;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 4 && this->dialogueId && this->controlHud &&
             this->controlHud->IsNpcDialogueVisible())
    {
        shot(TEXT("-dialogue.png"));
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE dialogue_visible"));
        this->controlSmokeStage = 5;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > 2)
    {
        if (!this->controlHud->ClickNpcWarpForTest() || !this->npcWarping || ChooseNpcWarp())
        {
            stop(TEXT("warp_button"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE warp_button"));
        this->controlSmokeStage = 6;
    }
    else if (this->controlSmokeStage == 6 && this->dungeonMatch.members.size() == 2 && this->controlHud)
    {
        if (!this->controlHud->ClickDungeonReadyForTest())
        {
            return;
        }

        shot(TEXT("-waiting.png"));
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE waiting_room room=%llu members=2 ready_button"),
               this->dungeonMatch.room);
        this->controlSmokeStage = 60;
    }
    else if (this->controlSmokeStage == 60 &&
             this->dungeonMatch.matchPhase == shared_world::party::MatchPhase::COUNTDOWN)
    {
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE countdown milliseconds=%u"), this->dungeonMatch.countdownMs);
        this->controlSmokeStage = 61;
    }
    else if (this->controlSmokeStage == 61 && this->prepared.map.id == 301 && this->prepared.request == 1 &&
             this->dungeonExits.Valid() && this->players.Num() == 2 && this->entityParties.Num() == 2)
    {
        if ((*own)->GetServerStats() != shared_world::EntityStats::PlayerDefaults())
        {
            stop(TEXT("dungeon_stats"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ENTITY_STATS_SMOKE dungeon_preserved"));
        if (this->npcs.Num() != 0 || this->predicted != shared_world::WorldPosition{0, 0, 0})
        {
            stop(TEXT("dungeon_admission"));
            return;
        }

        for (const auto& participant : this->players)
        {
            if (!IsPartyMember(participant.Key) || IsHostileEntity(participant.Key))
            {
                stop(TEXT("party_protection"));
                return;
            }

        }

        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE party_protection party=%llu"),
               this->entityParties.FindRef(this->prepared.epoch));

        if (FParse::Param(FCommandLine::Get(), TEXT("RpgPartyDungeonSmoke")))
        {
            if (!RequestMoveTo(FVector(200, 0, 0)))
            {
                stop(TEXT("party_dungeon_movement"));
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE dungeon instance=%llu"), this->prepared.instance);
            this->controlSmokeStage = 62;
            this->controlSmokeTime = NOW;
            return;
        }

        auto* exitPool = GetWorld()->GetSubsystem<UWorldExitPool>();
        if (exitPool->AllocatedCount() != 3 || this->prepared.entity != 0)
        {
            stop(TEXT("exit_pool"));
            return;
        }

        for (unsigned i = 0; i < this->dungeonExits.count; ++i)
        {
            if (!exitPool->GetZone(i) || exitPool->GetZone(i)->RingInstanceCount() != 32)
            {
                stop(TEXT("exit_geometry"));
                return;
            }

        }

        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE exit_layout count=%u run=%llu"), this->dungeonExits.count,
               this->dungeonExits.run);
        if (!RequestMoveTo(FVector(200, 0, 0)))
        {
            stop(TEXT("dungeon_movement"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE dungeon instance=%llu"), this->prepared.instance);
        this->controlSmokeStage = 7;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 62 && reached(200, 0) && NOW - this->controlSmokeTime > 3)
    {
        shot(TEXT("-dungeon.png"));
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE dungeon_movement"));
        this->controlSmokeStage = 63;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 63 && NOW - this->controlSmokeTime > 2)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("RpgPartyCombatSmoke")))
        {
            this->controlSmokeStage = 64;
            this->controlSmokeTime = NOW;
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else if (this->controlSmokeStage == 64)
    {
        const bool LATE = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeOffset"));
        if ((!LATE || NOW - this->controlSmokeTime > 6) && RequestMoveTo(FVector(-1800, 0, 0)))
        {
            this->controlSmokeStage = 65;
            this->controlSmokeTime = NOW;
        }

    }
    else if (this->controlSmokeStage == 65 && reached(-1800, 0) && this->npcs.Num() == 3)
    {
        for (const auto& participant : this->npcs)
        {
            if (!participant.Value->CombatAssetsReady() || !this->combatLives.FindRef(participant.Key))
            {
                return;
            }

        }

        if (GetWorld()->GetSubsystem<UWorldExitPool>()->DoorInstanceCount() != 0)
        {
            stop(TEXT("combat_created_barriers"));
            return;
        }

        shot(TEXT("-combat.png"));
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE combat_assets_ready open_entry monsters=3"));
        this->controlSmokeStage = 66;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 66)
    {
        bool together = true;
        for (const auto& participant : this->players)
        {
            const FVector POSITION = participant.Value->GetActorLocation() + FVector(GetWorld()->OriginLocation);
            if (shared_world::dungeon::RoomAt({FMath::RoundToInt32(POSITION.X), FMath::RoundToInt32(POSITION.Y), 0}) !=
                1)
            {
                together = false;
            }

        }

        if (!together)
        {
            return;
        }

        if (this->map->activeRoom == 0 && std::all_of(this->npcs.begin(), this->npcs.end(),
                                                      [](const auto& participant)
                                                      {
                                                          return participant.Value->dead;
                                                      }))

        {
            shot(TEXT("-cleared.png"));
            UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE combat_cleared both_inside connection_alive"));
            this->controlSmokeStage = 67;
            this->controlSmokeTime = NOW;
            return;
        }

        if (NOW - this->controlSmokeTime > 1.15)
        {
            for (const auto& participant : this->npcs)
            {
                if (!participant.Value->dead &&
                    RequestAttack(2, participant.Value->GetActorLocation(), participant.Key))
                {
                    this->controlSmokeTime = NOW;
                    break;
                }

            }

        }

    }
    else if (this->controlSmokeStage == 67 && NOW - this->controlSmokeTime > 2)
    {
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else if (this->controlSmokeStage == 7 && reached(200, 0) && NOW - this->controlSmokeTime > 3)
    {
#if WITH_EDITOR
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return;
        }

#endif
        if (this->controlHud && this->controlHud->IsNpcDialogueVisible())
        {
            stop(TEXT("stale_dialogue"));
            return;
        }

        shot(TEXT("-dungeon.png"));
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE dungeon_movement"));
        this->controlSmokeStage = 8;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > 2)
    {
        (*own)->arm->TargetArmLength = 10500;
        (*own)->arm->SetWorldRotation(FRotator(-85, 0, 0));
        this->controlSmokeStage = 70;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 70 && NOW - this->controlSmokeTime > 2)
    {
        shot(TEXT("-layout.png"));
        this->controlSmokeStage = 71;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 71 && NOW - this->controlSmokeTime > 2)
    {
        (*own)->arm->TargetArmLength = 650;
        (*own)->arm->SetWorldRotation(FRotator(-40, 0, 0));
        const auto& zone = this->dungeonExits.zones[0];
        this->smokeExitMidpoint = FIntPoint((200 + zone.feet.x) / 2, zone.feet.y / 2);
        if (!RequestMoveTo(FVector(this->smokeExitMidpoint.X, this->smokeExitMidpoint.Y, zone.feet.z)))
        {
            stop(TEXT("exit_route_mid"));
            return;
        }

        this->controlSmokeStage = 80;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 80 && reached(this->smokeExitMidpoint.X, this->smokeExitMidpoint.Y))
    {
        const auto& zone = this->dungeonExits.zones[0];
        if (!RequestMoveTo(FVector(zone.feet.x, zone.feet.y, zone.feet.z)))
        {
            stop(TEXT("exit_route"));
            return;
        }

        this->controlSmokeStage = 81;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 81 &&
             reached(this->dungeonExits.zones[0].feet.x, this->dungeonExits.zones[0].feet.y))
    {
        if (!this->dungeonExits.zones[0].Contains(this->predicted))
        {
            stop(TEXT("exit_not_inside"));
            return;
        }

        shot(TEXT("-exit.png"));
        this->smokeExpectedExitType = static_cast<int32>(this->dungeonExits.zones[0].type);
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE exit_inside type=%d"), this->smokeExpectedExitType);
        this->controlSmokeStage = 82;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 82 && NOW - this->controlSmokeTime > 2)
    {
        if (!TryUseDungeonExit() || !this->exitWarping)
        {
            stop(TEXT("exit_request"));
            return;
        }

        this->controlSmokeStage = 9;
    }
    else if (this->controlSmokeStage == 9 && this->prepared.map.id == 201 && this->prepared.request == 2 &&
             this->npcs.Num() == 2 && !this->pendingExit.run)
    {
        if ((*own)->GetServerStats() != shared_world::EntityStats::PlayerDefaults() ||
            this->npcs.CreateConstIterator().Value()->GetServerStats() != shared_world::EntityStats{})
        {
            stop(TEXT("return_stats"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("ENTITY_STATS_SMOKE return_preserved"));
        if (this->predicted != shared_world::WorldPosition{-1000, 0, 65} || this->currentNpc || this->dialogueId ||
            this->npcWarping || this->controlHud->IsNpcDialogueVisible())
        {
            stop(TEXT("return_state"));
            return;
        }

        if (this->lastDungeonExitType != this->smokeExpectedExitType ||
            this->lastDungeonExitOutcome != this->smokeExpectedExitType)
        {
            stop(TEXT("exit_outcome"));
            return;
        }

        shot(TEXT("-return.png"));
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE returned"));
        this->controlSmokeStage = 10;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 10 && NOW - this->controlSmokeTime > 2)
    {
        UE_LOG(LogTemp, Display, TEXT("NPC_WARP_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

    if (((this->controlSmokeStage >= 1 && this->controlSmokeStage <= 3) || this->controlSmokeStage == 80 ||
         this->controlSmokeStage == 81) &&
        NOW - this->controlSmokeTime > 30)
    {
        stop(TEXT("route_timeout"));
    }

}

#endif
