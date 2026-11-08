#pragma once
#include "../../ServerCore/Session.h"
#include "../Packet/PacketHaders.h"
#include "../../ServerCore/JobQueue.h"
#include "AccountDataClient.h"
#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

class LoginManager
{
public:
    static LoginManager& GetInstance();
    void Init(unsigned int workerCount, std::uint32_t jobTimeoutMilliseconds);
    void Stop();

    bool IsRunning() const
    {
        return this->running.load();
    }

    void StartGuestLogin(const SessionPtr& session, const RpgProtocol::GuestLoginRequestPacket& packet);
    void StartFirebaseLogin(const SessionPtr& session, const RpgProtocol::FirebaseLoginRequestPacket& packet);

private:
    LoginManager() : jobs(1024)
    {
    }

    ~LoginManager();
    void WorkerLoop();
    using Deadline = std::chrono::steady_clock::time_point;
    void ProcessGuestLogin(const SessionPtr& session, const RpgProtocol::GuestLoginRequestPacket& packet,
                           Deadline deadline);
    void ProcessFirebaseLogin(const SessionPtr& session, const RpgProtocol::FirebaseLoginRequestPacket& packet,
                              Deadline deadline);
    RpgProtocol::PacketSendResult VerifyFirebaseToken(const SessionPtr& session, Deadline deadline,
                                                      const std::string& idToken, std::string& uid);
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static int CurlProgressCallback(void* clientData, curl_off_t, curl_off_t, curl_off_t, curl_off_t);
    bool RequestGuestLoginFromWeb(const SessionPtr& session, Deadline deadline, std::string& uid, std::string& token);
    bool IsCancelled(const SessionPtr& session, Deadline deadline) const;
    bool FinishIfCancelled(const SessionPtr& session, Deadline deadline, std::uint32_t requestId) const;
    bool CompleteLogin(const SessionPtr& session, const std::string& uid, std::uint32_t requestId) const;
    LoginLookupResult FindLoginInfoByToken(const std::string& token, std::string& uid, std::string& dbToken,
                                           Deadline deadline);
    JobQueue jobs;
    std::vector<std::thread> workers;
    std::atomic<bool> running{false};
    std::chrono::milliseconds jobTimeout{15'000};
};

