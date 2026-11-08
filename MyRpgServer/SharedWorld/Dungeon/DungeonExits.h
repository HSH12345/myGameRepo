#pragma once
#include "../Core/MapDefinition.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>

namespace shared_world::dungeon
{
enum class ExitType : std::uint32_t
{
    NONE = 0,
    NORMAL = 1,
    EMERGENCY = 2,
    SPECIAL = 3
};

// Classification only for now. No rewards, penalties or persistence are applied.
enum class ExitOutcome : std::uint32_t
{
    NONE = 0,
    NORMAL_RETURN = 1,
    EMERGENCY_RETURN = 2,
    SPECIAL_RETURN = 3
};

enum class ExitError : std::uint32_t
{
    NOT_DUNGEON = 1,
    STALE_RUN = 2,
    UNKNOWN_EXIT = 3,
    TOO_FAR = 4,
    UNAVAILABLE = 5
};

inline const char* Name(ExitType type)
{
    switch (type)
    {
    case ExitType::NORMAL:
        return "Normal Exit";
    case ExitType::EMERGENCY:
        return "Emergency Exit";
    case ExitType::SPECIAL:
        return "Special Exit";
    default:
        return "Exit";
    }

}

struct Zone
{
    std::uint32_t id = 0;
    ExitType type = ExitType::NONE;
    ExitOutcome outcome = ExitOutcome::NONE;
    WorldPosition feet{};
    std::uint32_t radius = 250;

    bool Valid() const
    {
        return this->id && static_cast<unsigned>(this->type) >= 1 && static_cast<unsigned>(this->type) <= 3 &&
               static_cast<unsigned>(this->outcome) >= 1 && static_cast<unsigned>(this->outcome) <= 3 &&
               IsValid(this->feet) && this->radius >= 50 && this->radius <= 500;
    }

    bool Contains(WorldPosition p) const
    {
        return IsValid(p) && DistanceSquared2D(p, this->feet) <= std::uint64_t(this->radius) * this->radius &&
               std::abs(std::int64_t(p.z) - this->feet.z) <= 100;
    }

};

struct Layout
{
    std::uint64_t run = 0;
    std::uint32_t count = 0;
    std::array<Zone, 3> zones{};

    bool Valid() const
    {
        if (!this->run || this->count > 3)
        {
            return false;
        }

        for (unsigned i = 0; i < this->count; ++i)
        {
            if (!this->zones[i].Valid())
            {
                return false;
            }

            for (unsigned j = 0; j < i; ++j)
            {
                if (this->zones[i].id == this->zones[j].id)
                {
                    return false;
                }

            }

        }

        return true;
    }

};

struct Receipt
{
    std::uint64_t sourceInstance = 0, run = 0, sequence = 0;
    std::uint32_t exitId = 0;
    ExitType type = ExitType::NONE;
    ExitOutcome outcome = ExitOutcome::NONE;

    bool Valid() const
    {
        return this->sourceInstance && this->run && this->sequence && this->exitId &&
               static_cast<unsigned>(this->type) >= 1 && static_cast<unsigned>(this->type) <= 3 &&
               static_cast<unsigned>(this->outcome) >= 1 && static_cast<unsigned>(this->outcome) <= 3;
    }

};

struct Rule
{
    std::uint32_t map = 0, minCount = 2, maxCount = 3;
    std::int32_t inset = 700, jitter = 200;
    std::uint32_t radius = 250;

    void Validate(const WorldBounds& bounds) const
    {
        if (!this->map || this->minCount < 2 || this->maxCount > 3 || this->minCount > this->maxCount ||
            this->jitter < 0 || this->jitter > 1000 || this->radius < 50 || this->radius > 500 ||
            this->inset <= this->jitter + static_cast<int>(this->radius) + 34 || !bounds.IsValid() ||
            2 * (std::int64_t(this->inset) + this->jitter + this->radius + 34) >=
                std::int64_t(bounds.max.x) - bounds.min.x ||
            2 * (std::int64_t(this->inset) + this->jitter + this->radius + 34) >=
                std::int64_t(bounds.max.y) - bounds.min.y)
        {
            throw std::invalid_argument("invalid dungeon exit rule/bounds");
        }

    }

};

inline std::unordered_map<std::uint32_t, Rule> LoadRules(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || in.tellg() > 64 * 1024)
    {
        throw std::invalid_argument("dungeon exit rules missing/over budget");
    }

    in.seekg(0);
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    if (line != "mapId\tminCount\tmaxCount\tcornerInsetCm\tjitterCm\tradiusCm")
    {
        throw std::invalid_argument("dungeon exit rule schema");
    }

    std::unordered_map<std::uint32_t, Rule> rules;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            continue;
        }

        std::istringstream row(line);
        Rule r;
        std::string extra;
        if (!(row >> r.map >> r.minCount >> r.maxCount >> r.inset >> r.jitter >> r.radius) || (row >> extra) ||
            rules.size() >= 64 || !r.map || r.map > INT32_MAX || r.minCount < 2 || r.maxCount > 3 ||
            r.minCount > r.maxCount || r.jitter < 0 || r.jitter > 1000 || r.radius < 50 || r.radius > 500 ||
            r.inset <= r.jitter + static_cast<int>(r.radius) + 34 || r.inset > 1000000 ||
            !rules.emplace(r.map, r).second)
        {
            throw std::invalid_argument("invalid/duplicate dungeon exit rule");
        }

    }

    return rules;
}

inline Layout Generate(const Rule& rule, const WorldBounds& bounds, std::int32_t z, std::uint64_t run,
                       std::mt19937_64& random)
{
    rule.Validate(bounds);
    if (!run)
    {
        throw std::invalid_argument("dungeon run generation required");
    }

    Layout result;
    result.run = run;
    result.count = std::uniform_int_distribution<unsigned>(rule.minCount, rule.maxCount)(random);
    std::array<unsigned, 4> corners{0, 1, 2, 3};
    std::array<unsigned, 3> types{1, 2, 3};
    std::shuffle(corners.begin(), corners.end(), random);
    std::shuffle(types.begin(), types.end(), random);
    std::uniform_int_distribution<int> offset(-rule.jitter, rule.jitter);
    for (unsigned i = 0; i < result.count; ++i)
    {
        const auto C = corners[i];
        const auto X_VALUE = (C & 1 ? bounds.max.x - rule.inset : bounds.min.x + rule.inset) + offset(random);
        const auto Y = (C & 2 ? bounds.max.y - rule.inset : bounds.min.y + rule.inset) + offset(random);
        result.zones[i] = {
            i + 1, static_cast<ExitType>(types[i]), static_cast<ExitOutcome>(types[i]), {X_VALUE, Y, z}, rule.radius};
    }

    if (!result.Valid())
    {
        throw std::invalid_argument("invalid dungeon exit layout");
    }

    return result;
}

}

