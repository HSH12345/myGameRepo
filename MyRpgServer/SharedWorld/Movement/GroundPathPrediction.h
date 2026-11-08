#pragma once
#include "GroundPath.h"

namespace shared_world
{
// Six ordinary 30Hz inputs: at most 120cm before the complete route is known.
// The live client still validates each input, including after server correction.
constexpr unsigned GROUND_PREDICTION_STEPS = 6;

inline std::vector<WorldPosition> BuildGroundPredictionPrefix(const GroundCollision& collision, WorldPosition start,
                                                              WorldPosition goal,
                                                              const std::function<bool()>& cancelled = {})
{
    std::vector<WorldPosition> prefix{start};
    if (!collision || !IsValid(start) || !IsValid(goal))
    {
        return prefix;
    }

    for (unsigned i = 0; i < GROUND_PREDICTION_STEPS; ++i)
    {
        if (cancelled && cancelled())
        {
            break;
        }

        const auto FROM = prefix.back(), DELTA_VALUE = GroundStep(FROM, goal);
        if (!DELTA_VALUE.x && !DELTA_VALUE.y)
        {
            break;
        }

        const auto WORLD = collision(FROM);
        if (!WORLD || !collision({FROM.x + DELTA_VALUE.x, FROM.y + DELTA_VALUE.y, FROM.z}))
        {
            break;
        }

        const auto MOVE = MoveOnGround(*WORLD, FROM, DELTA_VALUE.x, DELTA_VALUE.y);
        if (MOVE.status == QueryStatus::BUDGET_EXCEEDED || MOVE.position == FROM)
        {
            break;
        }

        prefix.push_back(MOVE.position);
        if (MOVE.position.x != FROM.x + DELTA_VALUE.x || MOVE.position.y != FROM.y + DELTA_VALUE.y)
        {
            break;
        }

    }

    return prefix;
}

// Only an exact canonical prefix position may consume the prepared route.
// A correction off this prefix requires replanning, never a teleport/backtrack.
inline std::optional<std::size_t> GroundPrefixCursor(const std::vector<WorldPosition>& prefix, WorldPosition current)
{
    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        if (prefix[i] == current)
        {
            return i;
        }

    }

    return {};
}

}

