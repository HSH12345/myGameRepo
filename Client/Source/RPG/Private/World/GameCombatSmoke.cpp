#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldCombatEffect.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if !UE_BUILD_SHIPPING
void UGameSessionSubsystem::TickCombatSmoke()
{
    if (this->state != 5)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto shot = [&](const TCHAR* suffix)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(file), FPaths::GetBaseFilename(file) + suffix), true, false);
    };

    auto require = [&](bool condition, const TCHAR* what)
    {
        if (!condition)
        {
            UE_LOG(LogTemp, Error, TEXT("COMBAT_SMOKE failed %s"), what);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return condition;
    };

    if (this->controlSmokeStage == 0 && this->smokeRemote && this->prepared.map.id == 201)
    {
        if (UsePortal())
        {
            this->controlSmokeStage = 1;
        }

        return;
    }

    if (this->prepared.map.id != 301)
    {
        return;
    }

    auto* ownEntry = this->players.Find(this->prepared.epoch);
    auto* soldierEntry = this->npcs.Find(shared_world::npc::ENTITY_BIT | 101);
    auto* veteranEntry = this->npcs.Find(shared_world::npc::ENTITY_BIT | 103);
    if (!ownEntry || !soldierEntry || !veteranEntry)
    {
        return;
    }

    auto* own = ownEntry->Get();
    auto* soldier = soldierEntry->Get();
    auto* veteran = veteranEntry->Get();
    if (!own->CombatAssetsReady() || !soldier->CombatAssetsReady() || !veteran->CombatAssetsReady())
    {
        return;
    }

    if (this->controlSmokeStage == 1)
    {
        own->arm->TargetArmLength = 1600;
        own->arm->SetRelativeLocation(FVector(500, 0, 100));
        own->arm->SetWorldRotation(FRotator(-60, 0, 0));
        this->combatTarget = shared_world::npc::ENTITY_BIT | 101;
        if (RequestAttack(4, soldier->GetActorLocation()))
        {
            this->controlSmokeStage = 2;
            this->controlSmokeTime = NOW;
            UE_LOG(LogTemp, Display, TEXT("COMBAT_SMOKE assets_and_cast_ready"));
        }

        return;
    }

    if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > .5)
    {
        bool visible = false;
        for (const auto& e : this->combatEffects)
        {
            if (e->Active() && e->EffectKind() == 0 && e->Progress() > .2f && e->Progress() < .7f)
            {
                visible = true;
            }

        }

        if (!require(visible, TEXT("partial cast indicator")))
        {
            return;
        }

        shot(TEXT("-cast-partial.png"));
        this->controlSmokeStage = 3;
        UE_LOG(LogTemp, Display, TEXT("COMBAT_SMOKE indicator_partial"));
        return;
    }

    if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > 1.1)
    {
        shot(TEXT("-cast-filled.png"));
        this->controlSmokeStage = 4;
        return;
    }

    if (this->controlSmokeStage == 4 && NOW - this->controlSmokeTime > 2.4)
    {
        if (!require(soldier->hp == 36 && own->mp == 38, TEXT("area damage and MP")))
        {
            return;
        }

        if (RequestAttack(3))
        {
            this->controlSmokeStage = 5;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 5 && !this->controlMovingShot)
    {
        for (const auto& e : this->combatEffects)
        {
            if (e->Active() && e->EffectKind() == 1 && e->Progress() > .15f && e->Progress() < .8f)
            {
                shot(TEXT("-bolt-flight.png"));
                this->controlMovingShot = true;
                break;
            }

        }

    }

    if (this->controlSmokeStage == 5 &&
        ((soldier->hp == 10 && own->mp == 30 && !this->ownCast) || NOW - this->controlSmokeTime > 5))
    {
        if (!require(soldier->hp == 10 && own->mp == 30, TEXT("bolt damage and MP")))
        {
            return;
        }

        if (RequestAttack(2))
        {
            this->controlSmokeStage = 6;
            this->controlSmokeTime = NOW;
            shot(TEXT("-projectile.png"));
        }

        return;
    }

    if (this->controlSmokeStage == 6 && NOW - this->controlSmokeTime > 1.3)
    {
        if (!require(soldier->hp == 1, TEXT("ranged basic damage")))
        {
            return;
        }

        if (RequestAttack(2))
        {
            this->controlSmokeStage = 7;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 7 && soldier->dead)
    {
        this->controlSmokeStage = 8;
        this->controlSmokeTime = NOW;
        UE_LOG(LogTemp, Display, TEXT("COMBAT_SMOKE soldier_dead"));
        return;
    }

    if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > .8)
    {
        own->arm->TargetArmLength = 900;
        own->arm->SetRelativeLocation(FVector(600, 0, 100));
        shot(TEXT("-soldier-dying.png"));
        this->controlSmokeStage = 9;
        return;
    }

    if (this->controlSmokeStage == 9 && NOW - this->controlSmokeTime > 2.4)
    {
        if (!require(soldier->CombatAnimationPosition() > 2, TEXT("soldier death animation completed")))
        {
            return;
        }

        shot(TEXT("-soldier-dead.png"));
        this->combatTarget = shared_world::npc::ENTITY_BIT | 103;
        this->controlSmokeStage = 10;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 10)
    {
        if (veteran->dead)
        {
            own->arm->SetRelativeLocation(FVector(1200, 0, 100));
            this->controlSmokeStage = 11;
            this->controlSmokeTime = NOW;
            UE_LOG(LogTemp, Display, TEXT("COMBAT_SMOKE veteran_dead"));
            return;
        }

        if (NOW - this->controlSmokeTime > 1.25)
        {
            RequestAttack(2);
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 11 && NOW - this->controlSmokeTime > .8)
    {
        shot(TEXT("-veteran-dying.png"));
        this->controlSmokeStage = 12;
        return;
    }

    if (this->controlSmokeStage == 12 && NOW - this->controlSmokeTime > 2.4)
    {
        if (!require(veteran->CombatAnimationPosition() > 2 && own->mp == 30,
                     TEXT("veteran death completed and basic costs no MP")))
        {
            return;
        }

        shot(TEXT("-veteran-dead.png"));
        this->controlSmokeStage = 13;
        this->controlSmokeTime = NOW;
        UE_LOG(LogTemp, Display, TEXT("COMBAT_SMOKE passed"));
        return;
    }

    if (this->controlSmokeStage == 13 && NOW - this->controlSmokeTime > 1)
    {
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
