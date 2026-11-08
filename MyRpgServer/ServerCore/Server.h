#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <mswsock.h>

#include <atomic>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <thread>
#include <string>

#include "AcceptSocketPool.h"
#include "IocpCore.h"
#include "PacketSender.h"
#include "Session.h"

struct ServerOptions
{
    bool outboundOnly = false;
    std::string bindAddress = "127.0.0.1";
    bool allowInsecureNonLoopbackBind = false;
    std::size_t maxSessions = 10'000;
    std::size_t maxQueuedSendBytesGlobal = 512ULL * 1024 * 1024;
    std::size_t maxBufferedReceiveBytesGlobal = 512ULL * 1024 * 1024;
    std::size_t maxSessionsPerAddress = 256;
    std::uint32_t maxConnectionsPerAddressPerSecond = 64;
    std::uint32_t maxConnectionsGlobalPerSecond = 2'048;
    std::uint32_t ioDrainHardTimeoutMilliseconds = 30'000;
    SessionLimits sessionLimits;
    std::function<void(const SessionPtr&)> onSessionConnected;
    std::function<void(const SessionPtr&, const std::string&)> onSessionDisconnected;
};

class Server
{
public:
    Server(unsigned short newPort, int initialAcceptPoolCount, int newIoThreadCount, RecvCallback newRecvCallback,
           ServerOptions newOptions = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Start();
    void Stop();
    // Takes socket ownership. Called by the connector thread, never an IOCP callback.
    SessionPtr AdoptConnectedSocket(SOCKET socket);

    void Disconnect(const SessionPtr& session, const std::string& reason)
    {
        CloseSession(session, reason);
    }

    bool SendToClient(const SessionPtr& session, RpgProtocol::PacketType packetType,
                      const RpgProtocol::ByteBuffer& payload);

    PacketSender& GetPacketSender()
    {
        return this->packetSender;
    }

    std::size_t GetSessionCount() const;

    std::size_t GetQueuedSendBytes() const
    {
        return this->sendBudget ? this->sendBudget->UsedBytes() : 0;
    }

    std::size_t GetBufferedReceiveBytes() const
    {
        return this->receiveBudget ? this->receiveBudget->UsedBytes() : 0;
    }

private:
    void PostNewAccept();
    bool PostRecv(const SessionPtr& session);
    void PostSend(const SessionPtr& session);
    bool PostSendContext(SendContext* context);
    SendContext* AcquireSendContext(const SessionPtr& session, std::shared_ptr<const std::vector<std::uint8_t>> buffer);
    void ReleaseSendContext(SendContext* context);
    void ProcessCompletion(DWORD bytesTransferred, DWORD error, ULONG_PTR completionKey, LPOVERLAPPED overlapped);
    void CloseSession(const SessionPtr& session, const std::string& reason);
    bool RegisterSession(const SessionPtr& session);
    void BeginIo(IoOperation operation);
    void EndIo(IoOperation operation);
    void ScheduleAcceptRepost();
    void MaintenanceLoop();

    SOCKET listenSocket = INVALID_SOCKET;
    std::unique_ptr<IocpCore> iocpCore;
    AcceptSocketPool acceptPool;
    std::atomic<bool> running{false};
    unsigned short port = 0;
    int ioThreadCount = 0;
    int acceptDepth = 0;
    LPFN_ACCEPTEX pfnAcceptEx = nullptr;
    RecvCallback recvCallback;
    PacketSender packetSender;
    std::shared_ptr<SendQueueBudget> sendBudget;
    std::shared_ptr<ReceiveBufferBudget> receiveBudget;
    ServerOptions options;

    static constexpr std::size_t SESSION_SHARD_COUNT = 64;

    struct SessionShard
    {
        mutable std::mutex mutex;
        std::unordered_map<std::uint64_t, SessionPtr> sessions;
    };

    std::array<SessionShard, SESSION_SHARD_COUNT> sessionShards;
    std::atomic<std::size_t> sessionCount{0};

    struct AddressAdmissionState
    {
        std::size_t activeSessions = 0;
        double connectionTokens = 0.0;
        std::int64_t lastTokenRefillMilliseconds = 0;
        std::int64_t lastSeenMilliseconds = 0;
    };

    std::unordered_map<std::string, AddressAdmissionState> addressAdmission;
    mutable std::mutex admissionMutex;
    double globalConnectionTokens = 0.0;
    std::int64_t globalLastTokenRefillMilliseconds = 0;
    std::atomic<std::size_t> outstandingIo{0};
    std::array<std::atomic<std::size_t>, 3> outstandingIoByOperation{};
    std::mutex outstandingMutex;
    std::condition_variable outstandingCv;
    std::thread maintenanceThread;
    std::mutex maintenanceMutex;
    std::condition_variable maintenanceCv;
    std::atomic<int> pendingAcceptReposts{0};
    std::mutex sendContextPoolMutex;
    std::vector<std::unique_ptr<SendContext>> sendContexts;
    std::vector<SendContext*> freeSendContexts;
};

