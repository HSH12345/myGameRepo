#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldLoadingScreen.h"
#include "ContentStreaming.h"
#include "ShaderPipelineCache.h"
#include "PipelineStateCache.h"
#include "ShaderCompiler.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

bool UGameSessionSubsystem::TickLoadingScreen(float delta)
{
    if (!this->loadingScreen || !this->loadingScreen->IsVisible())
    {
#if !UE_BUILD_SHIPPING
        if (this->measureReveal)
        {
            this->revealFrameTimes.Add(delta * 1000);
            if (this->revealFrameTimes.Num() == 120)
            {
                this->measureReveal = false;
                this->revealFrameTimes.Sort();
                UE_LOG(LogTemp, Display,
                       TEXT("WORLD_LOADING frames map=%u n=120 p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f"),
                       this->prepared.map.id, this->revealFrameTimes[59], this->revealFrameTimes[113],
                       this->revealFrameTimes[118], this->revealFrameTimes[119]);
            }

        }

#endif
        return false;
    }

    if (FPlatformTime::Seconds() > this->presentationDeadline)
    {
        Fail(TEXT("World presentation readiness timed out"));
        return true;
    }

    const auto* own = this->players.FindRef(this->prepared.epoch).Get();
    bool ready = IsValid(own) && own->InitialCameraReady() && StreamingReady() && !IsAsyncLoading();
    // Wait for the entire initial preload area. Gameplay movement retains its smaller safety query.
    if (this->streamingSource && !this->streamingSource->IsStreamingCompleted())
    {
        ready = false;
    }

    auto checkResult = [&](const auto& entities)
    {
        for (const auto& entry : entities)
        {
            const auto* pawn = entry.Value.Get();
            if (!IsValid(pawn) || !pawn->InitialPresentationReady())
            {
                ready = false;
            }

            if (IsValid(pawn) && pawn->EquipmentFailed())
            {
                Fail(TEXT("World equipment assets are unavailable"));
                return false;
            }

        }

        return true;
    };

    if (!checkResult(this->players) || !checkResult(this->npcs))
    {
        return true;
    }

    ready = ready && IStreamingManager::Get().GetNumWantingResources() == 0 &&
            FShaderPipelineCache::NumPrecompilesRemaining() == 0;
    ready = ready && PipelineStateCache::NumActivePrecacheRequests() == 0 &&
            PipelineStateCache::GetNumActivePipelinePrecompileTasks() == 0;
    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
    {
        ready = false;
    }

#if WITH_EDITOR
    ready = ready && FAssetCompilingManager::Get().GetNumRemainingAssets() == 0;
#endif
#if !UE_BUILD_SHIPPING
    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgSkillSmoke")))
    {
        static double reportAt = 0;
        if (FPlatformTime::Seconds() > reportAt)
        {
            reportAt = FPlatformTime::Seconds() + 5;
            UE_LOG(LogTemp, Display, TEXT("SKILL_LOADING ready=%d streaming=%d async=%d textures=%d pso=%u"), ready,
                   StreamingReady(), IsAsyncLoading(), IStreamingManager::Get().GetNumWantingResources(),
                   PipelineStateCache::NumActivePrecacheRequests());
            for (const auto& pair : this->players)
            {
                UE_LOG(LogTemp, Display, TEXT("SKILL_LOADING player=%llu presentation=%d"), pair.Key,
                       pair.Value->InitialPresentationReady());
            }

            for (const auto& pair : this->npcs)
            {
                UE_LOG(LogTemp, Display, TEXT("SKILL_LOADING npc=%llu presentation=%d"), pair.Key,
                       pair.Value->InitialPresentationReady());
            }

        }

    }

#endif
    this->loadingScreen->Update(.85f, TEXT("외형과 렌더링을 준비하고 있습니다"));
#if !UE_BUILD_SHIPPING
    if (this->smoke && ready && this->loadingCaptureGeneration != this->generation)
    {
        this->loadingCaptureGeneration = this->generation;
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        if (!file.IsEmpty())
        {
            FScreenshotRequest::RequestScreenshot(
                FPaths::Combine(FPaths::GetPath(file),
                                FString::Printf(TEXT("Loading-%u-%llu.png"), this->prepared.map.id, this->generation)),
                true, false);
        }

        UE_LOG(LogTemp, Display, TEXT("WORLD_LOADING resources_ready map=%u generation=%llu"), this->prepared.map.id,
               this->generation);
    }

#endif
    if (this->loadingScreen->WarmFrame(ready, delta))
    {
        this->loadingScreen->Update(1.f, TEXT("준비 완료"));
        UE_LOG(LogTemp, Display, TEXT("WORLD_LOADING revealed map=%u generation=%llu camera_ready=%d"),
               this->prepared.map.id, this->generation, IsValid(own) && own->InitialCameraReady());
        this->loadingScreen->Hide();
#if !UE_BUILD_SHIPPING
        this->measureReveal = this->smoke;
        this->revealFrameTimes.Reset(120);
#endif
        return false;
    }

    // Network/economy and the actual world renderer continue; player input waits.
    return true;
}

