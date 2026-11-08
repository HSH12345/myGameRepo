// SendManager.cpp
#include "SendManager.h"
#include "../Managers/LoginManager.h"
#include "../../ServerCore/StructuredLogger.h"

#include <iostream>

SendManager::SendManager()
{
}

SendManager& SendManager::GetInstance()
{
    static SendManager instance;
    return instance;
}

void SendManager::Init(PacketSender& sender)
{
    this->packetSender = &sender;
}

bool SendManager::SendWorld(const SessionPtr& session, const shared_world::net::Message& packet)
{
    auto* sender = this->packetSender.load();
    return sender && sender->Send(session, RpgProtocol::PacketType::WORLD_MESSAGE, packet.Encode());
}

void SendManager::Shutdown()
{
    this->packetSender = nullptr;
}

bool SendManager::SendGuestLogin(const SessionPtr& session, std::uint32_t requestId,
                                 RpgProtocol::PacketSendResult result, const std::string& uid, const std::string& token)
{
    PacketSender* sender = this->packetSender.load();
    if (sender == nullptr)
    {
        RPG_LOG_ERROR("Lifecycle", "PacketSender is not initialized.");
        return false;
    }

    RpgProtocol::GuestLoginResponsePacket packet;
    packet.requestId = requestId;
    packet.result = result;
    packet.uid = uid;
    packet.token = token;

    RpgProtocol::ByteBuffer payload;
    if (!packet.Serialize(payload) || !sender->Send(session, RpgProtocol::PacketType::GUEST_LOGIN_RESPONSE, payload))
    {
        RPG_LOG_ERROR("Protocol", "Failed to serialize or queue guest-login response.");
        return false;
    }

    return true;
}

bool SendManager::SendHeartbeat(const SessionPtr& session)
{
    if (PacketSender* sender = this->packetSender.load())
    {
        return sender->Send(session, RpgProtocol::PacketType::HEARTBEAT_RESPONSE, {});
    }

    return false;
}

bool SendManager::SendFirebaseLogin(const SessionPtr& session, std::uint32_t requestId,
                                    RpgProtocol::PacketSendResult result, const std::string& uid)
{
    auto* sender = this->packetSender.load();
    RpgProtocol::FirebaseLoginResponsePacket packet{result, requestId, uid};
    RpgProtocol::ByteBuffer payload;
    return sender && packet.Serialize(payload) &&
           sender->Send(session, RpgProtocol::PacketType::FIREBASE_LOGIN_RESPONSE, payload);
}

bool SendManager::SendCharacterList(const SessionPtr& session, const RpgProtocol::CharacterListResponsePacket& packet)
{
    PacketSender* sender = this->packetSender.load();
    RpgProtocol::ByteBuffer payload;
    return sender && packet.Serialize(payload) &&
           sender->Send(session, RpgProtocol::PacketType::CHARACTER_LIST_RESPONSE, payload);
}

bool SendManager::SendCreateCharacter(const SessionPtr& session,
                                      const RpgProtocol::CreateCharacterResponsePacket& packet)
{
    PacketSender* sender = this->packetSender.load();
    RpgProtocol::ByteBuffer payload;
    return sender && packet.Serialize(payload) &&
           sender->Send(session, RpgProtocol::PacketType::CREATE_CHARACTER_RESPONSE, payload);
}

