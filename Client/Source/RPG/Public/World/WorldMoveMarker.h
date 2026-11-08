#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldMoveMarker.generated.h"
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Local presentation only. Two actors are prewarmed per loaded world and reused.
UCLASS()

class RPG_API AWorldMoveMarker : public AActor
{
    GENERATED_BODY()
public:
    AWorldMoveMarker();
    virtual void BeginPlay() override;
    virtual void Tick(float deltaSeconds) override;
    void Activate(FVector location, FVector normal, bool confirmed);
    void Confirm(FVector location, FVector normal);
    void Release();

    bool IsActive() const
    {
        return this->active;
    }

private:
    UPROPERTY()
    TObjectPtr<UInstancedStaticMeshComponent> shape;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> material;
    float age = 0;
    bool active = false;
};

