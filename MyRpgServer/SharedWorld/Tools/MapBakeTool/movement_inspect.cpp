#include "../../Movement/MovementController.h"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 8)
        {
            throw std::invalid_argument("Usage: WorldMovementInspect map x y z dx dy ticks");
        }

        auto number = [](const char* p, int limit)
        {
            std::size_t used = 0;
            auto n = std::stoll(p, &used);
            if (used != std::string(p).size() || n < -limit || n > limit)
            {
                throw std::invalid_argument("invalid argument");
            }

            return static_cast<int>(n);
        };

        shared_world::StaticWorld world(shared_world::LoadedMap::LoadFile(argv[1]));
        shared_world::WorldPosition p{number(argv[2], 100000000), number(argv[3], 100000000),
                                      number(argv[4], 100000000)};
        const int DX = number(argv[5], 100), DY = number(argv[6], 100), TICKS = number(argv[7], 10000);
        if (TICKS <= 0)
        {
            throw std::invalid_argument("invalid ticks");
        }

        for (int i = 0; i < TICKS; ++i)
        {
            const auto MOVE = shared_world::MoveOnGround(world, p, DX, DY);
            p = MOVE.position;
            if (MOVE.status != shared_world::QueryStatus::CLEAR)
            {
                std::cout << "Stopped tick=" << i << " status=" << int(MOVE.status) << " feet=" << p.x << "," << p.y
                          << "," << p.z << "\n";
                return 2;
            }

        }

        std::cout << "Moved feet=" << p.x << "," << p.y << "," << p.z << "\n";
        return 0;
    }

    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }

}

