#pragma once

#include "GameCommandQueue.h"
#include "GameLifecycleQueue.h"
#include "GameWorld.h"
#include "OwnerCompletionQueue.h"
#include "EconomyAccess.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <array>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <memory>

class LatencyHistogram final
{
public:
    void Observe(std::uint64_t microseconds)
    {
        for (std::size_t i = 0; i < BOUNDS.size(); ++i)
        {
            if (microseconds <= BOUNDS[i])
            {
                this->buckets[i].fetch_add(1, std::memory_order_relaxed);
                return;
            }

        }

    }

    std::uint64_t Percentile(std::uint32_t percentile) const
    {
        std::uint64_t total = 0;
        for (const auto& bucket : this->buckets)
        {
            total += bucket.load(std::memory_order_relaxed);
        }

        if (total == 0)
        {
            return 0;
        }

        const std::uint64_t TARGET = (total * (std::min)(percentile, 100U) + 99) / 100;
        std::uint64_t cumulative = 0;
        for (std::size_t i = 0; i < this->buckets.size(); ++i)
        {
            cumulative += this->buckets[i].load(std::memory_order_relaxed);
            if (cumulative >= TARGET)
            {
                return BOUNDS[i];
            }

        }

        return BOUNDS.back();
    }

private:
    static constexpr std::array<std::uint64_t, 12> BOUNDS{
        100,    250,    500,    1'000,   2'000,   5'000,
        10'000, 20'000, 50'000, 100'000, 500'000, (std::numeric_limits<std::uint64_t>::max)()};
    std::array<std::atomic<std::uint64_t>, BOUNDS.size()> buckets{};
};

struct GameRuntimeOptions
{
    std::uint32_t ticksPerSecond = 30;
    std::size_t maxQueuedCommands = 65'536;
    std::size_t maxCommandsPerTick = 4'096;
    std::uint32_t commandBudgetMicroseconds = 10'000;
    std::size_t maxTrackedLifecycleSessions = 20'000;
    std::uint32_t shutdownHardTimeoutMilliseconds = 30'000;
    std::shared_ptr<const GameMapCatalog> maps;
    std::function<bool(const SessionPtr&, const shared_world::net::Message&)> sendWorld;
    TransferWorld transferWorld;
    std::uint64_t sharedInstance = 0;
    std::vector<std::uint64_t> dungeonInstances;
    unsigned readyTimeoutSeconds = 120;
    std::function<void(std::uint64_t)> worldClosed;
    std::shared_ptr<const shared_world::economy::Catalog> economyCatalog;
    SubmitEconomy submitEconomy;
    TransferParty transferParty;
    std::uint32_t defaultDungeonMap = 0;
};

class GameRuntime final
{
public:
    explicit GameRuntime(GameRuntimeOptions newOptions = {});
    ~GameRuntime();
    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;

    bool Start();
    // Called only by this runtime's reserved owner completion.
    void AdmitTransfer(std::uint64_t instance, WorldTransfer transfer);
    void Stop();
    bool TryEnqueueGameplay(GameCommand command);
    bool TryNotifyConnected(GameCommand command);
    bool NotifyDisconnected(GameCommand command);

    std::shared_ptr<OwnerCompletionQueue::Slot> ReserveCompletion()
    {
        return this->completions.Reserve();
    }

    bool IsRunning() const
    {
        return this->running.load(std::memory_order_acquire);
    }

    std::size_t GetQueueDepth() const
    {
        return this->commands.Size();
    }

    std::size_t GetQueueCapacity() const
    {
        return this->commands.Capacity();
    }

    std::size_t GetLifecycleQueueDepth() const
    {
        return this->lifecycle.Size();
    }

    std::size_t GetTrackedLifecycleSessionCount() const
    {
        return this->lifecycle.TrackedSessionCount();
    }

    std::uint64_t GetDroppedCommandCount() const
    {
        return this->commands.DroppedCount() + this->admissionRejected.load() + this->lifecycle.DroppedCount() +
               this->lifecycle.DeadLetteredCount();
    }

    std::uint64_t GetProcessedCommandCount() const
    {
        return this->processedCommands.load();
    }

    std::uint64_t GetProcessedLifecycleCount() const
    {
        return this->processedLifecycle.load();
    }

    std::uint64_t GetTickOverrunCount() const
    {
        return this->tickOverrunCount.load(std::memory_order_relaxed);
    }

    std::uint64_t GetMaxTickMicroseconds() const
    {
        return this->maxTickMicroseconds.load(std::memory_order_relaxed);
    }

    std::uint64_t GetMaxCommandLatencyMicroseconds() const
    {
        return this->maxCommandLatencyMicroseconds.load(std::memory_order_relaxed);
    }

    std::uint64_t GetTickLatencyPercentile(std::uint32_t percentile) const
    {
        return this->tickLatency.Percentile(percentile);
    }

    std::uint64_t GetCommandLatencyPercentile(std::uint32_t percentile) const
    {
        return this->commandLatency.Percentile(percentile);
    }

private:
    void Run();
    void Process(GameCommand command);

    const GameRuntimeOptions options;
    GameCommandQueue commands;
    GameLifecycleQueue lifecycle;
    OwnerCompletionQueue completions;
    // Only this runtime's worker accesses these maps. Multiple maps share one worker.
    std::unordered_map<std::uint64_t, std::unique_ptr<GameWorld>> worlds;
    std::atomic<std::uint64_t> processedCommands{0}, processedLifecycle{0};
    std::thread gameThread;
    std::atomic<bool> running{false};
    std::atomic<bool> queueDraining{false};
    std::atomic<std::uint64_t> admissionRejected{0};
    std::mutex stopMutex;
    std::condition_variable stopCv;
    bool threadExited = true;
    std::atomic<std::uint64_t> tickOverrunCount{0};
    std::atomic<std::uint64_t> maxTickMicroseconds{0};
    std::atomic<std::uint64_t> maxCommandLatencyMicroseconds{0};
    LatencyHistogram tickLatency;
    LatencyHistogram commandLatency;
};

