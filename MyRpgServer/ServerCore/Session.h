#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

constexpr int ADDR_BUFFER_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;
constexpr int BUFFER_SIZE = 4 * 1024;

struct SessionLimits
{
    std::size_t maxQueuedSendBytes = 4 * 1024 * 1024;
    std::int64_t idleTimeoutMilliseconds = 60'000;
    std::int64_t partialFrameTimeoutMilliseconds = 10'000;
    std::uint32_t maxPacketsPerSecond = 200;
    std::size_t maxReceivedBytesPerSecond = 2 * 1024 * 1024;
};

class SendQueueBudget final
{
public:
    explicit SendQueueBudget(std::size_t newMaximumBytes) : maximumBytes(newMaximumBytes)
    {
    }

    bool TryReserve(std::size_t bytes)
    {
        std::size_t current = this->usedBytes.load(std::memory_order_relaxed);
        while (current <= this->maximumBytes && bytes <= this->maximumBytes - current)
        {
            if (this->usedBytes.compare_exchange_weak(current, current + bytes, std::memory_order_acq_rel))
            {
                return true;
            }

        }

        return false;
    }

    void Release(std::size_t bytes)
    {
        std::size_t current = this->usedBytes.load(std::memory_order_relaxed);
        for (;;)
        {
            const std::size_t NEXT = bytes >= current ? 0 : current - bytes;
            if (this->usedBytes.compare_exchange_weak(current, NEXT, std::memory_order_acq_rel))
            {
                return;
            }

        }

    }

    std::size_t UsedBytes() const
    {
        return this->usedBytes.load(std::memory_order_relaxed);
    }

private:
    const std::size_t maximumBytes;
    std::atomic<std::size_t> usedBytes{0};
};

// Accounts retained receive capacity, not only currently buffered bytes.
class ReceiveBufferBudget final
{
public:
    explicit ReceiveBufferBudget(std::size_t newMaximumBytes) : maximumBytes(newMaximumBytes)
    {
    }

    bool TryReserve(std::size_t bytes)
    {
        std::size_t current = this->usedBytes.load(std::memory_order_relaxed);
        while (current <= this->maximumBytes && bytes <= this->maximumBytes - current)
        {
            if (this->usedBytes.compare_exchange_weak(current, current + bytes, std::memory_order_acq_rel))
            {
                return true;
            }

        }

        return false;
    }

    void Release(std::size_t bytes)
    {
        std::size_t current = this->usedBytes.load(std::memory_order_relaxed);
        while (current != 0)
        {
            const std::size_t NEXT = bytes >= current ? 0 : current - bytes;
            if (this->usedBytes.compare_exchange_weak(current, NEXT, std::memory_order_acq_rel))
            {
                return;
            }

        }

    }

    std::size_t UsedBytes() const
    {
        return this->usedBytes.load(std::memory_order_relaxed);
    }

private:
    const std::size_t maximumBytes;
    std::atomic<std::size_t> usedBytes{0};
};

enum class SessionState : std::uint8_t
{
    CONNECTED,
    AUTHENTICATING,
    AUTHENTICATED,
    CLOSING,
    CLOSED
};

using SessionStateMask = std::uint32_t;

constexpr SessionStateMask SessionStateBit(SessionState state)
{
    return SessionStateMask{1} << static_cast<std::uint8_t>(state);
}

constexpr SessionStateMask OPEN_SESSION_STATES = SessionStateBit(SessionState::CONNECTED) |
                                                 SessionStateBit(SessionState::AUTHENTICATING) |
                                                 SessionStateBit(SessionState::AUTHENTICATED);

class Session;
using SessionPtr = std::shared_ptr<Session>;
using RecvCallback = std::function<bool(const SessionPtr& session, const char* buffer, DWORD bytesTransferred)>;

enum class IoOperation : std::uint8_t
{
    ACCEPT,
    RECV,
    SEND
};

struct IoContext
{
    OVERLAPPED overlapped = {};
    IoOperation operation;
    Session* owner = nullptr;
    SessionPtr keepAlive;

    explicit IoContext(IoOperation newOperation) : operation(newOperation)
    {
    }

    void Reset(const SessionPtr& session);
};

