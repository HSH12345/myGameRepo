#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "LobbyScreens.generated.h"

// Layout lives in UMG; the lobby controller owns navigation and requests.
UCLASS()

class RPG_API ULobbyLoginScreen : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> startButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> statusText;
    UPROPERTY(Transient)
    TObjectPtr<UButton> naverLoginButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> cancelAuthButton;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

UCLASS()

class RPG_API ULobbyCharacterSelectScreen : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UScrollBox> characterList;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> createButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> refreshButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> countText;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> statusText;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> selectedCharacterText;
    UPROPERTY(Transient)
    TObjectPtr<UButton> logoutButton;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

UCLASS()

class RPG_API ULobbyCharacterCreateScreen : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UEditableTextBox> characterNameInput;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UComboBoxString> genderChoice;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> submitButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> confirmPendingButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> backButton;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> statusText;
};

