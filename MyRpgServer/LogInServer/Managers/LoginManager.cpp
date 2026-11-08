#include "LoginManager.h"
#include "SendManager.h"
#include <iostream>
#include <string>
#include "../Urls.h"
#include "../AuthenticationUrl.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "AccountDataClient.h"
#include "AccountPresenceManager.h"
#include "../Packet/PacketHaders.h"
#include "../../ServerCore/StructuredLogger.h"
#include <cstdlib>
#include <limits>
#include <algorithm>

using json = nlohmann::json;

LoginManager& LoginManager::GetInstance()
{
    static LoginManager instance;
    return instance;
}

LoginManager::~LoginManager()
{
    Stop();
}

void LoginManager::Init(unsigned int workerCount, std::uint32_t jobTimeoutMilliseconds)
{
    if (workerCount == 0 || jobTimeoutMilliseconds == 0)
    {
        return;
    }

    if (this->running.exchange(true))
    {
        return;
    }

    this->jobTimeout = std::chrono::milliseconds(jobTimeoutMilliseconds);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        this->running = false;
        RPG_LOG_CRITICAL("Auth", "curl global initialization failed.");
        return;
    }

    this->jobs.Start();
    try
    {
        this->workers.reserve(workerCount);
        for (unsigned int i = 0; i < workerCount; ++i)
        {
            this->workers.emplace_back(&LoginManager::WorkerLoop, this);
        }

    }

    catch (const std::exception& exception)
    {
        RPG_LOG_CRITICAL("Lifecycle", "Failed to create login worker pool. error=" << exception.what());
        Stop();
    }

}

void LoginManager::Stop()
{
    if (!this->running.exchange(false))
    {
        return;
    }

    this->jobs.Stop(false);
    for (auto& worker : this->workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }

    }

    this->workers.clear();
    curl_global_cleanup();
}

void LoginManager::WorkerLoop()
{
    for (;;)
    {
        Job job;
        if (!this->jobs.WaitAndPop(job))
        {
            if (!this->running)
            {
                break;
            }

            continue;
        }

        if (job.task)
        {
            if (job.shouldCancel && job.shouldCancel())
            {
                if (job.onCancelled)
                {
                    job.onCancelled();
                }

                continue;
            }

            try
            {
                job.task();
            }

            catch (const std::exception& exception)
            {
                RPG_LOG_ERROR("Auth", "Login job failed. error=" << exception.what());
            }

            catch (...)
            {
                RPG_LOG_ERROR("Auth", "Login job failed with an unknown exception.");
            }

        }

    }

}

size_t LoginManager::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    constexpr std::size_t MAX_RESPONSE_BYTES = 64 * 1024;
    if (nmemb != 0 && size > (std::numeric_limits<std::size_t>::max)() / nmemb)
    {
        return 0;
    }

    const size_t TOTAL_SIZE = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    if (response->size() + TOTAL_SIZE > MAX_RESPONSE_BYTES)
    {
        return 0;
    }

    response->append(static_cast<char*>(contents), TOTAL_SIZE);
    return TOTAL_SIZE;
}

namespace
{
struct CurlCancellationContext
{
    LoginManager* manager = nullptr;
    SessionPtr session;
    std::chrono::steady_clock::time_point deadline;
};

struct WorkerCurlHandle
{
    WorkerCurlHandle() : value(curl_easy_init())
    {
    }

    ~WorkerCurlHandle()
    {
        if (this->value)
        {
            curl_easy_cleanup(this->value);
        }

    }

    CURL* value = nullptr;
};

}

int LoginManager::CurlProgressCallback(void* clientData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    auto* context = static_cast<CurlCancellationContext*>(clientData);
    return !context || context->manager->IsCancelled(context->session, context->deadline) ? 1 : 0;
}

bool LoginManager::IsCancelled(const SessionPtr& session, Deadline deadline) const
{
    return !this->running.load(std::memory_order_acquire) || !session || !session->IsOpen() ||
           std::chrono::steady_clock::now() >= deadline;
}

bool LoginManager::FinishIfCancelled(const SessionPtr& session, Deadline deadline, std::uint32_t requestId) const
{
    if (!IsCancelled(session, deadline))
    {
        return false;
    }

    if (session && session->IsOpen())
    {
        session->FailAuthentication();
        if (this->running.load(std::memory_order_acquire))
        {
            SendManager::GetInstance().SendGuestLogin(session, requestId, RpgProtocol::PacketSendResult::SERVER_BUSY,
                                                      "", "");
        }

    }

    return true;
}

