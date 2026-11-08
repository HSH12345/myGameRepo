#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WorldCameraOcclusion.generated.h"
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USpringArmComponent;

USTRUCT()

struct FWorldFadedWall
{
    GENERATED_BODY()
    UPROPERTY()
    TWeakObjectPtr<UPrimitiveComponent> component;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> originals;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> material;
    float opacity = 1;
    double lastSeen = 0;
    ECollisionResponse cameraResponse = ECR_Block;
    bool shadow = true;
};

// Presentation/query policy only. Pawn and authoritative collision stay intact.
UCLASS()

class RPG_API UWorldCameraOcclusion : public UActorComponent
{
    GENERATED_BODY()
public:
    UWorldCameraOcclusion();
    void Update(USpringArmComponent* arm, float delta);
    void Clear();
    bool TraceVisibleScene(FHitResult& hit, const FVector& start, const FVector& end) const;
    bool TraceGround(FHitResult& hit, const FVector& start, const FVector& end) const;
    static constexpr ECollisionChannel GROUND_CHANNEL = ECC_GameTraceChannel1;
    static void ConfigureGeometry(UPrimitiveComponent* component);
    bool PresentationReady() const;

    int32 FadedCount() const
    {
        return this->walls.Num();
    }

    bool IsFaded(const UPrimitiveComponent* component) const;

protected:
    virtual void EndPlay(const EEndPlayReason::Type reason) override;

private:
    void Restore(FWorldFadedWall& wall);
    UPROPERTY(Transient)
    TArray<FWorldFadedWall> walls;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> fadeMaterial;
    double nextProbe = 0;
    bool probed = false;
};

