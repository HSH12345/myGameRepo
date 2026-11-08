#pragma once
#include "../LogInServer/Packet/BinaryPackets.h"
#include "../Protocol/GatePackets.h"
#include "../Protocol/WorldPackets.h"
#include "../ServerCore/BoundedWorkerPool.h"
#include "../ServerCore/PacketRecvDispatcher.h"
#include "../ServerCore/Server.h"
#include <array>

class GateServer final
{
public:
    GateServer(unsigned short frontPort, unsigned short newLoginPort, unsigned short newGamePort, bool newDevelopment);

    ~GateServer()
    {
        Stop();
    }

    bool Start();
    void Stop();

    std::size_t Users()
    {
        std::lock_guard lock(this->mutex);
        return this->clients.size();
    }

private:
    struct Client
    {
        SessionPtr external, login, game;
        bool loginOpened = false, gameOpened = false, cleaned = false; // owning routing worker only
        std::atomic<bool> closeRequested{false};
        std::uint32_t selectionRequest = 0;
        shared_world::net::Message authorization;
        std::chrono::steady_clock::time_point ticketDeadline{};
        bool gameAuthorized = false;
    };

    static constexpr std::size_t WORKER_COUNT = 4, LANE_COUNT = 2;
    bool Forward(const SessionPtr&, RpgProtocol::PacketType, const std::uint8_t*, std::size_t);
    bool Reply(const SessionPtr&, const std::uint8_t*, std::size_t);
    bool Enqueue(std::uint64_t id, std::function<void()> work, std::size_t bytes = 0);
    bool Send(const SessionPtr&, RpgProtocol::GatePacket);
    void Cleanup(const std::shared_ptr<Client>&);
    void Maintain();
    void Connected(const SessionPtr&);
    static SOCKET Connect(unsigned short port);
    const unsigned short loginPort, gamePort;
    const bool development;
    PacketRecvDispatcher frontDispatcher, peerDispatcher;
    std::unique_ptr<Server> front, peers;
    std::array<std::unique_ptr<BoundedWorkerPool>, WORKER_COUNT> workers;
    std::shared_ptr<SendQueueBudget> workBudget = std::make_shared<SendQueueBudget>(32 * 1024 * 1024);
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<Client>> clients;
    std::array<SessionPtr, LANE_COUNT> loginLanes{}, gameLanes{};
    std::atomic<bool> running{false};
    std::thread maintenance;
    std::condition_variable wake;
    std::mutex waitMutex;
};

