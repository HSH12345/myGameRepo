#include "Managers/PacketManagerModules/ReceiveDispatcher.h"
#include "RPG/Packet/BinaryPackets.h"
#include "GameMain.h"
#include "Managers/LoginManager.h"
#include "Managers/CharacterManager.h"
#include "RPG/Packet/CreateCharacterPackets.h"
#include "Managers/LocalSaveManager.h"
#include "Managers/NetworkManager.h"
#include "Managers/PacketManager.h"
#include "Managers/PacketManagerModules/SendProcessor.h"
#include "Managers/UIManager.h"
#include "TimerManager.h"
#include "World/GameSessionSubsystem.h"

namespace
{
void EnableGuestLoginButton()
{
    if (UGameMain* game = UGameMain::GetInstance())
    {
        if (UUIManager* uiManager = game->GetSubsystem<UUIManager>())
        {
            if (UTitleUIController* title = uiManager->GetUIController())
            {
                title->SetGuestLoginEnabled(true);
            }

        }

    }

}

}

void UReceiveDispatcher::Init()
{
    RegisterHandler();
}

void UReceiveDispatcher::HandlePacket(const TArray<uint8>& rawPacket)
{
    if (rawPacket.Num() == 0)
    {
        UGameMain::GetInstance()->Log(TEXT("[HandlePacket] Empty packet received."), EMyLogType::LOG,
                                      EMyLogLocation::RECEIVE_DISPATCHER);
        return;
    }

    RpgProtocol::FFrameHeader header;
    if (!RpgProtocol::DecodeHeader(rawPacket.GetData(), rawPacket.Num(), header))
    {
        UGameMain::GetInstance()->Log(TEXT("[HandlePacket] Invalid binary protocol header."), EMyLogType::ERROR_VALUE,
                                      EMyLogLocation::RECEIVE_DISPATCHER);
        return;
    }

    const int32 EXPECTED_SIZE = RpgProtocol::HEADER_SIZE + static_cast<int32>(header.payloadSize);
    if (rawPacket.Num() != EXPECTED_SIZE)
    {
        UGameMain::GetInstance()->Log(TEXT("[HandlePacket] Frame size does not match its header."),
                                      EMyLogType::ERROR_VALUE, EMyLogLocation::RECEIVE_DISPATCHER);
        return;
    }

    auto handler = GetHandler(header.packetType);
    if (handler)
    {
        if (UGameMain* game = UGameMain::GetInstance())
        {
            if (UNetworkManager* network = game->GetSubsystem<UNetworkManager>())
            {
                network->MarkServerActivity();
            }

        }

        handler(rawPacket.GetData() + RpgProtocol::HEADER_SIZE, static_cast<int32>(header.payloadSize));
    }
    else
    {
        UGameMain::GetInstance()->Log(
            FString::Printf(TEXT("[HandlePacket] No handler for packetType=%d"), static_cast<int32>(header.packetType)),
            EMyLogType::ERROR_VALUE, EMyLogLocation::RECEIVE_DISPATCHER);
    }

}

HandlerFunc UReceiveDispatcher::GetHandler(RpgProtocol::EPacketType packetType) const
{
    const HandlerFunc* handler = this->handlers.Find(packetType);
    return handler ? *handler : HandlerFunc{};
}

void UReceiveDispatcher::RegisterHandler()
{
    this->handlers.Add(RpgProtocol::EPacketType::FIREBASE_LOGIN_RESPONSE,
                       [this](const uint8* data, int32 size)
                       {
                           HandleFirebaseLogin(data, size);
                       });

    this->handlers.Add(RpgProtocol::EPacketType::WORLD_MESSAGE,
                       [this](const uint8* payload, int32 size)
                       {
                           if (auto* game = GetTypedOuter<UGameInstance>())
                           {
                               game->GetSubsystem<UGameSessionSubsystem>()->Receive(payload, size);
                           }

                       });

    this->handlers.Add(RpgProtocol::EPacketType::GUEST_LOGIN_RESPONSE,
                       [this](const uint8* payload, int32 payloadSize)
                       {
                           HandleLogin(payload, payloadSize);
                       });

    this->handlers.Add(RpgProtocol::EPacketType::CHARACTER_LIST_RESPONSE,
                       [this](const uint8* payload, int32 payloadSize)
                       {
                           HandleCharacterList(payload, payloadSize);
                       });

    this->handlers.Add(RpgProtocol::EPacketType::CREATE_CHARACTER_RESPONSE,
                       [this](const uint8* payload, int32 payloadSize)
                       {
                           HandleCreateCharacter(payload, payloadSize);
                       });

    this->handlers.Add(RpgProtocol::EPacketType::HEARTBEAT_RESPONSE,
                       [](const uint8*, int32)
                       {
                           if (UGameMain* game = UGameMain::GetInstance())
                           {
                               if (UNetworkManager* network = game->GetSubsystem<UNetworkManager>())
                               {
                                   network->MarkHeartbeatResponse();
                               }

                           }

                       });

}

