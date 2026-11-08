#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetSwitcher.h"
#include "Characters/CharacterPreviewActor.h"
#include "Managers/CharacterManager.h"
#include "UIs/Lobby/LobbyScreens.h"
#include "UIs/Title/HeroSlot.h"
#include "TitleUIController.generated.h"

// Kept as the native type for existing caller compatibility; asset is WBP_LobbyRoot.
UCLASS()

class RPG_API UTitleUIController : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION()
    void ShowHeroSelectionUI();
    void SetGuestLoginEnabled(bool enabled);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UWidgetSwitcher> screenSwitcher;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<ULobbyLoginScreen> loginScreen;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<ULobbyCharacterSelectScreen> characterSelectScreen;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<ULobbyCharacterCreateScreen> characterCreateScreen;
    UPROPERTY(EditDefaultsOnly, Category = "Lobby")
    TSubclassOf<UHeroSlot> characterSlotClass;

private:
    friend class FLobbyPresentationTest;
    UCharacterManager* Characters() const;
    void SetScreen(UUserWidget* screen);
    void SetLoginStatus(const FText& text);
    void UpdateCreationControls();
    ACharacterPreviewActor* FindCharacterPreview();
    void ShowSelectedCharacterPreview();
    void HideCharacterPreview();
    void BindButtons(bool bind);
    void UpdateEntryControls();
    UFUNCTION()
    void Connect();
    UFUNCTION()
    void NaverLogin();
    UFUNCTION()
    void CancelLogin();
    UFUNCTION()
    void Logout();
    UFUNCTION()
    void HandleLoginStatus(const FString& status);
    UFUNCTION()
    void HandleLoginFailed(const FString& reason);
    void ConnectSelected();
    UFUNCTION()
    void ShowLoginUI();
    UFUNCTION()
    void GuestLogin();
    void LoginTimedOut();
    FTimerHandle loginTimer;
    UFUNCTION()
    void RefreshCharacters();
    UFUNCTION()
    void RebuildCharacterList();
    UFUNCTION()
    void HandleListState(ECharacterListState state);
    UFUNCTION()
    void SelectCharacter(int64 characterId);
    UFUNCTION()
    void EnterGame(int64 characterId);
    UFUNCTION()
    void HandleWorldStatus(const FString& status);
    UFUNCTION()
    void ShowCreateScreen();
    UFUNCTION()
    void HandleGenderSelectionChanged(FString selectedItem, ESelectInfo::Type selectionType);
    UFUNCTION()
    void HandlePreviewStateChanged(ECharacterPreviewLoadState state);
    UFUNCTION()
    void SubmitCreation();
    UFUNCTION()
    void ConfirmPendingCreation();
    UFUNCTION()
    void BackToSelection();
    UFUNCTION()
    void HandleCreationState(ECharacterCreationState state);
    UFUNCTION()
    void HandleCreationCompleted(const FString& operationId, ECharacterCreationResult result,
                                 const FCharacterSummary& character);
    UPROPERTY(Transient)
    TArray<TObjectPtr<UHeroSlot>> characterSlots;
    UPROPERTY(Transient)
    TWeakObjectPtr<ACharacterPreviewActor> characterPreview;
    int64 selectedCharacterId = 0;
    bool loginPending = false;
    bool naverSelected = false;
    bool screenTearingDown = false;
};

