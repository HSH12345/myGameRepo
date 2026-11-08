#include "AccountDatabaseManager.h"
#include "../../ServerCore/StructuredLogger.h"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdlib>

PreparedStatementLease::PreparedStatementLease(AccountDatabaseManager& newOwner, const char* sql) : owner(newOwner)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        this->connection = this->owner.AcquireConnection();
        if (!this->connection)
        {
            return;
        }

        this->statement = mysql_stmt_init(this->connection);
        if (this->statement &&
            mysql_stmt_prepare(this->statement, sql, static_cast<unsigned long>(std::strlen(sql))) == 0)
        {
            return;
        }

        if (this->statement)
        {
            if (attempt == 1)
            {
                RPG_LOG_ERROR("Database", "Prepared statement creation failed: " << mysql_stmt_error(statement));
            }

            mysql_stmt_close(this->statement);
            this->statement = nullptr;
        }
        else if (attempt == 1)
        {
            RPG_LOG_ERROR("Database", "mysql_stmt_init failed.");
        }

        this->owner.ReleaseConnection(this->connection, false);
        this->connection = nullptr;
    }

}

PreparedStatementLease::PreparedStatementLease(AccountDatabaseManager& newOwner, MYSQL* borrowedConnection,
                                               const char* sql)
    : owner(newOwner), connection(borrowedConnection), ownsConnection(false)
{
    if (!this->connection)
    {
        return;
    }

    this->statement = mysql_stmt_init(this->connection);
    if (this->statement && mysql_stmt_prepare(this->statement, sql, static_cast<unsigned long>(std::strlen(sql))) == 0)
    {
        return;
    }

    if (this->statement)
    {
        RPG_LOG_ERROR("Database", "Prepared statement creation failed: " << mysql_stmt_error(statement));
        mysql_stmt_close(this->statement);
        this->statement = nullptr;
    }

    this->reusableConnection = false;
}

PreparedStatementLease::~PreparedStatementLease()
{
    if (this->statement)
    {
        mysql_stmt_close(this->statement);
    }

    if (this->connection && this->ownsConnection)
    {
        this->owner.ReleaseConnection(this->connection, this->reusableConnection);
    }

}

DatabaseTransaction::DatabaseTransaction(AccountDatabaseManager& newOwner) : owner(newOwner)
{
    this->connection = this->owner.AcquireConnection();
    if (this->connection && mysql_autocommit(this->connection, 0) != 0)
    {
        RPG_LOG_ERROR("Database", "Failed to begin transaction: " << mysql_error(connection));
        this->owner.ReleaseConnection(this->connection, false);
        this->connection = nullptr;
    }

}

DatabaseTransaction::~DatabaseTransaction()
{
    if (!this->connection)
    {
        return;
    }

    bool reusable = true;
    if (!this->committed && mysql_rollback(this->connection) != 0)
    {
        reusable = false;
    }

    if (mysql_autocommit(this->connection, 1) != 0)
    {
        reusable = false;
    }

    this->owner.ReleaseConnection(this->connection, reusable);
}

bool DatabaseTransaction::Commit()
{
    if (!this->connection || this->committed)
    {
        return false;
    }

    if (mysql_commit(this->connection) != 0)
    {
        RPG_LOG_ERROR("Database", "Transaction commit failed: " << mysql_error(this->connection));
        return false;
    }

    this->committed = true;
    this->owner.RecordDatabaseSuccess();
    return true;
}

