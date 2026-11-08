#pragma once
#include "SharedWorld/Map/ChunkResidency.h"

struct FGameClientWorld
{
    unsigned activeRoom = 0;
    std::uint64_t dungeonRun = 0;
    std::shared_ptr<shared_world::WorldContent> content;
    std::unique_ptr<shared_world::ChunkResidency> residency;
};

