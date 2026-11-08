#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapCollisionDebugActor.generated.h"
struct FMapCollisionDebugData;

UCLASS()

class RPG_API AMapCollisionDebugActor : public AActor
{
    GENERATED_BODY()
public:
    AMapCollisionDebugActor();
    virtual ~AMapCollisionDebugActor() override;
    UPROPERTY(EditAnywhere, Category = "Map Collision")
    FString mapFilename;
    UPROPERTY(EditAnywhere, Category = "Map Collision")
    bool displayCollision = true;
    UPROPERTY(EditAnywhere, Category = "Map Collision", meta = (ClampMin = "100", ClampMax = "10000"))
    float displayRadius = 3000;
    UFUNCTION(CallInEditor, Category = "Map Collision")
    void ReloadMapData();
    virtual void Tick(float deltaSeconds) override;

    virtual bool ShouldTickIfViewportsOnly() const override
    {
        return true;
    }

protected:
    virtual void BeginPlay() override;

private:
    TSharedPtr<FMapCollisionDebugData> data;
    FString attemptedFilename;
};

