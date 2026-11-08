#include "World/WorldCameraOcclusion.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

UWorldCameraOcclusion::UWorldCameraOcclusion()
{
    this->PrimaryComponentTick.bCanEverTick = false;
}

void UWorldCameraOcclusion::ConfigureGeometry(UPrimitiveComponent* component)
{
    if (!IsValid(component) || !IsValid(component->GetOwner()))
    {
        return;
    }

    const auto TAGGED = [&](FName tag)
    {
        return component->ComponentHasTag(tag) || component->GetOwner()->ActorHasTag(tag);
    };

    const bool WALL = TAGGED(TEXT("RPG.CameraFade"));
    if (WALL || TAGGED(TEXT("RPG.NoGroundPick")))
    {
        component->SetCollisionResponseToChannel(GROUND_CHANNEL, ECR_Ignore);
        component->SetReceivesDecals(false);
    }

    if (WALL && !TAGGED(TEXT("RPG.CameraBoundary")))
    {
        component->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    }

}

void UWorldCameraOcclusion::Update(USpringArmComponent* arm, float delta)
{
    check(IsInGameThread());
    if (!arm)
    {
        return;
    }

    const double NOW = FPlatformTime::Seconds();
    if (NOW >= this->nextProbe)
    {
        this->nextProbe = NOW + .04; // One local camera, at most 25 broad-phase probes/s.
        this->probed = true;
        const FRotator ROTATION = arm->GetTargetRotation();
        const FVector START = arm->GetComponentLocation() + arm->TargetOffset;
        const FVector END = START - ROTATION.Vector() * arm->TargetArmLength + ROTATION.RotateVector(arm->SocketOffset);
        FCollisionObjectQueryParams objects;
        objects.AddObjectTypesToQuery(ECC_WorldStatic);
        objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        FCollisionQueryParams query(SCENE_QUERY_STAT(PlayerOcclusion), false, GetOwner());
        TArray<FHitResult> hits;
        // Object query returns every intervening wall, including initial overlaps.
        GetWorld()->SweepMultiByObjectType(hits, START, END, FQuat::Identity, objects, FCollisionShape::MakeSphere(85),
                                           query);
        for (const auto& hit : hits)
        {
            auto* component = hit.GetComponent();
            if (!IsValid(component) || !IsValid(component->GetOwner()))
            {
                continue;
            }

            if (!component->ComponentHasTag(TEXT("RPG.CameraFade")) &&
                !component->GetOwner()->ActorHasTag(TEXT("RPG.CameraFade")))
            {
                continue;
            }

            ConfigureGeometry(component);
            auto* existing = this->walls.FindByPredicate(
                [component](const FWorldFadedWall& w)
                {
                    return w.component == component;
                });

            if (existing)
            {
                existing->lastSeen = NOW;
                continue;
            }

            // This variant preserves the actual dungeon WorldGrid source graph.
            // Future art needs its own fade variant before it is tagged.
            if (this->walls.Num() >= 32 || component->GetNumMaterials() != 1 || !component->GetMaterial(0) ||
                component->GetMaterial(0)->GetMaterial()->GetName() != TEXT("WorldGridMaterial"))
            {
                continue;
            }

            if (!this->fadeMaterial)
            {
                this->fadeMaterial =
                    LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RPG/Camera/M_WallFade.M_WallFade"));
            }

            if (!ensureMsgf(this->fadeMaterial, TEXT("Wall fade material missing")))
            {
                continue;
            }

            auto* dynamic = UMaterialInstanceDynamic::Create(this->fadeMaterial, this);
            if (!dynamic)
            {
                continue;
            }

            auto& wall = this->walls.AddDefaulted_GetRef();
            wall.component = component;
            wall.lastSeen = NOW;
            wall.material = dynamic;
            wall.cameraResponse = component->GetCollisionResponseToChannel(ECC_Camera);
            wall.shadow = component->CastShadow;
            for (int32 i = 0; i < component->GetNumMaterials(); ++i)
            {
                wall.originals.Add(component->GetMaterial(i));
                component->SetMaterial(i, dynamic);
            }

            dynamic->SetScalarParameterValue(TEXT("WallOpacity"), 1);
            component->SetCastShadow(false);
        }

    }

    for (int32 i = this->walls.Num() - 1; i >= 0; --i)
    {
        auto& wall = this->walls[i];
        if (!wall.component.IsValid())
        {
            this->walls.RemoveAtSwap(i);
            continue;
        }

        const bool OCCLUDING = NOW - wall.lastSeen < .25;
        wall.opacity = FMath::FInterpConstantTo(wall.opacity, OCCLUDING ? .12f : 1.f, delta, OCCLUDING ? 7.f : 3.f);
        wall.material->SetScalarParameterValue(TEXT("WallOpacity"), wall.opacity);
        if (!OCCLUDING && wall.opacity >= .999f)
        {
            Restore(wall);
            this->walls.RemoveAtSwap(i);
        }

    }

}

