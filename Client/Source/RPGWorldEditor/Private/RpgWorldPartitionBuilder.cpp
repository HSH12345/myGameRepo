#include "RpgWorldPartitionBuilder.h"
#include "World/WorldGeometryExport.h"
#include "World/WorldCoordinateAdapter.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "SharedWorld/Tools/MapBakeTool/ChunkGeometry.h"
#include "SharedWorld/Collision/StaticCollision.h"
#include <filesystem>
#include <fstream>

bool URpgWorldPartitionBuilder::PreRun(UWorld* world, FPackageSourceControlHelper&)
{
    auto* partition = world ? world->GetWorldPartition() : nullptr;
    if (!partition)
    {
        return false;
    }

    bool supported = true;
    int32 descriptorCount = 0, initiallyLoaded = 0;
    partition->GetDataLayerManager()->ForEachDataLayerInstance(
        [&](UDataLayerInstance* layer)
        {
            if (layer->IsRuntime())
            {
                supported = false;
            }

            return true;
        });

    FWorldPartitionHelpers::ForEachActorDescInstance<AActor>(
        partition,
        [&](const FWorldPartitionActorDescInstance* actor)
        {
            ++descriptorCount;
            if (actor->IsChildContainerInstance() ||
                actor->GetActorNativeClass()->IsChildOf(ALevelInstance::StaticClass()))
            {
                supported = false;
            }

            return true;
        });

    for (TActorIterator<AActor> actor(world); actor; ++actor)
    {
        ++initiallyLoaded;
    }

    UE_LOG(LogTemp, Display, TEXT("RPG WP inventory descriptors=%d initiallyLoaded=%d"), descriptorCount,
           initiallyLoaded);
    if (!supported)
    {
        UE_LOG(LogTemp, Error,
               TEXT("RPG WP export refuses runtime Data Layers and Level Instances until their gameplay policy is "
                    "implemented."));
    }

    // Static editor layers are all loaded by the builder, independently of the
    // last editor visibility state. Runtime layer state is never guessed.
    this->bLoadNonDynamicDataLayers = true;
    return supported;
}

