#include "CharacterCreationRules.h"
#include "../ServerCore/StructuredLogger.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

CharacterCreationRules& CharacterCreationRules::GetInstance()
{
    static CharacterCreationRules rules;
    return rules;
}

bool CharacterCreationRules::IsValidName(const std::string& name)
{
    // Deliberately narrow canonical subset: ASCII letters/digits and precomposed
    // Hangul syllables. Reject combining sequences, whitespace and invalid UTF-8.
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < name.size(); ++count)
    {
        const auto C = static_cast<unsigned char>(name[offset]);
        if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'))
        {
            ++offset;
        }
        else
        {
            if (offset + 3 > name.size() || C < 0xE0 || C > 0xEF)
            {
                return false;
            }

            const auto B = static_cast<unsigned char>(name[offset + 1]);
            const auto D = static_cast<unsigned char>(name[offset + 2]);
            if ((B & 0xC0) != 0x80 || (D & 0xC0) != 0x80)
            {
                return false;
            }

            const unsigned int CODEPOINT = ((C & 0x0F) << 12) | ((B & 0x3F) << 6) | (D & 0x3F);
            if (CODEPOINT < 0xAC00 || CODEPOINT > 0xD7A3)
            {
                return false;
            }

            offset += 3;
        }

    }

    return count >= 2 && count <= 24;
}

const RpgProtocol::CharacterSummary* CharacterCreationRules::Find(std::uint8_t race, std::uint8_t gender) const
{
    const auto FOUND = this->templates.find((static_cast<unsigned int>(race) << 8) | gender);
    return FOUND == this->templates.end() ? nullptr : &FOUND->second;
}

bool CharacterCreationRules::Load(const std::string& path)
{
    this->templates.clear();
    if (path.empty())
    {
        for (std::uint8_t gender = 0; gender < 2; ++gender)
        {
            RpgProtocol::CharacterSummary value;
            value.gender = gender;
            this->templates.emplace(gender, std::move(value));
        }

        return true;
    }

    try
    {
        std::ifstream input(path);
        const auto CONFIG = nlohmann::json::parse(input);
        for (const auto& entry : CONFIG.at("templates"))
        {
            const int RACE = entry.at("race").get<int>();
            const int GENDER = entry.at("gender").get<int>();
            const int LEVEL = entry.value("level", 1);
            if (RACE < 0 || RACE > 255 || GENDER < 0 || GENDER > 255 || LEVEL < 1 || LEVEL > 1000)
            {
                return false;
            }

            RpgProtocol::CharacterSummary value;
            value.race = static_cast<std::uint8_t>(RACE);
            value.gender = static_cast<std::uint8_t>(GENDER);
            value.level = static_cast<std::uint32_t>(LEVEL);
            std::set<int> slots;
            for (const auto& equipment : entry.at("equipment"))
            {
                const int SLOT = equipment.at("slot").get<int>();
                const int ITEM = equipment.at("itemTemplateId").get<int>();
                const int APPEARANCE = equipment.at("appearanceId").get<int>();
                if (SLOT < 0 || SLOT >= 16 || ITEM <= 0 || APPEARANCE < 0 || !slots.insert(SLOT).second)
                {
                    return false;
                }

                value.equipment.push_back({static_cast<std::uint8_t>(SLOT), static_cast<std::uint32_t>(ITEM),
                                           static_cast<std::uint32_t>(APPEARANCE)});
            }

            if (!this->templates
                     .emplace((static_cast<unsigned int>(RACE) << 8) | static_cast<unsigned int>(GENDER),
                              std::move(value))
                     .second)
            {
                return false;
            }

        }

        return !this->templates.empty();
    }

    catch (...)
    {
        RPG_LOG_ERROR("Configuration", "Invalid character creation rules file.");
        return false;
    }

}

