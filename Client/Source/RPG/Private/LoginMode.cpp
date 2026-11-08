#include "LoginMode.h"
#include "Managers/UIManager.h"
#include "GameFramework/PlayerController.h"

ALoginMode::ALoginMode()
{
    this->DefaultPawnClass = nullptr;
}

void ALoginMode::BeginPlay()
{
    Super::BeginPlay();
    UGameInstance* game = GetGameInstance();
    APlayerController* player = game ? game->GetFirstLocalPlayerController() : nullptr;
    if (!game || !player || !this->lobbyWidgetClass)
    {
        UE_LOG(LogTemp, Error, TEXT("Lobby GameMode requires a local player and LobbyWidgetClass."));
        return;
    }

    UUIManager* ui = game->GetSubsystem<UUIManager>();
    TMap<UIControllerType, TSubclassOf<UUserWidget>> classes;
    classes.Add(UIControllerType::TITLE, this->lobbyWidgetClass);
    ui->SetUIControllers(MoveTemp(classes));
    ui->StartTitle();
    player->bShowMouseCursor = true;
    FInputModeUIOnly inputMode;
    if (UTitleUIController* lobby = ui->GetUIController())
    {
        inputMode.SetWidgetToFocus(lobby->TakeWidget());
    }

    inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    player->SetInputMode(inputMode);
}

