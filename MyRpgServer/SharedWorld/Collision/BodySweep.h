#pragma once
#include "StaticCollision.h"
#include <cmath>

namespace shared_world
{
// Conservative axis-aligned player body. It encloses a vertical capsule;
// corner clearance is intentionally stricter than Chaos capsule collision.
struct BodyShape
{
    std::int32_t radius = 34;
    std::int32_t height = 180;

    bool IsValid() const noexcept
    {
        return this->radius > 0 && this->radius <= 500 && this->height > 0 && this->height <= 2000;
    }

};

namespace body_detail
{
struct V
{
    double x, y, z;

    V operator-(V b) const
    {
        return {this->x - b.x, this->y - b.y, this->z - b.z};
    }

};

inline double Dot(V a, V b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline V Cross(V a, V b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline V Vector(WorldPosition p)
{
    return {double(p.x), double(p.y), double(p.z)};
}

// Continuous SAT: projection intervals must overlap at the same time on all
// triangle/box separating axes. No endpoint-only or sparse-ray approximation.
inline std::optional<double> SweepTriangle(WorldPosition start, WorldPosition end, BodyShape shape, const Triangle& t)
{
    const V ORIGIN{double(start.x), double(start.y), start.z + shape.height * 0.5};
    const V DELTA_VALUE = Vector(end) - Vector(start),
            EXTENT{double(shape.radius), double(shape.radius), shape.height * 0.5};
    const V A = Vector(t.a) - ORIGIN, B = Vector(t.b) - ORIGIN, C = Vector(t.c) - ORIGIN;
    const V edges[3] = {B - A, C - B, A - C};
    const V bases[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double enter = 0, leave = 1;
    auto axis = [&](V n)
    {
        const double LENGTH = std::sqrt(Dot(n, n));
        if (LENGTH < 1e-12)
        {
            return true;
        }

        n = {n.x / LENGTH, n.y / LENGTH, n.z / LENGTH};
        const double RADIUS = std::abs(n.x) * EXTENT.x + std::abs(n.y) * EXTENT.y + std::abs(n.z) * EXTENT.z + 0.001;
        const double PA = Dot(A, n), PB = Dot(B, n), PC = Dot(C, n);
        double lo = std::min({PA, PB, PC}) - RADIUS, hi = std::max({PA, PB, PC}) + RADIUS;
        const double SPEED = Dot(DELTA_VALUE, n);
        if (std::abs(SPEED) < 1e-12)
        {
            return lo <= 0 && hi >= 0;
        }

        lo /= SPEED;
        hi /= SPEED;
        if (lo > hi)
        {
            std::swap(lo, hi);
        }

        enter = std::max(enter, lo);
        leave = std::min(leave, hi);
        return enter <= leave;
    };

    for (auto n : bases)
    {
        if (!axis(n))
        {
            return std::nullopt;
        }

    }

    if (!axis(Cross(edges[0], edges[1])))
    {
        return std::nullopt;
    }

    for (auto e : edges)
    {
        for (auto n : bases)
        {
            if (!axis(Cross(e, n)))
            {
                return std::nullopt;
            }

        }

    }

    return enter;
}

inline std::optional<double> SweepVolume(WorldPosition start, WorldPosition end, BodyShape shape, WorldBounds b)
{
    double enter = 0, leave = 1;
    auto axis = [&](double p, double q, double lo, double hi)
    {
        const double D = q - p;
        if (D == 0)
        {
            return p >= lo && p <= hi;
        }

        double a = (lo - p) / D, c = (hi - p) / D;
        if (a > c)
        {
            std::swap(a, c);
        }

        enter = std::max(enter, a);
        leave = std::min(leave, c);
        return enter <= leave;
    };

    if (!axis(start.x, end.x, double(b.min.x) - shape.radius, double(b.max.x) + shape.radius) ||
        !axis(start.y, end.y, double(b.min.y) - shape.radius, double(b.max.y) + shape.radius) ||
        !axis(start.z, end.z, double(b.min.z) - shape.height, b.max.z))
    {
        return std::nullopt;
    }

    return enter;
}

}

inline StaticHitResult SweepBody(const StaticWorld& world, WorldPosition start, WorldPosition end, BodyShape shape = {},
                                 std::size_t budget = 50'000, const StaticWorld::QueryCache* cache = nullptr)
{
    if (!shape.IsValid() || !IsValid(start) || !IsValid(end) || !WithinQueryExtent(Difference(end, start)))
    {
        return {QueryStatus::INVALID_INPUT};
    }

    WorldBounds area{
        {std::min(start.x, end.x) - shape.radius, std::min(start.y, end.y) - shape.radius, std::min(start.z, end.z)},
        {std::max(start.x, end.x) + shape.radius + 1, std::max(start.y, end.y) + shape.radius + 1,
         std::max(start.z, end.z) + shape.height + 1}};
    if (!world.Map().Definition().bounds.Contains(area.min) ||
        !world.Map().Definition().bounds.Contains({area.max.x - 1, area.max.y - 1, area.max.z - 1}))
    {
        return {QueryStatus::INVALID_INPUT};
    }

    double earliest = 2;
    auto hit = [&](std::optional<double> time)
    {
        if (time)
        {
            earliest = std::min(earliest, *time);
        }

    };

    const auto STATUS = world.VisitGeometry(
        area, budget,
        [&](const Triangle& t)
        {
            hit(body_detail::SweepTriangle(start, end, shape, t));
        },

        [&](WorldBounds b)
        {
            hit(body_detail::SweepVolume(start, end, shape, b));
        },

        cache);
    if (STATUS != QueryStatus::CLEAR)
    {
        return {STATUS};
    }

    if (earliest > 1)
    {
        return {QueryStatus::CLEAR, FRACTION_ONE, end, {}};
    }

    const auto FRACTION = static_cast<std::uint64_t>(std::max(0.0, earliest) * double(FRACTION_ONE));
    return {QueryStatus::HIT, FRACTION, PointAt(start, end, FRACTION), {}};
}

}

