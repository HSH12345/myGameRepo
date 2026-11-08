#include "World/MapSyncTestCommandlet.h"
#include "World/MapCollisionDebugActor.h"
#if WITH_EDITOR
#include "FileHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/Paths.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#endif

UMapSyncTestCommandlet::UMapSyncTestCommandlet()
{
    this->IsClient = false;
    this->IsServer = false;
    this->IsEditor = true;
    this->LogToConsole = true;
}

int32 UMapSyncTestCommandlet::Main(const FString& params)
{
#if WITH_EDITOR
    FString mapPath = TEXT("/Game/RPG/Maps/Tests/L_MapSyncTest");
    const bool PEACE_FIXTURE = FParse::Param(*params, TEXT("PeaceWorldFixture"));
    FParse::Value(*params, TEXT("Map="), mapPath);
    if (!mapPath.StartsWith(TEXT("/Game/RPG/Maps/Tests/")) || !FPackageName::IsValidLongPackageName(mapPath))
    {
        UE_LOG(LogTemp, Error, TEXT("MapSyncTest output must be a new package under /Game/RPG/Maps/Tests/"));
        return 1;
    }

    const FString FILENAME = FPackageName::LongPackageNameToFilename(mapPath, FPackageName::GetMapPackageExtension());
    if (IFileManager::Get().FileExists(*FILENAME))
    {
        UE_LOG(LogTemp, Error, TEXT("MapSyncTest refuses to overwrite an existing level"));
        return 1;
    }

    UStaticMesh* cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* stairs = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Game/RuinedCrypt/Environment/Stairs_01/sm_Stairs_01_01.sm_Stairs_01_01"));
    if (!cube || !stairs)
    {
        UE_LOG(LogTemp, Error, TEXT("MapSyncTest requires the engine cube and RuinedCrypt stair mesh"));
        return 1;
    }

    UWorld* world = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!world || world->GetWorldPartition())
    {
        return 1;
    }

    int32 count = 0;
    auto mesh = [&](const TCHAR* label, UStaticMesh* asset, FVector position, FVector scale,
                    FRotator rotation = FRotator::ZeroRotator)
    {
        AStaticMeshActor* actor = world->SpawnActor<AStaticMeshActor>();
        check(actor);
        actor->SetActorLabel(label);
        auto* component = actor->GetStaticMeshComponent();
        component->SetMobility(EComponentMobility::Static);
        component->SetStaticMesh(asset);
        component->SetCollisionProfileName(TEXT("BlockAll"));
        component->SetCanEverAffectNavigation(true);
        actor->SetActorTransform(FTransform(rotation, position, scale));
        ++count;
        return actor;
    };

    // Centimeters; top of the 160m floor is canonical Z=0.
    const double SPAN = PEACE_FIXTURE ? 640 : 160, EDGE = SPAN * 50 - 50;
    mesh(TEXT("Ground"), cube, {0, 0, -50}, {SPAN, SPAN, 1});
    mesh(TEXT("Boundary_North"), cube, {0, EDGE, 250}, {SPAN, 1, 5});
    mesh(TEXT("Boundary_South"), cube, {0, -EDGE, 250}, {SPAN, 1, 5});
    mesh(TEXT("Boundary_East"), cube, {EDGE, 0, 250}, {1, SPAN, 5});
    mesh(TEXT("Boundary_West"), cube, {-EDGE, 0, 250}, {1, SPAN, 5});
    if (PEACE_FIXTURE)
    {
        mesh(TEXT("FarEast_Wall"), cube, {27000, 3000, 150}, {0.2, 12, 3});
        mesh(TEXT("FarWest_Wall"), cube, {-27000, 3000, 150}, {0.2, 12, 3});
        auto* farStart = world->SpawnActor<APlayerStart>();
        farStart->PlayerStartTag = FName(TEXT("2"));
        farStart->SetActorLocation({26000, 3000, 100});
    }

    mesh(TEXT("Wall_Thin"), cube, {0, -2500, 150}, {0.2, 12, 3});
    mesh(TEXT("Wall_Corner"), cube, {600, -1900, 150}, {12, 0.2, 3});
    // Bridge at Z=400; a player can also pass underneath at Z=0.
    mesh(TEXT("Bridge_UpperFloor"), cube, {0, 1500, 375}, {30, 8, 0.5});
    mesh(TEXT("Bridge_Pier_A"), cube, {-1400, 1500, 175}, {1, 8, 3.5});
    mesh(TEXT("Bridge_Pier_B"), cube, {1400, 1500, 175}, {1, 8, 3.5});
    // Twenty explicit 20cm risers with 35cm treads, ending at bridge height.
    for (int32 step = 0; step < 20; ++step)
    {
        const float HEIGHT = (step + 1) * 20.0f;
        const FString LABEL = FString::Printf(TEXT("Stair_%02d"), step);
        mesh(*LABEL, cube, {-2182.5f + step * 35.0f, 1500, HEIGHT / 2}, {0.35, 8, HEIGHT / 100});
    }

    // A shallow slope provides an independent sloped collision case.
    mesh(TEXT("Ramp_15deg"), cube, {3500, 1500, 200}, {16, 8, 0.5}, FRotator(15, 0, 0));
    mesh(TEXT("LowCeiling_ClearanceTest"), cube, {3500, -2000, 120}, {8, 8, 0.5});
    mesh(TEXT("Fab_RuinedCrypt_Stairs"), stairs, {-3500, 0, 0}, {1, 1, 1});
    APlayerStart* start = world->SpawnActor<APlayerStart>();
    start->SetActorLabel(TEXT("Spawn_Default"));
    start->Tags.Add(TEXT("RPG.Spawn.default"));
    start->PlayerStartTag = FName(TEXT("1"));
    start->SetActorLocation({-3000, -3000, 100});
    ADirectionalLight* sun = world->SpawnActor<ADirectionalLight>();
    sun->SetActorRotation(FRotator(-45, -30, 0));
    sun->GetLightComponent()->SetIntensity(3.0f);
    world->SpawnActor<ASkyLight>();
    auto* debug = world->SpawnActor<AMapCollisionDebugActor>();
    debug->SetActorLabel(TEXT("ServerCollision_GreenSurface_RedObstacle"));
    debug->mapFilename = TEXT("MapSync-v3.rpgmap");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FILENAME), true);
    if (!UEditorLoadingAndSavingUtils::SaveMap(world, mapPath))
    {
        return 1;
    }

    UE_LOG(LogTemp, Display, TEXT("MapSyncTest saved %s staticActors=%d size=%.0fm"), *mapPath, count, SPAN);
    return 0;
#else
    return 1;
#endif
}

