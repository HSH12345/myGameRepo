#include "Managers/LoginManager.h"
#include "Managers/LocalSaveManager.h"
#include "Managers/PacketManager.h"
#include "Managers/UIManager.h"
#include "Managers/CharacterManager.h"
#include "Managers/AuthManager.h"
#include "Managers/NetworkManager.h"

ULoginManager* ULoginManager::GetInstance(UObject* worldContext)
{
    if (UWorld* world = GEngine->GetWorldFromContextObject(worldContext, EGetWorldErrorMode::LogAndReturnNull))
    {
        if (UGameInstance* gameInstance = world->GetGameInstance())
        {
            return gameInstance->GetSubsystem<ULoginManager>();
        }

    }

    return nullptr;
}

void ULoginManager::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);
    collection.InitializeDependency<UCharacterManager>();
    collection.InitializeDependency<UAuthManager>();
    collection.InitializeDependency<UNetworkManager>();
    auto* auth = GetGameInstance()->GetSubsystem<UAuthManager>();
    auth->onAuthTokenReady.AddDynamic(this, &ULoginManager::HandleAuthToken);
    auth->onAuthFailed.AddDynamic(this, &ULoginManager::HandleAuthFailed);
    auth->onAuthStatus.AddDynamic(this, &ULoginManager::HandleAuthStatus);
    GetGameInstance()->GetSubsystem<UNetworkManager>()->onConnectionClosed.AddUObject(
        this, &ULoginManager::HandleNetworkClosed);
    GetGameInstance()->GetSubsystem<UCharacterManager>()->onCharacterListChanged.AddDynamic(
        this, &ULoginManager::HandleCharacterListChanged);
}

void ULoginManager::Deinitialize()
{
    if (auto* auth = GetGameInstance()->GetSubsystem<UAuthManager>())
    {
        auth->CancelAuth();
        auth->onAuthTokenReady.RemoveDynamic(this, &ULoginManager::HandleAuthToken);
        auth->onAuthFailed.RemoveDynamic(this, &ULoginManager::HandleAuthFailed);
        auth->onAuthStatus.RemoveDynamic(this, &ULoginManager::HandleAuthStatus);
    }

    if (auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>())
    {
        network->onConnectionClosed.RemoveAll(this);
    }

    if (UCharacterManager* characters = GetGameInstance()->GetSubsystem<UCharacterManager>())
    {
        characters->onCharacterListChanged.RemoveDynamic(this, &ULoginManager::HandleCharacterListChanged);
    }

    Super::Deinitialize();
}

bool ULoginManager::StartLoginPorcess()
{
    this->usingNaver = false;
    GetGameInstance()->GetSubsystem<UAuthManager>()->CancelAuth(true);
    FString slotName = TEXT("LoginInfo");
    FString uid;
    FString token;

    ULocalSaveManager::LoadUidAndToken(slotName, uid, token);
    UGameMain* gameMain = Cast<UGameMain>(GetGameInstance());
    UPacketManager* packetManager = gameMain ? gameMain->GetSubsystem<UPacketManager>() : nullptr;
    return packetManager && packetManager->sendProcessor &&
           packetManager->sendProcessor->SendGuestLoginPacket(uid, token);
}

void ULoginManager::StartCharacterSelectionProcess()
{
    if (UUIManager* ui = GetGameInstance()->GetSubsystem<UUIManager>())
    {
        if (UTitleUIController* title = ui->GetUIController())
        {
            title->ShowHeroSelectionUI();
        }

    }

}

void ULoginManager::OnAuthenticated(const FString& uid)
{
    this->accountInfo.characterSlots.Reset();
    UCharacterManager* characters = GetGameInstance()->GetSubsystem<UCharacterManager>();
    characters->OnAuthenticated(uid);
    characters->RequestCharacterList();
}

void ULoginManager::HandleCharacterListChanged()
{
    this->accountInfo.characterSlots = GetGameInstance()->GetSubsystem<UCharacterManager>()->GetCharacters();
    // Lobby navigation is driven by CharacterManager events, not this legacy cache adapter.
}

bool ULoginManager::StartNaverLoginProcess()
{
    auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>();
    if (network->GetConnectionState() != ENetworkConnectionState::CONNECTED)
    {
        return false;
    }

    this->usingNaver = true;
    this->tokenConnectionGeneration = network->GetConnectionGeneration();
    auto* auth = GetGameInstance()->GetSubsystem<UAuthManager>();
    return auth->HasNaverSession() ? auth->RefreshNaverToken() : auth->StartNaverLogin();
}

void ULoginManager::HandleAuthToken(const FString& token)
{
    auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>();
    if (!this->usingNaver || this->tokenConnectionGeneration != network->GetConnectionGeneration() ||
        network->GetConnectionState() != ENetworkConnectionState::CONNECTED)
    {
        return;
    }

    const auto* packets = GetGameInstance()->GetSubsystem<UPacketManager>();
    this->onLoginStatus.Broadcast(TEXT("게임 계정 확인 중..."));
    if (!packets || !packets->sendProcessor || !packets->sendProcessor->SendFirebaseLoginPacket(token))
    {
        ReportLoginFailure(TEXT("계정 로그인 요청을 보내지 못했습니다."));
    }

}

void ULoginManager::HandleAuthFailed(const FString& reason)
{
    if (this->usingNaver)
    {
        ReportLoginFailure(reason);
    }

}

void ULoginManager::HandleAuthStatus(const FString& status)
{
    if (this->usingNaver)
    {
        this->onLoginStatus.Broadcast(status);
    }

}

void ULoginManager::HandleNetworkClosed()
{
    GetGameInstance()->GetSubsystem<UAuthManager>()->CancelAuth();
}

void ULoginManager::ReportLoginFailure(const FString& reason)
{
    this->onLoginFailed.Broadcast(reason);
}

bool ULoginManager::CompleteNaverLogin(const FString& uid)
{
    if (!this->usingNaver || !GetGameInstance()->GetSubsystem<UAuthManager>()->MarkNaverAuthenticated(uid))
    {
        return false;
    }

    OnAuthenticated(uid);
    return true;
}

void ULoginManager::Logout()
{
    this->usingNaver = false;
    GetGameInstance()->GetSubsystem<UAuthManager>()->CancelAuth(true);
    GetGameInstance()->GetSubsystem<UNetworkManager>()->DisconnectLoginServer();
}

