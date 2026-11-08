// Match rooms and ready/countdown transitions belong to the town owner worker.
void GameWorld::MatchSnapshot(MatchRoom& room, std::chrono::steady_clock::time_point now, unsigned error)
{
    using namespace shared_world;
    net::Message message;
    message.kind = net::Kind::MATCH_STATE;
    message.room = room.id;
    message.revision = room.revision;
    message.targetMap = room.targetMap;
    message.members = room.members;
    message.error = error;
    message.matchPhase = party::MatchPhase::WAITING;
    if (room.deadline != std::chrono::steady_clock::time_point{})
    {
        message.matchPhase = party::MatchPhase::COUNTDOWN;
        message.countdownMs = static_cast<unsigned>(
            std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(room.deadline - now).count(),
                                     0, party::COUNTDOWN_MS));
    }

    for (const auto& member : room.members)
    {
        if (const auto PLAYER = this->players.find(member.entity); PLAYER != this->players.end())
        {
            Send(PLAYER->second.session.lock(), message);
        }

    }

}

void GameWorld::JoinMatch(const SessionPtr& session, Player& player, std::uint32_t targetMap)
{
    const auto NOW = std::chrono::steady_clock::now();
    if (player.matchRoom)
    {
        MatchSnapshot(this->matches.at(player.matchRoom), NOW);
        return;
    }

    if (!this->transferParty || !player.ready || player.stats.IsDead() || !targetMap || this->mapContent->exitRule ||
        player.economyPending || player.economyUnknown)
    {
        shared_world::net::Message empty;
        empty.kind = shared_world::net::Kind::MATCH_STATE;
        empty.error = 1;
        Send(session, empty);
        return;
    }

    MatchRoom* room = nullptr;
    for (auto& [id, candidate] : this->matches)
    {
        if (candidate.targetMap == targetMap && candidate.members.size() < shared_world::party::MAX_MEMBERS &&
            (!room || id < room->id))
        {
            room = &candidate;
        }

    }

    if (!room)
    {
        if (this->nextMatch == UINT32_MAX || this->matches.size() >= 32)
        {
            shared_world::net::Message empty;
            empty.kind = shared_world::net::Kind::MATCH_STATE;
            empty.error = 1;
            Send(session, empty);
            return;
        }

        MatchRoom created;
        created.id = (this->instanceId << 32) | ++this->nextMatch;
        created.targetMap = targetMap;
        created.members.reserve(shared_world::party::MAX_MEMBERS);
        room = &this->matches.emplace(created.id, std::move(created)).first->second;
    }

    room->members.push_back({session->GetId(), player.identity.character, player.identity.name, player.identity.race,
                             player.identity.gender, false});
    player.matchRoom = room->id;
    CancelCombat(session->GetId());
    room->deadline = {};
    ++room->revision;
    MatchSnapshot(*room, NOW);
}

void GameWorld::MatchInput(const SessionPtr& session, Player& player, const shared_world::net::Message& message)
{
    if (message.sequence <= player.lastMatchRequest)
    {
        return;
    }

    player.lastMatchRequest = message.sequence;
    if (!player.matchRoom || message.room != player.matchRoom)
    {
        shared_world::net::Message reply;
        reply.kind = shared_world::net::Kind::MATCH_STATE;
        reply.error = 3;
        if (player.matchRoom)
        {
            MatchSnapshot(this->matches.at(player.matchRoom), std::chrono::steady_clock::now(), 3);
        }
        else
        {
            Send(session, reply);
        }

        return;
    }

    if (message.choice == static_cast<unsigned>(shared_world::party::MatchAction::LEAVE))
    {
        LeaveMatch(session->GetId());
        return;
    }

    auto& room = this->matches.at(player.matchRoom);
    for (auto& member : room.members)
    {
        if (member.entity == session->GetId())
        {
            const bool READY = message.choice == static_cast<unsigned>(shared_world::party::MatchAction::READY);
            if (member.ready != READY)
            {
                member.ready = READY;
                ++room.revision;
                room.deadline = {};
            }

        }

    }

    if (room.deadline == std::chrono::steady_clock::time_point{} &&
        std::all_of(room.members.begin(), room.members.end(),
                    [](const auto& member)
                    {
                        return member.ready;
                    }))

    {
        room.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(shared_world::party::COUNTDOWN_MS);
        room.lastSecond = 5;
    }

    MatchSnapshot(room, std::chrono::steady_clock::now());
}

