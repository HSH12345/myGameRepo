// SendManager.h

#pragma once

#include <string>

#include <atomic>

#include "../../ServerCore/PacketSender.h"

#include "../../ServerCore/Session.h"

#include "../Packet/PacketHaders.h"

#include "../../Protocol/CharacterPackets.h"

#include "../../Protocol/CreateCharacterPackets.h"

#include "../../Protocol/WorldPackets.h"

class SendManager

{

public:
    static SendManager& GetInstance();

    void Init(PacketSender& sender);

    void Shutdown();

    bool SendGuestLogin(const SessionPtr& session, std::uint32_t requestId, RpgProtocol::PacketSendResult result,

                        const std::string& uid, const std::string& token);

    bool SendFirebaseLogin(const SessionPtr& session, std::uint32_t requestId, RpgProtocol::PacketSendResult result,

                           const std::string& uid = {});

    bool SendHeartbeat(const SessionPtr& session);

    bool SendCharacterList(const SessionPtr& session, const RpgProtocol::CharacterListResponsePacket& packet);

    bool SendCreateCharacter(const SessionPtr& session, const RpgProtocol::CreateCharacterResponsePacket& packet);

    bool SendWorld(const SessionPtr& session, const shared_world::net::Message& packet);

private:
    SendManager();

    std::atomic<PacketSender*> packetSender{nullptr};
};

