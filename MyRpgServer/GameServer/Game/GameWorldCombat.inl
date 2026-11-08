// Fixed combat bodies are shared with UE; presentation bounds never authorize damage.
unsigned GameWorld::CombatRadius(std::uint64_t id) const
{
    if (!(id & shared_world::npc::ENTITY_BIT))
    {
        return shared_world::combat::PLAYER_RADIUS;
    }

    if (this->mapContent && this->mapContent->combat)
    {
        for (const auto& n : this->npcSpawns)
        {
            if (n.entity == id)
            {
                const auto IT = this->mapContent->combat->monsters.find(n.npcIndex);
                if (IT != this->mapContent->combat->monsters.end())
                {
                    return IT->second.bodyRadius;
                }

            }

        }

    }

    return shared_world::combat::DEFAULT_NPC_RADIUS;
}

unsigned GameWorld::CombatHeight(std::uint64_t id) const
{
    if (this->mapContent && this->mapContent->combat)
    {
        for (const auto& n : this->npcSpawns)
        {
            if (n.entity == id)
            {
                const auto IT = this->mapContent->combat->monsters.find(n.npcIndex);
                if (IT != this->mapContent->combat->monsters.end())
                {
                    return IT->second.bodyHeight;
                }

            }

        }

    }

    return 160;
}

bool GameWorld::CombatBodyClear(std::uint64_t source, shared_world::WorldPosition from,
                                shared_world::WorldPosition to) const
{
    using namespace shared_world;
    using namespace combat;
    if (!this->mapContent || !this->mapContent->combat)
    {
        return true;
    }

    for (const auto& n : this->npcSpawns)
    {
        if (n.entity != source && !n.stats.IsDead())
        {
            const auto IT = this->mapContent->combat->monsters.find(n.npcIndex);
            if (IT != this->mapContent->combat->monsters.end() && IT->second.solidRadius &&
                !BodyMoveClear(from, to, n.feet, IT->second.solidRadius + CombatRadius(source), IT->second.bodyHeight))
            {
                return false;
            }

        }

    }

    // Only configured solid NPCs block walking; preserve ordinary goblin behavior.
    if (source & npc::ENTITY_BIT)
    {
        for (const auto& n : this->npcSpawns)
        {
            if (n.entity == source)
            {
                const auto IT = this->mapContent->combat->monsters.find(n.npcIndex);
                if (IT != this->mapContent->combat->monsters.end() && IT->second.solidRadius)
                {
                    for (const auto& [id, p] : this->players)
                    {
                        if (p.ready && !p.stats.IsDead() &&
                            !BodyMoveClear(from, to, p.feet, IT->second.solidRadius + PLAYER_RADIUS, 160))
                        {
                            return false;
                        }

                    }

                }

            }

        }

    }

    return true;
}

shared_world::WorldPosition GameWorld::MovePlayer(shared_world::WorldPosition from, int dx, int dy)
{
    auto attempt = [&](int x, int y)
    {
        const auto NEXT = this->residency->Move(from, x, y).position;
        return (!IsMaze() || shared_world::dungeon::DoorClear(from, NEXT, this->activeRoom)) &&
                       CombatBodyClear(0, from, NEXT)
                   ? NEXT
                   : from;
    };

    const auto FULL = attempt(dx, dy);
    if (FULL != from || !dx || !dy)
    {
        return FULL;
    }

    const auto X_VALUE = attempt(dx, 0), Y = attempt(0, dy);
    return shared_world::DistanceSquared2D(from, X_VALUE) >= shared_world::DistanceSquared2D(from, Y) ? X_VALUE : Y;
}

// Included by GameWorld.cpp. Every function executes on the map owner.
GameWorld::CombatState* GameWorld::FindCombat(std::uint64_t id)
{
    if (!this->mapContent || !this->mapContent->combat)
    {
        return nullptr;
    }

    if (id & shared_world::npc::ENTITY_BIT)
    {
        for (const auto& n : this->npcSpawns)
        {
            if (n.entity == id && this->mapContent->combat->monsters.count(n.npcIndex))
            {
                return &this->npcCombat[id];
            }

        }

        return nullptr;
    }

    const auto P = this->players.find(id);
    return P != this->players.end() && P->second.ready ? &P->second.combat : nullptr;
}

shared_world::WorldPosition* GameWorld::FindFeet(std::uint64_t id)
{
    if (id & shared_world::npc::ENTITY_BIT)
    {
        for (auto& n : this->npcSpawns)
        {
            if (n.entity == id)
            {
                return &n.feet;
            }

        }

        return nullptr;
    }

    const auto P = this->players.find(id);
    return P != this->players.end() && P->second.ready ? &P->second.feet : nullptr;
}

void GameWorld::SendCombat(shared_world::combat::Event e, const SessionPtr& recipient)
{
    e.serverMs = this->combatMs;
    shared_world::net::Message m;
    m.kind = shared_world::net::Kind::COMBAT_EVENT;
    m.combatEvent = e;
    if (recipient)
    {
        Send(recipient, m);
    }
    else
    {
        Broadcast(m);
    }

}

