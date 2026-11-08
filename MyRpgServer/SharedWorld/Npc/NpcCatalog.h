#pragma once
#include "../Core/WorldDataFile.h"
#include "../Core/CoordinateSystem.h"
#include "../Entity/EntityStats.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace shared_world::npc
{
inline constexpr std::uint64_t ENTITY_BIT = 1ULL << 63;
inline constexpr std::size_t MAX_PER_MAP = 256;
enum class Role : std::uint32_t
{
    AMBIENT = 0,
    DIALOGUE = 1,
    MERCHANT = 2,
    QUEST = 3,
    GUARD = 4,
    DIALOGUE_WARP = 5
};

struct Warp
{
    std::uint32_t id = 0, targetMap = 0, range = 300;
    std::string text, option;
};

inline bool InRange(WorldPosition player, WorldPosition npc, std::uint32_t range)
{
    return IsValid(player) && IsValid(npc) && DistanceSquared2D(player, npc) <= std::uint64_t(range) * range &&
           std::abs(std::int64_t(player.z) - npc.z) <= 100;
}

struct Definition
{
    std::uint32_t index = 0, appearance = 0, role = 0, roleData = 0;
    std::string name;
    std::uint32_t combatType = 0; // 0: service/dialogue, 1: combat (Guard role).
    EntityStats stats;
};

struct Appearance
{
    std::uint32_t index = 0;
    std::string mesh, idle;
    std::int32_t yaw = 0, scaleMilli = 1000;
};

struct Placement
{
    std::uint32_t id = 0, map = 0, npc = 0;
    WorldPosition feet{};
    std::int32_t yaw = 0;

    std::uint64_t Entity() const
    {
        return ENTITY_BIT | this->id;
    }

};

// Immutable after startup. Runtime state and entity lifetime belong to each map owner.
struct Catalog
{
    std::unordered_map<std::uint32_t, Definition> definitions;
    std::unordered_map<std::uint32_t, Appearance> appearances;
    std::vector<Placement> placements;
    std::unordered_map<std::uint32_t, Warp> warps;
    std::uint64_t checksum = 14695981039346656037ULL;
    using Rows = std::vector<std::vector<std::string>>;

    static std::int32_t Number(const std::string& text, std::int32_t low, std::int32_t high)
    {
        std::int32_t n = 0;
        const auto RESULT = std::from_chars(text.data(), text.data() + text.size(), n);
        if (RESULT.ec != std::errc{} || RESULT.ptr != text.data() + text.size() || n < low || n > high)
        {
            throw std::invalid_argument("NPC catalog integer out of range");
        }

        return n;
    }

    Rows Read(const std::filesystem::path& file, const std::string& header)
    {
        const auto TEXT = ReadWorldDataFile<std::string>(file, 0, 1024 * 1024, "NPC table unavailable or over budget",
                                                        "NPC table unavailable or over budget", "NPC table read failed");
        Rows rows;
        std::size_t start = 0;
        bool first = true;
        while (start < TEXT.size())
        {
            const auto END = TEXT.find('\n', start);
            auto line = TEXT.substr(start, END == std::string::npos ? END : END - start);
            start = END == std::string::npos ? TEXT.size() : END + 1;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.empty())
            {
                continue;
            }

            for (unsigned char c : line)
            {
                this->checksum ^= c;
                this->checksum *= 1099511628211ULL;
            }

            this->checksum ^= '\n';
            this->checksum *= 1099511628211ULL;
            if (first)
            {
                if (line != header)
                {
                    throw std::invalid_argument("NPC table schema mismatch");
                }

                first = false;
                continue;
            }

            std::vector<std::string> row;
            std::size_t pos = 0;
            while (true)
            {
                auto tab = line.find('\t', pos);
                row.push_back(line.substr(pos, tab == std::string::npos ? tab : tab - pos));
                if (tab == std::string::npos)
                {
                    break;
                }

                pos = tab + 1;
            }

            if (rows.size() >= 4096)
            {
                throw std::invalid_argument("NPC table row budget");
            }

            rows.push_back(std::move(row));
        }

        if (first)
        {
            throw std::invalid_argument("NPC table missing header");
        }

        return rows;
    }

    static Catalog Load(const std::filesystem::path& root)
    {
        Catalog result;
        auto id = [](const std::string& s)
        {
            return static_cast<std::uint32_t>(Number(s, 1, INT32_MAX));
        };

        auto asset = [](const std::string& s)
        {
            return s.size() < 256 && s.rfind("/Game/", 0) == 0 && s.find("..") == std::string::npos &&
                   s.find('.') != std::string::npos;
        };

        for (const auto& r : result.Read(root / "NpcWarps.tsv", "warpId\ttargetMapId\trangeCm\tdialogue\toptionLabel"))
        {
            if (r.size() != 5 || r[3].empty() || r[3].size() > 512 || r[4].empty() || r[4].size() > 128)
            {
                throw std::invalid_argument("NPC warp schema");
            }

            Warp w{id(r[0]), id(r[1]), static_cast<std::uint32_t>(Number(r[2], 50, 500)), r[3], r[4]};
            if (result.warps.size() >= 256 || !result.warps.emplace(w.id, w).second)
            {
                throw std::invalid_argument("NPC duplicate warp or budget");
            }

        }

        for (const auto& r :
             result.Read(root / "NpcAppearances.tsv", "appearanceIndex\tmesh\tidle\tyawMilliDegrees\tscaleMilli"))
        {
            if (r.size() != 5 || !asset(r[1]) || !asset(r[2]))
            {
                throw std::invalid_argument("NPC appearance schema");
            }

            Appearance a{id(r[0]), r[1], r[2], Number(r[3], -180000, 180000), Number(r[4], 10, 10000)};
            if (result.appearances.size() >= 256 || !result.appearances.emplace(a.index, a).second)
            {
                throw std::invalid_argument("NPC duplicate appearance or budget");
            }

        }

        for (const auto& r :
             result.Read(root / "NpcDefinitions.tsv", "npcIndex\tname\tappearanceIndex\trole\troleDataId\tcombatType\th"
                                                      "p\tmp\tattack\tdefense\tevasion\taccuracy"))
        {
            if (r.size() != 12 || r[1].empty() || r[1].size() > 128)
            {
                throw std::invalid_argument("NPC definition schema");
            }

            Definition d{id(r[0]), id(r[2]), static_cast<std::uint32_t>(Number(r[3], 0, 5)),
                         static_cast<std::uint32_t>(Number(r[4], 0, INT32_MAX)), r[1]};
            d.combatType = Number(r[5], 0, 1);
            d.stats.hp = d.stats.maxHp = Number(r[6], 0, EntityStats::LIMIT);
            d.stats.mp = d.stats.maxMp = Number(r[7], 0, EntityStats::LIMIT);
            d.stats.attack = Number(r[8], 0, EntityStats::LIMIT);
            d.stats.defense = Number(r[9], 0, EntityStats::LIMIT);
            d.stats.evasion = Number(r[10], 0, 100);
            d.stats.accuracy = Number(r[11], 0, 100);
            d.stats.flags = d.combatType ? EntityStats::ATTACKABLE | EntityStats::MORTAL : 0;
            if (!d.stats.Valid() || (d.role == static_cast<unsigned>(Role::GUARD)) != (d.combatType == 1))
            {
                throw std::invalid_argument("NPC combat type, role or stats inconsistent");
            }

            if (d.role == static_cast<unsigned>(Role::DIALOGUE_WARP) && !result.warps.count(d.roleData))
            {
                throw std::invalid_argument("NPC dialogue warp has unknown role data");
            }

            if (!result.appearances.count(d.appearance) || result.definitions.size() >= 256 ||
                !result.definitions.emplace(d.index, d).second)
            {
                throw std::invalid_argument("NPC duplicate definition, unknown appearance or budget");
            }

        }

        std::unordered_set<std::uint32_t> ids;
        std::unordered_map<std::uint32_t, std::size_t> counts;
        for (const auto& r :
             result.Read(root / "NpcPlacements.tsv", "spawnId\tmapId\tnpcIndex\tx\ty\tz\tyawMilliDegrees"))
        {
            if (r.size() != 7)
            {
                throw std::invalid_argument("NPC placement schema");
            }

            Placement p{id(r[0]),
                        id(r[1]),
                        id(r[2]),
                        {Number(r[3], -COORDINATE_LIMIT, COORDINATE_LIMIT),
                         Number(r[4], -COORDINATE_LIMIT, COORDINATE_LIMIT),
                         Number(r[5], -COORDINATE_LIMIT, COORDINATE_LIMIT)},
                        Number(r[6], -180000, 180000)};
            if (!ids.insert(p.id).second || !result.definitions.count(p.npc) || ++counts[p.map] > MAX_PER_MAP)
            {
                throw std::invalid_argument("NPC duplicate spawn, unknown definition or map budget");
            }

            result.placements.push_back(p);
        }

        return result;
    }

};

}

