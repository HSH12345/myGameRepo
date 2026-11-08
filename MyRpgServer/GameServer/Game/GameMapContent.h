#pragma once

#include "../../SharedWorld/Map/ChunkResidency.h"

#include <unordered_map>

using GameMapContent = shared_world::WorldContent;

// Each entry owns its asset queue; mutable caches remain inside GameWorld.

using GameMapCatalog = std::unordered_map<std::uint64_t, std::shared_ptr<const GameMapContent>>;
