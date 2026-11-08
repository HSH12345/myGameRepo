#include "CharacterService.h"
#include "CharacterRepository.h"
#include "EconomyRepository.h"

bool CharacterService::Enqueue(const SessionPtr& session, const shared_world::economy::RpcRequest& request)
{
    auto quota = PendingSessionWork::Acquire(session);
    if (!quota)
    {
        return false;
    }

    const bool MUTATION =
        request.request.Mutation() || request.request.action == shared_world::economy::Action::ACQUIRE;
    auto fallback = [this, quota, request, MUTATION]
    {
        shared_world::economy::RpcResponse reply;
        reply.requestId = request.requestId;
        reply.operation = request.request.operation;
        reply.result =
            MUTATION ? shared_world::economy::Result::UNKNOWN_OUTCOME : shared_world::economy::Result::UNAVAILABLE;
        Send(quota->session, RpgProtocol::PacketType::INTERNAL_ECONOMY_RESPONSE, reply);
    };

    Job job;
    job.onCancelled = fallback;
    if (!MUTATION)
    {
        job.shouldCancel = [quota]
        {
            return !quota->session->IsOpen();
        };

    }

    job.task = [this, quota, request]
    {
        Send(quota->session, RpgProtocol::PacketType::INTERNAL_ECONOMY_RESPONSE,
             EconomyRepository::Get().Execute(request));
    };

    if (!(MUTATION ? this->writes : this->reads).Submit(std::move(job)))
    {
        fallback();
    }

    return true;
}

CharacterService& CharacterService::GetInstance()
{
    static CharacterService service;
    return service;
}

bool CharacterService::Start(unsigned int count, PacketSender& output)
{
    if (count < 2)
    {
        return false;
    }

    this->sender = &output;
    const auto READ_COUNT = (std::max)(1U, count / 2);
    const auto WRITE_COUNT = (std::max)(1U, count - READ_COUNT);
    if (this->reads.Start(READ_COUNT) && this->writes.Start(WRITE_COUNT))
    {
        return true;
    }

    Stop();
    return false;
}

void CharacterService::Stop()
{
    this->reads.Stop();
    this->writes.Stop(true);
    this->sender = nullptr;
}

bool CharacterService::Enqueue(const SessionPtr& session,
                               const RpgProtocol::InternalCharacterListRequestPacket& request)
{
    auto quota = PendingSessionWork::Acquire(session);
    if (!quota)
    {
        return false;
    }

    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto fallback = [this, quota, request]
    {
        RpgProtocol::CharacterListResponsePacket response;
        response.requestId = request.requestId;
        response.result = RpgProtocol::CharacterListResult::UNAVAILABLE;
        Send(quota->session, RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_RESPONSE, response);
    };

    Job job;
    job.shouldCancel = [quota, DEADLINE]
    {
        return !quota->session->IsOpen() || std::chrono::steady_clock::now() >= DEADLINE;
    };

    job.onCancelled = fallback;
    job.task = [this, quota, request]
    {
        RpgProtocol::CharacterListResponsePacket response;
        response.requestId = request.requestId;
        response.result = CharacterRepository::GetInstance().FindByAccount(request.accountUid, response.characters)
                              ? RpgProtocol::CharacterListResult::SUCCESS
                              : RpgProtocol::CharacterListResult::UNAVAILABLE;
        Send(quota->session, RpgProtocol::PacketType::INTERNAL_CHARACTER_LIST_RESPONSE, response);
    };

    if (!this->reads.Submit(std::move(job)))
    {
        fallback();
    }

    return true;
}

bool CharacterService::Enqueue(const SessionPtr& session,
                               const RpgProtocol::InternalCreateCharacterRequestPacket& request)
{
    auto quota = PendingSessionWork::Acquire(session);
    if (!quota)
    {
        return false;
    }

    auto fallback = [this, quota, request]
    {
        RpgProtocol::CreateCharacterResponsePacket response;
        response.requestId = request.requestId;
        response.operationId = request.operationId;
        response.result = RpgProtocol::CreateCharacterResult::UNKNOWN_OUTCOME;
        Send(quota->session, RpgProtocol::PacketType::INTERNAL_CREATE_CHARACTER_RESPONSE, response);
    };

    Job job;
    job.onCancelled = fallback;
    // Once accepted, queued mutations also survive socket closure.
    job.task = [this, quota, request]
    {
        auto response = CharacterRepository::GetInstance().Create(request);
        Send(quota->session, RpgProtocol::PacketType::INTERNAL_CREATE_CHARACTER_RESPONSE, response);
    };

    if (!this->writes.Submit(std::move(job)))
    {
        fallback();
    }

    return true;
}

