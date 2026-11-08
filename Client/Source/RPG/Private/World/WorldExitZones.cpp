#include "World/WorldExitZones.h"
#include "World/WorldCameraOcclusion.h"
#include "SharedWorld/Dungeon/DungeonMaze.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

AWorldExitZone::AWorldExitZone()
{
    this->PrimaryActorTick.bCanEverTick = false;
    this->bReplicates = false;
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    this->ring = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ExitRing"));
    this->ring->SetupAttachment(this->RootComponent);
    this->ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->ring->SetGenerateOverlapEvents(false);
    this->ring->SetCanEverAffectNavigation(false);
    this->ring->SetCastShadow(false);
    this->ring->bReceivesDecals = false;
    this->ring->SetComponentTickEnabled(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterial> effect(
        TEXT("/Game/RPG/Effects/M_MoveDestination.M_MoveDestination"));
    if (cube.Succeeded())
    {
        this->ring->SetStaticMesh(cube.Object);
    }

    if (effect.Succeeded())
    {
        this->ring->SetMaterial(0, effect.Object);
    }

    this->label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ExitName"));
    this->label->SetupAttachment(this->RootComponent);
    this->label->SetRelativeLocation(FVector(0, 0, 140));
    this->label->SetHorizontalAlignment(EHTA_Center);
    this->label->SetWorldSize(36);
    this->label->SetComponentTickEnabled(false);
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
}

void AWorldExitZone::Present(const shared_world::dungeon::Zone& zone)
{
    if (this->ring->GetInstanceCount() == 0)
    {
        for (int32 i = 0; i < 32; ++i)
        {
            const float ANGLE = 2 * PI * i / 32;
            this->ring->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(ANGLE) + 90, 0),
                                               FVector(FMath::Cos(ANGLE) * 100, FMath::Sin(ANGLE) * 100, 0),
                                               FVector(.21, .04, .025)));
        }

    }

    if (!this->material)
    {
        this->material = this->ring->CreateDynamicMaterialInstance(0);
    }

    const FLinearColor COLOR = zone.type == shared_world::dungeon::ExitType::NORMAL      ? FLinearColor(.02f, 1, .3f)
                               : zone.type == shared_world::dungeon::ExitType::EMERGENCY ? FLinearColor(1, .35f, .02f)
                                                                                         : FLinearColor(.5f, .1f, 1);
    if (this->material)
    {
        this->material->SetVectorParameterValue(TEXT("Tint"), COLOR);
    }

    this->ring->SetRelativeScale3D(FVector(zone.radius / 100.f, zone.radius / 100.f, 1));
    this->ring->SetRelativeLocation(FVector(0, 0, 6));
    this->label->SetTextRenderColor(COLOR.ToFColor(true));
    this->label->SetText(
        FText::FromString(FString::Printf(TEXT("[ E ] %s"), UTF8_TO_TCHAR(shared_world::dungeon::Name(zone.type)))));
    SetActorLocation(FVector(zone.feet.x, zone.feet.y, zone.feet.z) - FVector(GetWorld()->OriginLocation));
    SetActorHiddenInGame(false);
}

void AWorldExitZone::Release()
{
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
}

void AWorldExitZone::FaceLabel(FVector cameraLocation)
{
    this->label->SetWorldRotation((cameraLocation - this->label->GetComponentLocation()).Rotation());
}

int32 AWorldExitZone::RingInstanceCount() const
{
    return this->ring->GetInstanceCount();
}

bool UWorldExitPool::Present(const shared_world::dungeon::Layout& layout)
{
    if (!layout.Valid())
    {
        return false;
    }

    while (this->actors.Num() < 3)
    {
        auto* actor = GetWorld()->SpawnActor<AWorldExitZone>();
        if (!actor)
        {
            ReleaseAll();
            return false;
        }

        this->actors.Add(actor);
    }

    for (int32 i = 0; i < this->actors.Num(); ++i)
    {
        if (!IsValid(this->actors[i]))
        {
            return false;
        }

        if (i < static_cast<int32>(layout.count))
        {
            this->actors[i]->Present(layout.zones[i]);
        }
        else
        {
            this->actors[i]->Release();
        }

    }

    return true;
}

void UWorldExitPool::ReleaseAll()
{
    PresentDoors(0);
    for (const auto& actor : this->actors)
    {
        if (IsValid(actor))
        {
            actor->Release();
        }

    }

}

void UWorldExitPool::FaceLabels(FVector cameraLocation)
{
    for (const auto& actor : this->actors)
    {
        if (IsValid(actor) && !actor->IsHidden())
        {
            actor->FaceLabel(cameraLocation);
        }

    }

}

int32 UWorldExitPool::DoorInstanceCount() const
{
    return this->doors ? this->doors->GetInstanceCount() : 0;
}

void UWorldExitPool::PresentDoors(unsigned)
{
    // Retire any legacy instances. Combat never creates collision or visual gates.
    if (this->doors)
    {
        this->doors->ClearInstances();
        this->doors->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

}

void UWorldExitPool::Deinitialize()
{
    for (const auto& actor : this->actors)
    {
        if (IsValid(actor))
        {
            actor->Destroy();
        }

    }

    this->actors.Empty();
    if (this->doorActor)
    {
        this->doorActor->Destroy();
    }

    this->doors = nullptr;
    this->doorActor = nullptr;
    Super::Deinitialize();
}

