#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "World/WorldPlayerPawn.h"
#include "World/WorldControlHUD.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void UGameSessionSubsystem::TickLoadingSmoke()
{
    if (this->state != 5)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto shot = [&](const TCHAR* name)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        if (file.IsEmpty())
        {
            return;
        }

        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(file), name), true, false);
    };

    auto stop = [&](const TCHAR* reason)
    {
        UE_LOG(LogTemp, Error, TEXT("LOADING_SMOKE failed=%s stage=%d"), reason, this->controlSmokeStage);
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };

    auto move = [&](FVector point, int32 next)
    {
        if (!RequestMoveTo(point))
        {
            stop(TEXT("route"));
            return;
        }

        this->controlSmokeStage = next;
        this->controlSmokeTime = NOW;
    };

    auto reached = [&](int32 x, int32 y)
    {
        return !this->clickPending && this->clickPath.empty() && this->pendingInputs.empty() &&
               FMath::Abs(this->predicted.x - x) <= 2 && FMath::Abs(this->predicted.y - y) <= 2;
    };

    if (this->controlSmokeStage == 0)
    {
        if (this->prepared.map.id != 201)
        {
            stop(TEXT("initial town"));
            return;
        }

        shot(TEXT("Town.png"));
        UE_LOG(LogTemp, Display, TEXT("LOADING_SMOKE town_revealed"));
        this->controlSmokeStage = 1;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 1 && NOW - this->controlSmokeTime > 1)
    {
        move(FVector(-3500, 0, 112), 2);
    }
    else if (this->controlSmokeStage == 2 && reached(-3500, 0))
    {
        move(FVector(-6000, 0, 507), 3);
    }
    else if (this->controlSmokeStage == 3 && reached(-6000, 0))
    {
        move(FVector(-6000, -600, 507), 4);
    }
    else if (this->controlSmokeStage == 4 && reached(-6000, -600))
    {
        if (!TalkToNearestNpc())
        {
            stop(TEXT("npc talk"));
            return;
        }

        this->controlSmokeStage = 5;
    }
    else if (this->controlSmokeStage == 5 && this->dialogueId && this->controlHud &&
             this->controlHud->IsNpcDialogueVisible())
    {
        if (!this->controlHud->ClickNpcWarpForTest())
        {
            stop(TEXT("warp click"));
            return;
        }

        this->controlSmokeStage = 6;
        UE_LOG(LogTemp, Display, TEXT("LOADING_SMOKE warp_clicked"));
    }
    else if (this->controlSmokeStage == 6 && this->prepared.map.id == 301)
    {
        shot(TEXT("Dungeon.png"));
        UE_LOG(LogTemp, Display, TEXT("LOADING_SMOKE dungeon_revealed"));
        this->controlSmokeStage = 7;
        this->controlSmokeTime = NOW;
    }
    else if (this->controlSmokeStage == 7 && NOW - this->controlSmokeTime > 3)
    {
        UE_LOG(LogTemp, Display, TEXT("LOADING_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
