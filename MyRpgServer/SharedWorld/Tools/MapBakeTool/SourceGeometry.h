#pragma once
#include "../../Collision/Geometry.h"
#include "../../Map/MapPackage.h"
#include "../../Map/MapManifest.h"
#include <istream>
#include <map>
#include <string>
#include <tuple>

namespace shared_world
{
struct SourceGeometry
{
    MapDefinition definition;
    std::vector<Triangle> triangles;
    std::vector<WorldBounds> noWalkVolumes;
    std::string contentPath;
    std::vector<SpawnPoint> spawns;
};

inline SourceGeometry ReadSourceGeometry(std::istream& stream)
{
    std::string magic;
    std::uint32_t version = 0;
    SourceGeometry source;
    auto& d = source.definition;
    if (!(stream >> magic >> version) || magic != "RPGW_SOURCE" || (version != 1 && version != 2))
    {
        throw std::invalid_argument("invalid geometry source format");
    }

    if (!(stream >> d.id >> d.version >> d.origin.x >> d.origin.y >> d.origin.z >> d.bounds.min.x >> d.bounds.min.y >>
          d.bounds.min.z >> d.bounds.max.x >> d.bounds.max.y >> d.bounds.max.z >> d.tileSize >> d.aoiCellSize >>
          d.collisionCellSize) ||
        !d.IsValid())
    {
        throw std::invalid_argument("invalid source map definition");
    }

    std::uint32_t count = 0;
    if (!(stream >> count) || count == 0 || count > 500'000)
    {
        throw std::invalid_argument("invalid source triangle count");
    }

    source.triangles.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        Triangle t;
        std::uint32_t kind = 0;
        if (!(stream >> kind >> t.a.x >> t.a.y >> t.a.z >> t.b.x >> t.b.y >> t.b.z >> t.c.x >> t.c.y >> t.c.z) ||
            kind < 1 || kind > 2 || !IsValid(t.a) || !IsValid(t.b) || !IsValid(t.c))
        {
            throw std::invalid_argument("invalid source triangle");
        }

        t.kind = static_cast<SurfaceKind>(kind);
        source.triangles.push_back(t);
    }

    std::uint32_t volumeCount = 0;
    if (!(stream >> volumeCount) || volumeCount > 4096)
    {
        throw std::invalid_argument("invalid no-walk volume count");
    }

    for (std::uint32_t i = 0; i < volumeCount; ++i)
    {
        WorldBounds b;
        if (!(stream >> b.min.x >> b.min.y >> b.min.z >> b.max.x >> b.max.y >> b.max.z) || !b.IsValid())
        {
            throw std::invalid_argument("invalid no-walk volume");
        }

        source.noWalkVolumes.push_back(b);
    }

    if (version == 2)
    {
        if (!(stream >> source.contentPath >> count) || count > 4096)
        {
            throw std::invalid_argument("invalid source metadata");
        }

        for (std::uint32_t i = 0; i < count; ++i)
        {
            SpawnPoint spawn;
            if (!(stream >> spawn.id >> spawn.feet.x >> spawn.feet.y >> spawn.feet.z >> spawn.yawMilliDegrees))
            {
                throw std::invalid_argument("invalid source spawn");
            }

            source.spawns.push_back(spawn);
        }

    }

    std::string extra;
    if (stream >> extra)
    {
        throw std::invalid_argument("trailing source geometry");
    }

    return source;
}

inline MapPackage BakeMap(SourceGeometry source)
{
    if (!source.definition.IsValid() || source.triangles.empty() || source.triangles.size() > 500'000 ||
        source.noWalkVolumes.size() > 4096)
    {
        throw std::invalid_argument("invalid bake input");
    }

    const auto KEY = [](const Triangle& t)
    {
        return std::make_tuple(t.a.x, t.a.y, t.a.z, t.b.x, t.b.y, t.b.z, t.c.x, t.c.y, t.c.z, t.kind);
    };

    std::sort(source.triangles.begin(), source.triangles.end(),
              [&](const Triangle& a, const Triangle& b)
              {
                  return KEY(a) < KEY(b);
              });

    std::map<TileCoord, std::vector<Triangle>> tiles;
    std::size_t references = 0;
    for (const auto& triangle : source.triangles)
    {
        if (!ValidTriangle(triangle))
        {
            throw std::invalid_argument("invalid/degenerate triangle or edge exceeds 1km; subdivide source geometry");
        }

        const auto& d = source.definition;
        // Source vertices may be on the upper boundary, but never outside the declared geometry extent.
        for (const auto P : {triangle.a, triangle.b, triangle.c})
        {
            if (!IsValid(P) || P.x < d.bounds.min.x || P.x > d.bounds.max.x || P.y < d.bounds.min.y ||
                P.y > d.bounds.max.y || P.z < d.bounds.min.z || P.z > d.bounds.max.z)
            {
                throw std::invalid_argument("source geometry outside map bounds");
            }

        }

        const std::int64_t ABX = static_cast<std::int64_t>(triangle.b.x) - triangle.a.x;
        const std::int64_t ABY = static_cast<std::int64_t>(triangle.b.y) - triangle.a.y;
        const std::int64_t ABZ = static_cast<std::int64_t>(triangle.b.z) - triangle.a.z;
        const std::int64_t ACX = static_cast<std::int64_t>(triangle.c.x) - triangle.a.x;
        const std::int64_t ACY = static_cast<std::int64_t>(triangle.c.y) - triangle.a.y;
        const std::int64_t ACZ = static_cast<std::int64_t>(triangle.c.z) - triangle.a.z;
        if (ABY * ACZ == ABZ * ACY && ABZ * ACX == ABX * ACZ && ABX * ACY == ABY * ACX)
        {
            throw std::invalid_argument("degenerate quantized triangle; fix source geometry");
        }

        const WorldPosition LOW{std::min({triangle.a.x, triangle.b.x, triangle.c.x}),
                                std::min({triangle.a.y, triangle.b.y, triangle.c.y}), 0};
        const WorldPosition HIGH{std::max({triangle.a.x, triangle.b.x, triangle.c.x}),
                                 std::max({triangle.a.y, triangle.b.y, triangle.c.y}), 0};
        const auto FIRST = WorldToTile(LOW, d), LAST = WorldToTile(HIGH, d);
        const auto COUNT =
            (static_cast<std::int64_t>(LAST.x) - FIRST.x + 1) * (static_cast<std::int64_t>(LAST.y) - FIRST.y + 1);
        if (COUNT > 4096)
        {
            throw std::invalid_argument("triangle spans too many tiles; subdivide source geometry");
        }

        for (auto x = FIRST.x; x <= LAST.x; ++x)
        {
            for (auto y = FIRST.y; y <= LAST.y; ++y)
            {
                const TileCoord COORDINATE{x, y};
                if (!GetTileBounds(COORDINATE, d))
                {
                    continue;
                }

                if (++references > 1'000'000)
                {
                    throw std::invalid_argument("bake reference budget exceeded");
                }

                tiles[COORDINATE].push_back(triangle);
                if (tiles.size() > MAX_MAP_TILES)
                {
                    throw std::invalid_argument("bake tile budget exceeded");
                }

            }

        }

    }

    MapPackage package{source.definition, {}, {}};
    for (const auto& entry : tiles)
    {
        TileData tile;
        tile.coordinate = entry.first;
        binary::U32(tile.collision, 1); // Triangle section version.
        binary::U32(tile.collision, static_cast<std::uint32_t>(entry.second.size()));
        for (const auto& triangle : entry.second)
        {
            binary::U32(tile.collision, static_cast<std::uint32_t>(triangle.kind));
            binary::Position(tile.collision, triangle.a);
            binary::Position(tile.collision, triangle.b);
            binary::Position(tile.collision, triangle.c);
        }

        std::vector<WorldBounds> volumes;
        const auto BOUNDS = *GetTileBounds(tile.coordinate, source.definition);
        for (const auto& volume : source.noWalkVolumes)
        {
            if (!volume.IsValid())
            {
                throw std::invalid_argument("invalid bake no-walk volume");
            }

            if (volume.min.x < BOUNDS.max.x && volume.max.x > BOUNDS.min.x && volume.min.y < BOUNDS.max.y &&
                volume.max.y > BOUNDS.min.y)
            {
                volumes.push_back(volume);
            }

        }

        std::sort(volumes.begin(), volumes.end(),
                  [](const WorldBounds& a, const WorldBounds& b)
                  {
                      return std::tie(a.min.x, a.min.y, a.min.z, a.max.x, a.max.y, a.max.z) <
                             std::tie(b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
                  });

        binary::U32(tile.collision, static_cast<std::uint32_t>(volumes.size()));
        for (const auto& volume : volumes)
        {
            binary::Position(tile.collision, volume.min);
            binary::Position(tile.collision, volume.max);
        }

        package.tiles.push_back(std::move(tile));
    }

    package.Validate();
    return package;
}

} // namespace shared_world

