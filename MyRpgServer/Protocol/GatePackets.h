#pragma once
#include "../ServerCore/BinaryProtocol.h"

namespace RpgProtocol
{
// The transport connection is the tunnel epoch. IDs are never reused within it.
enum class GateOperation : std::uint8_t
{
    OPEN = 1,
    DATA = 2,
    CLOSE = 3,
    PING = 4,
    PONG = 5
};

constexpr std::size_t MAX_GATE_FRAME = 64 * 1024;

struct GatePacket
{
    GateOperation operation = GateOperation::DATA;
    std::uint64_t clientId = 0;
    ByteBuffer frame;

    bool Serialize(ByteBuffer& out) const
    {
        if (this->frame.size() > MAX_GATE_FRAME)
        {
            return false;
        }

        out.clear();
        out.reserve(9 + this->frame.size());
        out.push_back(static_cast<std::uint8_t>(this->operation));
        WriteUInt64(out, this->clientId);
        out.insert(out.end(), this->frame.begin(), this->frame.end());
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        if (size < 9 || size > MAX_GATE_FRAME + 9 || data[0] < 1 || data[0] > 5)
        {
            return false;
        }

        this->operation = static_cast<GateOperation>(data[0]);
        this->clientId = ReadUInt64(data + 1);
        if ((this->operation == GateOperation::PING || this->operation == GateOperation::PONG) ? this->clientId != 0
                                                                                               : this->clientId == 0)
        {
            return false;
        }

        if (this->operation != GateOperation::DATA && size != 9)
        {
            return false;
        }

        if (this->operation == GateOperation::DATA)
        {
            FrameHeader header;
            if (!DecodeHeader(data + 9, size - 9, header) || size - 9 != HEADER_SIZE + header.payloadSize ||
                header.packetType == PacketType::GATE_FRAME)
            {
                return false;
            }

        }

        this->frame.assign(data + 9, data + size);
        return true;
    }

};

} // namespace RpgProtocol

