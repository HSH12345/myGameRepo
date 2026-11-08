#include "CharacterGatewayService.h"
#include "SendManager.h"
#include "../../ServerCore/EnvironmentConfig.h"
#include <algorithm>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

namespace
{
bool HasIdentity(const SessionPtr& session, const std::string& uid);
}

bool CharacterGatewayService::Select(const SessionPtr& session, const shared_world::net::Message& packet)
{
    using namespace shared_world::net;
    const auto UID = session->GetAccountId();
    if (!HasIdentity(session, UID))
    {
        return false;
    }

    auto quota = PendingSessionWork::Acquire(session);
    if (!quota)
    {
        return false;
    }

    return this->workers.Submit(
        256, false,
        [this, quota, UID, packet]
        {
            Message response;
            response.kind = Kind::SELECTED;
            response.request = packet.request;
            response.error = static_cast<unsigned>(Error::UNAVAILABLE);
            if (!HasIdentity(quota->session, UID))
            {
                return true;
            }

            if (this->worldDataEnabled && !this->worldClient.Available(this->gamePort, this->worldInstance))
            {
                if (HasIdentity(quota->session, UID))
                {
                    SendManager::GetInstance().SendWorld(quota->session, response);
                }

                return true; // World readiness must not trip the character DB circuit.
            }

            RpgProtocol::InternalCharacterListRequestPacket request{packet.request, UID};
            RpgProtocol::ByteBuffer payload, reply;
            RpgProtocol::CharacterListResponsePacket characters;
            if (request.Serialize(payload) &&
                this->client.Exchange(RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_REQUEST,
                                      RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_RESPONSE, payload, reply,
                                      std::chrono::steady_clock::now() + std::chrono::seconds(12)) &&
                characters.Deserialize(reply.data(), reply.size()) && characters.requestId == packet.request &&
                characters.result == RpgProtocol::CharacterListResult::SUCCESS)
            {
                response.error = static_cast<unsigned>(Error::NOT_OWNED);
                for (const auto& c : characters.characters)
                {
                    if (c.characterId == packet.character)
                    {
                        unsigned char random[16];
                        if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                        {
                            break;
                        }

                        const char* hex = "0123456789abcdef";
                        for (auto value : random)
                        {
                            response.ticket += hex[value >> 4];
                            response.ticket += hex[value & 15];
                        }

                        response.character = c.characterId;
                        response.name = c.characterName;
                        response.error = 0;
                        response.race = c.race;
                        response.gender = c.gender;
                        break;
                    }

                }

            }

            if (HasIdentity(quota->session, UID))
            {
                SendManager::GetInstance().SendWorld(quota->session, response);
            }

            return response.error != static_cast<unsigned>(Error::UNAVAILABLE);
        });

}

namespace
{
bool HasIdentity(const SessionPtr& session, const std::string& uid)
{
    return session && session->IsOpen() && session->GetState() == SessionState::AUTHENTICATED &&
           session->GetAccountId() == uid;
}

}

CharacterGatewayService& CharacterGatewayService::GetInstance()
{
    static CharacterGatewayService service;
    return service;
}

bool CharacterGatewayService::Start(unsigned int count, const std::string& host, unsigned short port)
{
    if (count < 2)
    {
        return false;
    }

    this->worldDataEnabled = RpgConfig::GetUnsigned("RPG_WORLD_DATA_ENABLED", 0, 0, 1) != 0;
    this->gamePort = static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_GAME_PORT", 9200, 1, 65535));
    this->worldInstance = RpgConfig::GetUnsigned("RPG_GAME_DEFAULT_INSTANCE", 1, 1, 1000000);
    if (this->worldDataEnabled && !this->worldClient.Configure(static_cast<unsigned short>(
                                      RpgConfig::GetUnsigned("RPG_WORLD_DATA_PORT", 9400, 1, 65535))))
    {
        return false;
    }

    const auto READ_COUNT = (std::max)(1U, count / 2);
    const auto WRITE_COUNT = (std::max)(1U, count - READ_COUNT);
    return this->client.Configure(host, port) && this->workers.Start(READ_COUNT, WRITE_COUNT,
                                                                     [this]
                                                                     {
                                                                         return Probe();
                                                                     });

}

bool CharacterGatewayService::Probe()
{
    if (!this->client.IsDrained(std::chrono::steady_clock::now() + std::chrono::seconds(2)))
    {
        return false;
    }

    RpgProtocol::InternalCharacterListRequestPacket request{1, "__service_health_probe__"};
    RpgProtocol::ByteBuffer payload, reply;
    RpgProtocol::CharacterListResponsePacket response;
    return request.Serialize(payload) &&
           this->client.Exchange(RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_REQUEST,
                                 RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_RESPONSE, payload, reply,
                                 std::chrono::steady_clock::now() + std::chrono::seconds(2), 1) &&
           response.Deserialize(reply.data(), reply.size()) && response.requestId == 1 &&
           response.result == RpgProtocol::CharacterListResult::SUCCESS;
}

