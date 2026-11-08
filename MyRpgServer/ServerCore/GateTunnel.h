#pragma once
#include "../Protocol/GatePackets.h"
#include "PacketRecvDispatcher.h"
#include "Server.h"
#include <unordered_map>

// Adapts transport-independent logical users to existing domain handlers.
// One receive per tunnel preserves user ordering; domain worker jobs remain async.
class GateTunnel final
{
public:
    void Configure(ServerOptions& options)
    {
        this->connected = options.onSessionConnected;
        this->disconnected = options.onSessionDisconnected;
        this->userLimits = options.sessionLimits;
        this->maxUsers = options.maxSessions;
        this->receiveBudget = std::make_shared<ReceiveBufferBudget>(options.maxBufferedReceiveBytesGlobal);
        // A physical tunnel is not one player's packet quota.
        options.sessionLimits.maxPacketsPerSecond = 100000;
        options.sessionLimits.maxReceivedBytesPerSecond = 64 * 1024 * 1024;
        options.onSessionConnected = [this](const SessionPtr& peer)
        {
            std::lock_guard lock(this->mutex);
            this->peers.emplace(peer->GetId(), Users{});
        };

        options.onSessionDisconnected = [this](const SessionPtr& peer, const std::string& reason)
        {
            Users removed;
            {
                std::lock_guard lock(this->mutex);
                auto it = this->peers.find(peer->GetId());
                if (it == this->peers.end())
                {
                    return;
                }

                removed.swap(it->second);
                this->userCount -= removed.size();
                this->peers.erase(it);
            }

            for (auto& [id, user] : removed)
            {
                End(user, reason);
            }

        };

        this->transport.RegisterHandler(RpgProtocol::PacketType::GATE_FRAME, RpgProtocol::MAX_GATE_FRAME + 9,
                                        OPEN_SESSION_STATES,
                                        [this](const SessionPtr& peer, const std::uint8_t* data, std::size_t size)
                                        {
                                            return Handle(peer, data, size);
                                        });

        this->transport.Seal();
    }

    void SetSender(PacketSender& value)
    {
        this->sender = &value;
    }

    bool Dispatch(const SessionPtr& peer, const char* data, DWORD size)
    {
        return this->transport.Dispatch(peer, data, size);
    }

    std::size_t UserCount() const
    {
        std::lock_guard lock(this->mutex);
        return this->userCount;
    }

private:
    using Users = std::unordered_map<std::uint64_t, SessionPtr>;

    bool Send(const SessionPtr& peer, RpgProtocol::GatePacket packet)
    {
        RpgProtocol::ByteBuffer bytes;
        return this->sender && packet.Serialize(bytes) &&
               this->sender->Send(peer, RpgProtocol::PacketType::GATE_FRAME, bytes);
    }

    void End(const SessionPtr& user, const std::string& reason)
    {
        // Removal from the tunnel owns the lifecycle notification. A game
        // timeout may already have closed the logical session itself.
        user->Close(reason);
        if (this->disconnected)
        {
            this->disconnected(user, reason);
        }

    }

    void Drop(const SessionPtr& peer, std::uint64_t id)
    {
        SessionPtr user;
        {
            std::lock_guard lock(this->mutex);
            auto p = this->peers.find(peer->GetId());
            if (p == this->peers.end())
            {
                return;
            }

            auto it = p->second.find(id);
            if (it == p->second.end())
            {
                return;
            }

            user = std::move(it->second);
            p->second.erase(it);
            --this->userCount;
        }

        End(user, "gate user disconnected");
    }

    bool Handle(const SessionPtr& peer, const std::uint8_t* data, std::size_t size)
    {
        using namespace RpgProtocol;
        GatePacket packet;
        if (!packet.Deserialize(data, size))
        {
            return false;
        }

        if (packet.operation == GateOperation::PING)
        {
            return Send(peer, {GateOperation::PONG, 0, {}});
        }

        if (packet.operation == GateOperation::CLOSE)
        {
            Drop(peer, packet.clientId);
            return true;
        }

        if (packet.operation == GateOperation::OPEN)
        {
            auto user = std::make_shared<Session>(this->userLimits, nullptr, this->receiveBudget);
            user->SetRemoteAddress("gate-user");
            const auto ID = packet.clientId;
            user->SetRoutedSend(
                [this, weak = std::weak_ptr<Session>(peer), ID](auto frame)
                {
                    auto target = weak.lock();
                    return target && Send(target, {GateOperation::DATA, ID, *frame});
                });

            bool accepted = false;
            {
                std::lock_guard lock(this->mutex);
                auto p = this->peers.find(peer->GetId());
                if (p != this->peers.end() && peer->IsOpen() && this->userCount < this->maxUsers)
                {
                    accepted = p->second.emplace(ID, user).second;
                    if (accepted)
                    {
                        ++this->userCount;
                    }

                }

            }

            if (!accepted)
            {
                return Send(peer, {GateOperation::CLOSE, ID, {}});
            }

            try
            {
                if (this->connected)
                {
                    this->connected(user);
                }

                // A transport failure can race publication of the logical connect.
                if (!user->IsOpen() && this->disconnected)
                {
                    this->disconnected(user, "tunnel closed during user registration");
                }

            }

            catch (...)
            {
                Drop(peer, ID);
                return Send(peer, {GateOperation::CLOSE, ID, {}});
            }

            return true;
        }

        if (packet.operation != GateOperation::DATA)
        {
            return false;
        }

        SessionPtr user;
        {
            std::lock_guard lock(this->mutex);
            auto p = this->peers.find(peer->GetId());
            if (p != this->peers.end())
            {
                auto it = p->second.find(packet.clientId);
                if (it != p->second.end())
                {
                    user = it->second;
                }

            }

        }

        bool valid = false;
        if (user && user->IsOpen())
        {
            user->TouchActivity();
            try
            {
                valid = PacketRecvDispatcher::GetInstance().Dispatch(
                    user, reinterpret_cast<const char*>(packet.frame.data()), static_cast<DWORD>(packet.frame.size()));
            }

            catch (...)
            {
            }

        }

        if (!valid)
        {
            Drop(peer, packet.clientId);
            return Send(peer, {GateOperation::CLOSE, packet.clientId, {}});
        }

        return true;
    }

    PacketRecvDispatcher transport;
    PacketSender* sender = nullptr;
    SessionLimits userLimits;
    std::shared_ptr<ReceiveBufferBudget> receiveBudget;
    std::function<void(const SessionPtr&)> connected;
    std::function<void(const SessionPtr&, const std::string&)> disconnected;
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, Users> peers;
    std::size_t userCount = 0, maxUsers = 10000;
};

