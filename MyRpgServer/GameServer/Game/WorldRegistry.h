#pragma once
#include "GameMapContent.h"
#include "../../ServerCore/WorldDataClient.h"
#include "../../ServerCore/StructuredLogger.h"
#include <bcrypt.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#pragma comment(lib, "bcrypt.lib")

// Only immutable startup identities cross into this registry thread.
class WorldRegistry final
{
public:
    ~WorldRegistry()
    {
        Stop();
    }

    bool Start(unsigned short dataPort, unsigned short gamePort, const GameMapCatalog& maps)
    {
        if (!this->client.Configure(dataPort))
        {
            return false;
        }

        this->request.operation = RpgProtocol::WorldDataOperation::PUBLISH;
        this->request.gamePort = gamePort;
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&this->request.boot), sizeof(this->request.boot),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
            !this->request.boot)
        {
            return false;
        }

        for (const auto& [instance, content] : maps)
        {
            this->request.maps.push_back(
                {instance, content->identity.id, content->identity.version, content->identity.checksum});
        }

        if (this->client.Execute(this->request) != RpgProtocol::WorldDataResult::SUCCESS)
        {
            return false;
        }

        this->stopping = false;
        try
        {
            this->worker = std::thread(
                [this]
                {
                    std::unique_lock lock(this->mutex);
                    while (!this->wake.wait_for(lock, std::chrono::seconds(5),
                                                [this]
                                                {
                                                    return this->stopping;
                                                }))

                    {
                        lock.unlock();
                        if (this->client.Execute(this->request) != RpgProtocol::WorldDataResult::SUCCESS)
                        {
                            RPG_LOG_WARNING("WorldData",
                                            "World registry renewal failed; new entry expires after the lease.");
                        }

                        lock.lock();
                    }

                });

        }

        catch (...)
        {
            Release();
            return false;
        }

        return true;
    }

    void Stop()
    {
        {
            std::lock_guard lock(this->mutex);
            this->stopping = true;
        }

        this->wake.notify_all();
        if (this->worker.joinable())
        {
            this->worker.join();
            Release();
        }

    }

private:
    void Release()
    {
        auto release = this->request;
        release.operation = RpgProtocol::WorldDataOperation::RELEASE;
        release.maps.clear();
        this->client.Execute(release);
    }

    WorldDataClient client;
    RpgProtocol::WorldDataRequest request;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = true;
    std::thread worker;
};

