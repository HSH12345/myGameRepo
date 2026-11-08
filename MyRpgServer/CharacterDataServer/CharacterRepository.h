#pragma once

#include "../Protocol/CreateCharacterPackets.h"

class CharacterRepository final

{

public:
    static CharacterRepository& GetInstance();

    bool ValidateSchema();

    bool FindByAccount(const std::string& accountUid, std::vector<RpgProtocol::CharacterSummary>& characters);

    RpgProtocol::CreateCharacterResponsePacket Create(const RpgProtocol::InternalCreateCharacterRequestPacket& request);
};

