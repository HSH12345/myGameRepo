#pragma once
#include "../ServerCore/BoundedWorkerPool.h"
#include "../ServerCore/PacketSender.h"
#include "../Protocol/ServicePressure.h"
#include "WorldRepository.h"

class WorldService final
{
public:
    bool Start(unsigned int count, PacketSender& output)
    {
        this->sender = &output;
        return this->workers.Start(count);
    }

    void Stop()
    {
        this->workers.Stop(true);
        this->sender = nullptr;
    }

    std::size_t Outstanding() const
    {
        return this->workers.OutstandingCount();
    }

    bool Pressure(const SessionPtr& session)
    {
        RpgProtocol::ByteBuffer payload;
        RpgProtocol::ServicePressureResponse response{static_cast<std::uint32_t>(Outstanding())};
        return this->sender && response.Serialize(payload) &&
               this->sender->Send(session, RpgProtocol::PacketType::SERVICE_PRESSURE_RESPONSE, payload);
    }

    bool Enqueue(const SessionPtr& session, const RpgProtocol::WorldDataRequest& request)
    {
        auto quota = PendingSessionWork::Acquire(session);
        if (!quota)
        {
            return false;
        }

        auto send = [this, quota](const RpgProtocol::WorldDataResponse& response)
        {
            RpgProtocol::ByteBuffer payload;
            if (this->sender && quota->session->IsOpen() && response.Serialize(payload))
            {
                this->sender->Send(quota->session, RpgProtocol::PacketType::INTERNAL_WORLD_DATA_RESPONSE, payload);
            }

        };

        Job job;
        job.onCancelled = [send, request]
        {
            send({request.requestId, request.operation, RpgProtocol::WorldDataResult::UNAVAILABLE});
        };

        // Accepted registry mutations finish even if the caller disconnects.
        job.task = [send, request]
        {
            send(WorldRepository::Execute(request));
        };

        if (!this->workers.Submit(std::move(job)))
        {
            send({request.requestId, request.operation, RpgProtocol::WorldDataResult::UNAVAILABLE});
        }

        return true;
    }

private:
    BoundedWorkerPool workers{512};
    PacketSender* sender = nullptr;
};

