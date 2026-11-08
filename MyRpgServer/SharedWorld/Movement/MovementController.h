#pragma once
#include "../Collision/BodySweep.h"
#include <array>

namespace shared_world
{
struct MovementSettings
{
    BodyShape body{};
    std::int32_t stepHeight = 25;
    std::int32_t maxMovePerTick = 20; // 600 cm/s at 30Hz; server owns the tick budget.

    bool IsValid() const
    {
        return this->body.IsValid() && this->stepHeight >= 0 && this->stepHeight < this->body.height &&
               this->maxMovePerTick > 0 && this->maxMovePerTick <= 100;
    }

};

struct MovementResult
{
    QueryStatus status = QueryStatus::INVALID_INPUT;
    WorldPosition position{}; // Canonical feet, not the visual capsule center.
};

// Preserve the nine-point ledge policy, then include narrow bumps between
// samples. Clip walkable triangles to the entire conservative body footprint.
inline SurfaceResult GroundSupport(const StaticWorld& world, WorldPosition point, MovementSettings settings = {},
                                   const StaticWorld::QueryCache* cache = nullptr)
{
    if (!settings.IsValid() || !IsValid(point))
    {
        return {QueryStatus::INVALID_INPUT, point};
    }

    int highest = -COORDINATE_LIMIT, lowest = COORDINATE_LIMIT;
    const auto SUPPORTS = world.ProjectSupportGrid(
        point, settings.body.radius, settings.stepHeight + settings.body.radius, settings.stepHeight, cache);
    for (const auto& support : SUPPORTS)
    {
        if (support.status != QueryStatus::HIT)
        {
            return {support.status, point};
        }

        highest = std::max(highest, support.position.z);
        lowest = std::min(lowest, support.position.z);
    }

    struct Vertex
    {
        double x, y, z;
    };

    const int RADIUS = settings.body.radius;
    const WorldBounds AREA{{point.x - RADIUS, point.y - RADIUS, point.z - settings.stepHeight - RADIUS},
                           {point.x + RADIUS + 1, point.y + RADIUS + 1, point.z + settings.stepHeight + 1}};
    const auto STATUS = world.VisitGeometry(
        AREA, 50000,
        [&](const Triangle& t)
        {
            const auto N = Cross(Difference(t.b, t.a), Difference(t.c, t.a));
            if (t.kind != SurfaceKind::SOLID || N.z <= 0 || std::abs(N.x) + std::abs(N.y) > N.z)
            {
                return;
            }

            Vertex a[12], b[12];
            int count = 3;
            a[0] = {double(t.a.x), double(t.a.y), double(t.a.z)};
            a[1] = {double(t.b.x), double(t.b.y), double(t.b.z)};
            a[2] = {double(t.c.x), double(t.c.y), double(t.c.z)};
            for (int plane = 0; plane < 4 && count; ++plane)
            {
                auto distance = [&](Vertex v)
                {
                    switch (plane)
                    {
                    case 0:
                        return v.x - (point.x - RADIUS);
                    case 1:
                        return (point.x + RADIUS) - v.x;
                    case 2:
                        return v.y - (point.y - RADIUS);
                    default:
                        return (point.y + RADIUS) - v.y;
                    }

                };

                int size = 0;
                for (int i = 0; i < count; ++i)
                {
                    const auto FROM = a[i], TO = a[(i + 1) % count];
                    const double D_0 = distance(FROM), D_1 = distance(TO);
                    if (D_0 >= 0)
                    {
                        b[size++] = FROM;
                    }

                    if ((D_0 >= 0) != (D_1 >= 0))
                    {
                        const double F = D_0 / (D_0 - D_1);
                        b[size++] = {FROM.x + (TO.x - FROM.x) * F, FROM.y + (TO.y - FROM.y) * F,
                                     FROM.z + (TO.z - FROM.z) * F};
                    }

                }

                count = size;
                for (int i = 0; i < count; ++i)
                {
                    a[i] = b[i];
                }

            }

            for (int i = 0; i < count; ++i)
            {
                const int Z = static_cast<int>(std::ceil(a[i].z - 1e-7));
                if (Z >= point.z - settings.stepHeight - RADIUS && Z <= point.z + settings.stepHeight)
                {
                    highest = std::max(highest, Z);
                }

            }

        },

        [](WorldBounds)
        {
        },

        cache);
    if (STATUS != QueryStatus::CLEAR)
    {
        return {STATUS, point};
    }

    // The returned feet must retain all nine supports on the next query.
    // Raising to a narrow high surface can otherwise leave the centre/low
    // samples beyond the projection range and permanently strand the walker.
    if (highest - lowest > settings.stepHeight + settings.body.radius)
    {
        return {QueryStatus::NOT_FOUND, point};
    }

    return {QueryStatus::HIT, {point.x, point.y, highest}};
}

// Walking-only kinematic controller. Both server and client call this function.
// A square support footprint and box upper body intentionally conservatively
// approximate a capsule. No falling, jumping or dynamic collision in this stage.
inline MovementResult MoveOnGround(const StaticWorld& world, WorldPosition start, std::int32_t dx, std::int32_t dy,
                                   MovementSettings settings = {})
{
    MovementResult result{QueryStatus::INVALID_INPUT, start};
    if (!settings.IsValid() || !IsValid(start) || std::abs(static_cast<std::int64_t>(dx)) > settings.maxMovePerTick ||
        std::abs(static_cast<std::int64_t>(dy)) > settings.maxMovePerTick ||
        static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dy) * dy >
            static_cast<std::int64_t>(settings.maxMovePerTick) * settings.maxMovePerTick)
    {
        return result;
    }

