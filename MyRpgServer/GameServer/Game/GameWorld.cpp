#include "GameWorld.h"

#include "../../ServerCore/StructuredLogger.h"

#include <cassert>
#include <type_traits>

namespace
{
std::string EconomyOperation()
{
    std::random_device random;
    static constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto BYTE = static_cast<unsigned>(random()) & 255;
        value += hex[BYTE >> 4];
        value += hex[BYTE & 15];
    }

    return value;
}

}

void GameWorld::BindToCurrentThread()
{
    this->ownerThreadId = std::this_thread::get_id();
}

void GameWorld::Process(GameCommand command)
{
    assert(IsOwnerThread());
    if (!IsOwnerThread())
    {
        RPG_LOG_CRITICAL("Game", "Rejected command outside the game thread.");
        return;
    }

    std::visit(
        [this, &command](const auto& payload)
        {
            Handle(command, payload);
        },

        command.payload);
}

void GameWorld::Tick(double deltaSeconds)
{
    assert(IsOwnerThread());
    ++this->tickNumber;
    if (this->residency)
    {
        std::vector<shared_world::WorldPosition> positions;
        for (const auto& entry : this->players)
        {
            if (entry.second.ready)
            {
                positions.push_back(entry.second.feet);
            }

        }

        this->residency->BeginFrame(positions);
    }

    std::vector<std::uint64_t> removed;
    for (auto& [id, p] : this->players)
    {
        const auto SESSION = p.session.lock();
        if (!SESSION || !SESSION->IsOpen() || (!p.ready && std::chrono::steady_clock::now() > p.deadline))
        {
            if (SESSION && SESSION->IsOpen())
            {
                SESSION->Close("world readiness timeout");
            }

            if (this->worldClosed)
            {
                this->worldClosed(id);
            }

            removed.push_back(id);
            continue;
        }

        if (!p.ready || !this->mapContent)
        {
            continue;
        }

        if (!p.inputs.empty())
        {
            const auto INPUT = p.inputs.front();
            p.inputs.pop_front();
            if (!p.stats.IsDead())
            {
                if (!p.matchRoom && (INPUT.dx || INPUT.dy))
                {
                    CancelCombat(id);
                }

                if (!p.matchRoom)
                {
                    p.feet = MovePlayer(p.feet, INPUT.dx, INPUT.dy);
                }

            }

            p.yaw = INPUT.yaw;
            p.ack = INPUT.sequence;
            Broadcast(Snapshot(id, p, shared_world::net::Kind::STATE));
        }

    }

    for (auto id : removed)
    {
        Remove(id);
    }

    TickMatches(std::chrono::steady_clock::now());
    TickCombat(deltaSeconds);
    TickDungeon();
}

std::uint64_t GameWorld::GetProcessedCommandCount() const
{
    return this->processedCommandCount.load(std::memory_order_relaxed);
}

std::uint64_t GameWorld::GetProcessedLifecycleCount() const
{
    return this->processedLifecycleCount.load(std::memory_order_relaxed);
}

bool GameWorld::IsOwnerThread() const
{
    return this->ownerThreadId == std::this_thread::get_id();
}

void GameWorld::Handle(const GameCommand& command, const TestMessageCommand&)
{
    // Gameplay commands queued before a disconnect may still be present. They
    // must not mutate a future player that reuses higher-level identity data.
    const SessionPtr SESSION = command.session.lock();
    if (!SESSION)
    {
        return;
    }

    if (!SESSION->IsOpen() || SESSION->GetId() != command.sessionId)
    {
        return;
    }

    RPG_LOG_DEBUG("Game", "Processed queued test command. sessionId=" << command.sessionId);
    this->processedCommandCount.fetch_add(1, std::memory_order_relaxed);
}

void GameWorld::Handle(const GameCommand& command, const SessionConnectedCommand&)
{
    const SessionPtr SESSION = command.session.lock();
    if (!SESSION || !SESSION->IsOpen() || SESSION->GetId() != command.sessionId)
    {
        return;
    }

    RPG_LOG_DEBUG("Game", "Session connected on game thread. sessionId=" << command.sessionId);
    this->processedLifecycleCount.fetch_add(1, std::memory_order_relaxed);
}

void GameWorld::Handle(const GameCommand& command, const SessionDisconnectedCommand& payload)
{
    Remove(command.sessionId);
    // Disconnect cleanup must run even though the socket is already closed.
    RPG_LOG_DEBUG("Game", "Session disconnected on game thread. sessionId=" << command.sessionId
                                                                            << ", reason=" << payload.reason);
    this->processedLifecycleCount.fetch_add(1, std::memory_order_relaxed);
}

void GameWorld::Send(const SessionPtr& session, shared_world::net::Message message)
{
    if (!session || !session->IsOpen())
    {
        return;
    }

    message.epoch = session->GetId();
    const auto PLAYER = this->players.find(session->GetId());
    if (PLAYER != this->players.end() && message.kind != shared_world::net::Kind::ERROR_VALUE)
    {
        message.request = PLAYER->second.visit;
    }

    if (!this->sendWorld || !this->sendWorld(session, message))
    {
        session->Close("world send failed");
    }

    if (message.kind == shared_world::net::Kind::SPAWN && session->IsOpen())
    {
        shared_world::net::Message relation;
        relation.kind = shared_world::net::Kind::PARTY;
        relation.entity = message.entity;
        if (const auto SOURCE = this->players.find(message.entity); SOURCE != this->players.end())
        {
            relation.partyId = SOURCE->second.partyId;
        }

        Send(session, std::move(relation));
    }

    if (this->economyCatalog && message.kind == shared_world::net::Kind::SPAWN && session->IsOpen())
    {
        message.kind = shared_world::net::Kind::EQUIPMENT;
        if (const auto SOURCE = this->players.find(message.entity); SOURCE != this->players.end())
        {
            message.sequence = SOURCE->second.economy.revision;
        }

        Send(session, std::move(message));
    }

}

