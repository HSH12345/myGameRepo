#pragma once
#include "MapLoader.h"
#include <set>
#include <sstream>

namespace shared_world
{
struct SpawnPoint
{
    std::uint32_t id = 0;
    WorldPosition feet{};
    std::int32_t yawMilliDegrees = 0;
};

struct MapChunkReference
{
    std::int32_t x = 0, y = 0;
    MapIdentity identity{};
    std::string filename;
};

// Static transition references reserve content identity without allocating an
// instance. Runtime admission/instance resolution belongs to the game service.
enum class MapTransitionPolicy : std::uint32_t
{
    SHARED = 1,
    CREATE_DUNGEON = 2,
    RETURN_TO_SAFE_POINT = 3
};

struct PortalReference
{
    std::uint32_t id = 0;
    WorldBounds trigger{};
    MapId targetMap = 0;
    std::uint32_t targetSpawn = 0;
    MapTransitionPolicy policy = MapTransitionPolicy::SHARED;
};

// Content path is a client-local allowlist entry, never an arbitrary path from a
// network packet. A manifest describes static content; instances live elsewhere.
struct MapManifest
{
    MapId id = 0;
    std::uint32_t version = 0;
    std::string contentPath;
    std::vector<MapChunkReference> chunks;
    std::vector<SpawnPoint> spawns;
    std::vector<PortalReference> portals;

    void Validate() const
    {
        if (!this->id || !this->version || this->contentPath.size() > 256 ||
            this->contentPath.rfind("/Game/", 0) != 0 ||
            this->contentPath.find_first_of(" \t\r\n\\") != std::string::npos ||
            this->contentPath.find("..") != std::string::npos || this->chunks.empty() || this->chunks.size() > 65536 ||
            this->spawns.size() > 4096 || this->portals.size() > 4096)
        {
            throw std::invalid_argument("invalid world manifest");
        }

        std::set<std::pair<std::int32_t, std::int32_t>> coordinates;
        std::set<std::string> names;
        for (const auto& c : this->chunks)
        {
            if (c.identity.id != this->id || c.identity.version != this->version || c.filename.empty() ||
                c.filename.size() > 128 || c.filename.find_first_of("/\\: \t\r\n") != std::string::npos ||
                c.filename.find("..") != std::string::npos || !coordinates.emplace(c.x, c.y).second ||
                !names.insert(c.filename).second)
            {
                throw std::invalid_argument("invalid map chunk reference");
            }

        }

        std::set<std::uint32_t> spawnIds;
        for (const auto& s : this->spawns)
        {
            if (!s.id || !IsValid(s.feet) || s.yawMilliDegrees < -180000 || s.yawMilliDegrees > 180000 ||
                !spawnIds.insert(s.id).second)
            {
                throw std::invalid_argument("invalid spawn point");
            }

        }

        std::set<std::uint32_t> portalIds;
        for (const auto& p : this->portals)
        {
            if (!p.id || !p.trigger.IsValid() || !p.targetMap || !p.targetSpawn ||
                static_cast<unsigned>(p.policy) < 1 || static_cast<unsigned>(p.policy) > 3 ||
                !portalIds.insert(p.id).second)
            {
                throw std::invalid_argument("invalid portal reference");
            }

        }

    }

    std::string Encode() const
    {
        Validate();
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "RPGW_MANIFEST " << (this->portals.empty() ? 1 : 2) << '\n'
            << this->id << ' ' << this->version << ' ' << this->contentPath << '\n'
            << this->chunks.size() << '\n';
        for (const auto& c : this->chunks)
        {
            out << c.x << ' ' << c.y << ' ' << c.identity.checksum << ' ' << c.filename << '\n';
        }

        out << this->spawns.size() << '\n';
        for (const auto& s : this->spawns)
        {
            out << s.id << ' ' << s.feet.x << ' ' << s.feet.y << ' ' << s.feet.z << ' ' << s.yawMilliDegrees << '\n';
        }

        if (!this->portals.empty())
        {
            out << this->portals.size() << '\n';
            for (const auto& p : this->portals)
            {
                out << p.id << ' ' << p.trigger.min.x << ' ' << p.trigger.min.y << ' ' << p.trigger.min.z << ' '
                    << p.trigger.max.x << ' ' << p.trigger.max.y << ' ' << p.trigger.max.z << ' ' << p.targetMap << ' '
                    << p.targetSpawn << ' ' << static_cast<unsigned>(p.policy) << '\n';
            }

        }

        return out.str();
    }

    static MapManifest Decode(const std::string& bytes)
    {
        if (bytes.size() > 16 * 1024 * 1024)
        {
            throw std::invalid_argument("manifest too large");
        }

        std::istringstream in(bytes);
        in.imbue(std::locale::classic());
        std::string magic;
        unsigned format = 0;
        MapManifest m;
        std::size_t count = 0;
        if (!(in >> magic >> format >> m.id >> m.version >> m.contentPath >> count) || magic != "RPGW_MANIFEST" ||
            (format != 1 && format != 2) || count > 65536)
        {
            throw std::invalid_argument("invalid manifest header");
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            MapChunkReference c;
            c.identity.id = m.id;
            c.identity.version = m.version;
            if (!(in >> c.x >> c.y >> c.identity.checksum >> c.filename))
            {
                throw std::invalid_argument("truncated chunk list");
            }

            m.chunks.push_back(std::move(c));
        }

        if (!(in >> count) || count > 4096)
        {
            throw std::invalid_argument("invalid spawn count");
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            SpawnPoint s;
            if (!(in >> s.id >> s.feet.x >> s.feet.y >> s.feet.z >> s.yawMilliDegrees))
            {
                throw std::invalid_argument("truncated spawn list");
            }

            m.spawns.push_back(s);
        }

        if (format == 2)
        {
            if (!(in >> count) || count > 4096)
            {
                throw std::invalid_argument("invalid portal count");
            }

            for (std::size_t i = 0; i < count; ++i)
            {
                PortalReference p;
                unsigned policy = 0;
                if (!(in >> p.id >> p.trigger.min.x >> p.trigger.min.y >> p.trigger.min.z >> p.trigger.max.x >>
                      p.trigger.max.y >> p.trigger.max.z >> p.targetMap >> p.targetSpawn >> policy))
                {
                    throw std::invalid_argument("truncated portal list");
                }

                p.policy = static_cast<MapTransitionPolicy>(policy);
                m.portals.push_back(p);
            }

        }

        if (in >> magic)
        {
            throw std::invalid_argument("trailing manifest data");
        }

        m.Validate();
        return m;
    }

};

}

