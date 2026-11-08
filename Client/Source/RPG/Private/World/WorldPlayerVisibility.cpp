#include "World/WorldPlayerPawn.h"
#include "World/WorldCameraOcclusion.h"
#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/SpringArmComponent.h"

void AWorldPlayerPawn::SetLocalVisibility(bool enabled)
{
    check(IsInGameThread());
    this->localVisibility = enabled;
    if (!enabled)
    {
        this->cameraOcclusion->Clear();
    }

    // Remote/NPC/pooled actors must not run camera collision sweeps.
    this->arm->bDoCollisionTest = enabled;
    this->arm->SetComponentTickEnabled(enabled);
    if (enabled && !this->visibilityMaterial)
    {
        this->visibilityMaterial =
            LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RPG/Camera/M_PlayerVisibility.M_PlayerVisibility"));
    }

    this->camera->PostProcessSettings.WeightedBlendables.Array.RemoveAll(
        [this](const FWeightedBlendable& entry)
        {
            return entry.Object == this->visibilityMaterial;
        });

    if (enabled && this->visibilityMaterial)
    {
        this->camera->PostProcessSettings.WeightedBlendables.Array.Add(
            FWeightedBlendable(1.f, this->visibilityMaterial));
    }

    ensureMsgf(!enabled || this->visibilityMaterial, TEXT("Own-player visibility material is missing"));
    RefreshVisibilityMeshes();
}

void AWorldPlayerPawn::RefreshVisibilityMeshes()
{
    check(IsInGameThread());
    TInlineComponentArray<UMeshComponent*> meshes;
    GetComponents(meshes);
    for (auto* mesh : meshes)
    {
        if (mesh->ComponentHasTag(TEXT("RPG.AttackOutline")))
        {
            continue;
        }

        mesh->SetCustomDepthStencilValue(this->localVisibility ? 241 : 0);
        mesh->SetRenderCustomDepth(this->localVisibility);
    }

    RefreshAttackOutline();
}

void AWorldPlayerPawn::SetCombatHighlighted(bool enabled)
{
    if (this->combatHighlighted == enabled)
    {
        return;
    }

    this->combatHighlighted = enabled;
    RefreshVisibilityMeshes();
}