bool PreparedStatementLease::BindStrings(std::initializer_list<const std::string*> values)
{
    if (!this->statement)
    {
        return false;
    }

    this->parameterBindings.assign(values.size(), MYSQL_BIND{});
    this->parameterLengths.resize(values.size());
    std::size_t index = 0;
    for (const std::string* value : values)
    {
        if (!value)
        {
            return false;
        }

        this->parameterLengths[index] = static_cast<unsigned long>(value->size());
        auto& binding = this->parameterBindings[index];
        binding.buffer_type = MYSQL_TYPE_STRING;
        binding.buffer = const_cast<char*>(value->data());
        binding.buffer_length = this->parameterLengths[index];
        binding.length = &this->parameterLengths[index];
        ++index;
    }

    if (mysql_stmt_bind_param(this->statement, this->parameterBindings.data()) == 0)
    {
        return true;
    }

    this->reusableConnection = false;
    RPG_LOG_ERROR("Database", "Prepared parameter binding failed: " << mysql_stmt_error(this->statement));
    return false;
}

bool PreparedStatementLease::Execute()
{
    if (this->statement && mysql_stmt_execute(this->statement) == 0)
    {
        this->owner.RecordDatabaseSuccess();
        return true;
    }

    this->reusableConnection = false;
    if (this->statement)
    {
        RPG_LOG_ERROR("Database", "Prepared statement execution failed: " << mysql_stmt_error(this->statement));
    }

    return false;
}

bool AccountDatabaseManager::Init(const char* host, const char* user, const char* password, const char* db,
                                  unsigned int port, int poolSize)
{
    Cleanup();
    {
        std::lock_guard<std::mutex> lock(this->poolMutex);
        this->stopping = false;
        this->configuredHost = host;
        this->configuredUser = user;
        this->configuredPassword = password;
        this->configuredDatabase = db;
        this->configuredPort = port;
        this->targetPoolSize = (std::max)(1, poolSize);
        this->totalConnections = 0;
        this->reconnectInProgress = false;
        this->reconnectBlockedUntilMilliseconds = 0;
    }

    for (int i = 0; i < this->targetPoolSize; ++i)
    {
        MYSQL* conn = CreateConnection();
        if (!conn)
        {
            Cleanup();
            return false;
        }

        std::lock_guard<std::mutex> lock(this->poolMutex);
        this->connectionPool.push(conn);
        ++this->totalConnections;
    }

    if (!EnsureSchemaVersion())
    {
        RPG_LOG_CRITICAL("Database", "Database schema is incomplete or incompatible; apply migrations through 007.");
        Cleanup();
        return false;
    }

    RPG_LOG_INFO("Database", "MySQL connection pool initialized. size=" << this->targetPoolSize);
    RecordDatabaseSuccess();
    return true;
}

