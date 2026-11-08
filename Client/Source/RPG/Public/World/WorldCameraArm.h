#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "WorldCameraArm.generated.h"

// User/zone zoom is a target. Collision may retract immediately for safety, but
// expansion is damped and ordinary dungeon walls do not participate in it.
UCLASS()

class RPG_API UWorldCameraArm : public USpringArmComponent
{
    GENERATED_BODY()
public:
    UWorldCameraArm();
    void RequestZoom(float length, float duration = .45f);

    float RequestedZoom() const
    {
        return this->zoomInitialized ? this->zoomTarget : this->TargetArmLength;
    }

    bool PresentationReady() const
    {
        return this->stableFrames >= 3;
    }

    void ResetPresentation();
#if WITH_DEV_AUTOMATION_TESTS
    friend class FWorldCameraStabilityTest;
#endif
protected:
    virtual void UpdateDesiredArmLocation(bool trace, bool locationLag, bool rotationLag, float delta) override;
    virtual FVector BlendLocations(const FVector& desired, const FVector& hit, bool blocked, float delta) override;

private:
    bool zoomInitialized = false, collisionInitialized = false, isPreviousCamera = false;
    float zoomStart = 650, zoomTarget = 650, zoomElapsed = 0, zoomDuration = .45f, lastAppliedZoom = 650,
          collisionDistance = 0;
    FVector previousCameraValue = FVector::ZeroVector;
    FQuat previousRotation = FQuat::Identity;
    int32 stableFrames = 0;
};

