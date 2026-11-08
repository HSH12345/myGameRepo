#pragma once

#include <mysql.h>

#include <condition_variable>
#include <atomic>
#include <initializer_list>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "../../Protocol/AccountDataPackets.h"

class AccountDatabaseManager;

// Owns one pooled connection and one prepared statement. Construction performs
// acquire/init/prepare; destruction always closes the statement and returns the
// connection, including every early-return and exception path.
class PreparedStatementLease final
{
public:
    PreparedStatementLease(AccountDatabaseManager& newOwner, const char* sql);
    PreparedStatementLease(AccountDatabaseManager& newOwner, MYSQL* borrowedConnection, const char* sql);
    ~PreparedStatementLease();
    PreparedStatementLease(const PreparedStatementLease&) = delete;
    PreparedStatementLease& operator=(const PreparedStatementLease&) = delete;

    explicit operator bool() const
    {
        return this->statement != nullptr;
    }

    MYSQL_STMT* Get() const
    {
        return this->statement;
    }

    MYSQL* GetConnection() const
    {
        return this->connection;
    }

    bool BindStrings(std::initializer_list<const std::string*> values);
    bool Execute();

    void InvalidateConnection()
    {
        this->reusableConnection = false;
    }

private:
    AccountDatabaseManager& owner;
    MYSQL* connection = nullptr;
    MYSQL_STMT* statement = nullptr;
    std::vector<MYSQL_BIND> parameterBindings;
    std::vector<unsigned long> parameterLengths;
    bool reusableConnection = true;
    bool ownsConnection = true;
};

class DatabaseTransaction final
{
public:
    explicit DatabaseTransaction(AccountDatabaseManager& newOwner);
    ~DatabaseTransaction();
    DatabaseTransaction(const DatabaseTransaction&) = delete;
    DatabaseTransaction& operator=(const DatabaseTransaction&) = delete;

    explicit operator bool() const
    {
        return this->connection != nullptr;
    }

    MYSQL* Get() const
    {
        return this->connection;
    }

    bool Commit();

private:
    AccountDatabaseManager& owner;
    MYSQL* connection = nullptr;
    bool committed = false;
};

class AccountDatabaseManager
{
public:
    static AccountDatabaseManager& GetInstance()
    {
        static AccountDatabaseManager instance;
        return instance;
    }

    bool Init(const char* host, const char* user, const char* password, const char* db, unsigned int port = 3307,
              int poolSize = 10);
    MYSQL* AcquireConnection();
    void ReleaseConnection(MYSQL* conn, bool reusable = true);
    void Cleanup();
    bool InsertLoginInfo(const std::string& uid, const std::string& token);
    bool EnsureFirebaseAccount(const std::string& uid);
    bool DeleteLoginInfo(const std::string& uid, const std::string& token);
    LoginLookupResult FindValidLoginByToken(const std::string& token, std::string& uid, std::string& tokenResult);
    bool InitializePresenceServer(const std::string& serverInstanceId);
    PresenceLeaseRenewalResult RenewPresenceServerLease(const std::string& serverInstanceId);
    bool UpdateLoginPresence(const std::string& serverInstanceId, const std::string& uid, bool active);
    bool ShutdownPresenceServer(const std::string& serverInstanceId);
    bool IsHealthy() const;

private:
    friend class PreparedStatementLease;
    friend class DatabaseTransaction;
    AccountDatabaseManager() = default;

    ~AccountDatabaseManager()
    {
        Cleanup();
    }

    AccountDatabaseManager(const AccountDatabaseManager&) = delete;
    AccountDatabaseManager& operator=(const AccountDatabaseManager&) = delete;
    MYSQL* CreateConnection();
    bool ExecuteStrings(const char* sql, std::initializer_list<const std::string*> values);
    bool ExecuteStrings(MYSQL* connection, const char* sql, std::initializer_list<const std::string*> values);
    bool ReconcilePresence(MYSQL* connection, const std::string& uid);
    bool CleanupExpiredPresence();
    void RecordDatabaseSuccess();
    bool EnsureSchemaVersion();

    std::queue<MYSQL*> connectionPool;
    mutable std::mutex poolMutex;
    std::condition_variable poolCv;
    bool stopping = false;
    std::string configuredHost;
    std::string configuredUser;
    std::string configuredPassword;
    std::string configuredDatabase;
    unsigned int configuredPort = 0;
    int targetPoolSize = 0;
    int totalConnections = 0;
    bool reconnectInProgress = false;
    std::int64_t reconnectBlockedUntilMilliseconds = 0;
    std::atomic<std::int64_t> lastSuccessfulOperationMilliseconds{0};
    std::atomic<std::uint32_t> consecutiveFailures{0};
};

