#include "../../Map/ChunkLayout.h"
#include "../../Map/ChunkLoadQueue.h"
#include "../../Movement/MovementController.h"
#include <iostream>
#include <chrono>

// Offline inspection: exercises the asset queue but WAITS for each completion.
// A game owner must poll without waiting and block movement while not resident.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 8)
        {
            throw std::invalid_argument("Usage: WorldChunkWalk world.rpgchunks x y z dx dy ticks");
        }

        const std::filesystem::path PATH(argv[1]);
        if (std::filesystem::file_size(PATH) > 16 * 1024 * 1024)
        {
            throw std::invalid_argument("index size limit");
        }

        std::ifstream index(PATH, std::ios::binary);
        const auto MANIFEST =
            shared_world::ChunkManifest::Decode(std::string(std::istreambuf_iterator<char>(index), {}));
        auto number = [](const char* s, int limit)
        {
            std::size_t used = 0;
            const auto N = std::stoll(s, &used);
            if (used != std::string(s).size() || N < -limit || N > limit)
            {
                throw std::invalid_argument("invalid integer");
            }

            return static_cast<int>(N);
        };

        shared_world::WorldPosition feet{number(argv[2], 100000000), number(argv[3], 100000000),
                                         number(argv[4], 100000000)};
        const int DX = number(argv[5], 20), DY = number(argv[6], 20), TICKS = number(argv[7], 10000);
        if (TICKS <= 0)
        {
            throw std::invalid_argument("invalid tick count");
        }

        shared_world::ChunkLoadQueue loader(
            MANIFEST, std::make_shared<const shared_world::FileMapDataProvider>(PATH.parent_path()), 1,
            shared_world::MAX_PACKAGE_BYTES);
        std::shared_ptr<const shared_world::StaticWorld> collision;
        shared_world::TileCoord loaded{};
        int loads = 0;
        for (int tick = 0; tick < TICKS; ++tick)
        {
            if (!MANIFEST.layout.world.bounds.Contains(feet))
            {
                throw std::invalid_argument("outside world");
            }

            const auto C = MANIFEST.layout.Coordinate(feet);
            if (!collision || !(loaded == C))
            {
                if (!loader.Submit(C, 1, static_cast<std::uint64_t>(loads) + 1))
                {
                    throw std::invalid_argument("chunk unavailable; movement blocked");
                }

                shared_world::ChunkLoadQueue::Result result;
                while (!loader.TryPop(result))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                if (result.status != shared_world::ChunkLoadQueue::Status::LOADED)
                {
                    throw std::invalid_argument("chunk load failed; movement blocked");
                }

                collision = std::move(result.collision);
                loaded = C;
                ++loads;
            }

            const auto STEP = shared_world::MoveOnGround(*collision, feet, DX, DY);
            feet = STEP.position;
            if (STEP.status != shared_world::QueryStatus::CLEAR)
            {
                std::cout << "Stopped tick=" << tick << " feet=" << feet.x << "," << feet.y << "," << feet.z << '\n';
                return 2;
            }

        }

        std::cout << "Moved feet=" << feet.x << "," << feet.y << "," << feet.z << " chunkLoads=" << loads << '\n';
        return 0;
    }

    catch (const std::exception& e)
    {
        std::cerr << "WorldChunkWalk: " << e.what() << '\n';
        return 1;
    }

}

