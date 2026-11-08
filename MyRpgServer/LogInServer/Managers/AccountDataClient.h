#pragma once
#include "../../Protocol/AccountDataPackets.h"
#include "../../ServerCore/InternalRpcClient.h"
#include "../../ServerCore/DependencyAdmission.h"
#include <atomic>

// LoginServer owns session/authentication orchestration, never a MySQL handle.
class AccountDataClient final
{
public:
    using Deadline = std::chrono::steady_clock::time_point;

    static AccountDataClient& GetInstance()
    {
        static AccountDataClient instance;
        return instance;
    }

    bool Configure(const std::string& host, unsigned short port)
    {
        return this->transport.Configure(host, port);
    }

    DependencyAdmission& Admission()
    {
        return this->admission;
    }

    static Deadline DefaultDeadline()
    {
        return std::chrono::steady_clock::now() + std::chrono::seconds(10);
    }

    bool InsertLoginInfo(const std::string& uid, const std::string& token, Deadline deadline = DefaultDeadline())
    {
        return Call({0, RpgProtocol::AccountOperation::INSERT_CREDENTIAL, uid, token, {}, false}, deadline).result ==
               RpgProtocol::AccountResult::SUCCESS;
    }

    bool EnsureFirebaseAccount(const std::string& uid, Deadline deadline = DefaultDeadline())
    {
        return Call({0, RpgProtocol::AccountOperation::ENSURE_FIREBASE_ACCOUNT, uid, {}, {}, false}, deadline).result ==
               RpgProtocol::AccountResult::SUCCESS;
    }

    bool DeleteLoginInfo(const std::string& uid, const std::string& token)
    {
        return Call({0, RpgProtocol::AccountOperation::DELETE_INACTIVE_CREDENTIAL, uid, token, {}, false}).result ==
               RpgProtocol::AccountResult::SUCCESS;
    }

    LoginLookupResult FindValidLoginByToken(const std::string& token, std::string& uid, std::string& returnedToken,
                                            Deadline deadline = DefaultDeadline())
    {
        uid.clear();
        returnedToken.clear();
        const auto RESPONSE = Call({0, RpgProtocol::AccountOperation::LOOKUP_TOKEN, {}, token, {}, false}, deadline);
        switch (RESPONSE.result)
        {
        case RpgProtocol::AccountResult::SUCCESS:
            if (RESPONSE.uid.empty())
            {
                return LoginLookupResult::QUERY_ERROR;
            }

            uid = RESPONSE.uid;
            returnedToken = token;
            return LoginLookupResult::FOUND;
        case RpgProtocol::AccountResult::NOT_FOUND:
            return LoginLookupResult::NOT_FOUND;
        case RpgProtocol::AccountResult::QUERY_ERROR:
            return LoginLookupResult::QUERY_ERROR;
        default:
            return LoginLookupResult::UNAVAILABLE;
        }

    }

    bool InitializePresenceServer(const std::string& instance, Deadline deadline = DefaultDeadline())
    {
        return Call({0, RpgProtocol::AccountOperation::INITIALIZE_PRESENCE, {}, {}, instance, false}, deadline)
                   .result == RpgProtocol::AccountResult::SUCCESS;
    }

    PresenceLeaseRenewalResult RenewPresenceServerLease(const std::string& instance)
    {
        const auto RESULT = Call({0, RpgProtocol::AccountOperation::RENEW_PRESENCE, {}, {}, instance, false}).result;
        return RESULT == RpgProtocol::AccountResult::RECREATED ? PresenceLeaseRenewalResult::RECREATED
               : RESULT == RpgProtocol::AccountResult::SUCCESS ? PresenceLeaseRenewalResult::RENEWED
                                                               : PresenceLeaseRenewalResult::FAILED_VALUE;
    }

    bool UpdateLoginPresence(const std::string& instance, const std::string& uid, bool active)
    {
        return Call({0, RpgProtocol::AccountOperation::UPDATE_PRESENCE, uid, {}, instance, active}).result ==
               RpgProtocol::AccountResult::SUCCESS;
    }

    bool ShutdownPresenceServer(const std::string& instance)
    {
        return Call({0, RpgProtocol::AccountOperation::SHUTDOWN_PRESENCE, {}, {}, instance, false}).result ==
               RpgProtocol::AccountResult::SUCCESS;
    }

    bool IsHealthy() const
    {
        return this->healthy.load() && Milliseconds() - this->lastSuccess.load() < 60000;
    }

private:
    static std::int64_t Milliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    RpgProtocol::AccountDataResponse Call(RpgProtocol::AccountDataRequest request,
                                          Deadline deadline = DefaultDeadline())
    {
        request.requestId = this->nextId.fetch_add(1);
        if (!request.requestId)
        {
            request.requestId = this->nextId.fetch_add(1);
        }

        RpgProtocol::AccountDataResponse response;
        RpgProtocol::ByteBuffer payload, reply;
        // Do not transparently replay a mutating RPC after an ambiguous response loss.
        if (!request.Serialize(payload) ||
            !this->transport.Exchange(RpgProtocol::PacketType::INTERNAL_ACCOUNT_REQUEST,
                                      RpgProtocol::PacketType::INTERNAL_ACCOUNT_RESPONSE, payload, reply, deadline,
                                      request.operation == RpgProtocol::AccountOperation::LOOKUP_TOKEN ? 2 : 1) ||
            !response.Deserialize(reply.data(), reply.size()) || response.requestId != request.requestId ||
            response.operation != request.operation)
        {
            this->healthy = false;
            this->admission.ObserveProbe(false);
            return {};
        }

        const bool AVAILABLE = response.result != RpgProtocol::AccountResult::UNAVAILABLE &&
                               response.result != RpgProtocol::AccountResult::QUERY_ERROR;
        this->healthy = AVAILABLE;
        if (AVAILABLE)
        {
            this->lastSuccess = Milliseconds();
        }

        if (!AVAILABLE)
        {
            this->admission.ObserveProbe(false);
        }
        else if (request.operation == RpgProtocol::AccountOperation::RENEW_PRESENCE)
        {
            this->admission.ObserveProbe(
                this->transport.IsDrained(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
        }

        return response;
    }

    InternalRpcClient transport;
    DependencyAdmission admission;
    std::atomic<std::uint32_t> nextId{1};
    std::atomic<bool> healthy{false};
    std::atomic<std::int64_t> lastSuccess{0};
};

