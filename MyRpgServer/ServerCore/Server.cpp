#include "Server.h"

#include "SocketUtils.h"
#include "StructuredLogger.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

Server::Server(unsigned short newPort, int initialAcceptPoolCount, int newIoThreadCount, RecvCallback callback,
               ServerOptions newOptions)
    : iocpCore(std::make_unique<IocpCore>(static_cast<DWORD>(newIoThreadCount),
                                          [this](DWORD bytes, DWORD error, ULONG_PTR key, LPOVERLAPPED overlapped)
                                          {
                                              ProcessCompletion(bytes, error, key, overlapped);
                                          })),

      acceptPool(initialAcceptPoolCount), port(newPort), ioThreadCount(newIoThreadCount),
      acceptDepth((std::max)(1, initialAcceptPoolCount)), recvCallback(std::move(callback)),
      packetSender(
          [this](const SessionPtr& session)
          {
              PostSend(session);
          },

          [this](const SessionPtr& session, const std::string& reason)
          {
              CloseSession(session, reason);
          }),

      sendBudget(std::make_shared<SendQueueBudget>((std::max)(std::size_t{1}, newOptions.maxQueuedSendBytesGlobal))),
      receiveBudget(
          std::make_shared<ReceiveBufferBudget>((std::max)(std::size_t{1}, newOptions.maxBufferedReceiveBytesGlobal))),
      options(std::move(newOptions))
{
}

Server::~Server()
{
    Stop();
    this->acceptPool.Cleanup();
}

SessionPtr Server::AdoptConnectedSocket(SOCKET socket)
{
    SessionPtr session;
    try
    {
        session = std::make_shared<Session>(this->options.sessionLimits, this->sendBudget, this->receiveBudget);
    }

    catch (...)
    {
        SocketUtils::CloseSocket(socket);
        throw;
    }

    session->socket = socket;
    session->SetRemoteAddress("127.0.0.1");
    if (!this->running || !SocketUtils::ConfigureConnectedSocket(socket) ||
        !CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), this->iocpCore->GetHandle(), 0, 0) ||
        !RegisterSession(session) || !PostRecv(session))
    {
        CloseSession(session, "outbound attachment failed");
        return {};
    }

    return session;
}

bool Server::Start()
{
    if (this->running || !this->iocpCore || !this->iocpCore->IsValid() || this->ioThreadCount <= 0)
    {
        return false;
    }

    const bool LOOPBACK = this->options.bindAddress == "127.0.0.1";
    if (!LOOPBACK && !this->options.allowInsecureNonLoopbackBind)
    {
        RPG_LOG_CRITICAL("Security", "Refused non-loopback plaintext bind. Configure TLS termination and explicitly "
                                     "allow the trusted bind address.");
        return false;
    }

    if (!this->options.outboundOnly)
    {
        this->listenSocket = SocketUtils::CreateListenSocket(this->options.bindAddress.c_str(), this->port);
        if (this->listenSocket == INVALID_SOCKET)
        {
            return false;
        }

        if (!SocketUtils::LoadAcceptEx(this->listenSocket, this->pfnAcceptEx) ||
            CreateIoCompletionPort(reinterpret_cast<HANDLE>(this->listenSocket), this->iocpCore->GetHandle(), 0, 0) ==
                nullptr)
        {
            SocketUtils::CloseSocket(this->listenSocket);
            this->listenSocket = INVALID_SOCKET;
            return false;
        }

    }

    this->running = true;
    if (!this->iocpCore->Start(static_cast<unsigned int>(this->ioThreadCount)))
    {
        this->running = false;
        SocketUtils::CloseSocket(this->listenSocket);
        this->listenSocket = INVALID_SOCKET;
        return false;
    }

    try
    {
        if (!this->options.outboundOnly)
        {
            for (int i = 0; i < this->acceptDepth; ++i)
            {
                PostNewAccept();
            }

        }

        this->maintenanceThread = std::thread(&Server::MaintenanceLoop, this);
    }

    catch (const std::exception& exception)
    {
        RPG_LOG_CRITICAL("Lifecycle", "Failed to finish server startup. error=" << exception.what());
        Stop();
        return false;
    }

    RPG_LOG_INFO("Lifecycle", "Server started. port=" << this->port << ", ioThreads=" << this->ioThreadCount);
    return true;
}

