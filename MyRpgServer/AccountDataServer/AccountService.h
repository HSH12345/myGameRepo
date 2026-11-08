#pragma once
#include "../ServerCore/BoundedWorkerPool.h"
#include "../ServerCore/PacketSender.h"
#include "Managers/AccountDatabaseManager.h"
#include "../Protocol/ServicePressure.h"

class AccountService final
{
public:
    explicit AccountService(PacketSender& output) : sender(output)
    {
    }

    bool Start(unsigned int count)
    {
        return this->authentication.Start(count) && this->presence.Start(1);
    }

    void Stop()
    {
        this->authentication.Stop();
        this->presence.Stop();
    }

    std::size_t PendingCount() const
    {
        return this->authentication.PendingCount() + this->presence.PendingCount();
    }

    std::size_t OutstandingCount() const
    {
        return this->authentication.OutstandingCount() + this->presence.OutstandingCount();
    }

    bool SendPressure(const SessionPtr& session)
    {
        RpgProtocol::ByteBuffer payload;
        RpgProtocol::ServicePressureResponse response{static_cast<std::uint32_t>(OutstandingCount())};
        return response.Serialize(payload) &&
               this->sender.Send(session, RpgProtocol::PacketType::SERVICE_PRESSURE_RESPONSE, payload);
    }

    bool Enqueue(const SessionPtr& session, RpgProtocol::AccountDataRequest request)
    {
        auto quota = PendingSessionWork::Acquire(session);
        if (!quota)
        {
            return false;
        }

        const bool IS_PRESENCE = request.operation >= RpgProtocol::AccountOperation::INITIALIZE_PRESENCE &&
                                 request.operation <= RpgProtocol::AccountOperation::SHUTDOWN_PRESENCE;
        const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto fallback = [this, quota, request]
        {
            Send(quota->session, {request.requestId, request.operation, RpgProtocol::AccountResult::UNAVAILABLE, {}});
        };

        Job job;
        job.shouldCancel = [quota, DEADLINE]
        {
            return !quota->session->IsOpen() || std::chrono::steady_clock::now() >= DEADLINE;
        };

        job.onCancelled = fallback;
        job.task = [this, quota, request]
        {
            Send(quota->session, Execute(request));
        };

        if (!(IS_PRESENCE ? this->presence : this->authentication).Submit(std::move(job)))
        {
            fallback();
        }

        return true;
    }

private:
    static RpgProtocol::AccountDataResponse Execute(const RpgProtocol::AccountDataRequest& request)
    {
        using namespace RpgProtocol;
        auto& database = AccountDatabaseManager::GetInstance();
        AccountDataResponse response{request.requestId, request.operation, AccountResult::QUERY_ERROR, {}};
        bool succeeded = false;
        switch (request.operation)
        {
        case AccountOperation::LOOKUP_TOKEN:
        {
            std::string unusedToken;
            const auto RESULT = database.FindValidLoginByToken(request.token, response.uid, unusedToken);
            response.result = RESULT == LoginLookupResult::FOUND         ? AccountResult::SUCCESS
                              : RESULT == LoginLookupResult::NOT_FOUND   ? AccountResult::NOT_FOUND
                              : RESULT == LoginLookupResult::UNAVAILABLE ? AccountResult::UNAVAILABLE
                                                                         : AccountResult::QUERY_ERROR;
            return response;
        }

        case AccountOperation::INSERT_CREDENTIAL:
            succeeded = database.InsertLoginInfo(request.uid, request.token);
            break;
        case AccountOperation::ENSURE_FIREBASE_ACCOUNT:
            succeeded = database.EnsureFirebaseAccount(request.uid);
            break;
        case AccountOperation::DELETE_INACTIVE_CREDENTIAL:
            succeeded = database.DeleteLoginInfo(request.uid, request.token);
            break;
        case AccountOperation::INITIALIZE_PRESENCE:
            succeeded = database.InitializePresenceServer(request.serverInstanceId);
            break;
        case AccountOperation::RENEW_PRESENCE:
        {
            const auto RESULT = database.RenewPresenceServerLease(request.serverInstanceId);
            response.result = RESULT == PresenceLeaseRenewalResult::RENEWED     ? AccountResult::SUCCESS
                              : RESULT == PresenceLeaseRenewalResult::RECREATED ? AccountResult::RECREATED
                                                                                : AccountResult::UNAVAILABLE;
            return response;
        }

        case AccountOperation::UPDATE_PRESENCE:
            succeeded = database.UpdateLoginPresence(request.serverInstanceId, request.uid, request.active);
            break;
        case AccountOperation::SHUTDOWN_PRESENCE:
            succeeded = database.ShutdownPresenceServer(request.serverInstanceId);
            break;
        }

        if (succeeded)
        {
            response.result = AccountResult::SUCCESS;
        }

        return response;
    }

    void Send(const SessionPtr& session, const RpgProtocol::AccountDataResponse& response)
    {
        RpgProtocol::ByteBuffer bytes;
        if (session->IsOpen() && response.Serialize(bytes))
        {
            this->sender.Send(session, RpgProtocol::PacketType::INTERNAL_ACCOUNT_RESPONSE, bytes);
        }

    }

    PacketSender& sender;
    BoundedWorkerPool authentication{1024};
    // Serialize presence updates across reconnects; DB work never executes on IOCP.
    BoundedWorkerPool presence{2048};
};

