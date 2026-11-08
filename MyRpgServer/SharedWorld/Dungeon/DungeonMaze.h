#pragma once
#include "../Core/MapDefinition.h"
#include <array>

namespace shared_world::dungeon
{
struct Door
{
    unsigned room;
    int x, y, z, sx, sy, sz;
};

inline constexpr std::array<WorldPosition, 6> ROOMS{
    {{-1800, 0, 0}, {-1800, -3600, 0}, {1800, 0, 0}, {1800, 3600, 0}, {-1800, 1800, 0}, {1800, -8400, 0}}};
inline constexpr WorldPosition BOSS_ROOM{1800, -8400, 0};
inline constexpr WorldPosition BOSS_ENTRY{-450, -8400, 0};
inline constexpr std::array<int, 6> ROOM_HALF_EXTENTS{{600, 600, 600, 600, 600, 2400}};
inline constexpr std::array<Door, 14> DOORS{{{1, -1200, 0, 150, 80, 400, 300},
                                             {1, -1800, -600, 150, 400, 80, 300},
                                             {1, -2400, 0, 150, 80, 400, 300},
                                             {2, -2400, -3600, 150, 80, 400, 300},
                                             {2, -1200, -3600, 150, 80, 400, 300},
                                             {3, 1800, -600, 150, 400, 80, 300},
                                             {3, 2400, 0, 150, 80, 400, 300},
                                             {3, 1800, 600, 150, 400, 80, 300},
                                             {4, 2400, 3600, 150, 80, 400, 300},
                                             {4, 1200, 3600, 150, 80, 400, 300},
                                             {5, -1200, 1800, 150, 80, 400, 300},
                                             {5, -2400, 1800, 150, 80, 400, 300},
                                             {6, -600, -8400, 500, 80, 400, 1000},
                                             {6, 4200, -8400, 500, 80, 400, 1000}}};

inline unsigned RoomAt(WorldPosition p, int inset = 0)
{
    for (unsigned i = 0; i < ROOMS.size(); ++i)
    {
        if (std::abs(p.x - ROOMS[i].x) <= ROOM_HALF_EXTENTS[i] - inset &&
            std::abs(p.y - ROOMS[i].y) <= ROOM_HALF_EXTENTS[i] - inset)
        {
            return i + 1;
        }

    }

    return 0;
}

// Encounter entrances stay open; static maze walls remain in the collision map.
inline bool DoorClear(WorldPosition, WorldPosition, unsigned, int = 34)
{
    return true;
}

}

