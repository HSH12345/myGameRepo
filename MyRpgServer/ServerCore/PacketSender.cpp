#include "PacketSender.h"
#include "StructuredLogger.h"

PacketSender::PacketSender(StartSendCallback callback, SendFailureCallback failureCallback)
    : startSend(std::move(callback)), sendFailure(std::move(failureCallback))
{
}

bool PacketSender::Send(const SessionPtr& session, RpgProtocol::PacketType packetType,
                        const RpgProtocol::ByteBuffer& payload)
{
    if (!session || !session->IsOpen())
    {
        return false;
    }

    auto frame = std::make_shared<RpgProtocol::ByteBuffer>();
    if (!RpgProtocol::BuildFrame(packetType, payload, *frame))
    {
        RPG_LOG_WARNING("Protocol", "Refused invalid or oversized outbound packet.");
        return false;
    }

    if (session->GetRoutedSend())
    {
        return session->GetRoutedSend()(std::move(frame));
    }

    bool shouldStart = false;
    if (!session->EnqueueSend(std::move(frame), shouldStart))
    {
        RPG_LOG_WARNING("Network", "Session/global send queue limit exceeded or session is closed.");
        if (session->IsOpen() && this->sendFailure)
        {
            this->sendFailure(session, "send queue limit exceeded");
        }

        return false;
    }

    if (shouldStart && this->startSend)
    {
        this->startSend(session);
    }

    return true;
}

