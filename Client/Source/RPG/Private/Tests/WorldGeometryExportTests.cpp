#include "World/WorldGeometryExport.h"
#include "World/WorldCoordinateAdapter.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && WITH_RECAST
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "SharedWorld/Tools/MapBakeTool/SourceGeometry.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldInstanceExportTest, "RPG.World.InstanceCollisionMatrix",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldInstanceExportTest::RunTest(const FString&)
{
    UWorld* world = UWorld::CreateWorld(EWorldType::Editor, false);
    auto* actor = world->SpawnActor<AActor>();
    auto* component = NewObject<UInstancedStaticMeshComponent>(actor, TEXT("TestInstances"));
    actor->AddInstanceComponent(component);
    actor->SetRootComponent(component);
    component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    component->SetMobility(EComponentMobility::Static);
    component->SetCollisionProfileName(TEXT("BlockAll"));
    component->SetCanEverAffectNavigation(true);
    component->RegisterComponent();
    const FTransform LOCAL(FRotator(30, 20, 15), FVector(70, 10, 60));
    component->AddInstance(LOCAL);
    for (double sign : {1., -1.})
    {
        const FTransform PARENT(FRotator(0, -90, 0), FVector(100, 200, 0), FVector(sign * 4, 4, 1));
        component->SetWorldTransform(PARENT);
        const FMatrix MATRIX = LOCAL.ToMatrixWithScale() * PARENT.ToMatrixWithScale();
        TArray<shared_world::WorldPosition> expected;
        for (int x : {-50, 50})
        {
            for (int y : {-50, 50})
            {
                for (int z : {-50, 50})
                {
                    expected.Add(
                        *RPGWorld::ToCanonical(MATRIX.TransformPosition(FVector(x, y, z)), FIntVector::ZeroValue));
                }

            }

        }

        const auto SOURCE = RPGWorld::ExportLoadedGeometry(world, 901, 1, TEXT("/Game/Test"), 512);
        TestEqual(TEXT("Box triangles exported"), int(SOURCE.triangles.size()), 12);
        for (const auto& t : SOURCE.triangles)
        {
            for (const auto P : {t.a, t.b, t.c})
            {
                TestTrue(TEXT("Collision vertex matches rendered matrix"), expected.Contains(P));
            }

        }

        const auto CENTRE = RPGWorld::ToCanonical(MATRIX.TransformPosition(FVector::ZeroVector), FIntVector::ZeroValue);
        for (const auto& t : SOURCE.triangles)
        {
            TestTrue(TEXT("Mirrored face keeps outward winding"),
                     shared_world::Dot(
                         shared_world::Cross(shared_world::Difference(t.b, t.a), shared_world::Difference(t.c, t.a)),
                         shared_world::Difference(t.a, *CENTRE)) > 0);
        }

    }

    actor->Tags.Add(FName(TEXT("RPGCollisionIgnore:TestInstances")));
    TestTrue(TEXT("Explicit replacement excludes only named component"),
             RPGWorld::ExportLoadedGeometry(world, 901, 1, TEXT("/Game/Test"), 512).triangles.empty());
    world->DestroyWorld(false);
    return true;
}

#endif
