#pragma once
#include "../Core/MapDefinition.h"
#include <algorithm>
#include <cmath>

namespace shared_world::combat
{
inline constexpr unsigned PLAYER_RADIUS = 34, DEFAULT_NPC_RADIUS = 40;

// Legacy melee range measured between pivots. Remove the old 34+40 footprint
// before adding the actual bodies, preserving ordinary player/goblin reach.
inline unsigned MeleeCenterRange(unsigned legacy, unsigned sourceRadius, unsigned targetRadius)
{
    return (legacy > PLAYER_RADIUS + DEFAULT_NPC_RADIUS ? legacy - PLAYER_RADIUS - DEFAULT_NPC_RADIUS : 0) +
           sourceRadius + targetRadius;
}

inline bool BodyMoveClear(WorldPosition from, WorldPosition to, WorldPosition body, unsigned radius, unsigned height)
{
    if (!radius || std::min(from.z, to.z) >= std::int64_t(body.z) + height || std::max(from.z, to.z) + 180 <= body.z)
    {
        return true;
    }

    const double X_VALUE = double(from.x) - body.x, Y = double(from.y) - body.y;
    const double DX = double(to.x) - from.x, DY = double(to.y) - from.y;
    const double BEFORE = X_VALUE * X_VALUE + Y * Y, R_2 = double(radius) * radius;
    // A spawn/correction inside a moving body may escape, but never tunnel
    // through its center. Ordinary movement is tested continuously.
    if (BEFORE < R_2)
    {
        return X_VALUE * DX + Y * DY >= 0 && (X_VALUE + DX) * (X_VALUE + DX) + (Y + DY) * (Y + DY) > BEFORE;
    }

    const double T = std::clamp(-(X_VALUE * DX + Y * DY) / std::max(1., DX * DX + DY * DY), 0., 1.);
    return (X_VALUE + DX * T) * (X_VALUE + DX * T) + (Y + DY * T) * (Y + DY * T) >= R_2;
}

// Earliest intersection of a segment with a sphere-expanded upright cylinder.
// A fixed body height avoids skeletal pose/bounds changing damage authority.
inline double SweepBodyTarget(WorldPosition from, WorldPosition to, WorldPosition feet, unsigned bodyRadius,
                              unsigned height, unsigned projectileRadius)
{
    const double X_VALUE = double(from.x) - feet.x, Y = double(from.y) - feet.y;
    const double DX = double(to.x) - from.x, DY = double(to.y) - from.y, DZ = double(to.z) - from.z;
    const double R = double(bodyRadius) + projectileRadius, A = DX * DX + DY * DY, B = X_VALUE * DX + Y * DY,
                 C = X_VALUE * X_VALUE + Y * Y - R * R;
    double lo = 0, hi = 1;
    if (A < 1e-9)
    {
        if (C > 0)
        {
            return 2;
        }

    }
    else
    {
        const double DISCRIMINANT = B * B - A * C;
        if (DISCRIMINANT < 0)
        {
            return 2;
        }

        const double ROOT = std::sqrt(DISCRIMINANT);
        lo = std::max(lo, (-B - ROOT) / A);
        hi = std::min(hi, (-B + ROOT) / A);
    }

    const double BOTTOM = double(feet.z) - projectileRadius, TOP = double(feet.z) + height + projectileRadius;
    if (std::abs(DZ) < 1e-9)
    {
        if (from.z < BOTTOM || from.z > TOP)
        {
            return 2;
        }

    }
    else
    {
        double t0 = (BOTTOM - from.z) / DZ, t1 = (TOP - from.z) / DZ;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }

        lo = std::max(lo, t0);
        hi = std::min(hi, t1);
    }

    return lo <= hi ? lo : 2;
}

// Exact circle/sector intersection in XY, including both finite radial edges.
inline bool BodyInSector(WorldPosition origin, WorldPosition feet, int yaw, unsigned reach, unsigned angle,
                         unsigned bodyRadius)
{
    if (std::abs(std::int64_t(origin.z) - feet.z) > 150)
    {
        return false;
    }

    const double X_VALUE = double(feet.x) - origin.x, Y = double(feet.y) - origin.y, LENGTH = std::hypot(X_VALUE, Y);
    if (LENGTH <= bodyRadius)
    {
        return true;
    }

    if (LENGTH > double(reach) + bodyRadius)
    {
        return false;
    }

    if (angle >= 360)
    {
        return true;
    }

    constexpr double PI_VALUE = 3.141592653589793;
    const double FACING = double(yaw) * PI_VALUE / 180000, HALF = double(angle) * PI_VALUE / 360;
    const double RELATIVE_VALUE = std::remainder(std::atan2(Y, X_VALUE) - FACING, 2 * PI_VALUE);
    if (std::abs(RELATIVE_VALUE) <= HALF)
    {
        return true;
    }

    const double EDGE = FACING + (RELATIVE_VALUE > 0 ? HALF : -HALF), EX = std::cos(EDGE), EY = std::sin(EDGE);
    const double PROJECTION = std::clamp(X_VALUE * EX + Y * EY, 0., double(reach));
    return (X_VALUE - EX * PROJECTION) * (X_VALUE - EX * PROJECTION) + (Y - EY * PROJECTION) * (Y - EY * PROJECTION) <=
           double(bodyRadius) * bodyRadius;
}

}

