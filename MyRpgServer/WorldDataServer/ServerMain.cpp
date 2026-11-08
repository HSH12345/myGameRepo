#include "WorldService.h"
#include "../DatabaseCore/MySqlConnectionPool.h"
#include "../ServerCore/EnvironmentConfig.h"
#include "../ServerCore/PacketRecvDispatcher.h"
#include "../ServerCore/Server.h"
#include <algorithm>
#include <cctype>
#include <iostream>

int main()
{
    const auto PASSWORD = RpgConfig::GetString("RPG_WORLD_DB_PASSWORD");
    if (PASSWORD.empty())
    {
        RPG_LOG_CRITICAL("Configuration", "RPG_WORLD_DB_PASSWORD is required.");
        return 1;
    }

    const auto COUNT = RpgConfig::GetUnsigned("RPG_WORLD_DATA_WORKERS", 2, 1, 128);
    auto& pool = MySqlConnectionPool::GetInstance();
    if (!pool.Initialize(RpgConfig::GetString("RPG_WORLD_DB_HOST", "127.0.0.1"),
                         RpgConfig::GetString("RPG_WORLD_DB_USER", "root"), PASSWORD,
                         RpgConfig::GetString("RPG_WORLD_DB_NAME", "world_db"),
                         RpgConfig::GetUnsigned("RPG_WORLD_DB_PORT", 3307, 1, 65535),
                         RpgConfig::GetUnsigned("RPG_WORLD_DB_POOL_SIZE", COUNT, COUNT, 128)))
    {
        return 1;
    }

    if (!WorldRepository::ValidateSchema())
    {
        pool.Shutdown();
        return 1;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
    {
        pool.Shutdown();
        return 1;
    }

    WorldService service;
    auto& dispatcher = PacketRecvDispatcher::GetInstance();
    const bool REGISTERED =
        dispatcher.RegisterHandler(RpgProtocol::PacketType::INTERNAL_WORLD_DATA_REQUEST, 1563,
                                   SessionStateBit(SessionState::CONNECTED),
                                   [&service](const SessionPtr& session, const std::uint8_t* data, std::size_t size)
                                   {
                                       RpgProtocol::WorldDataRequest request;
                                       return session->GetRemoteAddress() == "127.0.0.1" &&
                                              request.Deserialize(data, size) && service.Enqueue(session, request);
                                   }) &&

        dispatcher.RegisterHandler(
            RpgProtocol::PacketType::SERVICE_PRESSURE_REQUEST, 0, SessionStateBit(SessionState::CONNECTED),
            [&service](const SessionPtr& session, const std::uint8_t*, std::size_t size)
            {
                return !size && session->GetRemoteAddress() == "127.0.0.1" && service.Pressure(session);
            });

    if (!REGISTERED)
    {
        pool.Shutdown();
        WSACleanup();
        return 1;
    }

    dispatcher.Seal();
    ServerOptions options;
    options.bindAddress = "127.0.0.1";
    options.allowInsecureNonLoopbackBind = false;
    options.maxSessions = RpgConfig::GetUnsigned("RPG_WORLD_DATA_MAX_SESSIONS", 256, 1, 100000);
    options.maxSessionsPerAddress = options.maxSessions;
    const auto PORT = RpgConfig::GetUnsigned("RPG_WORLD_DATA_PORT", 9400, 1, 65535);
    Server server(
        static_cast<unsigned short>(PORT), 32, 2,
        [](const SessionPtr& session, const char* data, DWORD size)
        {
            return PacketRecvDispatcher::GetInstance().Dispatch(session, data, size);
        },

        options);
    if (!service.Start(COUNT, server.GetPacketSender()) || !server.Start())
    {
        server.Stop();
        service.Stop();
        pool.Shutdown();
        WSACleanup();
        return 1;
    }

    RPG_LOG_INFO("Lifecycle", "WorldDataServer is ready. port=" << PORT);
    std::string command;
    while (std::getline(std::cin, command))
    {
        std::transform(command.begin(), command.end(), command.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });

        if (command == "shutdown" || command == "-shutdown" || command == "exit")
        {
            break;
        }

        if (command == "ping")
        {
            RPG_LOG_INFO("Command", "pong");
        }
        else if (command == "status")
        {
            RPG_LOG_INFO("Command", "WorldDataServer status: RUNNING, sessions="
                                        << server.GetSessionCount() << ", outstanding=" << service.Outstanding()
                                        << ", database=" << (pool.IsHealthy() ? "healthy" : "degraded"));
        }

    }

    server.Stop();
    service.Stop();
    pool.Shutdown();
    RpgLogging::Logger::Shutdown();
    WSACleanup();
    return 0;
}

