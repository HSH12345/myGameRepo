#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace shared_world
{
// Centimeters, Unreal axes: X forward, Y right, Z up. ±1,000 km per axis.
// Three squared deltas fit int64_t: 3 * (2 * Limit)^2 = 120,000,000,000,000,000.
inline constexpr std::int32_t COORDINATE_LIMIT = 100'000'000;

struct WorldPosition
{
    std::int32_t x = 0, y = 0, z = 0;

    friend constexpr bool operator==(WorldPosition a, WorldPosition b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

};

constexpr bool IsValid(WorldPosition p) noexcept
{
    return p.x >= -COORDINATE_LIMIT && p.x <= COORDINATE_LIMIT && p.y >= -COORDINATE_LIMIT && p.y <= COORDINATE_LIMIT &&
           p.z >= -COORDINATE_LIMIT && p.z <= COORDINATE_LIMIT;
}

// Round nearest centimeter, ties away from zero. No dependence on FPU rounding mode.
inline std::optional<WorldPosition> WorldToCanonical(double x, double y, double z) noexcept
{
    const auto QUANTIZE = [](double value) -> std::optional<std::int32_t>
    {
        if (!std::isfinite(value))
        {
            return std::nullopt;
        }

        const double ROUNDED = value < 0 ? std::ceil(value - 0.5) : std::floor(value + 0.5);
        if (ROUNDED < -COORDINATE_LIMIT || ROUNDED > COORDINATE_LIMIT)
        {
            return std::nullopt;
        }

        return static_cast<std::int32_t>(ROUNDED);
    };

    const auto QX = QUANTIZE(x), QY = QUANTIZE(y), QZ = QUANTIZE(z);
    if (!QX || !QY || !QZ)
    {
        return std::nullopt;
    }

    return WorldPosition{*QX, *QY, *QZ};
}

inline std::array<double, 3> CanonicalToWorld(WorldPosition p)
{
    if (!IsValid(p))
    {
        throw std::invalid_argument("invalid canonical position");
    }

    return {static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)};
}

inline std::uint64_t DistanceSquared(WorldPosition a, WorldPosition b, bool includeZ)
{
    if (!IsValid(a) || !IsValid(b))
    {
        throw std::invalid_argument("invalid canonical position");
    }

    const std::int64_t DX = static_cast<std::int64_t>(a.x) - b.x;
    const std::int64_t DY = static_cast<std::int64_t>(a.y) - b.y;
    const std::int64_t DZ = includeZ ? static_cast<std::int64_t>(a.z) - b.z : 0;
    return static_cast<std::uint64_t>(DX * DX + DY * DY + DZ * DZ);
}

inline std::uint64_t DistanceSquared2D(WorldPosition a, WorldPosition b)
{
    return DistanceSquared(a, b, false);
}

// floor(sqrt(value)), integer-only, valid for the entire uint64_t domain.
constexpr std::uint64_t IntegerSqrt(std::uint64_t value) noexcept
{
    std::uint64_t result = 0;
    std::uint64_t bit = std::uint64_t{1} << 62;
    while (bit > value)
    {
        bit >>= 2;
    }

    while (bit != 0)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
        {
            result >>= 1;
        }

        bit >>= 2;
    }

    return result;
}

inline std::uint64_t Distance2D(WorldPosition a, WorldPosition b)
{
    return IntegerSqrt(DistanceSquared2D(a, b));
}

inline std::uint64_t Distance3D(WorldPosition a, WorldPosition b)
{
    return IntegerSqrt(DistanceSquared(a, b, true));
}

struct FixedDirection2D
{
    std::int32_t x = 0, y = 0;
};

// Q10000 direction; zero displacement returns zero. Integer length is rounded upward
// so truncation never makes the direction exceed unit length.
inline FixedDirection2D Direction2D(WorldPosition from, WorldPosition to)
{
    const auto SQUARED = DistanceSquared2D(from, to);
    if (!SQUARED)
    {
        return {};
    }

    auto length = IntegerSqrt(SQUARED);
    if (length * length < SQUARED)
    {
        ++length;
    }

    const auto DIVISOR = static_cast<std::int64_t>(length);
    return {static_cast<std::int32_t>((static_cast<std::int64_t>(to.x) - from.x) * 10'000 / DIVISOR),
            static_cast<std::int32_t>((static_cast<std::int64_t>(to.y) - from.y) * 10'000 / DIVISOR)};
}

} // namespace shared_world

