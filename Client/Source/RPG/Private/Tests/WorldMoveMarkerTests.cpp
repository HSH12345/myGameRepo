#include "World/WorldMoveMarker.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/GarbageCollection.h"
#include "Materials/Material.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMoveMarkerTest, "RPG.World.MoveMarkerReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldMoveMarkerTest::RunTest(const FString&)
{
    auto* world = UWorld::CreateWorld(EWorldType::Game, false);
    world->AddToRoot();
    auto* marker = world->SpawnActor<AWorldMoveMarker>();
    // This isolated world has no GameMode; initialise the actor explicitly.
    marker->DispatchBeginPlay();
    auto* shape = marker->FindComponentByClass<UInstancedStaticMeshComponent>();
    TestNotNull(TEXT("Marker mesh"), shape);
    if (shape)
    {
        const auto INSTANCES = shape->GetInstanceCount();
        TestTrue(TEXT("Prewarmed geometry has visible instances"), INSTANCES > 0);
        auto* material = shape->GetMaterial(0);
        TestNotNull(TEXT("Effect material"), material);
        if (material)
        {
            TestTrue(TEXT("Saved material supports instanced rendering"),
                     material->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        }

        for (int32 click = 0; click < 100; ++click)
        {
            marker->Activate(FVector(click * 10, 20, 0), FVector::UpVector, false);
            marker->Confirm(FVector(click * 10, 20, 0), FVector::UpVector);
            marker->Tick(.1f);
            marker->Release();
        }

        CollectGarbage(RF_NoFlags);
        TestEqual(TEXT("Instances reused across 100 clicks"), shape->GetInstanceCount(), INSTANCES);
        TestTrue(TEXT("Dynamic material survives GC and is reused"), shape->GetMaterial(0) == material);
        TestEqual(TEXT("Marker cannot intercept cursor or movement"), shape->GetCollisionEnabled(),
                  ECollisionEnabled::NoCollision);
        TestFalse(TEXT("Released actor stops ticking"), marker->IsActorTickEnabled());
        TestTrue(TEXT("Released actor hidden"), marker->IsHidden());
        TestFalse(TEXT("Released marker inactive"), marker->IsActive());
        marker->Activate(FVector(0, 0, 50), FVector(.2, 0, 1).GetSafeNormal(), true);
        TestTrue(TEXT("Reacquired marker visible"), marker->IsActive() && !marker->IsHidden());
        TestFalse(TEXT("Local marker never replicates"), marker->GetIsReplicated());
    }

    world->DestroyWorld(false);
    world->RemoveFromRoot();
    return true;
}

#endif
