#include "World/WorldControlHUD.h"
#include "World/GameSessionSubsystem.h"
#include "UIs/RpgUITheme.h"
#include "UIs/RpgArtBorder.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Tests/SlatePointerClick.h"

void UWorldControlHUD::BuildQuickSlots(UCanvasPanel* canvas)
{
    auto text = [&](const FString& value, int32 size)
    {
        auto* label = this->WidgetTree->ConstructWidget<UTextBlock>();
        label->SetText(FText::FromString(value));
        RpgUI::Text(label, size);
        label->SetJustification(ETextJustify::Center);
        label->SetAutoWrapText(false);
        label->SetClipping(EWidgetClipping::ClipToBounds);
        label->SetVisibility(ESlateVisibility::HitTestInvisible);
        return label;
    };

    auto sized = [&](UWidget* content, float height)
    {
        auto* box = this->WidgetTree->ConstructWidget<USizeBox>();
        box->SetHeightOverride(height);
        box->SetContent(content);
        return box;
    };

    auto frame = [&](float y, float height)
    {
        auto* panel = this->WidgetTree->ConstructWidget<URpgArtBorder>();
        RpgUI::Frame(panel);
        panel->SetPadding(FMargin(18));
        auto* positionSlot = canvas->AddChildToCanvas(panel);
        positionSlot->SetAnchors(FAnchors(0, 1));
        positionSlot->SetAlignment(FVector2D(0, 1));
        positionSlot->SetPosition(FVector2D(212, y));
        positionSlot->SetSize(FVector2D(490, height));
        this->pointerRegions.Add(panel);
        return panel;
    };

    auto* bar = this->WidgetTree->ConstructWidget<UUniformGridPanel>();
    bar->SetSlotPadding(FMargin(2, 3));
    auto* barFrame = frame(-16, 154);
    barFrame->SetPadding(FMargin(10));
    Cast<UCanvasPanelSlot>(barFrame->Slot)->SetSize(FVector2D(350, 154));
    barFrame->SetContent(bar);
    for (int32 i = 0; i < 10; ++i)
    {
        auto* button = this->WidgetTree->ConstructWidget<UWorldQuickSlotButton>();
        button->ownerHud = this;
        button->index = i;
        RpgUI::Button(button, false, true);
        auto style = button->GetStyle();
        style.NormalPadding = style.PressedPadding = FMargin(7);
        button->SetStyle(style);
        button->OnClicked.AddDynamic(button, &UWorldQuickSlotButton::Activate);
        auto* cell = this->WidgetTree->ConstructWidget<UOverlay>();
        auto* buttonSlot = cell->AddChildToOverlay(button);
        buttonSlot->SetHorizontalAlignment(HAlign_Fill);
        buttonSlot->SetVerticalAlignment(VAlign_Fill);
        auto* width = this->WidgetTree->ConstructWidget<USizeBox>();
        width->SetWidthOverride(60);
        width->SetContent(sized(cell, 60));
        bar->AddChildToUniformGrid(width, i / 5, i % 5);
        auto* icon = this->WidgetTree->ConstructWidget<UImage>();
        icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        button->SetContent(RpgUI::FitImage(this->WidgetTree, icon));
        auto* glow = RpgUI::SelectionGlow(this->WidgetTree);
        auto* glowSlot = cell->AddChildToOverlay(glow);
        glowSlot->SetHorizontalAlignment(HAlign_Fill);
        glowSlot->SetVerticalAlignment(VAlign_Fill);
        this->quickSelection.Add(glow);
        auto* key = text(FString::FromInt((i + 1) % 10), 13);
        auto* keySlot = cell->AddChildToOverlay(key);
        keySlot->SetHorizontalAlignment(HAlign_Left);
        keySlot->SetVerticalAlignment(VAlign_Top);
        keySlot->SetPadding(FMargin(6, 2));
        this->quickLabels.Add(key);
        this->quickButtons.Add(button);
        this->quickIcons.Add(icon);
    }

    BuildSkillBook(canvas);
    BuildAutoPotions(canvas);
}

void UWorldControlHUD::ToggleQuickSlotPicker(int32)
{
    if (!this->quickSlotPicker)
    {
        return;
    }

    if (this->quickSlotPicker->IsVisible())
    {
        HideQuickSlotPicker();
        return;
    }

    this->pendingSkillBinding = 0;
    this->quickSlotPicker->SetVisibility(ESlateVisibility::Visible);
    RefreshSkillBook();
}

