#include "Session.h"

#include "SocketUtils.h"
#include "StructuredLogger.h"
#include "BinaryProtocol.h"

#include <algorithm>

namespace
{
std::atomic<std::uint64_t> _nextSessionId{1};

std::int64_t SteadyMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

void IoContext::Reset(const SessionPtr& session)
{
    ZeroMemory(&this->overlapped, sizeof(this->overlapped));
    this->owner = session.get();
    this->keepAlive = session;
}

void SendContext::ResetSend(const SessionPtr& session, std::shared_ptr<const std::vector<std::uint8_t>> newBuffer)
{
    this->buffer = std::move(newBuffer);
    this->offset = 0;
    Reset(session);
    PrepareRemaining();
}

void SendContext::PrepareRemaining()
{
    ZeroMemory(&this->overlapped, sizeof(this->overlapped));
    this->wsabuf.buf = reinterpret_cast<char*>(const_cast<std::uint8_t*>(this->buffer->data() + this->offset));
    this->wsabuf.len = static_cast<ULONG>(this->buffer->size() - this->offset);
}

Session::Session(SessionLimits newLimits, std::shared_ptr<SendQueueBudget> newSendBudget,
                 std::shared_ptr<ReceiveBufferBudget> newReceiveBudget)
    : id(_nextSessionId.fetch_add(1, std::memory_order_relaxed)), limits(newLimits),
      sendBudget(std::move(newSendBudget)), receiveBudget(std::move(newReceiveBudget))
{
    this->recvWsabuf.buf = this->recvBuffer;
    this->recvWsabuf.len = BUFFER_SIZE;
    TouchActivity();
}

bool Session::BeginAuthentication()
{
    SessionState expected = SessionState::CONNECTED;
    return this->state.compare_exchange_strong(expected, SessionState::AUTHENTICATING);
}

bool Session::CompleteAuthentication(std::string newAccountId)
{
    std::lock_guard<std::mutex> lock(this->identityMutex);
    this->accountId = std::move(newAccountId);
    SessionState expected = SessionState::AUTHENTICATING;
    if (!this->state.compare_exchange_strong(expected, SessionState::AUTHENTICATED, std::memory_order_release))
    {
        this->accountId.clear();
        return false;
    }

    return true;
}

void Session::FailAuthentication()
{
    std::lock_guard<std::mutex> lock(this->identityMutex);
    SessionState current = this->state.load(std::memory_order_acquire);
    while (current == SessionState::AUTHENTICATING || current == SessionState::AUTHENTICATED)
    {
        if (this->state.compare_exchange_weak(current, SessionState::CONNECTED, std::memory_order_acq_rel))
        {
            this->accountId.clear();
            return;
        }

    }

}

std::string Session::GetAccountId() const
{
    std::lock_guard<std::mutex> lock(this->identityMutex);
    return this->accountId;
}

Session::~Session()
{
    Close("session destroyed");
    if (this->receiveBudget && this->reservedReceiveCapacity != 0)
    {
        this->receiveBudget->Release(this->reservedReceiveCapacity);
    }

}

bool Session::OnRecv(DWORD bytesTransferred, const RecvCallback& recvCallback)
{
    TouchActivity();
    return bytesTransferred != 0 &&
           (!recvCallback || recvCallback(shared_from_this(), this->recvBuffer, bytesTransferred));
}

void Session::TouchActivity()
{
    this->lastActivityMilliseconds = SteadyMilliseconds();
}

void Session::SetPartialFrame(bool hasPartialFrame)
{
    if (!hasPartialFrame)
    {
        this->partialFrameStartMilliseconds = 0;
    }
    else
    {
        std::int64_t expected = 0;
        this->partialFrameStartMilliseconds.compare_exchange_strong(expected, SteadyMilliseconds());
    }

}

bool Session::HasTimedOut(std::int64_t nowMilliseconds) const
{
    const auto PARTIAL_START = this->partialFrameStartMilliseconds.load();
    return nowMilliseconds - this->lastActivityMilliseconds.load() > this->limits.idleTimeoutMilliseconds ||
           (PARTIAL_START != 0 && nowMilliseconds - PARTIAL_START > this->limits.partialFrameTimeoutMilliseconds);
}

bool Session::AllowPacket(std::int64_t nowMilliseconds, std::size_t frameBytes)
{
    // Called only by the single outstanding receive completion for this session.
    if (this->receiveTokenRefillMilliseconds == 0)
    {
        this->receiveTokenRefillMilliseconds = nowMilliseconds;
        this->packetTokens = static_cast<double>(this->limits.maxPacketsPerSecond);
        this->byteTokens = static_cast<double>(this->limits.maxReceivedBytesPerSecond);
    }
    else if (nowMilliseconds > this->receiveTokenRefillMilliseconds)
    {
        const double ELAPSED_SECONDS =
            static_cast<double>(nowMilliseconds - this->receiveTokenRefillMilliseconds) / 1000.0;
        this->packetTokens =
            (std::min)(static_cast<double>(this->limits.maxPacketsPerSecond),
                       this->packetTokens + ELAPSED_SECONDS * static_cast<double>(this->limits.maxPacketsPerSecond));
        this->byteTokens = (std::min)(static_cast<double>(this->limits.maxReceivedBytesPerSecond),
                                      this->byteTokens + ELAPSED_SECONDS * static_cast<double>(
                                                                               this->limits.maxReceivedBytesPerSecond));
        this->receiveTokenRefillMilliseconds = nowMilliseconds;
    }

    if (this->packetTokens < 1.0 || this->byteTokens < static_cast<double>(frameBytes))
    {
        return false;
    }

    this->packetTokens -= 1.0;
    this->byteTokens -= static_cast<double>(frameBytes);
    return true;
}

bool Session::AppendReceived(const char* buffer, std::size_t bytes)
{
    if (!buffer || bytes == 0)
    {
        return false;
    }

    const std::size_t MAXIMUM = RpgProtocol::HEADER_SIZE + RpgProtocol::MAX_PAYLOAD_SIZE;
    if (bytes > MAXIMUM - this->receiveStream.size())
    {
        return false;
    }

    const std::size_t REQUIRED = this->receiveStream.size() + bytes;
    if (REQUIRED > this->receiveStream.capacity())
    {
        std::size_t target =
            (std::max)(REQUIRED, this->receiveStream.capacity() == 0 ? std::size_t{BUFFER_SIZE}
                                                                     : this->receiveStream.capacity() * 2);
        target = (std::min)(target, MAXIMUM);
        const std::size_t ADDITIONAL = target - this->reservedReceiveCapacity;
        if (this->receiveBudget && !this->receiveBudget->TryReserve(ADDITIONAL))
        {
            return false;
        }

        try
        {
            this->receiveStream.reserve(target);
        }

        catch (...)
        {
            if (this->receiveBudget)
            {
                this->receiveBudget->Release(ADDITIONAL);
            }

            return false;
        }

        const std::size_t ACTUAL_CAPACITY = this->receiveStream.capacity();
        if (ACTUAL_CAPACITY > target && this->receiveBudget &&
            !this->receiveBudget->TryReserve(ACTUAL_CAPACITY - target))
        {
            std::vector<std::uint8_t>().swap(this->receiveStream);
            this->receiveBudget->Release(target);
            this->reservedReceiveCapacity = 0;
            this->receiveOffset = 0;
            return false;
        }

        this->reservedReceiveCapacity = ACTUAL_CAPACITY;
    }

    const auto* received = reinterpret_cast<const std::uint8_t*>(buffer);
    this->receiveStream.insert(this->receiveStream.end(), received, received + bytes);
    return true;
}

bool Session::TryAcquirePendingCommand(std::uint32_t maximum)
{
    std::uint32_t current = this->pendingCommandCount.load(std::memory_order_relaxed);
    while (current < maximum)
    {
        if (this->pendingCommandCount.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel))
        {
            return true;
        }

    }

