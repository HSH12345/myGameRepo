#pragma once

#include "../Protocol/CreateCharacterPackets.h"

#include <map>

class CharacterCreationRules final

{

public:
    static CharacterCreationRules& GetInstance();

    bool Load(const std::string& path);

    static bool IsValidName(const std::string& name);

    const RpgProtocol::CharacterSummary* Find(std::uint8_t race, std::uint8_t gender) const;

private:
    std::map<unsigned int, RpgProtocol::CharacterSummary> templates;
};

