#include "Characters/CharacterPreviewActor.h"

#include "Algo/AllOf.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Characters/CharacterAppearanceDefinition.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

ACharacterPreviewActor::ACharacterPreviewActor()
{
    this->PrimaryActorTick.bCanEverTick = true;
    this->PrimaryActorTick.bStartWithTickEnabled = false;

    this->sceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(this->sceneRoot);

    this->previewMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
    this->previewMeshComponent->SetupAttachment(this->sceneRoot);
    this->previewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->previewMeshComponent->SetGenerateOverlapEvents(false);
    this->previewMeshComponent->SetVisibility(false);
    this->previewMeshComponent->VisibilityBasedAnimTickOption =
        EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    this->previewMeshComponent->bEnableUpdateRateOptimizations = true;

    this->previewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PreviewCamera"));
    this->previewCamera->SetupAttachment(this->sceneRoot);
    // The creation form occupies the right side; frame the full body in the left half.
    this->previewCamera->SetRelativeLocation(FVector(150.0, 500.0, 95.0));
    this->previewCamera->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
    this->previewCamera->FieldOfView = 55.0f;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> cubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    this->backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
    this->backdrop->SetupAttachment(this->sceneRoot);
    this->backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->backdrop->SetRelativeLocation(FVector(0.0, -75.0, 140.0));
    this->backdrop->SetRelativeScale3D(FVector(5.0, 0.05, 3.0));
    this->backdrop->SetVisibility(false);
    if (cubeMesh.Succeeded())
    {
        this->backdrop->SetStaticMesh(cubeMesh.Object);
    }

    this->floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
    this->floor->SetupAttachment(this->sceneRoot);
    this->floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->floor->SetRelativeLocation(FVector(0.0, 0.0, -7.0));
    this->floor->SetRelativeScale3D(FVector(4.0, 5.0, 0.05));
    this->floor->SetVisibility(false);
    if (cubeMesh.Succeeded())
    {
        this->floor->SetStaticMesh(cubeMesh.Object);
    }

    this->keyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
    this->keyLight->SetupAttachment(this->sceneRoot);
    this->keyLight->SetRelativeLocation(FVector(140.0, 150.0, 220.0));
    this->keyLight->SetIntensity(2600.0f);
    this->keyLight->SetAttenuationRadius(500.0f);
    this->keyLight->SetLightColor(FLinearColor(1.0f, 0.82f, 0.68f));
    this->keyLight->SetVisibility(false);

    this->fillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    this->fillLight->SetupAttachment(this->sceneRoot);
    this->fillLight->SetRelativeLocation(FVector(-160.0, 110.0, 150.0));
    this->fillLight->SetIntensity(1200.0f);
    this->fillLight->SetAttenuationRadius(450.0f);
    this->fillLight->SetLightColor(FLinearColor(0.50f, 0.68f, 1.0f));
    this->fillLight->SetCastShadows(false);
    this->fillLight->SetVisibility(false);

    this->rimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
    this->rimLight->SetupAttachment(this->sceneRoot);
    this->rimLight->SetRelativeLocation(FVector(0.0, -100.0, 210.0));
    this->rimLight->SetIntensity(1700.0f);
    this->rimLight->SetAttenuationRadius(400.0f);
    this->rimLight->SetLightColor(FLinearColor(0.38f, 0.48f, 1.0f));
    this->rimLight->SetCastShadows(false);
    this->rimLight->SetVisibility(false);
}

const UCharacterAppearanceDefinition* ACharacterPreviewActor::FindAppearance(uint8 race, uint8 gender) const
{
    const TObjectPtr<UCharacterAppearanceDefinition>* match = this->appearances.FindByPredicate(
        [race, gender](const UCharacterAppearanceDefinition* definition)
        {
            return definition && definition->race == race && definition->gender == gender;
        });

    return match ? match->Get() : nullptr;
}

TArray<FSoftObjectPath> ACharacterPreviewActor::GatherAppearanceAssets(uint8 race) const
{
    TArray<FSoftObjectPath> result;
    for (const UCharacterAppearanceDefinition* definition : this->appearances)
    {
        if (!definition || definition->race != race)
        {
            continue;
        }

        if (!definition->previewMesh.IsNull())
        {
            result.AddUnique(definition->previewMesh.ToSoftObjectPath());
        }

        if (!definition->previewIdle.IsNull())
        {
            result.AddUnique(definition->previewIdle.ToSoftObjectPath());
        }

        for (const TSoftObjectPtr<USkeletalMesh>& part : definition->additionalMeshes)
        {
            if (!part.IsNull())
            {
                result.AddUnique(part.ToSoftObjectPath());
            }

        }

    }

    return result;
}

