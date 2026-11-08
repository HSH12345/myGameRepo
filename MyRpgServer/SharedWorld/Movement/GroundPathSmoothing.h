#pragma once
#include "GroundPath.h"

namespace shared_world
{
// Shortcuts over an already verified path, validated by the same inputs that the
// follower/server will execute. A ray alone cannot prove body/floor clearance.
// On budget/cancellation, retain the untouched original suffix.
inline std::vector<WorldPosition> SmoothGroundPath(const GroundCollision& collision,
                                                   const std::vector<WorldPosition>& path, unsigned moveBudget = 4096,
                                                   const std::function<bool()>& stopped = {},
                                                   const GroundEdgeClear& edgeClear = {})
{
    if (!collision || path.size() < 3 || path.size() > 4098 || !moveBudget || (stopped && stopped()))
    {
        return path;
    }

    for (auto p : path)
    {
        if (!IsValid(p))
        {
            return path;
        }

    }

    std::vector<std::size_t> candidates;
    for (std::size_t i = 1; i + 1 < path.size(); ++i)
    {
        const auto AX = std::int64_t(path[i].x) - path[i - 1].x, AY = std::int64_t(path[i].y) - path[i - 1].y;
        const auto BX = std::int64_t(path[i + 1].x) - path[i].x, BY = std::int64_t(path[i + 1].y) - path[i].y;
        // Preserve every actual corner; same-lattice collinear runs below
        // reuse their existing proof instead of repeating collision checks.
        if (AX * BY != AY * BX || AX * BX + AY * BY <= 0)
        {
            candidates.push_back(i);
        }

    }

    candidates.push_back(path.size() - 1);
    auto replayStraight = [&](WorldPosition from, WorldPosition to)
    {
        // Reject obvious obstacle crossings with one continuous upper-body
        // sweep before doing hundreds of exact support steps. This is only a
        // rejection filter; acceptance still requires the complete replay.
        const auto INITIAL = collision(from);
        if (!INITIAL)
        {
            return false;
        }

        const int HEIGHT = std::max(from.z, to.z) + 26;
        if (SweepBody(*INITIAL, {from.x, from.y, HEIGHT}, {to.x, to.y, HEIGHT}, {34, 155}).status != QueryStatus::CLEAR)
        {
            return false;
        }

        while (from.x != to.x || from.y != to.y)
        {
            if (!moveBudget || (stopped && stopped()))
            {
                return false;
            }

            --moveBudget;
            const auto STEP = GroundStep(from, to);
            const auto WORLD = collision(from);
            if (!WORLD || !collision({from.x + STEP.x, from.y + STEP.y, from.z}))
            {
                return false;
            }

            const auto MOVE = MoveOnGround(*WORLD, from, STEP.x, STEP.y);
            if (MOVE.position.x != from.x + STEP.x || MOVE.position.y != from.y + STEP.y ||
                (edgeClear && !edgeClear(from, MOVE.position)))
            {
                return false;
            }

            from = MOVE.position;
        }

        // The untouched suffix was verified from exactly this height/layer.
        return from == to;
    };

    std::map<std::tuple<int, int, int, int, int, int>, bool> checked;
    auto straight = [&](WorldPosition from, WorldPosition to)
    {
        const auto KEY = std::make_tuple(from.x, from.y, from.z, to.x, to.y, to.z);
        const auto FOUND = checked.find(KEY);
        if (FOUND != checked.end())
        {
            return FOUND->second;
        }

        const bool CLEAR = replayStraight(from, to);
        if (checked.size() < 4096)
        {
            checked.emplace(KEY, CLEAR);
        }

        return CLEAR;
    };

    // First validate whole straight runs. This guarantees useful simplification
    // even if attempts to cut across obstacle corners consume the time budget.
    std::vector<WorldPosition> runs{path.front()};
    std::size_t runStart = 0;
    for (auto end : candidates)
    {
        const auto DX = std::int64_t(path[end].x) - path[runStart].x;
        const auto DY = std::int64_t(path[end].y) - path[runStart].y;
        // Axis/45-degree runs execute the identical sequence of centimetre
        // positions regardless of 20cm input grouping. Their verified edges
        // can be concatenated without re-running physics over the same floor.
        const bool SAME_LATTICE = DX == 0 || DY == 0 || std::abs(DX) == std::abs(DY);
        if (end > runStart + 1 && (SAME_LATTICE || straight(path[runStart], path[end])))
        {
            runs.push_back(path[end]);
        }
        else
        {
            runs.insert(runs.end(), path.begin() + runStart + 1, path.begin() + end + 1);
        }

        runStart = end;
        if (!moveBudget || (stopped && stopped()))
        {
            runs.insert(runs.end(), path.begin() + runStart + 1, path.end());
            return runs;
        }

    }

    // One backward corner-removal pass, including the destination corner.
    // Avoid a second quadratic farthest-first pass over these same segments.
    for (std::size_t i = runs.size() - 2; i > 0; --i)
    {
        if (straight(runs[i - 1], runs[i + 1]))
        {
            runs.erase(runs.begin() + i);
        }

        if (!moveBudget || (stopped && stopped()))
        {
            return runs;
        }

    }

    return runs;
}

}

