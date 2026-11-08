#pragma once
// Reusable MySQL statement and transaction ownership, separate from SQL migrations.
#include "MySqlConnectionPool.h"
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace MySql
{
// Connections are pooled across workers, so mysql_init() may have run on a
// different thread. Initialize and release C-client thread-local resources.
inline void EnsureThread()
{
    struct ThreadState
    {
        ThreadState()
        {
            if (mysql_thread_init() != 0)
            {
                throw std::runtime_error("MySQL thread initialization failed");
            }

        }

        ~ThreadState()
        {
            mysql_thread_end();
        }

    };

    static thread_local ThreadState state;
}

struct Error : std::runtime_error
{
    explicit Error(unsigned int number) : std::runtime_error("MySQL operation failed"), code(number)
    {
    }

    unsigned int code;
};

class Connection final
{
public:
    explicit Connection(MySqlConnectionPool& source = MySqlConnectionPool::GetInstance())
        : pool(source), value((EnsureThread(), this->pool.Acquire()))
    {
        if (!this->value)
        {
            throw Error(2006);
        }

    }

    ~Connection()
    {
        this->pool.Release(this->value, this->reusable);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    MYSQL* Get() const
    {
        return this->value;
    }

    void Invalidate()
    {
        this->reusable = false;
    }

    void Check(unsigned int code)
    {
        if (code >= 2000)
        {
            Invalidate();
        }

        throw Error(code);
    }

private:
    MySqlConnectionPool& pool;
    MYSQL* value;
    bool reusable = true;
};

class Statement final
{
public:
    Statement(Connection& owner, const char* sql) : connection(owner), value(mysql_stmt_init(owner.Get()))
    {
        if (!this->value)
        {
            this->connection.Check(mysql_errno(owner.Get()));
        }

        if (mysql_stmt_prepare(this->value, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
        {
            const auto ERROR_VALUE = mysql_stmt_errno(this->value);
            mysql_stmt_close(this->value);
            this->value = nullptr;
            this->connection.Check(ERROR_VALUE);
        }

    }

    ~Statement()
    {
        if (this->value)
        {
            mysql_stmt_close(this->value);
        }

    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    MYSQL_STMT* Get() const
    {
        return this->value;
    }

    void Execute(std::initializer_list<std::string> values = {})
    {
        if (mysql_stmt_param_count(this->value) != values.size())
        {
            throw std::invalid_argument("Prepared statement parameter count mismatch");
        }

        this->parameters.assign(values);
        this->bindings.assign(this->parameters.size(), MYSQL_BIND{});
        this->lengths.resize(this->parameters.size());
        for (std::size_t index = 0; index < this->parameters.size(); ++index)
        {
            this->lengths[index] = static_cast<unsigned long>(this->parameters[index].size());
            this->bindings[index].buffer_type = MYSQL_TYPE_BLOB;
            this->bindings[index].buffer = this->parameters[index].data();
            this->bindings[index].buffer_length = this->lengths[index];
            this->bindings[index].length = &this->lengths[index];
        }

        if ((!this->bindings.empty() && mysql_stmt_bind_param(this->value, this->bindings.data()) != 0) ||
            mysql_stmt_execute(this->value) != 0)
        {
            this->connection.Check(mysql_stmt_errno(this->value));
        }

    }

    void BindResult(MYSQL_BIND* output)
    {
        if (mysql_stmt_bind_result(this->value, output) != 0 || mysql_stmt_store_result(this->value) != 0)
        {
            this->connection.Check(mysql_stmt_errno(this->value));
        }

    }

    bool Fetch()
    {
        const int RESULT = mysql_stmt_fetch(this->value);
        if (RESULT == MYSQL_NO_DATA)
        {
            return false;
        }

        if (RESULT == MYSQL_DATA_TRUNCATED)
        {
            throw Error(1406);
        }

        if (RESULT != 0)
        {
            this->connection.Check(mysql_stmt_errno(this->value));
        }

        return true;
    }

private:
    Connection& connection;
    MYSQL_STMT* value;
    std::vector<std::string> parameters;
    std::vector<MYSQL_BIND> bindings;
    std::vector<unsigned long> lengths;
};

inline void Execute(Connection& connection, const char* sql, std::initializer_list<std::string> values = {})
{
    Statement statement(connection, sql);
    statement.Execute(values);
}

inline unsigned long long Scalar(Connection& connection, const char* sql,
                                 std::initializer_list<std::string> values = {})
{
    Statement statement(connection, sql);
    statement.Execute(values);
    unsigned long long result = 0;
    MYSQL_BIND output = {};
    output.buffer_type = MYSQL_TYPE_LONGLONG;
    output.buffer = &result;
    output.is_unsigned = true;
    statement.BindResult(&output);
    if (!statement.Fetch())
    {
        throw Error(1329);
    }

    return result;
}

class Transaction final
{
public:
    explicit Transaction(Connection& owner) : connection(owner)
    {
        if (mysql_autocommit(this->connection.Get(), 0) != 0)
        {
            this->connection.Check(mysql_errno(this->connection.Get()));
        }

    }

    ~Transaction()
    {
        if (!this->committed && mysql_rollback(this->connection.Get()) != 0)
        {
            this->connection.Invalidate();
        }

        if (mysql_autocommit(this->connection.Get(), 1) != 0)
        {
            this->connection.Invalidate();
        }

    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    bool Commit()
    {
        if (mysql_commit(this->connection.Get()) != 0)
        {
            this->connection.Invalidate();
            return false;
        }

        this->committed = true;
        return true;
    }

private:
    Connection& connection;
    bool committed = false;
};

}

