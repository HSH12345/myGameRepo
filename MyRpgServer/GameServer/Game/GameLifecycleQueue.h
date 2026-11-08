#pragma once

#include "GameCommand.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>

// Keeps one retained entry per accepted session. A disconnect updates that
// entry instead of competing with gameplay traffic for a queue slot.
class GameLifecycleQueue final
{
public:
    struct Work
    {
        GameCommand command;
        std::uint64_t generation = 0;
        std::uint32_t attempt = 0;
    };

    explicit GameLifecycleQueue(std::size_t maximumEntries) : capacity((std::max)(std::size_t{1}, maximumEntries))
    {
    }

    bool TryConnect(GameCommand command)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->entries.size() >= this->capacity || this->entries.contains(command.sessionId))
        {
            this->dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const std::uint64_t SESSION_ID = command.sessionId;
        Entry entry;
        entry.pending.emplace(std::move(command));
        entry.generation = 1;
        entry.queued = true;
        this->entries.emplace(SESSION_ID, std::move(entry));
        this->order.push_back(SESSION_ID);
        return true;
    }

    bool Disconnect(GameCommand command)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        const auto IT = this->entries.find(command.sessionId);
        if (IT == this->entries.end())
        {
            return false;
        }

        IT->second.pending.emplace(std::move(command));
        ++IT->second.generation;
        IT->second.attempt = 0;
        if (!IT->second.queued)
        {
            IT->second.queued = true;
            this->order.push_back(IT->first);
        }

        return true;
    }

    bool TryBegin(Work& output)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->order.empty())
        {
            return false;
        }

        const std::uint64_t SESSION_ID = this->order.front();
        this->order.pop_front();
        const auto IT = this->entries.find(SESSION_ID);
        if (IT == this->entries.end() || !IT->second.pending)
        {
            return false;
        }

        output.command = *IT->second.pending;
        output.generation = IT->second.generation;
        output.attempt = IT->second.attempt;
        IT->second.pending.reset();
        IT->second.queued = false;
        return true;
    }

    void Complete(Work work, bool succeeded)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        const auto IT = this->entries.find(work.command.sessionId);
        if (IT == this->entries.end())
        {
            return;
        }

        if (succeeded)
        {
            if (std::holds_alternative<SessionDisconnectedCommand>(work.command.payload) &&
                IT->second.generation == work.generation && !IT->second.pending)
            {
                this->entries.erase(IT);
            }

            return;
        }

        if (IT->second.generation != work.generation || IT->second.pending)
        {
            return;
        }

        if (work.attempt >= MAX_ATTEMPTS)
        {
            this->deadLettered.fetch_add(1, std::memory_order_relaxed);
            this->entries.erase(IT);
            return;
        }

        IT->second.pending.emplace(std::move(work.command));
        IT->second.attempt = work.attempt + 1;
        if (!IT->second.queued)
        {
            IT->second.queued = true;
            this->order.push_back(IT->first);
        }

    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->order.size();
    }

    std::size_t TrackedSessionCount() const
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->entries.size();
    }

    std::uint64_t DroppedCount() const
    {
        return this->dropped.load(std::memory_order_relaxed);
    }

    std::uint64_t DeadLetteredCount() const
    {
        return this->deadLettered.load(std::memory_order_relaxed);
    }

private:
    struct Entry
    {
        std::optional<GameCommand> pending;
        bool queued = false;
        std::uint64_t generation = 0;
        std::uint32_t attempt = 0;
    };

    static constexpr std::uint32_t MAX_ATTEMPTS = 3;
    const std::size_t capacity;
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, Entry> entries;
    std::deque<std::uint64_t> order;
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> deadLettered{0};
};

