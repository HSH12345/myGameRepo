#include "UIs/RpgArtBorder.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Widgets/Layout/SBorder.h"
#include "Rendering/DrawElements.h"

namespace
{
class SRpgArtBorder : public SBorder
{
public:
    void SetOwner(UButton* inOwner)
    {
        this->owner = inOwner;
    }

    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& geometry, const FSlateRect& cull,
                          FSlateWindowElementList& elements, int32 layer, const FWidgetStyle& style,
                          bool parentEnabled) const override
    {
        const FSlateBrush* source = GetBorderImage();
        FLinearColor ownerTint = FLinearColor::White;
        if (auto* button = this->owner.Get())
        {
            const auto& skin = button->GetStyle();
            ownerTint = button->GetBackgroundColor();
            source = !button->GetIsEnabled() ? &skin.Disabled
                     : button->IsPressed()   ? &skin.Pressed
                     : button->IsHovered()   ? &skin.Hovered
                                             : &skin.Normal;
        }

        if (source && source->GetResourceObject())
        {
            const bool PANEL = source->GetResourceObject()->GetName().EndsWith(TEXT("Panel"));
            const bool SLOT = source->GetResourceObject()->GetName().EndsWith(TEXT("Slot"));
            // The source art is square for panels/slots, 3:1 for action buttons.
            const FVector2D REFERENCE = PANEL ? FVector2D(256) : SLOT ? FVector2D(96) : FVector2D(72, 24);
            const float x[6] = {0, PANEL ? .19f : .18f, .43f, .57f, PANEL ? .81f : .82f, 1};
            const float y[6] = {0, PANEL ? .17f : SLOT ? .18f : .37f, .43f, .57f, PANEL ? .83f : SLOT ? .82f : .63f, 1};
            if (this->cachedResource != source->GetResourceObject())
            {
                this->cachedResource = source->GetResourceObject();
                this->parts.SetNum(25);
                for (int r = 0; r < 5; ++r)
                {
                    for (int c = 0; c < 5; ++c)
                    {
                        auto& part = this->parts[r * 5 + c];
                        part = *source;
                        part.DrawAs = ESlateBrushDrawType::Image;
                        part.SetUVRegion(FBox2f(FVector2f(x[c], y[r]), FVector2f(x[c + 1], y[r + 1])));
                    }

                }

            }

            const FVector2D SIZE = geometry.GetLocalSize();
            const float FIXED_X = (x[1] + x[3] - x[2] + 1 - x[4]) * REFERENCE.X;
            const float FIXED_Y = (y[1] + y[3] - y[2] + 1 - y[4]) * REFERENCE.Y;
            const float SCALE =
                FMath::Min(1.f, FMath::Min(SIZE.X / FMath::Max(1.f, FIXED_X), SIZE.Y / FMath::Max(1.f, FIXED_Y)));
            auto cuts = [&](const float* uv, float extent, float ref, float* out)
            {
                const float A = uv[1] * ref * SCALE, B = (uv[3] - uv[2]) * ref * SCALE, C = (1 - uv[4]) * ref * SCALE;
                const float STRETCH = FMath::Max(0.f, extent - A - B - C) * .5f;
                out[0] = 0;
                out[1] = A;
                out[2] = A + STRETCH;
                out[3] = out[2] + B;
                out[4] = out[3] + STRETCH;
                out[5] = extent;
            };

            float px[6], py[6];
            cuts(x, SIZE.X, REFERENCE.X, px);
            cuts(y, SIZE.Y, REFERENCE.Y, py);
            const auto TINT = source->GetTint(style) * style.GetColorAndOpacityTint() *
                              GetBorderBackgroundColor().GetColor(style) * ownerTint;
            const auto EFFECTS =
                ShouldBeEnabled(parentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
            for (int r = 0; r < 5; ++r)
            {
                for (int c = 0; c < 5; ++c)
                {
                    const FVector2D SPAN(px[c + 1] - px[c], py[r + 1] - py[r]);
                    if (SPAN.X <= 0 || SPAN.Y <= 0)
                    {
                        continue;
                    }

                    FSlateDrawElement::MakeBox(
                        elements, layer, geometry.ToPaintGeometry(SPAN, FSlateLayoutTransform(FVector2D(px[c], py[r]))),
                        &this->parts[r * 5 + c], EFFECTS, TINT);
                }

            }

        }

        return SCompoundWidget::OnPaint(args, geometry, cull, elements, layer + 1, style, parentEnabled);
    }

private:
    TWeakObjectPtr<UButton> owner;
    mutable const UObject* cachedResource = nullptr; // Brush UPROPERTY/UMG owns the resource.
    mutable TArray<FSlateBrush> parts;
};

}

TSharedRef<SWidget> URpgArtBorder::RebuildWidget()
{
    SetClipping(EWidgetClipping::ClipToBounds);
    auto art = SNew(SRpgArtBorder);
    this->MyBorder = art;
    art->SetOwner(this->styleOwner.Get());
    if (GetContentSlot())
    {
        CastChecked<UBorderSlot>(GetContentSlot())->BuildSlot(this->MyBorder.ToSharedRef());
    }

    return art;
}

void URpgArtBorder::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (this->MyBorder)
    {
        StaticCastSharedPtr<SRpgArtBorder>(this->MyBorder)->SetOwner(this->styleOwner.Get());
    }

}

void URpgArtBorder::SetStyleOwner(UButton* button)
{
    this->styleOwner = button;
    if (this->MyBorder)
    {
        StaticCastSharedPtr<SRpgArtBorder>(this->MyBorder)->SetOwner(button);
    }

}

