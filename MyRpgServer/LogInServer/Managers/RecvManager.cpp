#include "RecvManager.h"
#include "../Managers/LoginManager.h"
#include "../Managers/SendManager.h"
#include "../Managers/CharacterGatewayService.h"
#include "../../Protocol/CharacterPackets.h"
#include "../../ServerCore/StructuredLogger.h"

#include <iostream>

RecvManager& RecvManager::GetInstance()
{
    static RecvManager instance;
    return instance;
}

bool RecvManager::HandleGuestLogin(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
{
    RpgProtocol::GuestLoginRequestPacket packet;
    if (!packet.Deserialize(payload, payloadSize))
    {
        RPG_LOG_WARNING("Protocol", "Invalid guest-login payload.");
        SendManager::GetInstance().SendGuestLogin(session, 0, RpgProtocol::PacketSendResult::INVALID_PACKET, "", "");
        return true;
    }

    LoginManager::GetInstance().StartGuestLogin(session, packet);

    RPG_LOG_INFO("Auth", "Handled guest login request. uid=" << packet.uid);
    return true;
}

bool RecvManager::HandleHeartbeat(const SessionPtr& session, const std::uint8_t*, std::size_t payloadSize)
{
    if (payloadSize != 0)
    {
        return false;
    }

    return SendManager::GetInstance().SendHeartbeat(session);
}

bool RecvManager::HandleCharacterList(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
{
    RpgProtocol::CharacterListRequestPacket packet;
    if (!packet.Deserialize(payload, payloadSize))
    {
        return false;
    }

    if (!CharacterGatewayService::GetInstance().Request(session, packet))
    {
        RpgProtocol::CharacterListResponsePacket response;
        response.requestId = packet.requestId;
        response.result = RpgProtocol::CharacterListResult::UNAVAILABLE;
        SendManager::GetInstance().SendCharacterList(session, response);
    }

    return true;
}

RecvManager::RecvManager()
{
    auto& dispatcher = PacketRecvDispatcher::GetInstance();
    const bool FIREBASE_REGISTERED = dispatcher.RegisterHandler(
        RpgProtocol::PacketType::FIREBASE_LOGIN_REQUEST, 8 + RpgProtocol::MAX_FIREBASE_ID_TOKEN_SIZE,
        SessionStateBit(SessionState::CONNECTED),
        [this](const SessionPtr& session, const std::uint8_t* data, std::size_t size)
        {
            return HandleFirebaseLogin(session, data, size);
        });

    const bool WORLD_REGISTERED = dispatcher.RegisterHandler(
        RpgProtocol::PacketType::WORLD_MESSAGE, 1024, SessionStateBit(SessionState::AUTHENTICATED),
        [](const SessionPtr& session, const std::uint8_t* data, std::size_t size)
        {
            using namespace shared_world::net;
            try
            {
                const auto REQUEST = Message::Decode(data, size);
                if (REQUEST.kind != Kind::SELECT)
                {
                    return false;
                }

                if (!CharacterGatewayService::GetInstance().Select(session, REQUEST))
                {
                    Message reply;
                    reply.kind = Kind::SELECTED;
                    reply.request = REQUEST.request;
                    reply.error = static_cast<unsigned>(Error::UNAVAILABLE);
                    return SendManager::GetInstance().SendWorld(session, reply);
                }

                return true;
            }

            catch (const std::exception&)
            {
                return false;
            }

        });

    const bool CREATION_REGISTERED = dispatcher.RegisterHandler(
        RpgProtocol::PacketType::CREATE_CHARACTER_REQUEST, 142, SessionStateBit(SessionState::AUTHENTICATED),
        [](const SessionPtr& session, const std::uint8_t* payload, std::size_t size)
        {
            RpgProtocol::CreateCharacterRequestPacket request;
            if (!request.Deserialize(payload, size))
            {
                return false;
            }

            if (!CharacterGatewayService::GetInstance().Create(session, request))
            {
                RpgProtocol::CreateCharacterResponsePacket response;
                response.requestId = request.requestId;
                response.operationId = request.operationId;
                response.result = RpgProtocol::CreateCharacterResult::BUSY;
                SendManager::GetInstance().SendCreateCharacter(session, response);
            }

            return true;
        });

    this->valid = FIREBASE_REGISTERED && WORLD_REGISTERED && CREATION_REGISTERED &&
                  dispatcher.RegisterHandler(
                      RpgProtocol::PacketType::GUEST_LOGIN_REQUEST, 1 + sizeof(std::uint32_t) * 3 + 128 + 2048,
                      SessionStateBit(SessionState::CONNECTED),
                      [this](const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
                      {
                          return this->HandleGuestLogin(session, payload, payloadSize);
                      }) &&

                  dispatcher.RegisterHandler(
                      RpgProtocol::PacketType::HEARTBEAT_REQUEST, 0, OPEN_SESSION_STATES,
                      [this](const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
                      {
                          return this->HandleHeartbeat(session, payload, payloadSize);
                      }) &&

                  dispatcher.RegisterHandler(
                      RpgProtocol::PacketType::CHARACTER_LIST_REQUEST, sizeof(std::uint32_t),
                      SessionStateBit(SessionState::AUTHENTICATED),
                      [this](const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
                      {
                          return this->HandleCharacterList(session, payload, payloadSize);
                      });

}

bool RecvManager::HandleFirebaseLogin(const SessionPtr& session, const std::uint8_t* payload, std::size_t size)
{
    RpgProtocol::FirebaseLoginRequestPacket packet;
    if (!packet.Deserialize(payload, size))
    {
        return false;
    }

    LoginManager::GetInstance().StartFirebaseLogin(session, packet);
    return true;
}