void GameWorld::Broadcast(shared_world::net::Message message)
{
    for (const auto& [id, p] : this->players)
    {
        if (p.ready)
        {
            Send(p.session.lock(), message);
        }

    }

}

shared_world::net::Message GameWorld::Snapshot(std::uint64_t id, const Player& p, shared_world::net::Kind kind) const
{
    shared_world::net::Message m;
    m.kind = kind;
    m.entity = id;
    m.character = p.identity.character;
    m.name = p.identity.name;
    m.feet = p.feet;
    m.yaw = p.yaw;
    m.sequence = p.ack;
    m.tick = this->tickNumber;
    m.race = p.identity.race;
    m.gender = p.identity.gender;
    m.stats = p.stats;
    if (this->economyCatalog && p.economyReady)
    {
        m.sequence = p.economy.revision;
        for (const auto& item : p.economy.items)
        {
            if (item.equipped < 2)
            {
                m.equipment[item.equipped] = this->economyCatalog->Find(item.type)->appearance;
            }

        }

        if (kind != shared_world::net::Kind::EQUIPMENT)
        {
            m.sequence = p.ack;
        }

    }

    return m;
}

void GameWorld::Remove(std::uint64_t id)
{
    const auto FOUND = this->players.find(id);
    if (FOUND == this->players.end())
    {
        return;
    }

    LeaveMatch(id, false);
    CancelCombat(id);
    const bool VISIBLE = FOUND->second.ready;
    auto node = this->players.extract(FOUND);
    node.mapped() = Player{};
    this->playerPool.push_back(std::move(node));
    if (this->players.empty())
    {
        this->exitLayout = {};
    }

    if (VISIBLE)
    {
        shared_world::net::Message m;
        m.kind = shared_world::net::Kind::DESPAWN;
        m.entity = id;
        Broadcast(m);
    }

}

void GameWorld::AddPlayer(std::uint64_t id, Player player)
{
    assert(IsOwnerThread());
    if (this->playerPool.empty())
    {
        this->players.emplace(id, std::move(player));
        return;
    }

    auto node = std::move(this->playerPool.back());
    this->playerPool.pop_back();
    node.key() = id;
    node.mapped() = std::move(player);
    this->players.insert(std::move(node));
}

