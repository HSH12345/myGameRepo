#pragma once
#include "MapManifest.h"
#include "../Collision/StaticCollision.h"

namespace shared_world
{
// Loading APIs are blocking: call during startup or on the bounded asset worker,
// then publish immutable results through the map owner's completion queue.
class MapDataProvider
{
public:
    virtual ~MapDataProvider() = default;
    virtual std::shared_ptr<const LoadedMap> LoadChunk(const MapChunkReference& reference) const = 0;
};

class FileMapDataProvider final : public MapDataProvider
{
public:
    explicit FileMapDataProvider(std::filesystem::path newRoot) : root(ResolveWorldDataPath(newRoot))
    {
    }

    std::shared_ptr<const LoadedMap> LoadChunk(const MapChunkReference& reference) const override
    {
        // A manifest validation normally runs first; retain the path boundary here.
        if (reference.filename.empty() || reference.filename.find_first_of("/\\:") != std::string::npos ||
            reference.filename.find("..") != std::string::npos)
        {
            throw std::invalid_argument("invalid chunk filename");
        }

        return LoadedMap::LoadFile(this->root / reference.filename, reference.identity);
    }

private:
    std::filesystem::path root;
};

inline MapManifest LoadMapManifest(const std::filesystem::path& path)
{
    return MapManifest::Decode(ReadWorldDataFile<std::string>(
        path, 0, 16 * 1024 * 1024, "map manifest unavailable", "manifest size limit", "manifest changed while reading"));
}

}

