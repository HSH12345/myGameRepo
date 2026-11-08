#pragma once
#include "../Npc/NpcCatalog.h"
#include <array>
#include <map>

namespace shared_world::skills
{
enum class Aim : std::uint32_t
{
    TARGET = 0,
    GROUND = 1,
    DIRECTION = 2,
    SELF = 3
};

enum class Motion : std::uint32_t
{
    NONE = 0,
    DASH = 1,
    BACKSTEP = 2
};

struct Definition
{
    std::uint32_t id = 0;
    std::string name, icon;
    Aim aim = Aim::TARGET;
    Motion motion = Motion::NONE;
    bool piercing = false;
    std::uint32_t move = 0, initialRank = 0, maxRank = 0;
};

struct Rank
{
    std::uint32_t skill = 0, rank = 0, cost = 0, level = 1, attack = 0, flat = 0, mp = 0, cast = 1, recovery = 0,
                  cooldown = 1;
};

struct State
{
    std::uint32_t points = 0, spent = 0, level = 1;
    std::array<std::uint8_t, 65> ranks{};
};

struct Catalog
{
    std::map<std::uint32_t, Definition> definitions;
    std::map<std::pair<std::uint32_t, std::uint32_t>, Rank> ranks;
    std::uint32_t initialPoints = 0, pointsPerLevel = 0;
    std::uint64_t checksum = 0;

    static Catalog Load(const std::filesystem::path& root)
    {
        Catalog c;
        npc::Catalog reader;
        auto n = [](const std::string& s, int lo, int hi)
        {
            return static_cast<std::uint32_t>(npc::Catalog::Number(s, lo, hi));
        };

        std::unordered_set<std::string> keys;
        for (const auto& r : reader.Read(root / "SkillRules.tsv", "key\tvalue"))
        {
            if (r.size() != 2 || !keys.insert(r[0]).second)
            {
                throw std::invalid_argument("skill rule schema");
            }

            if (r[0] == "initialPoints")
            {
                c.initialPoints = n(r[1], 0, 1000);
            }
            else if (r[0] == "pointsPerLevel")
            {
                c.pointsPerLevel = n(r[1], 0, 100);
            }
            else
            {
                throw std::invalid_argument("unknown skill rule");
            }

        }

        if (keys.size() != 2)
        {
            throw std::invalid_argument("skill rules missing");
        }

        if (c.Budget(10000) > 1000000)
        {
            throw std::invalid_argument("skill point wire budget");
        }

        for (const auto& r : reader.Read(root / "PlayerSkills.tsv",
                                         "abilityId\tname\ticon\taim\tmotion\tpiercing\tmoveCm\tinitialRank\tmaxRank"))
        {
            if (r.size() != 9 || r[1].empty() || r[1].size() > 80 || r[2].empty() || r[2].size() > 24 ||
                c.definitions.size() >= 16)
            {
                throw std::invalid_argument("player skill schema");
            }

            Definition d{n(r[0], 3, 64),
                         r[1],
                         r[2],
                         static_cast<Aim>(n(r[3], 0, 3)),
                         static_cast<Motion>(n(r[4], 0, 2)),
                         n(r[5], 0, 1) != 0,
                         n(r[6], 0, 1000),
                         n(r[7], 0, 5),
                         n(r[8], 1, 5)};
            if (d.initialRank > d.maxRank || ((d.motion == Motion::NONE) != (d.move == 0)) ||
                !c.definitions.emplace(d.id, d).second)
            {
                throw std::invalid_argument("player skill policy");
            }

        }

        for (const auto& r :
             reader.Read(root / "SkillRanks.tsv", "abilityId\trank\tpointCost\trequiredLevel\tattackPermille\tflatDamag"
                                                  "e\tmp\tcastMs\trecoveryMs\tcooldownMs"))
        {
            if (r.size() != 10)
            {
                throw std::invalid_argument("skill rank schema");
            }

            Rank v{n(r[0], 3, 64),      n(r[1], 1, 5),       n(r[2], 1, 100),   n(r[3], 1, 10000), n(r[4], 0, 10000),
                   n(r[5], 0, 1000000), n(r[6], 0, 1000000), n(r[7], 1, 10000), n(r[8], 0, 10000), n(r[9], 1, 60000)};
            if (!c.definitions.count(v.skill) || v.rank > c.definitions.at(v.skill).maxRank ||
                !c.ranks.emplace(std::make_pair(v.skill, v.rank), v).second)
            {
                throw std::invalid_argument("skill rank reference");
            }

        }

        for (const auto& [id, d] : c.definitions)
        {
            for (unsigned rank = 1; rank <= d.maxRank; ++rank)
            {
                if (!c.ranks.count({id, rank}))
                {
                    throw std::invalid_argument("skill rank gap");
                }

            }

        }

        if (c.definitions.empty())
        {
            throw std::invalid_argument("player skills missing");
        }

        c.checksum = reader.checksum;
        return c;
    }

    std::uint32_t Budget(std::uint32_t level) const
    {
        if (!level || level > 10000)
        {
            throw std::invalid_argument("skill level");
        }

        return this->initialPoints + (level - 1) * this->pointsPerLevel;
    }

    State Initial(std::uint32_t level = 1) const
    {
        State s;
        s.level = level;
        s.points = Budget(level);
        for (const auto& [id, d] : this->definitions)
        {
            s.ranks[id] = static_cast<std::uint8_t>(d.initialRank);
        }

        return s;
    }

    bool Valid(const State& s) const
    {
        if (!s.level || s.level > 10000 || std::uint64_t(s.points) + s.spent != Budget(s.level))
        {
            return false;
        }

        std::uint32_t spent = 0;
        for (unsigned id = 0; id < s.ranks.size(); ++id)
        {
            const auto IT = this->definitions.find(id);
            if (IT == this->definitions.end())
            {
                if (s.ranks[id])
                {
                    return false;
                }

                continue;
            }

            if (s.ranks[id] < IT->second.initialRank || s.ranks[id] > IT->second.maxRank)
            {
                return false;
            }

            for (unsigned rank = IT->second.initialRank + 1; rank <= s.ranks[id]; ++rank)
            {
                spent += this->ranks.at({id, rank}).cost;
            }

        }

        return spent == s.spent;
    }

    bool Train(State& s, std::uint32_t id, std::uint32_t nextRank) const
    {
        const auto D = this->definitions.find(id);
        if (!Valid(s) || D == this->definitions.end() || nextRank != std::uint32_t(s.ranks[id]) + 1 ||
            nextRank > D->second.maxRank)
        {
            return false;
        }

        const auto& rank = this->ranks.at({id, nextRank});
        if (s.level < rank.level || s.points < rank.cost)
        {
            return false;
        }

        s.points -= rank.cost;
        s.spent += rank.cost;
        s.ranks[id] = static_cast<std::uint8_t>(nextRank);
        return true;
    }

};

}

