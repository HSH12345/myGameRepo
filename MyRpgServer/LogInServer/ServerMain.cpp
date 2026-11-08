#include "../ServerCore/PacketRecvDispatcher.h"
#include "../ServerCore/Server.h"
#include "../ServerCore/GateTunnel.h"
#include "../ServerCore/EnvironmentConfig.h"
#include "Managers/RecvManager.h"
#include "Managers/AccountDataClient.h"
#include "Managers/LoginManager.h"
#include "Managers/AccountPresenceManager.h"
#include "Managers/CharacterGatewayService.h"
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <iostream>
#include "Managers/SendManager.h"
#include "../ServerCore/StructuredLogger.h"

//
// 10. main: 서버 초기화, 실행 및 graceful shutdown
//
int main()
{
    unsigned int numCpUs = std::thread::hardware_concurrency();
    if (numCpUs == 0)
    {
        numCpUs = 4;
    }

    const unsigned int DEFAULT_IO_THREADS = (std::min)(numCpUs, 8U);
    const unsigned int LOGIN_WORKER_COUNT =
        RpgConfig::GetUnsigned("RPG_LOGIN_WORKERS", (std::min)(8U, (std::max)(2U, numCpUs / 2)), 1, 128);
    const unsigned int CHARACTER_WORKERS =
        RpgConfig::GetUnsigned("RPG_CHARACTER_LIST_WORKERS", (std::min)(4U, (std::max)(2U, numCpUs / 4)), 2, 64);
    const std::string CHARACTER_DATA_HOST = RpgConfig::GetString("RPG_CHARACTER_DATA_HOST", "127.0.0.1");
    const unsigned int CHARACTER_DATA_PORT = RpgConfig::GetUnsigned("RPG_CHARACTER_DATA_PORT", 9300, 1, 65535);

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return 1;
    }

    if (!AccountDataClient::GetInstance().Configure(
            RpgConfig::GetString("RPG_ACCOUNT_DATA_HOST", "127.0.0.1"),
            static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_PORT", 9100, 1, 65535))))
    {
        WSACleanup();
        return 1;
    }

    if (!AccountPresenceManager::GetInstance().Start())
    {
        RPG_LOG_CRITICAL("Lifecycle", "Account presence worker failed to initialize.");
        WSACleanup();
        return 1;
    }

    if (!RecvManager::GetInstance().IsValid())
    {
        RPG_LOG_CRITICAL("Protocol", "LoginServer packet handler registration failed.");
        AccountPresenceManager::GetInstance().Stop();
        WSACleanup();
        return 1;
    }

    PacketRecvDispatcher::GetInstance().Seal();

    const unsigned int LOGIN_PORT = RpgConfig::GetUnsigned("RPG_LOGIN_PORT", 9001, 1, 65535);
    const unsigned int IO_THREAD_COUNT = RpgConfig::GetUnsigned("RPG_IO_THREADS", DEFAULT_IO_THREADS, 1, 256);
    const unsigned int ACCEPT_DEPTH = RpgConfig::GetUnsigned("RPG_ACCEPT_DEPTH", 32, 1, 1024);
    const unsigned int LOGIN_JOB_TIMEOUT_MILLISECONDS =
        RpgConfig::GetUnsigned("RPG_LOGIN_JOB_TIMEOUT_MS", 15000, 1000, 120000);
    ServerOptions serverOptions;
    serverOptions.bindAddress = RpgConfig::GetString("RPG_LOGIN_BIND_ADDRESS", "127.0.0.1");
    serverOptions.allowInsecureNonLoopbackBind =
        RpgConfig::GetUnsigned("RPG_ALLOW_INSECURE_NON_LOOPBACK_BIND", 0, 0, 1) != 0;
    serverOptions.maxSessions = RpgConfig::GetUnsigned("RPG_MAX_SESSIONS", 10000, 1, 1'000'000);
    serverOptions.maxSessionsPerAddress = RpgConfig::GetUnsigned("RPG_MAX_SESSIONS_PER_IP", 256, 1, 100'000);
    serverOptions.maxConnectionsPerAddressPerSecond =
        RpgConfig::GetUnsigned("RPG_MAX_CONNECTIONS_PER_IP_PER_SECOND", 64, 1, 100'000);
    serverOptions.maxConnectionsGlobalPerSecond =
        RpgConfig::GetUnsigned("RPG_MAX_CONNECTIONS_GLOBAL_PER_SECOND", 2048, 1, 1'000'000);
    serverOptions.ioDrainHardTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_IO_DRAIN_HARD_TIMEOUT_MS", 30000, 10000, 300000);
    serverOptions.maxQueuedSendBytesGlobal =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_GLOBAL_SEND_QUEUE_MB", 512, 16, 65536)) * 1024 * 1024;
    serverOptions.maxBufferedReceiveBytesGlobal =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_GLOBAL_RECEIVE_BUFFER_MB", 256, 16, 65536)) * 1024 * 1024;
    serverOptions.sessionLimits.maxQueuedSendBytes =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_SESSION_SEND_QUEUE_KB", 4096, 64, 65536)) * 1024;
    serverOptions.sessionLimits.idleTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_IDLE_TIMEOUT_MS", 60000, 1000, 3'600'000);
    serverOptions.sessionLimits.partialFrameTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_PARTIAL_FRAME_TIMEOUT_MS", 10000, 1000, 300'000);
    serverOptions.sessionLimits.maxPacketsPerSecond =
        RpgConfig::GetUnsigned("RPG_MAX_PACKETS_PER_SECOND", 200, 10, 100'000);
    serverOptions.sessionLimits.maxReceivedBytesPerSecond =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_MAX_RECEIVED_KB_PER_SECOND", 2048, 64, 1'048'576)) * 1024;
    serverOptions.onSessionDisconnected = [](const SessionPtr& session, const std::string&)
    {
        AccountPresenceManager::GetInstance().Deactivate(session->GetId());
    };

    LoginManager::GetInstance().Init(LOGIN_WORKER_COUNT, LOGIN_JOB_TIMEOUT_MILLISECONDS);
    if (!LoginManager::GetInstance().IsRunning())
    {
        RPG_LOG_CRITICAL("Lifecycle", "Login workers failed to initialize.");
        AccountPresenceManager::GetInstance().Stop();
        WSACleanup();
        return 1;
    }

    if (!CharacterGatewayService::GetInstance().Start(CHARACTER_WORKERS, CHARACTER_DATA_HOST,
                                                      static_cast<unsigned short>(CHARACTER_DATA_PORT)))
    {
        RPG_LOG_CRITICAL("Lifecycle", "Character-list workers failed to initialize.");
        LoginManager::GetInstance().Stop();
        AccountPresenceManager::GetInstance().Stop();
        WSACleanup();
        return 1;
    }

    const bool GATE_ONLY = RpgConfig::GetUnsigned("RPG_GATE_ONLY", 1, 0, 1) != 0;
    GateTunnel tunnel;
    if (GATE_ONLY)
    {
        serverOptions.bindAddress = "127.0.0.1";
        tunnel.Configure(serverOptions);
    }

    Server server(
        static_cast<unsigned short>(LOGIN_PORT), static_cast<int>(ACCEPT_DEPTH), static_cast<int>(IO_THREAD_COUNT),
        [&](const SessionPtr& session, const char* buffer, DWORD bytesTransferred)
        {
            return GATE_ONLY ? tunnel.Dispatch(session, buffer, bytesTransferred)
                             : PacketRecvDispatcher::GetInstance().Dispatch(session, buffer, bytesTransferred);
        },

        serverOptions);
    tunnel.SetSender(server.GetPacketSender());
    SendManager::GetInstance().Init(server.GetPacketSender());
    if (!server.Start())
    {
        RPG_LOG_CRITICAL("Lifecycle", "LoginServer failed to start.");
        LoginManager::GetInstance().Stop();
        CharacterGatewayService::GetInstance().Stop();
        AccountPresenceManager::GetInstance().Stop();
        SendManager::GetInstance().Shutdown();
        WSACleanup();
        return 1;
    }

    RPG_LOG_INFO("Lifecycle", "LoginServer is ready. port=" << LOGIN_PORT);

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
            const auto ACCOUNT_PRESSURE = AccountDataClient::GetInstance().Admission().Inspect();
            const auto CHARACTER_PRESSURE = CharacterGatewayService::GetInstance().Pressure();
            RPG_LOG_INFO("Command",
                         "LoginServer status: RUNNING, port="
                             << LOGIN_PORT << ", sessions=" << server.GetSessionCount()
                             << ", accountAdmission=" << static_cast<int>(ACCOUNT_PRESSURE.state)
                             << ", characterAdmission=" << static_cast<int>(CHARACTER_PRESSURE.state)
                             << ", pendingCharacterBytes=" << CHARACTER_PRESSURE.bytes
                             << ", oldestCharacterMs=" << CHARACTER_PRESSURE.oldestAge.count()
                             << ", queuedSendBytes=" << server.GetQueuedSendBytes()
                             << ", bufferedReceiveBytes=" << server.GetBufferedReceiveBytes() << ", pendingPresence="
                             << AccountPresenceManager::GetInstance().PendingAccountCount() << ", accountData="
                             << (AccountDataClient::GetInstance().IsHealthy() ? "healthy" : "degraded"));
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
    CharacterGatewayService::GetInstance().Stop();
    LoginManager::GetInstance().Stop();
    AccountPresenceManager::GetInstance().Stop();
    SendManager::GetInstance().Shutdown();
    RPG_LOG_INFO("Lifecycle", "LoginServer shutdown completed.");
    RpgLogging::Logger::Shutdown();
    WSACleanup();
    return 0;
}