void UWorldCameraOcclusion::Restore(FWorldFadedWall& wall)
{
    if (auto* component = wall.component.Get())
    {
        for (int32 i = 0; i < wall.originals.Num(); ++i)
        {
            component->SetMaterial(i, wall.originals[i]);
        }

        component->SetCollisionResponseToChannel(ECC_Camera, wall.cameraResponse);
        component->SetCastShadow(wall.shadow);
    }

}

void UWorldCameraOcclusion::Clear()
{
    check(IsInGameThread());
    for (auto& wall : this->walls)
    {
        Restore(wall);
    }

    this->walls.Empty();
    this->nextProbe = 0;
    this->probed = false;
}

bool UWorldCameraOcclusion::PresentationReady() const
{
    if (!this->probed)
    {
        return false;
    }

    const double NOW = FPlatformTime::Seconds();
    for (const auto& wall : this->walls)
    {
        if (FMath::Abs(wall.opacity - (NOW - wall.lastSeen < .25 ? .12f : 1.f)) > .02f)
        {
            return false;
        }

    }

    return true;
}

bool UWorldCameraOcclusion::TraceGround(FHitResult& hit, const FVector& start, const FVector& end) const
{
    FCollisionQueryParams query(SCENE_QUERY_STAT(GroundPointerScene), false, GetOwner());
    // Also handle newly spawned/tagged walls before the next 25 Hz camera probe.
    // The query has a fixed cap and never changes movement collision.
    for (int32 attempt = 0; attempt < 16; ++attempt)
    {
        if (!GetWorld()->LineTraceSingleByChannel(hit, start, end, GROUND_CHANNEL, query))
        {
            return false;
        }

        const auto* component = hit.GetComponent();
        const auto* actor = hit.GetActor();
        if (component && actor &&
            (component->ComponentHasTag(TEXT("RPG.CameraFade")) || actor->ActorHasTag(TEXT("RPG.CameraFade")) ||
             component->ComponentHasTag(TEXT("RPG.NoGroundPick")) || actor->ActorHasTag(TEXT("RPG.NoGroundPick"))))
        {
            query.AddIgnoredComponent(component);
            continue;
        }

        return hit.bBlockingHit && hit.ImpactNormal.Z > .5;
    }

    return false;
}

bool UWorldCameraOcclusion::IsFaded(const UPrimitiveComponent* component) const
{
    if (!component)
    {
        return false;
    }

    return this->walls.ContainsByPredicate(
        [component](const FWorldFadedWall& wall)
        {
            return wall.component == component && wall.opacity < .95f;
        });

}

bool UWorldCameraOcclusion::TraceVisibleScene(FHitResult& hit, const FVector& start, const FVector& end) const
{
    FCollisionQueryParams query(SCENE_QUERY_STAT(VisiblePointerScene), false, GetOwner());
    for (const auto& wall : this->walls)
    {
        if (wall.component.IsValid() && wall.opacity < .95f)
        {
            query.AddIgnoredComponent(wall.component.Get());
        }

    }

    return GetWorld()->LineTraceSingleByChannel(hit, start, end, ECC_Pawn, query);
}

void UWorldCameraOcclusion::EndPlay(const EEndPlayReason::Type reason)
{
    Clear();
    Super::EndPlay(reason);
}

