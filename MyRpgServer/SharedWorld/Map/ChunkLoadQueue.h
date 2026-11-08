#pragma once
#include "ChunkLayout.h"
#include "MapDataProvider.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace shared_world
{
// One asset worker. A slot is reserved from admission through owner consumption,
// so even a stalled consumer cannot grow a separate completion queue.
// Construct/destroy on a service lifecycle thread, never on IOCP/game tick.
// RequestStop is nonblocking; destruction joins the last in-progress file read.
class ChunkLoadQueue final
{
public:
    enum class Status
    {
        LOADED,
        FAILED_VALUE,
        CANCELLED
    };

    struct Result
    {
        std::uint64_t request = 0, generation = 0;
        TileCoord coordinate{};
        Status status = Status::FAILED_VALUE;
        std::shared_ptr<const StaticWorld> collision;
    };

    ChunkLoadQueue(ChunkManifest newManifest, std::shared_ptr<const MapDataProvider> newProvider,
                   std::size_t newSlots = 8, std::size_t packageByteBudget = 8 * MAX_PACKAGE_BYTES)
        : manifest(std::move(newManifest)), provider(std::move(newProvider))
    {
        this->manifest.Validate();
        if (!this->provider || !newSlots || newSlots > 64 || packageByteBudget / MAX_PACKAGE_BYTES < newSlots)
        {
            throw std::invalid_argument("invalid chunk load queue budget");
        }

        this->slots.resize(newSlots);
        for (const auto& ref : this->manifest.content.chunks)
        {
            this->references.emplace(TileCoord{ref.x, ref.y}, ref);
        }

        this->worker = std::thread(
            [this]
            {
                Run();
            });

    }

    ~ChunkLoadQueue()
    {
        RequestStop();
        if (this->worker.joinable())
        {
            this->worker.join();
        }

    }

    ChunkLoadQueue(const ChunkLoadQueue&) = delete;
    ChunkLoadQueue& operator=(const ChunkLoadQueue&) = delete;

    // No filesystem calls or callbacks. False means no request was accepted.
    bool Submit(TileCoord coordinate, std::uint64_t generation, std::uint64_t request)
    {
        if (!request || !generation || !this->references.count(coordinate))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->stopping || this->nextOrder == UINT64_MAX)
        {
            return false;
        }

        for (const auto& slot : this->slots)
        {
            if (slot.state != State::FREE && slot.result.request == request && slot.result.generation == generation)
            {
                return false;
            }

        }

        for (auto& slot : this->slots)
        {
            if (slot.state == State::FREE)
            {
                slot.result = {request, generation, coordinate, Status::FAILED_VALUE, {}};
                slot.cancelled = false;
                slot.order = ++this->nextOrder;
                slot.state = State::QUEUED;
                ++this->outstanding;
                this->wake.notify_one();
                return true;
            }

        }

        return false;
    }

    // Owner must compare generation again before publishing into live map state.
    bool TryPop(Result& result)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        for (auto& slot : this->slots)
        {
            if (slot.state == State::COMPLETE)
            {
                result = std::move(slot.result);
                slot = Slot{};
                --this->outstanding;
                return true;
            }

        }

        return false;
    }

    void Cancel(std::uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        for (auto& slot : this->slots)
        {
            if (slot.state != State::FREE && slot.result.generation == generation)
            {
                CancelSlot(slot);
            }

        }

    }

    void RequestStop()
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->stopping = true;
        for (auto& slot : this->slots)
        {
            if (slot.state != State::FREE)
            {
                CancelSlot(slot);
            }

        }

        this->wake.notify_one();
    }

    std::size_t Outstanding() const
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->outstanding;
    }

    // Conservative encoded-package reservations, not total decoded heap usage.
    std::size_t ReservedPackageBytes() const
    {
        return Outstanding() * MAX_PACKAGE_BYTES;
    }

    std::size_t Completed() const
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        return static_cast<std::size_t>(std::count_if(this->slots.begin(), this->slots.end(),
                                                      [](const Slot& slot)
                                                      {
                                                          return slot.state == State::COMPLETE;
                                                      }));

    }

private:
    enum class State
    {
        FREE,
        QUEUED,
        LOADING,
        COMPLETE
    };

    struct Slot
    {
        State state = State::FREE;
        bool cancelled = false;
        std::uint64_t order = 0;
        Result result;
    };

    static void CancelSlot(Slot& slot)
    {
        slot.cancelled = true;
        if (slot.state != State::LOADING)
        {
            slot.result.collision.reset();
            slot.result.status = Status::CANCELLED;
            slot.state = State::COMPLETE;
        }

    }

    void Run()
    {
        for (;;)
        {
            std::size_t selected = this->slots.size();
            TileCoord coordinate{};
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                this->wake.wait(lock,
                                [&]
                                {
                                    return this->stopping || std::any_of(this->slots.begin(), this->slots.end(),
                                                                         [](const Slot& slot)
                                                                         {
                                                                             return slot.state == State::QUEUED;
                                                                         });

                                });

                if (this->stopping)
                {
                    return;
                }

                auto oldest = UINT64_MAX;
                for (std::size_t i = 0; i < this->slots.size(); ++i)
                {
                    if (this->slots[i].state == State::QUEUED &&
                        (selected == this->slots.size() || this->slots[i].order < oldest))
                    {
                        selected = i;
                        oldest = this->slots[i].order;
                    }

                }

                this->slots[selected].state = State::LOADING;
                coordinate = this->slots[selected].result.coordinate;
            }

            // The reserved slot cannot be reused until this worker publishes.
            std::shared_ptr<const StaticWorld> collision;
            try
            {
                const auto& ref = this->references.at(coordinate);
                auto map = this->provider->LoadChunk(ref);
                const auto BOUNDS = this->manifest.layout.Bounds(coordinate, true);
                if (!map || !(map->Identity() == ref.identity) || !(map->Definition().bounds.min == BOUNDS.min) ||
                    !(map->Definition().bounds.max == BOUNDS.max) ||
                    !(map->Definition().origin == this->manifest.layout.world.origin) ||
                    map->Definition().tileSize != this->manifest.layout.world.tileSize ||
                    map->Definition().aoiCellSize != this->manifest.layout.world.aoiCellSize ||
                    map->Definition().collisionCellSize != this->manifest.layout.world.collisionCellSize)
                {
                    throw std::invalid_argument("chunk does not match index");
                }

                collision = std::make_shared<const StaticWorld>(std::move(map));
            }

            catch (...)
            {
                collision.reset();
            }

            {
                std::lock_guard<std::mutex> lock(this->mutex);
                auto& slot = this->slots[selected];
                slot.result.status = slot.cancelled ? Status::CANCELLED
                                     : collision    ? Status::LOADED
                                                    : Status::FAILED_VALUE;
                if (!slot.cancelled)
                {
                    slot.result.collision = std::move(collision);
                }

                slot.state = State::COMPLETE;
            }

        }

    }

    const ChunkManifest manifest;
    const std::shared_ptr<const MapDataProvider> provider;
    std::map<TileCoord, MapChunkReference> references;
    std::vector<Slot> slots;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::size_t outstanding = 0;
    std::uint64_t nextOrder = 0;
    bool stopping = false;
};

}

