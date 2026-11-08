#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldEntityPool.generated.h"
class AWorldPlayerPawn;

// A pool belongs to a UWorld. OpenLevel destroys that world's actors naturally.
UCLASS()

class RPG_API UWorldEntityPool : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    AWorldPlayerPawn* Acquire(const FString& appearanceKey);
    void Release(AWorldPlayerPawn* pawn);

    int32 AllocatedCount() const
    {
        return this->actors.Num();
    }

    int32 ActiveCount() const;
    virtual void Deinitialize() override;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldPlayerPawn>> actors;
};

