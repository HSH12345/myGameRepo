#pragma once
#include "Economy.h"
#include "../Map/MapPackage.h"

namespace shared_world::economy
{
inline void U64(Bytes& b, std::uint64_t n)
{
    binary::U32(b, static_cast<std::uint32_t>(n >> 32));
    binary::U32(b, static_cast<std::uint32_t>(n));
}

inline std::uint64_t U64(binary::Reader& r)
{
    const auto HI = r.U32(), LO = r.U32();
    return (std::uint64_t(HI) << 32) | LO;
}

inline void String(Bytes& b, const std::string& s)
{
    binary::U32(b, static_cast<std::uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

inline std::string String(binary::Reader& r, std::size_t limit)
{
    auto n = r.U32();
    if (n > limit)
    {
        throw std::invalid_argument("economy string budget");
    }

    const auto B = r.Blob(n);
    return {B.begin(), B.end()};
}

inline void WriteRequest(Bytes& b, const Request& q)
{
    if (!q.Valid())
    {
        throw std::invalid_argument("invalid economy request");
    }

    binary::U32(b, static_cast<std::uint32_t>(q.action));
    U64(b, q.revision);
    U64(b, q.fence);
    U64(b, q.catalog);
    U64(b, q.instance);
    for (auto v : {q.shop, q.offer, q.quantity, q.destination})
    {
        binary::U32(b, v);
    }

    String(b, q.operation);
}

inline Request ReadRequest(binary::Reader& r)
{
    Request q;
    q.action = static_cast<Action>(r.U32());
    q.revision = U64(r);
    q.fence = U64(r);
    q.catalog = U64(r);
    q.instance = U64(r);
    q.shop = r.U32();
    q.offer = r.U32();
    q.quantity = r.U32();
    q.destination = r.U32();
    q.operation = String(r, 32);
    if (!q.Valid())
    {
        throw std::invalid_argument("invalid economy request");
    }

    return q;
}

inline void WriteState(Bytes& b, const State& s)
{
    if (s.items.size() > BAG_CAPACITY)
    {
        throw std::invalid_argument("economy item budget");
    }

    U64(b, s.gold);
    U64(b, s.revision);
    U64(b, s.fence);
    U64(b, s.nextId);
    binary::U32(b, static_cast<std::uint32_t>(s.items.size()));
    for (const auto& i : s.items)
    {
        U64(b, i.id);
        binary::U32(b, i.type);
        binary::U32(b, i.quantity);
        b.push_back(i.bag);
        b.push_back(i.equipped);
    }

    binary::U32(b, s.skills.points);
    binary::U32(b, s.skills.spent);
    binary::U32(b, s.skills.level);
    b.insert(b.end(), s.skills.ranks.begin(), s.skills.ranks.end());
    for (auto value : s.quickSlots)
    {
        binary::U32(b, value);
    }

    for (auto value : s.potions)
    {
        binary::U32(b, value);
    }

}

inline State ReadState(binary::Reader& r)
{
    State s;
    s.gold = U64(r);
    s.revision = U64(r);
    s.fence = U64(r);
    s.nextId = U64(r);
    const auto N = r.U32();
    if (N > BAG_CAPACITY)
    {
        throw std::invalid_argument("economy item budget");
    }

    for (std::uint32_t j = 0; j < N; ++j)
    {
        Item i;
        i.id = U64(r);
        i.type = r.U32();
        i.quantity = r.U32();
        const auto SLOTS = r.Blob(2);
        i.bag = SLOTS[0];
        i.equipped = SLOTS[1];
        s.items.push_back(i);
    }

    s.skills.points = r.U32();
    s.skills.spent = r.U32();
    s.skills.level = r.U32();
    const auto RANKS = r.Blob(65);
    std::copy(RANKS.begin(), RANKS.end(), s.skills.ranks.begin());
    for (auto& value : s.quickSlots)
    {
        value = r.U32();
        if (value > 64)
        {
            throw std::invalid_argument("quick slot bounds");
        }

    }

    for (auto& value : s.potions)
    {
        value = r.U32();
        if (value > INT32_MAX)
        {
            throw std::invalid_argument("potion type bounds");
        }

    }

    if (s.skills.points > 1000000 || s.skills.spent > 1000000 || !s.skills.level || s.skills.level > 10000 ||
        std::any_of(s.skills.ranks.begin(), s.skills.ranks.end(),
                    [](auto rank)
                    {
                        return rank > 5;
                    }))

    {
        throw std::invalid_argument("skill state bounds");
    }

    return s;
}

struct RpcRequest
{
    std::uint32_t requestId = 0;
    std::string uid;
    std::uint64_t character = 0;
    Request request;

    bool Serialize(Bytes& b) const
    {
        if (!this->requestId || this->uid.empty() || this->uid.size() > 128 || !this->character ||
            !this->request.Valid())
        {
            return false;
        }

        b.clear();
        binary::U32(b, this->requestId);
        String(b, this->uid);
        U64(b, this->character);
        WriteRequest(b, this->request);
        return true;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        try
        {
            if (!data || size > 512)
            {
                return false;
            }

            Bytes b(data, data + size);
            binary::Reader r(b, b.size());
            this->requestId = r.U32();
            this->uid = String(r, 128);
            this->character = U64(r);
            this->request = ReadRequest(r);
            return r.Done() && this->requestId && this->character && !this->uid.empty();
        }

        catch (...)
        {
            return false;
        }

    }

};

struct RpcResponse
{
    std::uint32_t requestId = 0;
    Result result = Result::UNAVAILABLE;
    std::string operation;
    State state;
    std::uint64_t committedRevision = 0;

    bool Serialize(Bytes& b) const
    {
        if (!this->requestId || this->operation.size() > 32 || static_cast<unsigned>(this->result) > 20)
        {
            return false;
        }

        b.clear();
        binary::U32(b, this->requestId);
        binary::U32(b, static_cast<unsigned>(this->result));
        String(b, this->operation);
        U64(b, this->committedRevision);
        WriteState(b, this->state);
        return b.size() <= 1024;
    }

    bool Deserialize(const std::uint8_t* data, std::size_t size)
    {
        try
        {
            if (!data || size > 1024)
            {
                return false;
            }

            Bytes b(data, data + size);
            binary::Reader r(b, b.size());
            this->requestId = r.U32();
            this->result = static_cast<Result>(r.U32());
            this->operation = String(r, 32);
            this->committedRevision = U64(r);
            this->state = ReadState(r);
            return r.Done() && this->requestId && static_cast<unsigned>(this->result) <= 20;
        }

        catch (...)
        {
            return false;
        }

    }

};

}

