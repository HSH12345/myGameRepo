#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace RpgProtocol
{
using ByteBuffer = std::vector<std::uint8_t>;

constexpr std::uint32_t MAGIC = 0x52504742; // "RPGB"
constexpr std::uint16_t VERSION = 2;
constexpr std::size_t HEADER_SIZE = 12;
constexpr std::uint32_t MAX_PAYLOAD_SIZE = 1024 * 1024;
constexpr std::uint32_t MAX_STRING_SIZE = 64 * 1024;
constexpr std::uint32_t MAX_FIREBASE_ID_TOKEN_SIZE = 8192;

enum class PacketType : std::uint16_t
{
    NONE = 0,
    TEST_MESSAGE = 1,
    GUEST_LOGIN_REQUEST = 2,
    GUEST_LOGIN_RESPONSE = 3,
    CHARACTER_LIST_RESPONSE = 4,
    HEARTBEAT_REQUEST = 5,
    HEARTBEAT_RESPONSE = 6,
    CHARACTER_LIST_REQUEST = 7,
    INTERNAL_CHARACTER_LIST_REQUEST = 8,
    INTERNAL_CHARACTER_LIST_RESPONSE = 9,
    CREATE_CHARACTER_REQUEST = 10,
    CREATE_CHARACTER_RESPONSE = 11,
    INTERNAL_CREATE_CHARACTER_REQUEST = 12,
    INTERNAL_CREATE_CHARACTER_RESPONSE = 13,
    INTERNAL_ACCOUNT_REQUEST = 14,
    INTERNAL_ACCOUNT_RESPONSE = 15,
    SERVICE_PRESSURE_REQUEST = 16,
    SERVICE_PRESSURE_RESPONSE = 17,
    GATE_FRAME = 18,
    WORLD_MESSAGE = 19,
    WORLD_AUTHORIZE = 20,
    INTERNAL_ECONOMY_REQUEST = 21,
    INTERNAL_ECONOMY_RESPONSE = 22,
    INTERNAL_WORLD_DATA_REQUEST = 23,
    INTERNAL_WORLD_DATA_RESPONSE = 24,
    FIREBASE_LOGIN_REQUEST = 25,
    FIREBASE_LOGIN_RESPONSE = 26,
    COUNT
};

enum class PacketSendResult : std::uint8_t
{
    SUCCESS = 0,
    QUERY = 1,
    WEB_NETWORK = 2,
    INVALID_PACKET = 3,
    AUTH_REJECTED = 4,
    SERVER_BUSY = 5
};

enum class LoginType : std::uint8_t
{
    GUEST = 0
};

struct FrameHeader
{
    std::uint16_t version = 0;
    PacketType packetType = PacketType::NONE;
    std::uint32_t payloadSize = 0;
};

inline bool IsKnownPacketType(PacketType value)
{
    switch (value)
    {
    case PacketType::TEST_MESSAGE:
    case PacketType::GUEST_LOGIN_REQUEST:
    case PacketType::GUEST_LOGIN_RESPONSE:
    case PacketType::CHARACTER_LIST_RESPONSE:
    case PacketType::HEARTBEAT_REQUEST:
    case PacketType::HEARTBEAT_RESPONSE:
    case PacketType::CHARACTER_LIST_REQUEST:
    case PacketType::INTERNAL_CHARACTER_LIST_REQUEST:
    case PacketType::INTERNAL_CHARACTER_LIST_RESPONSE:
    case PacketType::CREATE_CHARACTER_REQUEST:
    case PacketType::CREATE_CHARACTER_RESPONSE:
    case PacketType::INTERNAL_CREATE_CHARACTER_REQUEST:
    case PacketType::INTERNAL_CREATE_CHARACTER_RESPONSE:
    case PacketType::INTERNAL_ACCOUNT_REQUEST:
    case PacketType::INTERNAL_ACCOUNT_RESPONSE:
    case PacketType::SERVICE_PRESSURE_REQUEST:
    case PacketType::SERVICE_PRESSURE_RESPONSE:
    case PacketType::GATE_FRAME:
    case PacketType::WORLD_MESSAGE:
    case PacketType::WORLD_AUTHORIZE:
    case PacketType::INTERNAL_ECONOMY_REQUEST:
    case PacketType::INTERNAL_ECONOMY_RESPONSE:
    case PacketType::INTERNAL_WORLD_DATA_REQUEST:
    case PacketType::INTERNAL_WORLD_DATA_RESPONSE:
    case PacketType::FIREBASE_LOGIN_REQUEST:
    case PacketType::FIREBASE_LOGIN_RESPONSE:
        return true;
    default:
        return false;
    }

}

inline void WriteUInt16(ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    output.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

inline void WriteUInt32(ByteBuffer& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    output.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

inline void WriteUInt64(ByteBuffer& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }

}

inline std::uint16_t ReadUInt16(const std::uint8_t* input)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[0]) << 8) |
                                      static_cast<std::uint16_t>(input[1]));
}

