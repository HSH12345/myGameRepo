#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "World/WorldPlayerPawn.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"

class FEquipmentLifecycleCommand final : public IAutomationLatentCommand
{
public:
    FEquipmentLifecycleCommand(FAutomationTestBase* inTest, UWorld* world) : test(inTest), scene(world)
    {
        this->pawn = world->SpawnActor<AWorldPlayerPawn>();
        this->pawn->SetDisplayName(TEXT("Equipment audit"));
        this->pawn->SetActorTickEnabled(false);
        this->pawn->AwaitInitialEquipment();
        this->pawn->LoadAppearance(0, 0);
        this->deadline = FPlatformTime::Seconds() + 120;
        auto* light = world->SpawnActor<ADirectionalLight>(FVector(0, 0, 400), FRotator(-35, 135, 0));
        light->GetLightComponent()->SetIntensity(5.f);
        auto* fill = world->SpawnActor<ADirectionalLight>(FVector(0, 0, 400), FRotator(-25, -45, 0));
        fill->GetLightComponent()->SetIntensity(2.f);
        auto* sky = world->SpawnActor<ASkyLight>();
        auto* skyComponent = sky->GetLightComponent();
        skyComponent->SetMobility(EComponentMobility::Movable);
        skyComponent->SourceType = SLS_SpecifiedCubemap;
        skyComponent->SetCubemap(LoadObject<UTextureCube>(
            nullptr, TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")));
        skyComponent->SetIntensity(2.f);
        this->capture = world->SpawnActor<ASceneCapture2D>(
            FVector(450, -100, 180), FRotationMatrix::MakeFromX(FVector(-450, 100, -80)).Rotator());
        this->target = NewObject<UTextureRenderTarget2D>(this->capture.Get());
        this->target->InitAutoFormat(768, 768);
        this->target->ClearColor = FLinearColor(.08f, .1f, .14f);
        auto* component = this->capture->GetCaptureComponent2D();
        component->TextureTarget = this->target.Get();
        component->FOVAngle = 30;
        // BaseColor is a geometry/material inspection pass; real world lighting is checked by UnrealEconomySmoke.
        component->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
        component->bCaptureEveryFrame = false;
        component->bCaptureOnMovement = false;
    }

    bool Update() override
    {
        if (!this->pawn.IsValid() || FPlatformTime::Seconds() > this->deadline)
        {
            this->test->AddError(TEXT("Equipment lifecycle timed out"));
            return true;
        }

        if (!this->initialEquipmentSent && this->pawn->AppearanceReady())
        {
            this->test->TestTrue(TEXT("Loaded body stays hidden while initial equipment snapshot is delayed"),
                                 this->pawn->IsHidden());
            this->pawn->LoadEquipment({2001, 2002}, ++this->revision);
            this->initialEquipmentSent = true;
        }

        if (!this->pawn->AppearanceReady() || !this->pawn->EquipmentReady())
        {
            if (!this->pawn->IsHidden())
            {
                this->test->AddError(TEXT("Incomplete appearance/equipment was exposed"));
                return true;
            }

            return false;
        }

        this->test->TestFalse(TEXT("Complete outfit is visible"), this->pawn->IsHidden());
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() ||
            (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return false;
        }

        if (this->stage < 6)
        {
            if (!this->waiting)
            {
                const auto GENDER = this->stage / 3;
                const auto STYLE = this->stage % 3;
                if (this->currentGender != GENDER)
                {
                    this->currentGender = GENDER;
                    this->pawn->LoadAppearance(0, GENDER);
                    return false;
                }

                this->pawn->LoadEquipment({2001, STYLE == 0 ? 0U : STYLE == 1 ? 2002U : 2003U}, ++this->revision);
                this->test->TestTrue(TEXT("Outfit request hides partial body immediately"), this->pawn->IsHidden());
                this->waiting = true;
                return false;
            }

            TInlineComponentArray<USkeletalMeshComponent*> meshes;
            this->pawn->GetComponents(meshes);
            for (auto* mesh : meshes)
            {
                mesh->TickAnimation(.1f, false);
                mesh->RefreshBoneTransforms();
                mesh->UpdateChildTransforms(EUpdateTransformFlags::OnlyUpdateIfUsingSocket);
            }

            if (this->stage == 0)
            {
                TInlineComponentArray<UStaticMeshComponent*> statics;
                this->pawn->GetComponents(statics);
                for (auto* mesh : statics)
                {
                    if (mesh->GetStaticMesh() && mesh->GetStaticMesh()->GetName().Contains(TEXT("Sword")))
                    {
                        UE_LOG(LogTemp, Display, TEXT("EQUIPMENT_AUDIT sword_bounds=%s hand=%s"),
                               *mesh->GetStaticMesh()->GetBoundingBox().ToString(),
                               *mesh->GetComponentTransform().ToString());
                    }

                }

            }

            this->capture->GetCaptureComponent2D()->CaptureScene();
            FlushRenderingCommands();
            TArray<FColor> pixels;
            if (!this->target->GameThread_GetRenderTargetResource()->ReadPixels(pixels))
            {
                this->test->AddError(TEXT("Equipment render read failed"));
                return true;
            }

            TArray64<uint8> png;
            FImageUtils::PNGCompressImageArray(768, 768, pixels, png);
            const FString FILE =
                FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EquipmentVerification"),
                                FString::Printf(TEXT("Gender%d-Style%d.png"), this->stage / 3, this->stage % 3));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(FILE), true);
            this->test->TestTrue(TEXT("Equipment render exported"), FFileHelper::SaveArrayToFile(png, *FILE));
            this->test->TestTrue(TEXT("Equipment is ready"), this->pawn->EquipmentReady());
            ++this->stage;
            this->waiting = false;
            return false;
        }

        if (this->stage < 106)
        {
            if (!this->waiting)
            {
                this->pawn->LoadEquipment({this->stage % 2 ? 2001U : 0U, this->stage % 3 ? 2002U : 2003U},
                                          ++this->revision);
                this->waiting = true;
                return false;
            }

            ++this->stage;
            this->waiting = false;
            return false;
        }

        if (this->stage == 106)
        {
            CollectGarbage(RF_NoFlags); // Base body/idle must survive while the outfit replaces both references.
            this->pawn->LoadEquipment({2001, 2003}, ++this->revision);
            this->pawn->LoadEquipment({2001, 2002}, ++this->revision);
            this->pawn->SetPooledActive(false);
            this->pawn->SetPooledActive(true);
            this->pawn->LoadEquipment({}, ++this->revision);
            CollectGarbage(RF_NoFlags);
            ++this->stage;
            return false;
        }

        this->test->TestTrue(TEXT("Rapid changes/release/GC end in unequipped state"), this->pawn->EquipmentReady());
        TInlineComponentArray<USkeletalMeshComponent*> remaining;
        this->pawn->GetComponents(remaining);
        this->test->TestEqual(TEXT("Equipment parts removed"), remaining.Num(), 4);
        bool restored = false;
        for (auto* mesh : remaining)
        {
            if (mesh->GetSkeletalMeshAsset() &&
                mesh->GetSkeletalMeshAsset()->GetName().Equals(TEXT("Superhero_Female"), ESearchCase::IgnoreCase))
            {
                restored = true;
            }

        }

        this->test->TestTrue(TEXT("Base body restored after equipped GC"), restored);
        this->pawn->Destroy();
        this->capture->Destroy();
        return true;
    }

private:
    FAutomationTestBase* test;
    TWeakObjectPtr<UWorld> scene;
    TWeakObjectPtr<AWorldPlayerPawn> pawn;
    TWeakObjectPtr<ASceneCapture2D> capture;
    TWeakObjectPtr<UTextureRenderTarget2D> target;
    double deadline = 0;
    uint64 revision = 0;
    int32 stage = 0, currentGender = 0;
    bool waiting = false, initialEquipmentSent = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentAssetsLifecycleTest, "RPG.Equipment.AssetsAndLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEquipmentAssetsLifecycleTest::RunTest(const FString&)
{
    auto* world = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("Equipment test world"), world))
    {
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(FEquipmentLifecycleCommand(this, world));
    return true;
}

