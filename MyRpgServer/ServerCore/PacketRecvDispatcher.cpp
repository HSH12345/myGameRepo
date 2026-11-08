#include "PacketRecvDispatcher.h"

#include "Session.h"
#include "StructuredLogger.h"

#include <iostream>
#include <chrono>

PacketRecvDispatcher& PacketRecvDispatcher::GetInstance()
{
    static PacketRecvDispatcher instance;
    return instance;
}

bool PacketRecvDispatcher::RegisterHandler(PacketId packetId, std::size_t maxPayloadSize,
                                           SessionStateMask allowedStates, HandlerFunc handler)
{
    const std::size_t INDEX = static_cast<std::size_t>(packetId);
    if (!handler || INDEX == 0 || INDEX >= this->handlers.size() || maxPayloadSize > RpgProtocol::MAX_PAYLOAD_SIZE ||
        allowedStates == 0 || this->sealed.load(std::memory_order_acquire))
    {
        RPG_LOG_ERROR("Protocol", "Rejected invalid packet handler registration. packetId="
                                      << static_cast<std::uint16_t>(packetId));
        return false;
    }

    std::lock_guard<std::mutex> lock(this->registrationMutex);
    if (this->sealed.load(std::memory_order_relaxed) || this->handlers[INDEX].handler)
    {
        RPG_LOG_ERROR("Protocol", "Rejected duplicate packet handler registration. packetId="
                                      << static_cast<std::uint16_t>(packetId));
        return false;
    }

    this->handlers[INDEX] = HandlerEntry{maxPayloadSize, allowedStates, std::move(handler)};
    return true;
}

bool PacketRecvDispatcher::Seal()
{
    std::lock_guard<std::mutex> lock(this->registrationMutex);
    this->sealed.store(true, std::memory_order_release);
    return true;
}

const PacketRecvDispatcher::HandlerEntry* PacketRecvDispatcher::GetHandler(PacketId packetId) const
{
    if (!this->sealed.load(std::memory_order_acquire))
    {
        return nullptr;
    }

    const std::size_t INDEX = static_cast<std::size_t>(packetId);
    if (INDEX >= this->handlers.size() || !this->handlers[INDEX].handler)
    {
        return nullptr;
    }

    return &this->handlers[INDEX];
}

bool PacketRecvDispatcher::Dispatch(const SessionPtr& session, const char* buffer, DWORD bytesTransferred)
{
    if (session == nullptr || buffer == nullptr || bytesTransferred == 0)
    {
        return false;
    }

    auto& stream = session->receiveStream;
    auto& readOffset = session->receiveOffset;
    if (readOffset > 0 && (readOffset >= stream.size() / 2 || stream.capacity() - stream.size() < bytesTransferred))
    {
        stream.erase(stream.begin(), stream.begin() + readOffset);
        readOffset = 0;
    }

    if (!session->AppendReceived(buffer, bytesTransferred))
    {
        RPG_LOG_WARNING("Protocol",
                        "Process receive-buffer budget or frame limit exceeded. sessionId=" << session->GetId());
        return false;
    }

    while (stream.size() - readOffset >= RpgProtocol::HEADER_SIZE)
    {
        RpgProtocol::FrameHeader header;
        if (!RpgProtocol::DecodeHeader(stream.data() + readOffset, stream.size() - readOffset, header))
        {
            RPG_LOG_WARNING("Protocol", "Invalid protocol header received.");
            return false;
        }

        const HandlerEntry* handler = GetHandler(header.packetType);
        if (!handler)
        {
            RPG_LOG_WARNING("Protocol", "Packet is not accepted by this server. packetId="
                                            << static_cast<std::uint16_t>(header.packetType));
            return false;
        }

        if ((handler->allowedStates & SessionStateBit(session->GetState())) == 0)
        {
            RPG_LOG_WARNING("Protocol", "Packet is not allowed in the current session state. packetId="
                                            << static_cast<std::uint16_t>(header.packetType)
                                            << ", sessionId=" << session->GetId());
            return false;
        }

        if (header.payloadSize > handler->maxPayloadSize)
        {
            RPG_LOG_WARNING("Protocol", "Packet payload exceeds its type limit. packetId="
                                            << static_cast<std::uint16_t>(header.packetType)
                                            << ", bytes=" << header.payloadSize);
            return false;
        }

        const std::size_t FRAME_SIZE = RpgProtocol::HEADER_SIZE + header.payloadSize;
        if (stream.size() - readOffset < FRAME_SIZE)
        {
            break;
        }

        const auto NOW_MILLISECONDS =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (!session->AllowPacket(NOW_MILLISECONDS, FRAME_SIZE))
        {
            RPG_LOG_WARNING("Protocol", "Session packet rate limit exceeded. sessionId=" << session->GetId());
            return false;
        }

        try
        {
            if (!handler->handler(session, stream.data() + readOffset + RpgProtocol::HEADER_SIZE, header.payloadSize))
            {
                return false;
            }

        }

        catch (const std::exception& exception)
        {
            RPG_LOG_ERROR("Protocol", "Packet handler threw an exception. packetId="
                                          << static_cast<std::uint16_t>(header.packetType)
                                          << ", error=" << exception.what());
            return false;
        }

        catch (...)
        {
            RPG_LOG_ERROR("Protocol", "Packet handler threw an unknown exception. packetId="
                                          << static_cast<std::uint16_t>(header.packetType));
            return false;
        }

        readOffset += FRAME_SIZE;
    }

    if (readOffset == stream.size())
    {
        stream.clear();
        readOffset = 0;
    }

    if (stream.size() - readOffset > RpgProtocol::HEADER_SIZE + RpgProtocol::MAX_PAYLOAD_SIZE)
    {
        RPG_LOG_WARNING("Protocol", "Receive buffer limit exceeded.");
        return false;
    }

    session->SetPartialFrame(stream.size() != readOffset);

    return true;
}

