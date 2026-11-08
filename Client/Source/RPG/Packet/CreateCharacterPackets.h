#pragma once
#include "BinaryPackets.h"

namespace RpgProtocol
{
enum class ECreateCharacterResult : uint8
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

inline bool IsOperationId(const FString& value)
{
    if (value.Len() != 32)
    {
        return false;
    }

    for (TCHAR c : value)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }

    }

    return true;
}

struct FCreateCharacterRequestPacket
{
    uint32 requestId = 0;
    FString operationId;
    FString characterName;
    uint8 race = 0;
    uint8 gender = 0;

    bool Serialize(TArray<uint8>& output) const
    {
        if (this->requestId == 0 || !IsOperationId(this->operationId) ||
            FTCHARToUTF8(*this->characterName).Length() > 96)
        {
            return false;
        }

        FBinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        if (!writer.WriteString(this->operationId) || !writer.WriteString(this->characterName))
        {
            return false;
        }

        writer.WriteUInt8(this->race);
        writer.WriteUInt8(this->gender);
        output = writer.Data();
        return true;
    }

};

struct FCreateCharacterResponsePacket
{
    ECreateCharacterResult result = ECreateCharacterResult::UNKNOWN_OUTCOME;
    uint32 requestId = 0;
    FString operationId;
    TArray<FCharacterSummaryPacket> characters;

    bool Deserialize(const uint8* data, int32 size)
    {
        FBinaryReader reader(data, size);
        uint8 raw = 0;
        if (!reader.ReadUInt8(raw) || raw > static_cast<uint8>(ECreateCharacterResult::NOT_AUTHENTICATED))
        {
            return false;
        }

        this->result = static_cast<ECreateCharacterResult>(raw);
        return reader.ReadUInt32(this->requestId) && this->requestId != 0 && reader.ReadString(this->operationId) &&
               IsOperationId(this->operationId) && ReadCharacterSummaries(reader, this->characters) &&
               reader.IsAtEnd() && this->characters.Num() == (this->result == ECreateCharacterResult::SUCCESS ? 1 : 0);
    }

};

}

