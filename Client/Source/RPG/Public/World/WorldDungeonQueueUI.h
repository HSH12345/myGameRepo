#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SharedWorld/Network/WorldProtocol.h"
#include "WorldDungeonQueueUI.generated.h"

class UTextBlock;
class UButton;
class UImage;

UCLASS()

class RPG_API UWorldDungeonQueueUI : public UUserWidget
{
    GENERATED_BODY()
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDungeonWaitingRoomTest;
#endif

public:
    void Refresh(const shared_world::net::Message& state, uint64 ownEntity, double countdown);
#if !UE_BUILD_SHIPPING
    bool ClickReadyForTest();
#endif

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UFUNCTION()
    void ToggleReady();
    UFUNCTION()
    void Leave();
    bool ownReady = false;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> countText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> countdownText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> titleText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> hintText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> readyText;
    UPROPERTY(Transient)
    TObjectPtr<UButton> readyButton;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> memberNames;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> memberStates;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> memberPortraits;
};

