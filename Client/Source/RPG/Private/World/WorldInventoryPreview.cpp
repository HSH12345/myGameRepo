#include "World/WorldInventoryUI.h"
#include "UIs/RpgUITheme.h"
#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/MeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"

void UWorldInventoryUI::UpdateCharacterDetails()
{
    auto* game = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    if (auto* pawn = game->GetInventoryCharacter())
    {
        this->characterName->SetText(FText::FromString(pawn->GetDisplayName()));
        const FString VALUE =
            FString::Printf(TEXT("공격 %d\n방어 %d\nHP %d/%d\nMP %d/%d\n명중 %d%%\n회피 %d%%"), pawn->attack,
                            pawn->defense, pawn->hp, pawn->maxHp, pawn->mp, pawn->maxMp, pawn->accuracy, pawn->evasion);
        if (this->characterStats->GetText().ToString() != VALUE)
        {
            this->characterStats->SetText(FText::FromString(VALUE));
        }

    }

    if (auto* pawn = game->GetShopMerchant())
    {
        this->merchantName->SetText(FText::FromString(RpgUI::Caption(pawn->GetDisplayName())));
    }

}

void UWorldInventoryUI::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    if (!IsVisible())
    {
        return;
    }

    this->previewElapsed += deltaSeconds;
    if (this->previewElapsed < .1f)
    {
        return;
    }

    this->previewElapsed = 0;
    UpdateCharacterDetails();
    UpdatePreviews();
#if !UE_BUILD_SHIPPING
    if (this->longTextFixture)
    {
        LongTextForTest();
    }

#endif
}

void UWorldInventoryUI::UpdatePreviews()
{
    if (!FApp::CanEverRender() || !GetWorld() || !this->characterImage)
    {
        return;
    }

    auto* game = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    if (!IsValid(this->previewRig))
    {
        FActorSpawnParameters params;
        params.ObjectFlags |= RF_Transient;
        this->previewRig = GetWorld()->SpawnActor<AActor>(params);
        if (!this->previewRig)
        {
            return;
        }

    }

    auto capture = [&](AWorldPlayerPawn* pawn, TObjectPtr<USceneCaptureComponent2D>& camera,
                       TObjectPtr<UTextureRenderTarget2D>& target, UImage* image, bool portrait)
    {
        if (!IsValid(pawn) || !pawn->AppearanceReady() || !pawn->EquipmentReady())
        {
            return;
        }

        if (!camera)
        {
            target = NewObject<UTextureRenderTarget2D>(this);
            target->ClearColor = FLinearColor(.018f, .028f, .045f, 1);
            target->RenderTargetFormat = RTF_RGBA8;
            target->InitAutoFormat(256, portrait ? 320 : 384);
            camera = NewObject<USceneCaptureComponent2D>(this->previewRig);
            this->previewRig->AddInstanceComponent(camera);
            camera->bCaptureEveryFrame = false;
            camera->bCaptureOnMovement = false;
            camera->bAlwaysPersistRenderingState = true;
            camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
            camera->TextureTarget = target;
            camera->FOVAngle = 30;
            camera->MaxViewDistanceOverride = 650;
            camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
            camera->PostProcessSettings.AutoExposureMinBrightness = 1;
            camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
            camera->PostProcessSettings.AutoExposureMaxBrightness = 1;
            camera->RegisterComponent(); // OnRegister restores show flags from the archetype first.
            camera->ShowFlags.SetAtmosphere(false);
            camera->ShowFlags.SetFog(false);
            camera->ShowFlags.SetVolumetricFog(false);
            camera->ShowFlags.SetDeferredAtmospherePass(false);
            camera->ShowFlags.SetLighting(false);
            FSlateBrush brush;
            brush.SetResourceObject(target);
            brush.ImageSize = FVector2D(256, portrait ? 320 : 384);
            image->SetBrush(brush);
        }

        camera->ClearShowOnlyComponents();
        TInlineComponentArray<UMeshComponent*> meshes(pawn);
        for (auto* mesh : meshes)
        {
            if (mesh->IsVisible())
            {
                camera->ShowOnlyComponent(mesh);
            }

        }

        const FVector FOCUS = pawn->GetActorLocation() + FVector(0, 0, portrait ? 130 : 90);
        const FVector LOCATION = FOCUS + pawn->GetActorForwardVector() * (portrait ? 200 : 320);
        camera->SetWorldLocationAndRotation(LOCATION, (FOCUS - LOCATION).Rotation());
        camera->CaptureScene();
    };

    capture(game->GetInventoryCharacter(), this->characterCapture, this->characterTarget, this->characterImage, false);
    if (this->shopPanel->IsVisible())
    {
        capture(game->GetShopMerchant(), this->merchantCapture, this->merchantTarget, this->merchantImage, true);
    }

}

void UWorldInventoryUI::StopPreviews()
{
    if (this->characterImage)
    {
        this->characterImage->SetBrushFromTexture(nullptr);
    }

    if (this->merchantImage)
    {
        this->merchantImage->SetBrushFromTexture(nullptr);
    }

    if (IsValid(this->previewRig))
    {
        this->previewRig->Destroy();
    }

    this->previewRig = nullptr;
    this->characterCapture = nullptr;
    this->merchantCapture = nullptr;
    this->characterTarget = nullptr;
    this->merchantTarget = nullptr;
    this->previewElapsed = 0;
}