void Server::Stop()
{
    if (!this->running.exchange(false))
    {
        return;
    }

    if (this->listenSocket != INVALID_SOCKET)
    {
        CancelIoEx(reinterpret_cast<HANDLE>(this->listenSocket), nullptr);
        SocketUtils::CloseSocket(this->listenSocket);
        this->listenSocket = INVALID_SOCKET;
    }

    this->maintenanceCv.notify_all();
    if (this->maintenanceThread.joinable())
    {
        this->maintenanceThread.join();
    }

    std::vector<SessionPtr> active;
    for (auto& shard : this->sessionShards)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (auto& [id, session] : shard.sessions)
        {
            active.push_back(session);
        }

    }

    for (const auto& session : active)
    {
        CloseSession(session, "server shutdown");
    }

    {
        std::unique_lock<std::mutex> lock(this->outstandingMutex);
        const auto HARD_DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(this->options.ioDrainHardTimeoutMilliseconds);
        while (this->outstandingIo.load() != 0)
        {
            const auto REPORT_DEADLINE =
                (std::min)(HARD_DEADLINE, std::chrono::steady_clock::now() + std::chrono::seconds(10));
            if (this->outstandingCv.wait_until(lock, REPORT_DEADLINE,
                                               [this]
                                               {
                                                   return this->outstandingIo.load() == 0;
                                               }))

            {
                break;
            }

            RPG_LOG_CRITICAL(
                "IOCP",
                "I/O drain is still pending; process watchdog may terminate shutdown. outstanding="
                    << this->outstandingIo.load() << ", queuedSendBytes=" << GetQueuedSendBytes()
                    << ", bufferedReceiveBytes=" << GetBufferedReceiveBytes() << ", accept="
                    << this->outstandingIoByOperation[static_cast<std::size_t>(IoOperation::ACCEPT)].load()
                    << ", recv=" << this->outstandingIoByOperation[static_cast<std::size_t>(IoOperation::RECV)].load()
                    << ", send=" << this->outstandingIoByOperation[static_cast<std::size_t>(IoOperation::SEND)].load()
                    << ", pendingAcceptReposts=" << this->pendingAcceptReposts.load());
            if (std::chrono::steady_clock::now() >= HARD_DEADLINE)
            {
                RPG_LOG_CRITICAL(
                    "IOCP",
                    "I/O drain hard timeout reached; terminating the process to avoid unsafe context destruction.");
                lock.unlock();
                if (!TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT))
                {
                    std::abort();
                }

                std::abort();
            }

        }

    }

    if (this->iocpCore)
    {
        this->iocpCore->Stop();
    }

    RPG_LOG_INFO("Lifecycle", "Server stopped. port=" << this->port);
}

void Server::MaintenanceLoop()
{
    while (this->running)
    {
        std::unique_lock<std::mutex> waitLock(this->maintenanceMutex);
        this->maintenanceCv.wait_for(waitLock, std::chrono::seconds(1),
                                     [this]
                                     {
                                         return !this->running.load();
                                     });

        waitLock.unlock();
        if (!this->running)
        {
            break;
        }

        const auto NOW =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        std::vector<SessionPtr> timedOut;
        std::vector<SessionPtr> snapshot;
        snapshot.reserve(this->sessionCount.load(std::memory_order_relaxed));
        for (auto& shard : this->sessionShards)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            for (const auto& [id, session] : shard.sessions)
            {
                snapshot.push_back(session);
            }

        }

        for (const auto& session : snapshot)
        {
            if (session->HasTimedOut(NOW))
            {
                timedOut.push_back(session);
            }

        }

        {
            std::lock_guard<std::mutex> lock(this->admissionMutex);
            for (auto it = this->addressAdmission.begin(); it != this->addressAdmission.end();)
            {
                if (it->second.activeSessions == 0 && NOW - it->second.lastSeenMilliseconds > 60'000)
                {
                    it = this->addressAdmission.erase(it);
                }
                else
                {
                    ++it;
                }

            }

        }

        for (const auto& session : timedOut)
        {
            CloseSession(session, "session timeout");
        }

        const int ACCEPT_REPOSTS = this->pendingAcceptReposts.exchange(0);
        for (int i = 0; i < ACCEPT_REPOSTS && this->running; ++i)
        {
            PostNewAccept();
        }

    }

}

