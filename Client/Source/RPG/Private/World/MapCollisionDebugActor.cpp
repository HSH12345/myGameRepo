#include "World/MapCollisionDebugActor.h"
#include "World/WorldCoordinateAdapter.h"
#include "SharedWorld/Collision/StaticCollision.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "DrawDebugHelpers.h"

struct FMapCollisionDebugData
{
    std::shared_ptr<const shared_world::StaticWorld> collision;
};

AMapCollisionDebugActor::AMapCollisionDebugActor()
{
    this->PrimaryActorTick.bCanEverTick = true;
    this->PrimaryActorTick.TickInterval = 0.1f;
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AMapCollisionDebugActor::~AMapCollisionDebugActor() = default;

void AMapCollisionDebugActor::BeginPlay()
{
    Super::BeginPlay();
    ReloadMapData();
}

void AMapCollisionDebugActor::ReloadMapData()
{
    this->data.Reset();
    this->attemptedFilename = this->mapFilename;
    if (this->mapFilename.IsEmpty() || FPaths::GetCleanFilename(this->mapFilename) != this->mapFilename ||
        this->mapFilename.Contains(TEXT("..")))
    {
        return;
    }

    const FString PATH = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData"), this->mapFilename);
    const int64 SIZE = IFileManager::Get().FileSize(*PATH);
    if (SIZE < 84 || SIZE > 64 * 1024 * 1024)
    {
        return;
    }

    TArray<uint8> bytes;
    if (!FFileHelper::LoadFileToArray(bytes, *PATH))
    {
        return;
    }

    try
    {
        shared_world::Bytes native(bytes.GetData(), bytes.GetData() + bytes.Num());
        auto loaded = shared_world::LoadedMap::FromBytes(native);
        this->data = MakeShared<FMapCollisionDebugData>();
        this->data->collision = std::make_shared<const shared_world::StaticWorld>(loaded);
    }

    catch (const std::exception& error)
    {
        UE_LOG(LogTemp, Error, TEXT("Map collision debug load failed: %s"), UTF8_TO_TCHAR(error.what()));
    }

}

void AMapCollisionDebugActor::Tick(float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    if (!this->displayCollision || !GetWorld())
    {
        return;
    }

    if (this->attemptedFilename != this->mapFilename)
    {
        ReloadMapData();
    }

    if (!this->data || !this->data->collision)
    {
        return;
    }

    const auto CENTER = RPGWorld::ToCanonical(GetActorLocation(), GetWorld()->OriginLocation);
    if (!CENTER)
    {
        return;
    }

    const int32 RADIUS = FMath::Clamp(FMath::RoundToInt(this->displayRadius), 100, 10000);
    const shared_world::WorldBounds AREA{{CENTER->x - RADIUS, CENTER->y - RADIUS, CENTER->z - RADIUS},
                                         {CENTER->x + RADIUS, CENTER->y + RADIUS, CENTER->z + RADIUS}};
    const auto POSITION = [this](shared_world::WorldPosition p)
    {
        return FVector(p.x, p.y, p.z) - FVector(GetWorld()->OriginLocation);
    };

    this->data->collision->VisitGeometry(
        AREA, 10000,
        [&](const shared_world::Triangle& t)
        {
            const auto N = shared_world::Cross(shared_world::Difference(t.b, t.a), shared_world::Difference(t.c, t.a));
            const bool WALKABLE = N.z > 0 && std::abs(N.x) + std::abs(N.y) <= N.z;
            const FColor COLOR = WALKABLE ? FColor::Green : FColor::Red;
            const FVector A = POSITION(t.a) + FVector(0, 0, 2), B = POSITION(t.b) + FVector(0, 0, 2),
                          C = POSITION(t.c) + FVector(0, 0, 2);
            DrawDebugLine(GetWorld(), A, B, COLOR, false, 0.12f, 0, 2);
            DrawDebugLine(GetWorld(), B, C, COLOR, false, 0.12f, 0, 2);
            DrawDebugLine(GetWorld(), C, A, COLOR, false, 0.12f, 0, 2);
        },

        [&](shared_world::WorldBounds b)
        {
            const FVector LOW = POSITION(b.min), HIGH = POSITION(b.max);
            DrawDebugBox(GetWorld(), (LOW + HIGH) * 0.5, (HIGH - LOW) * 0.5, FColor::Yellow, false, 0.12f, 0, 3);
        });

}

