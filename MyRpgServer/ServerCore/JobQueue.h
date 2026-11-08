#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

struct Job
{
    std::function<void()> task;
    std::function<bool()> shouldCancel;
    std::function<void()> onCancelled;
};

class JobQueue
{
public:
    explicit JobQueue(std::size_t newMaxSize = 4096) : maxSize(newMaxSize)
    {
    }

    bool Push(Job job);
    bool TryPop(Job& job);
    bool WaitAndPop(Job& job);
    void Start();
    void Stop(bool drainPending = false);
    bool IsRunning() const;
    std::size_t Size() const;

private:
    std::deque<Job> queue;
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool running = false;
    bool drainOnStop = false;
    const std::size_t maxSize;
};

