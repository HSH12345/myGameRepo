#pragma once

#include "../Game/MapScheduler.h"
#include "GameSendManager.h"

class GameRecvManager final
{
public:
    GameRecvManager(MapScheduler& newRuntime, GameSendManager& newSendManager,
                    std::uint32_t newMaxPendingCommandsPerSession, bool enableDevelopmentPackets);

    bool IsValid() const
    {
        return this->valid;
    }

private:
    bool HandleTestMessage(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize);
    bool HandleHeartbeat(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize);

    MapScheduler& runtime;
    GameSendManager& sendManager;
    const std::uint32_t maxPendingCommandsPerSession;
    bool valid = false;
};

