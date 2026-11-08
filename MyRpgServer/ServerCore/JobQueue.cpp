#include "JobQueue.h"

bool JobQueue::Push(Job job)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    if (!this->running || this->queue.size() >= this->maxSize)
    {
        return false;
    }

    this->queue.push_back(std::move(job));
    this->cv.notify_one();
    return true;
}

bool JobQueue::TryPop(Job& job)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->queue.empty())
    {
        return false;
    }

    job = std::move(this->queue.front());
    this->queue.pop_front();
    return true;
}

bool JobQueue::WaitAndPop(Job& job)
{
    std::unique_lock<std::mutex> lock(this->mutex);
    this->cv.wait(lock,
                  [&]
                  {
                      return !this->running || !this->queue.empty();
                  });

    if (!this->running && !this->drainOnStop)
    {
        return false;
    }

    if (this->queue.empty())
    {
        return false;
    }

    job = std::move(this->queue.front());
    this->queue.pop_front();
    return true;
}

void JobQueue::Start()
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->drainOnStop = false;
    this->running = true;
}

void JobQueue::Stop(bool drainPending)
{
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->running = false;
        this->drainOnStop = drainPending;
        if (!drainPending)
        {
            this->queue.clear();
        }

    }

    this->cv.notify_all();
}

bool JobQueue::IsRunning() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->running;
}

std::size_t JobQueue::Size() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->queue.size();
}

