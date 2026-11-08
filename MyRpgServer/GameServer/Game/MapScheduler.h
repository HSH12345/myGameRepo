#pragma once
#include "GameRuntime.h"
#include "../../ServerCore/DependencyAdmission.h"
#include "GameDataGateway.h"
#include "GridPathfinding.h"
#include <vector>

// Static map affinity: a map never runs concurrently or migrates between workers.
// Networking may only provide server-authorized map IDs, never raw client input.
class MapScheduler final
{
public:
    MapScheduler(GameRuntimeOptions options, unsigned int count, unsigned short newAccountPort = 9100,
                 unsigned short newCharacterPort = 9300)
        : accountPort(newAccountPort), characterPort(newCharacterPort)
    {
        if (count == 0 || count > 64)
        {
            throw std::invalid_argument("Invalid map worker count");
        }

        this->economyEnabled = bool(options.economyCatalog);
        options.maxQueuedCommands = (std::max)(std::size_t{2}, options.maxQueuedCommands / count);
        this->sharedInstance = options.sharedInstance;
        this->dungeonInstances = options.dungeonInstances;
        this->mapCatalog = options.maps;
        if (!this->dungeonInstances.empty() && this->mapCatalog)
        {
            options.defaultDungeonMap = this->mapCatalog->at(this->dungeonInstances.front())->identity.id;
        }

        options.worldClosed = [this](std::uint64_t id)
        {
            GameCommand command;
            command.sessionId = id;
            command.payload = SessionDisconnectedCommand{"world closed"};
            NotifyDisconnected(std::move(command));
        };

        if (!this->dungeonInstances.empty())
        {
            options.transferWorld = [this](auto source, auto transfer, auto detach)
            {
                return Transfer(source, std::move(transfer), std::move(detach));
            };

        }

        if (!this->dungeonInstances.empty())
        {
            options.transferParty = [this](auto source, auto transfers, auto detach)
            {
                return TransferGroup(source, std::move(transfers), std::move(detach));
            };

        }

        options.submitEconomy = [this](std::uint64_t map, const shared_world::economy::RpcRequest& q, auto apply)
        {
            RpgProtocol::ByteBuffer bytes;
            if (!q.Serialize(bytes))
            {
                return false;
            }

            return SubmitData(map, GameDataGateway::Target::CHARACTER,
                              RpgProtocol::PacketType::INTERNAL_ECONOMY_REQUEST,
                              RpgProtocol::PacketType::INTERNAL_ECONOMY_RESPONSE, std::move(bytes),
                              q.request.Mutation() || q.request.action == shared_world::economy::Action::ACQUIRE,
                              [q, apply](bool received, RpgProtocol::ByteBuffer reply)
                              {
                                  shared_world::economy::RpcResponse value;
                                  value.requestId = q.requestId;
                                  value.operation = q.request.operation;
                                  value.result = shared_world::economy::Result::UNKNOWN_OUTCOME;
                                  if (received)
                                  {
                                      shared_world::economy::RpcResponse decoded;
                                      if (decoded.Deserialize(reply.data(), reply.size()) &&
                                          decoded.requestId == q.requestId && decoded.operation == q.request.operation)
                                      {
                                          value = std::move(decoded);
                                      }

                                  }

                                  apply(value);
                              });

        };

        for (unsigned int i = 0; i < count; ++i)
        {
            this->workers.push_back(std::make_unique<GameRuntime>(options));
        }

    }

    bool Start()
    {
        for (auto& worker : this->workers)
        {
            if (!worker->Start())
            {
                Stop();
                return false;
            }

        }

        if (!this->compute.Start(2) || !this->data.Start(this->accountPort, this->characterPort, this->economyEnabled))
        {
            Stop();
            return false;
        }

        return true;
    }

    ~MapScheduler()
    {
        Stop();
    }

    void Stop()
    {
        this->data.Stop();
        this->compute.Stop(true);
        for (auto& worker : this->workers)
        {
            worker->Stop();
        }

    }

