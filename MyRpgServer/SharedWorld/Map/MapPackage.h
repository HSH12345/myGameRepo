#pragma once
#include "../Core/Tile.h"
#include <vector>

namespace shared_world
{
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::uint32_t MAP_FORMAT_VERSION = 1;
inline constexpr std::size_t MAX_PACKAGE_BYTES = 64 * 1024 * 1024;
inline constexpr std::size_t MAX_MAP_TILES = 65'536;

// CRC64-ECMA detects accidental corruption, NOT malicious tampering or authenticity.
// The final checksum covers every byte of header and payload preceding the checksum.
inline std::uint64_t PackageChecksum(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint64_t crc = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= static_cast<std::uint64_t>(data[i]) << 56;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc << 1) ^ ((crc >> 63) ? 0x42F0E1EBA9EA3693ULL : 0);
        }

    }

    return crc;
}

namespace binary
{
inline void U32(Bytes& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }

}

inline void I32(Bytes& out, std::int32_t v)
{
    U32(out, static_cast<std::uint32_t>(v));
}

inline void Position(Bytes& out, WorldPosition p)
{
    I32(out, p.x);
    I32(out, p.y);
    I32(out, p.z);
}

class Reader
{
public:
    Reader(const Bytes& newBytes, std::size_t newLimit) : bytes(newBytes), limit(newLimit)
    {
        if (newLimit > newBytes.size())
        {
            throw std::invalid_argument("invalid reader size");
        }

    }

    std::uint32_t U32()
    {
        Require(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            v = (v << 8) | this->bytes[this->offset++];
        }

        return v;
    }

    std::int32_t I32()
    {
        const auto V = U32();
        // Avoid implementation-defined unsigned-to-signed conversion in C++17.
        const auto SIGNED_VALUE =
            V <= INT32_MAX ? static_cast<std::int64_t>(V) : static_cast<std::int64_t>(V) - 0x1'0000'0000LL;
        return static_cast<std::int32_t>(SIGNED_VALUE);
    }

    WorldPosition Position()
    {
        const auto X_VALUE = I32(), Y = I32(), Z = I32();
        return {X_VALUE, Y, Z};
    }

    Bytes Blob(std::uint32_t count)
    {
        Require(count);
        Bytes result(this->bytes.begin() + this->offset, this->bytes.begin() + this->offset + count);
        this->offset += count;
        return result;
    }

    bool Done() const noexcept
    {
        return this->offset == this->limit;
    }

private:
    void Require(std::size_t count) const
    {
        if (count > this->limit - this->offset)
        {
            throw std::invalid_argument("truncated map package");
        }

    }

    const Bytes& bytes;
    std::size_t limit, offset = 0;
};

} // namespace binary

struct TileData
{
    TileCoord coordinate{};
    std::uint32_t regionId = 0;
    Bytes collision; // Independently versioned section codecs are added by bake/query phases.
    Bytes navigation;
};

struct MapIdentity
{
    MapId id = 0;
    std::uint32_t version = 0;
    std::uint64_t checksum = 0;

    friend bool operator==(MapIdentity a, MapIdentity b) noexcept
    {
        return a.id == b.id && a.version == b.version && a.checksum == b.checksum;
    }

};

struct MapPackage
{
    MapDefinition definition;
    std::vector<Region> regions;
    std::vector<TileData> tiles; // Strictly ascending coordinates, permits binary-search lookup.

    void Validate() const
    {
        if (!this->definition.IsValid() || this->tiles.size() > MAX_MAP_TILES || this->regions.size() > MAX_MAP_TILES)
        {
            throw std::invalid_argument("invalid map definition/count");
        }

        std::uint32_t lastRegion = 0;
        for (const auto& region : this->regions)
        {
            if (region.id <= lastRegion || region.min.x >= region.max.x || region.min.y >= region.max.y)
            {
                throw std::invalid_argument("invalid or unsorted regions");
            }

            lastRegion = region.id;
        }

        std::size_t budget = 84 + this->regions.size() * 20;
        const TileData* previous = nullptr;
        for (const auto& tile : this->tiles)
        {
            if ((previous && !(previous->coordinate < tile.coordinate)) ||
                !GetTileBounds(tile.coordinate, this->definition))
            {
                throw std::invalid_argument("invalid or duplicate tile");
            }

            if (tile.regionId != 0)
            {
                const auto REGION = std::lower_bound(this->regions.begin(), this->regions.end(), tile.regionId,
                                                     [](const Region& r, std::uint32_t id)
                                                     {
                                                         return r.id < id;
                                                     });

                if (REGION == this->regions.end() || REGION->id != tile.regionId || !REGION->Contains(tile.coordinate))
                {
                    throw std::invalid_argument("invalid tile region");
                }

            }

            if (tile.collision.size() > MAX_PACKAGE_BYTES || tile.navigation.size() > MAX_PACKAGE_BYTES)
            {
                throw std::invalid_argument("oversize tile section");
            }

            budget += 20 + tile.collision.size() + tile.navigation.size();
            if (budget > MAX_PACKAGE_BYTES)
            {
                throw std::invalid_argument("oversize package");
            }

            previous = &tile;
        }

    }

