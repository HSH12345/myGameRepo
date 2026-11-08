#pragma once

#include "CoreMinimal.h"
#include "Containers/StringConv.h"

namespace RpgProtocol
{
constexpr uint32 MAGIC = 0x52504742; // "RPGB"
constexpr uint16 VERSION = 2;
constexpr int32 HEADER_SIZE = 12;
constexpr uint32 MAX_PAYLOAD_SIZE = 1024 * 1024;
constexpr uint32 MAX_STRING_SIZE = 64 * 1024;
constexpr uint32 MAX_FIREBASE_ID_TOKEN_SIZE = 8192;

enum class EPacketType : uint16
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
    FIREBASE_LOGIN_RESPONSE = 26
};

enum class EPacketSendResult : uint8
{
    SUCCESS = 0,
    QUERY = 1,
    WEB_NETWORK = 2,
    INVALID_PACKET = 3,
    AUTH_REJECTED = 4,
    SERVER_BUSY = 5
};

enum class ELoginType : uint8
{
    GUEST = 0
};

struct FFrameHeader
{
    uint16 versionNumber = 0;
    EPacketType packetType = EPacketType::NONE;
    uint32 payloadSize = 0;
};

inline bool IsKnownPacketType(EPacketType value)
{
    switch (value)
    {
    case EPacketType::TEST_MESSAGE:
    case EPacketType::GUEST_LOGIN_REQUEST:
    case EPacketType::GUEST_LOGIN_RESPONSE:
    case EPacketType::CHARACTER_LIST_RESPONSE:
    case EPacketType::HEARTBEAT_REQUEST:
    case EPacketType::HEARTBEAT_RESPONSE:
    case EPacketType::CHARACTER_LIST_REQUEST:
    case EPacketType::INTERNAL_CHARACTER_LIST_REQUEST:
    case EPacketType::INTERNAL_CHARACTER_LIST_RESPONSE:
    case EPacketType::CREATE_CHARACTER_REQUEST:
    case EPacketType::CREATE_CHARACTER_RESPONSE:
    case EPacketType::INTERNAL_CREATE_CHARACTER_REQUEST:
    case EPacketType::INTERNAL_CREATE_CHARACTER_RESPONSE:
    case EPacketType::INTERNAL_ACCOUNT_REQUEST:
    case EPacketType::INTERNAL_ACCOUNT_RESPONSE:
    case EPacketType::SERVICE_PRESSURE_REQUEST:
    case EPacketType::SERVICE_PRESSURE_RESPONSE:
    case EPacketType::GATE_FRAME:
    case EPacketType::WORLD_MESSAGE:
    case EPacketType::WORLD_AUTHORIZE:
    case EPacketType::INTERNAL_ECONOMY_REQUEST:
    case EPacketType::INTERNAL_ECONOMY_RESPONSE:
    case EPacketType::INTERNAL_WORLD_DATA_REQUEST:
    case EPacketType::INTERNAL_WORLD_DATA_RESPONSE:
    case EPacketType::FIREBASE_LOGIN_REQUEST:
    case EPacketType::FIREBASE_LOGIN_RESPONSE:
        return true;
    default:
        return false;
    }

}

inline void WriteUInt16(TArray<uint8>& output, uint16 value)
{
    output.Add(static_cast<uint8>((value >> 8) & 0xFF));
    output.Add(static_cast<uint8>(value & 0xFF));
}

inline void WriteUInt32(TArray<uint8>& output, uint32 value)
{
    output.Add(static_cast<uint8>((value >> 24) & 0xFF));
    output.Add(static_cast<uint8>((value >> 16) & 0xFF));
    output.Add(static_cast<uint8>((value >> 8) & 0xFF));
    output.Add(static_cast<uint8>(value & 0xFF));
}

inline void WriteUInt64(TArray<uint8>& output, uint64 value)
{
    for (int32 shift = 56; shift >= 0; shift -= 8)
    {
        output.Add(static_cast<uint8>((value >> shift) & 0xFF));
    }

}

inline uint16 ReadUInt16(const uint8* input)
{
    return static_cast<uint16>((static_cast<uint16>(input[0]) << 8) | static_cast<uint16>(input[1]));
}

inline uint32 ReadUInt32(const uint8* input)
{
    return (static_cast<uint32>(input[0]) << 24) | (static_cast<uint32>(input[1]) << 16) |
           (static_cast<uint32>(input[2]) << 8) | static_cast<uint32>(input[3]);
}