    bool TryEnqueueGameplay(GameCommand command)
    {
        if (this->dependency && !this->dependency->TryAdmitUser())
        {
            return false;
        }

        {
            std::lock_guard lock(this->routeMutex);
            const auto ROUTE = this->routes.find(command.sessionId);
            if (ROUTE == this->routes.end())
            {
                return false;
            }

            command.mapInstanceId = ROUTE->second;
            std::uint64_t claim = 0;
            if (const auto* world = std::get_if<WorldMessageCommand>(&command.payload);
                world && world->message.kind == shared_world::net::Kind::AUTHORIZE)
            {
                claim = world->message.character;
                if (this->characterOwners.contains(claim) || this->sessionCharacters.contains(command.sessionId))
                {
                    return false;
                }

                this->characterOwners.emplace(claim, command.sessionId);
                try
                {
                    this->sessionCharacters.emplace(command.sessionId, claim);
                }

                catch (...)
                {
                    this->characterOwners.erase(claim);
                    throw;
                }

            }

            const auto SESSION_ID = command.sessionId;
            try
            {
                if (For(command.mapInstanceId).TryEnqueueGameplay(std::move(command)))
                {
                    return true;
                }

            }

            catch (...)
            {
                if (claim)
                {
                    this->characterOwners.erase(claim);
                    this->sessionCharacters.erase(SESSION_ID);
                }

                throw;
            }

            if (claim)
            {
                this->characterOwners.erase(claim);
                this->sessionCharacters.erase(SESSION_ID);
            }

            return false;
        }

    }

    bool TryNotifyConnected(GameCommand command)
    {
        std::lock_guard lock(this->routeMutex);
        const auto ID = command.sessionId;
        if (!this->routes.emplace(ID, command.mapInstanceId).second)
        {
            return false;
        }

        try
        {
            this->homes.emplace(ID, command.mapInstanceId);
            this->routeParties.emplace(ID, 0);
            if (For(command.mapInstanceId).TryNotifyConnected(std::move(command)))
            {
                return true;
            }

        }

        catch (...)
        {
            this->routes.erase(ID);
            this->routeParties.erase(ID);
            this->homes.erase(ID);
            throw;
        }

        this->routes.erase(ID);
        this->routeParties.erase(ID);
        this->homes.erase(ID);
        return false;
    }

    bool NotifyDisconnected(GameCommand command)
    {
        std::lock_guard lock(this->routeMutex);
        const auto ROUTE = this->routes.find(command.sessionId);
        if (ROUTE == this->routes.end())
        {
            return false;
        }

        // Lifecycle reservation remains on the original worker. The current
        // world's tick also removes closed sessions, even during handoff.
        command.mapInstanceId = this->homes.at(command.sessionId);
        const bool ACCEPTED = For(command.mapInstanceId).NotifyDisconnected(std::move(command));
        if (ACCEPTED)
        {
            auto claimed = this->sessionCharacters.find(ROUTE->first);
            if (claimed != this->sessionCharacters.end())
            {
                this->characterOwners.erase(claimed->second);
                this->sessionCharacters.erase(claimed);
            }

            this->routeParties.erase(ROUTE->first);
            this->homes.erase(ROUTE->first);
            this->routes.erase(ROUTE);
        }

        return ACCEPTED;
    }

    // Computation returns a value-owning callback. Only that callback runs on the
    // map worker; it must validate entity generation/request ID before applying.
    bool SubmitComputation(std::uint64_t map, std::function<std::function<void()>()> work)
    {
        if (!work)
        {
            return false;
        }

        auto slot = For(map).ReserveCompletion();
        if (!slot)
        {
            return false;
        }

        try
        {
            Job job;
            job.task = [slot, work = std::move(work)]
            {
                try
                {
                    slot->Complete(work());
                }

                catch (...)
                {
                    slot->Complete({});
                }

            };

            if (this->compute.Submit(std::move(job)))
            {
                return true;
            }

        }

        catch (...)
        {
            slot->Complete({});
            throw;
        }

        slot->Complete({});
        return false;
    }

    bool SubmitData(std::uint64_t map, GameDataGateway::Target target, RpgProtocol::PacketType requestType,
                    RpgProtocol::PacketType responseType, RpgProtocol::ByteBuffer bytes, bool important,
                    std::function<void(bool, RpgProtocol::ByteBuffer)> apply)
    {
        if (!apply)
        {
            return false;
        }

        auto slot = For(map).ReserveCompletion();
        if (!slot)
        {
            return false;
        }

        try
        {
            if (this->data.Submit(target, requestType, responseType, std::move(bytes), important,
                                  [slot, apply = std::move(apply)](bool received, const RpgProtocol::ByteBuffer& reply)
                                  {
                                      try
                                      {
                                          slot->Complete(
                                              [apply, received, reply]
                                              {
                                                  apply(received, reply);
                                              });

                                      }

                                      catch (...)
                                      {
                                          slot->Complete({});
                                          return false;
                                      }

                                      return received;
                                  }))

            {
                return true;
            }

        }

        catch (...)
        {
            slot->Complete({});
            throw;
        }

        slot->Complete({});
        return false;
    }

