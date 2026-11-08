#pragma once

#include <cstdint>
#include <string>

namespace shared_world::party
{
constexpr unsigned MAX_MEMBERS = 4;
constexpr unsigned MAX_PARTIES = 4;
constexpr unsigned MAX_PLAYERS = MAX_MEMBERS * MAX_PARTIES;
constexpr unsigned COUNTDOWN_MS = 5000;

enum class MatchAction : std::uint32_t
{
    READY = 1,
    UNREADY = 2,
    LEAVE = 3
};

enum class MatchPhase : std::uint32_t
{
    NONE = 0,
    WAITING = 1,
    COUNTDOWN = 2,
    STARTING = 3
};

struct Member
{
    std::uint64_t entity = 0, character = 0;
    std::string name;
    std::uint32_t race = 0, gender = 0;
    bool ready = false;
};

inline bool CanDamage(bool sourceNpc, bool targetNpc, std::uint64_t sourceParty, std::uint64_t targetParty,
                      bool dungeon)
{
    if (sourceNpc || targetNpc)
    {
        return sourceNpc != targetNpc;
    }

    return dungeon && sourceParty && targetParty && sourceParty != targetParty;
}

// A future healing ability must authorize this relation before applying support.
// No healing ability or positive-HP client request is introduced here.
inline bool CanSupport(std::uint64_t source, std::uint64_t target, std::uint64_t sourceParty, std::uint64_t targetParty)
{
    return source && target && (source == target || (sourceParty && sourceParty == targetParty));
}

}