void GameWorld::Handle(const GameCommand& command, const WorldMessageCommand& payload)
{
    using namespace shared_world::net;
    const auto SESSION = command.session.lock();
    if (!SESSION || !SESSION->IsOpen())
    {
        return;
    }

    const auto& m = payload.message;
    auto fail = [&](Error error)
    {
        Message reply;
        reply.kind = Kind::ERROR_VALUE;
        reply.error = static_cast<unsigned>(error);
        reply.request = m.request;
        Send(SESSION, reply);
        Remove(SESSION->GetId());
        SESSION->Close("world request rejected");
        if (this->worldClosed)
        {
            this->worldClosed(SESSION->GetId());
        }

    };

    if (!this->mapContent)
    {
        fail(Error::UNAVAILABLE);
        return;
    }

    if (m.kind == Kind::AUTHORIZE)
    {
        // Only a trusted loopback Gate tunnel creates sessions with this address.
        if (SESSION->GetRemoteAddress() != "gate-user" || this->players.count(SESSION->GetId()) ||
            this->players.size() >= 32)
        {
            fail(Error::UNAVAILABLE);
            return;
        }

        for (const auto& [id, p] : this->players)
        {
            if (p.identity.character == m.character)
            {
                const auto PEER = p.session.lock();
                if (PEER && PEER->IsOpen())
                {
                    fail(Error::ALREADY_ONLINE);
                    return;
                }

            }

        }

        Player p;
        p.session = SESSION;
        p.identity = m;
        p.stats = shared_world::EntityStats::PlayerDefaults();
        if (this->mapContent->combat && this->mapContent->combat->playerAccuracyOverride >= 0)
        {
            p.stats.accuracy = static_cast<unsigned>(this->mapContent->combat->playerAccuracyOverride);
        }

        p.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        if (this->mapContent->exitRule && this->players.empty())
        {
            BeginDungeonRun();
        }

        AddPlayer(SESSION->GetId(), std::move(p));
        if (this->economyCatalog)
        {
            BeginEconomy(SESSION->GetId(), shared_world::economy::Action::LOAD);
        }

        return;
    }

    const auto FOUND = this->players.find(SESSION->GetId());
    // Commands queued on the previous owner before a handoff are harmless.
    if (FOUND == this->players.end())
    {
        return;
    }

    auto& player = FOUND->second;
    if (m.kind == Kind::ENTER)
    {
        if (player.entered || std::chrono::steady_clock::now() > player.deadline || m.ticket != player.identity.ticket)
        {
            fail(Error::EXPIRED);
            return;
        }

        player.identity.ticket.clear();
        player.entered = true;
        player.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(this->readyTimeoutSeconds);
        Message prepare;
        prepare.kind = Kind::PREPARE;
        prepare.instance = this->instanceId;
        prepare.entity = this->transferWorld && !this->mapContent->exitRule ? 1 : 0;
        prepare.map = this->mapContent->identity;
        Send(SESSION, prepare);
        return;
    }

    if (!player.entered || m.epoch != SESSION->GetId())
    {
        fail(Error::NOT_READY);
        return;
    }

    if (m.request != player.visit)
    {
        return; // stale Ready/input/portal from an old visit
    }

    if (m.kind == Kind::MATCH_ACTION)
    {
        MatchInput(SESSION, player, m);
        return;
    }

    if (m.kind == Kind::COMBAT_REQUEST || m.kind == Kind::COMBAT_CANCEL)
    {
        CombatInput(SESSION, player, m);
        return;
    }

    if (m.kind == Kind::ECONOMY_REQUEST)
    {
        HandleEconomy(SESSION, player, m);
        return;
    }

    if (m.kind == Kind::SHOP_OPEN || m.kind == Kind::SHOP_CLOSE)
    {
        Shop(SESSION, player, m);
        return;
    }

    if ((player.economyPending || player.economyUnknown) &&
        (m.kind == Kind::PORTAL || m.kind == Kind::NPC_WARP || m.kind == Kind::EXIT_USE))
    {
        if (m.kind == Kind::NPC_WARP)
        {
            Message reply;
            reply.kind = Kind::NPC_RESULT;
            reply.entity = m.entity;
            reply.sequence = m.sequence;
            reply.interaction = m.interaction;
            reply.error = 4;
            Send(SESSION, reply);
        }
        else if (m.kind == Kind::EXIT_USE)
        {
            Message reply;
            reply.kind = Kind::EXIT_RESULT;
            reply.exitReceipt = m.exitReceipt;
            reply.exitReceipt.sourceInstance = this->instanceId;
            reply.error = 5;
            Send(SESSION, reply);
        }
        else
        {
            SendEconomy(SESSION, player, 1, shared_world::economy::Result::BUSY);
        }

        return;
    }

    if (player.stats.IsDead() && (m.kind == Kind::PORTAL || m.kind == Kind::EXIT_USE || m.kind == Kind::NPC_TALK ||
                                  m.kind == Kind::NPC_WARP || m.kind == Kind::SHOP_OPEN))
    {
        return;
    }

    if (m.kind == Kind::EXIT_USE)
    {
        UseExit(SESSION, player, m);
        return;
    }

    if (m.kind == Kind::NPC_TALK || m.kind == Kind::NPC_WARP || m.kind == Kind::NPC_CANCEL)
    {
        Interact(SESSION, player, m);
        return;
    }

    if (m.kind == Kind::PORTAL)
    {
        if (this->mapContent->exitRule)
        {
            return; // This dungeon's only return path is an authoritative corner exit.
        }

        if (!player.ready || !this->transferWorld)
        {
            return;
        }

        const auto ORIGIN = this->mapContent->manifest.spawns.front().feet;
        const auto DX = std::int64_t(player.feet.x) - ORIGIN.x, DY = std::int64_t(player.feet.y) - ORIGIN.y;
        if (DX * DX + DY * DY > 300 * 300 || std::abs(std::int64_t(player.feet.z) - ORIGIN.z) > 100)
        {
            return;
        }

        if (this->transferParty && this->defaultDungeonMap)
        {
            JoinMatch(SESSION, player, this->defaultDungeonMap);
            return;
        }

        if (player.visit == UINT32_MAX)
        {
            fail(Error::UNAVAILABLE);
            return;
        }

        WorldTransfer transfer{player.session, player.identity, player.visit + 1};
        CancelCombat(SESSION->GetId());
        for (std::size_t i = 0; i < 65; ++i)
        {
            transfer.combatCooldownRemaining[i] =
                player.combat.cooldowns[i] > this->combatMs ? player.combat.cooldowns[i] - this->combatMs : 0;
        }

        transfer.stats = player.stats;
        transfer.economy = player.economy;
        transfer.economyReady = player.economyReady;
        transfer.potionAllowed = player.potionAllowed;
        if (!this->transferWorld(this->instanceId, std::move(transfer),
                                 [this, id = SESSION->GetId()]
                                 {
                                     Remove(id);
                                 }))

        {
            fail(Error::UNAVAILABLE);
        }

        return;
    }

    if (m.kind == Kind::READY)
    {
        if (m.instance != this->instanceId || !(m.map == this->mapContent->identity))
        {
            fail(Error::MAP_MISMATCH);
            return;
        }

        if (player.ready)
        {
            return; // duplicate Ready is idempotent
        }

        if (this->economyCatalog && !player.economyReady)
        {
            player.waitingReady = true;
            player.deferredReady = m;
            return;
        }

        const auto& spawn = this->mapContent->manifest.spawns.front();
        player.feet = spawn.feet;
        player.yaw = spawn.yawMilliDegrees;
        player.ready = true;
        for (const auto& [id, p] : this->players)
        {
            if (p.ready)
            {
                const auto PEER = p.session.lock();
                if (PEER && PEER->IsOpen())
                {
                    Send(SESSION, Snapshot(id, p, Kind::SPAWN));
                }

            }

        }

        const auto OWN = Snapshot(SESSION->GetId(), player, Kind::SPAWN);
        shared_world::combat::Event presence;
        presence.source = SESSION->GetId();
        presence.life = player.combat.life;
        presence.phase = shared_world::combat::Phase::RESPAWN;
        presence.point = player.feet;
        for (const auto& [id, p] : this->players)
        {
            if (p.ready && id != SESSION->GetId())
            {
                Send(p.session.lock(), OWN);
                if (this->mapContent->combat)
                {
                    // Earlier arrivals need the later player's authoritative life before targeting.
                    SendCombat(presence, p.session.lock());
                }

            }

        }

        for (const auto& npc : this->npcSpawns)
        {
            Send(SESSION, npc);
        }

        CombatSnapshot(SESSION);
        if (IsMaze())
        {
            DungeonSnapshot(SESSION);
        }

        if (player.economyReady)
        {
            SendEconomy(SESSION, player, 1, shared_world::economy::Result::SUCCESS);
        }

        if (this->exitLayout.Valid())
        {
            Message layout;
            layout.kind = Kind::EXIT_LAYOUT;
            layout.instance = this->instanceId;
            layout.exits = this->exitLayout;
            Send(SESSION, std::move(layout));
        }

        if (player.exitReceipt.Valid())
        {
            Message result;
            result.kind = Kind::EXIT_RESULT;
            result.exitReceipt = player.exitReceipt;
            Send(SESSION, result);
            player.exitReceipt = {};
        }

        return;
    }

    if (m.kind == Kind::MOVE)
    {
        if (!player.ready || m.sequence != player.lastQueued + 1 || player.inputs.size() >= 8)
        {
            fail(Error::INVALID_INPUT);
            return;
        }

        player.inputs.push_back(m);
        player.lastQueued = m.sequence;
        this->processedCommandCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    fail(Error::INVALID_INPUT);
}

void GameWorld::Interact(const SessionPtr& session, Player& player, const shared_world::net::Message& m)
{
    using namespace shared_world::net;
    assert(IsOwnerThread());
    const auto NOW = std::chrono::steady_clock::now();
    auto reject = [&](NpcError error)
    {
        Message result;
        result.kind = Kind::NPC_RESULT;
        result.entity = m.entity;
        result.sequence = m.sequence;
        result.interaction = m.interaction;
        result.error = static_cast<unsigned>(error);
        Send(session, result);
    };

    if (!player.ready)
    {
        reject(NpcError::NOT_ALLOWED);
        return;
    }

    if (m.kind == Kind::NPC_CANCEL)
    {
        if (m.interaction == player.dialogueId && m.entity == player.dialogueNpc &&
            m.sequence == player.dialogueSequence)
        {
            player.dialogueId = 0;
        }

        return;
    }

    if (m.kind == Kind::NPC_TALK)
    {
        if (m.sequence <= player.lastTalk)
        {
            return;
        }

        player.lastTalk = m.sequence;
        player.dialogueId = 0;
        if (NOW < player.talkAllowed)
        {
            reject(NpcError::RATE_LIMITED);
            return;
        }

        player.talkAllowed = NOW + std::chrono::milliseconds(500);
    }
    else if (!player.dialogueId || m.interaction != player.dialogueId || m.entity != player.dialogueNpc ||
             m.sequence != player.dialogueSequence || NOW > player.dialogueExpiry)
    {
        reject(NpcError::EXPIRED);
        return;
    }

    const auto FOUND = std::find_if(this->npcSpawns.begin(), this->npcSpawns.end(),
                                    [&](const auto& n)
                                    {
                                        return n.entity == m.entity;
                                    });

    if (FOUND == this->npcSpawns.end())
    {
        reject(NpcError::INVALID_NPC);
        return;
    }

    if (FOUND->role != static_cast<unsigned>(shared_world::npc::Role::DIALOGUE_WARP))
    {
        reject(NpcError::NOT_ALLOWED);
        return;
    }

    const auto& warp = this->mapContent->npcs->warps.at(FOUND->roleData);
    if (!shared_world::npc::InRange(player.feet, FOUND->feet, warp.range))
    {
        player.dialogueId = 0;
        reject(NpcError::TOO_FAR);
        return;
    }

    if (m.kind == Kind::NPC_TALK)
    {
        if (this->nextDialogue == UINT64_MAX)
        {
            reject(NpcError::UNAVAILABLE);
            return;
        }

        player.dialogueId = ++this->nextDialogue;
        player.dialogueNpc = m.entity;
        player.dialogueSequence = m.sequence;
        player.dialogueExpiry = NOW + std::chrono::seconds(30);
        Message dialogue;
        dialogue.kind = Kind::NPC_DIALOGUE;
        dialogue.entity = m.entity;
        dialogue.sequence = m.sequence;
        dialogue.interaction = player.dialogueId;
        dialogue.targetMap = warp.targetMap;
        dialogue.text = warp.text;
        dialogue.optionLabel = warp.option;
        Send(session, std::move(dialogue));
        return;
    }

    if (m.choice != 1)
    {
        reject(NpcError::NOT_ALLOWED);
        return;
    }

    player.dialogueId = 0; // Consume before reservation: a duplicate can never transfer twice.
    if (!this->transferWorld || player.visit == UINT32_MAX)
    {
        reject(NpcError::UNAVAILABLE);
        return;
    }

    if (this->transferParty)
    {
        JoinMatch(session, player, warp.targetMap);
        return;
    }

    WorldTransfer transfer{player.session, player.identity, player.visit + 1, warp.targetMap};
    CancelCombat(session->GetId());
    for (std::size_t i = 0; i < 65; ++i)
    {
        transfer.combatCooldownRemaining[i] =
            player.combat.cooldowns[i] > this->combatMs ? player.combat.cooldowns[i] - this->combatMs : 0;
    }

    transfer.stats = player.stats;
    transfer.economy = player.economy;
    transfer.economyReady = player.economyReady;
    transfer.potionAllowed = player.potionAllowed;
    if (!this->transferWorld(this->instanceId, std::move(transfer),
                             [this, id = session->GetId()]
                             {
                                 Remove(id);
                             }))

    {
        reject(NpcError::UNAVAILABLE); // Reservation failure leaves the source player intact.
    }

}

void GameWorld::AdmitTransfer(WorldTransfer transfer)
{
    assert(IsOwnerThread());
    auto session = transfer.session.lock();
    if (!session || !session->IsOpen())
    {
        return;
    }

    if (!this->mapContent || this->players.size() >= 32 || this->players.contains(session->GetId()) ||
        !transfer.stats.Valid() || !transfer.stats.CanDie())
    {
        session->Close("transfer admission failed");
        if (this->worldClosed)
        {
            this->worldClosed(session->GetId());
        }

        return;
    }

    if (this->mapContent->exitRule)
    {
        std::vector<std::uint64_t> retired;
        for (const auto& [id, p] : this->players)
        {
            const auto PEER = p.session.lock();
            if (!PEER || !PEER->IsOpen())
            {
                retired.push_back(id);
            }

        }

        for (auto id : retired)
        {
            Remove(id);
        }

        if (!this->players.empty() && !transfer.partyId)
        {
            session->Close("private dungeon occupied");
            if (this->worldClosed)
            {
                this->worldClosed(session->GetId());
            }

            return;
        }

        if (this->players.empty())
        {
            BeginDungeonRun();
        }

    }

    Player player;
    player.session = session;
    player.identity = std::move(transfer.identity);
    player.partyId = transfer.partyId;
    player.stats = transfer.stats;
    for (std::size_t i = 0; i < 65; ++i)
    {
        player.combat.cooldowns[i] = this->combatMs + transfer.combatCooldownRemaining[i];
    }

    player.economy = std::move(transfer.economy);
    player.economyReady = transfer.economyReady;
    player.potionAllowed = transfer.potionAllowed;
    player.exitReceipt = transfer.exitReceipt;
    player.visit = transfer.visit;
    player.entered = true;
    player.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(this->readyTimeoutSeconds);
    AddPlayer(session->GetId(), std::move(player));
    shared_world::net::Message prepare;
    prepare.kind = shared_world::net::Kind::PREPARE;
    prepare.instance = this->instanceId;
    prepare.entity = this->transferWorld && !this->mapContent->exitRule ? 1 : 0;
    prepare.map = this->mapContent->identity;
    Send(session, prepare);
}

void GameWorld::BeginDungeonRun()
{
    assert(IsOwnerThread());
    if (!this->mapContent || !this->mapContent->exitRule || this->nextRun == UINT64_MAX)
    {
        throw std::invalid_argument("dungeon run unavailable");
    }

    if (IsMaze())
    {
        ClearEncounter();
        this->activeRoom = this->clearedRooms = this->corridorRolls = 0;
        this->exitLayout = {};
        this->exitLayout.run = ++this->nextRun;
        return;
    }

    // A reused private instance starts new NPC leases, rather than keeping the previous run's damage.
    this->projectiles.clear();
    this->npcCombat.clear();
    for (auto& npc : this->npcSpawns)
    {
        npc.stats = this->mapContent->npcs->definitions.at(npc.npcIndex).stats;
    }

    const auto& world = *this->mapContent->collision;
    auto layout = shared_world::dungeon::Generate(*this->mapContent->exitRule, world.Map().Definition().bounds,
                                                  this->mapContent->manifest.spawns.front().feet.z, ++this->nextRun,
                                                  this->exitRandom);
    for (unsigned i = 0; i < layout.count; ++i)
    {
        auto& zone = layout.zones[i];
        const auto GROUND = shared_world::GroundSupport(world, zone.feet);
        if (GROUND.status != shared_world::QueryStatus::HIT)
        {
            throw std::invalid_argument("exit lacks ground");
        }

        zone.feet = GROUND.position;
        for (int x : {-1, 0, 1})
        {
            for (int y : {-1, 0, 1})
            {
                auto p = zone.feet;
                p.x += x * static_cast<int>(zone.radius);
                p.y += y * static_cast<int>(zone.radius);
                const auto SUPPORT = shared_world::GroundSupport(world, p);
                auto raised = p;
                ++raised.z;
                if (SUPPORT.status != shared_world::QueryStatus::HIT ||
                    std::abs(SUPPORT.position.z - zone.feet.z) > 1 ||
                    shared_world::SweepBody(world, raised, raised).status != shared_world::QueryStatus::CLEAR)
                {
                    throw std::invalid_argument("exit zone is not flat/clear");
                }

            }

        }

    }

    this->exitLayout = std::move(layout);
}

void GameWorld::UseExit(const SessionPtr& session, Player& player, const shared_world::net::Message& m)
{
    using namespace shared_world::net;
    using namespace shared_world::dungeon;
    auto reject = [&](ExitError error)
    {
        Message result;
        result.kind = Kind::EXIT_RESULT;
        result.exitReceipt = m.exitReceipt;
        result.exitReceipt.sourceInstance = this->instanceId;
        result.error = static_cast<unsigned>(error);
        Send(session, result);
    };

    if (!player.ready || !this->mapContent->exitRule || !this->exitLayout.Valid())
    {
        reject(ExitError::NOT_DUNGEON);
        return;
    }

    if (m.exitReceipt.run != this->exitLayout.run)
    {
        reject(ExitError::STALE_RUN);
        return;
    }

    const Zone* zone = nullptr;
    for (unsigned i = 0; i < this->exitLayout.count; ++i)
    {
        if (this->exitLayout.zones[i].id == m.exitReceipt.exitId)
        {
            zone = &this->exitLayout.zones[i];
        }

    }

    if (!zone)
    {
        reject(ExitError::UNKNOWN_EXIT);
        return;
    }

    if (IsMaze() && this->activeRoom)
    {
        reject(ExitError::UNAVAILABLE);
        return;
    }

    if (!zone->Contains(player.feet))
    {
        reject(ExitError::TOO_FAR);
        return;
    }

    if (!this->transferWorld || player.visit == UINT32_MAX)
    {
        reject(ExitError::UNAVAILABLE);
        return;
    }

    // Value receipt survives owner handoff; future outcome processing belongs at this boundary.
    WorldTransfer transfer{player.session, player.identity, player.visit + 1};
    CancelCombat(session->GetId());
    for (std::size_t i = 0; i < 65; ++i)
    {
        transfer.combatCooldownRemaining[i] =
            player.combat.cooldowns[i] > this->combatMs ? player.combat.cooldowns[i] - this->combatMs : 0;
    }

    transfer.stats = player.stats;
    transfer.economy = player.economy;
    transfer.economyReady = player.economyReady;
    transfer.potionAllowed = player.potionAllowed;
    transfer.exitReceipt = {this->instanceId, this->exitLayout.run, m.exitReceipt.sequence,
                            zone->id,         zone->type,           zone->outcome};
    if (!this->transferWorld(this->instanceId, std::move(transfer),
                             [this, id = session->GetId()]
                             {
                                 Remove(id);
                             }))

    {
        reject(ExitError::UNAVAILABLE);
    }

}

shared_world::EntityStats* GameWorld::FindStats(std::uint64_t entity)
{
    assert(IsOwnerThread());
    if (!IsOwnerThread())
    {
        return nullptr;
    }

    if (entity & shared_world::npc::ENTITY_BIT)
    {
        for (auto& npc : this->npcSpawns)
        {
            if (npc.entity == entity)
            {
                return &npc.stats;
            }

        }

        return nullptr;
    }

    const auto FOUND = this->players.find(entity);
    return FOUND != this->players.end() && FOUND->second.ready ? &FOUND->second.stats : nullptr;
}

void GameWorld::BroadcastStats(std::uint64_t entity, const shared_world::EntityStats& stats)
{
    shared_world::net::Message m;
    m.kind = shared_world::net::Kind::ENTITY_STATS;
    m.entity = entity;
    m.stats = stats;
    Broadcast(m);
}

std::uint32_t GameWorld::ApplyDamage(std::uint64_t entity, std::uint32_t amount)
{
    auto* stats = FindStats(entity);
    if (!stats)
    {
        return 0;
    }

    const auto APPLIED = stats->ApplyDamage(amount);
    if (APPLIED)
    {
        BroadcastStats(entity, *stats);
        if (stats->IsDead())
        {
            CombatDeath(entity);
        }

    }

    return APPLIED;
}

bool GameWorld::SpendMp(std::uint64_t entity, std::uint32_t amount)
{
    auto* stats = FindStats(entity);
    if (!stats || !stats->SpendMp(amount))
    {
        return false;
    }

    if (amount)
    {
        BroadcastStats(entity, *stats);
    }

    return true;
}

bool GameWorld::DispatchEconomy(const SessionPtr& session, Player& p, shared_world::economy::Request request,
                                std::function<void(Player&, const shared_world::economy::RpcResponse&)> apply)
{
    if (!this->submitEconomy || this->nextEconomyRequest == UINT32_MAX)
    {
        return false;
    }

    shared_world::economy::RpcRequest q;
    q.requestId = ++this->nextEconomyRequest;
    q.uid = p.identity.uid;
    q.character = p.identity.character;
    q.request = std::move(request);
    const auto SERIAL = ++p.economySerial, ID = session->GetId();
    const auto VISIT = p.visit;
    p.economyPending = true;
    // Worlds live until runtime shutdown, after data.Stop drains every reserved completion.
    // Player addresses are never captured: disconnect/pool reuse is checked by ID + serial + visit.
    bool accepted = false;
    try
    {
        accepted = this->submitEconomy(this->instanceId, q,
                                       [this, ID, SERIAL, VISIT, apply](const auto& reply)
                                       {
                                           const auto IT = this->players.find(ID);
                                           if (IT == this->players.end())
                                           {
                                               return;
                                           }

                                           auto& player = IT->second;
                                           auto peer = player.session.lock();
                                           if (!peer || !peer->IsOpen() || !player.economyPending ||
                                               player.economySerial != SERIAL || player.visit != VISIT)
                                           {
                                               return;
                                           }

                                           player.economyPending = false;
                                           apply(player, reply);
                                       });

    }

    catch (...)
    {
        accepted = false;
    }

    if (!accepted)
    {
        p.economyPending = false;
    }

    return accepted;
}

void GameWorld::BeginEconomy(std::uint64_t id, shared_world::economy::Action action)
{
    using namespace shared_world::economy;
    const auto IT = this->players.find(id);
    if (IT == this->players.end())
    {
        return;
    }

    auto& player = IT->second;
    const auto PEER = player.session.lock();
    if (!PEER || !PEER->IsOpen())
    {
        return;
    }

    Request q;
    q.action = action;
    q.fence = player.economy.fence;
    q.catalog = this->economyCatalog->revision;
    if (action == Action::ACQUIRE)
    {
        q.operation = EconomyOperation();
    }

    auto fail = [this, id, PEER]
    {
        shared_world::net::Message error;
        error.kind = shared_world::net::Kind::ERROR_VALUE;
        error.error = static_cast<unsigned>(shared_world::net::Error::UNAVAILABLE);
        Send(PEER, error);
        PEER->Close("economy initialization unavailable");
        if (this->worldClosed)
        {
            this->worldClosed(id);
        }

    };

    if (!DispatchEconomy(PEER, player, q,
                         [this, id, action, fail](Player& p, const RpcResponse& reply)
                         {
                             if (reply.result == Result::STALE_SESSION && action == Action::ACQUIRE &&
                                 ++p.economyRetries <= 3)
                             {
                                 BeginEconomy(id, Action::LOAD);
                                 return;
                             }

                             if (reply.result != Result::SUCCESS || !reply.state.Valid(*this->economyCatalog))
                             {
                                 fail();
                                 return;
                             }

                             p.economy = reply.state;
                             if (action == Action::LOAD)
                             {
                                 BeginEconomy(id, Action::ACQUIRE);
                                 return;
                             }

                             p.economyReady = true;
                             ApplyEconomy(p, reply.state);
                             if (p.waitingReady)
                             {
                                 p.waitingReady = false;
                                 GameCommand command;
                                 command.session = p.session;
                                 command.sessionId = id;
                                 command.mapInstanceId = this->instanceId;
                                 Handle(command, WorldMessageCommand{p.deferredReady});
                             }

                         }))

    {
        fail();
    }

}

void GameWorld::ApplyEconomy(Player& p, const shared_world::economy::State& state)
{
    p.economy = state;
    auto stats = state.Stats(*this->economyCatalog);
    stats.hp = std::min(p.stats.hp, stats.maxHp);
    stats.mp = std::min(p.stats.mp, stats.maxMp);
    p.stats = stats;
}

void GameWorld::SendEconomy(const SessionPtr& peer, const Player& p, std::uint32_t request,
                            shared_world::economy::Result result, const std::string& operation,
                            std::uint64_t committedRevision)
{
    shared_world::net::Message m;
    m.kind = shared_world::net::Kind::ECONOMY_SNAPSHOT;
    m.economyReply.requestId = request;
    m.economyReply.result = result;
    m.economyReply.operation = operation;
    m.economyReply.state = p.economy;
    m.economyReply.committedRevision = committedRevision;
    if (!p.economyReady)
    {
        m.economyReply.state.revision = 0;
    }

    Send(peer, std::move(m));
}

void GameWorld::Shop(const SessionPtr& peer, Player& p, const shared_world::net::Message& m)
{
    using namespace shared_world::net;
    using namespace shared_world::economy;
    if (m.kind == Kind::SHOP_CLOSE)
    {
        if (p.shopToken == m.interaction && p.shopNpc == m.entity)
        {
            p.shopToken = 0;
        }

        return;
    }

    p.shopToken = 0;
    if (!this->economyCatalog || !p.ready || !p.economyReady || p.stats.IsDead())
    {
        SendEconomy(peer, p, 1, Result::UNAVAILABLE);
        return;
    }

    const auto NOW = std::chrono::steady_clock::now();
    if (p.economyPending || p.economyUnknown || NOW < p.talkAllowed)
    {
        SendEconomy(peer, p, 1, Result::BUSY);
        return;
    }

    p.talkAllowed = NOW + std::chrono::milliseconds(500);
    const auto NPC = std::find_if(this->npcSpawns.begin(), this->npcSpawns.end(),
                                  [&](const auto& v)
                                  {
                                      return v.entity == m.entity;
                                  });

    if (NPC == this->npcSpawns.end() || NPC->role != 2 || !this->economyCatalog->HasShop(NPC->roleData))
    {
        SendEconomy(peer, p, 1, Result::INVALID_SHOP);
        return;
    }

    if (!shared_world::npc::InRange(p.feet, NPC->feet, 300))
    {
        SendEconomy(peer, p, 1, Result::TOO_FAR);
        return;
    }

    if (this->nextDialogue == UINT64_MAX)
    {
        SendEconomy(peer, p, 1, Result::UNAVAILABLE);
        return;
    }

    p.dialogueId = 0;
    p.shopId = NPC->roleData;
    p.shopNpc = m.entity;
    p.shopToken = ++this->nextDialogue;
    p.shopExpiry = NOW + std::chrono::seconds(30);
    Message reply;
    reply.kind = Kind::SHOP_SNAPSHOT;
    reply.entity = m.entity;
    reply.interaction = p.shopToken;
    reply.targetMap = static_cast<std::uint32_t>(p.shopId);
    reply.catalogChecksum = this->economyCatalog->revision;
    for (const auto& o : this->economyCatalog->offers)
    {
        if (o.shop == p.shopId && o.enabled)
        {
            reply.offers.push_back(o);
        }

    }

    Send(peer, std::move(reply));
}

void GameWorld::HandleEconomy(const SessionPtr& peer, Player& p, const shared_world::net::Message& m)
{
    using namespace shared_world::economy;
    const auto REQUEST_ID = m.economyReply.requestId;
    auto reject = [&](Result result)
    {
        SendEconomy(peer, p, REQUEST_ID, result, m.economyRequest.operation);
    };

    if (!this->economyCatalog || !p.ready || !p.economyReady)
    {
        reject(Result::UNAVAILABLE);
        return;
    }

    if (p.economyPending)
    {
        reject(Result::BUSY);
        return;
    }

    Request q = m.economyRequest;
    if (p.economyUnknown && (q.action != Action::QUERY || q.operation != p.pendingEconomy.operation))
    {
        reject(Result::UNKNOWN_OUTCOME);
        return;
    }

    if (q.action == Action::LOAD)
    {
        reject(Result::SUCCESS);
        return;
    }

    if (q.action == Action::ACQUIRE)
    {
        reject(Result::INVALID_ITEM);
        return;
    }

    const auto NOW = std::chrono::steady_clock::now();
    if (NOW < p.economyAllowed)
    {
        reject(Result::BUSY);
        return;
    }

    p.economyAllowed = NOW + std::chrono::milliseconds(100);
    if (q.Mutation() && p.stats.IsDead())
    {
        reject(Result::DEAD);
        return;
    }

    Consumable potion;
    if (q.action == Action::USE)
    {
        if (q.revision != p.economy.revision)
        {
            reject(Result::STALE_REVISION);
            return;
        }

        if (q.catalog != this->economyCatalog->revision)
        {
            reject(Result::INVALID_SHOP);
            return;
        }

        const auto ITEM = std::find_if(p.economy.items.begin(), p.economy.items.end(),
                                       [&](const auto& i)
                                       {
                                           return i.id == q.instance;
                                       });

        if (ITEM == p.economy.items.end())
        {
            reject(Result::NOT_OWNED);
            return;
        }

        const auto EFFECT = this->economyCatalog->consumables.find(ITEM->type);
        if (EFFECT == this->economyCatalog->consumables.end())
        {
            reject(Result::NOT_USABLE);
            return;
        }

        if ((EFFECT->second.heal && p.stats.hp >= p.stats.maxHp) ||
            (EFFECT->second.mana && p.stats.mp >= p.stats.maxMp))
        {
            reject(Result::FULL_HEALTH);
            return;
        } // Existing result18 means the potion's resource is full.

        if (NOW < p.potionAllowed)
        {
            reject(Result::COOLDOWN);
            return;
        }

        potion = EFFECT->second;
    }

    if (q.action == Action::BUY)
    {
        if (!p.shopToken || p.shopToken != m.interaction || p.shopNpc != m.entity || q.shop != p.shopId ||
            NOW > p.shopExpiry)
        {
            reject(Result::EXPIRED);
            return;
        }

        const auto NPC = std::find_if(this->npcSpawns.begin(), this->npcSpawns.end(),
                                      [&](const auto& v)
                                      {
                                          return v.entity == p.shopNpc && v.role == 2 && v.roleData == p.shopId;
                                      });

        if (NPC == this->npcSpawns.end())
        {
            reject(Result::INVALID_SHOP);
            return;
        }

        if (!shared_world::npc::InRange(p.feet, NPC->feet, 300))
        {
            p.shopToken = 0;
            reject(Result::TOO_FAR);
            return;
        }

    }

    q.fence =
        q.action == Action::QUERY ? (p.economyUnknown ? p.pendingEconomy.fence : p.economy.fence - 1) : p.economy.fence;
    if (q.Mutation())
    {
        p.pendingEconomy = q;
        p.pendingPotion = potion;
        p.pendingPotionLife = p.combat.life;
    }

    if (!DispatchEconomy(
            peer, p, q,
            [this, REQUEST_ID, q](Player& player, const RpcResponse& reply)
            {
                auto session = player.session.lock();
                if (!session)
                {
                    return;
                }

                if (reply.result == Result::UNKNOWN_OUTCOME || reply.result == Result::UNAVAILABLE)
                {
                    if (q.Mutation())
                    {
                        player.economyUnknown = true;
                    }

                    SendEconomy(session, player, REQUEST_ID, reply.result, q.operation, reply.committedRevision);
                    return;
                }

                if (!reply.state.Valid(*this->economyCatalog) || reply.state.fence != player.economy.fence)
                {
                    shared_world::net::Message error;
                    error.kind = shared_world::net::Kind::ERROR_VALUE;
                    error.error = static_cast<unsigned>(shared_world::net::Error::UNAVAILABLE);
                    Send(session, error);
                    session->Close("economy fence changed");
                    if (this->worldClosed)
                    {
                        this->worldClosed(session->GetId());
                    }

                    return;
                }

                const auto PREVIOUS = player.stats;
                const auto PREVIOUS_EQUIPMENT =
                    Snapshot(session->GetId(), player, shared_world::net::Kind::EQUIPMENT).equipment;
                // Inventory becomes authoritative first. Resolve a pending use once, including an ambiguous commit
                // query. A journal replay/old operation query cannot heal again. Death/respawn never receives an old
                // life's heal.
                const bool USED =
                    reply.result == Result::SUCCESS && (player.pendingPotion.heal || player.pendingPotion.mana) &&
                    player.pendingEconomy.action == Action::USE && q.operation == player.pendingEconomy.operation &&
                    reply.operation == q.operation && reply.committedRevision == player.pendingEconomy.revision + 1;
                ApplyEconomy(player, reply.state);
                if (USED)
                {
                    if (player.combat.life == player.pendingPotionLife)
                    {
                        player.stats.RestoreHp(player.pendingPotion.heal);
                        player.stats.RestoreMp(player.pendingPotion.mana);
                    }

                    player.potionAllowed =
                        std::chrono::steady_clock::now() + std::chrono::milliseconds(player.pendingPotion.cooldownMs);
                }

                player.pendingPotion = {};
                player.pendingPotionLife = 0;
                player.pendingEconomy = {};
                player.economyUnknown = false;
                SendEconomy(session, player, REQUEST_ID, reply.result, q.operation, reply.committedRevision);
                if (PREVIOUS != player.stats)
                {
                    BroadcastStats(session->GetId(), player.stats);
                }

                auto equipment = Snapshot(session->GetId(), player, shared_world::net::Kind::EQUIPMENT);
                if (PREVIOUS_EQUIPMENT != equipment.equipment)
                {
                    Broadcast(std::move(equipment));
                }

            }))

    {
        p.pendingPotion = {};
        p.pendingPotionLife = 0;
        reject(Result::BUSY);
    }

}

#include "GameWorldCombat.inl"

#include "GameWorldDungeon.inl"

#include "GameWorldParty.inl"
