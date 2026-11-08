#pragma once
#include "../Map/MapPackage.h"
#include "../Npc/NpcCatalog.h"
#include "../Dungeon/DungeonExits.h"
#include "../Dungeon/DungeonParty.h"
#include "../Items/EconomyCodec.h"
#include "../Combat/Combat.h"
#include <string>

namespace shared_world::net
{
// RPGB outer frame types: WorldMessage=19; WorldAuthorize=20 (Gate internal only).
enum class Kind : std::uint32_t
{
    SELECT = 1,
    SELECTED,
    ENTER,
    PREPARE,
    READY,
    SPAWN,
    MOVE,
    STATE,
    DESPAWN,
    ERROR_VALUE,
    AUTHORIZE,
    PORTAL,
    NPC_SPAWN,
    NPC_TALK,
    NPC_DIALOGUE,
    NPC_WARP,
    NPC_RESULT,
    NPC_CANCEL,
    EXIT_LAYOUT,
    EXIT_USE,
    EXIT_RESULT,
    ENTITY_STATS,
    ECONOMY_REQUEST,
    ECONOMY_SNAPSHOT,
    SHOP_OPEN,
    SHOP_SNAPSHOT,
    SHOP_CLOSE,
    EQUIPMENT,
    COMBAT_REQUEST,
    COMBAT_EVENT,
    COMBAT_CANCEL,
    DUNGEON_STATE,
    MATCH_ACTION = 33,
    MATCH_STATE = 34,
    PARTY = 35
};

enum class Error : std::uint32_t
{
    NONE = 0,
    UNAVAILABLE,
    NOT_OWNED,
    EXPIRED,
    NOT_READY,
    MAP_MISMATCH,
    ALREADY_ONLINE,
    INVALID_INPUT
};

enum class NpcError : std::uint32_t
{
    INVALID_NPC = 1,
    TOO_FAR,
    EXPIRED,
    UNAVAILABLE,
    NOT_ALLOWED,
    RATE_LIMITED
};

struct Message
{
    std::uint64_t dungeonRun = 0;
    std::uint32_t activeRoom = 0, clearedRooms = 0;
    combat::Event combatEvent;
    Kind kind = Kind::ERROR_VALUE;
    std::uint32_t request = 0;
    std::uint64_t epoch = 0, character = 0, entity = 0, instance = 0, sequence = 0, tick = 0;
    MapIdentity map{};
    WorldPosition feet{};
    std::int32_t dx = 0, dy = 0, yaw = 0;
    std::uint32_t error = 0;
    std::uint32_t race = 0, gender = 0;
    std::uint32_t npcIndex = 0, appearance = 0, role = 0, roleData = 0;
    std::uint64_t catalogChecksum = 0;
    std::uint64_t interaction = 0;
    std::uint32_t targetMap = 0, choice = 0;
    std::string text, optionLabel;
    std::string ticket, uid, name;
    dungeon::Layout exits;
    dungeon::Receipt exitReceipt;
    shared_world::EntityStats stats;
    economy::Request economyRequest;
    economy::RpcResponse economyReply;
    std::array<std::uint32_t, 2> equipment{}; // Public appearance IDs; no private bag/instance data.
    std::vector<economy::Offer> offers;
    std::uint64_t room = 0, partyId = 0, revision = 0;
    std::uint32_t countdownMs = 0;
    party::MatchPhase matchPhase = party::MatchPhase::NONE;
    std::vector<party::Member> members;