void ACharacterPreviewActor::CacheResolvedAppearanceAssets(const TArray<FSoftObjectPath>& assetPaths)
{
    for (const FSoftObjectPath& path : assetPaths)
    {
        if (UObject* asset = path.ResolveObject())
        {
            this->cachedAppearanceAssets.AddUnique(asset);
        }

    }

}

bool ACharacterPreviewActor::ShowAppearance(uint8 race, uint8 gender)
{
    return RequestAppearance(race, gender, true);
}

void ACharacterPreviewActor::BeginPlay()
{
    Super::BeginPlay();
    RequestAppearance(0, 0, false); // Lobby entry, not the first gender click.
}

bool ACharacterPreviewActor::RequestAppearance(uint8 race, uint8 gender, bool show)
{
    const UCharacterAppearanceDefinition* definition = FindAppearance(race, gender);
    if (!definition || definition->previewMesh.IsNull() || definition->previewIdle.IsNull())
    {
        UE_LOG(LogTemp, Error, TEXT("No complete preview definition for race=%u gender=%u."), race, gender);
        SetPreviewState(ECharacterPreviewLoadState::FAILED_VALUE);
        return false;
    }

    this->requestedRace = race;
    this->requestedGender = gender;
    this->wantsPreviewVisible = show;
    if (this->preparedVariants.ContainsByPredicate(
            [definition](const FPreparedCharacterPreview& variant)
            {
                return variant.definition == definition && variant.prepared;
            }))

    {
        DisplayRequestedVariant();
        return true;
    }

    if (this->activeLoad.IsValid())
    {
        if (this->loadingRace == race)
        {
            if (show)
            {
                SetPreviewState(ECharacterPreviewLoadState::LOADING);
            }

            return true;
        }

        this->activeLoad->CancelHandle();
        this->activeLoad.Reset();
    }

    const TArray<FSoftObjectPath> ASSETS_TO_LOAD = GatherAppearanceAssets(race);
    const uint64 GENERATION = ++this->loadGeneration;

    // The first create-screen entry loads every gender for this race in one batch.
    const bool B_ALL_ASSETS_ALREADY_LOADED = Algo::AllOf(ASSETS_TO_LOAD,
                                                         [](const FSoftObjectPath& path)
                                                         {
                                                             return path.ResolveObject() != nullptr;
                                                         });

    if (B_ALL_ASSETS_ALREADY_LOADED)
    {
        CacheResolvedAppearanceAssets(ASSETS_TO_LOAD);
        FinishAppearanceLoad(GENERATION, definition);
        return this->previewState != ECharacterPreviewLoadState::FAILED_VALUE;
    }

    this->loadingRace = race;
    if (show)
    {
        SetPreviewState(ECharacterPreviewLoadState::LOADING);
    }

    this->activeLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        ASSETS_TO_LOAD,
        FStreamableDelegate::CreateUObject(this, &ACharacterPreviewActor::FinishAppearancePreload, GENERATION, race));
    if (!this->activeLoad.IsValid())
    {
        SetPreviewState(ECharacterPreviewLoadState::FAILED_VALUE);
        return false;
    }

    return true;
}

void ACharacterPreviewActor::FinishAppearancePreload(uint64 generation, uint8 race)
{
    if (generation != this->loadGeneration || race != this->requestedRace)
    {
        return;
    }

    const TArray<FSoftObjectPath> ASSET_PATHS = GatherAppearanceAssets(race);
    const bool B_COMPLETE = Algo::AllOf(ASSET_PATHS,
                                        [](const FSoftObjectPath& path)
                                        {
                                            return path.ResolveObject() != nullptr;
                                        });

    if (!B_COMPLETE)
    {
        this->activeLoad.Reset();
        UE_LOG(LogTemp, Error, TEXT("The appearance cache did not finish loading for race=%u."), race);
        HidePreview();
        SetPreviewState(ECharacterPreviewLoadState::FAILED_VALUE);
        return;
    }

    CacheResolvedAppearanceAssets(ASSET_PATHS);
    // Establish GC ownership before releasing the streaming handle.
    this->activeLoad.Reset();
    FinishAppearanceLoad(generation, FindAppearance(this->requestedRace, this->requestedGender));
}

