#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SharedWorld/Dungeon/DungeonExits.h"
#include "WorldExitZones.generated.h"
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

UCLASS()

class RPG_API AWorldExitZone : public AActor
{
    GENERATED_BODY()
public:
    AWorldExitZone();
    void Present(const shared_world::dungeon::Zone& zone);
    void Release();
    void FaceLabel(FVector cameraLocation);
    int32 RingInstanceCount() const;

private:
    UPROPERTY()
    TObjectPtr<UInstancedStaticMeshComponent> ring;
    UPROPERTY()
    TObjectPtr<UTextRenderComponent> label;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> material;
};

UCLASS()

class RPG_API UWorldExitPool : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool Present(const shared_world::dungeon::Layout& layout);
    void ReleaseAll();
    void PresentDoors(unsigned activeRoom);
    void FaceLabels(FVector cameraLocation);
    int32 DoorInstanceCount() const;

    int32 AllocatedCount() const
    {
        return this->actors.Num();
    }

    AWorldExitZone* GetZone(int32 index) const
    {
        return this->actors.IsValidIndex(index) ? this->actors[index].Get() : nullptr;
    }

    virtual void Deinitialize() override;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldExitZone>> actors;
    UPROPERTY(Transient)
    TObjectPtr<AActor> doorActor;
    UPROPERTY(Transient)
    TObjectPtr<UInstancedStaticMeshComponent> doors;
};

