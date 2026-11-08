#pragma once
#include "BoundedWorkerPool.h"
#include "DependencyAdmission.h"
#include <condition_variable>
#include <functional>
#include <memory>

// Separate read/write execution capacity. Probes never share these saturated queues.
// true from work means the dependency answered normally (including business rejection).
// It is NOT the application's commit result; callers preserve that distinction.
class DependencyExecutor final
{
public:
    using Work = std::function<bool()>;

    explicit DependencyExecutor(DependencyAdmission::Options options = {})
        : admission(options), reads(options.maxCount), writes(options.maxCount)
    {
    }

    ~DependencyExecutor()
    {
        Stop();
    }

    bool Start(unsigned int readThreads, unsigned int writeThreads, Work healthProbe)
    {
        if (this->running.exchange(true))
        {
            return false;
        }

        this->probe = std::move(healthProbe);
        if (!this->probe || !this->reads.Start(readThreads) || !this->writes.Start(writeThreads))
        {
            Stop();
            return false;
        }

        try
        {
            this->probeThread = std::thread(
                [this]
                {
                    ProbeLoop();
                });

        }

        catch (...)
        {
            Stop();
            return false;
        }

        return true;
    }

    bool Submit(std::size_t bytes, bool important, Work work)
    {
        if (!this->running || !work)
        {
            return false;
        }

        const auto TICKET = this->admission.TryAccept(bytes);
        if (!TICKET)
        {
            return false;
        }

        try
        {
            Job job;
            job.task = [this, id = *TICKET, work = std::move(work)]
            {
                bool healthy = false;
                try
                {
                    healthy = work();
                }

                catch (...)
                {
                    RPG_LOG_ERROR("Dependency", "Accepted work failed; outcome must be reconciled by operation ID.");
                }

                this->admission.Complete(id, healthy);
            };

            if ((important ? this->writes : this->reads).Submit(std::move(job)))
            {
                return true;
            }

        }

        catch (...)
        {
            this->admission.Complete(*TICKET, true);
            throw;
        }

        this->admission.Complete(*TICKET, true);
        return false;
    }

    void Stop()
    {
        if (!this->running.exchange(false))
        {
            return;
        }

        this->wake.notify_all();
        // Accepted work survives a user disconnect and a graceful process stop.
        // Its own bounded I/O deadline still applies; an uncertain commit is not replayed here.
        this->reads.Stop(true);
        this->writes.Stop(true);
        if (this->probeThread.joinable())
        {
            this->probeThread.join();
        }

    }

    DependencyAdmission& Admission()
    {
        return this->admission;
    }

private:
    void ProbeLoop()
    {
        while (this->running)
        {
            if (this->admission.Inspect().state != DependencyAdmission::State::OPEN)
            {
                bool healthy = false;
                try
                {
                    healthy = this->probe();
                }

                catch (...)
                {
                }

                this->admission.ObserveProbe(healthy);
            }

            std::unique_lock lock(this->waitMutex);
            this->wake.wait_for(lock, std::chrono::milliseconds(500),
                                [this]
                                {
                                    return !this->running;
                                });

        }

    }

    DependencyAdmission admission;
    BoundedWorkerPool reads, writes;
    Work probe;
    std::atomic<bool> running{false};
    std::thread probeThread;
    std::mutex waitMutex;
    std::condition_variable wake;
};

