#include "World/WorldCameraArm.h"

UWorldCameraArm::UWorldCameraArm()
{
    this->bEnableCameraRotationLag = true;
    this->CameraRotationLagSpeed = 18;
    this->bUseCameraLagSubstepping = true;
    this->CameraLagMaxTimeStep = 1.f / 60;
}

void UWorldCameraArm::ResetPresentation()
{
    this->zoomInitialized = this->collisionInitialized = this->isPreviousCamera = false;
    this->stableFrames = 0;
}

void UWorldCameraArm::RequestZoom(float length, float duration)
{
    length = FMath::Clamp(length, 250.f, 2800.f);
    if (this->zoomInitialized && FMath::IsNearlyEqual(length, this->zoomTarget, .01f))
    {
        return;
    }

    this->zoomInitialized = true;
    this->zoomStart = this->TargetArmLength;
    this->zoomTarget = length;
    this->lastAppliedZoom = this->TargetArmLength;
    this->zoomElapsed = 0;
    this->zoomDuration = FMath::Max(.05f, duration);
    this->stableFrames = 0;
}

void UWorldCameraArm::UpdateDesiredArmLocation(bool trace, bool locationLag, bool rotationLag, float delta)
{
    if (!this->zoomInitialized)
    {
        this->zoomInitialized = true;
        this->zoomStart = this->zoomTarget = this->lastAppliedZoom = this->TargetArmLength;
        this->zoomElapsed = this->zoomDuration;
    }

    // Editor/debug tools may still set the inherited property directly.
    if (!FMath::IsNearlyEqual(this->TargetArmLength, this->lastAppliedZoom, .01f))
    {
        const float REQUESTED = this->TargetArmLength;
        this->TargetArmLength = this->lastAppliedZoom;
        RequestZoom(REQUESTED);
#if !UE_BUILD_SHIPPING
        // Explicit editor overview captures retain their authored distance.
        if (REQUESTED > 2800 && !this->bDoCollisionTest)
        {
            this->zoomTarget = REQUESTED;
        }

#endif
    }

    this->zoomElapsed = FMath::Min(this->zoomDuration, this->zoomElapsed + FMath::Max(0.f, delta));
    const float T = FMath::Clamp(this->zoomElapsed / this->zoomDuration, 0.f, 1.f);
    this->TargetArmLength = this->lastAppliedZoom = FMath::Lerp(this->zoomStart, this->zoomTarget, T * T * (3 - 2 * T));
    Super::UpdateDesiredArmLocation(trace, locationLag, rotationLag, delta);
    const auto CAMERA_TRANSFORM = GetSocketTransform(USpringArmComponent::SocketName);
    const FVector POSITION = CAMERA_TRANSFORM.GetLocation();
    const FQuat ROTATION = CAMERA_TRANSFORM.GetRotation();
    const bool SETTLED = this->isPreviousCamera && FVector::DistSquared(POSITION, this->previousCameraValue) < .25 &&
                         ROTATION.AngularDistance(this->previousRotation) < FMath::DegreesToRadians(.1) &&
                         ROTATION.AngularDistance(GetTargetRotation().Quaternion()) < FMath::DegreesToRadians(.15) &&
                         FMath::Abs(this->TargetArmLength - this->zoomTarget) < 1;
    this->stableFrames = SETTLED ? FMath::Min(this->stableFrames + 1, 10) : 0;
    this->isPreviousCamera = true;
    this->previousCameraValue = POSITION;
    this->previousRotation = ROTATION;
}

FVector UWorldCameraArm::BlendLocations(const FVector& desired, const FVector& hit, bool blocked, float delta)
{
    const FVector ORIGIN = GetComponentLocation() + this->TargetOffset;
    const FVector DIRECTION = (desired - ORIGIN).GetSafeNormal();
    const float SAFE =
        blocked ? FMath::Max(0.f, float(FVector::DotProduct(hit - ORIGIN, DIRECTION))) : FVector::Dist(ORIGIN, desired);
    if (!this->collisionInitialized || SAFE < this->collisionDistance)
    {
        this->collisionDistance = SAFE;
        this->collisionInitialized = true;
    }
    else
    {
        this->collisionDistance = FMath::FInterpTo(this->collisionDistance, SAFE, delta, 7.f);
    }

    if (FMath::Abs(SAFE - this->collisionDistance) < .5f)
    {
        this->collisionDistance = SAFE;
    }

    return ORIGIN + DIRECTION * FMath::Min(this->collisionDistance, SAFE);
}