bool GameWorld::CombatVisible(shared_world::WorldPosition a, shared_world::WorldPosition b)
{
    if (IsMaze() && !shared_world::dungeon::DoorClear(a, b, this->activeRoom, 0))
    {
        return false;
    }

    a.z += 80;
    b.z += 80;
    if (a == b)
    {
        return true;
    }

    const auto WORLD = this->residency->Get(a);
    const auto OTHER = this->residency->Get(b);
    if (!WORLD || WORLD != OTHER)
    {
        return false; // Fail closed for unavailable/cross-chunk combat geometry.
    }

    return WORLD->RaycastWorld(a, b).status == shared_world::QueryStatus::CLEAR;
}

void GameWorld::CancelCombat(std::uint64_t id)
{
    using namespace shared_world::combat;
    auto* state = FindCombat(id);
    if (!state || !state->cast.action)
    {
        return;
    }

    auto e = state->cast;
    state->cast = {};
    e.phase = Phase::CANCELLED;
    e.reason = static_cast<unsigned>(Reject::CANCELLED);
    SendCombat(e);
}

shared_world::combat::Reject GameWorld::StartAttack(std::uint64_t id, std::uint32_t skill, std::uint64_t target,
                                                    std::uint64_t life, shared_world::WorldPosition point)
{
    using namespace shared_world;
    using namespace combat;
    auto* state = FindCombat(id);
    if (const auto PLAYER = this->players.find(id); PLAYER != this->players.end() && PLAYER->second.matchRoom)
    {
        return Reject::UNAVAILABLE;
    }

    auto* stats = FindStats(id);
    auto* feet = FindFeet(id);
    if (!state || !stats || !feet)
    {
        return Reject::UNAVAILABLE;
    }

    if (stats->IsDead())
    {
        return Reject::DEAD;
    }

    const auto FOUND = this->mapContent->combat->abilities.find(skill);
    if (FOUND == this->mapContent->combat->abilities.end())
    {
        return Reject::UNAVAILABLE;
    }

    auto a = FOUND->second;
    const skills::Definition* definition = nullptr;
    if (!(id & npc::ENTITY_BIT) && skill > 2)
    {
        const auto D = this->mapContent->combat->skills.definitions.find(skill);
        if (D == this->mapContent->combat->skills.definitions.end())
        {
            return Reject::UNAVAILABLE;
        }

        definition = &D->second;
        const auto& player = this->players.at(id);
        const unsigned RANK = this->economyCatalog ? (player.economyReady ? player.economy.skills.ranks[skill] : 0)
                                                   : definition->initialRank;
        if (!RANK)
        {
            return Reject::UNAVAILABLE;
        }

        a = Ranked(a, this->mapContent->combat->skills.ranks.at({skill, RANK}));
        if (definition->aim == skills::Aim::TARGET && !target)
        {
            return Reject::TARGET;
        }

        if (definition->aim == skills::Aim::SELF)
        {
            target = 0;
            point = *feet;
        }

        if (definition->aim == skills::Aim::DIRECTION)
        {
            target = 0;
            const auto END = DirectedPoint(*feet, point, a.range);
            if (!END)
            {
                return Reject::TARGET;
            }

            point = *END;
        }

    }

    if (state->cast.action || state->busyUntil > this->combatMs)
    {
        return Reject::BUSY;
    }

    if (state->cooldowns[a.id] > this->combatMs)
    {
        return Reject::COOLDOWN;
    }

    if (stats->mp < a.mp)
    {
        return Reject::MP;
    }

    const bool NPC = (id & npc::ENTITY_BIT) != 0;
    if (NPC)
    {
        const auto SPAWN = std::find_if(this->npcSpawns.begin(), this->npcSpawns.end(),
                                        [&](const auto& n)
                                        {
                                            return n.entity == id;
                                        });

        const auto& monster = this->mapContent->combat->monsters.at(SPAWN->npcIndex);
        if (!monster.basic && skill <= 2)
        {
            return Reject::UNAVAILABLE;
        }

    }

    if ((a.shape == Shape::MELEE || a.shape == Shape::PROJECTILE) && !target && !NPC)
    {
        const auto DIRECTED = DirectedPoint(*feet, point, a.range);
        if (!DIRECTED)
        {
            return Reject::TARGET;
        }

        if (!this->residency->Get(*feet))
        {
            return Reject::BLOCKED;
        }

        point = *DIRECTED;
        life = state->life;
    }
    else if (a.shape == Shape::MELEE || a.shape == Shape::PROJECTILE)
    {
        auto* ts = FindStats(target);
        auto* tf = FindFeet(target);
        auto* tc = FindCombat(target);
        if (!ts || !tf || !tc || !ts->CanBeAttacked() || !CanDamage(id, target) || tc->life != life)
        {
            return Reject::TARGET;
        }

        const unsigned REACH = a.shape == Shape::MELEE
                                   ? MeleeCenterRange(a.range, CombatRadius(id), CombatRadius(target))
                                   : a.range + std::max(0, int(CombatRadius(id)) - int(DEFAULT_NPC_RADIUS)) +
                                         std::max(0, int(CombatRadius(target)) - int(DEFAULT_NPC_RADIUS));
        if (!InRange(*feet, *tf, REACH))
        {
            return Reject::RANGE;
        }

        if (!CombatVisible(*feet, *tf))
        {
            return Reject::BLOCKED;
        }

        point = *tf;
    }
    else if (a.shape == Shape::CIRCLE)
    {
        if (!InRange(*feet, point, a.range))
        {
            return Reject::RANGE;
        }

        const auto WORLD = this->residency->Get(point);
        if (!WORLD)
        {
            return Reject::BLOCKED;
        }

        const auto GROUND = GroundSupport(*WORLD, point);
        if (GROUND.status != QueryStatus::HIT || std::abs(GROUND.position.z - point.z) > 25 ||
            !CombatVisible(*feet, GROUND.position))
        {
            return Reject::BLOCKED;
        }

        point = GROUND.position;
        life = state->life;
    }

    if (this->nextAction == UINT64_MAX)
    {
        return Reject::UNAVAILABLE;
    }

    Event e;
    e.source = id;
    e.target = target;
    e.action = ++this->nextAction;
    e.skill = skill;
    e.life = life;
    e.origin = *feet;
    e.point = point;
    e.phase = Phase::STARTED;
    e.startMs = this->combatMs;
    e.endMs = this->combatMs + a.castMs;
    e.yaw = static_cast<std::int32_t>(std::atan2(double(point.y) - feet->y, double(point.x) - feet->x) * 180000 /
                                      3.141592653589793);
    if (a.shape == Shape::CONE)
    {
        e.point = *feet;
    }

    state->cast = e;
    state->castAbility = a;
    SendCombat(e);
    return Reject::NONE;
}

