#pragma once
#include "MapManifest.h"
#include <map>

namespace shared_world
{
// Independent of UE streaming cells and the AOI lattice. Core bounds own a
// position; a halo only provides collision for a body crossing that boundary.
struct ChunkLayout
{
    MapDefinition world;
    std::int32_t size = 25600;
    std::int32_t halo = 1024;

    void Validate() const
    {
        if (!this->world.IsValid() || this->size < 3200 || this->size > 51200 || this->size % this->world.tileSize ||
            this->halo < 100 || this->halo > this->size / 4)
        {
            throw std::invalid_argument("invalid chunk layout");
        }

    }

    TileCoord Coordinate(WorldPosition p) const
    {
        Validate();
        auto lattice = this->world;
        lattice.tileSize = this->size;
        return WorldToTile(p, lattice);
    }

    WorldBounds Bounds(TileCoord c, bool includeHalo) const
    {
        Validate();
        const std::int64_t PAD = includeHalo ? this->halo : 0;
        const auto X_VALUE = std::int64_t(this->world.origin.x) + std::int64_t(c.x) * this->size;
        const auto Y = std::int64_t(this->world.origin.y) + std::int64_t(c.y) * this->size;
        const auto LO_X = std::max<std::int64_t>(this->world.bounds.min.x, X_VALUE - PAD);
        const auto LO_Y = std::max<std::int64_t>(this->world.bounds.min.y, Y - PAD);
        const auto HI_X = std::min<std::int64_t>(this->world.bounds.max.x, X_VALUE + this->size + PAD);
        const auto HI_Y = std::min<std::int64_t>(this->world.bounds.max.y, Y + this->size + PAD);
        if (LO_X >= HI_X || LO_Y >= HI_Y)
        {
            throw std::invalid_argument("chunk outside world");
        }

        return {{static_cast<std::int32_t>(LO_X), static_cast<std::int32_t>(LO_Y), this->world.bounds.min.z},
                {static_cast<std::int32_t>(HI_X), static_cast<std::int32_t>(HI_Y), this->world.bounds.max.z}};
    }

    std::vector<TileCoord> Coordinates() const
    {
        const auto FIRST = Coordinate(this->world.bounds.min);
        auto lastPoint = this->world.bounds.max;
        --lastPoint.x;
        --lastPoint.y;
        --lastPoint.z;
        const auto LAST = Coordinate(lastPoint);
        if ((std::int64_t(LAST.x) - FIRST.x + 1) * (std::int64_t(LAST.y) - FIRST.y + 1) > 65536)
        {
            throw std::invalid_argument("chunk count budget exceeded");
        }

        std::vector<TileCoord> result;
        for (auto x = FIRST.x; x <= LAST.x; ++x)
        {
            for (auto y = FIRST.y; y <= LAST.y; ++y)
            {
                result.push_back({x, y});
            }

        }

        return result;
    }

};

// A separate versioned index prevents Phase 1 loaders from interpreting chunked
// content as a single complete collision world. Identity covers the whole index.
struct ChunkManifest
{
    ChunkLayout layout;
    MapManifest content;

    void Validate() const
    {
        this->layout.Validate();
        this->content.Validate();
        if (this->content.id != this->layout.world.id || this->content.version != this->layout.world.version)
        {
            throw std::invalid_argument("chunk index identity mismatch");
        }

        for (const auto& chunk : this->content.chunks)
        {
            (void)this->layout.Bounds({chunk.x, chunk.y}, false);
        }

        for (const auto& spawn : this->content.spawns)
        {
            if (!this->layout.world.bounds.Contains(spawn.feet))
            {
                throw std::invalid_argument("spawn outside chunk world");
            }

            const auto C = this->layout.Coordinate(spawn.feet);
            if (std::none_of(this->content.chunks.begin(), this->content.chunks.end(),
                             [&](const auto& r)
                             {
                                 return r.x == C.x && r.y == C.y;
                             }))

            {
                throw std::invalid_argument("spawn chunk is absent");
            }

        }

    }

    std::string Encode() const
    {
        Validate();
        std::ostringstream out;
        out.imbue(std::locale::classic());
        const auto& w = this->layout.world;
        out << "RPGW_CHUNKS 1\n"
            << this->layout.size << ' ' << this->layout.halo << '\n'
            << w.origin.x << ' ' << w.origin.y << ' ' << w.origin.z << ' ' << w.bounds.min.x << ' ' << w.bounds.min.y
            << ' ' << w.bounds.min.z << ' ' << w.bounds.max.x << ' ' << w.bounds.max.y << ' ' << w.bounds.max.z << ' '
            << w.tileSize << ' ' << w.aoiCellSize << ' ' << w.collisionCellSize << '\n'
            << this->content.Encode();
        return out.str();
    }

    static ChunkManifest Decode(const std::string& text)
    {
        if (text.size() > 16 * 1024 * 1024)
        {
            throw std::invalid_argument("chunk index size limit");
        }

        ChunkManifest m;
        auto& w = m.layout.world;
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        std::string magic;
        unsigned version = 0;
        if (!(in >> magic >> version >> m.layout.size >> m.layout.halo >> w.origin.x >> w.origin.y >> w.origin.z >>
              w.bounds.min.x >> w.bounds.min.y >> w.bounds.min.z >> w.bounds.max.x >> w.bounds.max.y >>
              w.bounds.max.z >> w.tileSize >> w.aoiCellSize >> w.collisionCellSize) ||
            magic != "RPGW_CHUNKS" || version != 1)
        {
            throw std::invalid_argument("invalid chunk index");
        }

        m.content = MapManifest::Decode(std::string(std::istreambuf_iterator<char>(in), {}));
        w.id = m.content.id;
        w.version = m.content.version;
        m.Validate();
        return m;
    }

    MapIdentity Identity() const
    {
        const auto BYTES = Encode();
        return {this->content.id, this->content.version,
                PackageChecksum(reinterpret_cast<const std::uint8_t*>(BYTES.data()), BYTES.size())};
    }

};

}

