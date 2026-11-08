#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "World/GameClientWorld.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldCombatEffect.h"
#include "World/WorldCombatPresentation.h"
#include "World/WorldCameraOcclusion.h"
#include "World/WorldControlHUD.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UIs/RpgUITheme.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"

void UGameSessionSubsystem::TickPresentationSmoke()
{
    using namespace shared_world;
    using namespace combat;
    if (this->state != 5 || !this->map)
    {
        return;
    }

    auto* own = GetInventoryCharacter();
    if (!own || !own->InitialPresentationReady())
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    static FVector base;
    static TArray<TWeakObjectPtr<AWorldCombatEffect>> fx;
    static TArray<FVector> near, far;
    static int32 gallery = 0;
    static const uint32 skills[] = {3, 4, 10, 11, 12, 13, 14, 15, 16, 17};
    auto require = [&](bool pass, const TCHAR* message)
    {
        if (!pass)
        {
            UE_LOG(LogTemp, Error, TEXT("PRESENTATION_SMOKE failed=%s"), message);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return pass;
    };

    auto shot = [&](const FString& name)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(FPaths::GetPath(file) / name, true, false);
    };

    auto clear = [&]()
    {
        for (auto p : fx)
        {
            if (p.IsValid())
            {
                p->Destroy();
            }

        }

        fx.Empty();
    };

    auto position = [&](FVector p)
    {
        p += FVector(GetWorld()->OriginLocation);
        return WorldPosition{FMath::RoundToInt32(p.X), FMath::RoundToInt32(p.Y), FMath::RoundToInt32(p.Z)};
    };

    auto spawn = [&](Event e, Ability a, int32 kind, double time)
    {
        auto* f = GetWorld()->SpawnActor<AWorldCombatEffect>();
        fx.Add(f);
        f->Activate(e, &a, 0, false, kind);
        f->Update(time, FVector::ZeroVector);
        return f;
    };

    auto recordSampleCoordinates = [&]()
    {
        int32 index = 0;
        auto points = near;
        points.Append(far);
        for (FVector p : points)
        {
            FHitResult ground;
            FCollisionQueryParams q;
            q.AddIgnoredActor(own);
            if (GetWorld()->LineTraceSingleByChannel(ground, p + FVector(0, 0, 800), p - FVector(0, 0, 500),
                                                     UWorldCameraOcclusion::GROUND_CHANNEL, q))
            {
                p = ground.ImpactPoint + FVector(0, 0, 2);
            }

            FVector2D screen;
            pc->ProjectWorldLocationToScreen(p, screen);
            UE_LOG(LogTemp, Display, TEXT("PRESENTATION_SMOKE sample stage=%d index=%d x=%.1f y=%.1f z=%.1f"),
                   this->controlSmokeStage, index++, screen.X, screen.Y, p.Z);
        }

    };

    // Newly instantiated decal/procedural permutations may compile after world
    // readiness. Wait for publication plus the stage's normal quiet frames.
    if (this->controlSmokeStage >= 2 && ((GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) ||
                                         FAssetCompilingManager::Get().GetNumRemainingAssets()))
    {
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 0)
    {
        if (!require(RPGCombatArt::CatalogReady(), TEXT("presentation catalog")))
        {
            return;
        }

        base = own->GetActorLocation();
        own->arm->SetRelativeLocation(FVector(400, 0, 100));
        own->arm->SetWorldRotation(FRotator(-60, 0, 0));
        own->SetCameraZoom(1900);
        auto cube = [&](FVector p, FVector scale, FRotator rotation)
        {
            auto* a = GetWorld()->SpawnActor<AStaticMeshActor>();
            auto* c = a->GetStaticMeshComponent();
            c->SetMobility(EComponentMobility::Movable);
            c->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            c->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            c->SetCollisionResponseToAllChannels(ECR_Ignore);
            c->SetCollisionResponseToChannel(UWorldCameraOcclusion::GROUND_CHANNEL, ECR_Block);
            a->SetActorLocation(base + p);
            a->SetActorScale3D(scale);
            a->SetActorRotation(rotation);
        };

        cube(FVector(480, -230, 70), FVector(5, 3, .2), FRotator(14, 0, 0));
        for (int32 i = 0; i < 3; ++i)
        {
            cube(FVector(300 + i * 140, 250, 20 + i * 25), FVector(1.4, 2, .4 + i * .5), FRotator::ZeroRotator);
        }

        this->economyState.skills = this->map->content->combat->skills.Initial(100);
        for (int32 i = 0; i < 10; ++i)
        {
            const auto ID = skills[i];
            if (!this->economyState.skills.ranks[ID])
            {
                this->map->content->combat->skills.Train(this->economyState.skills, ID, 1);
            }

            this->quickSlotBindings[i] = ID;
        }

        this->economyReady = true; // Local render fixture only: no economy server/DB or mutation RPC.
        TSet<UTexture2D*> icons;
        for (const auto ID : skills)
        {
            icons.Add(RpgUI::Texture(RpgUI::SkillIcon(ID)));
        }

        if (!require(icons.Num() == 10 && !icons.Contains(nullptr), TEXT("ten distinct imported textures")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("PRESENTATION_SMOKE unique_icons=10"));
        if (this->controlHud)
        {
            this->controlHud->OpenQuickSlotPickerForTest(0);
        }

        this->controlSmokeStage = 1;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 1 && NOW - this->controlSmokeTime > 1)
    {
        if ((GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) ||
            FAssetCompilingManager::Get().GetNumRemainingAssets())
        {
            return;
        }

        for (const auto ID : skills)
        {
            const auto* icon = RpgUI::Texture(RpgUI::SkillIcon(ID));
            UE_LOG(LogTemp, Display, TEXT("PRESENTATION_SMOKE texture id=%u size=%d,%d"), ID, icon->GetSizeX(),
                   icon->GetSizeY());
            if (!require(icon->GetSizeX() <= 512 && icon->GetSizeY() <= 512, TEXT("UI runtime texture budget")))
            {
                return;
            }

        }

        shot(TEXT("skill-icons.png"));
        this->controlSmokeStage = 2;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > .4)
    {
        if (this->controlHud)
        {
            this->controlHud->HideQuickSlotPicker();
        }

        near.Empty();
        far.Empty();
        for (int32 k = 0; k < 3; ++k)
        {
            Ability a = this->map->content->combat->abilities.at(k == 1 ? 17 : 16);
            a.radius = k == 1 ? 650 : k == 2 ? 65 : 80;
            a.range = k == 2 ? 600 : 700;
            a.angle = 60;
            Event e;
            e.skill = a.id;
            e.action = 100 + k;
            e.endMs = 100000;
            const FVector O = base + (k == 0   ? FVector(100, -230, 0)
                                      : k == 1 ? FVector(120, 260, 0)
                                               : FVector(850, -350, 0));
            e.origin = e.point = position(O);
            e.yaw = k == 2 ? 90000 : 0;
            spawn(e, a, 0, 25);
            near.Add(O + (k == 2 ? FVector(0, 150, 0) : FVector(k == 1 ? 140 : 175, 0, 0)));
            far.Add(O + (k == 2 ? FVector(0, 450, 0) : FVector(k == 1 ? 460 : 560, 0, 0)));
        }

        this->controlSmokeStage = 3;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > .8)
    {
        for (auto p : fx)
        {
            if (p.IsValid())
            {
                const auto* d = p->FindComponentByClass<UDecalComponent>();
                auto* m = Cast<UMaterialInstanceDynamic>(d->GetDecalMaterial());
                UE_LOG(LogTemp, Display,
                       TEXT("PRESENTATION_SMOKE decal active=%d hidden=%d visible=%d proxy=%d progress=%.2f "
                            "shaderProgress=%.2f loc=%s size=%s color=%s decals=%d"),
                       p->Active(), p->IsHidden(), d->IsVisible(), d->SceneProxy != nullptr, p->Progress(),
                       m ? m->K2_GetScalarParameterValue(TEXT("Progress")) : -1, *p->GetActorLocation().ToString(),
                       *d->DecalSize.ToString(),
                       m ? *m->K2_GetVectorParameterValue(TEXT("Color")).ToString() : TEXT("none"),
                       GetGameInstance()->GetGameViewportClient()->EngineShowFlags.Decals);
            }

        }

        recordSampleCoordinates();
        shot(TEXT("indicator-25.png"));
        this->controlSmokeStage = 4;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 4 && NOW - this->controlSmokeTime > .4)
    {
        for (auto p : fx)
        {
            if (p.IsValid())
            {
                p->Update(85, FVector::ZeroVector);
            }

        }

        this->controlSmokeStage = 5;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > .7)
    {
        recordSampleCoordinates();
        shot(TEXT("indicator-85.png"));
        UE_LOG(LogTemp, Display,
               TEXT("PRESENTATION_SMOKE terrain_captures complete")); // Offline PNG assertions determine success.
        this->controlSmokeStage = 6;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 6 && NOW - this->controlSmokeTime > .4)
    {
        clear();
        const uint32 ID = skills[gallery];
        Ability a = this->map->content->combat->abilities.at(ID);
        Event e;
        e.skill = ID;
        e.action = 200 + gallery;
        e.endMs = 1000;
        e.source = 1;
        e.target = 2;
        e.origin = position(base + FVector(ID == 12 ? 550 : 100, 0, a.shape == Shape::PROJECTILE ? 125 : 0));
        e.point = position(base + FVector(650, 0, a.shape == Shape::PROJECTILE ? 125 : 0));
        if (a.shape == Shape::CONE)
        {
            e.point = e.origin;
        }

        const int32 KIND = a.shape == Shape::PROJECTILE ? 1 : ID == 10 ? 5 : 2;
        if (ID == 10)
        {
            e.point = position(base + FVector(450, 0, 140));
        }

        spawn(e, a, KIND, KIND == 1 ? .5 : .18);
        if (ID == 12)
        {
            spawn(e, a, 2, .18);
        }

        Event hit = e;
        hit.point = position(base + FVector(480, 0, 160));
        hit.value = 138 + gallery * 67;
        spawn(hit, a, 5, .14);
        spawn(hit, a, 3, .2);
        this->controlSmokeStage = 7;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 7 && NOW - this->controlSmokeTime > .7)
    {
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
        {
            return;
        }

        shot(FString::Printf(TEXT("skill-%02u.png"), skills[gallery]));
        UE_LOG(LogTemp, Display, TEXT("PRESENTATION_SMOKE art skill=%u"), skills[gallery]);
        this->controlSmokeStage = 8;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 8 && NOW - this->controlSmokeTime > .4)
    {
        ++gallery;
        if (gallery < 10)
        {
            this->controlSmokeStage = 6;
            this->controlSmokeTime = NOW;
            return;
        }

        clear();
        UE_LOG(LogTemp, Display, TEXT("PRESENTATION_SMOKE captured"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
