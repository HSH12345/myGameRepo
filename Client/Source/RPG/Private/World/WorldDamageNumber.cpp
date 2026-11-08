#include "World/WorldDamageNumber.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UWorldDamageNumber::RebuildWidget()
{
    if (!this->WidgetTree->RootWidget)
    {
        auto* root = this->WidgetTree->ConstructWidget<UCanvasPanel>();
        this->WidgetTree->RootWidget = root;
        this->number = this->WidgetTree->ConstructWidget<UTextBlock>();
        this->number->SetJustification(ETextJustify::Center);
        this->number->SetShadowOffset(FVector2D(1, 2));
        this->number->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .85));
        auto* positionSlot = root->AddChildToCanvas(this->number);
        positionSlot->SetAnchors(FAnchors(.5, .5));
        positionSlot->SetAlignment(FVector2D(.5, .5));
        positionSlot->SetAutoSize(true);
        SetVisibility(ESlateVisibility::HitTestInvisible);
        SetRenderTransformPivot(FVector2D(.5, .5));
    }

    return Super::RebuildWidget();
}

void UWorldDamageNumber::Present(uint32 value, const FLinearColor& color, uint64 lane, bool incoming)
{
    TakeWidget();
    if (!this->number)
    {
        return;
    }

    auto font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), value ? 28 : 19);
    font.OutlineSettings.OutlineSize = 2;
    font.OutlineSettings.OutlineColor = FLinearColor(.015, .025, .045, .95);
    this->number->SetFont(font);
    this->number->SetText(value ? FText::AsNumber(value) : FText::FromString(TEXT("MISS")));
    this->number->SetColorAndOpacity(value ? (incoming ? FLinearColor(1, .27, .2) : color)
                                           : FLinearColor(.65, .72, .8));
    this->side = (int32(lane % 5) - 2) * .45f;
    SetRenderOpacity(1);
    Animate(0);
}

void UWorldDamageNumber::Animate(float t)
{
    const float LIFT = 1 - FMath::Square(1 - t);
    SetRenderTranslation(FVector2D(this->side * (18 + 26 * LIFT), -55 * LIFT));
    const float POP = t < .12f ? FMath::Lerp(.85f, 1.24f, t / .12f)
                               : FMath::Lerp(1.24f, 1.f, FMath::Clamp((t - .12f) / .18f, 0.f, 1.f));
    SetRenderScale(FVector2D(POP));
    const float FADE = FMath::Clamp((t - .6f) / .4f, 0.f, 1.f);
    SetRenderOpacity(1 - FADE * FADE * (3 - 2 * FADE));
}