class FEmptyEquipmentBeforeAppearance final : public IAutomationLatentCommand
{
public:
    FEmptyEquipmentBeforeAppearance(FAutomationTestBase* inTest, UWorld* world) : test(inTest)
    {
        this->pawn = world->SpawnActor<AWorldPlayerPawn>();
        this->pawn->AwaitInitialEquipment();
        this->pawn->LoadAppearance(0, 0);
        this->pawn->LoadEquipment({}, 1);
        this->deadline = FPlatformTime::Seconds() + 90;
    }

    bool Update() override
    {
        if (!this->pawn.IsValid() || FPlatformTime::Seconds() > this->deadline)
        {
            this->test->AddError(TEXT("Empty equipment before appearance timed out"));
            return true;
        }

        if (!this->pawn->AppearanceReady())
        {
            return false;
        }

        this->test->TestTrue(TEXT("Early empty equipment snapshot completes presentation"),
                             this->pawn->InitialPresentationReady());
        this->test->TestFalse(TEXT("Complete empty equipment body is revealed"), this->pawn->IsHidden());
        this->pawn->Destroy();
        return true;
    }

private:
    FAutomationTestBase* test;
    TWeakObjectPtr<AWorldPlayerPawn> pawn;
    double deadline = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmptyEquipmentOrderTest, "RPG.Equipment.EmptySnapshotBeforeAppearance",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEmptyEquipmentOrderTest::RunTest(const FString&)
{
    auto* world = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("Equipment order world"), world))
    {
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(FEmptyEquipmentBeforeAppearance(this, world));
    return true;
}

#endif
