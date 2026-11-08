#include "../ServerCore/PacketRecvDispatcher.h"
#include "../ServerCore/Server.h"
#include "../ServerCore/StructuredLogger.h"
#include "../ServerCore/EnvironmentConfig.h"
#include "AccountService.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <thread>

int main()
{
    WSADATA wsaData = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        RPG_LOG_CRITICAL("Network", "WSAStartup failed. error=" << WSAGetLastError());
        return 1;
    }

    unsigned int numCpUs = std::thread::hardware_concurrency();
    if (numCpUs == 0)
    {
        numCpUs = 4;
    }

    const unsigned int DEFAULT_IO_THREADS = (std::min)(numCpUs, 8U);
    const unsigned int WORKERS =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_WORKERS", (std::min)(8U, (std::max)(2U, numCpUs / 2)), 1, 128);
    const auto PASSWORD = RpgConfig::GetString("RPG_ACCOUNT_DB_PASSWORD");
    auto& database = AccountDatabaseManager::GetInstance();
    if (PASSWORD.empty() ||
        !database.Init(RpgConfig::GetString("RPG_ACCOUNT_DB_HOST", "127.0.0.1").c_str(),
                       RpgConfig::GetString("RPG_ACCOUNT_DB_USER", "root").c_str(), PASSWORD.c_str(),
                       RpgConfig::GetString("RPG_ACCOUNT_DB_NAME", "account_db").c_str(),
                       RpgConfig::GetUnsigned("RPG_ACCOUNT_DB_PORT", 3307, 1, 65535),
                       static_cast<int>(RpgConfig::GetUnsigned("RPG_ACCOUNT_DB_POOL_SIZE", WORKERS + 1, 1, 256))))
    {
        RPG_LOG_CRITICAL("Configuration", "Account database initialization failed; check credentials and schema.");
        WSACleanup();
        return 1;
    }

    const unsigned int PORT = RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_PORT", 9100, 1, 65535);
    const unsigned int IO_THREAD_COUNT =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_IO_THREADS", DEFAULT_IO_THREADS, 1, 256);
    const unsigned int ACCEPT_DEPTH = RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_ACCEPT_DEPTH", 32, 1, 1024);
    ServerOptions serverOptions;
    serverOptions.bindAddress = RpgConfig::GetString("RPG_ACCOUNT_DATA_BIND_ADDRESS", "127.0.0.1");
    serverOptions.allowInsecureNonLoopbackBind = false;
    serverOptions.maxSessions = RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_MAX_SESSIONS", 10000, 1, 1'000'000);
    serverOptions.maxSessionsPerAddress =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_MAX_SESSIONS_PER_IP", 256, 1, 100'000);
    serverOptions.maxConnectionsPerAddressPerSecond =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_MAX_CONNECTIONS_PER_IP_PER_SECOND", 64, 1, 100'000);
    serverOptions.maxConnectionsGlobalPerSecond =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_MAX_CONNECTIONS_GLOBAL_PER_SECOND", 2048, 1, 1'000'000);
    serverOptions.ioDrainHardTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_IO_DRAIN_HARD_TIMEOUT_MS", 30000, 10000, 300000);
    serverOptions.maxQueuedSendBytesGlobal =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_GLOBAL_SEND_QUEUE_MB", 256, 16, 65536)) *
        1024 * 1024;
    serverOptions.maxBufferedReceiveBytesGlobal =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_GLOBAL_RECEIVE_BUFFER_MB", 128, 16, 65536)) *
        1024 * 1024;
    serverOptions.sessionLimits.maxQueuedSendBytes =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_SESSION_SEND_QUEUE_KB", 1024, 64, 65536)) *
        1024;
    serverOptions.sessionLimits.idleTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_IDLE_TIMEOUT_MS", 60000, 1000, 3'600'000);
    serverOptions.sessionLimits.partialFrameTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_PARTIAL_FRAME_TIMEOUT_MS", 10000, 1000, 300'000);
    serverOptions.sessionLimits.maxPacketsPerSecond =
        RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_MAX_PACKETS_PER_SECOND", 100, 10, 100'000);
    serverOptions.sessionLimits.maxReceivedBytesPerSecond =
        static_cast<std::size_t>(
            RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_MAX_RECEIVED_KB_PER_SECOND", 1024, 64, 1'048'576)) *
        1024;

    Server server(
        static_cast<unsigned short>(PORT), static_cast<int>(ACCEPT_DEPTH), static_cast<int>(IO_THREAD_COUNT),
        [](const SessionPtr& session, const char* buffer, DWORD bytesTransferred)
        {
            RPG_LOG_DEBUG("Network", "Received bytes=" << bytesTransferred);
            return PacketRecvDispatcher::GetInstance().Dispatch(session, buffer, bytesTransferred);
        },

        serverOptions);

    AccountService service(server.GetPacketSender());
    auto& dispatcher = PacketRecvDispatcher::GetInstance();
    const bool REGISTERED =
        dispatcher.RegisterHandler(
            RpgProtocol::PacketType::INTERNAL_ACCOUNT_REQUEST, 2322, SessionStateBit(SessionState::CONNECTED),
            [&service](const SessionPtr& session, const std::uint8_t* payload, std::size_t size)
            {
                if (session->GetRemoteAddress() != "127.0.0.1")
                {
                    return false;
                }

                RpgProtocol::AccountDataRequest request;
                return request.Deserialize(payload, size) && service.Enqueue(session, std::move(request));
            }) &&

        dispatcher.RegisterHandler(
            RpgProtocol::PacketType::SERVICE_PRESSURE_REQUEST, 0, SessionStateBit(SessionState::CONNECTED),
            [&service](const SessionPtr& session, const std::uint8_t*, std::size_t size)
            {
                return size == 0 && session->GetRemoteAddress() == "127.0.0.1" && service.SendPressure(session);
            });

    dispatcher.Seal();
    if (!REGISTERED || !service.Start(WORKERS) || !server.Start())
    {
        RPG_LOG_CRITICAL("Lifecycle", "AccountDataServer failed to start.");
        service.Stop();
        database.Cleanup();
        WSACleanup();
        return 1;
    }

    RPG_LOG_INFO("Lifecycle", "AccountDataServer is ready. port=" << PORT);

    std::string command;
    while (std::getline(std::cin, command))
    {
        std::string normalized = command;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char value)
                       {
                           return static_cast<char>(std::tolower(value));
                       });

        if (normalized == "-shutdown" || normalized == "shutdown" || normalized == "exit")
        {
            RPG_LOG_INFO("Command", "Shutdown command accepted.");
            break;
        }

        if (normalized == "help")
        {
            RPG_LOG_INFO("Command", "Commands: help, status, ping, shutdown (-shutdown), exit");
        }
        else if (normalized == "status")
        {
            RPG_LOG_INFO("Command", "AccountDataServer status: RUNNING, port="
                                        << PORT << ", pending=" << service.PendingCount()
                                        << ", database=" << (database.IsHealthy() ? "healthy" : "degraded")
                                        << ", sessions=" << server.GetSessionCount()
                                        << ", queuedSendBytes=" << server.GetQueuedSendBytes()
                                        << ", bufferedReceiveBytes=" << server.GetBufferedReceiveBytes());
        }
        else if (normalized == "ping")
        {
            RPG_LOG_INFO("Command", "pong");
        }
        else if (!normalized.empty())
        {
            RPG_LOG_WARNING("Command", "Unknown command: " << command << ". Type 'help'.");
        }

    }

    server.Stop();
    service.Stop();
    database.Cleanup();
    RPG_LOG_INFO("Lifecycle", "AccountDataServer shutdown completed.");
    RpgLogging::Logger::Shutdown();
    WSACleanup();
    return 0;
}

