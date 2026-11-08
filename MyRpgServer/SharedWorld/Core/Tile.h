#pragma once
#include "MapDefinition.h"
#include <array>

namespace shared_world
{
// Type tags prevent silently passing a collision cell as a navigation tile.
struct TileTag
{
};

struct AoiTag
{
};

struct CollisionTag
{
};

template <class Tag> struct GridCoord
{
    std::int32_t x = 0, y = 0;

    friend constexpr bool operator==(GridCoord a, GridCoord b) noexcept
    {
        return a.x == b.x && a.y == b.y;
    }

    friend constexpr bool operator<(GridCoord a, GridCoord b) noexcept
    {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    }

};

using TileCoord = GridCoord<TileTag>;
using AoiCell = GridCoord<AoiTag>;
using CollisionCell = GridCoord<CollisionTag>;

inline std::int64_t FloorDivide(std::int64_t value, std::int32_t divisor)
{
    if (divisor <= 0)
    {
        throw std::invalid_argument("grid size must be positive");
    }

    const auto QUOTIENT = value / divisor;
    return QUOTIENT - (value % divisor < 0 ? 1 : 0);
}

template <class Tag> inline GridCoord<Tag> WorldToGrid(WorldPosition p, WorldPosition origin, std::int32_t size)
{
    if (!IsValid(p) || !IsValid(origin))
    {
        throw std::invalid_argument("invalid grid position");
    }

    return {static_cast<std::int32_t>(FloorDivide(static_cast<std::int64_t>(p.x) - origin.x, size)),
            static_cast<std::int32_t>(FloorDivide(static_cast<std::int64_t>(p.y) - origin.y, size))};
}

inline TileCoord WorldToTile(WorldPosition p, const MapDefinition& map)
{
    if (!map.IsValid())
    {
        throw std::invalid_argument("invalid map");
    }

    return WorldToGrid<TileTag>(p, map.origin, map.tileSize);
}

// Clip edge tiles to map bounds; reject out-of-map coordinates. Widen before multiplying.
inline std::optional<WorldBounds> GetTileBounds(TileCoord tile, const MapDefinition& map)
{
    if (!map.IsValid())
    {
        return std::nullopt;
    }

    const auto X_VALUE = static_cast<std::int64_t>(tile.x) * map.tileSize + map.origin.x;
    const auto Y = static_cast<std::int64_t>(tile.y) * map.tileSize + map.origin.y;
    const auto MIN_X = std::max<std::int64_t>(X_VALUE, map.bounds.min.x);
    const auto MIN_Y = std::max<std::int64_t>(Y, map.bounds.min.y);
    const auto MAX_X = std::min<std::int64_t>(X_VALUE + map.tileSize, map.bounds.max.x);
    const auto MAX_Y = std::min<std::int64_t>(Y + map.tileSize, map.bounds.max.y);
    if (MIN_X >= MAX_X || MIN_Y >= MAX_Y)
    {
        return std::nullopt;
    }

    return WorldBounds{{static_cast<std::int32_t>(MIN_X), static_cast<std::int32_t>(MIN_Y), map.bounds.min.z},
                       {static_cast<std::int32_t>(MAX_X), static_cast<std::int32_t>(MAX_Y), map.bounds.max.z}};
}

// Fixed W, E, S, N ordering. Optional avoids overflow for hostile index input.
inline std::array<std::optional<TileCoord>, 4> GetNeighborTiles(TileCoord tile) noexcept
{
    std::array<std::optional<TileCoord>, 4> result{};
    if (tile.x > INT32_MIN)
    {
        result[0] = TileCoord{tile.x - 1, tile.y};
    }

    if (tile.x < INT32_MAX)
    {
        result[1] = TileCoord{tile.x + 1, tile.y};
    }

    if (tile.y > INT32_MIN)
    {
        result[2] = TileCoord{tile.x, tile.y - 1};
    }

    if (tile.y < INT32_MAX)
    {
        result[3] = TileCoord{tile.x, tile.y + 1};
    }

    return result;
}

struct Region
{
    std::uint32_t id = 0;
    TileCoord min{}, max{}; // Half-open tile rectangle; metadata, not a worker ID.

    bool Contains(TileCoord tile) const noexcept
    {
        return this->id != 0 && tile.x >= this->min.x && tile.x < this->max.x && tile.y >= this->min.y &&
               tile.y < this->max.y;
    }

};

} // namespace shared_world

