#include "SourceGeometry.h"
#include "../../Collision/StaticCollision.h"
#include "../../Movement/MovementController.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
        {
            throw std::invalid_argument("Usage: MapBakeTool input.worldsource output.rpgmap (new file only)");
        }

        const std::filesystem::path INPUT(argv[1]), OUTPUT(argv[2]);
        if (std::filesystem::exists(OUTPUT))
        {
            throw std::invalid_argument("output exists; choose a new versioned output path");
        }

        auto manifestPath = OUTPUT;
        manifestPath.replace_extension(".rpgworld");
        if (std::filesystem::exists(manifestPath))
        {
            throw std::invalid_argument("manifest output exists");
        }

        if (std::filesystem::file_size(INPUT) > 128 * 1024 * 1024)
        {
            throw std::invalid_argument("source exceeds 128 MiB");
        }

        std::ifstream source(INPUT);
        if (!source)
        {
            throw std::runtime_error("cannot open source");
        }

        source.imbue(std::locale::classic());
        const auto GEOMETRY = shared_world::ReadSourceGeometry(source);
        const auto PACKAGE = shared_world::BakeMap(GEOMETRY);
        const auto BYTES = PACKAGE.Encode();
        // Verify before writing. No timestamps/random IDs: identical source gives identical bytes.
        (void)shared_world::MapPackage::Decode(BYTES);
        std::string manifestBytes;
        if (!GEOMETRY.contentPath.empty())
        {
            const auto LOADED = shared_world::LoadedMap::FromBytes(BYTES);
            const shared_world::StaticWorld COLLISION(LOADED);
            shared_world::MapManifest manifest;
            manifest.id = PACKAGE.definition.id;
            manifest.version = PACKAGE.definition.version;
            manifest.contentPath = GEOMETRY.contentPath;
            manifest.spawns = GEOMETRY.spawns;
            std::sort(manifest.spawns.begin(), manifest.spawns.end(),
                      [](const auto& a, const auto& b)
                      {
                          return a.id < b.id;
                      });

            for (auto& spawn : manifest.spawns)
            {
                const auto SURFACE = COLLISION.ProjectToWalkableSurface(spawn.feet, 100);
                if (SURFACE.status != shared_world::QueryStatus::HIT)
                {
                    throw std::invalid_argument("spawn has no nearby ground");
                }

                const auto SUPPORT = shared_world::GroundSupport(COLLISION, SURFACE.position);
                if (SUPPORT.status != shared_world::QueryStatus::HIT ||
                    shared_world::SweepBody(COLLISION, {SUPPORT.position.x, SUPPORT.position.y, SUPPORT.position.z + 1},
                                            {SUPPORT.position.x, SUPPORT.position.y, SUPPORT.position.z + 1})
                            .status != shared_world::QueryStatus::CLEAR)
                {
                    throw std::invalid_argument("spawn lacks full body support/clearance");
                }

                spawn.feet = SUPPORT.position;
            }

            manifest.chunks.push_back({0, 0, LOADED->Identity(), OUTPUT.filename().string()});
            manifestBytes = manifest.Encode();
        }

        std::ofstream result(OUTPUT, std::ios::binary);
        result.write(reinterpret_cast<const char*>(BYTES.data()), static_cast<std::streamsize>(BYTES.size()));
        result.close();
        if (!result)
        {
            throw std::runtime_error("map output write failed; do not publish partial output");
        }

        if (!manifestBytes.empty())
        {
            std::ofstream manifest(manifestPath, std::ios::binary);
            manifest << manifestBytes;
            manifest.close();
            if (!manifest)
            {
                throw std::runtime_error("manifest write failed; do not publish outputs");
            }

        }

        std::cout << "Baked map=" << PACKAGE.definition.id << " version=" << PACKAGE.definition.version
                  << " tiles=" << PACKAGE.tiles.size() << " bytes=" << BYTES.size() << '\n';
        return 0;
    }

    catch (const std::exception& error)
    {
        std::cerr << "MapBakeTool: " << error.what() << '\n';
        return 1;
    }

}

