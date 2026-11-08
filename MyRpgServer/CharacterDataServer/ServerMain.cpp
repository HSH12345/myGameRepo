#include "../DatabaseCore/MySqlConnectionPool.h"
#include "CharacterService.h"
#include "CharacterRepository.h"
#include "CharacterCreationRules.h"
#include "EconomyRepository.h"
#include "../Protocol/CharacterPackets.h"
#include "../ServerCore/EnvironmentConfig.h"
#include "../ServerCore/PacketRecvDispatcher.h"
#include "../ServerCore/Server.h"
#include "../ServerCore/StructuredLogger.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <thread>

int main()
{
    const std::string PASSWORD = RpgConfig::GetString("RPG_CHARACTER_DB_PASSWORD");
    if (PASSWORD.empty())
    {
        RPG_LOG_CRITICAL("Configuration", "RPG_CHARACTER_DB_PASSWORD environment variable is required.");
        return 1;
    }

    const unsigned int DB_PORT = RpgConfig::GetUnsigned("RPG_CHARACTER_DB_PORT", 3307, 1, 65535);
    unsigned int cpus = std::thread::hardware_concurrency();
    if (cpus == 0)
    {
        cpus = 4;
    }

    const unsigned int WORKERS =
        RpgConfig::GetUnsigned("RPG_CHARACTER_DATA_WORKERS", (std::min)(8U, (std::max)(2U, cpus / 2)), 2, 128);
    if (!MySqlConnectionPool::GetInstance().Initialize(
            RpgConfig::GetString("RPG_CHARACTER_DB_HOST", "127.0.0.1"),
            RpgConfig::GetString("RPG_CHARACTER_DB_USER", "root"), PASSWORD,
            RpgConfig::GetString("RPG_CHARACTER_DB_NAME", "character_db"), DB_PORT,
            RpgConfig::GetUnsigned("RPG_CHARACTER_DB_POOL_SIZE", WORKERS, WORKERS, 128)))
    {
        return 1;
    }

    if (!CharacterRepository::GetInstance().ValidateSchema() ||
        !CharacterCreationRules::GetInstance().Load(RpgConfig::GetString("RPG_CHARACTER_CREATION_RULES")))
    {
        RPG_LOG_CRITICAL("Configuration", "Character schema v2 or creation rules are invalid.");
        MySqlConnectionPool::GetInstance().Shutdown();
        return 1;
    }

    const bool ECONOMY_ENABLED = RpgConfig::GetUnsigned("RPG_CHARACTER_ECONOMY_ENABLED", 0, 0, 1) != 0;
    if (ECONOMY_ENABLED &&
        !EconomyRepository::Get().Configure(RpgConfig::GetString("RPG_ITEM_DATA_ROOT", "GameData/Items")))
    {
        RPG_LOG_CRITICAL("Configuration", "Economy/skill schema v5 or item/skill/action bar catalog unavailable.");
        MySqlConnectionPool::GetInstance().Shutdown();
        return 1;
    }

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        MySqlConnectionPool::GetInstance().Shutdown();
        return 1;
    }

    auto& dispatcher = PacketRecvDispatcher::GetInstance();
    const bool REGISTERED =
        dispatcher.RegisterHandler(RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_REQUEST, 4 + 4 + 128,
                                   SessionStateBit(SessionState::CONNECTED),
                                   [](const SessionPtr& session, const std::uint8_t* payload, std::size_t size)
                                   {
                                       if (session->GetRemoteAddress() != "127.0.0.1" &&
                                           session->GetRemoteAddress() != "::1")
                                       {
                                           return false;
                                       }

                                       RpgProtocol::InternalCharacterListRequestPacket request;
                                       return request.Deserialize(payload, size) &&
                                              CharacterService::GetInstance().Enqueue(session, std::move(request));
                                   }) &&

        dispatcher.RegisterHandler(
            RpgProtocol::PacketType::INTERNAL_CREATE_CHARACTER_REQUEST, 274, SessionStateBit(SessionState::CONNECTED),
            [](const SessionPtr& session, const std::uint8_t* payload, std::size_t size)
            {
                if (session->GetRemoteAddress() != "127.0.0.1")
                {
                    return false;
                }

                RpgProtocol::InternalCreateCharacterRequestPacket request;
                return request.Deserialize(payload, size) && CharacterService::GetInstance().Enqueue(session, request);
            }) &&

        dispatcher.RegisterHandler(RpgProtocol::PacketType::SERVICE_PRESSURE_REQUEST, 0,
                                   SessionStateBit(SessionState::CONNECTED),
                                   [](const SessionPtr& session, const std::uint8_t*, std::size_t size)
                                   {
                                       return size == 0 && session->GetRemoteAddress() == "127.0.0.1" &&
                                              CharacterService::GetInstance().SendPressure(session);
                                   });

    const bool ECONOMY_REGISTERED = dispatcher.RegisterHandler(
        RpgProtocol::PacketType::INTERNAL_ECONOMY_REQUEST, 512, SessionStateBit(SessionState::CONNECTED),
        [](const SessionPtr& session, const std::uint8_t* payload, std::size_t size)
        {
            if (session->GetRemoteAddress() != "127.0.0.1")
            {
                return false;
            }

            shared_world::economy::RpcRequest request;
            return request.Deserialize(payload, size) && CharacterService::GetInstance().Enqueue(session, request);
        });

    if (!REGISTERED || !ECONOMY_REGISTERED)
    {
        MySqlConnectionPool::GetInstance().Shutdown();
        WSACleanup();
        return 1;
    }

    dispatcher.Seal();

    ServerOptions options;
    options.bindAddress = RpgConfig::GetString("RPG_CHARACTER_DATA_BIND_ADDRESS", "127.0.0.1");
    options.allowInsecureNonLoopbackBind = false;
    options.maxSessions = RpgConfig::GetUnsigned("RPG_CHARACTER_DATA_MAX_SESSIONS", 256, 1, 100000);
    options.maxSessionsPerAddress = options.maxSessions;
    options.sessionLimits.maxPacketsPerSecond = 100;
    const unsigned int PORT = RpgConfig::GetUnsigned("RPG_CHARACTER_DATA_PORT", 9300, 1, 65535);
    Server server(
        static_cast<unsigned short>(PORT), 32, static_cast<int>((std::min)(cpus, 8U)),
        [](const SessionPtr& session, const char* data, DWORD size)
        {
            return PacketRecvDispatcher::GetInstance().Dispatch(session, data, size);
        },

        options);
    if (!CharacterService::GetInstance().Start(WORKERS, server.GetPacketSender()) || !server.Start())
    {
        CharacterService::GetInstance().Stop();
        MySqlConnectionPool::GetInstance().Shutdown();
        WSACleanup();
        return 1;
    }

    RPG_LOG_INFO("Lifecycle", "CharacterDataServer is ready. port=" << PORT);
    std::string command;
    while (std::getline(std::cin, command))
    {
        std::transform(command.begin(), command.end(), command.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });

        if (command == "-shutdown" || command == "shutdown" || command == "exit")
        {
            break;
        }

        if (command == "status")
        {
            RPG_LOG_INFO("Command", "CharacterDataServer status: RUNNING, sessions="
                                        << server.GetSessionCount() << ", pending="
                                        << CharacterService::GetInstance().PendingCount() << ", database="
                                        << (MySqlConnectionPool::GetInstance().IsHealthy() ? "healthy" : "degraded"));
        }
        else if (command == "ping")
        {
            RPG_LOG_INFO("Command", "pong");
        }

    }

    server.Stop();
    CharacterService::GetInstance().Stop();
    MySqlConnectionPool::GetInstance().Shutdown();
    RpgLogging::Logger::Shutdown();
    WSACleanup();
    return 0;
}

