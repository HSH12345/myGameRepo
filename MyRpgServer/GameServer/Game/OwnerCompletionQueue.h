#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

// Reserve before launching external work: completion delivery cannot fail due to
// user-command saturation. Poll only from the owning game worker.
class OwnerCompletionQueue final
{
public:
    struct Slot
    {
        void Complete(std::function<void()> value)
        {
            std::lock_guard lock(this->mutex);
            this->callback = std::move(value);
            this->ready = true;
        }

        std::mutex mutex;
        std::function<void()> callback;
        bool ready = false;
    };

    explicit OwnerCompletionQueue(std::size_t limit = 512) : capacity(limit)
    {
    }

    std::shared_ptr<Slot> Reserve()
    {
        std::lock_guard lock(this->mutex);
        if (this->slots.size() >= this->capacity)
        {
            return {};
        }

        auto slot = std::make_shared<Slot>();
        this->slots.push_back(slot);
        return slot;
    }

    bool TryPop(std::function<void()>& output)
    {
        std::lock_guard lock(this->mutex);
        for (auto it = this->slots.begin(); it != this->slots.end(); ++it)
        {
            const auto SLOT = *it;
            std::lock_guard slotLock(SLOT->mutex);
            if (!SLOT->ready)
            {
                continue;
            }

            output = std::move(SLOT->callback);
            this->slots.erase(it);
            return true;
        }

        return false;
    }

    std::size_t Size() const
    {
        std::lock_guard lock(this->mutex);
        return this->slots.size();
    }

private:
    const std::size_t capacity;
    mutable std::mutex mutex;
    std::deque<std::shared_ptr<Slot>> slots;
};

