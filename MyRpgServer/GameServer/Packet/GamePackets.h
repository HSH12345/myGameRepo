#pragma once

#include "../../ServerCore/BinaryProtocol.h"

#include <string>

namespace RpgProtocol
{
// TestMessage is the only currently shared command payload. Real gameplay
// packet types will be added after ticket authentication and session gating.
struct GameTestMessagePacket
{
    std::string message;

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadString(this->message) && reader.IsAtEnd();
    }

};

}

