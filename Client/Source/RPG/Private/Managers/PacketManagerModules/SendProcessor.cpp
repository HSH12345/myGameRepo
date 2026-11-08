#include "Managers/PacketManagerModules/SendProcessor.h"
#include "RPG/Packet/BinaryPackets.h"
#include "RPG/Packet/CreateCharacterPackets.h"
#include "Kismet/GameplayStatics.h"
#include "GameMain.h"
#include "Managers/NetworkManager.h"

void USendProcessor::SendTestPacket(const FString& msg)
{
    RpgProtocol::FTestMessagePacket packet;
    packet.message = msg;
    TArray<uint8> payload;
    if (!packet.Serialize(payload))
    {
        return;
    }

    if (UNetworkManager* network = UGameMain::GetInstance()->GetSubsystem<UNetworkManager>(); network)
    {
        if (USender* sender = network->GetSender())
        {
            sender->SendPacket(RpgProtocol::EPacketType::TEST_MESSAGE, payload);
        }

    }

}

bool USendProcessor::SendGuestLoginPacket(const FString& uid, const FString& token)
{
    if (this->pendingLoginRequestId != 0)
    {
        UGameMain::GetInstance()->Log(TEXT("Guest login request is already in progress"), EMyLogType::WARNING);
        return false;
    }

    RpgProtocol::FGuestLoginRequestPacket packet;
    packet.requestId = this->nextRequestId++;
    if (this->nextRequestId == 0)
    {
        this->nextRequestId = 1;
    }

    packet.loginType = RpgProtocol::ELoginType::GUEST;
    packet.uid = uid;
    packet.token = token;
    TArray<uint8> payload;
    if (!packet.Serialize(payload))
    {
        return false;
    }

    if (UNetworkManager* network = UGameMain::GetInstance()->GetSubsystem<UNetworkManager>(); network)
    {
        if (USender* sender = network->GetSender();
            !sender || !sender->SendPacket(RpgProtocol::EPacketType::GUEST_LOGIN_REQUEST, payload))
        {
            UGameMain::GetInstance()->Log(TEXT("Failed to queue login request"), EMyLogType::ERROR_VALUE);
            return false;
        }
        else
        {
            this->pendingLoginRequestId = packet.requestId;
            this->pendingFirebase = false;
            return true;
        }

    }

    return false;
}

bool USendProcessor::CompleteLoginRequest(uint32 requestId, bool firebase)
{
    if (requestId == 0 || requestId != this->pendingLoginRequestId || firebase != this->pendingFirebase)
    {
        return false;
    }

    this->pendingLoginRequestId = 0;
    return true;
}

namespace
{
template <typename TPacket>
bool SendCharacterPacket(UGameInstance* game, RpgProtocol::EPacketType type, const TPacket& packet)
{
    if (!game)
    {
        return false;
    }

    UNetworkManager* network = game->GetSubsystem<UNetworkManager>();
    USender* sender = network ? network->GetSender() : nullptr;
    TArray<uint8> payload;
    return sender && packet.Serialize(payload) && sender->SendPacket(type, payload);
}

}

bool USendProcessor::SendCharacterListPacket(uint32 requestId)
{
    RpgProtocol::FCharacterListRequestPacket packet;
    packet.requestId = requestId;
    return SendCharacterPacket(GetTypedOuter<UGameInstance>(), RpgProtocol::EPacketType::CHARACTER_LIST_REQUEST,
                               packet);
}

bool USendProcessor::SendCreateCharacterPacket(const RpgProtocol::FCreateCharacterRequestPacket& packet)
{
    return SendCharacterPacket(GetTypedOuter<UGameInstance>(), RpgProtocol::EPacketType::CREATE_CHARACTER_REQUEST,
                               packet);
}

bool USendProcessor::SendFirebaseLoginPacket(const FString& idToken)
{
    if (this->pendingLoginRequestId)
    {
        return false;
    }

    const auto* game = GetTypedOuter<UGameInstance>();
    auto* network = game ? game->GetSubsystem<UNetworkManager>() : nullptr;
    auto* sender = network ? network->GetSender() : nullptr;
    RpgProtocol::FFirebaseLoginRequestPacket packet;
    packet.requestId = this->nextRequestId++;
    if (!this->nextRequestId)
    {
        this->nextRequestId = 1;
    }

    packet.idToken = idToken;
    TArray<uint8> payload;
    if (!sender || !packet.Serialize(payload) ||
        !sender->SendPacket(RpgProtocol::EPacketType::FIREBASE_LOGIN_REQUEST, payload))
    {
        return false;
    }

    this->pendingLoginRequestId = packet.requestId;
    this->pendingFirebase = true;
    return true;
}

