#pragma once

#include "GameCommand.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

// Bounded lock-free MPSC queue based on per-slot sequence numbers. Producers
// never wait for the game thread or take a shared mutex. Capacity is rounded
// up to a power of two so a configured capacity is never silently reduced.
class GameCommandQueue final
{
public:
    explicit GameCommandQueue(std::size_t requestedCapacity)
        : capacity(NormalizeCapacity(requestedCapacity)), mask(this->capacity - 1),
          slots(std::make_unique<Slot[]>(this->capacity)), enqueueCursor(std::make_unique<Cursor>()),
          dequeueCursor(std::make_unique<Cursor>())
    {
        for (std::size_t index = 0; index < this->capacity; ++index)
        {
            this->slots[index].sequence.store(index, std::memory_order_relaxed);
        }

    }

    bool TryPush(GameCommand command)
    {
        std::size_t position = this->enqueueCursor->value.load(std::memory_order_relaxed);
        Slot* slot = nullptr;
        for (;;)
        {
            slot = &this->slots[position & this->mask];
            const std::size_t SEQUENCE = slot->sequence.load(std::memory_order_acquire);
            const auto DIFFERENCE_VALUE = static_cast<std::intptr_t>(SEQUENCE) - static_cast<std::intptr_t>(position);
            if (DIFFERENCE_VALUE == 0)
            {
                if (this->enqueueCursor->value.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    break;
                }

            }
            else if (DIFFERENCE_VALUE < 0)
            {
                this->dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            else
            {
                position = this->enqueueCursor->value.load(std::memory_order_relaxed);
            }

        }

        slot->command.emplace(std::move(command));
        slot->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(GameCommand& output)
    {
        const std::size_t POSITION = this->dequeueCursor->value.load(std::memory_order_relaxed);
        Slot& slot = this->slots[POSITION & this->mask];
        const std::size_t SEQUENCE = slot.sequence.load(std::memory_order_acquire);
        const auto DIFFERENCE_VALUE = static_cast<std::intptr_t>(SEQUENCE) - static_cast<std::intptr_t>(POSITION + 1);
        if (DIFFERENCE_VALUE != 0)
        {
            return false;
        }

        output = std::move(*slot.command);
        slot.command.reset();
        slot.sequence.store(POSITION + this->capacity, std::memory_order_release);
        this->dequeueCursor->value.store(POSITION + 1, std::memory_order_release);
        return true;
    }

    std::size_t Drain(std::vector<GameCommand>& output, std::size_t maxCount)
    {
        std::size_t count = 0;
        GameCommand command;
        while (count < maxCount && TryPop(command))
        {
            output.push_back(std::move(command));
            ++count;
        }

        return count;
    }

    std::size_t Size() const
    {
        const std::size_t DEQUEUED = this->dequeueCursor->value.load(std::memory_order_acquire);
        const std::size_t ENQUEUED = this->enqueueCursor->value.load(std::memory_order_acquire);
        // Approximate snapshot: the consumer may advance while producers refill.
        // Read dequeue first to avoid unsigned underflow from a newer dequeue.
        const auto DEPTH = ENQUEUED - DEQUEUED;
        return DEPTH > this->capacity ? this->capacity : DEPTH;
    }

    std::size_t Capacity() const
    {
        return this->capacity;
    }

    std::uint64_t DroppedCount() const
    {
        return this->dropped.load(std::memory_order_relaxed);
    }

private:
    struct Slot
    {
        std::atomic<std::size_t> sequence{0};
        std::optional<GameCommand> command;
    };

    struct alignas(64) Cursor
    {
        std::atomic<std::size_t> value{0};
        std::byte padding[64 - sizeof(std::atomic<std::size_t>)]{};
    };

    static_assert(sizeof(Cursor) == 64, "Cursor must occupy one cache line");

    static std::size_t NormalizeCapacity(std::size_t requested)
    {
        if (requested < 2)
        {
            return 2;
        }

        std::size_t result = 2;
        while (result < requested)
        {
            if (result > (std::numeric_limits<std::size_t>::max)() / 2)
            {
                throw std::length_error("Game command queue capacity is too large.");
            }

            result *= 2;
        }

        return result;
    }

    const std::size_t capacity;
    const std::size_t mask;
    std::unique_ptr<Slot[]> slots;
    std::unique_ptr<Cursor> enqueueCursor;
    std::unique_ptr<Cursor> dequeueCursor;
    std::atomic<std::uint64_t> dropped{0};
};

