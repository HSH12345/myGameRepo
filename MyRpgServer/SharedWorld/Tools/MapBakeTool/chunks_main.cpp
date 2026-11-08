#include "ChunkGeometry.h"
#include "../../Collision/StaticCollision.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3 && argc != 4)
        {
            throw std::invalid_argument("Usage: WorldChunkBake input.worldsource new-directory [chunk-size-cm]");
        }

        const std::filesystem::path SOURCE_PATH(argv[1]), DESTINATION(argv[2]);
        auto staging = DESTINATION;
        staging += ".building";
        if (std::filesystem::exists(DESTINATION) || std::filesystem::exists(staging))
        {
            throw std::invalid_argument("destination or staging already exists; choose a new build directory");
        }

        if (std::filesystem::file_size(SOURCE_PATH) > 128 * 1024 * 1024)
        {
            throw std::invalid_argument("source size limit");
        }

        std::ifstream input(SOURCE_PATH);
        input.imbue(std::locale::classic());
        const auto GEOMETRY = shared_world::ReadSourceGeometry(input);
        shared_world::ChunkManifest manifest;
        manifest.layout.world = GEOMETRY.definition;
        if (argc == 4)
        {
            std::size_t used = 0;
            const auto SIZE = std::stoi(argv[3], &used);
            if (used != std::string(argv[3]).size())
            {
                throw std::invalid_argument("invalid chunk size");
            }

            manifest.layout.size = SIZE;
            manifest.layout.halo = std::min(1024, SIZE / 4);
        }

        manifest.layout.Validate();
        manifest.content.id = GEOMETRY.definition.id;
        manifest.content.version = GEOMETRY.definition.version;
        manifest.content.contentPath = GEOMETRY.contentPath;
        if (GEOMETRY.contentPath.empty())
        {
            throw std::invalid_argument("chunk build requires source v2 content metadata");
        }

        // Validate the entire input before clipping; outside geometry must not
        // disappear silently from an otherwise valid output.
        (void)shared_world::BakeMap(GEOMETRY);
        if (!std::filesystem::create_directory(staging))
        {
            throw std::runtime_error("cannot create staging directory");
        }

        for (const auto COORDINATE : manifest.layout.Coordinates())
        {
            auto chunk = shared_world::ClipToChunk(GEOMETRY, manifest.layout, COORDINATE);
            if (chunk.triangles.empty())
            {
                if (!chunk.spawns.empty() || !chunk.noWalkVolumes.empty())
                {
                    throw std::invalid_argument("metadata in an empty geometry chunk");
                }

                continue;
            }

            const auto BYTES = shared_world::BakeMap(chunk).Encode();
            const auto LOADED = shared_world::LoadedMap::FromBytes(BYTES);
            const shared_world::StaticWorld COLLISION(LOADED);
            for (auto spawn : chunk.spawns)
            {
                const auto GROUND = COLLISION.ProjectToWalkableSurface(spawn.feet, 100);
                if (GROUND.status != shared_world::QueryStatus::HIT)
                {
                    throw std::invalid_argument("chunk spawn has no ground");
                }

                spawn.feet = GROUND.position;
                manifest.content.spawns.push_back(spawn);
            }

            const auto FILENAME =
                "chunk_" + std::to_string(COORDINATE.x) + "_" + std::to_string(COORDINATE.y) + ".rpgmap";
            std::ofstream out(staging / FILENAME, std::ios::binary);
            out.write(reinterpret_cast<const char*>(BYTES.data()), static_cast<std::streamsize>(BYTES.size()));
            out.close();
            if (!out)
            {
                throw std::runtime_error("chunk write failed");
            }

            manifest.content.chunks.push_back({COORDINATE.x, COORDINATE.y, LOADED->Identity(), FILENAME});
        }

        std::sort(manifest.content.spawns.begin(), manifest.content.spawns.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.id < b.id;
                  });

        const auto ENCODED = manifest.Encode();
        (void)shared_world::ChunkManifest::Decode(ENCODED);
        std::ofstream index(staging / "world.rpgchunks", std::ios::binary);
        index << ENCODED;
        index.close();
        if (!index)
        {
            throw std::runtime_error("chunk index write failed");
        }

        // Publish the complete directory only. Failed staging stays inspectable
        // and is never selected by runtime configuration.
        std::filesystem::rename(staging, DESTINATION);
        std::cout << "Chunk build complete: chunks=" << manifest.content.chunks.size()
                  << " checksum=" << manifest.Identity().checksum << '\n';
        return 0;
    }

    catch (const std::exception& e)
    {
        std::cerr << "WorldChunkBake: " << e.what() << '\n';
        return 1;
    }

}

