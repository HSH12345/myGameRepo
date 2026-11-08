#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <stdexcept>

// Tracks accepted work until completion, NOT just bytes still in a TCP queue.
// The owner must outlive every ticket. Accepted jobs bypass admission on execution.
class DependencyAdmission final
{
public:
    using Clock = std::chrono::steady_clock;
    enum class State
    {
        OPEN,
        DRAINING,
        RECOVERING
    };

    struct Options
    {
        std::size_t maxCount = 512;
        std::size_t maxBytes = 8 * 1024 * 1024;
        unsigned int highPercent = 80, lowPercent = 30;
        std::chrono::milliseconds maxAge{2000}, stableFor{2000}, recoveryFor{5000};
        std::size_t recoveryRequestsPerSecond = 8;
    };

    struct Snapshot
    {
        State state;
        std::size_t count, bytes;
        std::uint64_t rejected;
        std::chrono::milliseconds oldestAge;
        bool healthy;
    };

    DependencyAdmission() : DependencyAdmission(Options{})
    {
    }

    explicit DependencyAdmission(Options value) : options(value)
    {
        if (!this->options.maxCount || !this->options.maxBytes || !this->options.recoveryRequestsPerSecond ||
            this->options.highPercent > 100 || this->options.lowPercent >= this->options.highPercent ||
            this->options.maxAge.count() <= 0 || this->options.stableFor.count() < 0 ||
            this->options.recoveryFor.count() < 0)
        {
            throw std::invalid_argument("Invalid dependency admission limits");
        }

    }

    std::optional<std::uint64_t> TryAccept(std::size_t size, Clock::time_point now = Clock::now())
    {
        std::lock_guard lock(this->mutex);
        Refresh(now);
        if (this->state == State::DRAINING || this->pending.size() >= this->options.maxCount ||
            size > this->options.maxBytes - this->bytes)
        {
            ++this->rejected;
            return {};
        }

        if (this->state == State::RECOVERING)
        {
            if (now - this->recoveryWindow >= std::chrono::seconds(1))
            {
                this->recoveryWindow = now;
                this->recoveryCount = 0;
            }

            if (this->recoveryCount >= this->options.recoveryRequestsPerSecond)
            {
                ++this->rejected;
                return {};
            }

            ++this->recoveryCount;
        }

        const auto ID = ++this->nextId;
        this->pending.emplace(ID, Entry{size, now});
        this->bytes += size;
        Refresh(now);
        return ID;
    }

    void Complete(std::uint64_t id, bool dependencyHealthy, Clock::time_point now = Clock::now())
    {
        std::lock_guard lock(this->mutex);
        const auto ITEM = this->pending.find(id);
        if (ITEM == this->pending.end())
        {
            return;
        }

        this->bytes -= ITEM->second.bytes;
        this->pending.erase(ITEM);
        // Success of a single old request must not clear an outage. Only a probe does.
        if (!dependencyHealthy)
        {
            this->healthy = false;
        }

        Refresh(now);
    }

    void ObserveProbe(bool succeeded, Clock::time_point now = Clock::now())
    {
        std::lock_guard lock(this->mutex);
        this->healthy = succeeded;
        Refresh(now);
    }

    Snapshot Inspect(Clock::time_point now = Clock::now())
    {
        std::lock_guard lock(this->mutex);
        Refresh(now);
        return {this->state, this->pending.size(), this->bytes, this->rejected, OldestAge(now), this->healthy};
    }

    bool AllowsUsers(Clock::time_point now = Clock::now())
    {
        std::lock_guard lock(this->mutex);
        Refresh(now);
        return this->state != State::DRAINING;
    }

    bool TryAdmitUser(Clock::time_point now = Clock::now())
    {
        std::lock_guard lock(this->mutex);
        Refresh(now);
        if (this->state == State::DRAINING)
        {
            ++this->rejected;
            return false;
        }

        if (this->state == State::RECOVERING)
        {
            if (now - this->userWindow >= std::chrono::seconds(1))
            {
                this->userWindow = now;
                this->userCount = 0;
            }

            if (this->userCount >= this->options.recoveryRequestsPerSecond)
            {
                ++this->rejected;
                return false;
            }

            ++this->userCount;
        }

        return true;
    }

    struct Lease
    {
        DependencyAdmission& owner;
        std::uint64_t id;

        ~Lease()
        {
            this->owner.Complete(this->id, true);
        }

    };

    std::shared_ptr<Lease> Acquire(std::size_t size)
    {
        const auto ID = TryAccept(size);
        if (!ID)
        {
            return {};
        }

        try
        {
            return std::shared_ptr<Lease>(new Lease{*this, *ID});
        }

        catch (...)
        {
            Complete(*ID, true);
            throw;
        }

    }

private:
    struct Entry
    {
        std::size_t bytes;
        Clock::time_point started;
    };

    std::chrono::milliseconds OldestAge(Clock::time_point now) const
    {
        return this->pending.empty()
                   ? std::chrono::milliseconds{0}
                   : std::chrono::duration_cast<std::chrono::milliseconds>(now - this->pending.begin()->second.started);
    }

    void Refresh(Clock::time_point now)
    {
        const bool HIGH = !this->healthy ||
                          this->pending.size() >= (this->options.maxCount * this->options.highPercent + 99) / 100 ||
                          this->bytes >= (this->options.maxBytes * this->options.highPercent + 99) / 100 ||
                          OldestAge(now) >= this->options.maxAge;
        if (HIGH)
        {
            this->state = State::DRAINING;
            this->lowSince.reset();
            return;
        }

        if (this->state == State::DRAINING)
        {
            const bool LOW = this->pending.size() <= this->options.maxCount * this->options.lowPercent / 100 &&
                             this->bytes <= this->options.maxBytes * this->options.lowPercent / 100;
            if (!LOW)
            {
                this->lowSince.reset();
                return;
            }

            if (!this->lowSince)
            {
                this->lowSince = now;
            }

            if (now - *this->lowSince >= this->options.stableFor)
            {
                this->state = State::RECOVERING;
                this->recoveryStarted = this->recoveryWindow = this->userWindow = now;
                this->recoveryCount = this->userCount = 0;
            }

        }
        else if (this->state == State::RECOVERING && now - this->recoveryStarted >= this->options.recoveryFor)
        {
            this->state = State::OPEN;
        }

    }

    const Options options;
    std::mutex mutex;
    std::map<std::uint64_t, Entry> pending;
    std::size_t bytes = 0, recoveryCount = 0;
    std::uint64_t nextId = 0, rejected = 0;
    bool healthy = true;
    State state = State::OPEN;
    std::optional<Clock::time_point> lowSince;
    Clock::time_point recoveryStarted{}, recoveryWindow{};
    Clock::time_point userWindow{};
    std::size_t userCount = 0;
};

