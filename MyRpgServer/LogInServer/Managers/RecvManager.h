#pragma once
#include "../../ServerCore/PacketRecvDispatcher.h"
#include "../Packet/PacketHaders.h"

class RecvManager
{
public:
    static RecvManager& GetInstance();

    bool IsValid() const
    {
        return this->valid;
    }

    bool HandleGuestLogin(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize);
    bool HandleHeartbeat(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize);
    bool HandleCharacterList(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize);

private:
    bool HandleFirebaseLogin(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize);
    RecvManager();
    bool valid = false;
};

