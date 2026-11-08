#pragma once
#include "JobQueue.h"
#include "Session.h"
#include "StructuredLogger.h"
#include <atomic>
#include <thread>
#include <vector>

// Shared lifecycle only; domain validation and retry policy stay in services.
class BoundedWorkerPool final
{
public:
    explicit BoundedWorkerPool(std::size_t capacity) : jobs(capacity)
    {
    }

    ~BoundedWorkerPool()
    {
        Stop();
    }

    bool Start(unsigned int count)
    {
        if (count == 0 || this->running.exchange(true))
        {
            return false;
        }

        this->jobs.Start();
        try
        {
            for (unsigned int index = 0; index < count; ++index)
            {
                this->workers.emplace_back(
                    [this]
                    {
                        Run();
                    });

            }

        }

        catch (...)
        {
            Stop();
            return false;
        }

        return true;
    }

    void Stop(bool drainPending = false)
    {
        if (!this->running.exchange(false))
        {
            return;
        }

        this->jobs.Stop(drainPending);
        for (auto& worker : this->workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }

        }

        this->workers.clear();
    }

    bool Submit(Job job)
    {
        if (!this->running)
        {
            return false;
        }

        struct Ticket
        {
            explicit Ticket(std::atomic<std::size_t>& value) : count(value)
            {
                ++this->count;
            }

            ~Ticket()
            {
                --this->count;
            }

            std::atomic<std::size_t>& count;
        };

        auto ticket = std::make_shared<Ticket>(this->outstanding);
        auto task = std::move(job.task);
        job.task = [ticket, task = std::move(task)]
        {
            if (task)
            {
                task();
            }

        };

        return this->jobs.Push(std::move(job));
    }

    bool IsRunning() const
    {
        return this->running;
    }

    std::size_t PendingCount() const
    {
        return this->jobs.Size();
    }

    std::size_t OutstandingCount() const
    {
        return this->outstanding.load();
    }

private:
    void Run()
    {
        Job job;
        while (this->jobs.WaitAndPop(job))
        {
            try
            {
                if (job.shouldCancel && job.shouldCancel())
                {
                    if (job.onCancelled)
                    {
                        job.onCancelled();
                    }

                }
                else if (job.task)
                {
                    job.task();
                }

            }

            catch (...)
            {
                RPG_LOG_ERROR("Worker", "Domain job failed.");
                try
                {
                    if (job.onCancelled)
                    {
                        job.onCancelled();
                    }

                }

                catch (...)
                {
                }

            }

            // Release session quota immediately, not when the next job arrives.
            job = {};
        }

    }

    JobQueue jobs;
    std::vector<std::thread> workers;
    std::atomic<bool> running{false};
    std::atomic<std::size_t> outstanding{0};
};

struct PendingSessionWork final
{
    explicit PendingSessionWork(SessionPtr owner) : session(std::move(owner))
    {
    }

    ~PendingSessionWork()
    {
        this->session->CompletePendingCommand();
    }

    SessionPtr session;

    static std::shared_ptr<PendingSessionWork> Acquire(const SessionPtr& newSession)
    {
        if (!newSession || !newSession->IsOpen() || !newSession->TryAcquirePendingCommand(4))
        {
            return {};
        }

        try
        {
            return std::make_shared<PendingSessionWork>(newSession);
        }

        catch (...)
        {
            newSession->CompletePendingCommand();
            throw;
        }

    }

};

