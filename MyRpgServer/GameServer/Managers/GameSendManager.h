#pragma once

#include "../../ServerCore/PacketSender.h"

#include <atomic>
#include "../../Protocol/WorldPackets.h"

class GameSendManager final
{
public:
    void Init(PacketSender& sender)
    {
        this->packetSender = &sender;
    }

    void Shutdown()
    {
        this->packetSender = nullptr;
    }

    bool SendHeartbeat(const SessionPtr& session) const;

    bool SendWorld(const SessionPtr& session, const shared_world::net::Message& message) const
    {
        auto* sender = this->packetSender.load();
        return sender && sender->Send(session, RpgProtocol::PacketType::WORLD_MESSAGE, message.Encode());
    }

private:
    std::atomic<PacketSender*> packetSender{nullptr};
};

