#pragma once
#include "MovementController.h"
#include <functional>
#include <map>
#include <queue>
#include <tuple>
#include <cmath>

namespace shared_world
{
enum class GroundPathStatus
{
    FOUND,
    UNREACHABLE,
    BUDGET_EXCEEDED,
    CANCELLED,
    INVALID_INPUT
};

struct GroundPathResult
{
    GroundPathStatus status = GroundPathStatus::INVALID_INPUT;
    std::vector<WorldPosition> points;
    std::size_t expanded = 0;
};

// The caller supplies immutable collision snapshots. Missing terrain is blocked.
// This is a bounded local path suggestion, not authority to move: every emitted
// 30Hz input must still pass the ordinary server movement validator.
using GroundEdgeClear = std::function<bool(WorldPosition, WorldPosition)>;
using GroundCollision = std::function<std::shared_ptr<const StaticWorld>(WorldPosition)>;

inline bool GroundDestinationClear(const StaticWorld& world, WorldPosition goal)
{
    const auto SUPPORT = GroundSupport(world, goal);
    if (SUPPORT.status != QueryStatus::HIT || std::abs(SUPPORT.position.z - goal.z) > 25)
    {
        return false;
    }

    const auto STANDING = WorldPosition{goal.x, goal.y, SUPPORT.position.z + 1};
    return SweepBody(world, STANDING, STANDING).status == QueryStatus::CLEAR;
}

inline WorldPosition GroundStep(WorldPosition from, WorldPosition to, int maximum = 20)
{
    const auto DX = std::int64_t(to.x) - from.x, DY = std::int64_t(to.y) - from.y;
    const auto LENGTH = std::sqrt(double(DX * DX + DY * DY));
    if (LENGTH <= maximum)
    {
        return {static_cast<int>(DX), static_cast<int>(DY), 0};
    }

    return {static_cast<int>(DX * maximum / LENGTH), static_cast<int>(DY * maximum / LENGTH), 0};
}

inline GroundPathResult FindGroundPath(const GroundCollision& collision, WorldPosition start, WorldPosition goal,
                                       unsigned nodeBudget = 1024, const std::function<bool()>& cancelled = {},
                                       const GroundEdgeClear& edgeClear = {})
{
    GroundPathResult result;
    constexpr int CELL = 100, RADIUS = 4000;
    if (!collision || !IsValid(start) || !IsValid(goal) || !nodeBudget || nodeBudget > 4096 ||
        std::abs(std::int64_t(goal.x) - start.x) > RADIUS || std::abs(std::int64_t(goal.y) - start.y) > RADIUS)
    {
        return result;
    }

    if (cancelled && cancelled())
    {
        result.status = GroundPathStatus::CANCELLED;
        return result;
    }

    // Reject occupied/unsupported destinations before exploring a whole graph.
    // This uses the same full footprint and body shape as ordinary walking.
    const auto GOAL_WORLD = collision(goal);
    if (!GOAL_WORLD)
    {
        result.status = GroundPathStatus::UNREACHABLE;
        return result;
    }

    if (!GroundDestinationClear(*GOAL_WORLD, goal) || (edgeClear && !edgeClear(goal, goal)))
    {
        result.status = GroundPathStatus::UNREACHABLE;
        return result;
    }

    // Request-local, bounded cache. Repeated graph edges and the failed direct
    // attempt often ask for exactly the same immutable movement sweep.
    std::map<std::tuple<int, int, int, int, int>, MovementResult> moves;
    auto walk = [&](WorldPosition from, WorldPosition to, WorldPosition& end, unsigned stepLimit = 12)
    {
        end = from;
        for (unsigned step = 0; step < stepLimit && (end.x != to.x || end.y != to.y); ++step)
        {
            if (cancelled && cancelled())
            {
                return false;
            }

            const auto DELTA_VALUE = GroundStep(end, to);
            const auto DATA = collision(end);
            if (!DATA || !collision({end.x + DELTA_VALUE.x, end.y + DELTA_VALUE.y, end.z}))
            {
                return false;
            }

            const auto MOVE_KEY = std::make_tuple(end.x, end.y, end.z, DELTA_VALUE.x, DELTA_VALUE.y);
            auto previous = moves.find(MOVE_KEY);
            const auto MOVE =
                previous != moves.end() ? previous->second : MoveOnGround(*DATA, end, DELTA_VALUE.x, DELTA_VALUE.y);
            if (previous == moves.end() && moves.size() < 32768)
            {
                moves.emplace(MOVE_KEY, MOVE);
            }

            if (MOVE.position.x != end.x + DELTA_VALUE.x || MOVE.position.y != end.y + DELTA_VALUE.y ||
                (edgeClear && !edgeClear(end, MOVE.position)))
            {
                return false;
            }

            end = MOVE.position;
        }

        return end.x == to.x && end.y == to.y;
    };

    // Most nearby clicks have a clear direct route. Validate that route with
    // the same bounded movement sweeps before paying for graph expansion.
    // 600 steps cover the existing 40m-per-axis domain, including rounding.
    WorldPosition direct;
    if (walk(start, goal, direct, 600) && std::abs(direct.z - goal.z) <= 25)
    {
        result.status = GroundPathStatus::FOUND;
        result.points = {start, direct};
        return result;
    }

    struct Node
    {
        WorldPosition feet;
        unsigned cost, parent;
    };

    struct Candidate
    {
        unsigned score, cost, parent;
        WorldPosition target;
    };

    // Weighted A* (weight 2) prioritizes responsiveness over exact grid-cost
    // optimality. Eight directions reduce artificial staircase routes. Every
    // selected diagonal still validates the full body; no corner cutting.
    auto compare = [](const Candidate& a, const Candidate& b)
    {
        return a.score != b.score ? a.score > b.score : a.cost < b.cost;
    };

    std::priority_queue<Candidate, std::vector<Candidate>, decltype(compare)> open(compare);
    std::vector<Node> nodes;
    std::map<std::tuple<int, int, int>, unsigned> indices;
    auto key = [](WorldPosition p)
    {
        return std::make_tuple(p.x, p.y, p.z);
    };

    auto heuristic = [&](WorldPosition p)
    {
        const double X_VALUE = double(p.x) - goal.x, Y = double(p.y) - goal.y;
        return unsigned(std::sqrt(X_VALUE * X_VALUE + Y * Y));
    };

    nodes.push_back({start, 0, 0});
    indices.emplace(key(start), 0);
    auto enqueue = [&](unsigned index)
    {
        const auto CURRENT = nodes[index];
        for (auto delta : {WorldPosition{CELL, 0, 0},
                           {-CELL, 0, 0},
                           {0, CELL, 0},
                           {0, -CELL, 0},
                           {CELL, CELL, 0},
                           {CELL, -CELL, 0},
                           {-CELL, CELL, 0},
                           {-CELL, -CELL, 0}})
        {
            const auto NEXT = OffsetPosition(CURRENT.feet, delta, false);
            if (!NEXT || std::abs(NEXT->x - start.x) > RADIUS || std::abs(NEXT->y - start.y) > RADIUS)
            {
                continue;
            }

            const auto COST = CURRENT.cost + (delta.x && delta.y ? 142u : 100u);
            open.push({COST + heuristic(*NEXT) * 2, COST, index, *NEXT});
        }

    };

    enqueue(0);
    while (!open.empty())
    {
        if (cancelled && cancelled())
        {
            result.status = GroundPathStatus::CANCELLED;
            return result;
        }

        const auto TOP = open.top();
        open.pop();
        const auto PARENT = nodes[TOP.parent];
        const auto EDGE_COST = (TOP.target.x != PARENT.feet.x && TOP.target.y != PARENT.feet.y) ? 142u : 100u;
        if (TOP.cost != PARENT.cost + EDGE_COST)
        {
            continue;
        }

        // Validate an edge only when A* actually selects it. Previously all
        // outgoing edges paid full centimetre sweeps, including unused alternatives.
        WorldPosition end;
        if (!walk(nodes[TOP.parent].feet, TOP.target, end))
        {
            continue;
        }

        auto found = indices.find(key(end));
        unsigned index;
        if (found == indices.end())
        {
            if (nodes.size() >= nodeBudget + 1)
            {
                result.status = GroundPathStatus::BUDGET_EXCEEDED;
                return result;
            }

            index = static_cast<unsigned>(nodes.size());
            indices.emplace(key(end), index);
            nodes.push_back({end, TOP.cost, TOP.parent});
        }
        else
        {
            index = found->second;
            if (TOP.cost >= nodes[index].cost)
            {
                continue;
            }

            nodes[index].cost = TOP.cost;
            nodes[index].parent = TOP.parent;
        }

        const auto CURRENT = nodes[index];
        if (++result.expanded > nodeBudget)
        {
            result.status = GroundPathStatus::BUDGET_EXCEEDED;
            return result;
        }

        if (heuristic(CURRENT.feet) <= 142 && walk(CURRENT.feet, goal, end) && std::abs(end.z - goal.z) <= 25)
        {
            result.points.push_back(end);
            for (auto pathIndex = index;; pathIndex = nodes[pathIndex].parent)
            {
                result.points.push_back(nodes[pathIndex].feet);
                if (pathIndex == 0)
                {
                    break;
                }

            }

            std::reverse(result.points.begin(), result.points.end());
            result.status = GroundPathStatus::FOUND;
            return result;
        }

        enqueue(index);
    }

    result.status = GroundPathStatus::UNREACHABLE;
    return result;
}

}

