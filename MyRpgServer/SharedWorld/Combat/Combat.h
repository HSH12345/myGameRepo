#pragma once
#include "../Entity/EntityStats.h"
#include "../Npc/NpcCatalog.h"
#include <cmath>
#include <map>
#include <optional>
#include "BodyGeometry.h"
#include "Skills.h"

namespace shared_world::combat
{
inline constexpr std::size_t MAX_PROJECTILES = 128;
enum class Shape : std::uint32_t
{
    MELEE = 0,
    PROJECTILE = 1,
    CIRCLE = 2,
    CONE = 3,
    LINE = 4
};

enum class Phase : std::uint32_t
{
    STARTED = 1,
    FIRED,
    HIT,
    CANCELLED,
    DEATH,
    PROJECTILE,
    PROJECTILE_END,
    RESPAWN,
    REJECTED,
    CLOCK
};

enum class Reject : std::uint32_t
{
    NONE = 0,
    UNAVAILABLE,
    DEAD,
    BUSY,
    TARGET,
    RANGE,
    MP,
    COOLDOWN,
    RATE_LIMITED,
    BLOCKED,
    CANCELLED
};

struct Ability
{
    std::uint32_t id = 0;
    std::string name;
    Shape shape = Shape::MELEE;
    std::uint32_t attackPermille = 1000, flatDamage = 0, mp = 0, castMs = 0, recoveryMs = 0, cooldownMs = 0, range = 0,
                  radius = 0, angle = 0, speed = 0;
};

struct Monster
{
    std::uint32_t npc = 0, basic = 0, skill = 0, aggro = 0, leash = 0, speed = 0, respawnMs = 0;
    std::uint32_t bodyRadius = 40, bodyHeight = 160, solidRadius = 0;
};

struct Visual
{
    std::uint32_t appearance = 0;
    std::string move, attack, cast, hit, death;
    std::map<std::uint32_t, std::string> skills;
};

enum class Action : std::uint32_t
{
    WAIT = 0,
    WANDER,
    APPROACH,
    ATTACK
};

struct Step
{
    std::uint32_t order = 0;
    Action action = Action::WAIT;
    std::uint32_t ability = 0, durationMs = 0, distance = 0;
};

struct Pattern
{
    std::uint32_t npc = 0, state = 0, id = 0, weight = 0;
};

struct Event
{
    std::uint64_t source = 0, target = 0, action = 0, life = 1, serverMs = 0, startMs = 0, endMs = 0;
    std::uint32_t skill = 0, value = 0, reason = 0;
    Phase phase = Phase::CLOCK;
    WorldPosition origin{}, point{};
    std::int32_t yaw = 0;
};

struct Catalog
{
    skills::Catalog skills;
    std::map<std::uint32_t, Ability> abilities;
    std::map<std::uint32_t, Monster> monsters;
    std::map<std::uint32_t, Visual> visuals;
    std::map<std::uint32_t, std::vector<Step>> steps;
    std::vector<Pattern> patterns;
    int playerAccuracyOverride = -1;
    std::uint32_t playerRespawnMs = 5000;
    std::uint64_t checksum = 0;

