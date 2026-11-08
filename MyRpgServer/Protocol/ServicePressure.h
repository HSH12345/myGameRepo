#pragma once
#include "../ServerCore/BinaryProtocol.h"

namespace RpgProtocol
{
struct ServicePressureResponse
{
    std::uint32_t outstanding = 0;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        writer.WriteUInt32(this->outstanding);
        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadUInt32(this->outstanding) && reader.IsAtEnd();
    }

};

}