void ACharacterPreviewActor::FinishAppearanceLoad(uint64 generation, const UCharacterAppearanceDefinition* definition)
{
    if (generation != this->loadGeneration || !definition || definition->race != this->requestedRace ||
        definition->gender != this->requestedGender)
    {
        return; // A faster, newer gender selection owns the actor now.
    }

    for (const UCharacterAppearanceDefinition* candidate : this->appearances)
    {
        if (candidate && candidate->race == definition->race && !BuildVariant(candidate))
        {
            SetPreviewState(ECharacterPreviewLoadState::FAILED_VALUE);
            return;
        }

    }

    if (this->wantsPreviewVisible)
    {
        SetPreviewState(ECharacterPreviewLoadState::PREPARING);
    }

    SetActorTickEnabled(true);
}

bool ACharacterPreviewActor::BuildVariant(const UCharacterAppearanceDefinition* definition)
{
    if (this->preparedVariants.ContainsByPredicate(
            [definition](const FPreparedCharacterPreview& variant)
            {
                return variant.definition == definition;
            }))

    {
        return true;
    }

    USkeletalMesh* mesh = definition->previewMesh.Get();
    UAnimSequence* idle = definition->previewIdle.Get();
    if (!mesh || !mesh->GetSkeleton() || !idle || !idle->GetSkeleton())
    {
        UE_LOG(LogTemp, Error, TEXT("Preview assets are missing a mesh or skeleton for race=%u gender=%u."),
               definition->race, definition->gender);
        return false;
    }

    TArray<USkeletalMesh*> loadedParts;
    loadedParts.Reserve(definition->additionalMeshes.Num());
    for (const TSoftObjectPtr<USkeletalMesh>& partReference : definition->additionalMeshes)
    {
        USkeletalMesh* partMesh = partReference.Get();
        if (!partMesh)
        {
            UE_LOG(LogTemp, Error, TEXT("A preview mesh part failed to load for race=%u gender=%u."), definition->race,
                   definition->gender);
            return false;
        }

        loadedParts.Add(partMesh);
    }

    FPreparedCharacterPreview& variant = this->preparedVariants.AddDefaulted_GetRef();
    variant.definition = const_cast<UCharacterAppearanceDefinition*>(definition);
    auto makeMesh = [this](USceneComponent* parent)
    {
        USkeletalMeshComponent* component = NewObject<USkeletalMeshComponent>(this);
        component->SetupAttachment(parent);
        component->SetVisibility(false);
        component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        component->SetGenerateOverlapEvents(false);
        component->bEnableUpdateRateOptimizations = false;
        component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        component->RegisterComponent();
        component->SetComponentTickEnabled(false);
        return component;
    };

    variant.body = makeMesh(this->sceneRoot);
    variant.body->SetRelativeTransform(definition->meshTransform);
    variant.body->SetSkeletalMesh(mesh);
    variant.body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    variant.body->PlayAnimation(idle, true);
    for (USkeletalMesh* partMesh : loadedParts)
    {
        USkeletalMeshComponent* part = makeMesh(variant.body);
        part->SetSkeletalMesh(partMesh);
        part->SetLeaderPoseComponent(variant.body);
        variant.parts.Add(part);
    }

    return true;
}

void ACharacterPreviewActor::Tick(float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    for (FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        if (variant.prepared)
        {
            continue;
        }

        variant.body->TickAnimation(0.0f, false);
        // No tick-function argument: finish evaluation on the game thread before publishing.
        variant.body->RefreshBoneTransforms();
        variant.prepared = variant.body->GetNumComponentSpaceTransforms() > 0;
        if (!variant.prepared)
        {
            SetActorTickEnabled(false);
            SetPreviewState(ECharacterPreviewLoadState::FAILED_VALUE);
            return;
        }

    }

    SetActorTickEnabled(false);
    DisplayRequestedVariant();
}

void ACharacterPreviewActor::SetVariantVisible(FPreparedCharacterPreview& variant, bool visible)
{
    variant.body->SetVisibility(visible, true);
    variant.body->SetComponentTickEnabled(visible);
}

