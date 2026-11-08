#pragma once
#include "Geometry.h"
#include <algorithm>
#include <vector>

namespace shared_world
{
// Immutable spatial acceleration built once on the asset/startup worker.
// Preorder nodes and escape indices need no query allocations or mutable cache.
// Original triangles stay immutable; only a separate index array is partitioned.
class TriangleBoundsTree
{
    struct Node
    {
        WorldPosition low{}, high{}; // Inclusive: flat triangles are valid bounds.
        std::uint32_t first = 0, count = 0, escape = 0;
    };

    std::vector<Node> nodes;
    std::vector<std::uint32_t> indices;

    static bool Overlaps(const Node& n, WorldPosition low, WorldPosition high)
    {
        return n.high.x >= low.x && n.low.x <= high.x && n.high.y >= low.y && n.low.y <= high.y && n.high.z >= low.z &&
               n.low.z <= high.z;
    }

    std::uint32_t Build(const std::vector<Triangle>& triangles, std::uint32_t first, std::uint32_t count)
    {
        const auto INDEX = static_cast<std::uint32_t>(this->nodes.size());
        this->nodes.push_back({});
        Node node;
        node.first = first;
        node.low = triangles[this->indices[first]].a;
        node.high = node.low;
        for (auto i = first; i < first + count; ++i)
        {
            for (auto p : {triangles[this->indices[i]].a, triangles[this->indices[i]].b, triangles[this->indices[i]].c})
            {
                node.low = {std::min(node.low.x, p.x), std::min(node.low.y, p.y), std::min(node.low.z, p.z)};
                node.high = {std::max(node.high.x, p.x), std::max(node.high.y, p.y), std::max(node.high.z, p.z)};
            }

        }

        if (count <= 8)
        {
            node.count = count;
        }
        else
        {
            const auto EXTENT = Difference(node.high, node.low);
            const int AXIS = EXTENT.x >= EXTENT.y && EXTENT.x >= EXTENT.z ? 0 : EXTENT.y >= EXTENT.z ? 1 : 2;
            auto center = [&](std::uint32_t i)
            {
                const auto& t = triangles[i];
                return AXIS == 0   ? std::int64_t(t.a.x) + t.b.x + t.c.x
                       : AXIS == 1 ? std::int64_t(t.a.y) + t.b.y + t.c.y
                                   : std::int64_t(t.a.z) + t.b.z + t.c.z;
            };

            std::nth_element(this->indices.begin() + first, this->indices.begin() + first + count / 2,
                             this->indices.begin() + first + count,
                             [&](auto a, auto b)
                             {
                                 const auto X_VALUE = center(a), Y = center(b);
                                 return X_VALUE != Y ? X_VALUE < Y : a < b;
                             });

            Build(triangles, first, count / 2);
            Build(triangles, first + count / 2, count - count / 2);
        }

        node.escape = static_cast<std::uint32_t>(this->nodes.size());
        this->nodes[INDEX] = node;
        return INDEX;
    }

public:
    void Build(const std::vector<Triangle>& triangles)
    {
        this->nodes.clear();
        this->indices.resize(triangles.size());
        for (std::uint32_t i = 0; i < this->indices.size(); ++i)
        {
            this->indices[i] = i;
        }

        if (!triangles.empty())
        {
            Build(triangles, 0, static_cast<std::uint32_t>(triangles.size()));
        }

    }

    std::size_t ResidentBytes() const
    {
        return this->nodes.capacity() * sizeof(Node) + this->indices.capacity() * sizeof(std::uint32_t);
    }

    // Visitor returns false to abort; traversal order is spatial, not source order.
    template <class Visitor> bool Visit(WorldPosition low, WorldPosition high, Visitor visitor) const
    {
        for (std::uint32_t index = 0; index < this->nodes.size();)
        {
            const auto& node = this->nodes[index];
            if (!Overlaps(node, low, high))
            {
                index = node.escape;
                continue;
            }

            for (auto i = node.first; i < node.first + node.count; ++i)
            {
                if (!visitor(this->indices[i]))
                {
                    return false;
                }

            }

            ++index;
        }

        return true;
    }

};

}

