#pragma once
#include "CoordinateSystem.h"
#include <algorithm>

namespace shared_world
{
using MapId = std::uint32_t;
using EntityId = std::uint64_t;

// All axes half-open: [min, max). Adjacent volumes never own the same boundary.
struct WorldBounds
{
    WorldPosition min{}, max{};

    bool IsValid() const noexcept
    {
        return shared_world::IsValid(this->min) && shared_world::IsValid(this->max) && this->min.x < this->max.x &&
               this->min.y < this->max.y && this->min.z < this->max.z;
    }

    bool Contains(WorldPosition p) const noexcept
    {
        return IsValid() && p.x >= this->min.x && p.x < this->max.x && p.y >= this->min.y && p.y < this->max.y &&
               p.z >= this->min.z && p.z < this->max.z;
    }

};

inline WorldPosition ClampPosition(WorldPosition p, const WorldBounds& bounds)
{
    if (!bounds.IsValid())
    {
        throw std::invalid_argument("invalid bounds");
    }

    // Explicit recovery/display utility, never implicitly used to accept network input.
    return {std::clamp(p.x, bounds.min.x, bounds.max.x - 1), std::clamp(p.y, bounds.min.y, bounds.max.y - 1),
            std::clamp(p.z, bounds.min.z, bounds.max.z - 1)};
}

struct MapDefinition
{
    MapId id = 0;
    std::uint32_t version = 0;
    WorldPosition origin{}; // Global origin of map-local zero (also tile lattice origin).
    WorldBounds bounds{};   // Global canonical coordinates.
    std::int32_t tileSize = 3200;
    std::int32_t aoiCellSize = 6400;
    std::int32_t collisionCellSize = 200;

    bool IsValid() const noexcept
    {
        return this->id != 0 && this->version != 0 && shared_world::IsValid(this->origin) && this->bounds.IsValid() &&
               this->tileSize > 0 && this->tileSize <= COORDINATE_LIMIT && this->aoiCellSize > 0 &&
               this->aoiCellSize <= COORDINATE_LIMIT && this->collisionCellSize > 0 &&
               this->collisionCellSize <= COORDINATE_LIMIT;
    }

};

// Map-local values are also bounded canonical centimeters, not engine world offsets.
inline std::optional<WorldPosition> OffsetPosition(WorldPosition position, WorldPosition offset, bool subtract)
{
    if (!IsValid(position) || !IsValid(offset))
    {
        return std::nullopt;
    }

    const std::int64_t SIGN = subtract ? -1 : 1;
    const auto X_VALUE = static_cast<std::int64_t>(position.x) + SIGN * offset.x;
    const auto Y = static_cast<std::int64_t>(position.y) + SIGN * offset.y;
    const auto Z = static_cast<std::int64_t>(position.z) + SIGN * offset.z;
    if (X_VALUE < -COORDINATE_LIMIT || X_VALUE > COORDINATE_LIMIT || Y < -COORDINATE_LIMIT || Y > COORDINATE_LIMIT ||
        Z < -COORDINATE_LIMIT || Z > COORDINATE_LIMIT)
    {
        return std::nullopt;
    }

    return WorldPosition{static_cast<std::int32_t>(X_VALUE), static_cast<std::int32_t>(Y),
                         static_cast<std::int32_t>(Z)};
}

inline std::optional<WorldPosition> LocalToGlobal(const MapDefinition& map, WorldPosition local)
{
    if (!map.IsValid())
    {
        return std::nullopt;
    }

    auto result = OffsetPosition(local, map.origin, false);
    return result && map.bounds.Contains(*result) ? result : std::nullopt;
}

inline std::optional<WorldPosition> GlobalToLocal(const MapDefinition& map, WorldPosition global)
{
    if (!map.IsValid() || !map.bounds.Contains(global))
    {
        return std::nullopt;
    }

    return OffsetPosition(global, map.origin, true);
}

} // namespace shared_world