    DependencyAdmission::Snapshot DataPressure()
    {
        return this->data.Admission().Inspect();
    }

    // Submit/cancel for one entity must run on its owning map worker. CPU tasks
    // only read the snapshot; they never capture mutable entity state.
    bool SubmitPath(std::uint64_t map, std::uint64_t entity, std::shared_ptr<const NavigationGrid> grid,
                    std::uint32_t start, std::uint32_t goal, std::uint32_t nodeBudget,
                    std::function<void(GridPathResult)> apply)
    {
        if (!grid || !apply || !nodeBudget || nodeBudget > 262144)
        {
            return false;
        }

        std::uint64_t ticket;
        {
            std::lock_guard lock(this->pathMutex);
            if (this->paths.size() >= 512 && !this->paths.contains(entity))
            {
                return false;
            }

            ticket = ++this->nextPath;
            this->paths[entity] = {map, ticket};
        }

        auto current = [this, map, entity, ticket]
        {
            std::lock_guard lock(this->pathMutex);
            const auto IT = this->paths.find(entity);
            return IT != this->paths.end() && IT->second.first == map && IT->second.second == ticket;
        };

        auto release = [this, entity, ticket]
        {
            std::lock_guard lock(this->pathMutex);
            const auto IT = this->paths.find(entity);
            if (IT != this->paths.end() && IT->second.second == ticket)
            {
                this->paths.erase(IT);
            }

        };

        try
        {
            const bool ACCEPTED =
                SubmitComputation(map,
                                  [grid, start, goal, nodeBudget, apply, current, release]() -> std::function<void()>
                                  {
                                      try
                                      {
                                          auto result = FindGridPath(*grid, start, goal, nodeBudget,
                                                                     [current]
                                                                     {
                                                                         return !current();
                                                                     });

                                          return [apply, current, release, result = std::move(result)]() mutable
                                          {
                                              if (!current())
                                              {
                                                  return;
                                              }

                                              release();
                                              apply(std::move(result));
                                          };

                                      }

                                      catch (...)
                                      {
                                          release();
                                          throw;
                                      }

                                  });

            if (!ACCEPTED)
            {
                release();
            }

            return ACCEPTED;
        }

        catch (...)
        {
            release();
            throw;
        }

    }

    // Entity IDs must include spawn generation (or never be reused).
    // Invoke on the owning map before destruction/transfer; stale results are discarded.
    void CancelPaths(std::uint64_t entity)
    {
        std::lock_guard lock(this->pathMutex);
        this->paths.erase(entity);
    }

    void SetDependency(DependencyAdmission& value)
    {
        this->dependency = &value;
    }

    bool AllowsUsers()
    {
        return !this->dependency || this->dependency->AllowsUsers();
    }

    std::size_t WorkerIndex(std::uint64_t map) const
    {
        return static_cast<std::size_t>(map % this->workers.size());
    }

    std::size_t WorkerCount() const
    {
        return this->workers.size();
    }

    std::size_t GetQueueDepth() const
    {
        return Sum(&GameRuntime::GetQueueDepth);
    }

    std::size_t GetQueueCapacity() const
    {
        return Sum(&GameRuntime::GetQueueCapacity);
    }

    std::size_t GetLifecycleQueueDepth() const
    {
        return Sum(&GameRuntime::GetLifecycleQueueDepth);
    }

    std::size_t GetTrackedLifecycleSessionCount() const
    {
        return Sum(&GameRuntime::GetTrackedLifecycleSessionCount);
    }

    std::uint64_t GetDroppedCommandCount() const
    {
        return Sum(&GameRuntime::GetDroppedCommandCount);
    }

    std::uint64_t GetProcessedCommandCount() const
    {
        return Sum(&GameRuntime::GetProcessedCommandCount);
    }

    std::uint64_t GetProcessedLifecycleCount() const
    {
        return Sum(&GameRuntime::GetProcessedLifecycleCount);
    }

