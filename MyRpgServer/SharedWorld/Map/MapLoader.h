#pragma once
#include "MapPackage.h"
#include "../Core/WorldDataFile.h"
#include <filesystem>
#include <memory>

namespace shared_world
{
// Immutable package snapshot. Construct off simulation/IOCP threads; publish the shared_ptr
// through the owner's reserved completion queue. No live map mutation or implicit fallback.
class LoadedMap final
{
public:
    static std::shared_ptr<const LoadedMap> FromBytes(const Bytes& bytes,
                                                      std::optional<MapIdentity> expected = std::nullopt)
    {
        auto currentPackage = MapPackage::Decode(bytes, expected);
        const MapIdentity IDENTITY{currentPackage.definition.id, currentPackage.definition.version,
                                   PackageChecksum(bytes.data(), bytes.size() - 8)};
        return std::shared_ptr<const LoadedMap>(new LoadedMap(std::move(currentPackage), IDENTITY));
    }

    static std::shared_ptr<const LoadedMap> LoadFile(const std::filesystem::path& path,
                                                     std::optional<MapIdentity> expected = std::nullopt)
    {
        const auto BYTES = ReadWorldDataFile<Bytes, std::runtime_error>(
            path, 84, MAX_PACKAGE_BYTES, "map file cannot be opened", "map file size outside budget",
            "map file changed or read failed");
        return FromBytes(BYTES, expected);
    }

    const MapDefinition& Definition() const noexcept
    {
        return this->package.definition;
    }

    // Accounted object/vector capacity; allocator bookkeeping is not included.
    std::size_t ResidentBytes() const noexcept
    {
        return this->residentBytes;
    }

    const MapIdentity& Identity() const noexcept
    {
        return this->identity;
    }

    const std::vector<TileData>& Tiles() const noexcept
    {
        return this->package.tiles;
    }

    const std::vector<Region>& Regions() const noexcept
    {
        return this->package.regions;
    }

    const TileData* FindTile(TileCoord coordinate) const noexcept
    {
        const auto FOUND = std::lower_bound(this->package.tiles.begin(), this->package.tiles.end(), coordinate,
                                            [](const TileData& tile, TileCoord key)
                                            {
                                                return tile.coordinate < key;
                                            });

        return FOUND != this->package.tiles.end() && FOUND->coordinate == coordinate ? &*FOUND : nullptr;
    }

private:
    LoadedMap(MapPackage loadedPackage, MapIdentity loadedIdentity)
        : package(std::move(loadedPackage)), identity(loadedIdentity)
    {
        this->residentBytes = sizeof(*this) + this->package.tiles.capacity() * sizeof(TileData) +
                              this->package.regions.capacity() * sizeof(Region);
        for (const auto& tile : this->package.tiles)
        {
            this->residentBytes += tile.collision.capacity() + tile.navigation.capacity();
        }

    }

    const MapPackage package;
    const MapIdentity identity;
    std::size_t residentBytes = 0;
};

} // namespace shared_world