    bool Valid() const
    {
        if (static_cast<unsigned>(this->kind) < 1 || static_cast<unsigned>(this->kind) > 35 || this->error > 7 ||
            this->text.size() > 512 || this->optionLabel.size() > 128 || this->ticket.size() > 64 ||
            this->uid.size() > 128 || this->name.size() > 128 || this->race > 255 || this->gender > 1 ||
            !IsValid(this->feet) || this->yaw < -180000 || this->yaw > 180000)
        {
            return false;
        }

        switch (this->kind)
        {
        case Kind::MATCH_ACTION:
            return this->epoch && this->room && this->sequence && this->choice >= 1 && this->choice <= 3;
        case Kind::MATCH_STATE:
        {
            if (!this->epoch || this->countdownMs > party::COUNTDOWN_MS || this->error > 7 ||
                static_cast<unsigned>(this->matchPhase) > 3 || this->members.size() > party::MAX_MEMBERS)
            {
                return false;
            }

            if (this->matchPhase == party::MatchPhase::NONE)
            {
                return !this->room && !this->revision && !this->targetMap && !this->countdownMs &&
                       this->members.empty();
            }

            if (!this->room || !this->revision || !this->targetMap || this->members.empty() ||
                (this->matchPhase != party::MatchPhase::COUNTDOWN && this->countdownMs))
            {
                return false;
            }

            std::unordered_set<std::uint64_t> entities, characters;
            for (const auto& member : this->members)
            {
                if (!member.entity || (member.entity & npc::ENTITY_BIT) || !member.character || member.name.empty() ||
                    member.name.size() > 128 || member.race > 255 || member.gender > 1 ||
                    !entities.insert(member.entity).second || !characters.insert(member.character).second ||
                    (this->matchPhase != party::MatchPhase::WAITING && !member.ready))
                {
                    return false;
                }

            }

            return true;
        }

        case Kind::PARTY:
            return this->epoch && this->entity && !(this->entity & npc::ENTITY_BIT);
        case Kind::DUNGEON_STATE:
            return this->epoch && this->dungeonRun && this->activeRoom <= 6 && this->clearedRooms < 64 &&
                   (!this->activeRoom || !(this->clearedRooms & (1U << (this->activeRoom - 1))));
        case Kind::COMBAT_REQUEST:
            return this->epoch && this->sequence && this->combatEvent.skill >= 1 && this->combatEvent.skill <= 64 &&
                   IsValid(this->combatEvent.point);
        case Kind::COMBAT_CANCEL:
            return this->epoch && this->sequence;
        case Kind::COMBAT_EVENT:
            return this->epoch && this->combatEvent.source && this->combatEvent.life &&
                   static_cast<unsigned>(this->combatEvent.phase) >= 1 &&
                   static_cast<unsigned>(this->combatEvent.phase) <= 10 && this->combatEvent.skill <= 64 &&
                   this->combatEvent.reason <= 10 && IsValid(this->combatEvent.origin) &&
                   IsValid(this->combatEvent.point) && this->combatEvent.yaw >= -180000 &&
                   this->combatEvent.yaw <= 180000;
        case Kind::ECONOMY_REQUEST:
            return this->epoch && this->economyReply.requestId && this->economyRequest.Valid() &&
                   this->economyRequest.action != economy::Action::ACQUIRE &&
                   this->economyRequest.action != economy::Action::HEALTH;
        case Kind::ECONOMY_SNAPSHOT:
            return this->epoch && this->economyReply.requestId &&
                   static_cast<unsigned>(this->economyReply.result) <= 20 &&
                   this->economyReply.state.items.size() <= 32;
        case Kind::SHOP_OPEN:
            return this->epoch && this->entity && this->sequence;
        case Kind::SHOP_CLOSE:
            return this->epoch && this->entity && this->interaction;
        case Kind::SHOP_SNAPSHOT:
            return this->epoch && this->entity && this->interaction && this->targetMap && this->catalogChecksum &&
                   this->offers.size() <= 8;
        case Kind::EQUIPMENT:
            return this->epoch && this->entity && !(this->entity & npc::ENTITY_BIT);
        case Kind::SELECT:
            return this->request && this->character;
        case Kind::SELECTED:
            return this->request && (this->error || (this->character && this->ticket.size() == 32));
        case Kind::AUTHORIZE:
            return this->character && !this->uid.empty() && this->ticket.size() == 32;
        case Kind::ENTER:
            return this->ticket.size() == 32;
        case Kind::PREPARE:
        case Kind::READY:
            return this->epoch && this->instance && this->map.id && this->map.version && this->entity <= 1;
        case Kind::SPAWN:
            return this->epoch && this->entity && !(this->entity & npc::ENTITY_BIT) && this->stats.Valid() &&
                   this->stats.CanDie();
        case Kind::STATE:
            return this->epoch && this->entity && this->entity != npc::ENTITY_BIT;
        case Kind::ENTITY_STATS:
            return this->epoch && this->entity && (this->entity != npc::ENTITY_BIT) && this->stats.Valid() &&
                   ((this->entity & npc::ENTITY_BIT) || this->stats.CanDie());
        case Kind::NPC_SPAWN:
            return this->epoch && (this->entity & npc::ENTITY_BIT) && (this->entity & ~npc::ENTITY_BIT) &&
                   this->npcIndex && this->appearance && this->role <= 5 && this->catalogChecksum &&
                   !this->name.empty() && this->stats.Valid() &&
                   (this->stats.CanDie() == (this->role == static_cast<unsigned>(npc::Role::GUARD)));
        case Kind::NPC_TALK:
            return this->epoch && (this->entity & npc::ENTITY_BIT) && (this->entity & ~npc::ENTITY_BIT) &&
                   this->sequence;
        case Kind::NPC_DIALOGUE:
            return this->epoch && (this->entity & npc::ENTITY_BIT) && (this->entity & ~npc::ENTITY_BIT) &&
                   this->sequence && this->interaction && this->targetMap && !this->text.empty() &&
                   !this->optionLabel.empty();
        case Kind::NPC_WARP:
            return this->epoch && (this->entity & npc::ENTITY_BIT) && (this->entity & ~npc::ENTITY_BIT) &&
                   this->sequence && this->interaction && this->choice && this->choice <= 16;
        case Kind::NPC_CANCEL:
            return this->epoch && (this->entity & npc::ENTITY_BIT) && (this->entity & ~npc::ENTITY_BIT) &&
                   this->sequence && this->interaction;
        case Kind::NPC_RESULT:
            return this->epoch && (this->entity & npc::ENTITY_BIT) && (this->entity & ~npc::ENTITY_BIT) &&
                   this->sequence && this->error >= 1 && this->error <= 6;
        case Kind::EXIT_LAYOUT:
            return this->epoch && this->instance && this->exits.Valid();
        case Kind::EXIT_USE:
            return this->epoch && this->exitReceipt.run && this->exitReceipt.exitId && this->exitReceipt.sequence;
        case Kind::EXIT_RESULT:
            return this->epoch && this->error <= 5 &&
                   (this->error == 0
                        ? this->exitReceipt.Valid()
                        : this->exitReceipt.run && this->exitReceipt.exitId && this->exitReceipt.sequence &&
                              this->exitReceipt.type == dungeon::ExitType::NONE &&
                              this->exitReceipt.outcome == dungeon::ExitOutcome::NONE);
        case Kind::MOVE:
            return this->epoch && this->sequence && this->dx >= -20 && this->dx <= 20 && this->dy >= -20 &&
                   this->dy <= 20 && this->dx * this->dx + this->dy * this->dy <= 400;
        case Kind::DESPAWN:
            return this->epoch && this->entity;
        case Kind::PORTAL:
            return this->epoch && this->entity == 1;
        case Kind::ERROR_VALUE:
            return this->error != 0;
        }

        return false;
    }