    return false;
}

void Session::CompletePendingCommand()
{
    std::uint32_t current = this->pendingCommandCount.load(std::memory_order_relaxed);
    while (current != 0 &&
           !this->pendingCommandCount.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel))
    {
    }

}

bool Session::EnqueueSend(std::shared_ptr<const std::vector<std::uint8_t>> buffer, bool& shouldStart)
{
    shouldStart = false;
    if (!buffer || buffer->empty() || !IsOpen())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(this->sendMutex);
    if (!this->open || this->queuedSendBytes + buffer->size() > this->limits.maxQueuedSendBytes)
    {
        return false;
    }

    if (this->sendBudget && !this->sendBudget->TryReserve(buffer->size()))
    {
        return false;
    }

    this->queuedSendBytes += buffer->size();
    this->sendQueue.push_back(std::move(buffer));
    if (!this->sendInProgress)
    {
        this->sendInProgress = true;
        shouldStart = true;
    }

    return true;
}

std::shared_ptr<const std::vector<std::uint8_t>> Session::CurrentSendBuffer()
{
    std::lock_guard<std::mutex> lock(this->sendMutex);
    return this->sendQueue.empty() ? nullptr : this->sendQueue.front();
}

bool Session::CompleteCurrentSend()
{
    std::lock_guard<std::mutex> lock(this->sendMutex);
    if (!this->sendQueue.empty())
    {
        const std::size_t COMPLETED_BYTES = this->sendQueue.front()->size();
        this->queuedSendBytes -= COMPLETED_BYTES;
        this->sendQueue.pop_front();
        if (this->sendBudget)
        {
            this->sendBudget->Release(COMPLETED_BYTES);
        }

    }

    if (this->sendQueue.empty())
    {
        this->sendInProgress = false;
        return false;
    }

    return true;
}

bool Session::Close(const std::string& reason)
{
    if (!this->open.exchange(false))
    {
        return false;
    }

    this->state = SessionState::CLOSING;
    const SOCKET CLOSING_SOCKET = this->socket.exchange(INVALID_SOCKET);
    RPG_LOG_DEBUG("Session", "Closing session. socket=" << CLOSING_SOCKET << ", reason=" << reason);
    if (CLOSING_SOCKET != INVALID_SOCKET)
    {
        shutdown(CLOSING_SOCKET, SD_BOTH);
        CancelIoEx(reinterpret_cast<HANDLE>(CLOSING_SOCKET), nullptr);
        SocketUtils::CloseSocket(CLOSING_SOCKET);
    }

    std::size_t releasedBytes = 0;
    {
        std::lock_guard<std::mutex> lock(this->sendMutex);
        releasedBytes = this->queuedSendBytes;
        this->queuedSendBytes = 0;
        this->sendQueue.clear();
        this->sendInProgress = false;
    }

    if (this->sendBudget && releasedBytes != 0)
    {
        this->sendBudget->Release(releasedBytes);
    }

    this->state = SessionState::CLOSED;
    return true;
}