struct SendContext final : IoContext
{
    std::shared_ptr<const std::vector<std::uint8_t>> buffer;
    std::size_t offset = 0;
    WSABUF wsabuf = {};

    SendContext() : IoContext(IoOperation::SEND)
    {
    }

    void ResetSend(const SessionPtr& session, std::shared_ptr<const std::vector<std::uint8_t>> newBuffer);
    void PrepareRemaining();
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    explicit Session(SessionLimits newLimits = {}, std::shared_ptr<SendQueueBudget> newSendBudget = nullptr,
                     std::shared_ptr<ReceiveBufferBudget> newReceiveBudget = nullptr);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool OnRecv(DWORD bytesTransferred, const RecvCallback& recvCallback);
    bool EnqueueSend(std::shared_ptr<const std::vector<std::uint8_t>> buffer, bool& shouldStart);
    std::shared_ptr<const std::vector<std::uint8_t>> CurrentSendBuffer();
    bool CompleteCurrentSend();
    bool Close(const std::string& reason);

    bool IsOpen() const
    {
        return this->open.load(std::memory_order_acquire);
    }

    void TouchActivity();
    void SetPartialFrame(bool hasPartialFrame);
    bool HasTimedOut(std::int64_t nowMilliseconds) const;
    bool AllowPacket(std::int64_t nowMilliseconds, std::size_t frameBytes);
    bool AppendReceived(const char* buffer, std::size_t bytes);
    bool TryAcquirePendingCommand(std::uint32_t maximum);
    void CompletePendingCommand();

    std::uint32_t GetPendingCommandCount() const
    {
        return this->pendingCommandCount.load(std::memory_order_relaxed);
    }

    std::uint64_t GetId() const
    {
        return this->id;
    }

    SessionState GetState() const
    {
        return this->state.load(std::memory_order_acquire);
    }

    bool BeginAuthentication();
    bool CompleteAuthentication(std::string newAccountId);
    void FailAuthentication();
    std::string GetAccountId() const;

    void SetRemoteAddress(std::string address)
    {
        this->remoteAddress = std::move(address);
    }

    const std::string& GetRemoteAddress() const
    {
        return this->remoteAddress;
    }

    // Configure once before publication. A logical user owns no OS socket.
    using RoutedSend = std::function<bool(std::shared_ptr<const std::vector<std::uint8_t>>)>;

    void SetRoutedSend(RoutedSend value)
    {
        this->routedSend = std::move(value);
    }

    const RoutedSend& GetRoutedSend() const
    {
        return this->routedSend;
    }

    std::atomic<SOCKET> socket{INVALID_SOCKET};
    char acceptBuffer[ADDR_BUFFER_SIZE] = {};
    char recvBuffer[BUFFER_SIZE] = {};
    WSABUF recvWsabuf = {};
    IoContext acceptContext{IoOperation::ACCEPT};
    IoContext recvContext{IoOperation::RECV};

    // Serialized by the single outstanding receive operation.
    std::vector<std::uint8_t> receiveStream;
    std::size_t receiveOffset = 0;

private:
    const std::uint64_t id;
    RoutedSend routedSend;
    std::atomic<bool> open{true};
    std::atomic<SessionState> state{SessionState::CONNECTED};
    mutable std::mutex identityMutex;
    std::string accountId;
    std::string remoteAddress = "unknown";
    std::mutex sendMutex;
    std::deque<std::shared_ptr<const std::vector<std::uint8_t>>> sendQueue;
    std::size_t queuedSendBytes = 0;
    bool sendInProgress = false;
    std::atomic<std::int64_t> lastActivityMilliseconds{0};
    std::atomic<std::int64_t> partialFrameStartMilliseconds{0};
    const SessionLimits limits;
    const std::shared_ptr<SendQueueBudget> sendBudget;
    const std::shared_ptr<ReceiveBufferBudget> receiveBudget;
    std::size_t reservedReceiveCapacity = 0;
    std::int64_t receiveTokenRefillMilliseconds = 0;
    double packetTokens = 0.0;
    double byteTokens = 0.0;
    std::atomic<std::uint32_t> pendingCommandCount{0};
};