void Server::BeginIo(IoOperation operation)
{
    this->outstandingIo.fetch_add(1, std::memory_order_relaxed);
    this->outstandingIoByOperation[static_cast<std::size_t>(operation)].fetch_add(1, std::memory_order_relaxed);
}

void Server::EndIo(IoOperation operation)
{
    this->outstandingIoByOperation[static_cast<std::size_t>(operation)].fetch_sub(1, std::memory_order_relaxed);
    if (this->outstandingIo.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        this->outstandingCv.notify_all();
    }

}

void Server::ScheduleAcceptRepost()
{
    if (this->running)
    {
        this->pendingAcceptReposts.fetch_add(1, std::memory_order_relaxed);
    }

}

void Server::PostNewAccept()
{
    if (!this->running || this->options.outboundOnly)
    {
        return;
    }

    try
    {
        auto session = std::make_shared<Session>(this->options.sessionLimits, this->sendBudget, this->receiveBudget);
        session->socket = this->acceptPool.Acquire();
        if (session->socket == INVALID_SOCKET)
        {
            ScheduleAcceptRepost();
            return;
        }

        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(session->socket.load()), this->iocpCore->GetHandle(), 0,
                                   0) == nullptr)
        {
            session->Close("failed to associate accept socket");
            ScheduleAcceptRepost();
            return;
        }

        session->acceptContext.Reset(session);
        DWORD bytesReceived = 0;
        BeginIo(IoOperation::ACCEPT);
        const BOOL RESULT = this->pfnAcceptEx(this->listenSocket, session->socket.load(), session->acceptBuffer, 0,
                                              sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytesReceived,
                                              &session->acceptContext.overlapped);
        if (!RESULT && WSAGetLastError() != WSA_IO_PENDING)
        {
            EndIo(IoOperation::ACCEPT);
            session->acceptContext.keepAlive.reset();
            session->Close("AcceptEx failed");
            ScheduleAcceptRepost();
        }

    }

    catch (const std::exception& exception)
    {
        RPG_LOG_ERROR("Network", "Failed to create or post an accept operation. error=" << exception.what());
        ScheduleAcceptRepost();
    }

    catch (...)
    {
        RPG_LOG_ERROR("Network", "Failed to create or post an accept operation.");
        ScheduleAcceptRepost();
    }

}

bool Server::PostRecv(const SessionPtr& session)
{
    if (!this->running || !session || !session->IsOpen())
    {
        return false;
    }

    session->recvContext.Reset(session);
    session->recvWsabuf.buf = session->recvBuffer;
    session->recvWsabuf.len = BUFFER_SIZE;
    DWORD flags = 0;
    DWORD received = 0;
    BeginIo(IoOperation::RECV);
    const int RESULT = WSARecv(session->socket.load(), &session->recvWsabuf, 1, &received, &flags,
                               &session->recvContext.overlapped, nullptr);
    if (RESULT == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        EndIo(IoOperation::RECV);
        session->recvContext.keepAlive.reset();
        CloseSession(session, "WSARecv failed");
        return false;
    }

    return true;
}

void Server::PostSend(const SessionPtr& session)
{
    if (!this->running || !session || !session->IsOpen())
    {
        return;
    }

    auto buffer = session->CurrentSendBuffer();
    if (!buffer)
    {
        return;
    }

    auto* context = AcquireSendContext(session, std::move(buffer));
    if (!PostSendContext(context))
    {
        ReleaseSendContext(context);
    }

}

SendContext* Server::AcquireSendContext(const SessionPtr& session,
                                        std::shared_ptr<const std::vector<std::uint8_t>> buffer)
{
    std::lock_guard<std::mutex> lock(this->sendContextPoolMutex);
    SendContext* context = nullptr;
    if (!this->freeSendContexts.empty())
    {
        context = this->freeSendContexts.back();
        this->freeSendContexts.pop_back();
    }
    else
    {
        this->sendContexts.push_back(std::make_unique<SendContext>());
        context = this->sendContexts.back().get();
    }

    context->ResetSend(session, std::move(buffer));
    return context;
}

void Server::ReleaseSendContext(SendContext* context)
{
    if (!context)
    {
        return;
    }

    context->keepAlive.reset();
    context->buffer.reset();
    std::lock_guard<std::mutex> lock(this->sendContextPoolMutex);
    this->freeSendContexts.push_back(context);
}

