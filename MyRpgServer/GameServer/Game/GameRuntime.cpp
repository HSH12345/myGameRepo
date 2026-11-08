#include "GameRuntime.h"

#include "../../ServerCore/StructuredLogger.h"

#include <algorithm>
#include <Windows.h>
#include <cstdlib>

namespace
{
void UpdateMaximum(std::atomic<std::uint64_t>& target, std::uint64_t value)
{
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }

}

}

GameRuntime::GameRuntime(GameRuntimeOptions newOptions)
    : options(newOptions), commands((std::max)(std::size_t{1}, newOptions.maxQueuedCommands)),
      lifecycle(newOptions.maxTrackedLifecycleSessions)
{
}

GameRuntime::~GameRuntime()
{
    Stop();
}

bool GameRuntime::Start()
{
    if (this->options.ticksPerSecond == 0 || this->options.maxCommandsPerTick == 0 ||
        this->options.commandBudgetMicroseconds == 0 || this->options.maxTrackedLifecycleSessions == 0)
    {
        return false;
    }

    bool expected = false;
    if (!this->running.compare_exchange_strong(expected, true))
    {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(this->stopMutex);
        this->threadExited = false;
    }

    try
    {
        this->gameThread = std::thread(&GameRuntime::Run, this);
    }

    catch (...)
    {
        this->running = false;
        std::lock_guard<std::mutex> lock(this->stopMutex);
        this->threadExited = true;
        return false;
    }

    return true;
}

void GameRuntime::Stop()
{
    if (!this->running.exchange(false))
    {
        return;
    }

    this->stopCv.notify_all();
    {
        std::unique_lock<std::mutex> lock(this->stopMutex);
        if (!this->stopCv.wait_for(lock, std::chrono::milliseconds(this->options.shutdownHardTimeoutMilliseconds),
                                   [this]
                                   {
                                       return this->threadExited;
                                   }))

        {
            RPG_LOG_CRITICAL("Game", "Game thread shutdown hard timeout reached; terminating process.");
            lock.unlock();
            if (!TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT))
            {
                std::abort();
            }

            std::abort();
        }

    }

    if (this->gameThread.joinable())
    {
        this->gameThread.join();
    }

}

bool GameRuntime::TryEnqueueGameplay(GameCommand command)
{
    if (!this->running.load(std::memory_order_acquire))
    {
        return false;
    }

    const auto DEPTH = this->commands.Size();
    const auto CAPACITY = this->commands.Capacity();
    if (DEPTH >= (CAPACITY * 80 + 99) / 100)
    {
        this->queueDraining.store(true);
    }
    else if (DEPTH <= CAPACITY * 30 / 100)
    {
        this->queueDraining.store(false);
    }

    if (this->queueDraining.load())
    {
        ++this->admissionRejected;
        return false;
    }

    return this->commands.TryPush(std::move(command));
}

bool GameRuntime::TryNotifyConnected(GameCommand command)
{
    return this->running.load(std::memory_order_acquire) && this->lifecycle.TryConnect(std::move(command));
}

bool GameRuntime::NotifyDisconnected(GameCommand command)
{
    return this->lifecycle.Disconnect(std::move(command));
}

void GameRuntime::Process(GameCommand command)
{
    struct PendingGuard
    {
        SessionPtr session;

        ~PendingGuard()
        {
            if (this->session)
            {
                this->session->CompletePendingCommand();
            }

        }

    } pending{(std::holds_alternative<TestMessageCommand>(command.payload) ||

               std::holds_alternative<WorldMessageCommand>(command.payload))
                  ? command.session.lock()
                  : nullptr};

    auto& world = this->worlds[command.mapInstanceId];
    if (!world)
    {
        std::shared_ptr<const GameMapContent> content;
        if (this->options.maps)
        {
            auto found = this->options.maps->find(command.mapInstanceId);
            if (found != this->options.maps->end())
            {
                content = found->second;
            }

        }

        world = std::make_unique<GameWorld>(
            content, command.mapInstanceId, this->options.sendWorld, this->options.transferWorld,
            this->options.readyTimeoutSeconds, this->options.worldClosed, this->options.economyCatalog,
            this->options.submitEconomy, this->options.transferParty,
            command.mapInstanceId == this->options.sharedInstance ? this->options.defaultDungeonMap : 0);
        world->BindToCurrentThread();
    }

    const auto COMMANDS_BEFORE = world->GetProcessedCommandCount();
    const auto LIFECYCLE_BEFORE = world->GetProcessedLifecycleCount();
    world->Process(std::move(command));
    this->processedCommands.fetch_add(world->GetProcessedCommandCount() - COMMANDS_BEFORE);
    this->processedLifecycle.fetch_add(world->GetProcessedLifecycleCount() - LIFECYCLE_BEFORE);
}

void GameRuntime::AdmitTransfer(std::uint64_t instance, WorldTransfer transfer)
{
    auto session = transfer.session.lock();
    if (!session || !session->IsOpen())
    {
        return;
    }

    auto& world = this->worlds[instance];
    if (!world)
    {
        auto found = this->options.maps->find(instance);
        if (found == this->options.maps->end())
        {
            session->Close("transfer content unavailable");
            return;
        }

        world = std::make_unique<GameWorld>(
            found->second, instance, this->options.sendWorld, this->options.transferWorld,
            this->options.readyTimeoutSeconds, this->options.worldClosed, this->options.economyCatalog,
            this->options.submitEconomy, this->options.transferParty,
            instance == this->options.sharedInstance ? this->options.defaultDungeonMap : 0);
        world->BindToCurrentThread();
    }

    world->AdmitTransfer(std::move(transfer));
}