void ACharacterPreviewActor::DisplayRequestedVariant()
{
    if (!this->wantsPreviewVisible)
    {
        return;
    }

    FPreparedCharacterPreview* selected = this->preparedVariants.FindByPredicate(
        [this](const FPreparedCharacterPreview& variant)
        {
            return variant.definition->race == this->requestedRace &&
                   variant.definition->gender == this->requestedGender && variant.prepared;
        });

    if (!selected)
    {
        return;
    }

    // Resume the retained pose; never reset mesh or animation on gender changes.
    selected->body->TickAnimation(0.0f, false);
    selected->body->RefreshBoneTransforms();
    for (FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        SetVariantVisible(variant, &variant == selected);
    }

    this->displayedRace = this->requestedRace;
    this->displayedGender = this->requestedGender;
    if (APlayerController* player = this->activePlayerController.Get())
    {
        ActivateForPlayer(player);
    }

    SetPreviewState(ECharacterPreviewLoadState::READY);
}

int32 ACharacterPreviewActor::GetPreparedVariantCount() const
{
    int32 count = 0;
    for (const FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        count += variant.prepared ? 1 : 0;
    }

    return count;
}

const USkeletalMeshComponent* ACharacterPreviewActor::GetVariantBody(uint8 race, uint8 gender) const
{
    for (const FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        if (variant.definition->race == race && variant.definition->gender == gender)
        {
            return variant.body;
        }

    }

    return nullptr;
}

void ACharacterPreviewActor::HidePreview()
{
    this->wantsPreviewVisible = false;
    for (FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        SetVariantVisible(variant, false);
    }

    this->previewMeshComponent->SetVisibility(false, true);
    this->previewMeshComponent->SetComponentTickEnabled(false);
    for (USkeletalMeshComponent* part : this->additionalMeshComponents)
    {
        if (part)
        {
            part->SetVisibility(false, true);
        }

    }

    SetPreviewState(ECharacterPreviewLoadState::HIDDEN);
}

void ACharacterPreviewActor::ActivateForPlayer(APlayerController* playerController)
{
    if (!playerController)
    {
        return;
    }

    if (this->activePlayerController.Get() != playerController)
    {
        this->previousViewTarget = playerController->GetViewTarget();
        this->activePlayerController = playerController;
    }

    const bool B_READY_TO_DISPLAY = this->preparedVariants.ContainsByPredicate(
        [this](const FPreparedCharacterPreview& variant)
        {
            return variant.prepared && variant.body->IsVisible() && this->wantsPreviewVisible;
        });

    if (!B_READY_TO_DISPLAY)
    {
        return;
    }

    if (playerController->GetViewTarget() != this)
    {
        playerController->SetViewTargetWithBlend(this, 0.2f);
    }

    this->backdrop->SetVisibility(true, true);
    this->floor->SetVisibility(true, true);
    this->keyLight->SetVisibility(true, true);
    this->fillLight->SetVisibility(true, true);
    this->rimLight->SetVisibility(true, true);
}

void ACharacterPreviewActor::DeactivateForPlayer()
{
    if (APlayerController* playerController = this->activePlayerController.Get())
    {
        AActor* restoreTarget = this->previousViewTarget.Get();
        if (playerController->GetViewTarget() == this && restoreTarget && restoreTarget != this)
        {
            playerController->SetViewTargetWithBlend(restoreTarget, 0.15f);
        }

    }

    this->activePlayerController.Reset();
    this->previousViewTarget.Reset();
    this->backdrop->SetVisibility(false, true);
    this->floor->SetVisibility(false, true);
    this->keyLight->SetVisibility(false, true);
    this->fillLight->SetVisibility(false, true);
    this->rimLight->SetVisibility(false, true);
    HidePreview();
}

void ACharacterPreviewActor::RotatePreview(float deltaYawDegrees)
{
    for (FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        if (variant.body->IsVisible())
        {
            variant.body->AddRelativeRotation(FRotator(0.0, deltaYawDegrees, 0.0));
        }

    }

}

void ACharacterPreviewActor::ResetPreviewRotation()
{
    for (FPreparedCharacterPreview& variant : this->preparedVariants)
    {
        variant.body->SetRelativeRotation(variant.definition->meshTransform.Rotator());
    }

}

void ACharacterPreviewActor::SetPreviewState(ECharacterPreviewLoadState newState)
{
    if (this->previewState == newState)
    {
        return;
    }

    this->previewState = newState;
    this->onPreviewStateChanged.Broadcast(newState);
}

void ACharacterPreviewActor::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    DeactivateForPlayer();
    ++this->loadGeneration;
    if (this->activeLoad.IsValid())
    {
        this->activeLoad->CancelHandle();
    }

    this->activeLoad.Reset();
    SetActorTickEnabled(false);
    Super::EndPlay(endPlayReason);
}

