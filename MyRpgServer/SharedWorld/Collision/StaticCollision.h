#pragma once
#include "../Map/MapLoader.h"
#include "Geometry.h"
#include "TriangleBoundsTree.h"
#include <cstdlib>
#include <array>

namespace shared_world
{
enum class QueryStatus
{
    CLEAR,
    HIT,
    NOT_FOUND,
    INVALID_INPUT,
    BUDGET_EXCEEDED
};

inline constexpr std::uint64_t FRACTION_ONE = std::uint64_t{1} << 32;

inline std::uint64_t FractionQ32(std::uint64_t numerator, std::uint64_t denominator) noexcept
{
    if (numerator >= denominator)
    {
        return FRACTION_ONE;
    }

    std::uint64_t result = 0;
    for (int i = 0; i < 32; ++i)
    {
        numerator *= 2; // Geometry products are bounded below 2^63 by MaxQueryExtent.
        result <<= 1;
        if (numerator >= denominator)
        {
            numerator -= denominator;
            ++result;
        }

    }

    return result;
}

inline WorldPosition PointAt(WorldPosition a, WorldPosition b, std::uint64_t fraction) noexcept
{
    const auto INTERPOLATE = [fraction](std::int32_t start, std::int32_t end)
    {
        const auto DELTA_VALUE = (static_cast<std::int64_t>(end) - start) * static_cast<std::int64_t>(fraction);
        return static_cast<std::int32_t>(start + DELTA_VALUE / static_cast<std::int64_t>(FRACTION_ONE));
    };

    return {INTERPOLATE(a.x, b.x), INTERPOLATE(a.y, b.y), INTERPOLATE(a.z, b.z)};
}

struct StaticHitResult
{
    QueryStatus status = QueryStatus::CLEAR;
    std::uint64_t fractionQ32 = FRACTION_ONE;
    WorldPosition position{};
    WideVector normal{}; // Exact unnormalized triangle normal; no floating point in core queries.
};

struct SurfaceResult
{
    QueryStatus status = QueryStatus::NOT_FOUND;
    WorldPosition position{};
};

class StaticWorld final
{
    struct CollisionTile
    {
        TileCoord coordinate;
        std::vector<Triangle> triangles;
        std::vector<WorldBounds> noWalk;
        TriangleBoundsTree boundsTree;
    };

public:
    // Request-local broad-phase candidates. Immutable world ownership stays with
    // the caller; this object never escapes the movement query that creates it.
    struct QueryCache
    {
        struct Tile
        {
            TileCoord coordinate;
            std::vector<const Triangle*> triangles;
        };

        WorldBounds area{};
        std::vector<Tile> tiles;

        bool Contains(WorldBounds query) const
        {
            return query.min.x >= this->area.min.x && query.min.y >= this->area.min.y &&
                   query.max.x <= this->area.max.x && query.max.y <= this->area.max.y;
        }

    };

    QueryCache CacheGeometry(WorldBounds area) const
    {
        QueryCache cache;
        cache.area = area;
        const auto FIRST = WorldToTile(area.min, this->map->Definition());
        const auto LAST = WorldToTile({area.max.x - 1, area.max.y - 1, area.max.z - 1}, this->map->Definition());
        if (std::int64_t(LAST.x - FIRST.x + 1) * (LAST.y - FIRST.y + 1) > 16)
        {
            return {};
        }

        for (int x = FIRST.x; x <= LAST.x; ++x)
        {
            for (int y = FIRST.y; y <= LAST.y; ++y)
            {
                const auto* tile = FindTile({x, y});
                if (!tile)
                {
                    continue;
                }

                QueryCache::Tile local{tile->coordinate, {}};
                tile->boundsTree.Visit({area.min.x, area.min.y, -COORDINATE_LIMIT},
                                       {area.max.x, area.max.y, COORDINATE_LIMIT},
                                       [&](std::uint32_t index)
                                       {
                                           const auto& t = tile->triangles[index];
                                           if (Overlaps(t, {area.min.x, area.min.y, -COORDINATE_LIMIT},
                                                        {area.max.x, area.max.y, COORDINATE_LIMIT}))
                                           {
                                               local.triangles.push_back(&t);
                                           }

                                           return local.triangles.size() <= 4096;
                                       });

                if (local.triangles.size() > 4096)
                {
                    return {};
                }

                cache.tiles.push_back(std::move(local));
            }

        }

        return cache;
    }

