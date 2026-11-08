#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "World/WorldExitZones.h"
#include "SharedWorld/Dungeon/DungeonMaze.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "Materials/Material.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonExitPoolTest, "RPG.World.DungeonExitPool",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDungeonExitPoolTest::RunTest(const FString&)
{
    const auto* fade = LoadObject<UMaterial>(nullptr, TEXT("/Game/RPG/Camera/M_WallFade.M_WallFade"));
    if (!TestNotNull(TEXT("Shared wall/door fade material"), fade))
    {
        return false;
    }

    TestTrue(TEXT("Closed instanced doors have a saved fade shader permutation"),
             fade->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
    TestEqual(TEXT("Door fade uses actual translucent opacity"), fade->GetBlendMode(), BLEND_Translucent);
    auto* world = UWorld::CreateWorld(EWorldType::Game, false);
    world->AddToRoot();
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
    auto* pool = world->GetSubsystem<UWorldExitPool>();
    if (!TestNotNull(TEXT("Exit zone pool"), pool))
    {
        return false;
    }

    std::mt19937_64 random(777);
    shared_world::dungeon::Rule rule{301, 2, 3, 700, 200, 250};
    const shared_world::WorldBounds BOUNDS{{-5001, -5001, -101}, {5001, 5001, 512}};
    TArray<AWorldExitZone*> original;
    for (unsigned run = 1; run <= 100; ++run)
    {
        const auto LAYOUT = shared_world::dungeon::Generate(rule, BOUNDS, 0, run, random);
        TestTrue(TEXT("Present server layout"), pool->Present(LAYOUT));
        TestEqual(TEXT("Fixed pool capacity"), pool->AllocatedCount(), 3);
        for (int32 i = 0; i < 3; ++i)
        {
            auto* zone = pool->GetZone(i);
            if (run == 1)
            {
                original.Add(zone);
            }

            TestTrue(TEXT("Actors reused between layouts"), zone == original[i]);
            TestFalse(TEXT("No per-zone actor tick"), zone->IsActorTickEnabled());
            if (i < static_cast<int32>(LAYOUT.count))
            {
                TestFalse(TEXT("Active zone visible"), zone->IsHidden());
                TestEqual(TEXT("Ring instances reused"), zone->RingInstanceCount(), 32);
            }

        }

        pool->ReleaseAll();
        for (int32 i = 0; i < 3; ++i)
        {
            TestTrue(TEXT("Released zone hidden"), pool->GetZone(i)->IsHidden());
        }

    }

    shared_world::dungeon::Layout empty;
    empty.run = 101;
    TestTrue(TEXT("Empty initial exit snapshot"), pool->Present(empty));
    for (unsigned room = 1; room <= 6; ++room)
    {
        pool->PresentDoors(room);
        TestEqual(TEXT("Combat room never creates blocking doors"), pool->DoorInstanceCount(), 0);
        CollectGarbage(RF_NoFlags);
        TestEqual(TEXT("Entrances remain open after GC"), pool->DoorInstanceCount(), 0);
        pool->ReleaseAll();
        TestEqual(TEXT("Clear/death opens every door"), pool->DoorInstanceCount(), 0);
    }

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Pool actors survive GC"), IsValid(pool->GetZone(0)) && IsValid(pool->GetZone(2)));
    world->DestroyWorld(false);
    GEngine->DestroyWorldContext(world);
    world->RemoveFromRoot();
    return true;
}

#endif
