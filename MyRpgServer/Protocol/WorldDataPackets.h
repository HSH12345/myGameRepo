#pragma once
#include "../ServerCore/BinaryProtocol.h"

namespace RpgProtocol
{
enum class WorldDataOperation : std::uint8_t
{
    PUBLISH = 1,
    LOOKUP = 2,
    RELEASE = 3
};

enum class WorldDataResult : std::uint8_t
{
    SUCCESS = 0,
    NOT_FOUND = 1,
    CONFLICT = 2,
    UNAVAILABLE = 3
};

struct WorldMapRecord
{
    std::uint64_t instance = 0;
    std::uint32_t map = 0, version = 0;
    std::uint64_t checksum = 0;
};

// Local registry only. GameServer remains the sole simulation owner.
struct WorldDataRequest
{
    std::uint32_t requestId = 1;
    WorldDataOperation operation = WorldDataOperation::LOOKUP;
    std::uint16_t gamePort = 9200;
    std::uint64_t boot = 0, instance = 0;
    std::vector<WorldMapRecord> maps;

    bool IsValid() const
    {
        if (!this->requestId || !this->gamePort || this->maps.size() > 64)
        {
            return false;
        }

        if (this->operation == WorldDataOperation::LOOKUP)
        {
            return !this->boot && this->instance && this->maps.empty();
        }

        if (this->operation == WorldDataOperation::RELEASE)
        {
            return this->boot && !this->instance && this->maps.empty();
        }

        if (this->operation != WorldDataOperation::PUBLISH || !this->boot || this->instance || this->maps.empty())
        {
            return false;
        }

        for (std::size_t i = 0; i < this->maps.size(); ++i)
        {
            if (!this->maps[i].instance || !this->maps[i].map || !this->maps[i].version || !this->maps[i].checksum)
            {
                return false;
            }

            for (std::size_t j = 0; j < i; ++j)
            {
                if (this->maps[j].instance == this->maps[i].instance)
                {
                    return false;
                }

            }

        }

        return true;
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
        writer.WriteUInt8(static_cast<std::uint8_t>(this->operation));
        writer.WriteUInt16(this->gamePort);
        writer.WriteUInt64(this->boot);
        writer.WriteUInt64(this->instance);
        writer.WriteUInt32(static_cast<std::uint32_t>(this->maps.size()));
        for (const auto& m : this->maps)
        {
            writer.WriteUInt64(m.instance);
            writer.WriteUInt32(m.map);
            writer.WriteUInt32(m.version);
            writer.WriteUInt64(m.checksum);
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        std::uint8_t op = 0;
        std::uint32_t count = 0;
        if (!reader.ReadUInt32(this->requestId) || !reader.ReadUInt8(op) || !reader.ReadUInt16(this->gamePort) ||
            !reader.ReadUInt64(this->boot) || !reader.ReadUInt64(this->instance) || !reader.ReadUInt32(count) ||
            count > 64)
        {
            return false;
        }

        this->operation = static_cast<WorldDataOperation>(op);
        this->maps.clear();
        this->maps.resize(count);
        for (auto& m : this->maps)
        {
            if (!reader.ReadUInt64(m.instance) || !reader.ReadUInt32(m.map) || !reader.ReadUInt32(m.version) ||
                !reader.ReadUInt64(m.checksum))
            {
                return false;
            }

        }

        return reader.IsAtEnd() && IsValid();
    }

};

struct WorldDataResponse
{
    std::uint32_t requestId = 1;
    WorldDataOperation operation = WorldDataOperation::LOOKUP;
    WorldDataResult result = WorldDataResult::UNAVAILABLE;

    bool Serialize(ByteBuffer& output) const
    {
        output.clear();
        BinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        writer.WriteUInt8(static_cast<std::uint8_t>(this->operation));
        writer.WriteUInt8(static_cast<std::uint8_t>(this->result));
        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        BinaryReader reader(data, size);
        std::uint8_t op = 0, code = 0;
        if (!reader.ReadUInt32(this->requestId) || !this->requestId || !reader.ReadUInt8(op) || op < 1 || op > 3 ||
            !reader.ReadUInt8(code) || code > 3 || !reader.IsAtEnd())
        {
            return false;
        }

        this->operation = static_cast<WorldDataOperation>(op);
        this->result = static_cast<WorldDataResult>(code);
        return true;
    }

};

}