inline std::uint32_t ReadUInt32(const std::uint8_t* input)
{
    return (static_cast<std::uint32_t>(input[0]) << 24) | (static_cast<std::uint32_t>(input[1]) << 16) |
           (static_cast<std::uint32_t>(input[2]) << 8) | static_cast<std::uint32_t>(input[3]);
}

inline std::uint64_t ReadUInt64(const std::uint8_t* input)
{
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index)
    {
        value = (value << 8) | input[index];
    }

    return value;
}

inline bool DecodeHeader(const std::uint8_t* input, std::size_t size, FrameHeader& output)
{
    if (input == nullptr || size < HEADER_SIZE || ReadUInt32(input) != MAGIC)
    {
        return false;
    }

    output.version = ReadUInt16(input + 4);
    output.packetType = static_cast<PacketType>(ReadUInt16(input + 6));
    output.payloadSize = ReadUInt32(input + 8);
    return output.version == VERSION && IsKnownPacketType(output.packetType) && output.payloadSize <= MAX_PAYLOAD_SIZE;
}

inline bool BuildFrame(PacketType packetType, const ByteBuffer& payload, ByteBuffer& output)
{
    if (!IsKnownPacketType(packetType) || packetType == PacketType::NONE || payload.size() > MAX_PAYLOAD_SIZE ||
        payload.size() > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }

    output.clear();
    output.reserve(HEADER_SIZE + payload.size());
    WriteUInt32(output, MAGIC);
    WriteUInt16(output, VERSION);
    WriteUInt16(output, static_cast<std::uint16_t>(packetType));
    WriteUInt32(output, static_cast<std::uint32_t>(payload.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    return true;
}

class BinaryWriter
{
public:
    void WriteUInt8(std::uint8_t value)
    {
        this->buffer.push_back(value);
    }

    void WriteUInt16(std::uint16_t value)
    {
        RpgProtocol::WriteUInt16(this->buffer, value);
    }

    void WriteUInt32(std::uint32_t value)
    {
        RpgProtocol::WriteUInt32(this->buffer, value);
    }

    void WriteUInt64(std::uint64_t value)
    {
        RpgProtocol::WriteUInt64(this->buffer, value);
    }

    bool WriteString(const std::string& value)
    {
        if (value.size() > MAX_STRING_SIZE || value.size() > (std::numeric_limits<std::uint32_t>::max)())
        {
            return false;
        }

        RpgProtocol::WriteUInt32(this->buffer, static_cast<std::uint32_t>(value.size()));
        this->buffer.insert(this->buffer.end(), value.begin(), value.end());
        return true;
    }

    const ByteBuffer& Data() const
    {
        return this->buffer;
    }

private:
    ByteBuffer buffer;
};

class BinaryReader
{
public:
    BinaryReader(const std::uint8_t* input, std::size_t inputSize) : data(input), size(inputSize)
    {
    }

    bool ReadUInt8(std::uint8_t& output)
    {
        if (this->position >= this->size || this->data == nullptr)
        {
            return false;
        }

        output = this->data[this->position++];
        return true;
    }

    bool ReadUInt32(std::uint32_t& output)
    {
        if (this->data == nullptr || this->size - this->position < sizeof(std::uint32_t))
        {
            return false;
        }

        output = RpgProtocol::ReadUInt32(this->data + this->position);
        this->position += sizeof(std::uint32_t);
        return true;
    }

    bool ReadUInt16(std::uint16_t& output)
    {
        if (this->data == nullptr || this->size - this->position < sizeof(std::uint16_t))
        {
            return false;
        }

        output = RpgProtocol::ReadUInt16(this->data + this->position);
        this->position += sizeof(std::uint16_t);
        return true;
    }

    bool ReadUInt64(std::uint64_t& output)
    {
        if (this->data == nullptr || this->size - this->position < sizeof(std::uint64_t))
        {
            return false;
        }

        output = RpgProtocol::ReadUInt64(this->data + this->position);
        this->position += sizeof(std::uint64_t);
        return true;
    }

    bool ReadString(std::string& output)
    {
        if (this->data == nullptr || this->size - this->position < sizeof(std::uint32_t))
        {
            return false;
        }

        const std::uint32_t LENGTH = RpgProtocol::ReadUInt32(this->data + this->position);
        this->position += sizeof(std::uint32_t);
        if (LENGTH > MAX_STRING_SIZE || LENGTH > this->size - this->position)
        {
            return false;
        }

        output.assign(reinterpret_cast<const char*>(this->data + this->position), LENGTH);
        this->position += LENGTH;
        return true;
    }

    bool IsAtEnd() const
    {
        return this->position == this->size;
    }

private:
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t position = 0;
};

}