void GameWorld::LeaveMatch(std::uint64_t id, bool notify)
{
    const auto PLAYER = this->players.find(id);
    if (PLAYER == this->players.end() || !PLAYER->second.matchRoom)
    {
        return;
    }

    const auto ROOM_ID = PLAYER->second.matchRoom;
    PLAYER->second.matchRoom = 0;
    auto found = this->matches.find(ROOM_ID);
    if (found != this->matches.end())
    {
        auto& room = found->second;
        std::erase_if(room.members,
                      [id](const auto& member)
                      {
                          return member.entity == id;
                      });

        room.deadline = {};
        ++room.revision;
        if (room.members.empty())
        {
            this->matches.erase(found);
        }
        else
        {
            const auto NOW = std::chrono::steady_clock::now();
            if (std::all_of(room.members.begin(), room.members.end(),
                            [](const auto& member)
                            {
                                return member.ready;
                            }))

            {
                room.deadline = NOW + std::chrono::milliseconds(shared_world::party::COUNTDOWN_MS);
                room.lastSecond = 5;
            }

            MatchSnapshot(room, NOW);
        }

    }

    if (notify)
    {
        shared_world::net::Message empty;
        empty.kind = shared_world::net::Kind::MATCH_STATE;
        Send(PLAYER->second.session.lock(), empty);
    }

}

void GameWorld::TickMatches(std::chrono::steady_clock::time_point now)
{
    std::vector<std::uint64_t> invalid;
    for (const auto& [id, player] : this->players)
    {
        if (player.matchRoom && player.stats.IsDead())
        {
            invalid.push_back(id);
        }

    }

    for (auto id : invalid)
    {
        LeaveMatch(id);
    }

    for (auto it = this->matches.begin(); it != this->matches.end();)
    {
        auto& room = it->second;
        if (room.deadline == std::chrono::steady_clock::time_point{})
        {
            ++it;
            continue;
        }

        if (now < room.deadline)
        {
            const auto SECOND = static_cast<unsigned>(
                (std::chrono::duration_cast<std::chrono::milliseconds>(room.deadline - now).count() + 999) / 1000);
            if (SECOND != room.lastSecond)
            {
                room.lastSecond = SECOND;
                MatchSnapshot(room, now);
            }

            ++it;
            continue;
        }

        std::vector<WorldTransfer> transfers;
        transfers.reserve(room.members.size());
        bool valid = true;
        for (const auto& member : room.members)
        {
            auto& player = this->players.at(member.entity);
            const auto SESSION = player.session.lock();
            if (!SESSION || !SESSION->IsOpen() || !player.ready || player.stats.IsDead() || player.economyPending ||
                player.economyUnknown || player.visit == UINT32_MAX)
            {
                valid = false;
                break;
            }

            WorldTransfer transfer{player.session, player.identity, player.visit + 1, room.targetMap};
            transfer.partyId = room.id;
            transfer.stats = player.stats;
            transfer.economy = player.economy;
            transfer.economyReady = player.economyReady;
            transfer.potionAllowed = player.potionAllowed;
            for (std::size_t i = 0; i < 65; ++i)
            {
                transfer.combatCooldownRemaining[i] =
                    player.combat.cooldowns[i] > this->combatMs ? player.combat.cooldowns[i] - this->combatMs : 0;
            }

            transfers.push_back(std::move(transfer));
        }

        std::vector<std::uint64_t> ids;
        for (const auto& member : room.members)
        {
            ids.push_back(member.entity);
            this->players.at(member.entity).matchRoom = 0;
        }

        const bool ACCEPTED = valid && this->transferParty &&
                              this->transferParty(this->instanceId, std::move(transfers),
                                                  [this, ids]
                                                  {
                                                      for (auto id : ids)
                                                      {
                                                          Remove(id);
                                                      }

                                                  });

        if (ACCEPTED)
        {
            it = this->matches.erase(it);
        }
        else
        {
            for (auto& member : room.members)
            {
                member.ready = false;
                this->players.at(member.entity).matchRoom = room.id;
            }

            room.deadline = {};
            ++room.revision;
            MatchSnapshot(room, now, 1);
            ++it;
        }

    }

}

bool GameWorld::CanDamage(std::uint64_t source, std::uint64_t target) const
{
    using namespace shared_world;
    if (!source || !target || source == target)
    {
        return false;
    }

    const bool SOURCE_NPC = (source & npc::ENTITY_BIT) != 0;
    const bool TARGET_NPC = (target & npc::ENTITY_BIT) != 0;
    const auto SOURCE = this->players.find(source), TARGET = this->players.find(target);
    if ((!SOURCE_NPC && (SOURCE == this->players.end() || !SOURCE->second.ready)) ||
        (!TARGET_NPC && (TARGET == this->players.end() || !TARGET->second.ready)))
    {
        return false;
    }

    return party::CanDamage(SOURCE_NPC, TARGET_NPC, SOURCE_NPC ? 0 : SOURCE->second.partyId,
                            TARGET_NPC ? 0 : TARGET->second.partyId, true);
}

