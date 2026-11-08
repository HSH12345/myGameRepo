#pragma once
#include "ChunkLoadQueue.h"
#include "../Movement/MovementController.h"
#include "../Npc/NpcCatalog.h"
#include "../Combat/Combat.h"
#include "../Dungeon/DungeonExits.h"
#include <chrono>

namespace shared_world
{
// Load on startup/asset thread. Lifetime (including queue join) ends off tick.
// One queue belongs to one owner/cache; do not share it across map instances.
struct WorldContent
{
    MapManifest manifest;
    MapIdentity identity;
    std::shared_ptr<const npc::Catalog> npcs;
    std::shared_ptr<const combat::Catalog> combat;
    std::optional<dungeon::Rule> exitRule;
    std::optional<ChunkManifest> chunks;
    std::shared_ptr<const StaticWorld> collision; // Validated initial spawn chunk.
    std::unique_ptr<ChunkLoadQueue> loader;

    static std::shared_ptr<WorldContent> Load(const std::filesystem::path& path)
    {
        auto result = std::make_shared<WorldContent>();
        if (path.extension() == ".rpgchunks")
        {
            result->chunks = ChunkManifest::Decode(ReadWorldDataFile<std::string>(
                path, 0, 16 * 1024 * 1024, "chunk index unavailable or over budget",
                "chunk index unavailable or over budget", "chunk index read failed"));
            result->manifest = result->chunks->content;
            result->identity = result->chunks->Identity();
        }
        else
        {
            result->manifest = LoadMapManifest(path);
            if (result->manifest.chunks.size() != 1)
            {
                throw std::invalid_argument("multiple chunks require rpgchunks index");
            }

            result->identity = result->manifest.chunks.front().identity;
        }

        if (result->manifest.spawns.empty() || !result->manifest.portals.empty())
        {
            throw std::invalid_argument("explicit spawn required; portal transitions are not implemented");
        }

        auto provider = std::make_shared<const FileMapDataProvider>(path.parent_path());
        auto ref = result->manifest.chunks.front();
        if (result->chunks)
        {
            const auto C = result->chunks->layout.Coordinate(result->manifest.spawns.front().feet);
            for (const auto& candidate : result->manifest.chunks)
            {
                if (candidate.x == C.x && candidate.y == C.y)
                {
                    ref = candidate;
                }

            }

            // Validate initial data using exactly the same worker path as later chunks.
            result->loader = std::make_unique<ChunkLoadQueue>(*result->chunks, provider);
            if (!result->loader->Submit(C, 1, 1))
            {
                throw std::invalid_argument("initial chunk unavailable");
            }

            ChunkLoadQueue::Result loaded;
            while (!result->loader->TryPop(loaded))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            if (loaded.status != ChunkLoadQueue::Status::LOADED)
            {
                throw std::invalid_argument("initial chunk invalid");
            }

            result->collision = std::move(loaded.collision);
        }
        else
        {
            result->collision = std::make_shared<const StaticWorld>(provider->LoadChunk(ref));
        }

        if (result->chunks && result->collision->ResidentBytes() > 128 * 1024 * 1024)
        {
            throw std::invalid_argument("initial chunk exceeds default resident budget");
        }

        const auto COUNT = result->chunks ? std::size_t{1} : result->manifest.spawns.size();
        for (std::size_t i = 0; i < COUNT; ++i)
        {
            const auto SPAWN = result->manifest.spawns[i].feet;
            auto raised = SPAWN;
            ++raised.z;
            const auto SUPPORT = GroundSupport(*result->collision, SPAWN);
            if (SUPPORT.status != QueryStatus::HIT || std::abs(SUPPORT.position.z - SPAWN.z) > 1 ||
                SweepBody(*result->collision, raised, raised).status != QueryStatus::CLEAR)
            {
                throw std::invalid_argument("spawn lacks ground or body clearance");
            }

        }

        return result;
    }

};

}

