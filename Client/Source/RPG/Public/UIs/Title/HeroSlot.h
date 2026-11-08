#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Managers/LoginManagerModules/Struct_LoginManager.h"
#include "HeroSlot.generated.h"

class UImage;
class UBorder;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCharacterSlotSelected, int64, CharacterId);

UCLASS()

class RPG_API UHeroSlot : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetCharacterData(const FCharacterSummary& character);
    void SetSelected(bool selected);
    void SetEntryEnabled(bool enabled);

    int64 GetCharacterId() const
    {
        return this->summary.characterUnique;
    }

    UPROPERTY(BlueprintAssignable)
    FCharacterSlotSelected onSelected;
    UPROPERTY(BlueprintAssignable)
    FCharacterSlotSelected onEnterRequested;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> selectButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> enterGameButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> characterNameText;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> characterDetailText;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> selectionText;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> portraitImage;

private:
    friend class FLobbyPresentationTest;
    void BuildRow();
    bool isSelected = false;
    UPROPERTY(Transient)
    TObjectPtr<UBorder> selectionOutline;
    UPROPERTY(Transient)
    TObjectPtr<UImage> selectionGlow;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> levelText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> raceText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> genderText;
    UFUNCTION()
    void HandleClick();
    UFUNCTION()
    void HandleEnter();
    UPROPERTY(Transient)
    FCharacterSummary summary;
};

