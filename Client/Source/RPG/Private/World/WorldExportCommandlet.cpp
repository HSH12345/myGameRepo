#include "World/WorldExportCommandlet.h"
#include "World/WorldCoordinateAdapter.h"
#include "World/WorldGeometryExport.h"
#if WITH_EDITOR
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "AI/Navigation/NavigationElement.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "NavMesh/RecastGeometryExport.h"
#include "NavModifierVolume.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "SharedWorld/Tools/MapBakeTool/SourceGeometry.h"
#include <sstream>
#endif

UWorldExportCommandlet::UWorldExportCommandlet()
{
    this->IsClient = false;
    this->IsServer = false;
    this->IsEditor = true;
    this->LogToConsole = true;
}

int32 UWorldExportCommandlet::Main(const FString& params)
{
#if WITH_EDITOR && WITH_RECAST
    FString mapPath, output;
    int32 mapId = 0, mapVersion = 0;
    int32 verticalPadding = 512;
    int32 tileSize = 3200;
    FParse::Value(*params, TEXT("VerticalPadding="), verticalPadding);
    FParse::Value(*params, TEXT("TileSize="), tileSize);
    const bool FIXTURE = FParse::Param(*params, TEXT("Fixture"));
    if ((!FIXTURE && !FParse::Value(*params, TEXT("Map="), mapPath)) || !FParse::Value(*params, TEXT("Out="), output) ||
        !FParse::Value(*params, TEXT("MapId="), mapId) || !FParse::Value(*params, TEXT("MapVersion="), mapVersion) ||
        mapId <= 0 || mapVersion <= 0 || verticalPadding < 1 || verticalPadding > 10000 || tileSize < 400 ||
        tileSize > 10000 || IFileManager::Get().FileExists(*output))
    {
        UE_LOG(LogTemp, Error, TEXT("WorldExport needs -Map=... -Out=new.worldsource -MapId=N -MapVersion=N"));
        return 1;
    }

    UWorld* world =
        FIXTURE ? UEditorLoadingAndSavingUtils::NewBlankMap(false) : UEditorLoadingAndSavingUtils::LoadMap(mapPath);
    if (!world || world->GetWorldPartition() || !world->GetStreamingLevels().IsEmpty())
    {
        UE_LOG(LogTemp, Error,
               TEXT("WorldExport requires a fully loaded non-partitioned map; refusing incomplete world export."));
        return 1;
    }

    try
    {
        if (FIXTURE)
        {
            UStaticMesh* cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
            if (!cube)
            {
                throw std::runtime_error("fixture cube unavailable");
            }

            for (int32 floor = 0; floor < 2; ++floor)
            {
                AStaticMeshActor* actor = world->SpawnActor<AStaticMeshActor>();
                if (!actor)
                {
                    throw std::runtime_error("fixture actor unavailable");
                }

                UStaticMeshComponent* component = actor->GetStaticMeshComponent();
                component->SetMobility(EComponentMobility::Static);
                component->SetStaticMesh(cube);
                component->SetCollisionProfileName(TEXT("BlockAll"));
                component->SetCanEverAffectNavigation(true);
                actor->SetActorTransform(
                    FTransform(FRotator::ZeroRotator, FVector(0, 0, floor * 300 - 10), FVector(10, 10, 0.2)));
            }

        }

        auto source =
            RPGWorld::ExportLoadedGeometry(world, mapId, mapVersion, FIXTURE ? FString() : mapPath, verticalPadding);
        source.definition.tileSize = tileSize;
        if (source.triangles.empty())
        {
            throw std::runtime_error("map contains no exportable static collision");
        }

        // Validate geometry and resource budgets before emitting a source file.
        (void)shared_world::BakeMap(source);
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        const auto POSITION = [&](shared_world::WorldPosition p)
        {
            stream << p.x << ' ' << p.y << ' ' << p.z << ' ';
        };

        stream << "RPGW_SOURCE " << (FIXTURE ? 1 : 2) << '\n' << mapId << ' ' << mapVersion << ' ';
        POSITION(source.definition.origin);
        POSITION(source.definition.bounds.min);
        POSITION(source.definition.bounds.max);
        stream << source.definition.tileSize << " 6400 200\n" << source.triangles.size() << '\n';
        for (const auto& triangle : source.triangles)
        {
            stream << static_cast<uint32>(triangle.kind) << ' ';
            POSITION(triangle.a);
            POSITION(triangle.b);
            POSITION(triangle.c);
            stream << '\n';
        }

        stream << source.noWalkVolumes.size() << '\n';
        for (const auto& volume : source.noWalkVolumes)
        {
            POSITION(volume.min);
            POSITION(volume.max);
            stream << '\n';
        }

        if (!FIXTURE)
        {
            stream << source.contentPath << '\n' << source.spawns.size() << '\n';
            for (const auto& spawn : source.spawns)
            {
                stream << spawn.id << ' ';
                POSITION(spawn.feet);
                stream << spawn.yawMilliDegrees << '\n';
            }

        }

        if (!FFileHelper::SaveStringToFile(UTF8_TO_TCHAR(stream.str().c_str()), *output,
                                           FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            throw std::runtime_error("source output write failed");
        }

        UE_LOG(LogTemp, Display, TEXT("WorldExport completed: triangles=%llu noWalkVolumes=%llu"),
               static_cast<uint64>(source.triangles.size()), static_cast<uint64>(source.noWalkVolumes.size()));
        return 0;
    }

    catch (const std::exception& error)
    {
        UE_LOG(LogTemp, Error, TEXT("WorldExport: %s"), UTF8_TO_TCHAR(error.what()));
        return 1;
    }

#else
    return 1;
#endif
}

