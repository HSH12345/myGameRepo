#include "../ServerCore/PacketRecvDispatcher.h"
#include "../ServerCore/Server.h"
#include "../ServerCore/GateTunnel.h"
#include "../ServerCore/StructuredLogger.h"
#include "../ServerCore/EnvironmentConfig.h"
#include "Game/MapScheduler.h"
#include "Game/WorldRegistry.h"
#include "Managers/GameRecvManager.h"
#include "Managers/GameSendManager.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <thread>
#include <stdexcept>

int main()
{
    WSADATA wsaData = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        RPG_LOG_CRITICAL("Network", "WSAStartup failed. error=" << WSAGetLastError());
        return 1;
    }

    unsigned int cpuCount = std::thread::hardware_concurrency();
    if (cpuCount == 0)
    {
        cpuCount = 4;
    }

    const unsigned int DEFAULT_IO_THREADS = (std::min)(cpuCount, 8U);
    const unsigned int PORT = RpgConfig::GetUnsigned("RPG_GAME_PORT", 9200, 1, 65535);
    const unsigned int IO_THREADS = RpgConfig::GetUnsigned("RPG_GAME_IO_THREADS", DEFAULT_IO_THREADS, 1, 256);
    const unsigned int ACCEPT_DEPTH = RpgConfig::GetUnsigned("RPG_GAME_ACCEPT_DEPTH", 32, 1, 1024);
    const unsigned int TICK_RATE = RpgConfig::GetUnsigned("RPG_GAME_TICK_RATE", 30, 1, 240);
    const unsigned int MAX_COMMANDS = RpgConfig::GetUnsigned("RPG_GAME_MAX_QUEUED_COMMANDS", 65536, 1024, 4'000'000);
    const unsigned int MAX_COMMANDS_PER_TICK =
        RpgConfig::GetUnsigned("RPG_GAME_MAX_COMMANDS_PER_TICK", 4096, 1, 100'000);
    const unsigned int COMMAND_BUDGET_MICROSECONDS =
        RpgConfig::GetUnsigned("RPG_GAME_COMMAND_BUDGET_US", 10000, 100, 1'000'000);
    const unsigned int MAX_PENDING_COMMANDS_PER_SESSION =
        RpgConfig::GetUnsigned("RPG_GAME_MAX_PENDING_COMMANDS_PER_SESSION", 128, 1, 10'000);
    const unsigned int MAX_SESSIONS = RpgConfig::GetUnsigned("RPG_GAME_MAX_SESSIONS", 10000, 1, 1'000'000);
    const bool ENABLE_DEVELOPMENT_PACKETS = RpgConfig::GetUnsigned("RPG_GAME_ENABLE_DEVELOPMENT_PACKETS", 0, 0, 1) != 0;
    const unsigned int GAME_SHUTDOWN_TIMEOUT =
        RpgConfig::GetUnsigned("RPG_GAME_THREAD_SHUTDOWN_TIMEOUT_MS", 30000, 10000, 300000);

    const auto MAP_WORKERS =
        RpgConfig::GetUnsigned("RPG_GAME_WORKERS", (std::min)(4U, (std::max)(1U, cpuCount / 4)), 1, 64);
    GameSendManager sendManager;
    auto maps = std::make_shared<GameMapCatalog>();
    const auto DEFAULT_INSTANCE = RpgConfig::GetUnsigned("RPG_GAME_DEFAULT_INSTANCE", 1, 1, 1000000);
    const auto MANIFEST_PATH = RpgConfig::GetString("RPG_GAME_WORLD_MANIFEST", "");
    const auto DUNGEON_PATH = RpgConfig::GetString("RPG_GAME_DUNGEON_MANIFEST", "");
    std::vector<std::uint64_t> dungeonInstances;
    std::shared_ptr<const shared_world::economy::Catalog> economyCatalog;
    try
    {
        if (RpgConfig::GetUnsigned("RPG_GAME_ECONOMY_ENABLED", 0, 0, 1))
        {
            economyCatalog = std::make_shared<const shared_world::economy::Catalog>(
                shared_world::economy::Catalog::Load(RpgConfig::GetString("RPG_ITEM_DATA_ROOT", "GameData/Items")));
        }

        if (!MANIFEST_PATH.empty())
        {
            const auto NPCS = std::make_shared<const shared_world::npc::Catalog>(
                shared_world::npc::Catalog::Load(RpgConfig::GetString("RPG_GAME_NPC_ROOT", "GameData/Npcs")));
            if (economyCatalog)
            {
                for (const auto& pair : NPCS->definitions)
                {
                    if (pair.second.role == 2 && !economyCatalog->HasShop(pair.second.roleData))
                    {
                        throw std::invalid_argument("merchant has unknown shop");
                    }

                }

            }

            const auto EXIT_RULES = shared_world::dungeon::LoadRules(
                RpgConfig::GetString("RPG_GAME_EXIT_RULES", "GameData/Dungeons/ExitRules.tsv"));
            const auto COMBAT = std::make_shared<const shared_world::combat::Catalog>(
                shared_world::combat::Catalog::Load(RpgConfig::GetString("RPG_COMBAT_DATA_ROOT", "GameData/Combat")));
            if (economyCatalog && economyCatalog->skills.checksum != COMBAT->skills.checksum)
            {
                throw std::invalid_argument("combat/economy skill catalog mismatch");
            }

            auto configure = [&](std::shared_ptr<GameMapContent> content)
            {
                content->npcs = NPCS;
                content->combat = COMBAT;
                const auto& bounds = content->chunks ? content->chunks->layout.world.bounds
                                                     : content->collision->Map().Definition().bounds;
                if (const auto RULE = EXIT_RULES.find(content->identity.id); RULE != EXIT_RULES.end())
                {
                    if (content->chunks)
                    {
                        throw std::invalid_argument("dungeon exit layout requires fully resident collision");
                    }

                    RULE->second.Validate(bounds);
                    content->exitRule = RULE->second;
                }

                if (content->identity.id == 301 && content->identity.version == 2)
                {
                    throw std::invalid_argument("Dungeon01 v2 is obsolete; deploy v3 arena collision with this build");
                }

                if (content->identity.id == 301 && content->identity.version >= 3)
                {
                    const auto COUNT = std::count_if(NPCS->placements.begin(), NPCS->placements.end(),
                                                     [&](const auto& p)
                                                     {
                                                         return p.map == 301 && COMBAT->monsters.count(p.npc);
                                                     });

                    const auto BOSS = std::count_if(NPCS->placements.begin(), NPCS->placements.end(),
                                                    [](const auto& p)
                                                    {
                                                        return p.map == 301 && p.npc == 2201;
                                                    });

                    if (!content->exitRule || COUNT != 4 || BOSS != 1 || COMBAT->monsters.at(2201).basic != 0)
                    {
                        throw std::invalid_argument(
                            "maze requires exit rules, three regular templates and one skill-only boss");
                    }

                }

                for (const auto& p : NPCS->placements)
                {
                    if (p.map == content->identity.id && !bounds.Contains(p.feet))
                    {
                        throw std::invalid_argument("NPC placement outside map bounds");
                    }

                }

                return content;
            };

            if (TICK_RATE != 30)
            {
                throw std::invalid_argument("World movement currently requires 30Hz");
            }

            auto hub = configure(GameMapContent::Load(MANIFEST_PATH));
            if (hub->exitRule)
            {
                throw std::invalid_argument("exit rules require a private dungeon, not the shared map");
            }

            maps->emplace(DEFAULT_INSTANCE, std::move(hub));
            if (!DUNGEON_PATH.empty())
            {
                const auto COUNT = RpgConfig::GetUnsigned("RPG_GAME_DUNGEON_CAPACITY", 8, 1, 32);
                for (unsigned i = 1; i <= COUNT; ++i)
                {
                    // Each instance owns its own loader and mutable residency.
                    auto content = configure(GameMapContent::Load(DUNGEON_PATH));
                    if (content->identity.id == maps->at(DEFAULT_INSTANCE)->identity.id)
                    {
                        throw std::invalid_argument("dungeon must have a distinct map identity");
                    }

                    dungeonInstances.push_back(DEFAULT_INSTANCE + i);
                    maps->emplace(DEFAULT_INSTANCE + i, std::move(content));
                }

            }

        }

    }

    catch (const std::exception& error)
    {
        RPG_LOG_CRITICAL("World", "Map startup validation failed: " << error.what());
        RpgLogging::Logger::Shutdown();
        WSACleanup();
        return 1;
    }

    MapScheduler runtime(
        GameRuntimeOptions{TICK_RATE,
                           MAX_COMMANDS,
                           MAX_COMMANDS_PER_TICK,
                           COMMAND_BUDGET_MICROSECONDS,
                           static_cast<std::size_t>(MAX_SESSIONS) * 2,
                           GAME_SHUTDOWN_TIMEOUT,
                           maps,
                           [&sendManager](const SessionPtr& session, const shared_world::net::Message& message)
                           {
                               return sendManager.SendWorld(session, message);
                           },

                           {},
                           DEFAULT_INSTANCE,
                           dungeonInstances,
                           RpgConfig::GetUnsigned("RPG_GAME_READY_TIMEOUT_SECONDS", 120, 2, 120),
                           {},
                           economyCatalog},
        MAP_WORKERS, static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_ACCOUNT_DATA_PORT", 9100, 1, 65535)),
        static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_CHARACTER_DATA_PORT", 9300, 1, 65535)));
    if (!runtime.Start())
    {
        RPG_LOG_CRITICAL("Game", "Failed to start the game thread.");
        WSACleanup();
        return 1;
    }

    ServerOptions serverOptions;
    serverOptions.bindAddress = RpgConfig::GetString("RPG_GAME_BIND_ADDRESS", "127.0.0.1");
    serverOptions.allowInsecureNonLoopbackBind =
        RpgConfig::GetUnsigned("RPG_GAME_ALLOW_INSECURE_NON_LOOPBACK_BIND", 0, 0, 1) != 0;
    serverOptions.maxSessions = MAX_SESSIONS;
    serverOptions.maxSessionsPerAddress = RpgConfig::GetUnsigned("RPG_GAME_MAX_SESSIONS_PER_IP", 256, 1, 100'000);
    serverOptions.maxConnectionsPerAddressPerSecond =
        RpgConfig::GetUnsigned("RPG_GAME_MAX_CONNECTIONS_PER_IP_PER_SECOND", 64, 1, 100'000);
    serverOptions.maxConnectionsGlobalPerSecond =
        RpgConfig::GetUnsigned("RPG_GAME_MAX_CONNECTIONS_GLOBAL_PER_SECOND", 2048, 1, 1'000'000);
    serverOptions.ioDrainHardTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_GAME_IO_DRAIN_HARD_TIMEOUT_MS", 30000, 10000, 300000);
    serverOptions.maxQueuedSendBytesGlobal =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_GAME_GLOBAL_SEND_QUEUE_MB", 512, 16, 65536)) * 1024 * 1024;
    serverOptions.maxBufferedReceiveBytesGlobal =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_GAME_GLOBAL_RECEIVE_BUFFER_MB", 256, 16, 65536)) * 1024 *
        1024;
    serverOptions.sessionLimits.maxQueuedSendBytes =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_GAME_SESSION_SEND_QUEUE_KB", 4096, 64, 65536)) * 1024;
    serverOptions.sessionLimits.idleTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_GAME_IDLE_TIMEOUT_MS", 60000, 1000, 3'600'000);
    serverOptions.sessionLimits.partialFrameTimeoutMilliseconds =
        RpgConfig::GetUnsigned("RPG_GAME_PARTIAL_FRAME_TIMEOUT_MS", 10000, 1000, 300'000);
    serverOptions.sessionLimits.maxPacketsPerSecond =
        RpgConfig::GetUnsigned("RPG_GAME_MAX_PACKETS_PER_SECOND", 200, 10, 100'000);
    serverOptions.sessionLimits.maxReceivedBytesPerSecond =
        static_cast<std::size_t>(RpgConfig::GetUnsigned("RPG_GAME_MAX_RECEIVED_KB_PER_SECOND", 2048, 64, 1'048'576)) *
        1024;
    serverOptions.onSessionConnected = [&runtime, DEFAULT_INSTANCE](const SessionPtr& session)
    {
        GameCommand command;
        command.session = session;
        command.sessionId = session->GetId();
        command.mapInstanceId = DEFAULT_INSTANCE;
        command.payload = SessionConnectedCommand{};
        if (!runtime.TryNotifyConnected(std::move(command)))
        {
            throw std::runtime_error("game lifecycle tracker is full");
        }

    };

    serverOptions.onSessionDisconnected = [&runtime](const SessionPtr& session, const std::string& reason)
    {
        GameCommand command;
        command.sessionId = session->GetId();
        command.payload = SessionDisconnectedCommand{reason};
        if (!runtime.NotifyDisconnected(std::move(command)))
        {
            RPG_LOG_DEBUG("Game",
                          "Ignored disconnect for an untracked rejected session. sessionId=" << session->GetId());
        }

    };

    const bool GATE_ONLY = RpgConfig::GetUnsigned("RPG_GATE_ONLY", 1, 0, 1) != 0;
    GateTunnel tunnel;
    if (GATE_ONLY)
    {
        serverOptions.bindAddress = "127.0.0.1";
        tunnel.Configure(serverOptions);
    }

    Server server(
        static_cast<unsigned short>(PORT), static_cast<int>(ACCEPT_DEPTH), static_cast<int>(IO_THREADS),
        [&](const SessionPtr& session, const char* buffer, DWORD bytesTransferred)
        {
            return GATE_ONLY ? tunnel.Dispatch(session, buffer, bytesTransferred)
                             : PacketRecvDispatcher::GetInstance().Dispatch(session, buffer, bytesTransferred);
        },

        serverOptions);

    tunnel.SetSender(server.GetPacketSender());
    sendManager.Init(server.GetPacketSender());
    GameRecvManager recvManager(runtime, sendManager, MAX_PENDING_COMMANDS_PER_SESSION, ENABLE_DEVELOPMENT_PACKETS);
    if (!recvManager.IsValid())
    {
        RPG_LOG_CRITICAL("Protocol", "GameServer packet handler registration failed.");
        sendManager.Shutdown();
        runtime.Stop();
        WSACleanup();
        return 1;
    }

    PacketRecvDispatcher::GetInstance().Seal();
    if (!server.Start())
    {
        RPG_LOG_CRITICAL("Lifecycle", "GameServer failed to start.");
        sendManager.Shutdown();
        runtime.Stop();
        WSACleanup();
        return 1;
    }

    WorldRegistry worldRegistry;
    if (RpgConfig::GetUnsigned("RPG_WORLD_DATA_ENABLED", 0, 0, 1) &&
        !worldRegistry.Start(static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_WORLD_DATA_PORT", 9400, 1, 65535)),
                             static_cast<unsigned short>(PORT), *maps))
    {
        RPG_LOG_CRITICAL("WorldData", "World registry publication failed.");
        server.Stop();
        sendManager.Shutdown();
        runtime.Stop();
        WSACleanup();
        return 1;
    }

    RPG_LOG_INFO("Lifecycle", "GameServer is ready. port=" << PORT << ", tickRate=" << TICK_RATE
                                                           << ", commandQueueCapacity=" << runtime.GetQueueCapacity()
                                                           << ", developmentPackets="
                                                           << (ENABLE_DEVELOPMENT_PACKETS ? "enabled" : "disabled"));
    std::string command;
    while (std::getline(std::cin, command))
    {
        std::transform(command.begin(), command.end(), command.begin(),
                       [](unsigned char value)
                       {
                           return static_cast<char>(std::tolower(value));
                       });

        if (command == "-shutdown" || command == "shutdown" || command == "exit")
        {
            break;
        }

        if (command == "status")
        {
            const auto PRESSURE = runtime.DataPressure();
            RPG_LOG_INFO("Command", "GameServer status: sessions="
                                        << server.GetSessionCount() << ", mapWorkers=" << runtime.WorkerCount()
                                        << ", dataAdmission=" << static_cast<int>(PRESSURE.state)
                                        << ", pendingData=" << PRESSURE.count << ", pendingDataBytes=" << PRESSURE.bytes
                                        << ", oldestDataMs=" << PRESSURE.oldestAge.count() << ", rejectedData="
                                        << PRESSURE.rejected << ", queuedSendBytes=" << server.GetQueuedSendBytes()
                                        << ", bufferedReceiveBytes=" << server.GetBufferedReceiveBytes()
                                        << ", queue=" << runtime.GetQueueDepth() << '/' << runtime.GetQueueCapacity()
                                        << ", lifecycleQueue=" << runtime.GetLifecycleQueueDepth()
                                        << ", lifecycleTracked=" << runtime.GetTrackedLifecycleSessionCount()
                                        << ", processed=" << runtime.GetProcessedCommandCount()
                                        << ", lifecycle=" << runtime.GetProcessedLifecycleCount()
                                        << ", dropped=" << runtime.GetDroppedCommandCount()
                                        << ", tickOverruns=" << runtime.GetTickOverrunCount()
                                        << ", maxTickUs=" << runtime.GetMaxTickMicroseconds()
                                        << ", tickP50Us=" << runtime.GetTickLatencyPercentile(50)
                                        << ", tickP95Us=" << runtime.GetTickLatencyPercentile(95)
                                        << ", tickP99Us=" << runtime.GetTickLatencyPercentile(99)
                                        << ", maxCommandLatencyUs=" << runtime.GetMaxCommandLatencyMicroseconds()
                                        << ", commandP95Us=" << runtime.GetCommandLatencyPercentile(95)
                                        << ", commandP99Us=" << runtime.GetCommandLatencyPercentile(99));
        }
        else if (command == "ping")
        {
            RPG_LOG_INFO("Command", "pong");
        }
        else if (!command.empty())
        {
            RPG_LOG_WARNING("Command", "Unknown command: " << command);
        }

    }

    worldRegistry.Stop();
    server.Stop();
    sendManager.Shutdown();
    runtime.Stop();
    RPG_LOG_INFO("Lifecycle", "GameServer shutdown completed.");
    RpgLogging::Logger::Shutdown();
    WSACleanup();
    return 0;
}

