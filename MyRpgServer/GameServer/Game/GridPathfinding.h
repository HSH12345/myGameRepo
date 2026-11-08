#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>

// Small server-side navigation baseline. Not an Unreal NavMesh importer.
class NavigationGrid final
{
public:
    NavigationGrid(std::uint32_t newWidth, std::uint32_t newHeight, std::vector<std::uint8_t> obstacles)
        : width(newWidth), height(newHeight), cells(std::move(obstacles))
    {
        if (!newWidth || !newHeight || static_cast<std::uint64_t>(newWidth) * newHeight > 262144 ||
            this->cells.size() != static_cast<std::size_t>(newWidth) * newHeight)
        {
            throw std::invalid_argument("Invalid navigation grid");
        }

    }

    const std::uint32_t width, height;

    bool Walkable(std::uint32_t index) const
    {
        return index < this->cells.size() && this->cells[index] == 0;
    }

    std::size_t Size() const
    {
        return this->cells.size();
    }

private:
    const std::vector<std::uint8_t> cells;
};

enum class PathStatus
{
    FOUND,
    UNREACHABLE,
    CANCELLED,
    BUDGET_EXCEEDED,
    INVALID_INPUT
};

struct GridPathResult
{
    PathStatus status = PathStatus::INVALID_INPUT;
    std::vector<std::uint32_t> cells;
};

inline GridPathResult FindGridPath(const NavigationGrid& grid, std::uint32_t start, std::uint32_t goal,
                                   std::uint32_t nodeBudget, const std::function<bool()>& cancelled = {})
{
    if (!nodeBudget || !grid.Walkable(start) || !grid.Walkable(goal))
    {
        return {};
    }

    struct Node
    {
        std::uint32_t score, distance, index;
    };

    auto compare = [](const Node& a, const Node& b)
    {
        return a.score > b.score;
    };

    std::priority_queue<Node, std::vector<Node>, decltype(compare)> open(compare);
    constexpr auto MISSING = (std::numeric_limits<std::uint32_t>::max)();
    std::vector<std::uint32_t> distances(grid.Size(), MISSING), parents(grid.Size(), MISSING);
    auto heuristic = [&grid, goal](std::uint32_t index)
    {
        const auto AX = index % grid.width, AY = index / grid.width, BX = goal % grid.width, BY = goal / grid.width;
        return (AX > BX ? AX - BX : BX - AX) + (AY > BY ? AY - BY : BY - AY);
    };

    distances[start] = 0;
    open.push({heuristic(start), 0, start});
    std::uint32_t visited = 0;
    while (!open.empty())
    {
        if (cancelled && cancelled())
        {
            return {PathStatus::CANCELLED, {}};
        }

        const auto NODE = open.top();
        open.pop();
        if (NODE.distance != distances[NODE.index])
        {
            continue;
        }

        if (++visited > nodeBudget)
        {
            return {PathStatus::BUDGET_EXCEEDED, {}};
        }

        if (NODE.index == goal)
        {
            GridPathResult result{PathStatus::FOUND, {}};
            for (auto index = goal; index != MISSING; index = parents[index])
            {
                result.cells.push_back(index);
            }

            std::reverse(result.cells.begin(), result.cells.end());
            return result;
        }

        auto visit = [&](std::uint32_t next)
        {
            const auto DISTANCE = NODE.distance + 1;
            if (!grid.Walkable(next) || DISTANCE >= distances[next])
            {
                return;
            }

            distances[next] = DISTANCE;
            parents[next] = NODE.index;
            open.push({DISTANCE + heuristic(next), DISTANCE, next});
        };

        if (NODE.index % grid.width)
        {
            visit(NODE.index - 1);
        }

        if (NODE.index % grid.width + 1 < grid.width)
        {
            visit(NODE.index + 1);
        }

        if (NODE.index >= grid.width)
        {
            visit(NODE.index - grid.width);
        }

        if (NODE.index / grid.width + 1 < grid.height)
        {
            visit(NODE.index + grid.width);
        }

    }

    return {PathStatus::UNREACHABLE, {}};
}

