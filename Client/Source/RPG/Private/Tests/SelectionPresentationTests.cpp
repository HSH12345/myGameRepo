#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "UIs/RpgUITheme.h"
#include "Materials/Material.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"

class FSelectionRenderCommand final : public IAutomationLatentCommand
{
public:
    FSelectionRenderCommand(FAutomationTestBase* inTest, UWidgetTree* inTree)
        : test(inTest), tree(inTree), renderer(MakeUnique<FWidgetRenderer>(false))
    {
    }

    bool Update() override
    {
        const FIntPoint SIZE = this->stage == 0 ? FIntPoint(64, 64) : FIntPoint(800, 80);
        if (!this->target)
        {
            this->target.Reset(FWidgetRenderer::CreateTargetFor(FVector2D(SIZE), TF_Bilinear, false));
            this->deadline = FPlatformTime::Seconds() + 5;
        }

        auto* image = CastChecked<UImage>(this->tree->RootWidget);
        // UI shader/PSO publication needs real frames, not two draws in one game frame.
        this->renderer->DrawWidget(this->target.Get(), image->TakeWidget(), FVector2D(SIZE), 1.f / 60);
        FlushRenderingCommands();
        TArray<FColor> pixels;
        if (!this->test->TestTrue(TEXT("Read selection pixels"),
                                  this->target->GameThread_GetRenderTargetResource()->ReadPixels(pixels)))
        {
            return true;
        }

        int32 gold = 0;
        uint8 maxR = 0, maxG = 0, maxA = 0;
        for (const auto& pixel : pixels)
        {
            maxR = FMath::Max(maxR, pixel.R);
            maxG = FMath::Max(maxG, pixel.G);
            maxA = FMath::Max(maxA, pixel.A);
        }

        for (const auto& pixel : pixels)
        {
            if (pixel.R > 90 && pixel.R > pixel.B * 1.2 && pixel.G > 40)
            {
                ++gold;
            }

        }

        if (gold <= SIZE.X && FPlatformTime::Seconds() < this->deadline)
        {
            return false;
        }

        this->test->TestTrue(TEXT("Visible gold border on square and wide row"), gold > SIZE.X);
        const auto CENTER = pixels[SIZE.Y / 2 * SIZE.X + SIZE.X / 2];
        this->test->TestTrue(TEXT("Center remains transparent for the underlying icon/text"),
                             CENTER.R < 20 && CENTER.G < 20 && CENTER.B < 20);
        this->test->AddInfo(
            FString::Printf(TEXT("Selection %dx%d gold=%d center=%s"), SIZE.X, SIZE.Y, gold, *CENTER.ToString()));
        this->test->AddInfo(FString::Printf(TEXT("Selection geometry=%s desired=%s max=%u,%u,%u"),
                                            *image->GetCachedGeometry().GetLocalSize().ToString(),
                                            *image->GetDesiredSize().ToString(), maxR, maxG, maxA));
        FBufferArchive bytes;
        if (FImageUtils::ExportRenderTarget2DAsPNG(this->target.Get(), bytes))
        {
            FFileHelper::SaveArrayToFile(
                bytes, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Selection-%dx%d.png"), SIZE.X, SIZE.Y)));
        }

        this->target.Reset();
        return ++this->stage == 2;
    }

private:
    FAutomationTestBase* test;
    TStrongObjectPtr<UWidgetTree> tree;
    TStrongObjectPtr<UTextureRenderTarget2D> target;
    TUniquePtr<FWidgetRenderer> renderer;
    int32 stage = 0;
    double deadline = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSelectionPresentationTest, "RPG.UI.SelectionGlow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSelectionPresentationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UWidgetTree> tree(NewObject<UWidgetTree>());
    auto* image = RpgUI::SelectionGlow(tree.Get());
    tree->RootWidget = image;
    auto* material = Cast<UMaterialInterface>(image->GetBrush().GetResourceObject());
    if (!TestNotNull(TEXT("Selection shader asset"), material))
    {
        return false;
    }

    TestEqual(TEXT("UI material domain"), int32(material->GetMaterial()->MaterialDomain), int32(MD_UI));
    TestEqual(TEXT("Unselected effect hidden"), image->GetVisibility(), ESlateVisibility::Collapsed);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Tree and shared material survive GC"), IsValid(image) && IsValid(material));
    image->SetVisibility(ESlateVisibility::HitTestInvisible);
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }

    ADD_LATENT_AUTOMATION_COMMAND(FSelectionRenderCommand(this, tree.Get()));
    return true;
}

#endif
