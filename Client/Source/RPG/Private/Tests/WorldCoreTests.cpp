#include "World/WorldCoordinateAdapter.h"
#include "SharedWorld/Map/MapPackage.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/WorldCameraArm.h"
#include "World/WorldCameraOcclusion.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldCoordinateTest, "RPG.World.Coordinates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldCoordinateTest::RunTest(const FString&)
{
    using namespace shared_world;
    const FIntVector ORIGIN(10000, -20000, 30000);
    const FVector LOCAL(0.5, -0.5, 12.49);
    const auto CANONICAL = RPGWorld::ToCanonical(LOCAL, ORIGIN);
    TestTrue(TEXT("Rebased canonical conversion"), CANONICAL == WorldPosition{10001, -20001, 30012});
    if (CANONICAL)
    {
        const FVector RESTORED = RPGWorld::ToUnreal(*CANONICAL, ORIGIN);
        TestTrue(TEXT("Canonical rebased round trip"), RPGWorld::ToCanonical(RESTORED, ORIGIN) == CANONICAL);
    }

    TestFalse(TEXT("Overflow rejected"), RPGWorld::ToCanonical(FVector(1e20, 0, 0), ORIGIN).has_value());
    MapPackage package;
    package.definition = MapDefinition{101, 1, {}, {{-100, -100, -100}, {100, 100, 100}}};
    const auto BYTES = package.Encode();
    TestTrue(TEXT("Same portable map codec"), MapPackage::Decode(BYTES).Encode() == BYTES);
    TestTrue(TEXT("Negative tile"), WorldToTile({-1, 0, 0}, package.definition) == TileCoord{-1, 0});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldCameraStabilityTest, "RPG.World.CameraStability",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldCameraStabilityTest::RunTest(const FString&)
{
    auto* arm = NewObject<UWorldCameraArm>();
    arm->TargetArmLength = 650;
    arm->ResetPresentation();
    arm->UpdateDesiredArmLocation(false, false, false, 0);
    arm->RequestZoom(2000);
    float previous = 650;
    for (int32 frame = 0; frame < 30; ++frame)
    {
        arm->UpdateDesiredArmLocation(false, false, false, 1.f / 60);
        TestTrue(TEXT("Zoom advances monotonically"), arm->TargetArmLength >= previous && arm->TargetArmLength <= 2000);
        if (frame == 0)
        {
            TestTrue(TEXT("Boss entry does not jump to wide view"),
                     arm->TargetArmLength > 650 && arm->TargetArmLength < 700);
        }

        previous = arm->TargetArmLength;
    }

    TestTrue(TEXT("Boss zoom reaches goal"), FMath::IsNearlyEqual(arm->TargetArmLength, 2000.f));
    arm->RequestZoom(650);
    arm->UpdateDesiredArmLocation(false, false, false, .1f);
    TestTrue(TEXT("Boss exit is gradual"), arm->TargetArmLength > 650 && arm->TargetArmLength < 2000);
    arm->RequestZoom(1800, .2f);
    for (int32 frame = 0; frame < 20; ++frame)
    {
        arm->UpdateDesiredArmLocation(false, false, false, 1.f / 60);
    }

    TestTrue(TEXT("Wheel interrupts transition and reaches new goal"),
             FMath::IsNearlyEqual(arm->TargetArmLength, 1800.f));
    arm->SetWorldRotation(FRotator(-40, 90, 0));
    arm->UpdateDesiredArmLocation(false, false, true, 1.f / 60);
    TestFalse(TEXT("Rotation in progress is not loading-ready"), arm->PresentationReady());
    TestTrue(TEXT("Actual camera rotation is damped"),
             arm->GetSocketTransform(USpringArmComponent::SocketName).Rotator().Yaw < 89);
    for (int32 frame = 0; frame < 120; ++frame)
    {
        arm->UpdateDesiredArmLocation(false, false, true, 1.f / 60);
    }

    TestTrue(TEXT("Settled camera becomes loading-ready"), arm->PresentationReady());
    arm->ResetPresentation();
    const FVector DESIRED(1000, 0, 0);
    TestTrue(TEXT("Boundary retracts immediately without penetration"),
             arm->BlendLocations(DESIRED, FVector(200, 0, 0), true, 1.f / 60).Equals(FVector(200, 0, 0)));
    const double RECOVERY = arm->BlendLocations(DESIRED, FVector::ZeroVector, false, 1.f / 60).X;
    TestTrue(TEXT("Boundary recovery is smooth"), RECOVERY > 200 && RECOVERY < 400);
    TestTrue(TEXT("New nearer obstruction remains safe"),
             arm->BlendLocations(DESIRED, FVector(100, 0, 0), true, 1.f / 60).Equals(FVector(100, 0, 0)));

    auto* world = UWorld::CreateWorld(EWorldType::Game, false);
    world->AddToRoot();
    auto* actor = world->SpawnActor<AActor>();
    auto* wall = NewObject<UBoxComponent>(actor);
    wall->SetCollisionProfileName(TEXT("BlockAll"));
    wall->ComponentTags.Add(TEXT("RPG.CameraFade"));
    UWorldCameraOcclusion::ConfigureGeometry(wall);
    TestEqual(TEXT("Opaque tagged wall cannot catch ground cursor"),
              wall->GetCollisionResponseToChannel(UWorldCameraOcclusion::GROUND_CHANNEL), ECR_Ignore);
    TestEqual(TEXT("Interior wall never pushes camera"), wall->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
    TestEqual(TEXT("Movement collision is preserved"), wall->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
    wall->SetCollisionProfileName(TEXT("BlockAll"));
    wall->ComponentTags.Add(TEXT("RPG.CameraBoundary"));
    UWorldCameraOcclusion::ConfigureGeometry(wall);
    TestEqual(TEXT("Outer wall retains camera collision"), wall->GetCollisionResponseToChannel(ECC_Camera), ECR_Block);
    TestEqual(TEXT("Boundary still cannot catch ground cursor"),
              wall->GetCollisionResponseToChannel(UWorldCameraOcclusion::GROUND_CHANNEL), ECR_Ignore);
    world->DestroyWorld(false);
    world->RemoveFromRoot();
    return true;
}

#endif
