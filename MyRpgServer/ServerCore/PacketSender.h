#pragma once

#include "BinaryProtocol.h"
#include "Session.h"

#include <functional>

class PacketSender
{
public:
    using StartSendCallback = std::function<void(const SessionPtr&)>;
    using SendFailureCallback = std::function<void(const SessionPtr&, const std::string&)>;
    PacketSender(StartSendCallback newStartSend, SendFailureCallback newSendFailure);
    bool Send(const SessionPtr& session, RpgProtocol::PacketType packetType, const RpgProtocol::ByteBuffer& payload);

private:
    StartSendCallback startSend;
    SendFailureCallback sendFailure;
};