void GameWorld::CombatInput(const SessionPtr& session, Player& p, const shared_world::net::Message& m)
{
    using namespace shared_world::combat;
    if (m.sequence <= p.combat.lastRequest)
    {
        return; // Duplicate/stale requests never create a second attack.
    }

    p.combat.lastRequest = m.sequence;
    Reject reason = Reject::UNAVAILABLE;
    if (this->mapContent->combat && p.ready)
    {
        if (m.kind == shared_world::net::Kind::COMBAT_CANCEL)
        {
            if (!m.combatEvent.action || m.combatEvent.action == p.combat.cast.action)
            {
                CancelCombat(session->GetId());
            }

            return;
        }

        if (this->combatMs < p.combat.allowedRequest)
        {
            reason = Reject::RATE_LIMITED;
        }
        else
        {
            p.combat.allowedRequest = this->combatMs + 100;
            if (p.dialogueId || p.shopToken || p.economyPending || p.economyUnknown)
            {
                reason = Reject::BUSY;
            }
            else
            {
                reason = StartAttack(session->GetId(), m.combatEvent.skill, m.combatEvent.target, m.combatEvent.life,
                                     m.combatEvent.point);
            }

        }

    }

    if (reason != Reject::NONE)
    {
        Event e;
        e.source = session->GetId();
        e.action = m.sequence;
        e.skill = m.combatEvent.skill;
        e.phase = Phase::REJECTED;
        e.reason = static_cast<unsigned>(reason);
        SendCombat(e, session);
    }

}

void GameWorld::CombatHit(const shared_world::combat::Event& e, std::uint64_t target,
                          const shared_world::EntityStats& attacker,
                          const shared_world::combat::Ability* overrideAbility)
{
    using namespace shared_world::combat;
    auto* ts = FindStats(target);
    auto* tf = FindFeet(target);
    if (!ts || !tf || !ts->CanBeAttacked() || !CanDamage(e.source, target))
    {
        return;
    }

    const auto* tc = FindCombat(target);
    if (!tc)
    {
        return;
    }

    const auto& a = overrideAbility ? *overrideAbility : this->mapContent->combat->abilities.at(e.skill);
    const bool HIT = std::uniform_int_distribution<unsigned>(1, 100)(this->combatRandom) <= HitChance(attacker, *ts);
    const auto DAMAGE = HIT ? Damage(attacker, *ts, a) : 0;
    auto event = e;
    event.target = target;
    event.life = tc->life;
    event.phase = Phase::HIT;
    event.point = *tf;
    event.value = std::min(ts->hp, DAMAGE);
    // Place feedback at the visible near side rather than inside a giant body.
    if (CombatRadius(target) > DEFAULT_NPC_RADIUS)
    {
        const double DX = double(e.origin.x) - tf->x, DY = double(e.origin.y) - tf->y, LENGTH = std::hypot(DX, DY);
        if (LENGTH > 1)
        {
            event.point.x += static_cast<int>(std::round(DX / LENGTH * CombatRadius(target)));
            event.point.y += static_cast<int>(std::round(DY / LENGTH * CombatRadius(target)));
        }

    }

    SendCombat(event);
    if (DAMAGE)
    {
        ApplyDamage(target, DAMAGE);
    }

}

