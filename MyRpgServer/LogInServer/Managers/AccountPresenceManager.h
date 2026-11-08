#pragma once

#include "../../ServerCore/Session.h"

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <queue>
#include <vector>

class AccountPresenceManager final
{
public:
    static AccountPresenceManager& GetInstance();

    ~AccountPresenceManager()
    {
        Stop();
    }

    bool Start();
    void Stop();
    bool Activate(const SessionPtr& session, const std::string& uid);
    void Deactivate(std::uint64_t sessionId);
    std::size_t PendingAccountCount() const;

private:
    AccountPresenceManager() = default;

    struct PendingPresence
    {
        bool desiredActive = false;
        std::uint64_t generation = 0;
        std::uint32_t failedAttempts = 0;
        std::chrono::steady_clock::time_point nextAttempt{};
    };

    struct ScheduledPresence
    {
        std::chrono::steady_clock::time_point due;
        std::string uid;
        std::uint64_t generation = 0;

        bool operator>(const ScheduledPresence& other) const
        {
            return this->due > other.due;
        }

    };

    void ScheduleLocked(const std::string& uid, bool active);
    void WorkerLoop();

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<std::uint64_t, std::string> sessionAccounts;
    std::unordered_map<std::string, std::size_t> accountSessionCounts;
    std::unordered_map<std::string, PendingPresence> pending;
    std::priority_queue<ScheduledPresence, std::vector<ScheduledPresence>, std::greater<ScheduledPresence>> schedule;
    std::thread worker;
    std::string serverInstanceId;
    bool accepting = false;
    bool stopping = false;
};