    std::uint64_t GetTickOverrunCount() const
    {
        return Sum(&GameRuntime::GetTickOverrunCount);
    }

    std::uint64_t GetMaxTickMicroseconds() const
    {
        return Maximum(&GameRuntime::GetMaxTickMicroseconds);
    }

    std::uint64_t GetMaxCommandLatencyMicroseconds() const
    {
        return Maximum(&GameRuntime::GetMaxCommandLatencyMicroseconds);
    }

    // Conservative worst-worker bucket, not a global percentile.
    std::uint64_t GetTickLatencyPercentile(std::uint32_t p) const
    {
        std::uint64_t result = 0;
        for (auto& w : this->workers)
        {
            result = (std::max)(result, w->GetTickLatencyPercentile(p));
        }

        return result;
    }

    std::uint64_t GetCommandLatencyPercentile(std::uint32_t p) const
    {
        std::uint64_t result = 0;
        for (auto& w : this->workers)
        {
            result = (std::max)(result, w->GetCommandLatencyPercentile(p));
        }

        return result;
    }

private:
    bool Transfer(std::uint64_t source, WorldTransfer transfer, std::function<void()> detach)
    {
        const auto SESSION = transfer.session.lock();
        if (!SESSION || !SESSION->IsOpen())
        {
            return false;
        }

        std::lock_guard lock(this->routeMutex);
        const auto ROUTE = this->routes.find(SESSION->GetId());
        if (ROUTE == this->routes.end() || ROUTE->second != source)
        {
            return false;
        }

        if (transfer.exitReceipt.run &&
            (!transfer.exitReceipt.Valid() || transfer.exitReceipt.sourceInstance != source ||
             source == this->sharedInstance || transfer.targetMap))
        {
            return false;
        }

        std::uint64_t destination = this->sharedInstance;
        if (transfer.targetMap && source != this->sharedInstance)
        {
            return false;
        }

        if (source == this->sharedInstance)
        {
            destination = 0;
            for (auto candidate : this->dungeonInstances)
            {
                if (transfer.targetMap && (!this->mapCatalog || !this->mapCatalog->contains(candidate) ||
                                           this->mapCatalog->at(candidate)->identity.id != transfer.targetMap))
                {
                    continue;
                }

                bool occupied = false;
                for (const auto& entry : this->routes)
                {
                    if (entry.second == candidate)
                    {
                        occupied = true;
                        break;
                    }

                }

                if (!occupied)
                {
                    destination = candidate;
                    break;
                }

            }

        }

        if (!destination)
        {
            return false;
        }

        if (destination == this->sharedInstance)
        {
            std::size_t admitted = 0;
            for (const auto& entry : this->routes)
            {
                if (entry.second == destination)
                {
                    ++admitted;
                }

            }

            if (admitted >= 32)
            {
                return false; // Reserve the town's logical capacity before detaching the source.
            }

        }

        auto* target = &For(destination);
        // Construct before detach: allocation failure leaves the source intact.
        std::function<void()> publish = [this, target, destination, SESSION, transfer = std::move(transfer)]() mutable
        {
            try
            {
                target->AdmitTransfer(destination, std::move(transfer));
            }

            catch (...)
            {
                SESSION->Close("transfer publication failed");
                GameCommand command;
                command.sessionId = SESSION->GetId();
                command.payload = SessionDisconnectedCommand{"transfer failed"};
                NotifyDisconnected(std::move(command));
            }

        };

        auto slot = target->ReserveCompletion();
        if (!slot)
        {
            return false;
        }

        try
        {
            detach();
        }

        catch (...)
        {
            slot->Complete({});
            SESSION->Close("transfer detach failed");
            return false;
        }

        this->routeParties.at(SESSION->GetId()) = 0;
        ROUTE->second = destination;
        slot->Complete(std::move(publish));
        return true;
    }