bool URpgWorldPartitionBuilder::RunInternal(UWorld* world, const FCellInfo&, FPackageSourceControlHelper&)
{
    try
    {
        FString output;
        int32 mapId = 0, version = 0, chunkSize = 25600;
        GetParamValue(TEXT("ChunkSize="), chunkSize);
        if (!GetParamValue(TEXT("Out="), output) || !GetParamValue(TEXT("MapId="), mapId) ||
            !GetParamValue(TEXT("MapVersion="), version) || mapId <= 0 || version <= 0)
        {
            throw std::invalid_argument("builder requires Out=new-directory MapId=N MapVersion=N");
        }

        const std::filesystem::path DESTINATION(*FPaths::ConvertRelativePathToFull(output));
        auto staging = DESTINATION;
        staging += ".building";
        if (std::filesystem::exists(DESTINATION) || std::filesystem::exists(staging))
        {
            throw std::invalid_argument("output or staging already exists");
        }

        shared_world::ChunkManifest manifest;
        auto& layout = manifest.layout;
        auto& definition = layout.world;
        definition.id = mapId;
        definition.version = version;
        const FBox BOUNDS = world->GetWorldPartition()->GetEditorWorldBounds();
        const auto LOW = RPGWorld::ToCanonical(BOUNDS.Min - FVector(1), world->OriginLocation);
        const auto HIGH = RPGWorld::ToCanonical(BOUNDS.Max + FVector(1, 1, 512), world->OriginLocation);
        if (!LOW || !HIGH)
        {
            throw std::invalid_argument("world bounds exceed canonical limits");
        }

        definition.bounds = {*LOW, *HIGH};
        layout.size = chunkSize;
        layout.halo = FMath::Min(1024, chunkSize / 4);
        layout.Validate();
        manifest.content.id = mapId;
        manifest.content.version = version;
        const FString MAP_PATH = world->GetOutermost()->GetName();
        manifest.content.contentPath = TCHAR_TO_UTF8(*MAP_PATH);
        if (!std::filesystem::create_directory(staging))
        {
            throw std::runtime_error("cannot create staging directory");
        }

        const auto COORDINATES = layout.Coordinates();
        for (const auto COORDINATE : COORDINATES)
        {
            const auto BOUNDS_TO_LOAD = layout.Bounds(COORDINATE, true);
            const FVector ORIGIN(world->OriginLocation);
            const FVector MIN = FVector(BOUNDS_TO_LOAD.min.x, BOUNDS_TO_LOAD.min.y, BOUNDS_TO_LOAD.min.z) - ORIGIN;
            const FVector MAX = FVector(BOUNDS_TO_LOAD.max.x, BOUNDS_TO_LOAD.max.y, BOUNDS_TO_LOAD.max.z) - ORIGIN;
            {
                // One loader lifetime per server chunk, including crossing
                // geometry. Unlike the generic iterative builder, no collection
                // of loaders survives until a machine-wide memory threshold.
                auto loader = MakeUnique<FLoaderAdapterShape>(world, FBox(MIN, MAX), TEXT("RPG server chunk"));
                loader->Load();
                FWorldPartitionHelpers::FakeEngineTick(world);
                const auto SOURCE = RPGWorld::ExportLoadedGeometry(world, mapId, version, MAP_PATH);
                auto chunk = shared_world::ClipToChunk(SOURCE, layout, COORDINATE);
                if (!chunk.triangles.empty())
                {
                    const auto BYTES = shared_world::BakeMap(chunk).Encode();
                    const auto LOADED = shared_world::LoadedMap::FromBytes(BYTES);
                    shared_world::StaticWorld collision(LOADED);
                    for (auto spawn : chunk.spawns)
                    {
                        const auto GROUND = collision.ProjectToWalkableSurface(spawn.feet, 100);
                        if (GROUND.status != shared_world::QueryStatus::HIT)
                        {
                            throw std::invalid_argument("spawn has no ground");
                        }

                        spawn.feet = GROUND.position;
                        manifest.content.spawns.push_back(spawn);
                    }

                    const auto FILENAME =
                        "chunk_" + std::to_string(COORDINATE.x) + "_" + std::to_string(COORDINATE.y) + ".rpgmap";
                    std::ofstream out(staging / FILENAME, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(BYTES.data()), static_cast<std::streamsize>(BYTES.size()));
                    out.close();
                    if (!out)
                    {
                        throw std::runtime_error("chunk output write failed");
                    }

                    manifest.content.chunks.push_back({COORDINATE.x, COORDINATE.y, LOADED->Identity(), FILENAME});
                    UE_LOG(LogTemp, Display, TEXT("RPG WP chunk %d,%d triangles=%llu bytes=%llu"), COORDINATE.x,
                           COORDINATE.y, static_cast<uint64>(chunk.triangles.size()),
                           static_cast<uint64>(BYTES.size()));
                }
                else if (!chunk.spawns.empty() || !chunk.noWalkVolumes.empty())
                {
                    throw std::invalid_argument("metadata in empty chunk");
                }

            }

            FWorldPartitionHelpers::DoCollectGarbage();
        }

        std::sort(manifest.content.spawns.begin(), manifest.content.spawns.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.id < b.id;
                  });

        const auto ENCODED = manifest.Encode();
        std::ofstream index(staging / "world.rpgchunks", std::ios::binary);
        index << ENCODED;
        index.close();
        if (!index)
        {
            throw std::runtime_error("index write failed");
        }

        std::filesystem::rename(staging, DESTINATION);
        UE_LOG(LogTemp, Display, TEXT("RPG WP export complete chunks=%llu checksum=%llu"),
               static_cast<uint64>(manifest.content.chunks.size()), static_cast<uint64>(manifest.Identity().checksum));
        return true;
    }

    catch (const std::exception& error)
    {
        UE_LOG(LogTemp, Error, TEXT("RPG WP export: %s"), UTF8_TO_TCHAR(error.what()));
        return false;
    }

}

