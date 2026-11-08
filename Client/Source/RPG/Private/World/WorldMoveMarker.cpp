#include "World/WorldMoveMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

AWorldMoveMarker::AWorldMoveMarker()
{
    this->PrimaryActorTick.bCanEverTick = true;
    this->PrimaryActorTick.bStartWithTickEnabled = false;
    this->bReplicates = false;
    this->shape = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ring"));
    SetRootComponent(this->shape);
    this->shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->shape->SetGenerateOverlapEvents(false);
    this->shape->SetCastShadow(false);
    this->shape->bReceivesDecals = false;
    this->shape->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterial> effect(
        TEXT("/Game/RPG/Effects/M_MoveDestination.M_MoveDestination"));
    if (cube.Succeeded())
    {
        this->shape->SetStaticMesh(cube.Object);
    }

    if (effect.Succeeded())
    {
        this->shape->SetMaterial(0, effect.Object);
    }

    SetActorHiddenInGame(true);
}

void AWorldMoveMarker::BeginPlay()
{
    Super::BeginPlay();
    // One instanced draw, built once. No actor/component/material allocation on clicks.
    for (int32 i = 0; i < 32; ++i)
    {
        const float ANGLE = 2 * PI * i / 32;
        this->shape->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(ANGLE) + 90, 0),
                                            FVector(FMath::Cos(ANGLE) * 55, FMath::Sin(ANGLE) * 55, 0),
                                            FVector(.115, .045, .018)));
    }

    // Four inward pointing pairs make the selected centre easy to read.
    for (int32 i = 0; i < 4; ++i)
    {
        for (int32 side : {-1, 1})
        {
            const FRotator ROTATION(0, i * 90.f, 0);
            this->shape->AddInstance(FTransform(FRotator(0, i * 90.f + side * 45.f, 0),
                                                ROTATION.RotateVector(FVector(30, side * 7, 0)),
                                                FVector(.20, .045, .018)));
        }

    }

    this->material = this->shape->CreateDynamicMaterialInstance(0);
    UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL marker_prewarmed instances=%d material=%s"),
           this->shape->GetInstanceCount(), *GetNameSafe(this->material));
    Release();
}

void AWorldMoveMarker::Activate(FVector location, FVector normal, bool confirmed)
{
    this->age = 0;
    this->active = true;
    SetActorLocation(location + normal * 5);
    SetActorRotation(FRotationMatrix::MakeFromZ(normal).Rotator());
    SetActorScale3D(FVector(.9));
    if (this->material)
    {
        this->material->SetVectorParameterValue(TEXT("Tint"),
                                                confirmed ? FLinearColor(.015, .25, 1) : FLinearColor(1, .3, .01));
    }

    SetActorHiddenInGame(false);
    SetActorTickEnabled(true);
}

void AWorldMoveMarker::Confirm(FVector location, FVector normal)
{
    Activate(location, normal, true);
}

void AWorldMoveMarker::Release()
{
    this->active = false;
    this->age = 0;
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
}

void AWorldMoveMarker::Tick(float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    this->age += deltaSeconds;
    const float PULSE = .98f + .10f * FMath::Sin(this->age * 6.f);
    SetActorScale3D(FVector(PULSE, PULSE, 1));
}