void GameWorld::CombatDeath(std::uint64_t id)
{
    using namespace shared_world::combat;
    auto* state = FindCombat(id);
    if (!state || state->respawnAt)
    {
        return;
    }

    CancelCombat(id);
    std::uint32_t delay = this->mapContent->combat->playerRespawnMs;
    if (id & shared_world::npc::ENTITY_BIT)
    {
        for (const auto& n : this->npcSpawns)
        {
            if (n.entity == id)
            {
                delay = this->mapContent->combat->monsters.at(n.npcIndex).respawnMs;
            }

        }

    }

    state->respawnAt = this->combatMs + delay;
    Event e;
    e.source = id;
    e.life = state->life;
    e.phase = Phase::DEATH;
    e.startMs = this->combatMs;
    e.endMs = state->respawnAt;
    e.point = *FindFeet(id);
    SendCombat(e);
}

void GameWorld::CombatSnapshot(const SessionPtr& session)
{
    using namespace shared_world::combat;
    if (!this->mapContent->combat)
    {
        return;
    }

    Event clock;
    clock.phase = Phase::CLOCK;
    clock.source = session->GetId();
    clock.action = this->mapContent->combat->checksum;
    SendCombat(clock, session);
    auto snapshot = [&](std::uint64_t id)
    {
        auto* state = FindCombat(id);
        if (!state)
        {
            return;
        }

        Event e;
        e.source = id;
        e.life = state->life;
        e.point = *FindFeet(id);
        e.phase = state->respawnAt ? Phase::DEATH : Phase::RESPAWN;
        e.endMs = state->respawnAt;
        SendCombat(e, session);
        if (state->cast.action)
        {
            SendCombat(state->cast, session);
        }

    };

    for (const auto& n : this->npcSpawns)
    {
        snapshot(n.entity);
    }

    for (const auto& [id, p] : this->players)
    {
        if (p.ready)
        {
            snapshot(id);
        }

    }

    for (const auto& projectile : this->projectiles)
    {
        auto e = projectile.event;
        e.phase = Phase::PROJECTILE;
        e.origin = projectile.feet;
        SendCombat(e, session);
    }

}