    Bytes Encode() const
    {
        Validate();
        Bytes out;
        binary::U32(out, 0x52504757); // RPGW
        binary::U32(out, MAP_FORMAT_VERSION);
        binary::U32(out, this->definition.id);
        binary::U32(out, this->definition.version);
        binary::U32(out, 1); // One centimeter per integer unit.
        binary::Position(out, this->definition.origin);
        binary::Position(out, this->definition.bounds.min);
        binary::Position(out, this->definition.bounds.max);
        binary::I32(out, this->definition.tileSize);
        binary::I32(out, this->definition.aoiCellSize);
        binary::I32(out, this->definition.collisionCellSize);
        binary::U32(out, static_cast<std::uint32_t>(this->regions.size()));
        binary::U32(out, static_cast<std::uint32_t>(this->tiles.size()));
        for (const auto& region : this->regions)
        {
            binary::U32(out, region.id);
            binary::I32(out, region.min.x);
            binary::I32(out, region.min.y);
            binary::I32(out, region.max.x);
            binary::I32(out, region.max.y);
        }

        // Length-delimited tile records are also the tile table. No unchecked file offsets.
        for (const auto& tile : this->tiles)
        {
            binary::I32(out, tile.coordinate.x);
            binary::I32(out, tile.coordinate.y);
            binary::U32(out, tile.regionId);
            binary::U32(out, static_cast<std::uint32_t>(tile.collision.size()));
            binary::U32(out, static_cast<std::uint32_t>(tile.navigation.size()));
            out.insert(out.end(), tile.collision.begin(), tile.collision.end());
            out.insert(out.end(), tile.navigation.begin(), tile.navigation.end());
        }

        const auto CHECKSUM = PackageChecksum(out.data(), out.size());
        binary::U32(out, static_cast<std::uint32_t>(CHECKSUM >> 32));
        binary::U32(out, static_cast<std::uint32_t>(CHECKSUM));
        return out;
    }

    static MapPackage Decode(const Bytes& bytes, std::optional<MapIdentity> expected = std::nullopt)
    {
        if (bytes.size() < 84 || bytes.size() > MAX_PACKAGE_BYTES)
        {
            throw std::invalid_argument("invalid package length");
        }

        std::uint64_t stored = 0;
        for (std::size_t i = bytes.size() - 8; i < bytes.size(); ++i)
        {
            stored = (stored << 8) | bytes[i];
        }

        if (stored != PackageChecksum(bytes.data(), bytes.size() - 8))
        {
            throw std::invalid_argument("map checksum mismatch");
        }

        binary::Reader reader(bytes, bytes.size() - 8);
        if (reader.U32() != 0x52504757 || reader.U32() != MAP_FORMAT_VERSION)
        {
            throw std::invalid_argument("map format mismatch");
        }

        MapPackage package;
        auto& d = package.definition;
        d.id = reader.U32();
        d.version = reader.U32();
        if (expected && !(*expected == MapIdentity{d.id, d.version, stored}))
        {
            throw std::invalid_argument("map identity mismatch");
        }

        if (reader.U32() != 1)
        {
            throw std::invalid_argument("coordinate unit mismatch");
        }

        d.origin = reader.Position();
        d.bounds.min = reader.Position();
        d.bounds.max = reader.Position();
        d.tileSize = reader.I32();
        d.aoiCellSize = reader.I32();
        d.collisionCellSize = reader.I32();
        const auto REGION_COUNT = reader.U32(), TILE_COUNT = reader.U32();
        if (REGION_COUNT > MAX_MAP_TILES || TILE_COUNT > MAX_MAP_TILES)
        {
            throw std::invalid_argument("excessive map count");
        }

        for (std::uint32_t i = 0; i < REGION_COUNT; ++i)
        {
            Region r;
            r.id = reader.U32();
            r.min.x = reader.I32();
            r.min.y = reader.I32();
            r.max.x = reader.I32();
            r.max.y = reader.I32();
            package.regions.push_back(r);
        }

        for (std::uint32_t i = 0; i < TILE_COUNT; ++i)
        {
            TileData tile;
            tile.coordinate.x = reader.I32();
            tile.coordinate.y = reader.I32();
            tile.regionId = reader.U32();
            const auto COLLISION_BYTES = reader.U32(), NAVIGATION_BYTES = reader.U32();
            tile.collision = reader.Blob(COLLISION_BYTES);
            tile.navigation = reader.Blob(NAVIGATION_BYTES);
            package.tiles.push_back(std::move(tile));
        }

        if (!reader.Done())
        {
            throw std::invalid_argument("trailing map data");
        }

        package.Validate();
        return package;
    }

};

} // namespace shared_world