bool AccountDatabaseManager::EnsureSchemaVersion()
{
    MYSQL* connection = AcquireConnection();
    if (!connection)
    {
        return false;
    }

    auto finish = [this, connection](bool reusable)
    {
        ReleaseConnection(connection, reusable);
        return reusable;
    };

    const char* validation = "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND ("
                             "(table_name='login_infos' AND column_name IN "
                             "('uid','token_hash','is_active','create_at','login_at','login_out','expires_at')) OR "
                             "(table_name='login_server_instances' AND column_name='lease_expires_at') OR "
                             "(table_name='login_presence_owners' AND column_name IN ('server_instance_id','uid')))";
    if (mysql_query(connection, validation) != 0)
    {
        return finish(false);
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (!result)
    {
        return finish(false);
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    const bool COMPATIBLE = row && row[0] && std::strtoul(row[0], nullptr, 10) == 10;
    mysql_free_result(result);
    if (!COMPATIBLE)
    {
        finish(true);
        return false;
    }

    const char* createVersionTable = "CREATE TABLE IF NOT EXISTS schema_migrations("
                                     "version INT NOT NULL PRIMARY KEY, checksum CHAR(64) NOT NULL, applied_at "
                                     "DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB";
    if (mysql_query(connection, createVersionTable) != 0)
    {
        return finish(false);
    }

    const char* recordBaseline =
        "INSERT IGNORE INTO schema_migrations(version, checksum) VALUES(7, SHA2('007_schema_version_baseline',256))";
    if (mysql_query(connection, recordBaseline) != 0)
    {
        return finish(false);
    }

    if (mysql_query(connection,
                    "SELECT checksum=SHA2('007_schema_version_baseline',256) FROM schema_migrations WHERE version=7") !=
        0)
    {
        return finish(false);
    }

    result = mysql_store_result(connection);
    if (!result)
    {
        return finish(false);
    }

    row = mysql_fetch_row(result);
    const bool CHECKSUM_MATCHES = row && row[0] && row[0][0] == '1';
    mysql_free_result(result);
    if (!CHECKSUM_MATCHES)
    {
        RPG_LOG_CRITICAL("Database", "Schema migration checksum mismatch for version 7.");
        finish(true);
        return false;
    }

    return finish(true);
}

MYSQL* AccountDatabaseManager::CreateConnection()
{
    MYSQL* conn = mysql_init(nullptr);
    if (!conn)
    {
        return nullptr;
    }

    unsigned int connectTimeoutSeconds = 3;
    unsigned int ioTimeoutSeconds = 5;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSeconds);
    mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &ioTimeoutSeconds);
    mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &ioTimeoutSeconds);
    if (!mysql_real_connect(conn, this->configuredHost.c_str(), this->configuredUser.c_str(),
                            this->configuredPassword.c_str(), this->configuredDatabase.c_str(), this->configuredPort,
                            nullptr, 0))
    {
        RPG_LOG_ERROR("Database", "mysql_real_connect failed: " << mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }

    if (mysql_set_character_set(conn, "utf8mb4") != 0)
    {
        RPG_LOG_ERROR("Database", "Failed to configure utf8mb4: " << mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }

    return conn;
}

MYSQL* AccountDatabaseManager::AcquireConnection()
{
    struct ThreadContext
    {
        bool ready = mysql_thread_init() == 0;

        ~ThreadContext()
        {
            if (this->ready)
            {
                mysql_thread_end();
            }

        }

    };

    static thread_local ThreadContext context;
    if (!context.ready)
    {
        return nullptr;
    }

    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < DEADLINE)
    {
        MYSQL* conn = nullptr;
        bool reservedConnectionSlot = false;
        {
            std::unique_lock<std::mutex> lock(this->poolMutex);
            if (this->stopping)
            {
                return nullptr;
            }

            if (!this->connectionPool.empty())
            {
                conn = this->connectionPool.front();
                this->connectionPool.pop();
            }
            else if (this->totalConnections < this->targetPoolSize)
            {
                const auto NOW_MILLISECONDS = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now().time_since_epoch())
                                                  .count();
                if (this->reconnectInProgress)
                {
                    return nullptr;
                }

                if (NOW_MILLISECONDS < this->reconnectBlockedUntilMilliseconds)
                {
                    return nullptr;
                }

                ++this->totalConnections;
                this->reconnectInProgress = true;
                reservedConnectionSlot = true;
            }
            else
            {
                this->poolCv.wait_until(lock, DEADLINE);
                continue;
            }

        }

        if (reservedConnectionSlot)
        {
            conn = CreateConnection();
            if (!conn)
            {
                {
                    std::lock_guard<std::mutex> lock(this->poolMutex);
                    --this->totalConnections;
                    this->reconnectInProgress = false;
                    const auto NOW_MILLISECONDS = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now().time_since_epoch())
                                                      .count();
                    const std::uint32_t FAILURES =
                        this->consecutiveFailures.fetch_add(1, std::memory_order_relaxed) + 1;
                    const std::uint32_t SHIFT = (std::min)(FAILURES, 6U);
                    this->reconnectBlockedUntilMilliseconds = NOW_MILLISECONDS + (100LL << SHIFT);
                }

                this->poolCv.notify_all();
                return nullptr;
            }

            {
                std::lock_guard<std::mutex> lock(this->poolMutex);
                this->reconnectInProgress = false;
                this->reconnectBlockedUntilMilliseconds = 0;
            }

            this->poolCv.notify_all();
            RecordDatabaseSuccess();
            return conn;
        }

        return conn;
    }

    RPG_LOG_WARNING("Database", "Timed out acquiring or recreating a database connection.");
    return nullptr;
}