bool LoginManager::CompleteLogin(const SessionPtr& session, const std::string& uid, std::uint32_t requestId) const
{
    if (!session->CompleteAuthentication(uid))
    {
        return false;
    }

    if (AccountPresenceManager::GetInstance().Activate(session, uid))
    {
        return true;
    }

    session->FailAuthentication();
    if (session->IsOpen())
    {
        SendManager::GetInstance().SendGuestLogin(session, requestId, RpgProtocol::PacketSendResult::SERVER_BUSY, "",
                                                  "");
    }

    return false;
}

bool LoginManager::RequestGuestLoginFromWeb(const SessionPtr& session, Deadline deadline, std::string& uid,
                                            std::string& token)
{
    static const std::string BASE_URL = []
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "RPG_AUTH_GUEST_URL") == 0 && value)
        {
            std::string result(value);
            std::free(value);
            return result;
        }

        return Urls::GUEST_AUTH_ENDPOINT;
    }();

    if (!RpgAuthentication::IsSafeUrl(BASE_URL))
    {
        RPG_LOG_ERROR("Auth", "Authentication URL must use HTTPS or an exact loopback host over HTTP.");
        return false;
    }

    const std::string& url = BASE_URL;

    static thread_local WorkerCurlHandle workerCurl;
    CURL* curl = workerCurl.value;
    if (curl == nullptr)
    {
        RPG_LOG_ERROR("Auth", "CURL initialization failed.");
        return false;
    }

    curl_easy_reset(curl);

    std::string responseData;
    CurlCancellationContext cancellation{this, session, deadline};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancellation);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        RPG_LOG_ERROR("Auth", "Login web request failed: " << curl_easy_strerror(res));
        return false;
    }

    long httpStatus = 0;
    char* contentType = nullptr;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
    const bool IS_JSON_RESPONSE =
        contentType != nullptr && std::string(contentType).find("application/json") != std::string::npos;
    if (httpStatus < 200 || httpStatus >= 300)
    {
        RPG_LOG_ERROR("Auth", "Login web request returned HTTP " << httpStatus);
        return false;
    }

    if (!IS_JSON_RESPONSE)
    {
        RPG_LOG_ERROR("Auth", "Login web response is not application/json.");
        return false;
    }

    try
    {
        json jsonData = json::parse(responseData);
        uid = jsonData.at("uid").get<std::string>();
        token = jsonData.at("token").get<std::string>();
        if (uid.empty() || uid.size() > 128 || token.size() < 16 || token.size() > 2048)
        {
            RPG_LOG_ERROR("Auth", "Login response contains invalid uid or token length.");
            return false;
        }

    }

    catch (const std::exception& e)
    {
        RPG_LOG_ERROR("Auth", "Login response JSON parse failed: " << e.what());
        return false;
    }

    return true;
}

LoginLookupResult LoginManager::FindLoginInfoByToken(const std::string& token, std::string& uid, std::string& dbToken,
                                                     Deadline deadline)
{
    return AccountDataClient::GetInstance().FindValidLoginByToken(token, uid, dbToken, deadline);
}

void LoginManager::StartGuestLogin(const SessionPtr& session, const RpgProtocol::GuestLoginRequestPacket& packet)
{
    if (!session || !session->BeginAuthentication())
    {
        SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::SERVER_BUSY,
                                                  "", "");
        return;
    }

    if (packet.uid.size() > 128 || packet.token.size() > 2048)
    {
        session->FailAuthentication();
        SendManager::GetInstance().SendGuestLogin(session, packet.requestId,
                                                  RpgProtocol::PacketSendResult::INVALID_PACKET, "", "");
        return;
    }

    auto admission = AccountDataClient::GetInstance().Admission().Acquire(32 + packet.uid.size() + packet.token.size());
    if (!admission)
    {
        session->FailAuthentication();
        SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::SERVER_BUSY,
                                                  "", "");
        return;
    }

    Job job;
    const Deadline DEADLINE = std::chrono::steady_clock::now() + this->jobTimeout;
    job.shouldCancel = [this, weakSession = std::weak_ptr<Session>(session), DEADLINE]
    {
        return IsCancelled(weakSession.lock(), DEADLINE);
    };

    job.onCancelled = [session, requestId = packet.requestId]
    {
        if (!session->IsOpen())
        {
            return;
        }

        session->FailAuthentication();
        SendManager::GetInstance().SendGuestLogin(session, requestId, RpgProtocol::PacketSendResult::SERVER_BUSY, "",
                                                  "");
    };

    job.task = [this, session, packet, DEADLINE, admission]
    {
        try
        {
            ProcessGuestLogin(session, packet, DEADLINE);
        }

        catch (const std::exception& exception)
        {
            session->FailAuthentication();
            RPG_LOG_ERROR("Auth", "Guest login failed unexpectedly. error=" << exception.what());
            SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::QUERY,
                                                      "", "");
        }

        catch (...)
        {
            session->FailAuthentication();
            RPG_LOG_ERROR("Auth", "Guest login failed with an unknown exception.");
            SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::QUERY,
                                                      "", "");
        }

    };

    if (!this->jobs.Push(std::move(job)))
    {
        session->FailAuthentication();
        RPG_LOG_WARNING("Auth", "Login work queue is full.");
        SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::SERVER_BUSY,
                                                  "", "");
    }

}

