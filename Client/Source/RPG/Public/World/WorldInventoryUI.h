#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "WorldInventoryUI.generated.h"
class UWorldInventoryUI;
class UTextBlock;
class UVerticalBox;
class UImage;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
UCLASS()

class RPG_API UEconomySlotButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient)
    TObjectPtr<UWorldInventoryUI> ownerUi;
    UPROPERTY(Transient)
    int32 value = 0;
    UPROPERTY(Transient)
    bool offer = false;
    UPROPERTY(Transient)
    bool equipment = false;
    UFUNCTION()
    void Click();
};

UCLASS()

class RPG_API UWorldInventoryUI : public UUserWidget
{
    GENERATED_BODY()
public:
    void Refresh();
    void SlotClicked(int32 value);
    void BuyClicked(int32 value);
    void EquipmentClicked(int32 bag);
    void StopPreviews();
#if !UE_BUILD_SHIPPING
    bool ClickOfferForTest(int32 offer);
    bool FramedTextInsideForTest() const;
    void LongTextForTest();
    bool longTextFixture = false;
    bool ClickActionForTest(int32 bag, int32 action);
    bool ClickMoveForTest(int32 from, int32 to);
#endif
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;
    void UpdateCharacterDetails();
    void UpdatePreviews();
    UFUNCTION()
    void ShowTradeStatus(const FString& status);
    UFUNCTION()
    void EquipClicked();
    UFUNCTION()
    void UnequipClicked();
    UFUNCTION()
    void SplitClicked();
    UFUNCTION()
    void UseClicked();
    UFUNCTION()
    void RegisterClicked();
    UFUNCTION()
    void CloseClicked();
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> summary;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> header;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> feedback;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> selection;
    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> offers;
    UPROPERTY(Transient)
    TObjectPtr<UWidget> shopPanel;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> characterName;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> characterStats;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> merchantName;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> shopGold;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> shopFeedback;
    UPROPERTY(Transient)
    TObjectPtr<UImage> characterImage;
    UPROPERTY(Transient)
    TObjectPtr<UImage> merchantImage;
    UPROPERTY(Transient)
    TObjectPtr<AActor> previewRig;
    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> characterCapture;
    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> merchantCapture;
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> characterTarget;
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> merchantTarget;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UEconomySlotButton>> equipmentSlots;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> equipmentLabels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UEconomySlotButton>> slots;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> labels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UButton>> actions;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> slotSelection;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> slotIcons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> gearIcons;
    int32 selected = -1;
    float previewElapsed = 0;
};