void GameWorld::TickCombat(double delta)
{
    using namespace shared_world;
    using namespace combat;
    using namespace net;
    if (!this->mapContent || !this->mapContent->combat)
    {
        return;
    }

    this->combatRemainder += std::clamp(delta, 0.0, 0.25) * 1000;
    const auto ELAPSED = static_cast<std::uint64_t>(this->combatRemainder);
    this->combatRemainder -= double(ELAPSED);
    this->combatMs += ELAPSED;
    if (this->players.empty())
    {
        this->projectiles.clear();
        return;
    }

    // Respawn retains equipment-derived player stats. NPC life increments invalidate old melee targets.
    auto respawn = [&](std::uint64_t id, EntityStats& stats, WorldPosition& feet, WorldPosition home)
    {
        auto* s = FindCombat(id);
        if (!s || !s->respawnAt || this->combatMs < s->respawnAt)
        {
            return;
        }

        const auto NEXT_LIFE = s->life + 1;
        if (id & npc::ENTITY_BIT)
        {
            *s = CombatState{};
        }

        s->respawnAt = 0;
        s->life = NEXT_LIFE;
        s->busyUntil = 0;
        s->cast = {};
        feet = home;
        stats.hp = stats.maxHp;
        stats.mp = stats.maxMp;
        BroadcastStats(id, stats);
        Event e;
        e.source = id;
        e.life = s->life;
        e.phase = Phase::RESPAWN;
        e.point = feet;
        SendCombat(e);
        Message move;
        move.kind = Kind::STATE;
        move.entity = id;
        move.feet = feet;
        move.tick = this->tickNumber;
        if (!(id & npc::ENTITY_BIT))
        {
            move.sequence = this->players.at(id).ack;
        }

        Broadcast(move);
    };

    for (auto& [id, p] : this->players)
    {
        if (p.ready)
        {
            respawn(id, p.stats, p.feet, this->mapContent->manifest.spawns.front().feet);
        }

    }

    for (auto& n : this->npcSpawns)
    {
        const auto RULE = this->mapContent->combat->monsters.find(n.npcIndex);
        if (RULE == this->mapContent->combat->monsters.end())
        {
            continue;
        }

        const auto& def = RULE->second;
        const auto PLACEMENT =
            std::find_if(this->mapContent->npcs->placements.begin(), this->mapContent->npcs->placements.end(),
                         [&](const auto& v)
                         {
                             return v.Entity() == n.entity;
                         });

        if (PLACEMENT == this->mapContent->npcs->placements.end())
        {
            continue;
        }

        const auto HOME = IsMaze() ? this->npcHomes.at(n.entity) : PLACEMENT->feet;
        if (!IsMaze())
        {
            respawn(n.entity, n.stats, n.feet, HOME);
        }

        if (n.stats.IsDead())
        {
            continue;
        }

        auto* state = FindCombat(n.entity);
        if (!state || state->cast.action || state->busyUntil > this->combatMs)
        {
            continue;
        }

        std::uint64_t target = 0;
        auto distance = std::uint64_t(def.aggro) * def.aggro + 1;
        for (const auto& [id, p] : this->players)
        {
            if (p.ready && p.stats.CanBeAttacked())
            {
                const auto D = DistanceSquared2D(n.feet, p.feet);
                if (D < distance)
                {
                    target = id;
                    distance = D;
                }

            }

        }

        if (!InRange(n.feet, HOME, def.leash))
        {
            state->returning = true;
        }

        if (state->returning && InRange(n.feet, HOME, 30))
        {
            state->returning = false;
        }

        if (state->returning)
        {
            target = 0;
        }

        if (state->target != target)
        {
            state->target = target;
            state->pattern = 0;
            state->stepUntil = 0;
            state->step = 0;
        }

        auto move = [&](WorldPosition goal)
        {
            const double GX = double(goal.x) - n.feet.x, GY = double(goal.y) - n.feet.y;
            const double LENGTH = std::max(20.0, std::sqrt(GX * GX + GY * GY));
            const double SCALE = std::min(1.0, double(def.speed) * std::clamp(delta, 0.0, 0.1) / 20);
            const int DX = static_cast<int>(GX / LENGTH * 20 * SCALE), DY = static_cast<int>(GY / LENGTH * 20 * SCALE);
            const auto NEXT = this->residency->Move(n.feet, DX, DY).position;
            const int MARGIN =
                def.bodyRadius <= DEFAULT_NPC_RADIUS
                    ? 34
                    : static_cast<int>(def.bodyRadius * 160 /
                                       100); // Keep the scaled boss body inside the arena, not just its pivot.
            if (IsMaze() && (shared_world::dungeon::RoomAt(NEXT, MARGIN) != this->activeRoom ||
                             !shared_world::dungeon::DoorClear(n.feet, NEXT, this->activeRoom)))
            {
                return;
            }

            if (!CombatBodyClear(n.entity, n.feet, NEXT))
            {
                return;
            }

            if (NEXT != n.feet)
            {
                n.yaw = static_cast<int>(std::atan2(double(NEXT.y) - n.feet.y, double(NEXT.x) - n.feet.x) * 180000 /
                                         3.141592653589793);
                n.feet = NEXT;
                Message m;
                m.kind = Kind::STATE;
                m.entity = n.entity;
                m.feet = n.feet;
                m.yaw = n.yaw;
                m.tick = this->tickNumber;
                Broadcast(m);
            }

        };

        if (state->returning)
        {
            state->pattern = 0;
            move(HOME);
            continue;
        }

        if (!state->pattern)
        {
            unsigned total = 0;
            auto eligible = [&](const Pattern& p)
            {
                if (p.npc != n.npcIndex || p.state != (target ? 1U : 0U))
                {
                    return false;
                }

                for (const auto& step : this->mapContent->combat->steps.at(p.id))
                {
                    if (step.action == Action::ATTACK)
                    {
                        const auto& a = this->mapContent->combat->abilities.at(step.ability);
                        if (n.stats.mp < a.mp || state->cooldowns[a.id] > this->combatMs)
                        {
                            return false;
                        }

                    }

                }

                return true;
            };

            for (const auto& p : this->mapContent->combat->patterns)
            {
                if (eligible(p))
                {
                    total += p.weight;
                }

            }

            if (!total)
            {
                continue;
            }

            auto pick = std::uniform_int_distribution<unsigned>(1, total)(this->combatRandom);
            for (const auto& p : this->mapContent->combat->patterns)
            {
                if (eligible(p))
                {
                    if (pick <= p.weight)
                    {
                        state->pattern = p.id;
                        state->step = 0;
                        state->stepUntil = 0;
                        break;
                    }

                    pick -= p.weight;
                }

            }

        }

        const auto& steps = this->mapContent->combat->steps.at(state->pattern);
        if (state->step >= steps.size())
        {
            state->pattern = 0;
            continue;
        }

        const auto& step = steps[state->step];
        auto advance = [&]
        {
            ++state->step;
            state->stepUntil = 0;
            if (state->step >= steps.size())
            {
                state->pattern = 0;
            }

        };

        if (!state->stepUntil)
        {
            state->stepUntil = this->combatMs + step.durationMs;
            if (step.action == Action::WANDER)
            {
                const double ANGLE = std::uniform_real_distribution<double>(0, 6.283185307)(this->combatRandom);
                const double RADIUS = std::uniform_real_distribution<double>(0, step.distance)(this->combatRandom);
                state->wander = HOME;
                state->wander.x += static_cast<int>(std::cos(ANGLE) * RADIUS);
                state->wander.y += static_cast<int>(std::sin(ANGLE) * RADIUS);
            }

        }

        if (this->combatMs >= state->stepUntil)
        {
            advance();
            continue;
        }

        if (step.action == Action::WAIT)
        {
            continue;
        }

        if (step.action == Action::WANDER)
        {
            if (InRange(n.feet, state->wander, 20))
            {
                advance();
            }
            else
            {
                move(state->wander);
            }

            continue;
        }

        if (!target)
        {
            state->pattern = 0;
            continue;
        }

        const auto GOAL = this->players.at(target).feet;
        if (step.action == Action::APPROACH)
        {
            if (InRange(n.feet, GOAL, MeleeCenterRange(step.distance, def.bodyRadius, PLAYER_RADIUS)))
            {
                advance();
            }
            else
            {
                move(GOAL);
            }

            continue;
        }

        const auto REJECTION = StartAttack(n.entity, step.ability, target, this->players.at(target).combat.life, GOAL);
        if (REJECTION == Reject::NONE)
        {
            n.yaw = state->cast.yaw;
            advance();
        }
        else if (REJECTION != Reject::COOLDOWN && REJECTION != Reject::BUSY)
        {
            state->pattern = 0;
        }

    }

    auto finish = [&](std::uint64_t id)
    {
        auto* state = FindCombat(id);
        auto* stats = FindStats(id);
        if (!state || !stats || !state->cast.action || state->cast.endMs > this->combatMs)
        {
            return;
        }

        auto e = state->cast;
        state->cast = {};
        const auto A = state->castAbility;
        const auto SKILL = this->mapContent->combat->skills.definitions.find(e.skill);
        const auto* definition = !(id & npc::ENTITY_BIT) && SKILL != this->mapContent->combat->skills.definitions.end()
                                     ? &SKILL->second
                                     : nullptr;
        if (stats->IsDead() || stats->mp < A.mp)
        {
            e.phase = Phase::CANCELLED;
            SendCombat(e);
            return;
        }

        // Skill-only NPC projectiles commit their aim at telegraph start, allowing a dodge.
        bool lockedAim = false;
        if ((id & npc::ENTITY_BIT) && A.shape == Shape::PROJECTILE)
        {
            for (const auto& n : this->npcSpawns)
            {
                if (n.entity == id)
                {
                    lockedAim = !this->mapContent->combat->monsters.at(n.npcIndex).basic;
                }

            }

        }

        if ((A.shape == Shape::MELEE || A.shape == Shape::PROJECTILE) && e.target && !lockedAim)
        {
            const auto* ts = FindStats(e.target);
            const auto* tf = FindFeet(e.target);
            const auto* tc = FindCombat(e.target);
            if (!ts || !tf || !tc || !ts->CanBeAttacked() || tc->life != e.life ||
                !InRange(*FindFeet(id), *tf,
                         A.shape == Shape::MELEE
                             ? MeleeCenterRange(A.range, CombatRadius(id), CombatRadius(e.target))
                             : A.range + std::max(0, int(CombatRadius(id)) - int(DEFAULT_NPC_RADIUS)) +
                                   std::max(0, int(CombatRadius(e.target)) - int(DEFAULT_NPC_RADIUS))) ||
                !CombatVisible(*FindFeet(id), *tf))
            {
                e.phase = Phase::CANCELLED;
                e.reason = static_cast<unsigned>(Reject::TARGET);
                SendCombat(e);
                return;
            }

            e.point = *tf;
        }

        if (A.shape == Shape::PROJECTILE && this->projectiles.size() >= MAX_PROJECTILES)
        {
            e.phase = Phase::CANCELLED;
            e.reason = 1;
            SendCombat(e);
            return;
        }

        if (definition && definition->motion != skills::Motion::NONE)
        {
            SkillMotion(id, *definition, e.point);
            e.origin = *FindFeet(id);
            if (definition->motion == skills::Motion::DASH && e.target &&
                !InRange(e.origin, *FindFeet(e.target),
                         MeleeCenterRange(220, CombatRadius(id), CombatRadius(e.target))))
            {
                e.phase = Phase::CANCELLED;
                e.reason = static_cast<unsigned>(Reject::BLOCKED);
                SendCombat(e);
                return;
            }

        }

        if (!SpendMp(id, A.mp))
        {
            e.phase = Phase::CANCELLED;
            SendCombat(e);
            return;
        }

        state->cooldowns[A.id] = this->combatMs + A.cooldownMs;
        state->busyUntil = this->combatMs + A.recoveryMs;
        e.phase = Phase::FIRED;
        e.origin = *FindFeet(id);
        SendCombat(e);
        if (A.shape == Shape::MELEE)
        {
            if (e.target)
            {
                CombatHit(e, e.target, *stats, &A);
            }
            else
            {
                // Force-attack hits at most one live enemy in the forward 90-degree arc.
                Ability swing = A;
                swing.shape = Shape::CONE;
                swing.radius = A.range;
                swing.angle = 90;
                std::uint64_t victim = 0, best = UINT64_MAX;
                for (const auto& n : this->npcSpawns)
                {
                    if (FindCombat(n.entity) && n.stats.CanBeAttacked() &&
                        BodyInSector(e.origin, n.feet, e.yaw, MeleeCenterRange(A.range, CombatRadius(id), 0), 90,
                                     CombatRadius(n.entity)) &&
                        CombatVisible(e.origin, n.feet))
                    {
                        const auto D = DistanceSquared2D(e.origin, n.feet);
                        if (D < best)
                        {
                            best = D;
                            victim = n.entity;
                        }

                    }

                }

                for (const auto& [target, player] : this->players)
                {
                    if (player.ready && player.stats.CanBeAttacked() && CanDamage(id, target) &&
                        BodyInSector(e.origin, player.feet, e.yaw, MeleeCenterRange(A.range, CombatRadius(id), 0), 90,
                                     CombatRadius(target)) &&
                        CombatVisible(e.origin, player.feet))
                    {
                        const auto DISTANCE = DistanceSquared2D(e.origin, player.feet);
                        if (DISTANCE < best)
                        {
                            best = DISTANCE;
                            victim = target;
                        }

                    }

                }

                if (victim)
                {
                    CombatHit(e, victim, *stats, &A);
                }

            }

            return;
        }

        if (A.shape == Shape::PROJECTILE)
        {
            e.origin.z += 80;
            e.point.z += 80;
            const double DX = double(e.point.x) - e.origin.x, DY = double(e.point.y) - e.origin.y,
                         DZ = double(e.point.z) - e.origin.z;
            const double LENGTH = std::max(1.0, std::sqrt(DX * DX + DY * DY + DZ * DZ));
            const unsigned MUZZLE = std::max(0, int(CombatRadius(id)) - int(DEFAULT_NPC_RADIUS));
            if (MUZZLE)
            {
                auto mouth = e.origin;
                mouth.x += static_cast<int>(std::round(DX / LENGTH * MUZZLE));
                mouth.y += static_cast<int>(std::round(DY / LENGTH * MUZZLE));
                // No projectile may emerge through a wall or a closed door.
                if (!CombatVisible(e.origin, mouth))
                {
                    e.phase = Phase::PROJECTILE_END;
                    e.point = e.origin;
                    SendCombat(e);
                    return;
                }

                e.origin = mouth;
            }

            e.point = {e.origin.x + static_cast<int>(std::round(DX / LENGTH * A.range)),
                       e.origin.y + static_cast<int>(std::round(DY / LENGTH * A.range)),
                       e.origin.z + static_cast<int>(std::round(DZ / LENGTH * A.range))};
            e.phase = Phase::PROJECTILE;
            e.startMs = this->combatMs;
            e.endMs = this->combatMs + std::uint64_t(A.range) * 1000 / A.speed;
            Projectile projectile{e, e.origin, e.point, *stats, this->combatMs, e.endMs};
            projectile.ability = A;
            projectile.piercing = definition && definition->piercing;
            this->projectiles.push_back(std::move(projectile));
            SendCombat(e);
            return;
        }

        const auto CENTER = A.shape == Shape::CONE || A.shape == Shape::LINE ? e.origin : e.point;
        auto hit = [&](std::uint64_t target, WorldPosition feet)
        {
            const bool INSIDE = A.shape == Shape::LINE
                                    ? BodyInLine(CENTER, feet, e.yaw, A.range, A.radius, CombatRadius(target))
                                    : BodyInSector(CENTER, feet, e.yaw, A.radius,
                                                   A.shape == Shape::CONE ? A.angle : 360, CombatRadius(target));
            if (INSIDE && CombatVisible(CENTER, feet))
            {
                CombatHit(e, target, *stats, &A);
            }

        };

        if (id & npc::ENTITY_BIT)
        {
            for (const auto& [target, p] : this->players)
            {
                if (p.ready)
                {
                    hit(target, p.feet);
                }

            }

        }
        else
        {
            for (const auto& n : this->npcSpawns)
            {
                if (FindCombat(n.entity))
                {
                    hit(n.entity, n.feet);
                }

            }

            for (const auto& [target, player] : this->players)
            {
                if (player.ready && CanDamage(id, target))
                {
                    hit(target, player.feet);
                }

            }

        }

    };

    for (auto& [id, p] : this->players)
    {
        if (p.ready)
        {
            finish(id);
        }

    }

    for (const auto& n : this->npcSpawns)
    {
        finish(n.entity);
    }

    for (std::size_t i = 0; i < this->projectiles.size();)
    {
        auto& p = this->projectiles[i];
        const auto& a = p.ability;
        const double FRACTION =
            std::clamp(double(this->combatMs - p.fired) / std::max(1.0, double(p.expires - p.fired)), 0.0, 1.0);
        const auto FROM = p.feet;
        const WorldPosition TO{p.event.origin.x + static_cast<int>(std::round((p.end.x - p.event.origin.x) * FRACTION)),
                               p.event.origin.y + static_cast<int>(std::round((p.end.y - p.event.origin.y) * FRACTION)),
                               p.event.origin.z +
                                   static_cast<int>(std::round((p.end.z - p.event.origin.z) * FRACTION))};
        double best = 2;
        std::uint64_t victim = 0;
        const auto COLLISION = this->residency->Get(FROM);
        const auto OTHER = this->residency->Get(TO);
        if (!COLLISION || COLLISION != OTHER)
        {
            best = 0;
        }
        else if (FROM != TO)
        {
            auto sweepFrom = FROM, sweepTo = TO;
            sweepFrom.z -= static_cast<int>(a.radius);
            sweepTo.z -= static_cast<int>(a.radius);
            const auto WALL = SweepBody(*COLLISION, sweepFrom, sweepTo,
                                        BodyShape{static_cast<int>(a.radius), static_cast<int>(a.radius * 2)});
            if (WALL.status == QueryStatus::HIT)
            {
                best = double(WALL.fractionQ32) / double(FRACTION_ONE);
            }
            else if (WALL.status != QueryStatus::CLEAR)
            {
                best = 0;
            }

        }

        if (IsMaze() && !dungeon::DoorClear(FROM, TO, this->activeRoom, static_cast<int>(a.radius)))
        {
            best = 0;
        }

        auto candidate = [&](std::uint64_t target, WorldPosition feet, const EntityStats& ts)
        {
            if (!ts.CanBeAttacked() || !CanDamage(p.event.source, target))
            {
                return;
            }

            if (std::find(p.hits.begin(), p.hits.begin() + p.hitCount, target) != p.hits.begin() + p.hitCount)
            {
                return;
            }

            const double T = SweepBodyTarget(FROM, TO, feet, CombatRadius(target), CombatHeight(target), a.radius);
            if (p.piercing && T >= 0 && T <= 1 && T < best)
            {
                if (p.hitCount < p.hits.size())
                {
                    p.hits[p.hitCount++] = target;
                    CombatHit(p.event, target, p.attacker, &p.ability);
                }

                return;
            }

            if (T >= 0 && T <= 1 && T < best)
            {
                best = T;
                victim = target;
            }

        };

        if (p.event.source & npc::ENTITY_BIT)
        {
            for (const auto& [target, player] : this->players)
            {
                if (player.ready)
                {
                    candidate(target, player.feet, player.stats);
                }

            }

        }
        else
        {
            for (const auto& n : this->npcSpawns)
            {
                if (FindCombat(n.entity))
                {
                    candidate(n.entity, n.feet, n.stats);
                }

            }

            for (const auto& [target, player] : this->players)
            {
                if (player.ready)
                {
                    candidate(target, player.feet, player.stats);
                }

            }

        }

        p.feet = TO;
        if (best <= 1 || this->combatMs >= p.expires)
        {
            const auto EVENT = p.event;
            const auto ATTACKER = p.attacker;
            if (victim)
            {
                CombatHit(EVENT, victim, ATTACKER, &p.ability);
            }

            auto end = EVENT;
            end.phase = Phase::PROJECTILE_END;
            end.point = TO;
            if (best <= 1)
            {
                end.point = {FROM.x + static_cast<int>(std::round((TO.x - FROM.x) * best)),
                             FROM.y + static_cast<int>(std::round((TO.y - FROM.y) * best)),
                             FROM.z + static_cast<int>(std::round((TO.z - FROM.z) * best))};
            }

            SendCombat(end);
            this->projectiles[i] = std::move(this->projectiles.back());
            this->projectiles.pop_back();
        }
        else
        {
            ++i;
        }

    }

}

