#include "World/WorldPlayerPawn.h"
#include "Characters/CharacterAppearanceDefinition.h"
#include "Characters/EquipmentVisualDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"

void AWorldPlayerPawn::ResetEquipmentVisual()
{
    for (const auto& part : this->equipmentParts)
    {
        if (IsValid(part))
        {
            part->DestroyComponent();
        }

    }

    this->equipmentParts.Empty();
    if (IsValid(this->equippedWeapon))
    {
        this->equippedWeapon->DestroyComponent();
    }

    this->equippedWeapon = nullptr;
    this->equipmentDefinitions.Empty();
    if (this->skeletalBody && this->baseBodyMesh && this->baseIdleAnimation)
    {
        this->idleAnimation = this->baseIdleAnimation;
        this->skeletalBody->SetSkeletalMesh(this->baseBodyMesh);
        this->skeletalBody->PlayAnimation(this->moving ? CurrentMovementAnimation() : this->idleAnimation.Get(), true);
    }

    for (const auto& part : this->appearanceParts)
    {
        if (IsValid(part))
        {
            part->SetVisibility(true);
        }

    }

}

void AWorldPlayerPawn::AwaitInitialEquipment()
{
    this->awaitingInitialEquipment = true;
    this->equipmentReady = false;
    RefreshPresentation();
}

void AWorldPlayerPawn::LoadEquipment(const std::array<std::uint32_t, 2>& ids, uint64 revision)
{
    if (revision < this->equipmentRevision)
    {
        return;
    }

    if (revision == this->equipmentRevision && ids == this->desiredEquipment && !this->equipmentFailed &&
        !this->awaitingInitialEquipment)
    {
        return;
    }

    this->awaitingInitialEquipment = false;
    this->equipmentRevision = revision;
    this->desiredEquipment = ids;
    this->equipmentReady = false;
    RefreshPresentation();
    ++this->equipmentGeneration;
    if (this->equipmentLoad)
    {
        this->equipmentLoad->CancelHandle();
    }

    this->equipmentLoad.Reset();
    if (this->appearanceReady)
    {
        LoadEquipmentDefinitions(this->equipmentGeneration);
    }

}

void AWorldPlayerPawn::LoadEquipmentDefinitions(uint64 generation)
{
    if (!this->appearance || !this->appearanceReady)
    {
        return;
    }

    this->equipmentFailed = false;
    this->equipmentFailureReported = false;
    TArray<FSoftObjectPath> paths;
    for (auto id : this->desiredEquipment)
    {
        if (id)
        {
            paths.Add(
                FSoftObjectPath(FString::Printf(TEXT("/Game/RPG/Items/Visuals/DA_Equipment_%u_%u.DA_Equipment_%u_%u"),
                                                id, this->appearance->gender, id, this->appearance->gender)));
        }

    }

    if (paths.IsEmpty())
    {
        ResetEquipmentVisual();
        this->equipmentReady = true;
        RefreshPresentation();
        return;
    }

    this->equipmentLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        paths, FStreamableDelegate::CreateWeakLambda(
                   this,
                   [this, paths, generation]
                   {
                       if (generation != this->equipmentGeneration || !this->appearanceReady)
                       {
                           return;
                       }

                       TArray<FSoftObjectPath> parts;
                       this->equipmentDefinitions.Empty();
                       for (const auto& path : paths)
                       {
                           auto* definition = Cast<UEquipmentVisualDefinition>(path.ResolveObject());
                           if (!definition)
                           {
                               FailEquipment(TEXT("definition unavailable"));
                               return;
                           }

                           this->equipmentDefinitions.Add(definition);
                           if (!definition->outfitHead.IsNull())
                           {
                               parts.AddUnique(definition->outfitHead.ToSoftObjectPath());
                           }

                           for (const auto& part : definition->outfitParts)
                           {
                               parts.AddUnique(part.ToSoftObjectPath());
                           }

                           if (!definition->weapon.IsNull())
                           {
                               parts.AddUnique(definition->weapon.ToSoftObjectPath());
                           }

                           if (!definition->weaponIdle.IsNull())
                           {
                               parts.AddUnique(definition->weaponIdle.ToSoftObjectPath());
                           }

                       }

                       this->equipmentLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
                           parts, FStreamableDelegate::CreateWeakLambda(this,
                                                                        [this, generation]
                                                                        {
                                                                            FinishEquipment(generation);
                                                                        }));

                   }));

}