void AccountDatabaseManager::ReleaseConnection(MYSQL* conn, bool reusable)
{
    if (!conn)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(this->poolMutex);
        if (this->stopping || !reusable)
        {
            mysql_close(conn);
            --this->totalConnections;
            if (!this->stopping)
            {
                this->consecutiveFailures.fetch_add(1, std::memory_order_relaxed);
            }

        }
        else
        {
            this->connectionPool.push(conn);
        }

    }

    this->poolCv.notify_one();
}

void AccountDatabaseManager::Cleanup()
{
    std::lock_guard<std::mutex> lock(this->poolMutex);
    this->stopping = true;
    while (!this->connectionPool.empty())
    {
        MYSQL* conn = this->connectionPool.front();
        this->connectionPool.pop();
        if (conn)
        {
            mysql_close(conn);
            --this->totalConnections;
        }

    }

    this->poolCv.notify_all();
}

bool AccountDatabaseManager::InsertLoginInfo(const std::string& uid, const std::string& token)
{
    const char* sql = "INSERT INTO login_infos(uid, token_hash, is_active, create_at, login_at, login_out, expires_at) "
                      "VALUES(?, UNHEX(SHA2(?, 256)), FALSE, NOW(), NULL, NULL, DATE_ADD(NOW(), INTERVAL 30 DAY))";
    PreparedStatementLease statement(*this, sql);
    if (!statement)
    {
        return false;
    }

    return statement.BindStrings({&uid, &token}) && statement.Execute();
}

bool AccountDatabaseManager::EnsureFirebaseAccount(const std::string& uid)
{
    RpgProtocol::AccountDataRequest validation;
    validation.requestId = 1;
    validation.operation = RpgProtocol::AccountOperation::ENSURE_FIREBASE_ACCOUNT;
    validation.uid = uid;
    if (!validation.IsValid())
    {
        return false;
    }

    // login_infos also anchors presence. Firebase identities have no guest
    // credential: the random marker is never returned and is already expired.
    // Never store the ID token or change an existing account's credential.
    return ExecuteStrings("INSERT INTO login_infos(uid,token_hash,is_active,create_at,expires_at) "
                          "VALUES(?,RANDOM_BYTES(32),FALSE,NOW(),NOW()) ON DUPLICATE KEY UPDATE uid=VALUES(uid)",
                          {&uid});
}

bool AccountDatabaseManager::DeleteLoginInfo(const std::string& uid, const std::string& token)
{
    return ExecuteStrings("DELETE FROM login_infos WHERE uid=? AND token_hash=UNHEX(SHA2(?, 256)) AND is_active=FALSE",
                          {&uid, &token});
}

LoginLookupResult AccountDatabaseManager::FindValidLoginByToken(const std::string& token, std::string& uid,
                                                                std::string& tokenResult)
{
    const char* sql = "SELECT uid FROM login_infos WHERE token_hash = UNHEX(SHA2(?, 256)) "
                      "AND expires_at > NOW() LIMIT 1";
    PreparedStatementLease statement(*this, sql);
    if (!statement)
    {
        return statement.GetConnection() ? LoginLookupResult::QUERY_ERROR : LoginLookupResult::UNAVAILABLE;
    }

    MYSQL_STMT* stmt = statement.Get();
    if (!statement.BindStrings({&token}) || !statement.Execute())
    {
        return LoginLookupResult::QUERY_ERROR;
    }

    char uidBuffer[129] = {};
    unsigned long uidOutLength = 0;
    MYSQL_BIND result = {};
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = uidBuffer;
    result.buffer_length = sizeof(uidBuffer) - 1;
    result.length = &uidOutLength;
    if (mysql_stmt_bind_result(stmt, &result) != 0 || mysql_stmt_store_result(stmt) != 0)
    {
        statement.InvalidateConnection();
        RPG_LOG_ERROR("Database", "Prepared result binding failed: " << mysql_stmt_error(stmt));
        return LoginLookupResult::QUERY_ERROR;
    }

    const int FETCH_RESULT = mysql_stmt_fetch(stmt);
    LoginLookupResult lookupResult = LoginLookupResult::NOT_FOUND;
    if (FETCH_RESULT == 0)
    {
        uid.assign(uidBuffer, uidOutLength);
        tokenResult = token;
        lookupResult = LoginLookupResult::FOUND;
    }
    else if (FETCH_RESULT != MYSQL_NO_DATA)
    {
        statement.InvalidateConnection();
        RPG_LOG_ERROR("Database", "Prepared fetch failed: " << mysql_stmt_error(stmt));
        lookupResult = LoginLookupResult::QUERY_ERROR;
    }

    mysql_stmt_free_result(stmt);
    return lookupResult;
}

