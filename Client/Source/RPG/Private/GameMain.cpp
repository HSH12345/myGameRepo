#include "GameMain.h"
#include "Managers/UIManager.h"
#include "Managers/NetworkManager.h"
#include "Engine/GameEngine.h"
#include "Engine/Engine.h"

void UGameMain::Init()
{
    Super::Init();
    Log(TEXT("GameMain Initialized"), EMyLogType::LOG);
}

UGameMain* UGameMain::GetInstance()
{
    if (!GEngine)
    {
        UE_LOG(LogTemp, Error, TEXT("GameMain : GetInstance : GEngine is nullptr"));
        return nullptr;
    }

    // GameInstance subsystems are initialized before GameViewport can be created.
    // Never dereference the global viewport during that initialization window.
    if (!GEngine->GameViewport)
    {
        UE_LOG(LogTemp, Verbose, TEXT("GameMain : GetInstance : GameViewport is not ready"));
        return nullptr;
    }

    UGameInstance* gameInstance = GEngine->GameViewport->GetGameInstance();
    if (!gameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("GameMain : GetInstance : GameInstance is nullptr"));
        return nullptr;
    }

    return Cast<UGameMain>(gameInstance);
}

void UGameMain::Log(const FString& message, EMyLogType myLogType, EMyLogLocation logLocation)
{
    switch (myLogType)
    {
    case EMyLogType::LOG:
        UE_LOG(LogTemp, Log, TEXT("[LOG]: %s"), *message);
        break;

    case EMyLogType::WARNING:
        UE_LOG(LogTemp, Warning, TEXT("[WARNING]: %s"), *message);
        break;

    case EMyLogType::ERROR_VALUE:
        UE_LOG(LogTemp, Error, TEXT("[ERROR]: %s"), *message);
        break;

    default:
        UE_LOG(LogTemp, Log, TEXT("[UNKNOWN]: %s"), *message);
        break;
    }

}

void UGameMain::StartLoginPorcess()
{
    Log(TEXT("GameMain : StartLoginPorcess"), EMyLogType::LOG);

    this->GetSubsystem<UUIManager>()->StartTitle();
}

