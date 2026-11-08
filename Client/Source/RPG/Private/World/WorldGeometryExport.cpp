#include "World/WorldGeometryExport.h"
#include "World/WorldExportCommandlet.h"
#include "World/WorldCoordinateAdapter.h"
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

#if WITH_EDITOR && WITH_RECAST
shared_world::SourceGeometry RPGWorld::ExportLoadedGeometry(UWorld* world, int32 mapId, int32 mapVersion,
                                                            const FString& mapPath, int32 verticalPadding)
{
    shared_world::SourceGeometry source;
    source.definition.id = static_cast<uint32>(mapId);
    source.definition.version = static_cast<uint32>(mapVersion);
    source.contentPath = TCHAR_TO_UTF8(*mapPath);
    FBox geometryBounds(ForceInit);
    uint64 degenerateTriangles = 0;
    const auto QUANTIZE = [&](const FVector& value)
    {
        const auto RESULT = RPGWorld::ToCanonical(value, world->OriginLocation);
        if (!RESULT)
        {
            throw std::runtime_error("geometry exceeds canonical coordinate limit");
        }

        geometryBounds += FVector(RESULT->x, RESULT->y, RESULT->z);
        return *RESULT;
    };

    for (TActorIterator<AActor> actor(world); actor; ++actor)
    {
        if (APlayerStart* start = Cast<APlayerStart>(*actor))
        {
            const FString TAG = start->PlayerStartTag.ToString();
            uint32 spawnId = 0;
            if (!LexTryParseString(spawnId, *TAG) || spawnId == 0)
            {
                throw std::runtime_error(
                    "PlayerStart requires a positive numeric PlayerStartTag for stable spawn identity");
            }

            FVector feet = start->GetActorLocation();
            feet.Z -= start->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const auto POSITION = RPGWorld::ToCanonical(feet, world->OriginLocation);
            if (!POSITION)
            {
                throw std::runtime_error("spawn outside coordinate limits");
            }

            source.spawns.push_back(
                {spawnId, *POSITION,
                 static_cast<int32>(FMath::RoundToInt(FRotator::NormalizeAxis(start->GetActorRotation().Yaw) * 1000))});
            continue;
        }

        if (ANavModifierVolume* modifier = Cast<ANavModifierVolume>(*actor))
        {
            if (modifier->GetAreaClass() != UNavArea_Null::StaticClass() || modifier->GetAreaClassToReplace())
            {
                throw std::runtime_error("unsupported area cost/replacement modifier; export aborted");
            }

            // Conservative AABB: rotated/convex null volumes can block more, never less.
            const FBox BOUNDS = modifier->GetComponentsBoundingBox(true);
            source.noWalkVolumes.push_back({QUANTIZE(BOUNDS.Min), QUANTIZE(BOUNDS.Max)});
            continue;
        }

        if (actor->FindComponentByClass<UNavModifierComponent>())
        {
            throw std::runtime_error("component navigation modifier needs explicit bake support; export aborted");
        }

        TInlineComponentArray<UPrimitiveComponent*> components;
        actor->GetComponents(components);
        for (UPrimitiveComponent* component : components)
        {
            // Explicit level-authored replacement collision; other parts
            // of a BP (walls, rails, columns) remain in the export.
            if (actor->ActorHasTag(FName(*(FString(TEXT("RPGCollisionIgnore:")) + component->GetName()))))
            {
                continue;
            }

            if (component->Mobility != EComponentMobility::Static || !component->CanEverAffectNavigation() ||
                component->GetCollisionEnabled() == ECollisionEnabled::NoCollision ||
                component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
            {
                continue;
            }

            FNavigationRelevantData data(FNavigationElement::CreateFromNavRelevantInterface(*component));
            FRecastGeometryExport geometryExport(data);
            component->PrepareGeometryExportSync();
            if (auto* instances = Cast<UInstancedStaticMeshComponent>(component))
            {
                UBodySetup* body = instances->GetBodySetup();
                if (!body)
                {
                    throw std::runtime_error("instanced mesh lacks collision body");
                }

                // Rendering composes the instance and component matrices.
                // A world FTransform loses shear when a rotated instance
                // sits below a non-uniformly scaled BP (e.g. 4,4,1).
                FRecastGeometryExport local(data);
                local.ExportRigidBodySetup(*body, FTransform::Identity);
                for (int32 index = 0; index < instances->GetInstanceCount(); ++index)
                {
                    FTransform transform;
                    if (!instances->GetInstanceTransform(index, transform, false))
                    {
                        throw std::runtime_error("instance transform unavailable");
                    }

                    const FMatrix MATRIX =
                        transform.ToMatrixWithScale() * instances->GetComponentTransform().ToMatrixWithScale();
                    const int32 BASE = geometryExport.VertexBuffer.Num() / 3;
                    for (int32 v = 0; v < local.VertexBuffer.Num(); v += 3)
                    {
                        const FVector P = MATRIX.TransformPosition(
                            FVector(local.VertexBuffer[v], local.VertexBuffer[v + 1], local.VertexBuffer[v + 2]));
                        geometryExport.VertexBuffer.Add(P.X);
                        geometryExport.VertexBuffer.Add(P.Y);
                        geometryExport.VertexBuffer.Add(P.Z);
                    }

                    const bool MIRRORED = MATRIX.Determinant() < 0;
                    for (int32 i = 0; i < local.IndexBuffer.Num(); i += 3)
                    {
                        geometryExport.IndexBuffer.Add(BASE + local.IndexBuffer[i]);
                        geometryExport.IndexBuffer.Add(BASE + local.IndexBuffer[i + (MIRRORED ? 2 : 1)]);
                        geometryExport.IndexBuffer.Add(BASE + local.IndexBuffer[i + (MIRRORED ? 1 : 2)]);
                    }

                }

            }
            else
            {
                const auto CUSTOM = component->HasCustomNavigableGeometry();
                if (CUSTOM == EHasCustomNavigableGeometry::DontExport)
                {
                    continue;
                }

                const bool EXPORT_BODY = CUSTOM == EHasCustomNavigableGeometry::No ||
                                         component->DoCustomNavigableGeometryExport(geometryExport);
                if (EXPORT_BODY)
                {
                    UBodySetup* body = component->GetBodySetup();
                    if (!body)
                    {
                        throw std::runtime_error("blocking component lacks collision body");
                    }

                    geometryExport.ExportRigidBodySetup(*body, component->GetComponentTransform());
                }

            }

            if (geometryExport.IndexBuffer.IsEmpty() || geometryExport.IndexBuffer.Num() % 3 != 0)
            {
                throw std::runtime_error("blocking component produced no complete collision triangles");
            }

            if (FParse::Param(FCommandLine::Get(), TEXT("RpgGeometryAudit")))
            {
                FBox bounds(ForceInit);
                for (int32 v = 0; v < geometryExport.VertexBuffer.Num(); v += 3)
                {
                    bounds += FVector(geometryExport.VertexBuffer[v], geometryExport.VertexBuffer[v + 1],
                                      geometryExport.VertexBuffer[v + 2]);
                }

                UE_LOG(LogTemp, Display,
                       TEXT("GEOMETRY_AUDIT actor=%s component=%s triangles=%d collision=%s visual=%s"),
                       *actor->GetActorLabel(), *component->GetName(), geometryExport.IndexBuffer.Num() / 3,
                       *bounds.ToString(), *component->Bounds.GetBox().ToString());
            }

            for (int32 index = 0; index < geometryExport.IndexBuffer.Num(); index += 3)
            {
                const auto VERTEX = [&](int32 corner)
                {
                    const int64 OFFSET = static_cast<int64>(geometryExport.IndexBuffer[index + corner]) * 3;
                    if (OFFSET < 0 || OFFSET + 2 >= geometryExport.VertexBuffer.Num())
                    {
                        throw std::runtime_error("invalid exported collision index");
                    }

                    return QUANTIZE(FVector(geometryExport.VertexBuffer[OFFSET],
                                            geometryExport.VertexBuffer[OFFSET + 1],
                                            geometryExport.VertexBuffer[OFFSET + 2]));
                };

                // Chaos export uses clockwise outward faces in UE coordinates.
                // Canonical triangles use cross(B-A,C-A) as the outward normal.
                const shared_world::Triangle TRIANGLE{VERTEX(0), VERTEX(2), VERTEX(1)};
                const auto AB = shared_world::Difference(TRIANGLE.b, TRIANGLE.a);
                const auto AC = shared_world::Difference(TRIANGLE.c, TRIANGLE.a);
                const auto NORMAL = shared_world::Cross(AB, AC);
                if (!NORMAL.x && !NORMAL.y && !NORMAL.z)
                {
                    ++degenerateTriangles;
                    continue;
                }

                if (!shared_world::ValidTriangle(TRIANGLE))
                {
                    UE_LOG(LogTemp, Error,
                           TEXT("Invalid collision triangle actor=%s component=%s vertices=(%d,%d,%d) (%d,%d,%d) "
                                "(%d,%d,%d)"),
                           *actor->GetActorLabel(), *component->GetName(), TRIANGLE.a.x, TRIANGLE.a.y, TRIANGLE.a.z,
                           TRIANGLE.b.x, TRIANGLE.b.y, TRIANGLE.b.z, TRIANGLE.c.x, TRIANGLE.c.y, TRIANGLE.c.z);
                    throw std::runtime_error("invalid/degenerate collision triangle in named actor");
                }

                source.triangles.push_back(TRIANGLE);
                if (source.triangles.size() > 500'000)
                {
                    throw std::runtime_error("export triangle budget exceeded");
                }

            }

        }

    }

    if (degenerateTriangles)
    {
        UE_LOG(LogTemp, Warning, TEXT("WorldExport skipped %llu zero-area quantized collision triangles"),
               degenerateTriangles);
    }

    if (source.triangles.empty())
    {
        return source;
    }

    const auto MINIMUM = RPGWorld::ToCanonical(geometryBounds.Min - FVector(1), FIntVector::ZeroValue);
    // Playable space includes room for bodies above the highest surface.
    // Collision vertices remain unchanged; this expands the query domain only.
    const auto MAXIMUM =
        RPGWorld::ToCanonical(geometryBounds.Max + FVector(1, 1, verticalPadding), FIntVector::ZeroValue);
    if (!MINIMUM || !MAXIMUM)
    {
        throw std::runtime_error("map bounds exceed coordinate limit");
    }

    source.definition.bounds.min = *MINIMUM;
    source.definition.bounds.max = *MAXIMUM;
    return source;
}

#endif