void LoginManager::ProcessGuestLogin(const SessionPtr& session, const RpgProtocol::GuestLoginRequestPacket& packet,
                                     Deadline deadline)
{
    if (FinishIfCancelled(session, deadline, packet.requestId))
    {
        return;
    }

    std::string uid;
    std::string token;

    if (packet.token.empty())
    {
        // 새 게스트 로그인 요청
        if (!RequestGuestLoginFromWeb(session, deadline, uid, token))
        {
            if (FinishIfCancelled(session, deadline, packet.requestId))
            {
                return;
            }

            session->FailAuthentication();
            SendManager::GetInstance().SendGuestLogin(session, packet.requestId,
                                                      RpgProtocol::PacketSendResult::WEB_NETWORK, "", "");
            return;
        }

        // DB 저장
        if (FinishIfCancelled(session, deadline, packet.requestId))
        {
            return;
        }

        bool isQuerySuccess = AccountDataClient::GetInstance().InsertLoginInfo(uid, token, deadline);
        if (!isQuerySuccess)
        {
            session->FailAuthentication();
            RPG_LOG_ERROR("Database", "Failed to persist guest login. uid=" << uid);
            SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::QUERY,
                                                      "", "");
            return;
        }

        // Inserting the new credential is the irreversible commit point. Once it
        // succeeds, an open session receives success even if the soft job deadline
        // elapsed by a few milliseconds. If delivery cannot begin, remove the exact
        // inactive credential so an unreachable guest account is not left behind.
        if (!this->running.load(std::memory_order_acquire) || !session->IsOpen() ||
            !CompleteLogin(session, uid, packet.requestId))
        {
            if (!AccountDataClient::GetInstance().DeleteLoginInfo(uid, token))
            {
                RPG_LOG_ERROR("Database", "Failed to roll back an undeliverable guest credential. uid=" << uid);
            }

            return;
        }

        SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::SUCCESS,
                                                  uid, token);
    }
    else
    {
        // 기존 토큰으로 로그인
        std::string clientToken = packet.token;
        std::string dbToken;

        const LoginLookupResult LOOKUP = FindLoginInfoByToken(clientToken, uid, dbToken, deadline);
        if (LOOKUP != LoginLookupResult::FOUND)
        {
            session->FailAuthentication();
            const auto RESULT = LOOKUP == LoginLookupResult::NOT_FOUND ? RpgProtocol::PacketSendResult::AUTH_REJECTED
                                                                       : RpgProtocol::PacketSendResult::QUERY;
            SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RESULT, "", "");
            return;
        }

        if (FinishIfCancelled(session, deadline, packet.requestId) || !CompleteLogin(session, uid, packet.requestId))
        {
            return;
        }

        SendManager::GetInstance().SendGuestLogin(session, packet.requestId, RpgProtocol::PacketSendResult::SUCCESS,
                                                  uid, dbToken);
    }

}

