#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/UObjectThreadContext.h"
#include "Misc/PackagePath.h"
#include "Animation/AnimData/IAnimationDataModel.h"

// Explicit offline setup only. Python cannot assign AnimationAsset's read-only Skeleton property.
// Tools/PrepareMegaspikan.py backs up and saves exactly these nine authorized assets.
static FAutoConsoleCommand _prepareMegaspikanSkeletons(
    TEXT("RPG.PrepareMegaspikanSkeletons"),
    TEXT("Repair the nine Megaspikan boss animation skeleton references; does not save packages."),
    FConsoleCommandDelegate::CreateLambda(
        []
        {
            auto* skeleton = LoadObject<USkeleton>(
                nullptr,
                TEXT("/Game/Characters/Megaspikan/Meshes/SK_Megaspikan_Body_Skeleton.SK_Megaspikan_Body_Skeleton"));
            if (!skeleton)
            {
                UE_LOG(LogTemp, Error, TEXT("MEGASPIKAN_SETUP missing skeleton"));
                return;
            }

            auto* mesh = LoadObject<USkeletalMesh>(
                nullptr, TEXT("/Game/Characters/Megaspikan/Meshes/SK_Megaspikan_Body.SK_Megaspikan_Body"));
            if (!mesh)
            {
                UE_LOG(LogTemp, Error, TEXT("MEGASPIKAN_SETUP missing mesh"));
                return;
            }

            if (mesh->GetSkeleton() != skeleton)
            {
                mesh->SetSkeleton(skeleton);
                mesh->MarkPackageDirty();
            }

            if (auto* material = LoadObject<UMaterialInterface>(
                    nullptr, TEXT("/Game/RPG/Characters/Megaspikan/M_Megaspikan_Gameplay.M_Megaspikan_Gameplay")))
            {
                auto materials = mesh->GetMaterials();
                for (auto& slot : materials)
                {
                    slot.MaterialInterface = material;
                }

                mesh->SetMaterials(materials);
                mesh->MarkPackageDirty(); // Updates the serialized skeletal-material cache too.
            }

            for (const TCHAR* name :
                 {TEXT("Idle"), TEXT("WalkForward"), TEXT("LeftClawsAttack"), TEXT("Roar"), TEXT("GetHitFront"),
                  TEXT("Death"), TEXT("LeftSmashAttack"), TEXT("BigSpittersAttack"), TEXT("SmallSpittersAttack")})
            {
                const FString PATH = FString::Printf(
                    TEXT("/Game/Characters/Megaspikan/Animations/ANIM_Megaspikan_%s.ANIM_Megaspikan_%s"), name, name);
                // Bind before PostLoad: upgrading an old sequence with no Skeleton drops its bone curves.
                const FString PACKAGE_NAME =
                    FString::Printf(TEXT("/Game/Characters/Megaspikan/Animations/ANIM_Megaspikan_%s"), name);
                auto* context = FUObjectThreadContext::Get().GetSerializeContext();
                BeginLoad(context, TEXT("MegaspikanSkeletonRepair"));
                auto* package = CreatePackage(*PACKAGE_NAME);
                auto* linker =
                    GetPackageLinker(package, FPackagePath::FromPackageNameChecked(PACKAGE_NAME), LOAD_None, nullptr);
                if (linker)
                {
                    linker->LoadAllObjects(true);
                }

                auto* anim = FindObject<UAnimSequence>(package, *FString::Printf(TEXT("ANIM_Megaspikan_%s"), name));
                if (anim && anim->HasAnyFlags(RF_NeedPostLoad))
                {
                    anim->SetSkeleton(skeleton);
                }

                EndLoad(context);
                if (!anim)
                {
                    UE_LOG(LogTemp, Error, TEXT("MEGASPIKAN_SETUP missing animation %s"), name);
                    return;
                }

                if (anim->GetSkeleton() != skeleton)
                {
                    anim->SetSkeleton(skeleton);
                    anim->MarkPackageDirty();
                }

                UE_LOG(LogTemp, Display, TEXT("MEGASPIKAN_SETUP repaired %s skeleton=%s"), name,
                       *GetNameSafe(anim->GetSkeleton()));
                UE_LOG(LogTemp, Display, TEXT("MEGASPIKAN_SETUP tracks=%d keys=%d"),
                       anim->GetDataModel()->GetNumBoneTracks(), anim->GetDataModel()->GetNumberOfKeys());
            }

        }));

#endif
