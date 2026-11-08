#pragma once

#include "../ServerCore/BinaryProtocol.h"

#include <string>
#include <vector>

namespace RpgProtocol
{
constexpr std::uint16_t MAX_CHARACTERS_PER_ACCOUNT = 8;
constexpr std::uint8_t MAX_EQUIPMENT_PER_CHARACTER = 16;

enum class CharacterListResult : std::uint8_t
{
    SUCCESS = 0,
    UNAVAILABLE = 1,
    INVALID_REQUEST = 2,
    NOT_AUTHENTICATED = 3
};

struct EquipmentSummary
{
    std::uint8_t slot = 0;
    std::uint32_t itemTemplateId = 0;
    std::uint32_t appearanceId = 0;
};

struct CharacterSummary
{
    std::uint64_t characterId = 0;
    std::string characterName;
    std::uint8_t race = 0;
    std::uint8_t gender = 0;
    std::uint32_t level = 1;
    std::vector<EquipmentSummary> equipment;
};

inline bool WriteCharacterList(BinaryWriter& writer, const std::vector<CharacterSummary>& characters)
{
    if (characters.size() > MAX_CHARACTERS_PER_ACCOUNT)
    {
        return false;
    }

    writer.WriteUInt16(static_cast<std::uint16_t>(characters.size()));
    for (const CharacterSummary& character : characters)
    {
        if (character.characterId == 0 || character.characterName.empty() ||
            character.equipment.size() > MAX_EQUIPMENT_PER_CHARACTER)
        {
            return false;
        }

        writer.WriteUInt64(character.characterId);
        if (!writer.WriteString(character.characterName))
        {
            return false;
        }

        writer.WriteUInt8(character.race);
        writer.WriteUInt8(character.gender);
        writer.WriteUInt32(character.level);
        writer.WriteUInt8(static_cast<std::uint8_t>(character.equipment.size()));
        for (const EquipmentSummary& item : character.equipment)
        {
            writer.WriteUInt8(item.slot);
            writer.WriteUInt32(item.itemTemplateId);
            writer.WriteUInt32(item.appearanceId);
        }

    }

    return true;
}

inline bool ReadCharacterList(BinaryReader& reader, std::vector<CharacterSummary>& characters)
{
    std::uint16_t count = 0;
    if (!reader.ReadUInt16(count) || count > MAX_CHARACTERS_PER_ACCOUNT)
    {
        return false;
    }

    characters.clear();
    characters.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index)
    {
        CharacterSummary character;
        std::uint8_t equipmentCount = 0;
        if (!reader.ReadUInt64(character.characterId) || character.characterId == 0 ||
            !reader.ReadString(character.characterName) || character.characterName.empty() ||
            !reader.ReadUInt8(character.race) || !reader.ReadUInt8(character.gender) ||
            !reader.ReadUInt32(character.level) || !reader.ReadUInt8(equipmentCount) ||
            equipmentCount > MAX_EQUIPMENT_PER_CHARACTER)
        {
            return false;
        }

        character.equipment.reserve(equipmentCount);
        for (std::uint8_t itemIndex = 0; itemIndex < equipmentCount; ++itemIndex)
        {
            EquipmentSummary item;
            if (!reader.ReadUInt8(item.slot) || !reader.ReadUInt32(item.itemTemplateId) ||
                !reader.ReadUInt32(item.appearanceId))
            {
                return false;
            }

            character.equipment.push_back(item);
        }

        characters.push_back(std::move(character));
    }

    return true;
}

struct CharacterListRequestPacket
{
    std::uint32_t requestId = 0;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        output = writer.Data();
        return this->requestId != 0;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadUInt32(this->requestId) && this->requestId != 0 && reader.IsAtEnd();
    }

};

struct CharacterListResponsePacket
{
    CharacterListResult result = CharacterListResult::SUCCESS;
    std::uint32_t requestId = 0;
    std::vector<CharacterSummary> characters;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        writer.WriteUInt8(static_cast<std::uint8_t>(this->result));
        writer.WriteUInt32(this->requestId);
        if (!WriteCharacterList(writer, this->characters))
        {
            return false;
        }

        output = writer.Data();
        return this->requestId != 0;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        std::uint8_t rawResult = 0;
        return reader.ReadUInt8(rawResult) &&
               rawResult <= static_cast<std::uint8_t>(CharacterListResult::NOT_AUTHENTICATED) &&
               (this->result = static_cast<CharacterListResult>(rawResult), true) &&
               reader.ReadUInt32(this->requestId) && this->requestId != 0 &&
               ReadCharacterList(reader, this->characters) && reader.IsAtEnd();
    }

};

struct InternalCharacterListRequestPacket
{
    std::uint32_t requestId = 0;
    std::string accountUid;

    bool Serialize(ByteBuffer& output) const
    {
        BinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->accountUid))
        {
            return false;
        }

        output = writer.Data();
        return this->requestId != 0 && !this->accountUid.empty();
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        return reader.ReadUInt32(this->requestId) && this->requestId != 0 && reader.ReadString(this->accountUid) &&
               !this->accountUid.empty() && this->accountUid.size() <= 128 && reader.IsAtEnd();
    }

};

using InternalCharacterListResponsePacket = CharacterListResponsePacket;
}

