#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "GameClientWorld.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldCameraOcclusion.h"
#include "Materials/MaterialInterface.h"
#include "InputKeyEventArgs.h"
#include "World/WorldControlHUD.h"
#include "World/WorldInventoryUI.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/Material.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

void UGameSessionSubsystem::TickVisibilitySmoke()
{
    if (this->state != 5 || !this->map)
    {
        return;
    }

    auto* own = this->players.FindRef(this->prepared.epoch).Get();
    if (!IsValid(own) || !own->AppearanceReady())
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    auto require = [&](bool pass, const TCHAR* name)
    {
        if (!pass)
        {
            UE_LOG(LogTemp, Error, TEXT("VISIBILITY_SMOKE failed=%s"), name);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return pass;
    };

    auto shot = [&](const FString& suffix)
    {
        FString file;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), file);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::GetPath(file), FPaths::GetBaseFilename(file) + suffix), true, false);
    };

    auto logReveal = [&](const TCHAR* name)
    {
        FVector2D screen;
        GetGameInstance()->GetFirstLocalPlayerController()->ProjectWorldLocationToScreen(
            own->GetActorLocation() + FVector(0, 0, 90), screen);
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE reveal=%s x=%.1f y=%.1f"), name, screen.X, screen.Y);
    };

    if (this->controlSmokeStage == 0)
    {
        if (UsePortal())
        {
            this->controlSmokeStage = 1;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->prepared.map.id != 301)
    {
        return;
    }

    static int32 angle = 0;
    static TWeakObjectPtr<AStaticMeshActor> occluder;
    static TWeakObjectPtr<UStaticMeshComponent> occluderShape;
    static FVector expectedGround = FVector::ZeroVector;
    const FRotator views[] = {FRotator(-20, 0, 0),   FRotator(-20, 90, 0), FRotator(-40, 180, 0),
                              FRotator(-40, 270, 0), FRotator(-65, 0, 0),  FRotator(-65, 180, 0)};
    if (this->controlSmokeStage == 1)
    {
#if WITH_EDITOR
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return;
        }

#endif
        if (!require(own->HasLocalVisibility() && own->GetVisibilityMaterial(), TEXT("local effect")))
        {
            return;
        }

        if (!require(own->InitialCameraReady(), TEXT("camera settled before first reveal")))
        {
            return;
        }

        const auto& limits = this->map->content->collision->Map().Definition().bounds;
        const FVector REBASE(GetWorld()->OriginLocation);
        FVector boundaryStart = own->GetActorLocation();
        boundaryStart.Z = limits.max.z + 800 - REBASE.Z;
        const FVector ends[] = {FVector(limits.min.x - 100 - REBASE.X, boundaryStart.Y, boundaryStart.Z),
                                FVector(limits.max.x + 100 - REBASE.X, boundaryStart.Y, boundaryStart.Z),
                                FVector(boundaryStart.X, limits.min.y - 100 - REBASE.Y, boundaryStart.Z),
                                FVector(boundaryStart.X, limits.max.y + 100 - REBASE.Y, boundaryStart.Z)};
        for (const auto& end : ends)
        {
            FHitResult hit;
            GetWorld()->LineTraceSingleByChannel(hit, boundaryStart, end, ECC_Camera);
            if (!require(hit.GetActor() && hit.GetActor()->ActorHasTag(TEXT("RPG.CameraBoundary")) &&
                             hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore &&
                             hit.GetComponent()->GetCollisionResponseToChannel(UWorldCameraOcclusion::GROUND_CHANNEL) ==
                                 ECR_Ignore,
                         TEXT("four outer camera boundaries preserve movement/ground queries")))
            {
                return;
            }

        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE boundary directions=4"));
        // This wall is below the character in screen space and outside the
        // player-to-camera fade sweep. Test before any geometry configuration.
        const FVector GOAL = own->GetActorLocation() + FVector(0, -300, 0);
        expectedGround = GOAL;
        const FVector START = own->camera->GetComponentLocation();
        auto* block = GetWorld()->SpawnActor<AStaticMeshActor>();
        occluder = block;
        auto* shape = block->GetStaticMeshComponent();
        shape->SetMobility(EComponentMobility::Movable);
        occluderShape = shape;
        shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        shape->SetMaterial(0, LoadObject<UMaterialInterface>(
                                  nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));
        shape->SetCollisionProfileName(TEXT("BlockAll"));
        block->SetActorLocation(FMath::Lerp(START, GOAL, .85));
        block->SetActorScale3D(FVector(.5, .5, 1));
        block->Tags.Add(TEXT("RPG.CameraFade"));
        FHitResult ground;
        if (!require(!own->cameraOcclusion->IsFaded(shape) &&
                         own->cameraOcclusion->TraceGround(ground, START, GOAL - FVector(0, 0, 50)) &&
                         FMath::Abs(ground.ImpactPoint.Z) < 3,
                     TEXT("ground trace before first fade probe")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE first_ground camera_ready=1"));
        UWorldCameraOcclusion::ConfigureGeometry(shape);
        FVector2D screen;
        auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
        pc->ProjectWorldLocationToScreen(GOAL + FVector(0, 0, 2), screen);
        pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        this->controlSmokeStage = 19;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 19 && NOW - this->controlSmokeTime > .4)
    {
        auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
        FVector2D screen;
        pc->ProjectWorldLocationToScreen(expectedGround + FVector(0, 0, 2), screen);
        pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        FVector ground = FVector::ZeroVector;
        auto* shape = occluderShape.Get();
        if (!require(!own->cameraOcclusion->IsFaded(shape) && CursorGround(ground) &&
                         FVector::Dist2D(ground, expectedGround) < 15 && FMath::Abs(ground.Z) < 3 &&
                         shape->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block &&
                         shape->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore,
                     TEXT("opaque off-sweep wall permits ground cursor")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE opaque_ground x=%.1f y=%.1f"), ground.X, ground.Y);
        occluder->Destroy();
        occluder = nullptr;
        own->SetCameraZoom(1200);
        own->arm->SetWorldRotation(views[angle]);
        this->controlSmokeStage = 2;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 2 && NOW - this->controlSmokeTime > .8)
    {
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
        {
            return;
        }

#endif
        FCollisionQueryParams query;
        query.AddIgnoredActor(own);
        if (!require(own->arm->bDoCollisionTest &&
                         !GetWorld()->OverlapBlockingTestByChannel(own->camera->GetComponentLocation(), FQuat::Identity,
                                                                   ECC_Camera, FCollisionShape::MakeSphere(3), query),
                     TEXT("camera inside wall")))
        {
            return;
        }

        const double DISTANCE =
            FVector::Dist(own->camera->GetComponentLocation(), own->GetActorLocation() + FVector(0, 0, 100));
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE camera=%d distance=%.1f collision=%d"), angle, DISTANCE,
               own->arm->IsCollisionFixApplied());
        shot(FString::Printf(TEXT("-camera%d.png"), angle));
        ++angle;
        if (angle < UE_ARRAY_COUNT(views))
        {
            own->arm->SetWorldRotation(views[angle]);
            this->controlSmokeTime = NOW;
            return;
        }

        this->controlSmokeStage = 3;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > .5)
    {
        auto* block = GetWorld()->SpawnActor<AStaticMeshActor>();
        occluder = block;
        block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        // Match the actual closed-door rendering path, not a plain wall mesh.
        auto* shape = NewObject<UInstancedStaticMeshComponent>(block);
        occluderShape = shape;
        block->AddInstanceComponent(shape);
        shape->SetupAttachment(block->GetRootComponent());
        shape->SetMobility(EComponentMobility::Movable);
        shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        shape->SetMaterial(0, LoadObject<UMaterialInterface>(
                                  nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));
        shape->SetCollisionProfileName(TEXT("BlockAll"));
        shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        shape->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        shape->RegisterComponent();
        shape->AddInstance(FTransform::Identity);
        block->SetActorLocation((own->camera->GetComponentLocation() + own->GetActorLocation() + FVector(0, 0, 90)) *
                                .5);
        block->SetActorRotation(own->camera->GetComponentRotation());
        block->SetActorScale3D(FVector(.12, 3, 3));
        this->controlSmokeStage = 4;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 4 && NOW - this->controlSmokeTime > .8)
    {
        shot(TEXT("-wall-opaque.png"));
        this->controlSmokeStage = 40;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 40 && NOW - this->controlSmokeTime > .4)
    {
        occluder->Tags.Add(TEXT("RPG.CameraFade"));
        this->controlSmokeStage = 41;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 41 && NOW - this->controlSmokeTime > 1)
    {
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
        {
            return;
        }

#endif
        CollectGarbage(RF_NoFlags);
        if (!require(own->cameraOcclusion->IsFaded(occluderShape.Get()), TEXT("wall alpha/GC")))
        {
            return;
        }

        if (!require(Cast<UInstancedStaticMeshComponent>(occluderShape.Get()) &&
                         occluderShape->GetMaterial(0)->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes),
                     TEXT("door instanced fade shader usage")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE door_instanced_fade"));
        logReveal(TEXT("wall"));
        shot(TEXT("-wall-reveal.png"));
        auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
        FVector2D screen;
        pc->ProjectWorldLocationToScreen(own->GetActorLocation() + FVector(0, -120, 2), screen);
        pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        this->controlSmokeStage = 42;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 42 && NOW - this->controlSmokeTime > .2)
    {
        auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
        FVector start, direction, ground;
        FHitResult raw;
        FVector2D screen;
        pc->ProjectWorldLocationToScreen(own->GetActorLocation() + FVector(0, -120, 2), screen);
        pc->SetMouseLocation(FMath::RoundToInt(screen.X), FMath::RoundToInt(screen.Y));
        pc->DeprojectMousePositionToWorld(start, direction);
        GetWorld()->LineTraceSingleByChannel(raw, start, start + direction * 50000, ECC_Pawn);
        const bool HAS_GROUND = CursorGround(ground);
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE ground_probe raw=%s faded=%d ground=%d xyz=%.1f,%.1f,%.1f"),
               *GetNameSafe(raw.GetComponent()), own->cameraOcclusion->IsFaded(raw.GetComponent()), HAS_GROUND,
               ground.X, ground.Y, ground.Z);
        if (!require(raw.bBlockingHit && own->cameraOcclusion->IsFaded(raw.GetComponent()) && HAS_GROUND &&
                         FMath::Abs(ground.Z) < 3,
                     TEXT("click through faded wall to floor")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE wall_alpha_ground x=%.1f y=%.1f z=%.1f faded=%d"), ground.X,
               ground.Y, ground.Z, own->cameraOcclusion->FadedCount());
        expectedGround = ground;
        pc->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
        // Consume this real controller event with the same viewport cursor
        // snapshot. External OS mouse events must not change the fixture goal
        // between the trace assertion and the following core ticker pass.
        TickControl(0);
        this->controlSmokeStage = 43;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 43 && NOW - this->controlSmokeTime > .08)
    {
        GetGameInstance()->GetFirstLocalPlayerController()->InputKey(
            FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
        // Read the accepted route endpoint after the control tick consumes the
        // mouse event (the OS cursor/cached camera can advance by one frame).
        if (!this->clickPath.empty())
        {
            const auto GOAL = this->clickPath.back();
            expectedGround = FVector(GOAL.x, GOAL.y, GOAL.z);
        }

        this->controlSmokeStage = 44;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 44 && NOW - this->controlSmokeTime > .8 && this->pendingInputs.empty() &&
        this->clickPath.empty())
    {
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE click_destination feet=%d,%d goal=%.1f,%.1f"),
               this->predicted.x, this->predicted.y, expectedGround.X, expectedGround.Y);
        if (!require(FVector::Dist2D(FVector(this->predicted.x, this->predicted.y, this->predicted.z), expectedGround) <
                             5 &&
                         !this->ownCast,
                     TEXT("real click moved to selected floor")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE wall_alpha_click_move feet=%d,%d,%d"), this->predicted.x,
               this->predicted.y, this->predicted.z);
        logReveal(TEXT("clicked"));
        shot(TEXT("-clicked-reveal.png"));
        this->controlSmokeStage = 45;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 45 && NOW - this->controlSmokeTime > .4)
    {
        own->arm->SetWorldRotation(FRotator(-40, 90, 0));
        this->controlSmokeStage = 46;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 46 && NOW - this->controlSmokeTime > .8)
    {
        auto* component = occluderShape.Get();
        if (!require(!own->cameraOcclusion->IsFaded(component) &&
                         component->GetMaterial(0)->GetName() == TEXT("WorldGridMaterial") && component->CastShadow,
                     TEXT("automatic wall restore")))
        {
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE wall_auto_restore"));
        own->arm->SetWorldRotation(FRotator(-65, 180, 0));
        this->controlSmokeStage = 47;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 47 && NOW - this->controlSmokeTime > .8)
    {
        if (!require(own->cameraOcclusion->IsFaded(occluderShape.Get()), TEXT("wall reentry")))
        {
            return;
        }

        occluder->Tags.Remove(TEXT("RPG.CameraFade"));
        own->SetLocalVisibility(false);
        auto* component = occluderShape.Get();
        if (!require(component->GetMaterial(0)->GetName() == TEXT("WorldGridMaterial") &&
                         component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block &&
                         component->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore && component->CastShadow,
                     TEXT("wall restore")))
        {
            return;
        }

        if (!require(own->cameraOcclusion->FadedCount() == 0 && !own->arm->IsComponentTickEnabled(),
                     TEXT("local visibility release")))
        {
            return;
        }

        own->SetLocalVisibility(true);
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE wall_restore"));
        this->controlSmokeStage = 5;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > .4)
    {
        if (occluder.IsValid())
        {
            occluder->Destroy();
        }

        own->arm->SetWorldRotation(FRotator(-55, 0, 0));
        if (RequestMoveTo(FVector(-1000, -8400, 0)))
        {
            this->controlSmokeStage = 6;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 6 && this->predicted.y == -8400 && this->pendingInputs.empty())
    {
        if (RequestMoveTo(FVector(-450, -8400, 0)))
        {
            this->controlSmokeStage = 7;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 7 && this->map->activeRoom == 6 && this->npcs.Num() == 1)
    {
        for (const auto& n : this->npcs)
        {
            if (!n.Value->AppearanceReady())
            {
                return;
            }

        }

        if (RequestMoveTo(FVector(1280, -8400, 0)))
        {
            this->controlSmokeStage = 8;
            this->controlSmokeTime = NOW;
        }

        return;
    }

    if (this->controlSmokeStage == 8 && this->predicted.x == 1280 && this->pendingInputs.empty())
    {
        own->arm->TargetArmLength = 2000;
        own->arm->SetWorldRotation(FRotator(-25, 180, 0));
        for (const auto& n : this->npcs)
        {
            this->combatTarget = n.Key;
        }

        this->controlSmokeStage = 9;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 9 && NOW - this->controlSmokeTime > 1)
    {
        auto* mesh = own->FindComponentByClass<USkeletalMeshComponent>();
        CollectGarbage(RF_NoFlags);
        if (!require(mesh && mesh->bRenderCustomDepth && mesh->CustomDepthStencilValue == 241 &&
                         IsValid(own->GetVisibilityMaterial()),
                     TEXT("GC/stencil")))
        {
            return;
        }

        logReveal(TEXT("boss"));
        shot(TEXT("-boss-reveal.png"));
        if (!require(this->controlHud && this->controlHud->ClickQuickSlotForTest(2), TEXT("framed quickslot click")))
        {
            return;
        }

        CancelCombatIntent();
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE quickslot_click"));
        this->controlSmokeStage = 10;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 10 && NOW - this->controlSmokeTime > .5)
    {
        // Presentation-only fixture: load the normal item catalog so empty
        // bag icons initialize normally. No economy RPC or persistent mutation.
        if (!this->itemCatalog)
        {
            this->itemCatalog = std::make_shared<const shared_world::economy::Catalog>(
                shared_world::economy::Catalog::Load(std::filesystem::u8path(
                    TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Items"))))));
        }

        this->inventoryUi = CreateWidget<UWorldInventoryUI>(GetGameInstance()->GetFirstLocalPlayerController(),
                                                            UWorldInventoryUI::StaticClass());
        this->inventoryUi->AddToViewport(100);
        this->controlSmokeStage = 11;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 11 && NOW - this->controlSmokeTime > 1)
    {
        shot(TEXT("-inventory.png"));
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE inventory_frame"));
        this->controlSmokeStage = 12;
        this->controlSmokeTime = NOW;
        return;
    }

    if (this->controlSmokeStage == 12 && NOW - this->controlSmokeTime > 1)
    {
        this->inventoryUi->RemoveFromParent();
        this->inventoryUi = nullptr;
        UE_LOG(LogTemp, Display, TEXT("VISIBILITY_SMOKE passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }

}

#endif