bool CharacterGatewayService::Request(const SessionPtr& session, const RpgProtocol::CharacterListRequestPacket& packet)
{
    const std::string UID = session->GetAccountId();
    if (!HasIdentity(session, UID))
    {
        return false;
    }

    auto quota = PendingSessionWork::Acquire(session);
    if (!quota)
    {
        return false;
    }

    const bool ACCEPTED = this->workers.Submit(
        8 + UID.size(), false,
        [this, quota, UID, packet]
        {
            RpgProtocol::CharacterListResponsePacket response;
            response.requestId = packet.requestId;
            response.result = RpgProtocol::CharacterListResult::UNAVAILABLE;
            if (!HasIdentity(quota->session, UID))
            {
                return true;
            }

            RpgProtocol::InternalCharacterListRequestPacket request{packet.requestId, UID};
            RpgProtocol::ByteBuffer payload, reply;
            if (request.Serialize(payload) &&
                this->client.Exchange(RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_REQUEST,
                                      RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_RESPONSE, payload, reply,
                                      std::chrono::steady_clock::now() + std::chrono::seconds(12)))
            {
                RpgProtocol::CharacterListResponsePacket decoded;
                if (decoded.Deserialize(reply.data(), reply.size()) && decoded.requestId == packet.requestId)
                {
                    response = std::move(decoded);
                }

            }

            if (HasIdentity(quota->session, UID))
            {
                SendManager::GetInstance().SendCharacterList(quota->session, response);
            }

            return response.result == RpgProtocol::CharacterListResult::SUCCESS;
        });

    if (!ACCEPTED)
    {
        RpgProtocol::CharacterListResponsePacket response;
        response.requestId = packet.requestId;
        response.result = RpgProtocol::CharacterListResult::UNAVAILABLE;
        SendManager::GetInstance().SendCharacterList(session, response);
    }

    return true;
}

bool CharacterGatewayService::Create(const SessionPtr& session, const RpgProtocol::CreateCharacterRequestPacket& packet)
{
    const std::string UID = session->GetAccountId();
    if (!HasIdentity(session, UID))
    {
        return false;
    }

    auto quota = PendingSessionWork::Acquire(session);
    if (!quota)
    {
        return false;
    }

    const bool ACCEPTED = this->workers.Submit(
        32 + UID.size() + packet.operationId.size() + packet.characterName.size(), true,
        [this, quota, UID, packet]
        {
            RpgProtocol::CreateCharacterResponsePacket response;
            response.requestId = packet.requestId;
            response.operationId = packet.operationId;
            response.result = RpgProtocol::CreateCharacterResult::UNKNOWN_OUTCOME;
            // Admission commits us to attempting this mutation even if the user's socket closes.
            RpgProtocol::InternalCreateCharacterRequestPacket request;
            static_cast<RpgProtocol::CreateCharacterRequestPacket&>(request) = packet;
            request.accountUid = UID;
            RpgProtocol::ByteBuffer payload, reply;
            if (request.Serialize(payload) &&
                this->client.Exchange(RpgProtocol::PacketType::INTERNAL_CREATE_CHARACTER_REQUEST,
                                      RpgProtocol::PacketType::INTERNAL_CREATE_CHARACTER_RESPONSE, payload, reply,
                                      std::chrono::steady_clock::now() + std::chrono::seconds(12)))
            {
                RpgProtocol::CreateCharacterResponsePacket decoded;
                if (decoded.Deserialize(reply.data(), reply.size()) && decoded.requestId == packet.requestId &&
                    decoded.operationId == packet.operationId)
                {
                    response = std::move(decoded);
                }

            }

            if (HasIdentity(quota->session, UID))
            {
                SendManager::GetInstance().SendCreateCharacter(quota->session, response);
            }

            return response.result != RpgProtocol::CreateCharacterResult::UNKNOWN_OUTCOME &&
                   response.result != RpgProtocol::CreateCharacterResult::UNAVAILABLE &&
                   response.result != RpgProtocol::CreateCharacterResult::BUSY;
        });

    if (!ACCEPTED)
    {
        RpgProtocol::CreateCharacterResponsePacket response;
        response.requestId = packet.requestId;
        response.operationId = packet.operationId;
        response.result = RpgProtocol::CreateCharacterResult::BUSY;
        SendManager::GetInstance().SendCreateCharacter(session, response);
    }

    return true;
}