    if (world.ProjectToWalkableSurface(start, settings.stepHeight + settings.body.radius).status != QueryStatus::HIT)
    {
        return {QueryStatus::NOT_FOUND, start};
    }

    const int COUNT = std::max(std::abs(dx), std::abs(dy));
    if (!COUNT)
    {
        return {QueryStatus::CLEAR, start};
    }

    const int RADIUS = settings.body.radius;
    const auto CACHE = world.CacheGeometry(
        {{std::min(start.x, start.x + dx) - RADIUS, std::min(start.y, start.y + dy) - RADIUS, start.z},
         {std::max(start.x, start.x + dx) + RADIUS + 1, std::max(start.y, start.y + dy) + RADIUS + 1, start.z + 1}});
    auto current = start;
    std::array<WorldPosition, 101> flat;
    unsigned flatCount = 1;
    flat[0] = start;
    auto flushFlat = [&]() -> MovementResult
    {
        if (flatCount < 2)
        {
            return {QueryStatus::CLEAR, current};
        }

        auto shape = settings.body;
        // The integer staircase lies within 2cm of its endpoint chord. A
        // slightly wider rejection test proves the entire actual path clear.
        // Axis/45-degree inputs already lie exactly on the continuous chord.
        if (dx && dy && std::abs(dx) != std::abs(dy))
        {
            shape.radius += 2;
        }

        const auto FIRST = flat[0], LAST = flat[flatCount - 1];
        const auto MERGED =
            SweepBody(world, {FIRST.x, FIRST.y, FIRST.z + 1}, {LAST.x, LAST.y, LAST.z + 1}, shape, 50000, &CACHE);
        if (MERGED.status != QueryStatus::CLEAR)
        {
            // A conservative broad test may reject a valid route or cross a
            // query budget boundary. Replay the original steps for exact status
            // and last accepted centimetre, never infer a partial position.
            for (unsigned i = 1; i < flatCount; ++i)
            {
                const auto FROM = flat[i - 1], TO = flat[i];
                const auto HIT = SweepBody(world, {FROM.x, FROM.y, FROM.z + 1}, {TO.x, TO.y, TO.z + 1}, settings.body,
                                           50000, &CACHE);
                if (HIT.status != QueryStatus::CLEAR)
                {
                    return {HIT.status, FROM};
                }

            }

        }

        flat[0] = LAST;
        flatCount = 1;
        return {QueryStatus::CLEAR, LAST};
    };

    // One-centimeter support progression matches the canonical coordinate grid.
    // Each upper body segment is swept continuously, including very thin walls.
    for (int i = 1; i <= COUNT; ++i)
    {
        WorldPosition next{start.x + dx * i / COUNT, start.y + dy * i / COUNT, current.z};
        // Keep the entire support footprint on a reachable floor; do not bridge
        // gaps or switch to an unrelated floor at an overlapping XY position.
        const auto SUPPORT = GroundSupport(world, next, settings, &CACHE);
        if (SUPPORT.status != QueryStatus::HIT)
        {
            const auto PENDING = flushFlat();
            if (PENDING.status != QueryStatus::CLEAR)
            {
                return PENDING;
            }

            return {SUPPORT.status, current};
        }

        next.z = SUPPORT.position.z;
        if (std::abs(next.z - current.z) > settings.stepHeight)
        {
            const auto PENDING = flushFlat();
            if (PENDING.status != QueryStatus::CLEAR)
            {
                return PENDING;
            }

            return {QueryStatus::HIT, current};
        }

        if (next.z == current.z)
        {
            flat[flatCount++] = next;
            current = next;
            continue;
        }

        const auto PENDING = flushFlat();
        if (PENDING.status != QueryStatus::CLEAR)
        {
            return PENDING;
        }

        const int RAISED = std::max(current.z, next.z) + 1;
        // Step up, move across, then settle: all three segments sweep the full
        // body. The 1cm skin avoids treating support contact as penetration.
        // The across sweep includes both endpoints. Zero-length raise/settle
        // checks on the same plane duplicate exactly those endpoint tests.
        if (current.z < next.z)
        {
            const auto HIT = SweepBody(world, {current.x, current.y, current.z + 1}, {current.x, current.y, RAISED},
                                       settings.body, 50000, &CACHE);
            if (HIT.status != QueryStatus::CLEAR)
            {
                return {HIT.status, current};
            }

        }

        auto hit =
            SweepBody(world, {current.x, current.y, RAISED}, {next.x, next.y, RAISED}, settings.body, 50000, &CACHE);
        if (hit.status != QueryStatus::CLEAR)
        {
            return {hit.status, current};
        }

        if (next.z < current.z)
        {
            hit =
                SweepBody(world, {next.x, next.y, RAISED}, {next.x, next.y, next.z + 1}, settings.body, 50000, &CACHE);
            if (hit.status != QueryStatus::CLEAR)
            {
                return {hit.status, current};
            }

        }

        current = next;
        flat[0] = current;
    }

    return flushFlat();
}

}

