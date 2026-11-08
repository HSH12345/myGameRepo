#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SharedWorld/Entity/EntityStats.h"
#include "Components/Button.h"
#include "WorldControlHUD.generated.h"
class UTextBlock;
class UImage;
class UCanvasPanel;
class UBorder;
class UButton;
class UProgressBar;
class UWorldMinimap;
class UWorldDungeonQueueUI;
class UWorldControlHUD;
UCLASS()

class RPG_API UWorldQuickSlotButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient)
    TObjectPtr<UWorldControlHUD> ownerHud;
    int32 index = 0;
    bool registerValue = false, bindingOption = false, train = false, potionSlot = false, potionRegister = false;
    uint32 binding = 0;
    UFUNCTION()
    void Activate();
};

UCLASS()

class RPG_API UWorldControlHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    void ShowCombat(const FString& target, const FString& action, const FString& slots, float progress);
    void ShowStats(const shared_world::EntityStats& stats);
    void ShowEnemy(const FString& name, int32 hp, int32 maxHp);
    void ActivateQuickSlot(int32 index);
    void ToggleQuickSlotPicker(int32 index);
    void HideQuickSlotPicker();
    void BindQuickSlotFromPicker(uint32 binding);
    bool IsPointerOverHud() const;
#if PLATFORM_ANDROID
    bool IsScreenPositionOverHud(const FVector2D& position) const;
#endif
    bool IsSkillBookVisible() const;
    void TrainFromBook(uint32 skill);
    void TogglePotionPicker(int32 index);
    void RegisterPotionFromPicker(uint32 type);
    void RefreshSkillBook();
    void BuildSkillBook(UCanvasPanel* canvas);
    void BuildAutoPotions(UCanvasPanel* canvas);
    void ShowNpcDialogue(const FString& name, const FString& text, const FString& option);
    void HideNpcDialogue();
    void SetNpcWarpPending();
    bool IsNpcDialogueVisible() const;
#if !UE_BUILD_SHIPPING
    bool ClickNpcWarpForTest();
    bool ClickDungeonReadyForTest();
    bool ClickQuickSlotForTest(int32 index);
    bool CombatHudReadyForTest() const;
    bool OpenQuickSlotPickerForTest(int32 index);
    bool ClickBindingForTest(uint32 binding);
    bool ClickTrainForTest(uint32 skill);
    bool ClickPotionRegisterForTest(int32 index, uint32 type);
    bool QuickIconsInsideForTest() const;
    bool QuickGridForTest() const;
    bool QuickSelectionCoversSlotForTest(int32 index) const;
#endif
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;
    bool IsAbsolutePositionOverHud(const FVector2D& position) const;
    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> enemyBar;
    UPROPERTY(Transient)
    TObjectPtr<UBorder> enemyPanel;
    UPROPERTY(Transient)
    TObjectPtr<UWorldMinimap> minimap;
    void BuildQuickSlots(UCanvasPanel* canvas);
    UPROPERTY(Transient)
    TObjectPtr<UBorder> quickSlotPicker;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> pickerCaption;
    int32 pickerSlot = 0, pickerPotion = 0;
    uint32 pendingSkillBinding = 0;
    UPROPERTY(Transient)
    TObjectPtr<UBorder> potionPicker;
    UPROPERTY(Transient)
    TObjectPtr<class UVerticalBox> potionChoices;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UWorldQuickSlotButton>> trainButtons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> skillLabels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> trainLabels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> skillGlows;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> potionIcons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> potionLabels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UWorldQuickSlotButton>> registerButtons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UWorldQuickSlotButton>> bindingButtons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> quickIcons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UWorldQuickSlotButton>> quickButtons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> quickLabels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UWidget>> pointerRegions;
    UPROPERTY(Transient)
    TObjectPtr<UWorldDungeonQueueUI> dungeonQueue;
    float hudElapsed = 0;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> quickSelection;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> combatTargetText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> combatActionText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> combatSlotsText;
    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> castBar;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> hpText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> mpText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> baseStatsText;
    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> hpBar;
    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> mpBar;
    UPROPERTY(Transient)
    TObjectPtr<UBorder> dialoguePanel;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> dialogueName;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> dialogueText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> warpLabel;
    UPROPERTY(Transient)
    TObjectPtr<UButton> warpButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> closeButton;
    UFUNCTION()
    void WarpClicked();
    UFUNCTION()
    void InteractClicked();
    UFUNCTION()
    void CloseClicked();
};