    explicit StaticWorld(std::shared_ptr<const LoadedMap> newMap) : map(std::move(newMap))
    {
        if (!this->map)
        {
            throw std::invalid_argument("missing map snapshot");
        }

        this->tiles.reserve(this->map->Tiles().size());
        for (const auto& tile : this->map->Tiles())
        {
            if (tile.collision.empty())
            {
                continue; // Absent collision is NOT a walkable floor.
            }

            binary::Reader reader(tile.collision, tile.collision.size());
            if (reader.U32() != 1)
            {
                throw std::invalid_argument("unsupported collision section");
            }

            const auto COUNT = reader.U32();
            if (COUNT > 500'000 || COUNT > tile.collision.size() / 40)
            {
                throw std::invalid_argument("collision triangle budget exceeded");
            }

            CollisionTile data{tile.coordinate, {}, {}, {}};
            data.triangles.reserve(COUNT);
            for (std::uint32_t i = 0; i < COUNT; ++i)
            {
                Triangle t;
                t.kind = static_cast<SurfaceKind>(reader.U32());
                t.a = reader.Position();
                t.b = reader.Position();
                t.c = reader.Position();
                if (!ValidTriangle(t))
                {
                    throw std::invalid_argument("invalid collision triangle");
                }

                data.triangles.push_back(t);
            }

            const auto VOLUMES = reader.U32();
            if (VOLUMES > 4096)
            {
                throw std::invalid_argument("no-walk volume budget exceeded");
            }

            for (std::uint32_t i = 0; i < VOLUMES; ++i)
            {
                WorldBounds bounds;
                bounds.min = reader.Position();
                bounds.max = reader.Position();
                if (!bounds.IsValid())
                {
                    throw std::invalid_argument("invalid no-walk volume");
                }

                data.noWalk.push_back(bounds);
            }

            if (!reader.Done())
            {
                throw std::invalid_argument("trailing collision section");
            }

            data.boundsTree.Build(data.triangles);
            this->tiles.push_back(std::move(data));
        }

        this->residentBytes =
            sizeof(*this) + this->map->ResidentBytes() + this->tiles.capacity() * sizeof(CollisionTile);
        for (const auto& tile : this->tiles)
        {
            this->residentBytes += tile.triangles.capacity() * sizeof(Triangle) +
                                   tile.noWalk.capacity() * sizeof(WorldBounds) + tile.boundsTree.ResidentBytes();
        }

    }

    std::size_t ResidentBytes() const noexcept
    {
        return this->residentBytes;
    }

    const LoadedMap& Map() const noexcept
    {
        return *this->map;
    }

    // Bounded geometry access for body sweeps/debug rendering. Callers must not
    // turn BudgetExceeded into Clear. A triangle may occur in several tiles.
    template <class TriangleVisitor, class VolumeVisitor>
    QueryStatus VisitGeometry(WorldBounds newArea, std::size_t budget, TriangleVisitor triangleVisitor,
                              VolumeVisitor volumeVisitor, const QueryCache* cache = nullptr) const
    {
        if (!newArea.IsValid())
        {
            return QueryStatus::INVALID_INPUT;
        }

        const auto FIRST = WorldToTile(newArea.min, this->map->Definition());
        const auto LAST =
            WorldToTile({newArea.max.x - 1, newArea.max.y - 1, newArea.max.z - 1}, this->map->Definition());
        const auto COUNT =
            (static_cast<std::int64_t>(LAST.x) - FIRST.x + 1) * (static_cast<std::int64_t>(LAST.y) - FIRST.y + 1);
        if (COUNT > 4096)
        {
            return QueryStatus::BUDGET_EXCEEDED;
        }

        for (auto x = FIRST.x; x <= LAST.x; ++x)
        {
            for (auto y = FIRST.y; y <= LAST.y; ++y)
            {
                const auto* tile = FindTile({x, y});
                if (!tile)
                {
                    continue;
                }

                if (tile->triangles.size() > budget)
                {
                    return QueryStatus::BUDGET_EXCEEDED;
                }

                budget -= tile->triangles.size(); // Retain the existing logical budget.
                const QueryCache::Tile* cached = nullptr;
                if (cache && cache->Contains(newArea))
                {
                    for (const auto& entry : cache->tiles)
                    {
                        if (entry.coordinate == tile->coordinate)
                        {
                            cached = &entry;
                            break;
                        }

                    }

                }

                if (cached)
                {
                    for (const auto* t : cached->triangles)
                    {
                        if (Overlaps(*t, newArea.min, newArea.max))
                        {
                            triangleVisitor(*t);
                        }

                    }

                }
                else
                {
                    tile->boundsTree.Visit(newArea.min, newArea.max,
                                           [&](std::uint32_t index)
                                           {
                                               const auto& t = tile->triangles[index];
                                               if (Overlaps(t, newArea.min, newArea.max))
                                               {
                                                   triangleVisitor(t);
                                               }

                                               return true;
                                           });

                }

                for (const auto& v : tile->noWalk)
                {
                    if (!budget--)
                    {
                        return QueryStatus::BUDGET_EXCEEDED;
                    }

                    volumeVisitor(v);
                }

            }

        }

        return QueryStatus::CLEAR;
    }

    // Vertical projection at the SAME XY. No lateral snapping or accidental floor switching.
    // Default slope bound is L1 normal ratio <= 1 (conservative 45 degrees).
    SurfaceResult ProjectToWalkableSurface(WorldPosition input, std::int32_t maxVerticalDelta,
                                           std::size_t triangleBudget = 4096, bool preferHighest = false,
                                           std::int32_t maxRise = MAX_QUERY_EXTENT,
                                           const QueryCache* cache = nullptr) const
    {
        if (!this->map->Definition().bounds.Contains(input) || maxVerticalDelta < 0 ||
            maxVerticalDelta > MAX_QUERY_EXTENT)
        {
            return {QueryStatus::INVALID_INPUT, input};
        }

        const auto* tile = FindTile(WorldToTile(input, this->map->Definition()));
        if (!tile)
        {
            return {};
        }

        if (tile->triangles.size() > triangleBudget)
        {
            return {QueryStatus::BUDGET_EXCEEDED, input};
        }

        SurfaceResult result;
        std::int64_t bestDistance = static_cast<std::int64_t>(maxVerticalDelta) + 1;
        std::size_t tested = 0;
        auto project = [&](const Triangle& t)
        {
            if (++tested > triangleBudget)
            {
                return false;
            }

            if (t.kind != SurfaceKind::SOLID || !ContainsXY(t, input))
            {
                return true;
            }

            const auto N = Cross(Difference(t.b, t.a), Difference(t.c, t.a));
            if (N.z <= 0 || std::abs(N.x) + std::abs(N.y) > N.z)
            {
                return true;
            }

            const std::int64_t DX = static_cast<std::int64_t>(input.x) - t.a.x;
            const std::int64_t DY = static_cast<std::int64_t>(input.y) - t.a.y;
            const auto NUMERATOR = -(N.x * DX + N.y * DY);
            const auto OFFSET = NUMERATOR / N.z; // <1cm height truncation relative to triangle origin.
            const auto HEIGHT = static_cast<std::int64_t>(t.a.z) + OFFSET;
            const auto DISTANCE = std::abs(HEIGHT - input.z);
            if (HEIGHT - input.z > maxRise || DISTANCE > maxVerticalDelta ||
                (!preferHighest && DISTANCE > bestDistance))
            {
                return true;
            }

            WorldPosition candidate{input.x, input.y, static_cast<std::int32_t>(HEIGHT)};
            if (!this->map->Definition().bounds.Contains(candidate))
            {
                return true;
            }

            bool blocked = false;
            for (const auto& volume : tile->noWalk)
            {
                if (++tested > triangleBudget)
                {
                    return false;
                }

                if (volume.Contains(candidate))
                {
                    blocked = true;
                    break;
                }

            }

            if (blocked)
            {
                return true;
            }

            // Stable tie: lower floor first, independent of source triangle enumeration.
            if (result.status == QueryStatus::HIT &&
                ((preferHighest && candidate.z <= result.position.z) ||
                 (!preferHighest && DISTANCE == bestDistance && candidate.z >= result.position.z)))
            {
                return true;
            }

            bestDistance = DISTANCE;
            result = {QueryStatus::HIT, candidate};
            return true;
        };

        if (tile->noWalk.empty())
        {
            const QueryCache::Tile* cached = nullptr;
            if (cache && cache->Contains({input, {input.x + 1, input.y + 1, input.z + 1}}))
            {
                for (const auto& entry : cache->tiles)
                {
                    if (entry.coordinate == tile->coordinate)
                    {
                        cached = &entry;
                        break;
                    }

                }

            }

            if (cached)
            {
                for (const auto* t : cached->triangles)
                {
                    if (!project(*t))
                    {
                        return {QueryStatus::BUDGET_EXCEEDED, input};
                    }

                }

            }
            else if (!tile->boundsTree.Visit({input.x, input.y, -COORDINATE_LIMIT},
                                             {input.x, input.y, COORDINATE_LIMIT},
                                             [&](std::uint32_t index)
                                             {
                                                 return project(tile->triangles[index]);
                                             }))

            {
                return {QueryStatus::BUDGET_EXCEEDED, input};
            }

        }
        else
        {
            // Preserve source-order volume tests and their existing budget semantics.
            for (std::uint32_t i = 0; i < tile->triangles.size(); ++i)
            {
                if (!project(tile->triangles[i]))
                {
                    return {QueryStatus::BUDGET_EXCEEDED, input};
                }

            }

        }

        return result;
    }

    // Nine support samples share one candidate traversal and one plane normal
    // per triangle. Mixed tiles/NoWalk retain the original projection path and
    // its exact budget/error ordering.
    std::array<SurfaceResult, 9> ProjectSupportGrid(WorldPosition point, int radius, int delta, int maxRise,
                                                    const QueryCache* cache = nullptr) const
    {
        std::array<SurfaceResult, 9> results;
        std::array<WorldPosition, 9> samples;
        unsigned sampleIndex = 0;
        for (int x : {-radius, 0, radius})
        {
            for (int y : {-radius, 0, radius})
            {
                samples[sampleIndex++] = {point.x + x, point.y + y, point.z};
            }

        }

        const auto* tile = FindTile(WorldToTile(samples.front(), this->map->Definition()));
        bool fast = tile && tile->noWalk.empty() && tile->triangles.size() <= 4096;
        for (auto sample : samples)
        {
            fast = fast && this->map->Definition().bounds.Contains(sample) &&
                   WorldToTile(sample, this->map->Definition()) == tile->coordinate;
        }

        if (!fast)
        {
            for (unsigned i = 0; i < samples.size(); ++i)
            {
                results[i] = ProjectToWalkableSurface(samples[i], delta, 4096, true, maxRise, cache);
            }

            return results;
        }

        auto project = [&](const Triangle& t)
        {
            if (t.kind != SurfaceKind::SOLID)
            {
                return;
            }

            const auto N = Cross(Difference(t.b, t.a), Difference(t.c, t.a));
            if (N.z <= 0 || std::abs(N.x) + std::abs(N.y) > N.z)
            {
                return;
            }

            for (unsigned i = 0; i < samples.size(); ++i)
            {
                const auto SAMPLE = samples[i];
                if (!ContainsXY(t, SAMPLE))
                {
                    continue;
                }

                const auto DX = std::int64_t(SAMPLE.x) - t.a.x, DY = std::int64_t(SAMPLE.y) - t.a.y;
                const auto HEIGHT = std::int64_t(t.a.z) - (N.x * DX + N.y * DY) / N.z;
                if (HEIGHT - SAMPLE.z > maxRise || std::abs(HEIGHT - SAMPLE.z) > delta)
                {
                    continue;
                }

                const WorldPosition CANDIDATE{SAMPLE.x, SAMPLE.y, static_cast<int>(HEIGHT)};
                if (!this->map->Definition().bounds.Contains(CANDIDATE))
                {
                    continue;
                }

                if (results[i].status != QueryStatus::HIT || CANDIDATE.z > results[i].position.z)
                {
                    results[i] = {QueryStatus::HIT, CANDIDATE};
                }

            }

        };

        const WorldBounds AREA{{point.x - radius, point.y - radius, point.z},
                               {point.x + radius + 1, point.y + radius + 1, point.z + 1}};
        const QueryCache::Tile* cached = nullptr;
        if (cache && cache->Contains(AREA))
        {
            for (const auto& entry : cache->tiles)
            {
                if (entry.coordinate == tile->coordinate)
                {
                    cached = &entry;
                    break;
                }

            }

        }

        if (cached)
        {
            for (const auto* t : cached->triangles)
            {
                project(*t);
            }

        }
        else
        {
            tile->boundsTree.Visit({AREA.min.x, AREA.min.y, -COORDINATE_LIMIT},
                                   {AREA.max.x, AREA.max.y, COORDINATE_LIMIT},
                                   [&](std::uint32_t index)
                                   {
                                       project(tile->triangles[index]);
                                       return true;
                                   });

        }

        return results;
    }

    bool IsWalkable(WorldPosition input, std::int32_t heightTolerance = 1) const
    {
        return ProjectToWalkableSurface(input, heightTolerance).status == QueryStatus::HIT;
    }

    StaticHitResult RaycastWorld(WorldPosition start, WorldPosition end, std::size_t triangleBudget = 50'000) const
    {
        if (!this->map->Definition().bounds.Contains(start) || !this->map->Definition().bounds.Contains(end) ||
            !WithinQueryExtent(Difference(end, start)))
        {
            return {QueryStatus::INVALID_INPUT};
        }

        if (start == end)
        {
            return {QueryStatus::INVALID_INPUT};
        }

        const WorldPosition LOW{std::min(start.x, end.x), std::min(start.y, end.y), std::min(start.z, end.z)};
        const WorldPosition HIGH{std::max(start.x, end.x), std::max(start.y, end.y), std::max(start.z, end.z)};
        const auto FIRST = WorldToTile(LOW, this->map->Definition()), LAST = WorldToTile(HIGH, this->map->Definition());
        const auto TILE_COUNT =
            (static_cast<std::int64_t>(LAST.x) - FIRST.x + 1) * (static_cast<std::int64_t>(LAST.y) - FIRST.y + 1);
        if (TILE_COUNT > 4096)
        {
            return {QueryStatus::BUDGET_EXCEEDED};
        }

        StaticHitResult result;
        result.position = end;
        std::size_t tested = 0;
        for (auto x = FIRST.x; x <= LAST.x; ++x)
        {
            for (auto y = FIRST.y; y <= LAST.y; ++y)
            {
                const auto* tile = FindTile({x, y});
                if (!tile)
                {
                    continue;
                }

                for (const auto& volume : tile->noWalk)
                {
                    if (++tested > triangleBudget)
                    {
                        return {QueryStatus::BUDGET_EXCEEDED};
                    }

                    const auto FRACTION = RayBox(start, end, volume);
                    if (FRACTION && (result.status != QueryStatus::HIT || *FRACTION < result.fractionQ32))
                    {
                        result = {QueryStatus::HIT, *FRACTION, PointAt(start, end, *FRACTION), {}};
                    }

                }

                for (const auto& triangle : tile->triangles)
                {
                    if (++tested > triangleBudget)
                    {
                        return {QueryStatus::BUDGET_EXCEEDED};
                    }

                    if (!Overlaps(triangle, LOW, HIGH))
                    {
                        continue;
                    }

                    const auto FRACTION = RayTriangle(start, end, triangle);
                    if (FRACTION && (result.status != QueryStatus::HIT || *FRACTION < result.fractionQ32))
                    {
                        result = {QueryStatus::HIT, *FRACTION, PointAt(start, end, *FRACTION),
                                  Cross(Difference(triangle.b, triangle.a), Difference(triangle.c, triangle.a))};
                    }

                }

            }

        }

        return result;
    }

private:
    const CollisionTile* FindTile(TileCoord coordinate) const noexcept
    {
        const auto FOUND = std::lower_bound(this->tiles.begin(), this->tiles.end(), coordinate,
                                            [](const CollisionTile& t, TileCoord c)
                                            {
                                                return t.coordinate < c;
                                            });

        return FOUND != this->tiles.end() && FOUND->coordinate == coordinate ? &*FOUND : nullptr;
    }

    static bool ContainsXY(const Triangle& t, WorldPosition point) noexcept
    {
        if (point.x < std::min({t.a.x, t.b.x, t.c.x}) || point.x > std::max({t.a.x, t.b.x, t.c.x}) ||
            point.y < std::min({t.a.y, t.b.y, t.c.y}) || point.y > std::max({t.a.y, t.b.y, t.c.y}))
        {
            return false;
        }

        const auto SIDE = [point](WorldPosition a, WorldPosition b)
        {
            return (static_cast<std::int64_t>(b.x) - a.x) * (static_cast<std::int64_t>(point.y) - a.y) -
                   (static_cast<std::int64_t>(b.y) - a.y) * (static_cast<std::int64_t>(point.x) - a.x);
        };

        const auto A = SIDE(t.a, t.b), B = SIDE(t.b, t.c), C = SIDE(t.c, t.a);
        return (A >= 0 && B >= 0 && C >= 0) || (A <= 0 && B <= 0 && C <= 0);
    }

    static bool Overlaps(const Triangle& t, WorldPosition low, WorldPosition high) noexcept
    {
        return std::max({t.a.x, t.b.x, t.c.x}) >= low.x && std::min({t.a.x, t.b.x, t.c.x}) <= high.x &&
               std::max({t.a.y, t.b.y, t.c.y}) >= low.y && std::min({t.a.y, t.b.y, t.c.y}) <= high.y &&
               std::max({t.a.z, t.b.z, t.c.z}) >= low.z && std::min({t.a.z, t.b.z, t.c.z}) <= high.z;
    }

    static std::optional<std::uint64_t> RayTriangle(WorldPosition start, WorldPosition end, const Triangle& t) noexcept
    {
        const auto DIRECTION = Difference(end, start), AB = Difference(t.b, t.a), AC = Difference(t.c, t.a);
        const auto P = Cross(DIRECTION, AC), RELATIVE_VALUE = Difference(start, t.a);
        auto determinant = Dot(AB, P);
        if (determinant == 0)
        {
            // Coplanar overlap is conservatively blocked, not reported as definitely clear.
            if (Dot(RELATIVE_VALUE, Cross(AB, AC)) == 0)
            {
                return 0;
            }

            return std::nullopt;
        }

        const auto Q = Cross(RELATIVE_VALUE, AB);
        auto u = Dot(RELATIVE_VALUE, P), v = Dot(DIRECTION, Q), time = Dot(AC, Q);
        if (determinant < 0)
        {
            determinant = -determinant;
            u = -u;
            v = -v;
            time = -time;
        }

        if (u < 0 || v < 0 || u > determinant || v > determinant - u || time < 0 || time > determinant)
        {
            return std::nullopt;
        }

        return FractionQ32(static_cast<std::uint64_t>(time), static_cast<std::uint64_t>(determinant));
    }

    static std::optional<std::uint64_t> RayBox(WorldPosition a, WorldPosition b, const WorldBounds& bounds) noexcept
    {
        std::uint64_t enter = 0, leave = FRACTION_ONE;
        const auto SLAB = [&](std::int32_t start, std::int32_t end, std::int32_t low, std::int32_t high)
        {
            std::int64_t delta = static_cast<std::int64_t>(end) - start;
            if (!delta)
            {
                return start >= low && start < high;
            }

            std::int64_t nearValue = static_cast<std::int64_t>(low) - start;
            std::int64_t farValue = static_cast<std::int64_t>(high) - start;
            if (delta < 0)
            {
                delta = -delta;
                const auto OLD = nearValue;
                nearValue = -farValue;
                farValue = -OLD;
            }

            if (farValue < 0 || nearValue > delta)
            {
                return false;
            }

            const auto NEAR_FRACTION = nearValue <= 0 ? 0 : FractionQ32(nearValue, delta);
            const auto FAR_FRACTION = farValue >= delta ? FRACTION_ONE : FractionQ32(farValue, delta);
            enter = std::max(enter, NEAR_FRACTION);
            leave = std::min(leave, FAR_FRACTION);
            return enter <= leave;
        };

        if (!SLAB(a.x, b.x, bounds.min.x, bounds.max.x) || !SLAB(a.y, b.y, bounds.min.y, bounds.max.y) ||
            !SLAB(a.z, b.z, bounds.min.z, bounds.max.z))
        {
            return std::nullopt;
        }

        return enter;
    }

    std::shared_ptr<const LoadedMap> map;
    std::vector<CollisionTile> tiles;
    std::size_t residentBytes = 0;
};

} // namespace shared_world

