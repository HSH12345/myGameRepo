#pragma once
#include "../ServerCore/BinaryProtocol.h"

enum class LoginLookupResult
{
    FOUND,
    NOT_FOUND,
    UNAVAILABLE,
    QUERY_ERROR
};

enum class PresenceLeaseRenewalResult
{
    RENEWED,
    RECREATED,
    FAILED_VALUE
};

namespace RpgProtocol
{
enum class AccountOperation : std::uint8_t
{
    LOOKUP_TOKEN = 1,
    INSERT_CREDENTIAL = 2,
    DELETE_INACTIVE_CREDENTIAL = 3,
    INITIALIZE_PRESENCE = 4,
    RENEW_PRESENCE = 5,
    UPDATE_PRESENCE = 6,
    SHUTDOWN_PRESENCE = 7,
    ENSURE_FIREBASE_ACCOUNT = 8
};

enum class AccountResult : std::uint8_t
{
    SUCCESS = 0,
    NOT_FOUND = 1,
    UNAVAILABLE = 2,
    QUERY_ERROR = 3,
    RECREATED = 4
};

// Domain operations only: never accept arbitrary SQL from LoginServer.
struct AccountDataRequest
{
    std::uint32_t requestId = 0;
    AccountOperation operation = AccountOperation::LOOKUP_TOKEN;
    std::string uid, token, serverInstanceId;
    bool active = false;

    bool IsValid() const
    {
        if (!this->requestId || this->uid.size() > 128 || this->token.size() > 2048 ||
            this->serverInstanceId.size() > 96)
        {
            return false;
        }

        switch (this->operation)
        {
        case AccountOperation::LOOKUP_TOKEN:
            return !this->token.empty() && this->uid.empty() && this->serverInstanceId.empty() && !this->active;
        case AccountOperation::INSERT_CREDENTIAL:
        case AccountOperation::DELETE_INACTIVE_CREDENTIAL:
            return !this->token.empty() && !this->uid.empty() && this->serverInstanceId.empty() && !this->active;
        case AccountOperation::INITIALIZE_PRESENCE:
        case AccountOperation::RENEW_PRESENCE:
        case AccountOperation::SHUTDOWN_PRESENCE:
            return !this->serverInstanceId.empty() && this->uid.empty() && this->token.empty() && !this->active;
        case AccountOperation::UPDATE_PRESENCE:
            return !this->serverInstanceId.empty() && !this->uid.empty() && this->token.empty();
        case AccountOperation::ENSURE_FIREBASE_ACCOUNT:
            if (this->uid.size() != 73 || this->uid.compare(0, 9, "naver_v1_") || !this->token.empty() ||
                !this->serverInstanceId.empty() || this->active)
            {
                return false;
            }

            for (std::size_t i = 9; i < this->uid.size(); ++i)
            {
                if (!((this->uid[i] >= '0' && this->uid[i] <= '9') || (this->uid[i] >= 'a' && this->uid[i] <= 'f')))
                {
                    return false;
                }

            }

            return true;
        default:
            return false;
        }

    }

    bool Serialize(ByteBuffer& output) const
    {
        output.clear();
        if (!IsValid())
        {
            return false;
        }

        BinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        writer.WriteUInt8(static_cast<std::uint8_t>(this->operation));
        if (!writer.WriteString(this->uid) || !writer.WriteString(this->token) ||
            !writer.WriteString(this->serverInstanceId))
        {
            return false;
        }

        writer.WriteUInt8(this->active ? 1 : 0);
        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* bytes, std::size_t size)
    {
        BinaryReader reader(bytes, size);
        std::uint8_t op = 0, flag = 0;
        if (!reader.ReadUInt32(this->requestId) || !reader.ReadUInt8(op) || !reader.ReadString(this->uid) ||
            !reader.ReadString(this->token) || !reader.ReadString(this->serverInstanceId) || !reader.ReadUInt8(flag) ||
            flag > 1 || !reader.IsAtEnd())
        {
            return false;
        }

        this->operation = static_cast<AccountOperation>(op);
        this->active = flag != 0;
        return IsValid();
    }

};

struct AccountDataResponse
{
    std::uint32_t requestId = 0;
    AccountOperation operation = AccountOperation::LOOKUP_TOKEN;
    AccountResult result = AccountResult::UNAVAILABLE;
    std::string uid; // No token echoed on internal responses.

    bool Serialize(ByteBuffer& output) const
    {
        output.clear();
        if (this->uid.size() > 128)
        {
            return false;
        }

        BinaryWriter writer;
        writer.WriteUInt32(this->requestId);
        writer.WriteUInt8(static_cast<std::uint8_t>(this->operation));
        writer.WriteUInt8(static_cast<std::uint8_t>(this->result));
        if (!writer.WriteString(this->uid))
        {
            return false;
        }

        output = writer.Data();
        return true;
    }

    bool Deserialize(const std::uint8_t* bytes, std::size_t size)
    {
        BinaryReader reader(bytes, size);
        std::uint8_t op = 0, code = 0;
        if (!reader.ReadUInt32(this->requestId) || !this->requestId || !reader.ReadUInt8(op) || op < 1 || op > 8 ||
            !reader.ReadUInt8(code) || code > 4 || !reader.ReadString(this->uid) || this->uid.size() > 128 ||
            !reader.IsAtEnd())
        {
            return false;
        }

        this->operation = static_cast<AccountOperation>(op);
        this->result = static_cast<AccountResult>(code);
        return true;
    }

};

}

