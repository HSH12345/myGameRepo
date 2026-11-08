#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "GameClientWorld.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldExitZones.h"
#include "World/WorldCombatEffect.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif
void UGameSessionSubsystem::TickMazeSmoke()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgQuickSlotSmoke")))
    {
        TickQuickSlotSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeVisibility")))
    {
        TickVisibilitySmoke();
        return;
    }

    if (this->state != 5 || !this->map)
    {
        return;
    }

    auto* entry = this->players.Find(this->prepared.epoch);
    if (!entry || !IsValid(*entry) || !(*entry)->AppearanceReady())
    {
        return;
    }

    auto* own = entry->Get();
    const double NOW = FPlatformTime::Seconds();
    auto shot = [&](const TCHAR* suffix)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(file), FPaths::GetBaseFilename(file) + suffix), true, false);
    };

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeBoss")))
    {
        // Rendered acceptance fixture: normal Gate handoff, corridor spawn beside room6, weak boss damage.
        static TSet<FString> seenSkills;
        static int32 warnings = 0;
        static FVector previous;
        static bool moved = false;
        static bool surfaceMelee = false;
        if (this->controlSmokeStage == 0)
        {
            if (UsePortal())
            {
                this->controlSmokeStage = 1;
                this->controlSmokeTime = NOW;
            }

            return;
        }

        if (this->prepared.map.id != 301 || !this->map->dungeonRun)
        {
            return;
        }

        if (this->controlSmokeStage == 1)
        {
            own->SetCameraZoom(2400);
            UpdateBossCamera(true);
            own->SetCameraZoom(1800);
            UpdateBossCamera(true);
            UpdateBossCamera(false);
            if (!FMath::IsNearlyEqual(own->GetCameraZoom(), 1800.f))
            {
                UE_LOG(LogTemp, Error, TEXT("BOSS_SMOKE manual_zoom_overridden"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE manual_zoom_preserved"));
            own->SetCameraZoom(650);
            own->arm->SetRelativeLocation(FVector(500, 0, 250));
            own->arm->SetWorldRotation(FRotator(-65, 0, 0));
            if (RequestMoveTo(FVector(-450, -8400, 0)))
            {
                this->controlSmokeStage = 2;
                this->controlSmokeTime = NOW;
            }

            return;
        }

        AWorldPlayerPawn* boss = nullptr;
        uint64 bossId = 0;
        for (const auto& n : this->npcs)
        {
            if (n.Value && n.Value->npcIndex == 2201)
            {
                boss = n.Value;
                bossId = n.Key;
                break;
            }

        }

        if (!boss || !boss->CombatAssetsReady())
        {
            return;
        }

        if (this->controlSmokeStage == 2)
        {
#if WITH_EDITOR
            if (FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 ||
                (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
            {
                return;
            }

#endif
            if (this->map->activeRoom != 6 || this->npcs.Num() != 1 ||
                GetWorld()->GetSubsystem<UWorldExitPool>()->DoorInstanceCount() != 2)
            {
                return;
            }

            if (!FMath::IsNearlyEqual(own->GetCameraZoom(), 2000.f))
            {
                UE_LOG(LogTemp, Error, TEXT("BOSS_SMOKE camera_not_framed"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE camera_framed"));
            previous = boss->GetActorLocation();
            this->combatTarget = bossId;
            if (auto* mesh = boss->FindComponentByClass<USkeletalMeshComponent>())
            {
                if (!FMath::IsNearlyEqual(mesh->GetRelativeScale3D().X, .15))
                {
                    UE_LOG(LogTemp, Error, TEXT("BOSS_SMOKE wrong_scale"));
                    FPlatformMisc::RequestExitWithStatus(false, 1);
                    return;
                }

            }

            QueueCombatIntent(1, bossId, boss->GetActorLocation());
            shot(TEXT("-boss.png"));
            this->controlSmokeStage = 3;
            this->controlSmokeTime = NOW;
            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE sealed_assets_ready"));
            return;
        }

        if (this->controlSmokeStage == 3)
        {
            if (FVector::Dist2D(boss->GetActorLocation(), previous) > 40)
            {
                moved = true;
            }

            if (auto* mesh = boss->FindComponentByClass<USkeletalMeshComponent>())
            {
                if (auto* node = mesh->GetSingleNodeInstance())
                {
                    if (auto* anim = node->GetCurrentAsset())
                    {
                        const FString NAME = anim->GetName();
                        if ((NAME.Contains(TEXT("LeftSmashAttack")) || NAME.Contains(TEXT("BigSpittersAttack")) ||
                             NAME.Contains(TEXT("SmallSpittersAttack")) || NAME.EndsWith(TEXT("_Roar"))) &&
                            !seenSkills.Contains(NAME))
                        {
                            seenSkills.Add(NAME);
                            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE skill_animation=%s"), *NAME);
                            for (const auto& effect : this->combatEffects)
                            {
                                if (effect && effect->Active() && effect->EffectKind() == 0)
                                {
                                    ++warnings;
                                    shot(*FString::Printf(TEXT("-skill%d.png"), seenSkills.Num()));
                                    break;
                                }

                            }

                        }

                    }

                }

            }

            if (!surfaceMelee && boss->hp < boss->maxHp)
            {
                const double DISTANCE = FVector::Dist2D(own->GetActorLocation(), boss->GetActorLocation());
                if (DISTANCE < 330)
                {
                    UE_LOG(LogTemp, Error, TEXT("BOSS_SMOKE overlapping_melee distance=%.1f"), DISTANCE);
                    FPlatformMisc::RequestExitWithStatus(false, 1);
                    return;
                }

                surfaceMelee = true;
                shot(TEXT("-surface-melee.png"));
                UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE surface_melee distance=%.1f"), DISTANCE);
            }

            if (!surfaceMelee && !this->intentSkill && !this->ownCast && !this->attackRequestPending &&
                NOW - this->controlSmokeTime > 2)
            {
                QueueCombatIntent(1, bossId, boss->GetActorLocation());
                this->controlSmokeTime = NOW;
            }

            if (seenSkills.Num() == 4 && warnings >= 3 && moved && surfaceMelee)
            {
                if (RequestAttack(4, boss->GetActorLocation(), 0))
                {
                    this->controlSmokeStage = 4;
                    this->controlSmokeTime = NOW;
                }

                return;
            }

        }

        if (this->controlSmokeStage == 4 && boss->dead && !this->map->activeRoom)
        {
            if (!FMath::IsNearlyEqual(own->GetCameraZoom(), 2000.f))
            {
                UE_LOG(LogTemp, Error, TEXT("BOSS_SMOKE corpse_camera_too_close"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE corpse_camera_preserved"));
            this->controlSmokeStage = 5;
            this->controlSmokeTime = NOW;
            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE death_open skills=%d warnings=%d moved=%d"), seenSkills.Num(),
                   warnings, moved);
            return;
        }

        if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > 1)
        {
            shot(TEXT("-dead.png"));
            if (RequestMoveTo(FVector(-1000, -8400, 0)))
            {
                this->controlSmokeStage = 6;
                this->controlSmokeTime = NOW;
            }

            return;
        }

        if (this->controlSmokeStage == 6 && this->predicted.x < -600 && NOW - this->controlSmokeTime > 2)
        {
            this->controlSmokeStage = 7;
            this->controlSmokeTime = NOW;
            return;
        }

        // Smoke executes before TickControl. Check after its camera update has
        // observed the new authoritative/predicted room on a following frame.
        if (this->controlSmokeStage == 7 && NOW - this->controlSmokeTime > .8)
        {
            if (!FMath::IsNearlyEqual(own->arm->TargetArmLength, 650.f))
            {
                UE_LOG(LogTemp, Error, TEXT("BOSS_SMOKE camera_not_restored"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE camera_restored"));
            UE_LOG(LogTemp, Display, TEXT("BOSS_SMOKE passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        (void)bossId;
        return;
    }

    if (this->controlSmokeStage == 0)
    {
        if (UsePortal())
        {
            this->controlSmokeStage = 1;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->prepared.map.id != 301 || this->prepared.map.version != 3 || !this->map->dungeonRun)
    {
        return;
    }

    if (this->controlSmokeStage == 1)
    {
        if (this->npcs.Num() != 0 || this->dungeonExits.count != 0)
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        own->arm->TargetArmLength = 11500;
        own->arm->SetWorldRotation(FRotator(-89, 0, 0));
        own->arm->bDoCollisionTest = false;
        this->controlSmokeStage = 2;
        this->controlSmokeTime = NOW;
        UE_LOG(LogTemp, Display, TEXT("MAZE_SMOKE empty_entry"));
        return;
    }

    if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > 3)
    {
#if WITH_EDITOR
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return;
        }

#endif
        shot(TEXT("-maze.png"));
        this->controlSmokeStage = 3;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > 1)
    {
        own->arm->TargetArmLength = 1700;
        own->arm->SetWorldRotation(FRotator(-65, 0, 0));
        if (RequestMoveTo(FVector(-1800, 0, 0)))
        {
            this->controlSmokeStage = 4;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 4 && this->map->activeRoom == 1 && this->npcs.Num() == 3)
    {
        for (const auto& n : this->npcs)
        {
            if (!n.Value->CombatAssetsReady())
            {
                return;
            }

        }

        shot(TEXT("-sealed.png"));
        this->controlSmokeStage = 5;
        this->controlSmokeTime = NOW;
        UE_LOG(LogTemp, Display, TEXT("MAZE_SMOKE sealed_spawn"));
        return;
    }

    if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > 2)
    {
        RequestMoveTo(FVector(-1100, 0, 0));
        this->controlSmokeStage = 6;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 6 && NOW - this->controlSmokeTime > 3)
    {
        if (this->predicted.x > -1274)
        {
            UE_LOG(LogTemp, Error, TEXT("MAZE_SMOKE door_escape"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }

        CancelClickMove();
        this->controlSmokeStage = 7;
        this->controlSmokeTime = NOW;
        UE_LOG(LogTemp, Display, TEXT("MAZE_SMOKE retreat_blocked"));
        return;
    }

    if (this->controlSmokeStage == 7)
    {
        if (!this->map->activeRoom)
        {
            shot(TEXT("-cleared.png"));
            this->controlSmokeStage = 8;
            this->controlSmokeTime = NOW;
            UE_LOG(LogTemp, Display, TEXT("MAZE_SMOKE cleared_open"));
            return;
        }

        if (NOW - this->controlSmokeTime > 1.1)
        {
            for (const auto& n : this->npcs)
            {
                if (!n.Value->dead)
                {
                    RequestAttack(2, FVector::ZeroVector, n.Key);
                    break;
                }

            }

            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > 2)
    {
        RequestMoveTo(FVector(0, 0, 0));
        this->controlSmokeStage = 9;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 9 && this->predicted.x >= -1200)
    {
        UE_LOG(LogTemp, Display, TEXT("MAZE_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
