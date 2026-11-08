#include "MySqlConnectionPool.h"
#include "../ServerCore/StructuredLogger.h"

#include <chrono>

MySqlConnectionPool& MySqlConnectionPool::GetInstance()
{
    static MySqlConnectionPool instance;
    return instance;
}

bool MySqlConnectionPool::Initialize(std::string newHost, std::string newUser, std::string newPassword,
                                     std::string newDatabase, unsigned int newPort, unsigned int poolSize)
{
    Shutdown();
    this->host = std::move(newHost);
    this->user = std::move(newUser);
    this->password = std::move(newPassword);
    this->database = std::move(newDatabase);
    this->port = newPort;
    this->targetPoolSize = poolSize;
    this->stopping = false;
    for (unsigned int index = 0; index < this->targetPoolSize; ++index)
    {
        MYSQL* connection = CreateConnection();
        if (!connection)
        {
            Shutdown();
            return false;
        }

        std::lock_guard<std::mutex> lock(this->mutex);
        this->connections.push(connection);
        ++this->totalConnections;
    }

    RPG_LOG_INFO("Database", "MySQL connection pool initialized. size=" << this->targetPoolSize);
    return true;
}

MYSQL* MySqlConnectionPool::CreateConnection()
{
    MYSQL* connection = mysql_init(nullptr);
    if (!connection)
    {
        return nullptr;
    }

    unsigned int connectTimeout = 3, ioTimeout = 5;
    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);
    mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &ioTimeout);
    mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &ioTimeout);
    if (!mysql_real_connect(connection, this->host.c_str(), this->user.c_str(), this->password.c_str(),
                            this->database.c_str(), this->port, nullptr, 0) ||
        mysql_set_character_set(connection, "utf8mb4") != 0)
    {
        RPG_LOG_ERROR("Database", "MySQL connection failed. code=" << mysql_errno(connection));
        mysql_close(connection);
        return nullptr;
    }

    return connection;
}

MYSQL* MySqlConnectionPool::Acquire()
{
    std::unique_lock<std::mutex> lock(this->mutex);
    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;)
    {
        if (this->stopping)
        {
            return nullptr;
        }

        if (!this->connections.empty())
        {
            MYSQL* connection = this->connections.front();
            this->connections.pop();
            return connection;
        }

        if (this->totalConnections < this->targetPoolSize)
        {
            ++this->totalConnections;
            lock.unlock();
            MYSQL* connection = CreateConnection();
            lock.lock();
            if (connection && !this->stopping)
            {
                return connection;
            }

            if (connection)
            {
                mysql_close(connection);
            }

            --this->totalConnections;
            this->available.notify_one();
            return nullptr;
        }

        if (this->available.wait_until(lock, DEADLINE) == std::cv_status::timeout)
        {
            return nullptr;
        }

    }

}

void MySqlConnectionPool::Release(MYSQL* connection, bool reusable)
{
    if (!connection)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->stopping || !reusable)
    {
        mysql_close(connection);
        --this->totalConnections;
    }
    else
    {
        this->connections.push(connection);
    }

    this->available.notify_one();
}

void MySqlConnectionPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->stopping = true;
    while (!this->connections.empty())
    {
        mysql_close(this->connections.front());
        this->connections.pop();
        --this->totalConnections;
    }

    this->available.notify_all();
}

bool MySqlConnectionPool::IsHealthy() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return !this->stopping && this->totalConnections != 0;
}

