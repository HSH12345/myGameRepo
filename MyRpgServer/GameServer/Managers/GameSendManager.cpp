#include "GameSendManager.h"

bool GameSendManager::SendHeartbeat(const SessionPtr& session) const
{
    PacketSender* sender = this->packetSender.load(std::memory_order_acquire);
    return sender && sender->Send(session, RpgProtocol::PacketType::HEARTBEAT_RESPONSE, {});
}

