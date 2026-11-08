#pragma once

#include "GameCommand.h"
#include "../../SharedWorld/Dungeon/DungeonMaze.h"
#include "GameMapContent.h"
#include "WorldTransfer.h"
#include "EconomyAccess.h"
#include <deque>
#include <functional>

#include <atomic>
#include <cstdint>
#include <thread>

// Each map instance is mutated only by its scheduler-assigned owner worker.
class GameWorld final
{
    friend struct PartyDungeonTestAccess;
    friend struct CombatOwnerTestAccess;
    friend struct SkillOwnerTestAccess;
    friend struct DungeonMazeTestAccess;
    friend struct PotionOwnerTestAccess;

public:
    explicit GameWorld(std::shared_ptr<const GameMapContent> content = {}, std::uint64_t instance = 0,
                       std::function<bool(const SessionPtr&, const shared_world::net::Message&)> sender = {},
                       TransferWorld transfer = {}, unsigned readyTimeout = 120,
                       std::function<void(std::uint64_t)> closed = {},
                       std::shared_ptr<const shared_world::economy::Catalog> economy = {}, SubmitEconomy submit = {},
                       TransferParty partyTransfer = {}, std::uint32_t matchTarget = 0)
        : mapContent(std::move(content)), instanceId(instance), sendWorld(std::move(sender)),
          transferWorld(std::move(transfer)), readyTimeoutSeconds(readyTimeout), worldClosed(std::move(closed))
    {
        this->transferParty = std::move(partyTransfer);
        this->defaultDungeonMap = matchTarget;
        this->economyCatalog = std::move(economy);
        this->submitEconomy = std::move(submit);
        this->players.reserve(32);
        this->playerPool.reserve(32);
        this->projectiles.reserve(shared_world::combat::MAX_PROJECTILES);
        if (this->mapContent)
        {
            this->residency = std::make_unique<shared_world::ChunkResidency>(*this->mapContent);
        }

        if (this->mapContent && this->mapContent->exitRule)
        {
            this->exitRandom.seed(
                std::random_device{}() ^ this->instanceId ^
                static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        }

        // Fixed NPC slots: owner-only vitals, no idle tick or per-frame allocation.
        if (this->mapContent && this->mapContent->npcs)
        {
            for (const auto& placement : this->mapContent->npcs->placements)
            {
                if (placement.map == this->mapContent->identity.id)
                {
                    // Megaspikan belongs to the v2 maze encounter, never the legacy open test map.
                    if (placement.npc == 2201 && !IsMaze())
                    {
                        continue;
                    }

                    const auto& definition = this->mapContent->npcs->definitions.at(placement.npc);
                    shared_world::net::Message m;
                    m.kind = shared_world::net::Kind::NPC_SPAWN;
                    m.entity = placement.Entity();
                    m.npcIndex = definition.index;
                    m.appearance = definition.appearance;
                    m.role = definition.role;
                    m.roleData = definition.roleData;
                    m.name = definition.name;
                    m.stats = definition.stats;
                    m.catalogChecksum = this->mapContent->npcs->checksum;
                    m.feet = placement.feet;
                    m.yaw = placement.yaw;
                    this->npcSpawns.push_back(std::move(m));
                }

            }

        }

    }

    void BindToCurrentThread();
    void AdmitTransfer(WorldTransfer transfer);
    // Server combat integration boundary. Never registered as client packet handlers.
    std::uint32_t ApplyDamage(std::uint64_t entity, std::uint32_t amount);
    bool SpendMp(std::uint64_t entity, std::uint32_t amount);
    void Process(GameCommand command);
    void Tick(double deltaSeconds);
    std::uint64_t GetProcessedCommandCount() const;
    std::uint64_t GetProcessedLifecycleCount() const;

private:
    bool IsOwnerThread() const;
    void Handle(const GameCommand& command, const TestMessageCommand& payload);
    void Handle(const GameCommand& command, const SessionConnectedCommand& payload);
    void Handle(const GameCommand& command, const SessionDisconnectedCommand& payload);
    void Handle(const GameCommand& command, const WorldMessageCommand& payload);

    struct CombatState
    {
        shared_world::combat::Event cast;
        shared_world::combat::Ability castAbility;
        std::uint32_t pattern = 0, step = 0;
        std::uint64_t stepUntil = 0, target = 0;
        bool returning = false;
        shared_world::WorldPosition wander{};
        std::array<std::uint64_t, 65> cooldowns{};
        std::uint64_t busyUntil = 0, lastRequest = 0, allowedRequest = 0, life = 1, respawnAt = 0;
    };

    struct Projectile
    {
        shared_world::combat::Event event;
        shared_world::WorldPosition feet{}, end{};
        shared_world::EntityStats attacker;
        std::uint64_t fired = 0, expires = 0;
        shared_world::combat::Ability ability;
        bool piercing = false;
        std::array<std::uint64_t, shared_world::npc::MAX_PER_MAP> hits{};
        unsigned hitCount = 0;
    };

    struct Player
    {
        std::weak_ptr<Session> session;
        shared_world::net::Message identity;
        shared_world::EntityStats stats;
        CombatState combat;
        shared_world::WorldPosition feet{};
        std::int32_t yaw = 0;
        std::uint64_t lastQueued = 0, ack = 0;
        bool entered = false, ready = false;
        std::uint32_t visit = 0;
        std::uint64_t partyId = 0, matchRoom = 0, lastMatchRequest = 0;
        std::chrono::steady_clock::time_point deadline;
        std::deque<shared_world::net::Message> inputs;
        std::uint64_t lastTalk = 0, dialogueId = 0, dialogueNpc = 0, dialogueSequence = 0;
        std::chrono::steady_clock::time_point talkAllowed{}, dialogueExpiry{};
        shared_world::dungeon::Receipt exitReceipt;
        shared_world::economy::State economy;
        bool economyReady = false, economyPending = false, economyUnknown = false, waitingReady = false;
        shared_world::net::Message deferredReady;
        shared_world::economy::Request pendingEconomy;
        shared_world::economy::Consumable pendingPotion;
        std::uint64_t pendingPotionLife = 0;
        std::chrono::steady_clock::time_point potionAllowed{};
        std::uint64_t economySerial = 0, shopId = 0, shopNpc = 0, shopToken = 0;
        std::chrono::steady_clock::time_point shopExpiry{}, economyAllowed{};
        unsigned economyRetries = 0;
    };

    void Send(const SessionPtr&, shared_world::net::Message);
    void Broadcast(shared_world::net::Message);
    void Remove(std::uint64_t sessionId);
    void AddPlayer(std::uint64_t sessionId, Player player);
    void Interact(const SessionPtr&, Player&, const shared_world::net::Message&);
    void BeginDungeonRun();

    bool IsMaze() const
    {
        return this->mapContent && this->mapContent->identity.id == 301 && this->mapContent->identity.version >= 2;
    }

    void TickDungeon();
    void DungeonSnapshot(const SessionPtr& recipient = {});
    void ClearEncounter();
    void AddDungeonExit(shared_world::WorldPosition, shared_world::dungeon::ExitType, std::uint32_t);
    std::uint32_t activeRoom = 0, clearedRooms = 0, corridorRolls = 0;
    std::uint64_t encounterLife = 0;
    std::unordered_map<std::uint64_t, shared_world::WorldPosition> npcHomes;
    shared_world::EntityStats* FindStats(std::uint64_t entity);
    void BroadcastStats(std::uint64_t entity, const shared_world::EntityStats& stats);
    void UseExit(const SessionPtr&, Player&, const shared_world::net::Message&);
    void BeginEconomy(std::uint64_t, shared_world::economy::Action);
    void HandleEconomy(const SessionPtr&, Player&, const shared_world::net::Message&);
    void Shop(const SessionPtr&, Player&, const shared_world::net::Message&);
    void ApplyEconomy(Player&, const shared_world::economy::State&);
    void SendEconomy(const SessionPtr&, const Player&, std::uint32_t, shared_world::economy::Result,
                     const std::string& operation = {}, std::uint64_t committedRevision = 0);
    bool DispatchEconomy(const SessionPtr&, Player&, shared_world::economy::Request,
                         std::function<void(Player&, const shared_world::economy::RpcResponse&)>);
    shared_world::net::Message Snapshot(std::uint64_t id, const Player&, shared_world::net::Kind) const;
    std::shared_ptr<const GameMapContent> mapContent;
    std::unique_ptr<shared_world::ChunkResidency> residency;
    const std::uint64_t instanceId;
    std::function<bool(const SessionPtr&, const shared_world::net::Message&)> sendWorld;
    TransferWorld transferWorld;
    TransferParty transferParty;
    std::uint32_t defaultDungeonMap = 0;

    struct MatchRoom
    {
        std::uint64_t id = 0, revision = 1;
        std::uint32_t targetMap = 0, lastSecond = 0;
        std::vector<shared_world::party::Member> members;
        std::chrono::steady_clock::time_point deadline{};
    };

    std::unordered_map<std::uint64_t, MatchRoom> matches;
    std::uint32_t nextMatch = 0;
    void JoinMatch(const SessionPtr&, Player&, std::uint32_t targetMap);
    void MatchInput(const SessionPtr&, Player&, const shared_world::net::Message&);
    void LeaveMatch(std::uint64_t id, bool notify = true);
    void MatchSnapshot(MatchRoom&, std::chrono::steady_clock::time_point, unsigned error = 0);
    void TickMatches(std::chrono::steady_clock::time_point);
    bool CanDamage(std::uint64_t source, std::uint64_t target) const;
    unsigned readyTimeoutSeconds = 120;
    std::function<void(std::uint64_t)> worldClosed;
    std::unordered_map<std::uint64_t, Player> players;
    std::vector<decltype(players)::node_type> playerPool;
    std::vector<shared_world::net::Message> npcSpawns;
    std::uint64_t tickNumber = 0, combatMs = 0, nextAction = 0;
    double combatRemainder = 0;
    std::unordered_map<std::uint64_t, CombatState> npcCombat;
    std::vector<Projectile> projectiles;
    std::mt19937 combatRandom{std::random_device{}()};
    CombatState* FindCombat(std::uint64_t);
    shared_world::WorldPosition* FindFeet(std::uint64_t);
    void CombatInput(const SessionPtr&, Player&, const shared_world::net::Message&);
    shared_world::combat::Reject StartAttack(std::uint64_t, std::uint32_t, std::uint64_t, std::uint64_t,
                                             shared_world::WorldPosition);
    void TickCombat(double);
    void CancelCombat(std::uint64_t);
    void SendCombat(shared_world::combat::Event, const SessionPtr& recipient = {});
    void CombatSnapshot(const SessionPtr&);
    void CombatHit(const shared_world::combat::Event&, std::uint64_t, const shared_world::EntityStats&,
                   const shared_world::combat::Ability* ability = nullptr);
    void SkillMotion(std::uint64_t, const shared_world::skills::Definition&, shared_world::WorldPosition);
    void CombatDeath(std::uint64_t);
    bool CombatVisible(shared_world::WorldPosition, shared_world::WorldPosition);
    unsigned CombatRadius(std::uint64_t) const;
    unsigned CombatHeight(std::uint64_t) const;
    bool CombatBodyClear(std::uint64_t, shared_world::WorldPosition, shared_world::WorldPosition) const;
    shared_world::WorldPosition MovePlayer(shared_world::WorldPosition, int, int);

    std::uint64_t nextDialogue = 0;
    std::mt19937_64 exitRandom;
    std::uint64_t nextRun = 0;
    std::shared_ptr<const shared_world::economy::Catalog> economyCatalog;
    SubmitEconomy submitEconomy;
    std::uint32_t nextEconomyRequest = 0;
    shared_world::dungeon::Layout exitLayout;

    std::thread::id ownerThreadId;
    std::atomic<std::uint64_t> processedCommandCount{0};
    std::atomic<std::uint64_t> processedLifecycleCount{0};
};

