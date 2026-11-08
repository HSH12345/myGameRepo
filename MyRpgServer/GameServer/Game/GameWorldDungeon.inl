// All encounter state belongs to the map owner. No DB/IOCP work occurs here.
void GameWorld::DungeonSnapshot(const SessionPtr& recipient)
{
    shared_world::net::Message m;
    m.kind = shared_world::net::Kind::DUNGEON_STATE;
    m.dungeonRun = this->exitLayout.run;
    m.activeRoom = this->activeRoom;
    m.clearedRooms = this->clearedRooms;
    if (recipient)
    {
        Send(recipient, m);
    }
    else
    {
        Broadcast(m);
    }

}

void GameWorld::ClearEncounter()
{
    for (const auto& n : this->npcSpawns)
    {
        CancelCombat(n.entity);
        shared_world::net::Message m;
        m.kind = shared_world::net::Kind::DESPAWN;
        m.entity = n.entity;
        Broadcast(m);
    }

    this->npcSpawns.clear();
    this->npcCombat.clear();
    this->npcHomes.clear();
    for (const auto& p : this->projectiles)
    {
        auto e = p.event;
        e.phase = shared_world::combat::Phase::PROJECTILE_END;
        SendCombat(e);
    }

    this->projectiles.clear();
}

void GameWorld::AddDungeonExit(shared_world::WorldPosition feet, shared_world::dungeon::ExitType type, std::uint32_t id)
{
    using namespace shared_world::dungeon;
    // Bounded display pool: replace oldest when all three slots are occupied.
    if (this->exitLayout.count == 3)
    {
        this->exitLayout.zones[0] = this->exitLayout.zones[1];
        this->exitLayout.zones[1] = this->exitLayout.zones[2];
        --this->exitLayout.count;
    }

    this->exitLayout.zones[this->exitLayout.count++] = {id, type, static_cast<ExitOutcome>(type), feet, 150};
    shared_world::net::Message m;
    m.kind = shared_world::net::Kind::EXIT_LAYOUT;
    m.instance = this->instanceId;
    m.exits = this->exitLayout;
    Broadcast(m);
}

void GameWorld::TickDungeon()
{
    using namespace shared_world;
    using namespace dungeon;
    using namespace net;
    if (!IsMaze() || !this->exitLayout.run || this->players.empty())
    {
        return;
    }

    if (this->activeRoom)
    {
        bool alive = false;
        for (const auto& [id, p] : this->players)
        {
            if (p.ready && !p.stats.IsDead())
            {
                alive = true;
            }

        }

        if (!alive)
        {
            ClearEncounter();
            this->activeRoom = 0;
            DungeonSnapshot();
            return; // Party wipe resets this unfinished encounter for a fresh attempt.
        }

        if (!this->npcSpawns.empty() && std::all_of(this->npcSpawns.begin(), this->npcSpawns.end(),
                                                    [](const auto& n)
                                                    {
                                                        return n.stats.IsDead();
                                                    }))

        {
            const auto CLEARED = this->activeRoom;
            this->clearedRooms |= 1U << (CLEARED - 1);
            this->activeRoom = 0;
            DungeonSnapshot();
            // Once per clear, not once per tick. Default tuning: 45% room / 3% corridor.
            if (CLEARED == 6 || (this->clearedRooms & 31) == 31 ||
                std::uniform_int_distribution<unsigned>(1, 100)(this->exitRandom) <= 45)
            {
                AddDungeonExit(ROOMS[CLEARED - 1], ExitType::NORMAL, 100 + CLEARED);
            }

        }

        return;
    }

    for (const auto& [id, p] : this->players)
    {
        if (!p.ready || p.stats.IsDead())
        {
            continue;
        }

        const auto ROOM = RoomAt(p.feet, 100);
        if (ROOM && !(this->clearedRooms & (1U << (ROOM - 1))))
        {
            ClearEncounter();
            this->activeRoom = ROOM;
            ++this->encounterLife;
            DungeonSnapshot(); // Entrances remain open while this encounter is active.
            unsigned slot = 0;
            for (const auto& placement : this->mapContent->npcs->placements)
            {
                if (placement.map == 301)
                {
                    const auto& def = this->mapContent->npcs->definitions.at(placement.npc);
                    if (!this->mapContent->combat->monsters.count(def.index) || (def.index == 2201) != (ROOM == 6))
                    {
                        continue;
                    }

                    Message n;
                    n.kind = Kind::NPC_SPAWN;
                    n.entity = placement.Entity();
                    n.npcIndex = def.index;
                    n.appearance = def.appearance;
                    n.role = def.role;
                    n.roleData = def.roleData;
                    n.name = def.name;
                    n.stats = def.stats;
                    n.catalogChecksum = this->mapContent->npcs->checksum;
                    n.feet = ROOMS[ROOM - 1];
                    if (ROOM != 6)
                    {
                        n.feet.x += slot == 0 ? -220 : 220;
                        n.feet.y += slot == 2 ? 220 : -180;
                    }

                    ++slot;
                    this->npcHomes[n.entity] = n.feet;
                    this->npcCombat[n.entity].life = this->encounterLife;
                    this->npcSpawns.push_back(n);
                    Broadcast(n);
                    // All participants need the new NPC life, including users following behind.
                    combat::Event presence;
                    presence.source = n.entity;
                    presence.life = this->encounterLife;
                    presence.point = n.feet;
                    presence.phase = combat::Phase::RESPAWN;
                    SendCombat(presence);
                }

            }

            return;
        }

        // One roll per visited 1800cm cell, only while physically in a narrow corridor.
        const int X_VALUE = (p.feet.x + 4500) / 1800, Y = (p.feet.y + 4500) / 1800;
        if (X_VALUE < 0 || X_VALUE >= 5 || Y < 0 || Y >= 5)
        {
            continue;
        }

        const unsigned BIT = 1U << (Y * 5 + X_VALUE);
        const int CX = (X_VALUE - 2) * 1800, CY = (Y - 2) * 1800;
        if (std::abs(p.feet.x - CX) < 650 && std::abs(p.feet.y - CY) < 650)
        {
            continue;
        }

        if (this->corridorRolls & BIT)
        {
            continue;
        }

        this->corridorRolls |= BIT;
        if (std::uniform_int_distribution<unsigned>(1, 100)(this->exitRandom) <= 3)
        {
            AddDungeonExit(p.feet, ExitType::EMERGENCY, 200 + Y * 5 + X_VALUE);
        }

    }

}

