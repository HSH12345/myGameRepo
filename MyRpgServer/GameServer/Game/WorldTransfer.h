#pragma once

#include "GameCommand.h"

#include <functional>

#include <chrono>

// Value-only handoff. No source-world pointer crosses the owner boundary.

struct WorldTransfer

{

    std::weak_ptr<Session> session;

    shared_world::net::Message identity;

    std::uint32_t visit = 0;

    std::uint32_t targetMap = 0; // Server-selected map; zero preserves legacy portal/return routing.

    shared_world::dungeon::Receipt exitReceipt;

    shared_world::EntityStats stats;

    shared_world::economy::State economy;

    bool economyReady = false;

    std::uint64_t partyId = 0;

    std::chrono::steady_clock::time_point potionAllowed{};

    std::array<std::uint64_t, 65> combatCooldownRemaining{};
};

using TransferWorld = std::function<bool(std::uint64_t, WorldTransfer, std::function<void()>)>;

using TransferParty = std::function<bool(std::uint64_t, std::vector<WorldTransfer>, std::function<void()>)>;
