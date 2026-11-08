#pragma once

#include "BinaryProtocol.h"

namespace RpgProtocol
{
inline bool IsNaverFirebaseUid(const FString& uid)
{
    if (uid.Len() != 73 || !uid.StartsWith(TEXT("naver_v1_"), ESearchCase::CaseSensitive))
    {
        return false;
    }

    for (int32 i = 9; i < uid.Len(); ++i)
    {
        if (!((uid[i] >= '0' && uid[i] <= '9') || (uid[i] >= 'a' && uid[i] <= 'f')))
        {
            return false;
        }

    }

    return true;
}

struct FFirebaseLoginRequestPacket
{
    uint32 requestId = 0;
    FString idToken;

    bool IsValid() const
    {
        return this->requestId && !this->idToken.IsEmpty() &&
               FTCHARToUTF8(*this->idToken).Length() <= MAX_FIREBASE_ID_TOKEN_SIZE;
    }

    bool Serialize(TArray<uint8>& output) const
    {
        output.Reset();
        if (!IsValid())
        {
            return false;
        }

        FBinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->idToken))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const uint8* data, int32 size)
    {
        FBinaryReader reader(data, size);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->idToken) && reader.IsAtEnd() && IsValid();
    }

};

struct FFirebaseLoginResponsePacket
{
    EPacketSendResult result = EPacketSendResult::SUCCESS;
    uint32 requestId = 0;
    FString uid;

    bool IsValid() const
    {
        return this->requestId && this->result <= EPacketSendResult::SERVER_BUSY &&
               (this->result == EPacketSendResult::SUCCESS ? IsNaverFirebaseUid(this->uid) : this->uid.IsEmpty());
    }

    bool Serialize(TArray<uint8>& output) const
    {
        output.Reset();
        if (!IsValid())
        {
            return false;
        }

        FBinaryWriter writer;
        writer.WriteUInt8(static_cast<uint8>(this->result));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->uid))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const uint8* data, int32 size)
    {
        FBinaryReader reader(data, size);
        uint8 raw = 0;
        if (!reader.ReadUInt8(raw))
        {
            return false;
        }

        this->result = static_cast<EPacketSendResult>(raw);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->uid) && reader.IsAtEnd() && IsValid();
    }

};

constexpr uint16 MAX_CHARACTERS_PER_ACCOUNT = 8;
constexpr uint8 MAX_EQUIPMENT_PER_CHARACTER = 16;

enum class ECharacterListResult : uint8
{
    SUCCESS = 0,
    UNAVAILABLE = 1,
    INVALID_REQUEST = 2,
    NOT_AUTHENTICATED = 3
};

struct FEquipmentSummaryPacket
{
    uint8 slot = 0;
    uint32 itemTemplateId = 0;
    uint32 appearanceId = 0;
};

struct FCharacterSummaryPacket
{
    uint64 characterId = 0;
    FString characterName;
    uint8 race = 0;
    uint8 gender = 0;
    uint32 level = 1;
    TArray<FEquipmentSummaryPacket> equipment;
};

struct FCharacterListRequestPacket
{
    uint32 requestId = 0;

    bool Serialize(TArray<uint8>& output) const
    {
        FBinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        output = writer.Data();
        return this->requestId != 0;
    }

};

inline bool ReadCharacterSummaries(FBinaryReader& reader, TArray<FCharacterSummaryPacket>& characters)
{
    uint16 count = 0;
    if (!reader.ReadUInt16(count) || count > MAX_CHARACTERS_PER_ACCOUNT)
    {
        return false;
    }

    TArray<FCharacterSummaryPacket> parsed;
    parsed.Reserve(count);
    for (uint16 index = 0; index < count; ++index)
    {
        FCharacterSummaryPacket character;
        uint8 equipmentCount = 0;
        if (!reader.ReadUInt64(character.characterId) || character.characterId == 0 ||
            character.characterId > static_cast<uint64>(MAX_int64) || !reader.ReadString(character.characterName) ||
            character.characterName.IsEmpty() || !reader.ReadUInt8(character.race) ||
            !reader.ReadUInt8(character.gender) || !reader.ReadUInt32(character.level) || character.level > 1000 ||
            character.level == 0 || !reader.ReadUInt8(equipmentCount) || equipmentCount > MAX_EQUIPMENT_PER_CHARACTER)
        {
            return false;
        }

        for (uint8 item = 0; item < equipmentCount; ++item)
        {
            FEquipmentSummaryPacket value;
            if (!reader.ReadUInt8(value.slot) || !reader.ReadUInt32(value.itemTemplateId) ||
                !reader.ReadUInt32(value.appearanceId) || value.itemTemplateId > MAX_int32 ||
                value.appearanceId > MAX_int32)
            {
                return false;
            }

            character.equipment.Add(value);
        }

        parsed.Add(MoveTemp(character));
    }

    characters = MoveTemp(parsed);
    return true;
}

struct FCharacterListResponsePacket
{
    ECharacterListResult result = ECharacterListResult::SUCCESS;
    uint32 requestId = 0;
    TArray<FCharacterSummaryPacket> characters;

    bool Deserialize(const uint8* data, int32 size)
    {
        FBinaryReader reader(data, size);
        uint8 raw = 0;
        if (!reader.ReadUInt8(raw) || raw > static_cast<uint8>(ECharacterListResult::NOT_AUTHENTICATED) ||
            !reader.ReadUInt32(this->requestId) || this->requestId == 0 ||
            !ReadCharacterSummaries(reader, this->characters) || !reader.IsAtEnd())
        {
            return false;
        }

        this->result = static_cast<ECharacterListResult>(raw);
        return true;
    }

};

struct FTestMessagePacket
{
    FString message;

    bool Serialize(TArray<uint8>& output) const
    {
        FBinaryWriter writer;
        if (!writer.WriteString(this->message))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

};

struct FGuestLoginRequestPacket
{
    ELoginType loginType = ELoginType::GUEST;
    uint32 requestId = 0;
    FString uid;
    FString token;

    bool Serialize(TArray<uint8>& output) const
    {
        FBinaryWriter writer;
        writer.WriteUInt8(static_cast<uint8>(this->loginType));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->uid) || !writer.WriteString(this->token))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const uint8* data, int32 size)
    {
        FBinaryReader reader(data, size);
        uint8 rawLoginType = 0;
        if (!reader.ReadUInt8(rawLoginType) || rawLoginType != static_cast<uint8>(ELoginType::GUEST))
        {
            return false;
        }

        this->loginType = static_cast<ELoginType>(rawLoginType);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->uid) && reader.ReadString(this->token) &&
               reader.IsAtEnd();
    }

};

struct FGuestLoginResponsePacket
{
    EPacketSendResult result = EPacketSendResult::SUCCESS;
    uint32 requestId = 0;
    FString token;
    FString uid;

    bool Serialize(TArray<uint8>& output) const
    {
        FBinaryWriter writer;
        writer.WriteUInt8(static_cast<uint8>(this->result));
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->token) || !writer.WriteString(this->uid))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const uint8* data, int32 size)
    {
        FBinaryReader reader(data, size);
        uint8 rawResult = 0;
        if (!reader.ReadUInt8(rawResult) || rawResult > static_cast<uint8>(EPacketSendResult::SERVER_BUSY))
        {
            return false;
        }

        this->result = static_cast<EPacketSendResult>(rawResult);
        return reader.ReadUInt32(this->requestId) && reader.ReadString(this->token) && reader.ReadString(this->uid) &&
               reader.IsAtEnd();
    }

};

}