bool Server::PostSendContext(SendContext* context)
{
    if (!context || !this->running || !context->keepAlive || !context->keepAlive->IsOpen())
    {
        return false;
    }

    context->PrepareRemaining();
    DWORD sent = 0;
    BeginIo(IoOperation::SEND);
    const int RESULT =
        WSASend(context->keepAlive->socket.load(), &context->wsabuf, 1, &sent, 0, &context->overlapped, nullptr);
    if (RESULT == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        EndIo(IoOperation::SEND);
        CloseSession(context->keepAlive, "WSASend failed");
        return false;
    }

    return true;
}

bool Server::RegisterSession(const SessionPtr& session)
{
    if (!session)
    {
        return false;
    }

    std::size_t count = this->sessionCount.load(std::memory_order_relaxed);
    while (count < this->options.maxSessions)
    {
        if (this->sessionCount.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel))
        {
            break;
        }

    }

    if (count >= this->options.maxSessions)
    {
        return false;
    }

    const auto NOW =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    {
        std::lock_guard<std::mutex> lock(this->admissionMutex);
        auto consumeToken = [NOW](double& tokens, std::int64_t& lastRefill, std::uint32_t rate)
        {
            if (lastRefill == 0)
            {
                tokens = static_cast<double>(rate);
                lastRefill = NOW;
            }
            else if (NOW > lastRefill)
            {
                tokens = (std::min)(static_cast<double>(rate), tokens + static_cast<double>(NOW - lastRefill) *
                                                                            static_cast<double>(rate) / 1000.0);
                lastRefill = NOW;
            }

            if (tokens < 1.0)
            {
                return false;
            }

            tokens -= 1.0;
            return true;
        };

        if (!consumeToken(this->globalConnectionTokens, this->globalLastTokenRefillMilliseconds,
                          this->options.maxConnectionsGlobalPerSecond))
        {
            this->sessionCount.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }

        auto& admission = this->addressAdmission[session->GetRemoteAddress()];
        admission.lastSeenMilliseconds = NOW;
        if (admission.activeSessions >= this->options.maxSessionsPerAddress ||
            !consumeToken(admission.connectionTokens, admission.lastTokenRefillMilliseconds,
                          this->options.maxConnectionsPerAddressPerSecond))
        {
            // Return the global token because address admission rejected this session.
            this->globalConnectionTokens = (std::min)(static_cast<double>(this->options.maxConnectionsGlobalPerSecond),
                                                      this->globalConnectionTokens + 1.0);
            this->sessionCount.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }

        ++admission.activeSessions;
    }

    const std::size_t SHARD_INDEX = session->GetId() % SESSION_SHARD_COUNT;
    try
    {
        std::lock_guard<std::mutex> lock(this->sessionShards[SHARD_INDEX].mutex);
        if (!this->sessionShards[SHARD_INDEX].sessions.emplace(session->GetId(), session).second)
        {
            throw std::runtime_error("duplicate session identifier");
        }

    }

    catch (const std::exception& exception)
    {
        this->sessionCount.fetch_sub(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(this->admissionMutex);
        const auto ADMISSION = this->addressAdmission.find(session->GetRemoteAddress());
        if (ADMISSION != this->addressAdmission.end() && ADMISSION->second.activeSessions != 0)
        {
            --ADMISSION->second.activeSessions;
        }

        RPG_LOG_ERROR("Lifecycle", "Failed to register session. error=" << exception.what());
        return false;
    }

    if (this->options.onSessionConnected)
    {
        try
        {
            this->options.onSessionConnected(session);
        }

        catch (const std::exception& exception)
        {
            RPG_LOG_ERROR("Lifecycle", "Session-connected callback failed. sessionId=" << session->GetId() << ", error="
                                                                                       << exception.what());
            return false;
        }

        catch (...)
        {
            RPG_LOG_ERROR("Lifecycle", "Session-connected callback failed. sessionId=" << session->GetId());
            return false;
        }

    }

    return true;
}