void AWorldPlayerPawn::FinishEquipment(uint64 generation)
{
    if (generation != this->equipmentGeneration || !this->appearanceReady || !this->skeletalBody)
    {
        return;
    }

    for (const auto& definition : this->equipmentDefinitions)
    {
        if ((!definition->outfitHead.IsNull() && !definition->outfitHead.Get()) ||
            (!definition->weapon.IsNull() && !definition->weapon.Get()) ||
            (!definition->weaponIdle.IsNull() && !definition->weaponIdle.Get()))
        {
            FailEquipment(TEXT("asset unavailable"));
            return;
        }

        for (const auto& part : definition->outfitParts)
        {
            if (!part.Get())
            {
                FailEquipment(TEXT("outfit part unavailable"));
                return;
            }

        }

        if (definition->outfitHead.Get() &&
            !this->idleAnimation->GetSkeleton()->IsCompatibleMesh(definition->outfitHead.Get()))
        {
            FailEquipment(TEXT("incompatible head"));
            return;
        }

        for (const auto& part : definition->outfitParts)
        {
            if (!this->idleAnimation->GetSkeleton()->IsCompatibleMesh(part.Get()))
            {
                FailEquipment(TEXT("incompatible outfit"));
                return;
            }

        }

    }

    // Preserve the loaded definitions while clearing the previous components.
    auto definitions = this->equipmentDefinitions;
    ResetEquipmentVisual();
    this->equipmentDefinitions = definitions;
    for (const auto& definition : this->equipmentDefinitions)
    {
        if (definition->outfitHead.Get())
        {
            this->skeletalBody->SetSkeletalMesh(definition->outfitHead.Get());
        }

        for (const auto& mesh : definition->outfitParts)
        {
            auto* component = NewObject<USkeletalMeshComponent>(this);
            AddInstanceComponent(component);
            component->SetReceivesDecals(false);
            component->SetupAttachment(this->skeletalBody);
            component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            component->SetSkeletalMesh(mesh.Get());
            component->RegisterComponent();
            component->SetLeaderPoseComponent(this->skeletalBody);
            this->equipmentParts.Add(component);
        }

        if (definition->hideHair && this->appearanceParts.Num() > 2)
        {
            this->appearanceParts[2]->SetVisibility(false);
        }

        if (definition->weapon.Get())
        {
            if (definition->weaponIdle.Get())
            {
                this->idleAnimation = definition->weaponIdle.Get();
            }

            this->equippedWeapon = NewObject<UStaticMeshComponent>(this);
            AddInstanceComponent(this->equippedWeapon);
            this->equippedWeapon->SetReceivesDecals(false);
            this->equippedWeapon->SetupAttachment(this->skeletalBody, definition->weaponBone);
            this->equippedWeapon->SetStaticMesh(definition->weapon.Get());
            this->equippedWeapon->SetRelativeTransform(definition->weaponTransform);
            this->equippedWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            this->equippedWeapon->RegisterComponent();
        }

    }

    this->skeletalBody->PlayAnimation(this->moving ? CurrentMovementAnimation() : this->idleAnimation.Get(), true);
    this->skeletalBody->TickAnimation(0, false);
    this->skeletalBody->RefreshBoneTransforms();
    this->skeletalBody->UpdateChildTransforms(EUpdateTransformFlags::OnlyUpdateIfUsingSocket);
    this->equipmentReady = true;
    this->equipmentLoad.Reset();
    RefreshVisibilityMeshes();
    RefreshPresentation();
    UE_LOG(LogTemp, Display, TEXT("EQUIPMENT ready revision=%llu main=%u outfit=%u"), this->equipmentRevision,
           this->desiredEquipment[0], this->desiredEquipment[1]);
}

void AWorldPlayerPawn::FailEquipment(const TCHAR* reason)
{
    ResetEquipmentVisual();
    this->equipmentReady = false;
    this->equipmentFailed = true;
    this->equipmentFailureReported = false;
    RefreshPresentation();
    this->equipmentLoad.Reset();
    UE_LOG(LogTemp, Warning, TEXT("Equipment display failed: %s"), reason);
}

void AWorldPlayerPawn::RetryEquipment()
{
    if (!this->equipmentFailed || !this->appearanceReady)
    {
        return;
    }

    ++this->equipmentGeneration;
    if (this->equipmentLoad)
    {
        this->equipmentLoad->CancelHandle();
    }

    this->equipmentLoad.Reset();
    LoadEquipmentDefinitions(this->equipmentGeneration);
}