void GameRuntime::Run()
{
    // Start/Stop are serialized by the owner; a restarted worker has a new ID.
    for (auto& [id, world] : this->worlds)
    {
        if (world)
        {
            world->BindToCurrentThread();
        }

    }

    using Clock = std::chrono::steady_clock;
    const auto TICK_INTERVAL =
        std::chrono::nanoseconds(std::chrono::nanoseconds::period::den / this->options.ticksPerSecond);
    auto nextTick = Clock::now();
    RPG_LOG_INFO("Game", "Game thread started. ticksPerSecond=" << this->options.ticksPerSecond);

    while (this->running.load(std::memory_order_acquire) || this->lifecycle.Size() != 0 || this->commands.Size() != 0 ||
           this->completions.Size() != 0)
    {
        nextTick += TICK_INTERVAL;
        const auto TICK_START = Clock::now();
        const auto COMMAND_DEADLINE = TICK_START + std::chrono::microseconds(this->options.commandBudgetMicroseconds);
        std::size_t processedThisTick = 0;
        // Internal results must keep draining while new user work is rejected.
        std::function<void()> completion;
        for (std::size_t i = 0; i < 128 && Clock::now() < COMMAND_DEADLINE && this->completions.TryPop(completion); ++i)
        {
            try
            {
                if (completion)
                {
                    completion();
                }

            }

            catch (...)
            {
                RPG_LOG_ERROR("Game", "Owner completion failed.");
            }

            completion = {};
        }

        const std::size_t LIFECYCLE_QUOTA = (std::max)(std::size_t{1}, this->options.maxCommandsPerTick / 4);
        auto processLifecycle = [&]() -> bool
        {
            GameLifecycleQueue::Work work;
            if (!this->lifecycle.TryBegin(work))
            {
                return false;
            }

            const auto LATENCY =
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - work.command.enqueuedAt).count();
            if (LATENCY > 0)
            {
                UpdateMaximum(this->maxCommandLatencyMicroseconds, static_cast<std::uint64_t>(LATENCY));
                this->commandLatency.Observe(static_cast<std::uint64_t>(LATENCY));
            }

            bool succeeded = false;
            try
            {
                Process(work.command);
                succeeded = true;
            }

            catch (const std::exception& exception)
            {
                RPG_LOG_ERROR("Game", "Game command failed. error=" << exception.what());
            }

            catch (...)
            {
                RPG_LOG_ERROR("Game", "Game command failed with an unknown exception.");
            }

            this->lifecycle.Complete(std::move(work), succeeded);
            return true;
        };

        for (std::size_t count = 0; count < LIFECYCLE_QUOTA && processedThisTick < this->options.maxCommandsPerTick &&
                                    Clock::now() < COMMAND_DEADLINE;
             ++count, ++processedThisTick)
        {
            if (!processLifecycle())
            {
                break;
            }

        }

        for (; processedThisTick < this->options.maxCommandsPerTick && Clock::now() < COMMAND_DEADLINE;
             ++processedThisTick)
        {
            GameCommand command;
            if (!this->commands.TryPop(command))
            {
                break;
            }

            const auto LATENCY =
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - command.enqueuedAt).count();
            if (LATENCY > 0)
            {
                UpdateMaximum(this->maxCommandLatencyMicroseconds, static_cast<std::uint64_t>(LATENCY));
                this->commandLatency.Observe(static_cast<std::uint64_t>(LATENCY));
            }

            try
            {
                Process(std::move(command));
            }

            catch (const std::exception& exception)
            {
                RPG_LOG_ERROR("Game", "Game command failed. error=" << exception.what());
            }

            catch (...)
            {
                RPG_LOG_ERROR("Game", "Game command failed with an unknown exception.");
            }

        }

        for (; processedThisTick < this->options.maxCommandsPerTick && Clock::now() < COMMAND_DEADLINE;
             ++processedThisTick)
        {
            if (!processLifecycle())
            {
                break;
            }

        }

        if (this->running.load(std::memory_order_acquire))
        {
            for (auto& [id, world] : this->worlds)
            {
                try
                {
                    if (world)
                    {
                        world->Tick(std::chrono::duration<double>(TICK_INTERVAL).count());
                    }

                }

                catch (...)
                {
                    RPG_LOG_CRITICAL("Game", "World tick failed. mapInstanceId=" << id);
                }

            }

        }

        const auto TICK_END = Clock::now();
        const auto TICK_MICROSECONDS =
            std::chrono::duration_cast<std::chrono::microseconds>(TICK_END - TICK_START).count();
        if (TICK_MICROSECONDS > 0)
        {
            UpdateMaximum(this->maxTickMicroseconds, static_cast<std::uint64_t>(TICK_MICROSECONDS));
            this->tickLatency.Observe(static_cast<std::uint64_t>(TICK_MICROSECONDS));
        }

        if (TICK_END > nextTick)
        {
            this->tickOverrunCount.fetch_add(1, std::memory_order_relaxed);
        }

        if (this->running.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> lock(this->stopMutex);
            this->stopCv.wait_until(lock, nextTick,
                                    [this]
                                    {
                                        return !this->running.load(std::memory_order_acquire);
                                    });

        }

        // Do not execute an unbounded catch-up loop after a long stall.
        if (Clock::now() - nextTick > TICK_INTERVAL * 3)
        {
            nextTick = Clock::now();
        }

    }

    RPG_LOG_INFO("Game", "Game thread stopped. processed=" << GetProcessedCommandCount()
                                                           << ", dropped=" << GetDroppedCommandCount());
    {
        std::lock_guard<std::mutex> lock(this->stopMutex);
        this->threadExited = true;
    }

    this->stopCv.notify_all();
}