    Bytes Encode() const
    {
        if (!Valid())
        {
            throw std::invalid_argument("invalid world message");
        }

        Bytes b;
        auto u32 = [&](std::uint32_t v)
        {
            binary::U32(b, v);
        };

        auto u64 = [&](std::uint64_t v)
        {
            u32(static_cast<std::uint32_t>(v >> 32));
            u32(static_cast<std::uint32_t>(v));
        };

        auto str = [&](const std::string& s)
        {
            u32(static_cast<std::uint32_t>(s.size()));
            b.insert(b.end(), s.begin(), s.end());
        };

        auto stat = [&]()
        {
            for (auto v : {this->stats.hp, this->stats.maxHp, this->stats.mp, this->stats.maxMp, this->stats.attack,
                           this->stats.defense, this->stats.evasion, this->stats.accuracy, this->stats.flags})
            {
                u32(v);
            }

        };

        u32(static_cast<std::uint32_t>(this->kind));
        u32(this->request);
        u64(this->epoch);
        switch (this->kind)
        {
        case Kind::MATCH_ACTION:
            u64(this->room);
            u64(this->sequence);
            u32(this->choice);
            break;
        case Kind::MATCH_STATE:
            u64(this->room);
            u64(this->revision);
            u32(this->targetMap);
            u32(static_cast<unsigned>(this->matchPhase));
            u32(this->countdownMs);
            u32(this->error);
            u32(static_cast<std::uint32_t>(this->members.size()));
            for (const auto& member : this->members)
            {
                u64(member.entity);
                u64(member.character);
                str(member.name);
                u32(member.race);
                u32(member.gender);
                u32(member.ready ? 1 : 0);
            }

            break;
        case Kind::PARTY:
            u64(this->entity);
            u64(this->partyId);
            break;
        case Kind::DUNGEON_STATE:
            u64(this->dungeonRun);
            u32(this->activeRoom);
            u32(this->clearedRooms);
            break;
        case Kind::COMBAT_REQUEST:
            u64(this->sequence);
            u32(this->combatEvent.skill);
            u64(this->combatEvent.target);
            u64(this->combatEvent.life);
            binary::Position(b, this->combatEvent.point);
            break;
        case Kind::COMBAT_CANCEL:
            u64(this->sequence);
            u64(this->combatEvent.action);
            break;
        case Kind::COMBAT_EVENT:
            u64(this->combatEvent.source);
            u64(this->combatEvent.target);
            u64(this->combatEvent.action);
            u64(this->combatEvent.life);
            u64(this->combatEvent.serverMs);
            u64(this->combatEvent.startMs);
            u64(this->combatEvent.endMs);
            u32(this->combatEvent.skill);
            u32(static_cast<unsigned>(this->combatEvent.phase));
            u32(this->combatEvent.value);
            u32(this->combatEvent.reason);
            binary::Position(b, this->combatEvent.origin);
            binary::Position(b, this->combatEvent.point);
            binary::I32(b, this->combatEvent.yaw);
            break;
        case Kind::ECONOMY_REQUEST:
            u32(this->economyReply.requestId);
            u64(this->entity);
            u64(this->interaction);
            economy::WriteRequest(b, this->economyRequest);
            break;
        case Kind::ECONOMY_SNAPSHOT:
        {
            Bytes reply;
            if (!this->economyReply.Serialize(reply))
            {
                throw std::invalid_argument("economy reply");
            }

            b.insert(b.end(), reply.begin(), reply.end());
            break;
        }

        case Kind::SHOP_OPEN:
            u64(this->entity);
            u64(this->sequence);
            break;
        case Kind::SHOP_CLOSE:
            u64(this->entity);
            u64(this->interaction);
            break;
        case Kind::SHOP_SNAPSHOT:
            u64(this->entity);
            u64(this->interaction);
            u32(this->targetMap);
            u64(this->catalogChecksum);
            u32(static_cast<std::uint32_t>(this->offers.size()));
            for (const auto& o : this->offers)
            {
                u32(o.id);
                u32(o.item);
                u64(o.price);
                u32(o.limit);
            }

            break;
        case Kind::EQUIPMENT:
            u64(this->entity);
            u64(this->sequence);
            u32(this->equipment[0]);
            u32(this->equipment[1]);
            break;
        case Kind::SELECT:
            u64(this->character);
            break;
        case Kind::SELECTED:
            u32(this->error);
            u64(this->character);
            str(this->ticket);
            str(this->name);
            u32(this->race);
            u32(this->gender);
            break;
        case Kind::AUTHORIZE:
            u64(this->character);
            str(this->ticket);
            str(this->uid);
            str(this->name);
            u32(this->race);
            u32(this->gender);
            break;
        case Kind::ENTER:
            str(this->ticket);
            break;
        case Kind::PREPARE:
        case Kind::READY:
            u64(this->instance);
            u32(this->map.id);
            u32(this->map.version);
            u64(this->map.checksum);
            u64(this->entity);
            break;
        case Kind::SPAWN:
            u64(this->character);
            str(this->name);
            u32(this->race);
            u32(this->gender);
            stat();
            [[fallthrough]];
        case Kind::STATE:
            u64(this->entity);
            u64(this->sequence);
            u64(this->tick);
            binary::Position(b, this->feet);
            binary::I32(b, this->yaw);
            break;
        case Kind::MOVE:
            u64(this->sequence);
            binary::I32(b, this->dx);
            binary::I32(b, this->dy);
            binary::I32(b, this->yaw);
            break;
        case Kind::NPC_SPAWN:
            u64(this->entity);
            u32(this->npcIndex);
            u32(this->appearance);
            u32(this->role);
            u32(this->roleData);
            u64(this->catalogChecksum);
            str(this->name);
            binary::Position(b, this->feet);
            binary::I32(b, this->yaw);
            stat();
            break;
        case Kind::ENTITY_STATS:
            u64(this->entity);
            stat();
            break;
        case Kind::NPC_TALK:
            u64(this->entity);
            u64(this->sequence);
            break;
        case Kind::NPC_DIALOGUE:
            u64(this->entity);
            u64(this->sequence);
            u64(this->interaction);
            u32(this->targetMap);
            str(this->text);
            str(this->optionLabel);
            break;
        case Kind::NPC_WARP:
            u64(this->entity);
            u64(this->sequence);
            u64(this->interaction);
            u32(this->choice);
            break;
        case Kind::NPC_CANCEL:
            u64(this->entity);
            u64(this->sequence);
            u64(this->interaction);
            break;
        case Kind::NPC_RESULT:
            u64(this->entity);
            u64(this->sequence);
            u64(this->interaction);
            u32(this->error);
            break;
        case Kind::EXIT_LAYOUT:
            u64(this->instance);
            u64(this->exits.run);
            u32(this->exits.count);
            for (unsigned i = 0; i < this->exits.count; ++i)
            {
                const auto& z = this->exits.zones[i];
                u32(z.id);
                u32(static_cast<unsigned>(z.type));
                u32(static_cast<unsigned>(z.outcome));
                u32(z.radius);
                binary::Position(b, z.feet);
            }

            break;
        case Kind::EXIT_USE:
            u64(this->exitReceipt.run);
            u32(this->exitReceipt.exitId);
            u64(this->exitReceipt.sequence);
            break;
        case Kind::EXIT_RESULT:
            u64(this->exitReceipt.sourceInstance);
            u64(this->exitReceipt.run);
            u64(this->exitReceipt.sequence);
            u32(this->exitReceipt.exitId);
            u32(static_cast<unsigned>(this->exitReceipt.type));
            u32(static_cast<unsigned>(this->exitReceipt.outcome));
            u32(this->error);
            break;
        case Kind::DESPAWN:
        case Kind::PORTAL:
            u64(this->entity);
            break;
        case Kind::ERROR_VALUE:
            u32(this->error);
            break;
        }

        return b;
    }

