#pragma once
#include "SourceGeometry.h"
#include "../../Map/ChunkLayout.h"

namespace shared_world
{
namespace chunk_detail
{
inline std::int32_t Axis(WorldPosition p, int axis)
{
    return axis == 0 ? p.x : p.y;
}

inline std::int32_t RoundedRatio(std::int64_t n, std::int64_t d)
{
    if (d < 0)
    {
        d = -d;
        n = -n;
    }

    const auto VALUE = n < 0 ? -((-n + d / 2) / d) : (n + d / 2) / d;
    if (VALUE < -COORDINATE_LIMIT || VALUE > COORDINATE_LIMIT)
    {
        throw std::invalid_argument("clipped coordinate overflow");
    }

    return static_cast<std::int32_t>(VALUE);
}

inline WorldPosition Intersection(WorldPosition a, WorldPosition b, int axis, std::int32_t plane)
{
    const std::int64_t D = std::int64_t(Axis(b, axis)) - Axis(a, axis), T = std::int64_t(plane) - Axis(a, axis);
    auto coord = [&](std::int32_t u, std::int32_t v)
    {
        return RoundedRatio(std::int64_t(u) * D + (std::int64_t(v) - u) * T, D);
    };

    auto p = WorldPosition{coord(a.x, b.x), coord(a.y, b.y), coord(a.z, b.z)};
    (axis == 0 ? p.x : p.y) = plane;
    return p;
}

}

// Integer clipping is deterministic even for reversed shared edges. Vertical
// extent is retained; no artificial wall is introduced at a server chunk seam.
inline SourceGeometry ClipToChunk(const SourceGeometry& input, const ChunkLayout& layout, TileCoord coordinate)
{
    layout.Validate();
    SourceGeometry out;
    out.definition = layout.world;
    out.definition.bounds = layout.Bounds(coordinate, true);
    out.contentPath = input.contentPath;
    const auto& bounds = out.definition.bounds;
    for (const auto& t : input.triangles)
    {
        if (!ValidTriangle(t))
        {
            throw std::invalid_argument("invalid chunk source triangle");
        }

        std::vector<WorldPosition> polygon = {t.a, t.b, t.c};
        for (int axis = 0; axis < 2; ++axis)
        {
            for (int side = 0; side < 2; ++side)
            {
                if (polygon.empty())
                {
                    break;
                }

                const auto PLANE = chunk_detail::Axis(side ? bounds.max : bounds.min, axis);
                auto inside = [&](WorldPosition p)
                {
                    return side ? chunk_detail::Axis(p, axis) <= PLANE : chunk_detail::Axis(p, axis) >= PLANE;
                };

                std::vector<WorldPosition> clipped;
                auto previous = polygon.back();
                bool wasInside = inside(previous);
                for (auto p : polygon)
                {
                    const bool IS_INSIDE = inside(p);
                    if (IS_INSIDE != wasInside)
                    {
                        clipped.push_back(chunk_detail::Intersection(previous, p, axis, PLANE));
                    }

                    if (IS_INSIDE)
                    {
                        clipped.push_back(p);
                    }

                    previous = p;
                    wasInside = IS_INSIDE;
                }

                polygon = std::move(clipped);
            }

        }

        for (std::size_t i = 2; i < polygon.size(); ++i)
        {
            Triangle clipped{polygon[0], polygon[i - 1], polygon[i], t.kind};
            // Quantized sub-centimeter slivers have no canonical area.
            if (ValidTriangle(clipped))
            {
                out.triangles.push_back(clipped);
            }

        }

        if (out.triangles.size() > 500000)
        {
            throw std::invalid_argument("chunk geometry budget exceeded");
        }

    }

    for (const auto& v : input.noWalkVolumes)
    {
        if (!v.IsValid())
        {
            throw std::invalid_argument("invalid source no-walk bounds");
        }

        WorldBounds clipped{
            {std::max(v.min.x, bounds.min.x), std::max(v.min.y, bounds.min.y), std::max(v.min.z, bounds.min.z)},
            {std::min(v.max.x, bounds.max.x), std::min(v.max.y, bounds.max.y), std::min(v.max.z, bounds.max.z)}};
        if (clipped.IsValid())
        {
            out.noWalkVolumes.push_back(clipped);
        }

    }

    for (const auto& s : input.spawns)
    {
        if (layout.Bounds(coordinate, false).Contains(s.feet))
        {
            out.spawns.push_back(s);
        }

    }

    return out;
}

}

