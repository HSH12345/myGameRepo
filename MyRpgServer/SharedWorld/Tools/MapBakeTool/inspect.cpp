#include "../../Collision/StaticCollision.h"
#include "../../Map/MapLoader.h"
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2 && argc != 5)
        {
            throw std::invalid_argument("Usage: WorldMapInspect map.rpgmap [probeX probeY probeZ]");
        }

        const auto MAP = shared_world::LoadedMap::LoadFile(argv[1]);
        const auto IDENTITY = MAP->Identity();
        std::cout << "Map id=" << IDENTITY.id << " version=" << IDENTITY.version << " tiles=" << MAP->Tiles().size()
                  << " checksum=" << std::hex << std::setw(16) << std::setfill('0') << IDENTITY.checksum << '\n';
        if (argc == 5)
        {
            const auto COORDINATE = [](const char* text)
            {
                std::size_t used = 0;
                const auto VALUE = std::stoll(text, &used);
                if (used != std::string(text).size() || VALUE < -shared_world::COORDINATE_LIMIT ||
                    VALUE > shared_world::COORDINATE_LIMIT)
                {
                    throw std::invalid_argument("invalid probe coordinate");
                }

                return static_cast<std::int32_t>(VALUE);
            };

            const shared_world::WorldPosition POINT{COORDINATE(argv[2]), COORDINATE(argv[3]), COORDINATE(argv[4])};
            const shared_world::StaticWorld COLLISION(MAP);
            if (!COLLISION.IsWalkable(POINT))
            {
                throw std::runtime_error("probe is not on a walkable surface");
            }

            std::cout << "Walkable probe passed\n";
        }

        return 0;
    }

    catch (const std::exception& error)
    {
        std::cerr << "WorldMapInspect: " << error.what() << '\n';
        return 1;
    }

}

