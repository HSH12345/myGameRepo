#pragma once

#include "../Protocol/WorldDataPackets.h"

class WorldRepository final

{

public:
    static bool ValidateSchema();

    static RpgProtocol::WorldDataResponse Execute(const RpgProtocol::WorldDataRequest& request);
};