RpgProtocol::PacketSendResult LoginManager::VerifyFirebaseToken(const SessionPtr& session, Deadline deadline,
                                                                const std::string& idToken, std::string& uid)
{
    using R = RpgProtocol::PacketSendResult;
    uid.clear();
    static const std::string URL = []
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "RPG_AUTH_FIREBASE_VERIFY_URL") == 0 && value)
        {
            std::string endpoint(value);
            std::free(value);
            return endpoint;
        }

        return std::string("http://127.0.0.1:3000/auth/firebase/verify");
    }();

    if (!RpgAuthentication::IsSafeUrl(URL))
    {
        return R::WEB_NETWORK;
    }

    const auto REMAINING =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (REMAINING <= 0 || IsCancelled(session, deadline))
    {
        return R::SERVER_BUSY;
    }

    static thread_local WorkerCurlHandle workerCurl;
    auto* curl = workerCurl.value;
    if (!curl)
    {
        return R::WEB_NETWORK;
    }

    curl_easy_reset(curl);
    const std::string BODY = json{{"idToken", idToken}}.dump();
    std::string response;
    CurlCancellationContext cancellation{this, session, deadline};
    auto* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!headers)
    {
        return R::WEB_NETWORK;
    }

    curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, BODY.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(BODY.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>((std::min)(REMAINING, 10000LL)));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancellation);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    const auto PERFORMED = curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
    curl_slist_free_all(headers);
    if (PERFORMED != CURLE_OK)
    {
        return R::WEB_NETWORK;
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status == 401 || status == 403 || status == 400)
    {
        return R::AUTH_REJECTED;
    }

    if (status == 503 || status == 504 || status == 429)
    {
        return R::SERVER_BUSY;
    }

    if (status != 200)
    {
        return R::WEB_NETWORK;
    }

    char* contentType = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
    if (!contentType || std::string(contentType).find("application/json") == std::string::npos)
    {
        return R::WEB_NETWORK;
    }

    try
    {
        const auto VERIFIED = json::parse(response);
        if (!VERIFIED.contains("uid") || !VERIFIED["uid"].is_string())
        {
            return R::AUTH_REJECTED;
        }

        uid = VERIFIED["uid"].get<std::string>();
        return RpgProtocol::IsNaverFirebaseUid(uid) ? R::SUCCESS : R::AUTH_REJECTED;
    }

    catch (...)
    {
        return R::WEB_NETWORK;
    }

}

void LoginManager::StartFirebaseLogin(const SessionPtr& session, const RpgProtocol::FirebaseLoginRequestPacket& packet)
{
    using R = RpgProtocol::PacketSendResult;
    if (!session || !packet.IsValid())
    {
        return;
    }

    if (!session->BeginAuthentication())
    {
        SendManager::GetInstance().SendFirebaseLogin(session, packet.requestId, R::SERVER_BUSY);
        return;
    }

    auto admission = AccountDataClient::GetInstance().Admission().Acquire(32 + packet.idToken.size());
    if (!admission)
    {
        session->FailAuthentication();
        SendManager::GetInstance().SendFirebaseLogin(session, packet.requestId, R::SERVER_BUSY);
        return;
    }

    const auto DEADLINE = std::chrono::steady_clock::now() + this->jobTimeout;
    Job job;
    job.shouldCancel = [this, weak = std::weak_ptr<Session>(session), DEADLINE]
    {
        return IsCancelled(weak.lock(), DEADLINE);
    };

    job.onCancelled = [session, id = packet.requestId]
    {
        session->FailAuthentication();
        if (session->IsOpen())
        {
            SendManager::GetInstance().SendFirebaseLogin(session, id, R::SERVER_BUSY);
        }

    };

    job.task = [this, session, packet, DEADLINE, admission]
    {
        try
        {
            ProcessFirebaseLogin(session, packet, DEADLINE);
        }

        catch (...)
        {
            session->FailAuthentication();
            if (session->IsOpen())
            {
                SendManager::GetInstance().SendFirebaseLogin(session, packet.requestId, R::WEB_NETWORK);
            }

        }

    };

    if (!this->jobs.Push(std::move(job)))
    {
        session->FailAuthentication();
        SendManager::GetInstance().SendFirebaseLogin(session, packet.requestId, R::SERVER_BUSY);
    }

}

void LoginManager::ProcessFirebaseLogin(const SessionPtr& session,
                                        const RpgProtocol::FirebaseLoginRequestPacket& packet, Deadline deadline)
{
    using R = RpgProtocol::PacketSendResult;
    const auto FAIL = [&](R result)
    {
        session->FailAuthentication();
        if (session->IsOpen())
        {
            SendManager::GetInstance().SendFirebaseLogin(session, packet.requestId, result);
        }

    };

    if (IsCancelled(session, deadline))
    {
        FAIL(R::SERVER_BUSY);
        return;
    }

    std::string uid;
    const auto VERIFIED = VerifyFirebaseToken(session, deadline, packet.idToken, uid);
    if (IsCancelled(session, deadline))
    {
        FAIL(R::SERVER_BUSY);
        return;
    }

    if (VERIFIED != R::SUCCESS)
    {
        FAIL(VERIFIED);
        return;
    }

    if (!AccountDataClient::GetInstance().EnsureFirebaseAccount(uid, deadline))
    {
        FAIL(R::QUERY);
        return;
    }

    if (IsCancelled(session, deadline))
    {
        FAIL(R::SERVER_BUSY);
        return;
    }

    if (!session->CompleteAuthentication(uid) || !AccountPresenceManager::GetInstance().Activate(session, uid))
    {
        FAIL(R::SERVER_BUSY);
        return;
    }

    SendManager::GetInstance().SendFirebaseLogin(session, packet.requestId, R::SUCCESS, uid);
}

