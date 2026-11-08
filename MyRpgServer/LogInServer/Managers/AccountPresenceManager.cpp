#include "AccountPresenceManager.h"

#include "AccountDataClient.h"
#include "../../ServerCore/StructuredLogger.h"

#include <chrono>
#include <process.h>

AccountPresenceManager& AccountPresenceManager::GetInstance()
{
    static AccountPresenceManager instance;
    return instance;
}

bool AccountPresenceManager::Start()
{
    std::unique_lock<std::mutex> lock(this->mutex);
    if (this->accepting)
    {
        return true;
    }

    this->serverInstanceId =
        std::to_string(_getpid()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    lock.unlock();
    const auto STARTUP_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    // ServerManager may launch Login while the account database is still opening.
    // Initializing the same instance lease is idempotent; never change its ID here.
    bool initialized = false;
    do
    {
        initialized =
            AccountDataClient::GetInstance().InitializePresenceServer(this->serverInstanceId, STARTUP_DEADLINE);
        if (initialized)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < STARTUP_DEADLINE);

    if (!initialized)
    {
        RPG_LOG_ERROR("Database", "Failed to initialize the login presence server lease.");
        return false;
    }

    lock.lock();
    this->stopping = false;
    this->accepting = true;
    try
    {
        this->worker = std::thread(&AccountPresenceManager::WorkerLoop, this);
    }

    catch (...)
    {
        this->accepting = false;
        lock.unlock();
        AccountDataClient::GetInstance().ShutdownPresenceServer(this->serverInstanceId);
        this->serverInstanceId.clear();
        return false;
    }

    return true;
}

void AccountPresenceManager::Stop()
{
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (!this->accepting && !this->worker.joinable())
        {
            return;
        }

        this->accepting = false;
        this->stopping = true;
        // A single set-based update below is both faster and more reliable than
        // serializing thousands of per-account updates during server shutdown.
        this->pending.clear();
        while (!this->schedule.empty())
        {
            this->schedule.pop();
        }

    }

    this->cv.notify_all();
    if (this->worker.joinable())
    {
        this->worker.join();
    }

    if (!this->serverInstanceId.empty() &&
        !AccountDataClient::GetInstance().ShutdownPresenceServer(this->serverInstanceId))
    {
        RPG_LOG_ERROR("Database", "Failed to persist final logout presence during shutdown.");
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    this->sessionAccounts.clear();
    this->accountSessionCounts.clear();
    this->pending.clear();
    while (!this->schedule.empty())
    {
        this->schedule.pop();
    }

    this->serverInstanceId.clear();
}

bool AccountPresenceManager::Activate(const SessionPtr& session, const std::string& uid)
{
    if (!session || uid.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    if (!this->accepting || !session->IsOpen())
    {
        return false;
    }

    if (!this->sessionAccounts.emplace(session->GetId(), uid).second)
    {
        return true;
    }

    auto& count = this->accountSessionCounts[uid];
    if (++count == 1)
    {
        ScheduleLocked(uid, true);
    }

    return true;
}

void AccountPresenceManager::Deactivate(std::uint64_t sessionId)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    const auto SESSION = this->sessionAccounts.find(sessionId);
    if (SESSION == this->sessionAccounts.end())
    {
        return;
    }

    const std::string UID = SESSION->second;
    this->sessionAccounts.erase(SESSION);
    const auto ACCOUNT = this->accountSessionCounts.find(UID);
    if (ACCOUNT == this->accountSessionCounts.end())
    {
        return;
    }

    if (ACCOUNT->second > 1)
    {
        --ACCOUNT->second;
    }
    else
    {
        this->accountSessionCounts.erase(ACCOUNT);
        ScheduleLocked(UID, false);
    }

}

std::size_t AccountPresenceManager::PendingAccountCount() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->pending.size();
}

void AccountPresenceManager::ScheduleLocked(const std::string& uid, bool active)
{
    auto& state = this->pending[uid];
    state.desiredActive = active;
    ++state.generation;
    state.failedAttempts = 0;
    state.nextAttempt = std::chrono::steady_clock::now();
    this->schedule.push(ScheduledPresence{state.nextAttempt, uid, state.generation});
    this->cv.notify_one();
}