void Server::CloseSession(const SessionPtr& session, const std::string& reason)
{
    if (!session)
    {
        return;
    }

    bool wasRegistered = false;
    {
        const std::size_t SHARD_INDEX = session->GetId() % SESSION_SHARD_COUNT;
        std::lock_guard<std::mutex> lock(this->sessionShards[SHARD_INDEX].mutex);
        wasRegistered = this->sessionShards[SHARD_INDEX].sessions.erase(session->GetId()) != 0;
    }

    if (wasRegistered)
    {
        this->sessionCount.fetch_sub(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(this->admissionMutex);
        const auto ADMISSION = this->addressAdmission.find(session->GetRemoteAddress());
        if (ADMISSION != this->addressAdmission.end() && ADMISSION->second.activeSessions != 0)
        {
            --ADMISSION->second.activeSessions;
        }

    }

    session->Close(reason);
    if (wasRegistered && this->options.onSessionDisconnected)
    {
        try
        {
            this->options.onSessionDisconnected(session, reason);
        }

        catch (const std::exception& exception)
        {
            RPG_LOG_ERROR("Lifecycle", "Session-disconnected callback failed. sessionId="
                                           << session->GetId() << ", error=" << exception.what());
        }

        catch (...)
        {
            RPG_LOG_ERROR("Lifecycle", "Session-disconnected callback failed. sessionId=" << session->GetId());
        }

    }

}

void Server::ProcessCompletion(DWORD bytesTransferred, DWORD error, ULONG_PTR, LPOVERLAPPED overlapped)
{
    if (!overlapped)
    {
        return;
    }

    auto* context = reinterpret_cast<IoContext*>(overlapped);
    EndIo(context->operation);
    SessionPtr session = context->keepAlive;
    context->keepAlive.reset();
    if (!session)
    {
        return;
    }

    if (error != ERROR_SUCCESS || !this->running)
    {
        const IoOperation OPERATION = context->operation;
        if (OPERATION == IoOperation::SEND)
        {
            ReleaseSendContext(static_cast<SendContext*>(context));
        }

        CloseSession(session, error == ERROR_OPERATION_ABORTED ? "I/O cancelled" : "I/O completion failed");
        if (this->running && OPERATION == IoOperation::ACCEPT)
        {
            PostNewAccept();
        }

        return;
    }

    switch (context->operation)
    {
    case IoOperation::ACCEPT:
        if (!SocketUtils::UpdateAcceptContext(session->socket.load(), this->listenSocket) ||
            !SocketUtils::ConfigureConnectedSocket(session->socket.load()))
        {
            CloseSession(session, "UpdateAcceptContext failed");
        }
        else
        {
            session->SetRemoteAddress(SocketUtils::GetRemoteAddress(session->socket.load()));
            if (RegisterSession(session))
            {
                PostRecv(session);
            }
            else
            {
                CloseSession(session, "connection admission limit reached");
            }

        }

        PostNewAccept();
        break;

    case IoOperation::RECV:
        try
        {
            if (!session->OnRecv(bytesTransferred, this->recvCallback) || !session->IsOpen())
            {
                CloseSession(session, "peer closed or invalid packet");
            }
            else
            {
                PostRecv(session);
            }

        }

        catch (const std::exception& exception)
        {
            RPG_LOG_ERROR("IOCP", "Receive completion failed; closing session. sessionId="
                                      << session->GetId() << ", error=" << exception.what());
            CloseSession(session, "receive completion exception");
        }

        catch (...)
        {
            RPG_LOG_ERROR("IOCP", "Receive completion failed; closing session. sessionId=" << session->GetId());
            CloseSession(session, "receive completion exception");
        }

        break;

    case IoOperation::SEND:
    {
        auto* sendContext = static_cast<SendContext*>(context);
        sendContext->keepAlive = session;
        sendContext->offset += bytesTransferred;
        if (bytesTransferred == 0)
        {
            ReleaseSendContext(sendContext);
            CloseSession(session, "zero-byte send completion");
        }
        else if (sendContext->offset < sendContext->buffer->size())
        {
            if (!PostSendContext(sendContext))
            {
                ReleaseSendContext(sendContext);
            }

        }
        else
        {
            ReleaseSendContext(sendContext);
            if (session->CompleteCurrentSend())
            {
                PostSend(session);
            }

        }

        break;
    }

    }

}

bool Server::SendToClient(const SessionPtr& session, RpgProtocol::PacketType packetType,
                          const RpgProtocol::ByteBuffer& payload)
{
    return this->packetSender.Send(session, packetType, payload);
}

std::size_t Server::GetSessionCount() const
{
    return this->sessionCount.load(std::memory_order_acquire);
}

