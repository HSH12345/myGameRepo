#pragma once
#include "WorldContent.h"
#include <set>

namespace shared_world
{
// Mutable cache: exactly one owner. Queue slots are the reserved completion
// channel; Poll publishes on that owner only. No file I/O, waits or callbacks.
class ChunkResidency final
{
public:
    explicit ChunkResidency(const WorldContent& newContent, std::size_t count = 64,
                            std::size_t bytes = 128 * 1024 * 1024)
        : content(newContent), maxCount(count), maxBytes(bytes)
    {
        if (!count || count > 256 || !bytes || !newContent.collision)
        {
            throw std::invalid_argument("invalid cache budget");
        }

        if (newContent.chunks)
        {
            const auto C = newContent.chunks->layout.Coordinate(newContent.manifest.spawns.front().feet);
            if (newContent.collision->ResidentBytes() > bytes)
            {
                throw std::invalid_argument("initial chunk exceeds cache budget");
            }

            this->resident.emplace(C, Entry{newContent.collision, 0});
            this->used = newContent.collision->ResidentBytes();
        }

    }

    void BeginFrame(const std::vector<WorldPosition>& positions)
    {
        if (!this->content.chunks)
        {
            return;
        }

        ++this->frame;
        this->pinned.clear();
        this->pinned.insert(Coordinate(this->content.manifest.spawns.front().feet));
        for (auto p : positions)
        {
            if (this->content.chunks->layout.world.bounds.Contains(p))
            {
                this->pinned.insert(Coordinate(p));
            }

        }

        Poll();
        for (auto c : this->pinned)
        {
            Request(c);
        }

        for (auto p : positions)
        {
            Prefetch(p);
        }

    }

    std::shared_ptr<const StaticWorld> Get(WorldPosition p)
    {
        if (!this->content.chunks)
        {
            return this->content.collision;
        }

        if (!this->content.chunks->layout.world.bounds.Contains(p))
        {
            return {};
        }

        const auto C = Coordinate(p);
        auto it = this->resident.find(C);
        if (it == this->resident.end())
        {
            Request(C);
            return {};
        }

        it->second.used = this->frame;
        return it->second.world;
    }

    MovementResult Move(WorldPosition start, int dx, int dy)
    {
        if (!IsValid(start) || dx < -20 || dx > 20 || dy < -20 || dy > 20 || dx * dx + dy * dy > 400)
        {
            return {QueryStatus::INVALID_INPUT, start};
        }

        const auto TARGET = OffsetPosition(start, {dx, dy, 0}, false);
        if (!TARGET)
        {
            return {QueryStatus::INVALID_INPUT, start};
        }

        // Current halo is not permission to enter an absent/unready core.
        const auto CURRENT = Get(start);
        const auto NEXT = Get(*TARGET);
        if (!CURRENT || !NEXT)
        {
            return {QueryStatus::BUDGET_EXCEEDED, start};
        }

        const auto MOVED = MoveOnGround(*CURRENT, start, dx, dy);
        if (this->content.chunks && !this->content.chunks->layout.world.bounds.Contains(MOVED.position))
        {
            return {QueryStatus::NOT_FOUND, start};
        }

        return MOVED;
    }

    bool Failed(WorldPosition p) const
    {
        return this->content.chunks && this->failed.count(Coordinate(p)) != 0;
    }

    std::size_t ResidentCount() const
    {
        return this->resident.size();
    }

    std::size_t ResidentBytes() const
    {
        return this->used;
    }

    std::uint64_t Loads() const
    {
        return this->loads;
    }

private:
    struct Entry
    {
        std::shared_ptr<const StaticWorld> world;
        std::uint64_t used = 0;
    };

    TileCoord Coordinate(WorldPosition p) const
    {
        return this->content.chunks->layout.Coordinate(p);
    }

    void Request(TileCoord c)
    {
        if (this->resident.count(c) || this->pending.count(c) || this->failed.count(c))
        {
            return;
        }

        const auto RETRY = this->retryAfter.find(c);
        if (RETRY != this->retryAfter.end() && this->frame < RETRY->second)
        {
            return;
        }

        if (this->content.loader->Submit(c, 1, ++this->request))
        {
            this->pending.emplace(c, this->request);
            this->retryAfter.erase(c);
        }

    }

    void Prefetch(WorldPosition p)
    {
        if (!this->content.chunks->layout.world.bounds.Contains(p))
        {
            return;
        }

        // Two seconds of walking ahead, independent of UE's streaming grid.
        for (int dx : {-1200, 0, 1200})
        {
            for (int dy : {-1200, 0, 1200})
            {
                if (auto q = OffsetPosition(p, {dx, dy, 0}, false);
                    q && this->content.chunks->layout.world.bounds.Contains(*q))
                {
                    Request(Coordinate(*q));
                }

            }

        }

    }

    void Poll()
    {
        ChunkLoadQueue::Result result;
        while (this->content.loader->TryPop(result))
        {
            auto p = this->pending.find(result.coordinate);
            if (p == this->pending.end() || result.generation != 1 || p->second != result.request)
            {
                continue;
            }

            this->pending.erase(p);
            if (result.status != ChunkLoadQueue::Status::LOADED)
            {
                this->failed.insert(result.coordinate);
                continue;
            }

            const auto COST = result.collision->ResidentBytes();
            if (COST > this->maxBytes)
            {
                this->failed.insert(result.coordinate);
                continue;
            }

            while (this->resident.size() >= this->maxCount || COST > this->maxBytes - this->used)
            {
                auto victim = this->resident.end();
                for (auto it = this->resident.begin(); it != this->resident.end(); ++it)
                {
                    if (!this->pinned.count(it->first) && it->second.world.use_count() == 1 &&
                        (victim == this->resident.end() || it->second.used < victim->second.used))
                    {
                        victim = it;
                    }

                }

                if (victim == this->resident.end())
                {
                    break;
                }

                this->used -= victim->second.world->ResidentBytes();
                this->resident.erase(victim);
            }

            if (this->resident.size() < this->maxCount && COST <= this->maxBytes - this->used)
            {
                this->used += COST;
                this->resident.emplace(result.coordinate, Entry{std::move(result.collision), this->frame});
                ++this->loads;
            }
            else
            {
                this->retryAfter[result.coordinate] = this->frame + 30;
            }

            // If all residents are pinned, refuse publication. A later frame may
            // request again after pins/references are released; never exceed budget.
        }

    }

    const WorldContent& content;
    const std::size_t maxCount, maxBytes;
    std::size_t used = 0;
    std::uint64_t frame = 0, request = 1, loads = 0;
    std::map<TileCoord, Entry> resident;
    std::map<TileCoord, std::uint64_t> pending;
    std::map<TileCoord, std::uint64_t> retryAfter;
    std::set<TileCoord> pinned, failed;
};

}