    static Catalog Load(const std::filesystem::path& root)
    {
        npc::Catalog reader;
        Catalog result;
        auto n = [](const std::string& s, int low, int high)
        {
            return static_cast<std::uint32_t>(npc::Catalog::Number(s, low, high));
        };

        std::unordered_set<std::string> rules;
        for (const auto& r : reader.Read(root / "Rules.tsv", "key\tvalue"))
        {
            if (r.size() != 2 || !rules.insert(r[0]).second)
            {
                throw std::invalid_argument("combat rule schema");
            }

            if (r[0] == "playerAccuracyOverride")
            {
                result.playerAccuracyOverride = npc::Catalog::Number(r[1], -1, 100);
            }
            else if (r[0] == "playerRespawnMs")
            {
                result.playerRespawnMs = n(r[1], 1000, 60000);
            }
            else
            {
                throw std::invalid_argument("unknown combat rule");
            }

        }

        if (rules.size() != 2)
        {
            throw std::invalid_argument("combat rules missing");
        }

        for (const auto& r :
             reader.Read(root / "Abilities.tsv", "id\tname\tshape\tattackPermille\tflatDamage\tmp\tcastMs\trecoveryMs\t"
                                                 "cooldownMs\trangeCm\tradiusCm\tangleDegrees\tspeedCmPerSec"))
        {
            if (r.size() != 13 || r[1].empty() || r[1].size() > 80)
            {
                throw std::invalid_argument("combat ability schema");
            }

            Ability a{n(r[0], 1, 64),
                      r[1],
                      static_cast<Shape>(n(r[2], 0, 4)),
                      n(r[3], 0, 10000),
                      n(r[4], 0, 1000000),
                      n(r[5], 0, 1000000),
                      n(r[6], 1, 10000),
                      n(r[7], 0, 10000),
                      n(r[8], 1, 60000),
                      n(r[9], 50, 2000),
                      n(r[10], 0, 1000),
                      n(r[11], 0, 360),
                      n(r[12], 0, 6000)};
            if ((a.shape == Shape::PROJECTILE && !a.speed) ||
                ((a.shape == Shape::CIRCLE || a.shape == Shape::CONE || a.shape == Shape::LINE) && !a.radius) ||
                (a.shape == Shape::CONE && !a.angle) || result.abilities.size() >= 32 ||
                !result.abilities.emplace(a.id, a).second)
            {
                throw std::invalid_argument("combat ability policy");
            }

        }

        for (const auto& r :
             reader.Read(root / "Monsters.tsv", "npcIndex\tbasicAbility\tskillAbility\taggroCm\tleashCm\tspeedCmPerSec"
                                                "\trespawnMs\tbodyRadiusCm\tbodyHeightCm\tsolidRadiusCm"))
        {
            if (r.size() != 10)
            {
                throw std::invalid_argument("combat monster schema");
            }

            Monster m{n(r[0], 1, INT32_MAX), n(r[1], 0, 64),  n(r[2], 0, 64),        n(r[3], 100, 6000),
                      n(r[4], 100, 8000),    n(r[5], 1, 600), n(r[6], 3000, 120000), n(r[7], 10, 1000),
                      n(r[8], 40, 2000),     n(r[9], 0, 1000)};
            if (m.solidRadius > m.bodyRadius || m.leash < m.aggro || (m.basic && !result.abilities.count(m.basic)) ||
                (m.skill && !result.abilities.count(m.skill)) || !result.monsters.emplace(m.npc, m).second)
            {
                throw std::invalid_argument("combat monster reference");
            }

        }

        for (const auto& r : reader.Read(root / "Visuals.tsv", "appearanceIndex\tmove\tattack\tcast\thit\tdeath"))
        {
            if (r.size() != 6)
            {
                throw std::invalid_argument("combat visual schema");
            }

            for (unsigned i = 1; i < 6; ++i)
            {
                if (r[i].size() > 255 || r[i].rfind("/Game/", 0) != 0 || r[i].find('.') == std::string::npos)
                {
                    throw std::invalid_argument("combat visual path");
                }

            }

            Visual v{n(r[0], 0, INT32_MAX), r[1], r[2], r[3], r[4], r[5]};
            if (!result.visuals.emplace(v.appearance, v).second)
            {
                throw std::invalid_argument("combat visual duplicate");
            }

        }

        for (const auto& r :
             reader.Read(root / "PatternSteps.tsv", "patternId\tstep\taction\tabilityId\tdurationMs\tdistanceCm"))
        {
            if (r.size() != 6)
            {
                throw std::invalid_argument("combat pattern step schema");
            }

            const auto ID = n(r[0], 1, 256);
            Step s{n(r[1], 1, 8), static_cast<Action>(n(r[2], 0, 3)), n(r[3], 0, 64), n(r[4], 1, 30000),
                   n(r[5], 0, 2000)};
            auto& currentSteps = result.steps[ID];
            if (s.order != currentSteps.size() + 1 ||
                (s.action == Action::ATTACK ? !result.abilities.count(s.ability) : s.ability != 0))
            {
                throw std::invalid_argument("combat pattern step order/reference");
            }

            currentSteps.push_back(s);
        }

        for (const auto& r : reader.Read(root / "SkillVisuals.tsv", "appearanceIndex\tabilityId\tanimation"))
        {
            if (r.size() != 3 || r[2].size() > 255 || r[2].rfind("/Game/", 0) != 0 ||
                r[2].find('.') == std::string::npos)
            {
                throw std::invalid_argument("combat skill visual schema");
            }

            const auto APPEARANCE = n(r[0], 0, INT32_MAX), ABILITY = n(r[1], 1, 64);
            if (!result.visuals.count(APPEARANCE) || !result.abilities.count(ABILITY) ||
                !result.visuals.at(APPEARANCE).skills.emplace(ABILITY, r[2]).second)
            {
                throw std::invalid_argument("combat skill visual reference");
            }

        }

        for (const auto& r : reader.Read(root / "NpcPatterns.tsv", "npcIndex\tstate\tpatternId\tweight"))
        {
            if (r.size() != 4)
            {
                throw std::invalid_argument("combat pattern schema");
            }

            Pattern p{n(r[0], 1, INT32_MAX), n(r[1], 0, 1), n(r[2], 1, 256), n(r[3], 1, 1000)};
            if (!result.monsters.count(p.npc) || !result.steps.count(p.id))
            {
                throw std::invalid_argument("combat pattern reference");
            }

            if (!result.monsters.at(p.npc).basic)
            {
                for (const auto& step : result.steps.at(p.id))
                {
                    if (step.action == Action::ATTACK && step.ability <= 2)
                    {
                        throw std::invalid_argument("skill-only monster has basic attack pattern");
                    }

                }

            }

            for (const auto& old : result.patterns)
            {
                if (old.npc == p.npc && old.state == p.state && old.id == p.id)
                {
                    throw std::invalid_argument("combat pattern duplicate");
                }

            }

            if (p.state == 0)
            {
                for (const auto& step : result.steps.at(p.id))
                {
                    if (step.action == Action::ATTACK || step.action == Action::APPROACH)
                    {
                        throw std::invalid_argument("idle pattern needs no target");
                    }

                }

            }

            result.patterns.push_back(p);
        }

        for (const auto& [id, monster] : result.monsters)
        {
            for (unsigned state = 0; state < 2; ++state)
            {
                if (std::none_of(result.patterns.begin(), result.patterns.end(),
                                 [&](const auto& p)
                                 {
                                     return p.npc == id && p.state == state;
                                 }))

                {
                    throw std::invalid_argument("monster pattern state missing");
                }

            }

        }

        for (auto id : {1U, 2U, 3U, 4U})
        {
            if (!result.abilities.count(id))
            {
                throw std::invalid_argument("player ability missing");
            }

        }

        result.skills = skills::Catalog::Load(root);
        for (const auto& [id, d] : result.skills.definitions)
        {
            if (!result.abilities.count(id))
            {
                throw std::invalid_argument("player skill ability missing");
            }

        }

        result.checksum = reader.checksum ^ result.skills.checksum;
        return result;
    }

};

inline Ability Ranked(Ability a, const skills::Rank& r)
{
    a.attackPermille = r.attack;
    a.flatDamage = r.flat;
    a.mp = r.mp;
    a.castMs = r.cast;
    a.recoveryMs = r.recovery;
    a.cooldownMs = r.cooldown;
    return a;
}

// Cursor aiming uses a bounded horizontal direction, never a client-authored damage position.
inline std::optional<WorldPosition> DirectedPoint(WorldPosition origin, WorldPosition cursor, std::uint32_t range)
{
    const double DX = double(cursor.x) - origin.x, DY = double(cursor.y) - origin.y;
    const double LENGTH = std::sqrt(DX * DX + DY * DY);
    if (LENGTH < 1)
    {
        return {};
    }

    WorldPosition end{origin.x + static_cast<int>(std::round(DX / LENGTH * range)),
                      origin.y + static_cast<int>(std::round(DY / LENGTH * range)), origin.z};
    if (!shared_world::IsValid(end))
    {
        return {};
    }

    return end;
}

inline bool BodyInLine(WorldPosition origin, WorldPosition point, std::int32_t yaw, unsigned length, unsigned halfWidth,
                       unsigned bodyRadius)
{
    if (std::abs(std::int64_t(point.z) - origin.z) > 150)
    {
        return false;
    }

    const double ANGLE = yaw * 3.141592653589793 / 180000, DX = double(point.x) - origin.x,
                 DY = double(point.y) - origin.y;
    const double ALONG = DX * std::cos(ANGLE) + DY * std::sin(ANGLE),
                 SIDE = -DX * std::sin(ANGLE) + DY * std::cos(ANGLE);
    return ALONG >= -double(bodyRadius) && ALONG <= double(length) + bodyRadius &&
           std::abs(SIDE) <= double(halfWidth) + bodyRadius;
}

inline std::uint32_t Damage(const EntityStats& attacker, const EntityStats& target, const Ability& a)
{
    const auto RAW = std::uint64_t(attacker.attack) * a.attackPermille / 1000 + a.flatDamage;
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(EntityStats::LIMIT, RAW > target.defense ? RAW - target.defense : 1));
}

inline std::uint32_t HitChance(const EntityStats& attacker, const EntityStats& target)
{
    return static_cast<std::uint32_t>(std::clamp(int(attacker.accuracy) - int(target.evasion), 5, 100));
}

inline bool InRange(WorldPosition a, WorldPosition b, std::uint32_t range)
{
    return DistanceSquared2D(a, b) <= std::uint64_t(range) * range && std::abs(std::int64_t(a.z) - b.z) <= 150;
}

inline bool InArea(WorldPosition origin, WorldPosition point, std::int32_t yaw, const Ability& a)
{
    if (!InRange(origin, point, a.radius))
    {
        return false;
    }

    if (a.shape != Shape::CONE)
    {
        return true;
    }

    const double DX = double(point.x) - origin.x, DY = double(point.y) - origin.y;
    if (DX * DX + DY * DY < 1)
    {
        return true;
    }

    const double RADIANS = double(yaw) / 1000 * 3.141592653589793 / 180;
    return (DX * std::cos(RADIANS) + DY * std::sin(RADIANS)) / std::sqrt(DX * DX + DY * DY) >=
           std::cos(double(a.angle) * 3.141592653589793 / 360);
}

}