bool AccountDatabaseManager::ExecuteStrings(const char* sql, std::initializer_list<const std::string*> values)
{
    PreparedStatementLease statement(*this, sql);
    return statement && statement.BindStrings(values) && statement.Execute();
}

bool AccountDatabaseManager::ExecuteStrings(MYSQL* connection, const char* sql,
                                            std::initializer_list<const std::string*> values)
{
    PreparedStatementLease statement(*this, connection, sql);
    return statement && statement.BindStrings(values) && statement.Execute();
}

bool AccountDatabaseManager::ReconcilePresence(MYSQL* connection, const std::string& uid)
{
    return ExecuteStrings(connection,
                          "UPDATE login_infos li SET "
                          "login_at=IF(li.is_active=FALSE AND EXISTS(SELECT 1 FROM login_presence_owners po "
                          "JOIN login_server_instances si ON si.server_instance_id=po.server_instance_id "
                          "WHERE po.uid=li.uid AND si.lease_expires_at>NOW()), NOW(), li.login_at), "
                          "login_out=IF(EXISTS(SELECT 1 FROM login_presence_owners po "
                          "JOIN login_server_instances si ON si.server_instance_id=po.server_instance_id "
                          "WHERE po.uid=li.uid AND si.lease_expires_at>NOW()), NULL, "
                          "IF(li.is_active=TRUE, NOW(), li.login_out)), "
                          "is_active=EXISTS(SELECT 1 FROM login_presence_owners po "
                          "JOIN login_server_instances si ON si.server_instance_id=po.server_instance_id "
                          "WHERE po.uid=li.uid AND si.lease_expires_at>NOW()) WHERE li.uid=?",
                          {&uid});
}

bool AccountDatabaseManager::InitializePresenceServer(const std::string& serverInstanceId)
{
    if (!ExecuteStrings("INSERT INTO login_server_instances(server_instance_id, lease_expires_at) "
                        "VALUES(?, DATE_ADD(NOW(), INTERVAL 30 SECOND)) "
                        "ON DUPLICATE KEY UPDATE lease_expires_at=VALUES(lease_expires_at)",
                        {&serverInstanceId}))
    {
        return false;
    }

    return CleanupExpiredPresence();
}

PresenceLeaseRenewalResult AccountDatabaseManager::RenewPresenceServerLease(const std::string& serverInstanceId)
{
    bool recreated = false;
    {
        PreparedStatementLease statement(*this,
                                         "INSERT INTO login_server_instances(server_instance_id, lease_expires_at) "
                                         "VALUES(?, DATE_ADD(NOW(), INTERVAL 30 SECOND)) "
                                         "ON DUPLICATE KEY UPDATE lease_expires_at=VALUES(lease_expires_at)");
        if (!statement || !statement.BindStrings({&serverInstanceId}) || !statement.Execute())
        {
            return PresenceLeaseRenewalResult::FAILED_VALUE;
        }

        recreated = mysql_stmt_affected_rows(statement.Get()) == 1;
    }

    if (!CleanupExpiredPresence())
    {
        return PresenceLeaseRenewalResult::FAILED_VALUE;
    }

    return recreated ? PresenceLeaseRenewalResult::RECREATED : PresenceLeaseRenewalResult::RENEWED;
}

