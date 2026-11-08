#pragma once

#include "../../ServerCore/BinaryProtocol.h"

#include <string>

namespace RpgProtocol
{
inline bool IsNaverFirebaseUid(const std::string& uid)
{
    if (uid.size() != 73 || uid.compare(0, 9, "naver_v1_") != 0)
    {
        return false;
    }

    for (std::size_t i = 9; i < uid.size(); ++i)
    {
        if (!((uid[i] >= '0' && uid[i] <= '9') || (uid[i] >= 'a' && uid[i] <= 'f')))
        {
            return false;
        }

    }

    return true;
}

struct FirebaseLoginRequestPacket
{
    std::uint32_t requestId = 0;
    std::string idToken;

    bool IsValid() const
    {
        return this->requestId && !this->idToken.empty() && this->idToken.size() <= MAX_FIREBASE_ID_TOKEN_SIZE;
    }

    bool Serialize(ByteBuffer& output) const
    {
        output.clear();
        if (!IsValid())
        {
            return false;
        }

        BinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->idToken))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->idToken) && reader.IsAtEnd() && IsValid();
    }

};

struct FirebaseLoginResponsePacket
{
    PacketSendResult result = PacketSendResult::SUCCESS;
    std::uint32_t requestId = 0;
    std::string uid;

    bool IsValid() const
    {
        return this->requestId && this->result <= PacketSendResult::SERVER_BUSY &&
               (this->result == PacketSendResult::SUCCESS ? IsNaverFirebaseUid(this->uid) : this->uid.empty());
    }

    bool Serialize(ByteBuffer& output) const
    {
        output.clear();
        if (!IsValid())
        {
            return false;
        }

        BinaryWriter writer;
        writer.WriteUInt8(static_cast<std::uint8_t>(this->result));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->uid))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        std::uint8_t raw = 0;
        if (!reader.ReadUInt8(raw))
        {
            return false;
        }

        this->result = static_cast<PacketSendResult>(raw);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->uid) && reader.IsAtEnd() && IsValid();
    }

};

struct TestMessagePacket
{
    std::string message;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        if (!writer.WriteString(this->message))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadString(this->message) && reader.IsAtEnd();
    }

};

struct GuestLoginRequestPacket
{
    LoginType loginType = LoginType::GUEST;
    std::uint32_t requestId = 0;
    std::string uid;
    std::string token;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        writer.WriteUInt8(static_cast<std::uint8_t>(this->loginType));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->uid) || !writer.WriteString(this->token))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        std::uint8_t rawLoginType = 0;
        if (!reader.ReadUInt8(rawLoginType) || rawLoginType != static_cast<std::uint8_t>(LoginType::GUEST))
        {
            return false;
        }

        this->loginType = static_cast<LoginType>(rawLoginType);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->uid) && reader.ReadString(this->token) &&
               reader.IsAtEnd();
    }

};

struct GuestLoginResponsePacket
{
    PacketSendResult result = PacketSendResult::SUCCESS;
    std::uint32_t requestId = 0;
    std::string token;
    std::string uid;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        writer.WriteUInt8(static_cast<std::uint8_t>(this->result));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->token) || !writer.WriteString(this->uid))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        std::uint8_t rawResult = 0;
        if (!reader.ReadUInt8(rawResult) || rawResult > static_cast<std::uint8_t>(PacketSendResult::SERVER_BUSY))
        {
            return false;
        }

        this->result = static_cast<PacketSendResult>(rawResult);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->token) && reader.ReadString(this->uid) &&
               reader.IsAtEnd();
    }

};

}