    static Message Decode(const std::uint8_t* data, std::size_t size)
    {
        if (!data || size < 16 || size > 1024)
        {
            throw std::invalid_argument("world message size");
        }

        Bytes b(data, data + size);
        binary::Reader r(b, b.size());
        Message m;
        auto u64 = [&]()
        {
            const auto HI = r.U32(), LO = r.U32();
            return (std::uint64_t(HI) << 32) | LO;
        };

        auto str = [&](std::uint32_t limit = 128)
        {
            const auto N = r.U32();
            if (N > limit)
            {
                throw std::invalid_argument("world string limit");
            }

            const auto BYTES = r.Blob(N);
            return std::string(BYTES.begin(), BYTES.end());
        };

        auto stat = [&]()
        {
            m.stats = {r.U32(), r.U32(), r.U32(), r.U32(), r.U32(), r.U32(), r.U32(), r.U32(), r.U32()};
        };

        m.kind = static_cast<Kind>(r.U32());
        m.request = r.U32();
        m.epoch = u64();
        switch (m.kind)
        {
        case Kind::MATCH_ACTION:
            m.room = u64();
            m.sequence = u64();
            m.choice = r.U32();
            break;
        case Kind::MATCH_STATE:
        {
            m.room = u64();
            m.revision = u64();
            m.targetMap = r.U32();
            m.matchPhase = static_cast<party::MatchPhase>(r.U32());
            m.countdownMs = r.U32();
            m.error = r.U32();
            const auto COUNT = r.U32();
            if (COUNT > party::MAX_MEMBERS)
            {
                throw std::invalid_argument("match member limit");
            }

            for (unsigned i = 0; i < COUNT; ++i)
            {
                party::Member member;
                member.entity = u64();
                member.character = u64();
                member.name = str();
                member.race = r.U32();
                member.gender = r.U32();
                const auto READY = r.U32();
                if (READY > 1)
                {
                    throw std::invalid_argument("match ready flag");
                }

                member.ready = READY != 0;
                m.members.push_back(std::move(member));
            }

            break;
        }

        case Kind::PARTY:
            m.entity = u64();
            m.partyId = u64();
            break;
        case Kind::DUNGEON_STATE:
            m.dungeonRun = u64();
            m.activeRoom = r.U32();
            m.clearedRooms = r.U32();
            break;
        case Kind::COMBAT_REQUEST:
            m.sequence = u64();
            m.combatEvent.skill = r.U32();
            m.combatEvent.target = u64();
            m.combatEvent.life = u64();
            m.combatEvent.point = WorldPosition{r.I32(), r.I32(), r.I32()};
            break;
        case Kind::COMBAT_CANCEL:
            m.sequence = u64();
            m.combatEvent.action = u64();
            break;
        case Kind::COMBAT_EVENT:
            m.combatEvent.source = u64();
            m.combatEvent.target = u64();
            m.combatEvent.action = u64();
            m.combatEvent.life = u64();
            m.combatEvent.serverMs = u64();
            m.combatEvent.startMs = u64();
            m.combatEvent.endMs = u64();
            m.combatEvent.skill = r.U32();
            m.combatEvent.phase = static_cast<combat::Phase>(r.U32());
            m.combatEvent.value = r.U32();
            m.combatEvent.reason = r.U32();
            m.combatEvent.origin = WorldPosition{r.I32(), r.I32(), r.I32()};
            m.combatEvent.point = WorldPosition{r.I32(), r.I32(), r.I32()};
            m.combatEvent.yaw = r.I32();
            break;
        case Kind::ECONOMY_REQUEST:
            m.economyReply.requestId = r.U32();
            m.entity = u64();
            m.interaction = u64();
            m.economyRequest = economy::ReadRequest(r);
            break;
        case Kind::ECONOMY_SNAPSHOT:
        {
            const auto REPLY = r.Blob(static_cast<std::uint32_t>(size - 16));
            if (!m.economyReply.Deserialize(REPLY.data(), REPLY.size()))
            {
                throw std::invalid_argument("economy reply");
            }

            break;
        }

        case Kind::SHOP_OPEN:
            m.entity = u64();
            m.sequence = u64();
            break;
        case Kind::SHOP_CLOSE:
            m.entity = u64();
            m.interaction = u64();
            break;
        case Kind::SHOP_SNAPSHOT:
        {
            m.entity = u64();
            m.interaction = u64();
            m.targetMap = r.U32();
            m.catalogChecksum = u64();
            const auto N = r.U32();
            if (N > 8)
            {
                throw std::invalid_argument("shop offer budget");
            }

            std::unordered_set<std::uint32_t> ids;
            for (std::uint32_t i = 0; i < N; ++i)
            {
                economy::Offer o;
                o.shop = m.targetMap;
                o.id = r.U32();
                o.item = r.U32();
                o.price = u64();
                o.limit = r.U32();
                o.enabled = true;
                if (!o.id || !o.item || !o.price || o.price > economy::GOLD_LIMIT || !o.limit || o.limit > 999 ||
                    !ids.insert(o.id).second)
                {
                    throw std::invalid_argument("invalid shop offer");
                }

                m.offers.push_back(o);
            }

            break;
        }

        case Kind::EQUIPMENT:
            m.entity = u64();
            m.sequence = u64();
            m.equipment = {r.U32(), r.U32()};
            break;
        case Kind::SELECT:
            m.character = u64();
            break;
        case Kind::SELECTED:
            m.error = r.U32();
            m.character = u64();
            m.ticket = str();
            m.name = str();
            m.race = r.U32();
            m.gender = r.U32();
            break;
        case Kind::AUTHORIZE:
            m.character = u64();
            m.ticket = str();
            m.uid = str();
            m.name = str();
            m.race = r.U32();
            m.gender = r.U32();
            break;
        case Kind::ENTER:
            m.ticket = str();
            break;
        case Kind::PREPARE:
        case Kind::READY:
            m.instance = u64();
            m.map.id = r.U32();
            m.map.version = r.U32();
            m.map.checksum = u64();
            m.entity = u64();
            break;
        case Kind::SPAWN:
            m.character = u64();
            m.name = str();
            m.race = r.U32();
            m.gender = r.U32();
            stat();
            [[fallthrough]];
        case Kind::STATE:
            m.entity = u64();
            m.sequence = u64();
            m.tick = u64();
            m.feet = r.Position();
            m.yaw = r.I32();
            break;
        case Kind::MOVE:
            m.sequence = u64();
            m.dx = r.I32();
            m.dy = r.I32();
            m.yaw = r.I32();
            break;
        case Kind::NPC_SPAWN:
            m.entity = u64();
            m.npcIndex = r.U32();
            m.appearance = r.U32();
            m.role = r.U32();
            m.roleData = r.U32();
            m.catalogChecksum = u64();
            m.name = str();
            m.feet = r.Position();
            m.yaw = r.I32();
            stat();
            break;
        case Kind::ENTITY_STATS:
            m.entity = u64();
            stat();
            break;
        case Kind::NPC_TALK:
            m.entity = u64();
            m.sequence = u64();
            break;
        case Kind::NPC_DIALOGUE:
            m.entity = u64();
            m.sequence = u64();
            m.interaction = u64();
            m.targetMap = r.U32();
            m.text = str(512);
            m.optionLabel = str();
            break;
        case Kind::NPC_WARP:
            m.entity = u64();
            m.sequence = u64();
            m.interaction = u64();
            m.choice = r.U32();
            break;
        case Kind::NPC_CANCEL:
            m.entity = u64();
            m.sequence = u64();
            m.interaction = u64();
            break;
        case Kind::NPC_RESULT:
            m.entity = u64();
            m.sequence = u64();
            m.interaction = u64();
            m.error = r.U32();
            break;
        case Kind::EXIT_LAYOUT:
            m.instance = u64();
            m.exits.run = u64();
            m.exits.count = r.U32();
            if (m.exits.count > 3)
            {
                throw std::invalid_argument("exit layout count");
            }

            for (unsigned i = 0; i < m.exits.count; ++i)
            {
                auto& z = m.exits.zones[i];
                z.id = r.U32();
                z.type = static_cast<dungeon::ExitType>(r.U32());
                z.outcome = static_cast<dungeon::ExitOutcome>(r.U32());
                z.radius = r.U32();
                z.feet = r.Position();
            }

            break;
        case Kind::EXIT_USE:
            m.exitReceipt.run = u64();
            m.exitReceipt.exitId = r.U32();
            m.exitReceipt.sequence = u64();
            break;
        case Kind::EXIT_RESULT:
            m.exitReceipt.sourceInstance = u64();
            m.exitReceipt.run = u64();
            m.exitReceipt.sequence = u64();
            m.exitReceipt.exitId = r.U32();
            m.exitReceipt.type = static_cast<dungeon::ExitType>(r.U32());
            m.exitReceipt.outcome = static_cast<dungeon::ExitOutcome>(r.U32());
            m.error = r.U32();
            break;
        case Kind::DESPAWN:
        case Kind::PORTAL:
            m.entity = u64();
            break;
        case Kind::ERROR_VALUE:
            m.error = r.U32();
            break;
        default:
            throw std::invalid_argument("unknown world message kind");
        }

        if (!r.Done() || !m.Valid())
        {
            throw std::invalid_argument("invalid world message fields");
        }

        return m;
    }

};

}