void AccountPresenceManager::WorkerLoop()
{
    using Clock = std::chrono::steady_clock;
    auto nextLeaseRenewal = Clock::now() + std::chrono::seconds(10);
    bool restoreAfterRenewFailure = false;
    for (;;)
    {
        std::string uid;
        bool active = false;
        std::uint64_t generation = 0;
        bool renewLease = false;
        {
            std::unique_lock<std::mutex> lock(this->mutex);
            auto now = Clock::now();
            while (!this->schedule.empty())
            {
                const auto STATE = this->pending.find(this->schedule.top().uid);
                if (STATE != this->pending.end() && STATE->second.generation == this->schedule.top().generation &&
                    STATE->second.nextAttempt == this->schedule.top().due)
                {
                    break;
                }

                this->schedule.pop();
            }

            auto nextWake =
                this->schedule.empty() ? nextLeaseRenewal : (std::min)(nextLeaseRenewal, this->schedule.top().due);
            if (now < nextWake)
            {
                this->cv.wait_until(lock, nextWake);
            }

            if (this->stopping)
            {
                break;
            }

            now = Clock::now();
            if (now >= nextLeaseRenewal)
            {
                renewLease = true;
                nextLeaseRenewal = now + std::chrono::seconds(10);
            }
            else
            {
                if (!this->schedule.empty() && this->schedule.top().due <= now)
                {
                    const ScheduledPresence DUE = this->schedule.top();
                    this->schedule.pop();
                    const auto STATE = this->pending.find(DUE.uid);
                    if (STATE != this->pending.end() && STATE->second.generation == DUE.generation)
                    {
                        uid = DUE.uid;
                        active = STATE->second.desiredActive;
                        generation = STATE->second.generation;
                    }

                }

            }

        }

        if (renewLease)
        {
            const auto RESULT = AccountDataClient::GetInstance().RenewPresenceServerLease(this->serverInstanceId);
            if (RESULT == PresenceLeaseRenewalResult::FAILED_VALUE)
            {
                restoreAfterRenewFailure = true;
                RPG_LOG_ERROR("Database", "Failed to renew the login presence server lease.");
            }
            else if (RESULT == PresenceLeaseRenewalResult::RECREATED || restoreAfterRenewFailure)
            {
                // A recreated-lease response may have been lost on the previous RPC.
                restoreAfterRenewFailure = false;
                std::lock_guard<std::mutex> lock(this->mutex);
                for (const auto& [activeUid, count] : this->accountSessionCounts)
                {
                    if (count != 0)
                    {
                        ScheduleLocked(activeUid, true);
                    }

                }

                RPG_LOG_WARNING("Database",
                                "Presence lease was recreated; active account owners are being restored. count="
                                    << this->accountSessionCounts.size());
            }

            continue;
        }

        if (uid.empty())
        {
            continue;
        }

        const bool UPDATED = AccountDataClient::GetInstance().UpdateLoginPresence(this->serverInstanceId, uid, active);

        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->stopping)
        {
            break;
        }

        const auto IT = this->pending.find(uid);
        if (IT == this->pending.end())
        {
            continue;
        }

        if (UPDATED && IT->second.generation == generation)
        {
            this->pending.erase(IT);
        }
        else if (!UPDATED && IT->second.generation == generation)
        {
            ++IT->second.failedAttempts;
            const std::uint32_t SHIFT = (std::min)(IT->second.failedAttempts, 6U);
            IT->second.nextAttempt = Clock::now() + std::chrono::milliseconds(100U << SHIFT);
            this->schedule.push(ScheduledPresence{IT->second.nextAttempt, uid, IT->second.generation});
            if (IT->second.failedAttempts == 1 || IT->second.failedAttempts % 10 == 0)
            {
                RPG_LOG_ERROR("Database", "Login presence update is pending retry. uid=" << uid << ", attempts="
                                                                                         << IT->second.failedAttempts);
            }

        }

    }

}

