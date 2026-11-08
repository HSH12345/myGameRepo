#pragma once
#include "../ServerCore/BoundedWorkerPool.h"
#include "../ServerCore/PacketSender.h"
#include "../Protocol/CreateCharacterPackets.h"
#include "../Protocol/ServicePressure.h"
#include "../Protocol/EconomyPackets.h"

class CharacterService final
{
public:
    static CharacterService& GetInstance();
    bool Start(unsigned int count, PacketSender& output);
    void Stop();

    std::size_t PendingCount() const
    {
        return this->reads.PendingCount() + this->writes.PendingCount();
    }

    std::size_t OutstandingCount() const
    {
        return this->reads.OutstandingCount() + this->writes.OutstandingCount();
    }

    bool SendPressure(const SessionPtr& session)
    {
        RpgProtocol::ByteBuffer payload;
        RpgProtocol::ServicePressureResponse response{static_cast<std::uint32_t>(OutstandingCount())};
        return this->sender && response.Serialize(payload) &&
               this->sender->Send(session, RpgProtocol::PacketType::SERVICE_PRESSURE_RESPONSE, payload);
    }

    bool Enqueue(const SessionPtr& session, const RpgProtocol::InternalCharacterListRequestPacket& request);
    bool Enqueue(const SessionPtr& session, const RpgProtocol::InternalCreateCharacterRequestPacket& request);
    bool Enqueue(const SessionPtr& session, const shared_world::economy::RpcRequest& request);

private:
    template <class Response>
    void Send(const SessionPtr& session, RpgProtocol::PacketType type, const Response& response)
    {
        RpgProtocol::ByteBuffer payload;
        if (this->sender && session->IsOpen() && response.Serialize(payload))
        {
            this->sender->Send(session, type, payload);
        }

    }

    BoundedWorkerPool reads{512};
    BoundedWorkerPool writes{1536};
    PacketSender* sender = nullptr;
};

