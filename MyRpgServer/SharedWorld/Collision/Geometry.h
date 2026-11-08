#pragma once
#include "../Core/MapDefinition.h"

namespace shared_world
{
// Limit each narrow-phase calculation to 1 km per axis. Global map size is independent.
// With a segment overlapping the triangle AABB, relative inputs are <= 2*limit and
// scalar triple products stay well below int64. Longer queries must be subdivided.
inline constexpr std::int32_t MAX_QUERY_EXTENT = 100'000;
enum class SurfaceKind : std::uint32_t
{
    SOLID = 1,
    NO_WALK = 2
};

struct Triangle
{
    WorldPosition a{}, b{}, c{};
    SurfaceKind kind = SurfaceKind::SOLID;
};

struct WideVector
{
    std::int64_t x = 0, y = 0, z = 0;
};

inline WideVector Difference(WorldPosition a, WorldPosition b) noexcept
{
    return {static_cast<std::int64_t>(a.x) - b.x, static_cast<std::int64_t>(a.y) - b.y,
            static_cast<std::int64_t>(a.z) - b.z};
}

inline WideVector Cross(WideVector a, WideVector b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline std::int64_t Dot(WideVector a, WideVector b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline bool WithinQueryExtent(WideVector v) noexcept
{
    return v.x >= -MAX_QUERY_EXTENT && v.x <= MAX_QUERY_EXTENT && v.y >= -MAX_QUERY_EXTENT && v.y <= MAX_QUERY_EXTENT &&
           v.z >= -MAX_QUERY_EXTENT && v.z <= MAX_QUERY_EXTENT;
}

inline bool ValidTriangle(const Triangle& t) noexcept
{
    if (!IsValid(t.a) || !IsValid(t.b) || !IsValid(t.c) ||
        (t.kind != SurfaceKind::SOLID && t.kind != SurfaceKind::NO_WALK))
    {
        return false;
    }

    const auto AB = Difference(t.b, t.a), AC = Difference(t.c, t.a);
    if (!WithinQueryExtent(AB) || !WithinQueryExtent(AC) || !WithinQueryExtent(Difference(t.c, t.b)))
    {
        return false;
    }

    const auto N = Cross(AB, AC);
    return N.x || N.y || N.z;
}

} // namespace shared_world