bool UWorldControlHUD::IsSkillBookVisible() const
{
    return this->quickSlotPicker && this->quickSlotPicker->IsVisible();
}

void UWorldControlHUD::HideQuickSlotPicker()
{
    this->pendingSkillBinding = 0;
    if (this->quickSlotPicker)
    {
        this->quickSlotPicker->SetVisibility(ESlateVisibility::Collapsed);
    }

}

void UWorldControlHUD::BindQuickSlotFromPicker(uint32 binding)
{
    if (binding == UINT32_MAX)
    {
        HideQuickSlotPicker();
        return;
    }

    if (!binding)
    {
        this->pendingSkillBinding = UINT32_MAX;
        return;
    }

    if (auto* game = GetGameInstance())
    {
        if (game->GetSubsystem<UGameSessionSubsystem>()->GetSkillRank(binding))
        {
            this->pendingSkillBinding = binding;
        }

    }

    RefreshSkillBook();
}

#if !UE_BUILD_SHIPPING
bool UWorldControlHUD::QuickSelectionCoversSlotForTest(int32 index) const
{
    if (!this->quickSelection.IsValidIndex(index) || !this->quickSelection[index]->IsVisible())
    {
        return false;
    }

    const auto& buttonGeometry = this->quickButtons[index]->GetCachedGeometry();
    const auto& glow = this->quickSelection[index]->GetCachedGeometry();
    return buttonGeometry.GetLocalSize().Equals(glow.GetLocalSize(), .5f) &&
           buttonGeometry.GetAbsolutePosition().Equals(glow.GetAbsolutePosition(), .5f);
}

bool UWorldControlHUD::OpenQuickSlotPickerForTest(int32 index)
{
    if (index < 0 || index >= this->quickButtons.Num())
    {
        return false;
    }

    ToggleQuickSlotPicker(index);
    return this->quickSlotPicker && this->quickSlotPicker->IsVisible();
}

bool UWorldControlHUD::ClickBindingForTest(uint32 binding)
{
    for (auto button : this->bindingButtons)
    {
        if (button->binding == binding)
        {
            return ClickSlateButtonForTest(button);
        }

    }

    return false;
}

bool UWorldControlHUD::QuickIconsInsideForTest() const
{
    if (this->quickButtons.Num() != 10 || this->quickIcons.Num() != 10 || this->quickLabels.Num() != 10)
    {
        return false;
    }

    for (int32 i = 0; i < this->quickIcons.Num(); ++i)
    {
        const auto& outer = this->quickButtons[i]->GetCachedGeometry();
        const auto& inner = this->quickIcons[i]->GetCachedGeometry();
        const auto A = outer.AbsoluteToLocal(inner.GetAbsolutePosition()),
                   B = outer.AbsoluteToLocal(inner.LocalToAbsolute(inner.GetLocalSize()));
        if (A.X < -.5 || A.Y < -.5 || B.X > outer.GetLocalSize().X + .5 || B.Y > outer.GetLocalSize().Y + .5)
        {
            return false;
        }

        const auto ART = this->quickIcons[i]->GetBrush().ImageSize;
        const auto DISPLAY = inner.GetLocalSize();
        if (ART.Y > 0 && DISPLAY.Y > 0 &&
            !FMath::IsNearlyEqual(double(ART.X / ART.Y), double(DISPLAY.X / DISPLAY.Y), .02))
        {
            return false;
        }

    }

    return this->quickButtons.Num() == 10 && this->quickLabels.Num() == 10;
}

bool UWorldControlHUD::QuickGridForTest() const
{
    if (!QuickIconsInsideForTest())
    {
        return false;
    }

    auto position = [&](int32 i)
    {
        return this->quickButtons[i]->GetCachedGeometry().GetAbsolutePosition();
    };

    return FMath::IsNearlyEqual(position(0).Y, position(4).Y, 1.) &&
           FMath::IsNearlyEqual(position(5).Y, position(9).Y, 1.) && position(5).Y > position(0).Y + 50 &&
           FMath::IsNearlyEqual(position(0).X, position(5).X, 1.) &&
           FMath::IsNearlyEqual(position(4).X, position(9).X, 1.) &&
           this->quickLabels[4]->GetText().ToString() == TEXT("5") &&
           this->quickLabels[9]->GetText().ToString() == TEXT("0");
}

#endif
