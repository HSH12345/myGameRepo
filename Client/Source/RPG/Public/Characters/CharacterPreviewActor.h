#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CharacterPreviewActor.generated.h"

class APlayerController;
class UCameraComponent;
class UCharacterAppearanceDefinition;
class UPointLightComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class ECharacterPreviewLoadState : uint8
{
    HIDDEN,
    LOADING,
    READY,
    FAILED_VALUE,
    PREPARING
};

USTRUCT()

struct FPreparedCharacterPreview
{
    GENERATED_BODY()
    UPROPERTY()
    TObjectPtr<UCharacterAppearanceDefinition> definition;
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> body;
    UPROPERTY()
    TArray<TObjectPtr<USkeletalMeshComponent>> parts;
    bool prepared = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCharacterPreviewStateChanged, ECharacterPreviewLoadState, State);

// One reusable actor renders every lobby appearance. It owns no game character state.
UCLASS(Blueprintable)

class RPG_API ACharacterPreviewActor : public AActor
{
    GENERATED_BODY()

public:
    ACharacterPreviewActor();

    UFUNCTION(BlueprintCallable, Category = "Character Preview")
    bool ShowAppearance(uint8 race, uint8 gender);

    UFUNCTION(BlueprintCallable, Category = "Character Preview")
    void HidePreview();

    UFUNCTION(BlueprintCallable, Category = "Character Preview")
    void ActivateForPlayer(APlayerController* playerController);

    UFUNCTION(BlueprintCallable, Category = "Character Preview")
    void DeactivateForPlayer();

    UFUNCTION(BlueprintCallable, Category = "Character Preview")
    void RotatePreview(float deltaYawDegrees);

    UFUNCTION(BlueprintCallable, Category = "Character Preview")
    void ResetPreviewRotation();

    UFUNCTION(BlueprintPure, Category = "Character Preview")

    ECharacterPreviewLoadState GetPreviewState() const
    {
        return this->previewState;
    }

    UFUNCTION(BlueprintPure, Category = "Character Preview")

    uint8 GetDisplayedRace() const
    {
        return this->displayedRace;
    }

    UFUNCTION(BlueprintPure, Category = "Character Preview")

    uint8 GetDisplayedGender() const
    {
        return this->displayedGender;
    }

    UFUNCTION(BlueprintPure, Category = "Character Preview")

    int32 GetCachedAppearanceAssetCount() const
    {
        return this->cachedAppearanceAssets.Num();
    }

    int32 GetPreparedVariantCount() const;
    const USkeletalMeshComponent* GetVariantBody(uint8 race, uint8 gender) const;

    UPROPERTY(BlueprintAssignable, Category = "Character Preview")
    FCharacterPreviewStateChanged onPreviewStateChanged;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Preview")
    TArray<TObjectPtr<UCharacterAppearanceDefinition>> appearances;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float deltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:
    bool RequestAppearance(uint8 race, uint8 gender, bool show);
    bool BuildVariant(const UCharacterAppearanceDefinition* definition);
    void DisplayRequestedVariant();
    void SetVariantVisible(FPreparedCharacterPreview& variant, bool visible);
    const UCharacterAppearanceDefinition* FindAppearance(uint8 race, uint8 gender) const;
    TArray<FSoftObjectPath> GatherAppearanceAssets(uint8 race) const;
    void CacheResolvedAppearanceAssets(const TArray<FSoftObjectPath>& assetPaths);
    void FinishAppearancePreload(uint64 generation, uint8 race);
    void FinishAppearanceLoad(uint64 generation, const UCharacterAppearanceDefinition* definition);
    void SetPreviewState(ECharacterPreviewLoadState newState);

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<USceneComponent> sceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<USkeletalMeshComponent> previewMeshComponent;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> additionalMeshComponents;

    UPROPERTY(Transient)
    TArray<FPreparedCharacterPreview> preparedVariants;

    bool wantsPreviewVisible = false;

    // Keep both gender variants resident while the lobby preview actor exists.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> cachedAppearanceAssets;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<UCameraComponent> previewCamera;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<UStaticMeshComponent> backdrop;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<UStaticMeshComponent> floor;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<UPointLightComponent> keyLight;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<UPointLightComponent> fillLight;

    UPROPERTY(VisibleAnywhere, Category = "Character Preview")
    TObjectPtr<UPointLightComponent> rimLight;

    UPROPERTY(Transient)
    TWeakObjectPtr<APlayerController> activePlayerController;

    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> previousViewTarget;

    TSharedPtr<FStreamableHandle> activeLoad;
    uint64 loadGeneration = 0;
    uint8 requestedRace = 0;
    uint8 requestedGender = 0;
    uint8 loadingRace = 0;
    uint8 displayedRace = 0;
    uint8 displayedGender = 0;
    ECharacterPreviewLoadState previewState = ECharacterPreviewLoadState::HIDDEN;
    FRotator initialMeshRotation = FRotator::ZeroRotator;
};