bool AccountDatabaseManager::UpdateLoginPresence(const std::string& serverInstanceId, const std::string& uid,
                                                 bool active)
{
    DatabaseTransaction transaction(*this);
    if (!transaction)
    {
        return false;
    }

    const bool OWNER_UPDATED =
        active ? ExecuteStrings(transaction.Get(),
                                "INSERT IGNORE INTO login_presence_owners(server_instance_id, uid) VALUES(?, ?)",
                                {&serverInstanceId, &uid})
               : ExecuteStrings(transaction.Get(),
                                "DELETE FROM login_presence_owners WHERE server_instance_id=? AND uid=?",
                                {&serverInstanceId, &uid});
    return OWNER_UPDATED && ReconcilePresence(transaction.Get(), uid) && transaction.Commit();
}

bool AccountDatabaseManager::ShutdownPresenceServer(const std::string& serverInstanceId)
{
    DatabaseTransaction transaction(*this);
    if (!transaction ||
        !ExecuteStrings(transaction.Get(),
                        "UPDATE login_infos li JOIN login_presence_owners own "
                        "ON own.uid=li.uid AND own.server_instance_id=? SET li.is_active=FALSE, "
                        "li.login_out=IF(li.is_active=TRUE, NOW(), li.login_out) "
                        "WHERE NOT EXISTS(SELECT 1 FROM login_presence_owners other "
                        "JOIN login_server_instances live ON live.server_instance_id=other.server_instance_id "
                        "WHERE other.uid=li.uid AND other.server_instance_id<>? AND live.lease_expires_at>NOW())",
                        {&serverInstanceId, &serverInstanceId}))
    {
        return false;
    }

    return ExecuteStrings(transaction.Get(), "DELETE FROM login_server_instances WHERE server_instance_id=?",
                          {&serverInstanceId}) &&
           transaction.Commit();
}

bool AccountDatabaseManager::CleanupExpiredPresence()
{
    DatabaseTransaction transaction(*this);
    if (!transaction)
    {
        return false;
    }

    PreparedStatementLease deactivate(
        *this, transaction.Get(),
        "UPDATE login_infos li JOIN (SELECT DISTINCT po.uid FROM login_presence_owners po "
        "JOIN login_server_instances expired ON expired.server_instance_id=po.server_instance_id "
        "WHERE expired.lease_expires_at<=NOW()) stale ON stale.uid=li.uid "
        "SET li.is_active=FALSE, li.login_out=IF(li.is_active=TRUE, NOW(), li.login_out) "
        "WHERE NOT EXISTS(SELECT 1 FROM login_presence_owners other "
        "JOIN login_server_instances live ON live.server_instance_id=other.server_instance_id "
        "WHERE other.uid=li.uid AND live.lease_expires_at>NOW())");
    if (!deactivate || !deactivate.Execute())
    {
        return false;
    }

    PreparedStatementLease cleanup(*this, transaction.Get(),
                                   "DELETE FROM login_server_instances WHERE lease_expires_at<=NOW()");
    return cleanup && cleanup.Execute() && transaction.Commit();
}

void AccountDatabaseManager::RecordDatabaseSuccess()
{
    const auto NOW =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    this->lastSuccessfulOperationMilliseconds.store(NOW, std::memory_order_release);
    this->consecutiveFailures.store(0, std::memory_order_release);
}

bool AccountDatabaseManager::IsHealthy() const
{
    std::lock_guard<std::mutex> lock(this->poolMutex);
    const auto NOW =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    return !this->stopping && this->totalConnections > 0 &&
           this->consecutiveFailures.load(std::memory_order_acquire) < 3 &&
           NOW - this->lastSuccessfulOperationMilliseconds.load(std::memory_order_acquire) < 60'000;
}

