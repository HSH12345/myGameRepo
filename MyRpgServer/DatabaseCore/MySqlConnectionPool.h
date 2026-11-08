#pragma once
// Lifecycle: initialize before workers start; shut down after all leases return.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <mysql.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

class MySqlConnectionPool final
{
public:
    static MySqlConnectionPool& GetInstance();
    bool Initialize(std::string newHost, std::string newUser, std::string newPassword, std::string newDatabase,
                    unsigned int newPort, unsigned int poolSize);
    void Shutdown();
    bool IsHealthy() const;
    MYSQL* Acquire();
    void Release(MYSQL* connection, bool reusable);

private:
    MySqlConnectionPool() = default;

    ~MySqlConnectionPool()
    {
        Shutdown();
    }

    MYSQL* CreateConnection();
    mutable std::mutex mutex;
    std::condition_variable available;
    std::queue<MYSQL*> connections;
    bool stopping = true;
    unsigned int totalConnections = 0;
    unsigned int targetPoolSize = 0;
    std::string host, user, password, database;
    unsigned int port = 3307;
};

