#pragma once
#include "CharacterPackets.h"

namespace RpgProtocol
{
enum class CreateCharacterResult : std::uint8_t
{
    SUCCESS = 0,
    INVALID_NAME = 1,
    INVALID_APPEARANCE = 2,
    NAME_TAKEN = 3,
    LIMIT_REACHED = 4,
    OPERATION_CONFLICT = 5,
    BUSY = 6,
    UNAVAILABLE = 7,
    UNKNOWN_OUTCOME = 8,
    NOT_AUTHENTICATED = 9
};

inline bool IsOperationId(const std::string& value)
{
    if (value.size() != 32)
    {
        return false;
    }

    for (char c : value)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }

    }

    return true;
}

struct CreateCharacterRequestPacket
{
    std::uint32_t requestId = 0;
    std::string operationId;
    std::string characterName;
    std::uint8_t race = 0;
    std::uint8_t gender = 0;

    bool Write(BinaryWriter& writer) const
    {
        if (this->requestId == 0 || !IsOperationId(this->operationId) || this->characterName.size() > 96)
        {
            return false;
        }

        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->operationId) || !writer.WriteString(this->characterName))
        {
            return false;
        }

        writer.WriteUInt8(this->race);
        writer.WriteUInt8(this->gender);
        return true;
    }

    bool Read(BinaryReader& reader)
    {
        return reader.ReadUInt32(this->requestId) && this->requestId != 0 && reader.ReadString(this->operationId) &&
               IsOperationId(this->operationId) && reader.ReadString(this->characterName) &&
               this->characterName.size() <= 96 && reader.ReadUInt8(this->race) && reader.ReadUInt8(this->gender);
    }

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        if (!Write(writer))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return Read(reader) && reader.IsAtEnd();
    }

    std::string FingerprintInput() const
    {
        return std::to_string(this->race) + ":" + std::to_string(this->gender) + ":" + this->characterName;
    }

};

struct InternalCreateCharacterRequestPacket : CreateCharacterRequestPacket
{
    std::string accountUid;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        if (this->accountUid.empty() || this->accountUid.size() > 128 || !writer.WriteString(this->accountUid) ||
            !Write(writer))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadString(this->accountUid) && !this->accountUid.empty() && this->accountUid.size() <= 128 &&
               Read(reader) && reader.IsAtEnd();
    }

};

struct CreateCharacterResponsePacket
{
    CreateCharacterResult result = CreateCharacterResult::UNKNOWN_OUTCOME;
    std::uint32_t requestId = 0;
    std::string operationId;
    // Exactly one summary on success, none on failure. Reuses the list codec.
    std::vector<CharacterSummary> characters;

    bool Serialize(ByteBuffer& output) const
    {
        if (this->requestId == 0 || !IsOperationId(this->operationId) ||
            this->characters.size() != (this->result == CreateCharacterResult::SUCCESS ? 1U : 0U))
        {
            return false;
        }

        BinaryWriter writer;
        writer.WriteUInt8(static_cast<std::uint8_t>(this->result));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->operationId) || !WriteCharacterList(writer, this->characters))
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
        if (!reader.ReadUInt8(raw) || raw > static_cast<std::uint8_t>(CreateCharacterResult::NOT_AUTHENTICATED))
        {
            return false;
        }

        this->result = static_cast<CreateCharacterResult>(raw);
        return reader.ReadUInt32(this->requestId) && this->requestId != 0 && reader.ReadString(this->operationId) &&
               IsOperationId(this->operationId) && ReadCharacterList(reader, this->characters) && reader.IsAtEnd() &&
               this->characters.size() == (this->result == CreateCharacterResult::SUCCESS ? 1U : 0U);
    }

};

}