    bool TransferGroup(std::uint64_t source, std::vector<WorldTransfer> transfers, std::function<void()> detach)
    {
        using namespace shared_world;
        if (source != this->sharedInstance || transfers.empty() || transfers.size() > party::MAX_MEMBERS)
        {
            return false;
        }

        std::lock_guard lock(this->routeMutex);
        const auto PARTY_ID = transfers.front().partyId;
        const auto TARGET_MAP = transfers.front().targetMap;
        std::vector<SessionPtr> sessions;
        sessions.reserve(transfers.size());
        std::unordered_set<std::uint64_t> unique;
        for (const auto& transfer : transfers)
        {
            const auto SESSION = transfer.session.lock();
            if (!SESSION || !SESSION->IsOpen() || !PARTY_ID || !TARGET_MAP || transfer.partyId != PARTY_ID ||
                transfer.targetMap != TARGET_MAP || !transfer.stats.Valid() || !transfer.stats.CanDie() ||
                transfer.stats.IsDead() || !unique.insert(SESSION->GetId()).second ||
                !this->routes.contains(SESSION->GetId()) || this->routes.at(SESSION->GetId()) != source)
            {
                return false;
            }

            sessions.push_back(SESSION);
        }

        std::uint64_t destination = 0;
        for (auto candidate : this->dungeonInstances)
        {
            if (!this->mapCatalog || !this->mapCatalog->contains(candidate) ||
                this->mapCatalog->at(candidate)->identity.id != TARGET_MAP)
            {
                continue;
            }

            unsigned admitted = 0;
            bool legacy = false;
            std::unordered_set<std::uint64_t> parties;
            for (const auto& [id, route] : this->routes)
            {
                if (route == candidate)
                {
                    ++admitted;
                    const auto PARTY = this->routeParties.at(id);
                    legacy = legacy || !PARTY;
                    parties.insert(PARTY);
                }

            }

            if (!legacy && admitted + transfers.size() <= party::MAX_PLAYERS && parties.size() < party::MAX_PARTIES)
            {
                destination = candidate;
                break;
            }

        }

        if (!destination)
        {
            return false;
        }

        auto* target = &For(destination);
        // One reserved owner completion publishes the whole group without an intervening map tick.
        std::function<void()> publish =
            [this, target, destination, sessions, transfers = std::move(transfers)]() mutable
        {
            try
            {
                for (auto& transfer : transfers)
                {
                    target->AdmitTransfer(destination, std::move(transfer));
                }

            }

            catch (...)
            {
                for (const auto& session : sessions)
                {
                    session->Close("party transfer publication failed");
                    GameCommand command;
                    command.sessionId = session->GetId();
                    command.payload = SessionDisconnectedCommand{"party transfer failed"};
                    NotifyDisconnected(std::move(command));
                }

            }

        };

        auto slot = target->ReserveCompletion();
        if (!slot)
        {
            return false;
        }

        try
        {
            detach();
        }

        catch (...)
        {
            slot->Complete({});
            for (const auto& session : sessions)
            {
                session->Close("party transfer detach failed");
            }

            // Source removal may have partially run. Close the entire group and let lifecycle
            // cleanup finish; never report a reversible reservation failure to the source.
            return true;
        }

        for (const auto& session : sessions)
        {
            this->routes.at(session->GetId()) = destination;
            this->routeParties.at(session->GetId()) = PARTY_ID;
        }

        slot->Complete(std::move(publish));
        return true;
    }

    GameRuntime& For(std::uint64_t map)
    {
        return *this->workers[WorkerIndex(map)];
    }

    template <class T> T Sum(T (GameRuntime::*get)() const) const
    {
        T result = 0;
        for (auto& w : this->workers)
        {
            result += (w.get()->*get)();
        }

        return result;
    }

    template <class T> T Maximum(T (GameRuntime::*get)() const) const
    {
        T result = 0;
        for (auto& w : this->workers)
        {
            result = (std::max)(result, (w.get()->*get)());
        }

        return result;
    }

    std::vector<std::unique_ptr<GameRuntime>> workers;
    const unsigned short accountPort, characterPort;
    std::mutex routeMutex;
    std::unordered_map<std::uint64_t, std::uint64_t> routes;
    std::unordered_map<std::uint64_t, std::uint64_t> homes, routeParties;
    std::unordered_map<std::uint64_t, std::uint64_t> characterOwners, sessionCharacters;
    std::uint64_t sharedInstance = 0;
    std::vector<std::uint64_t> dungeonInstances;
    std::shared_ptr<const GameMapCatalog> mapCatalog;
    std::mutex pathMutex;
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> paths;
    std::uint64_t nextPath = 0;
    GameDataGateway data;
    bool economyEnabled = false;
    BoundedWorkerPool compute{128};
    DependencyAdmission* dependency = &this->data.Admission();
};