void UReceiveDispatcher::HandleLogin(const uint8* payload, int32 payloadSize)
{
    RpgProtocol::FGuestLoginResponsePacket loginPacket;

    if (!loginPacket.Deserialize(payload, payloadSize))
    {
        UGameMain::GetInstance()->Log(TEXT("[HandleLogin] Failed to parse guest-login response."),
                                      EMyLogType::ERROR_VALUE, EMyLogLocation::RECEIVE_DISPATCHER);
        EnableGuestLoginButton();
        return;
    }

    UPacketManager* packetManager = UGameMain::GetInstance()->GetSubsystem<UPacketManager>();
    if (!packetManager || !packetManager->sendProcessor ||
        !packetManager->sendProcessor->CompleteLoginRequest(loginPacket.requestId))
    {
        UGameMain::GetInstance()->Log(TEXT("Ignored stale login response"), EMyLogType::WARNING,
                                      EMyLogLocation::RECEIVE_DISPATCHER);
        return;
    }

    if (loginPacket.result != RpgProtocol::EPacketSendResult::SUCCESS)
    {
        UGameMain::GetInstance()->Log(
            FString::Printf(TEXT("[HandleLogin] Login failed. result=%d"), static_cast<int32>(loginPacket.result)),
            EMyLogType::ERROR_VALUE, EMyLogLocation::RECEIVE_DISPATCHER);
        EnableGuestLoginButton();
        return;
    }

    if (!ULocalSaveManager::SaveUidAndToken(loginPacket.uid, loginPacket.token))
    {
        UGameMain::GetInstance()->Log(TEXT("Failed to store login credentials"), EMyLogType::ERROR_VALUE,
                                      EMyLogLocation::RECEIVE_DISPATCHER);
        EnableGuestLoginButton();
        return;
    }

    if (UGameInstance* game = GetTypedOuter<UGameInstance>())
    {
        if (ULoginManager* login = game->GetSubsystem<ULoginManager>())
        {
            login->OnAuthenticated(loginPacket.uid);
        }

    }

}

void UReceiveDispatcher::HandleCharacterList(const uint8* payload, int32 payloadSize)
{
    RpgProtocol::FCharacterListResponsePacket packet;
    if (!packet.Deserialize(payload, payloadSize))
    {
        return;
    }

    if (UGameInstance* game = GetTypedOuter<UGameInstance>())
    {
        if (UCharacterManager* characters = game->GetSubsystem<UCharacterManager>())
        {
            characters->HandleList(packet);
        }

    }

}

void UReceiveDispatcher::HandleCreateCharacter(const uint8* payload, int32 payloadSize)
{
    RpgProtocol::FCreateCharacterResponsePacket packet;
    if (!packet.Deserialize(payload, payloadSize))
    {
        return;
    }

    if (UGameInstance* game = GetTypedOuter<UGameInstance>())
    {
        if (UCharacterManager* characters = game->GetSubsystem<UCharacterManager>())
        {
            characters->HandleCreation(packet);
        }

    }

}

void UReceiveDispatcher::HandleFirebaseLogin(const uint8* payload, int32 payloadSize)
{
    const auto* game = GetTypedOuter<UGameInstance>();
    if (!game)
    {
        return;
    }

    auto* network = game->GetSubsystem<UNetworkManager>();
    auto* login = game->GetSubsystem<ULoginManager>();
    auto* packets = game->GetSubsystem<UPacketManager>();
    RpgProtocol::FFirebaseLoginResponsePacket packet;
    if (!packet.Deserialize(payload, payloadSize))
    {
        network->DisconnectLoginServer();
        login->ReportLoginFailure(TEXT("계정 로그인 응답이 올바르지 않습니다."));
        return;
    }

    if (!packets || !packets->sendProcessor || !packets->sendProcessor->CompleteLoginRequest(packet.requestId, true))
    {
        return;
    }

    if (packet.result != RpgProtocol::EPacketSendResult::SUCCESS)
    {
        login->ReportLoginFailure(TEXT("계정 인증에 실패했습니다. 잠시 후 다시 시도하세요."));
        return;
    }

    if (!login->CompleteNaverLogin(packet.uid))
    {
        network->DisconnectLoginServer();
        login->ReportLoginFailure(TEXT("인증 계정이 일치하지 않습니다. 다시 로그인하세요."));
    }

}

