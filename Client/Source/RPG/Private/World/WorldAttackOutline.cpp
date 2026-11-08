#include "World/WorldPlayerPawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

void AWorldPlayerPawn::RefreshAttackOutline()
{
    check(IsInGameThread());
    for (auto mesh : this->attackOutlineMeshes)
    {
        if (IsValid(mesh))
        {
            mesh->DestroyComponent();
        }

    }

    this->attackOutlineMeshes.Empty();
    if (!this->combatHighlighted)
    {
        return;
    }

    if (!this->attackOutlineMaterial)
    {
        this->attackOutlineMaterial =
            LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RPG/Camera/M_AttackOutline.M_AttackOutline"));
    }

    if (!ensureMsgf(this->attackOutlineMaterial, TEXT("Attack outline material missing")))
    {
        return;
    }

    TInlineComponentArray<UMeshComponent*> sources;
    GetComponents(sources);
    for (auto* source : sources)
    {
        if (!source->IsVisible() || source->bHiddenInGame || source->ComponentHasTag(TEXT("RPG.AttackOutline")))
        {
            continue;
        }

        UMeshComponent* outline = nullptr;
        if (auto* skin = Cast<USkeletalMeshComponent>(source); skin && skin->GetSkeletalMeshAsset())
        {
            auto* copy = NewObject<USkeletalMeshComponent>(this);
            copy->SetSkeletalMesh(skin->GetSkeletalMeshAsset());
            copy->SetLeaderPoseComponent(skin);
            outline = copy;
        }
        else if (auto* staticValue = Cast<UStaticMeshComponent>(source); staticValue && staticValue->GetStaticMesh())
        {
            auto* copy = NewObject<UStaticMeshComponent>(this);
            copy->SetStaticMesh(staticValue->GetStaticMesh());
            outline = copy;
        }

        if (!outline)
        {
            continue;
        }

        AddInstanceComponent(outline);
        outline->ComponentTags.Add(TEXT("RPG.AttackOutline"));
        outline->SetupAttachment(source);
        outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        outline->SetGenerateOverlapEvents(false);
        outline->SetCanEverAffectNavigation(false);
        outline->SetCastShadow(false);
        outline->SetRenderCustomDepth(false);
        outline->SetRenderInDepthPass(false);
        outline->SetBoundsScale(1.1f);
        for (int32 i = 0; i < outline->GetNumMaterials(); ++i)
        {
            outline->SetMaterial(i, this->attackOutlineMaterial);
        }

        outline->RegisterComponent();
        outline->SetComponentTickEnabled(false);
        this->attackOutlineMeshes.Add(outline);
    }

}

