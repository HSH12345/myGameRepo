#include "Managers/UIManager.h"
#include "GameFramework/PlayerController.h"

void UUIManager::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);
}

void UUIManager::Deinitialize()
{
    for (const auto& pair : this->uiControllers)
    {
        if (pair.Value)
        {
            pair.Value->RemoveFromParent();
        }

    }

    this->uiControllers.Reset();
    this->uiControllerClasses.Reset();
    Super::Deinitialize();
}

void UUIManager::SetUIControllers(TMap<UIControllerType, TSubclassOf<UUserWidget>> classes)
{
    // Class configuration is separate from widget creation; no eager viewport work.
    for (const auto& pair : this->uiControllers)
    {
        if (pair.Value)
        {
            pair.Value->RemoveFromParent();
        }

    }

    this->uiControllers.Reset();
    this->uiControllerClasses = MoveTemp(classes);
}

UTitleUIController* UUIManager::GetUIController()
{
    return Cast<UTitleUIController>(this->uiControllers.FindRef(UIControllerType::TITLE));
}

void UUIManager::AddUIController(UIControllerType type, UUserWidget* widget)
{
    if (widget)
    {
        this->uiControllers.Add(type, widget);
    }

}

void UUIManager::StartTitle()
{
    CreateMyWidget(UIControllerType::TITLE);
}

void UUIManager::CreateMyWidget(UIControllerType type)
{
    UUserWidget* widget = this->uiControllers.FindRef(type);
    if (!widget)
    {
        const TSubclassOf<UUserWidget> WIDGET_CLASS = this->uiControllerClasses.FindRef(type);
        APlayerController* player = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
        if (!WIDGET_CLASS || !player)
        {
            UE_LOG(LogTemp, Error, TEXT("Cannot show lobby: widget class or local player is missing."));
            return;
        }

        widget = CreateWidget<UUserWidget>(player, WIDGET_CLASS);
        if (!widget)
        {
            UE_LOG(LogTemp, Error, TEXT("Cannot show lobby: CreateWidget failed."));
            return;
        }

        AddUIController(type, widget);
    }

    // A GameInstance-owned widget can survive OpenLevel while its old player
    // controller does not. Rebind before presenting the reconnect screen.
    if (auto* player = GetGameInstance()->GetFirstLocalPlayerController())
    {
        widget->SetOwningPlayer(player);
    }

    if (!widget->IsInViewport())
    {
        widget->AddToViewport();
    }

}