// Bounded swept movement; a skill never teleports through terrain, closed doors or solid combat bodies.
void GameWorld::SkillMotion(std::uint64_t id, const shared_world::skills::Definition& definition,
                            shared_world::WorldPosition target)
{
    using namespace shared_world;
    auto* feet = FindFeet(id);
    if (!feet)
    {
        return;
    }

    const auto ORIGIN = *feet;
    double dx = double(target.x) - ORIGIN.x, dy = double(target.y) - ORIGIN.y;
    const auto LENGTH = std::hypot(dx, dy);
    if (LENGTH < 1)
    {
        return;
    }

    dx /= LENGTH;
    dy /= LENGTH;
    unsigned distance = definition.move;
    if (definition.motion == skills::Motion::BACKSTEP)
    {
        dx = -dx;
        dy = -dy;
    }
    else
    {
        distance = static_cast<unsigned>(std::max(0., std::min(double(distance), LENGTH - 180 - CombatRadius(0))));
    }

    for (unsigned step = 0; step < distance; step += 20)
    {
        const auto SIZE = std::min(20U, distance - step);
        const auto BEFORE = *feet;
        const auto NEXT =
            MovePlayer(BEFORE, static_cast<int>(std::round(dx * SIZE)), static_cast<int>(std::round(dy * SIZE)));
        if (NEXT == BEFORE)
        {
            break;
        }

        *feet = NEXT;
    }

    if (*feet != ORIGIN)
    {
        net::Message move;
        move.kind = net::Kind::STATE;
        move.entity = id;
        move.feet = *feet;
        move.tick = this->tickNumber;
        auto& player = this->players.at(id);
        move.sequence = player.ack;
        move.yaw = player.yaw;
        Broadcast(move);
    }

}