inline uint64 ReadUInt64(const uint8* input)
{
    uint64 value = 0;
    for (int32 index = 0; index < 8; ++index)
    {
        value = (value << 8) | input[index];
    }

    return value;
}

inline bool DecodeHeader(const uint8* input, int32 size, FFrameHeader& output)
{
    if (input == nullptr || size < HEADER_SIZE || ReadUInt32(input) != MAGIC)
    {
        return false;
    }

    output.versionNumber = ReadUInt16(input + 4);
    output.packetType = static_cast<EPacketType>(ReadUInt16(input + 6));
    output.payloadSize = ReadUInt32(input + 8);
    return output.versionNumber == VERSION && IsKnownPacketType(output.packetType) &&
           output.payloadSize <= MAX_PAYLOAD_SIZE;
}

inline bool BuildFrame(EPacketType packetType, const TArray<uint8>& payload, TArray<uint8>& output)
{
    if (!IsKnownPacketType(packetType) || packetType == EPacketType::NONE || payload.Num() < 0 ||
        static_cast<uint32>(payload.Num()) > MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    output.Reset(HEADER_SIZE + payload.Num());
    WriteUInt32(output, MAGIC);
    WriteUInt16(output, VERSION);
    WriteUInt16(output, static_cast<uint16>(packetType));
    WriteUInt32(output, static_cast<uint32>(payload.Num()));
    output.Append(payload);
    return true;
}

class FBinaryWriter
{
public:
    void WriteUInt8(uint8 value)
    {
        this->buffer.Add(value);
    }

    void WriteUInt16(uint16 value)
    {
        RpgProtocol::WriteUInt16(this->buffer, value);
    }

    void WriteUInt32(uint32 value)
    {
        RpgProtocol::WriteUInt32(this->buffer, value);
    }

    void WriteUInt64(uint64 value)
    {
        RpgProtocol::WriteUInt64(this->buffer, value);
    }

    bool WriteString(const FString& value)
    {
        FTCHARToUTF8 utf8(*value);
        const int32 LENGTH = utf8.Length();
        if (LENGTH < 0 || static_cast<uint32>(LENGTH) > MAX_STRING_SIZE)
        {
            return false;
        }

        RpgProtocol::WriteUInt32(this->buffer, static_cast<uint32>(LENGTH));
        if (LENGTH > 0)
        {
            this->buffer.Append(reinterpret_cast<const uint8*>(utf8.Get()), LENGTH);
        }

        return true;
    }

    const TArray<uint8>& Data() const
    {
        return this->buffer;
    }

private:
    TArray<uint8> buffer;
};

class FBinaryReader
{
public:
    FBinaryReader(const uint8* input, int32 inputSize) : data(input), size(inputSize)
    {
    }

    bool ReadUInt8(uint8& output)
    {
        if (this->data == nullptr || this->position >= this->size)
        {
            return false;
        }

        output = this->data[this->position++];
        return true;
    }

    bool ReadUInt32(uint32& output)
    {
        if (this->data == nullptr || this->position < 0 || this->size - this->position < 4)
        {
            return false;
        }

        output = RpgProtocol::ReadUInt32(this->data + this->position);
        this->position += 4;
        return true;
    }

    bool ReadUInt16(uint16& output)
    {
        if (this->data == nullptr || this->position < 0 || this->size - this->position < 2)
        {
            return false;
        }

        output = RpgProtocol::ReadUInt16(this->data + this->position);
        this->position += 2;
        return true;
    }

    bool ReadUInt64(uint64& output)
    {
        if (this->data == nullptr || this->position < 0 || this->size - this->position < 8)
        {
            return false;
        }

        output = RpgProtocol::ReadUInt64(this->data + this->position);
        this->position += 8;
        return true;
    }

    bool ReadString(FString& output)
    {
        if (this->data == nullptr || this->position < 0 || this->size - this->position < 4)
        {
            return false;
        }

        const uint32 LENGTH = RpgProtocol::ReadUInt32(this->data + this->position);
        this->position += 4;
        if (LENGTH > MAX_STRING_SIZE || LENGTH > static_cast<uint32>(this->size - this->position))
        {
            return false;
        }

        FUTF8ToTCHAR converted(reinterpret_cast<const ANSICHAR*>(this->data + this->position),
                               static_cast<int32>(LENGTH));
        output = FString(converted.Length(), converted.Get());
        this->position += static_cast<int32>(LENGTH);
        return true;
    }

    bool IsAtEnd() const
    {
        return this->position == this->size;
    }

private:
    const uint8* data = nullptr;
    int32 size = 0;
    int32 position = 0;
};

}

