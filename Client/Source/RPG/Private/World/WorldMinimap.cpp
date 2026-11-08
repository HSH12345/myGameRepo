#include "World/WorldMinimap.h"
#include "UIs/RpgUITheme.h"
#include "World/GameSessionSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void UWorldMinimap::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    this->elapsed += deltaSeconds;
    if (this->elapsed < .3f)
    {
        return;
    }

    this->elapsed = 0;
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->BuildMinimap(this->snapshot);
    }

}

int32 UWorldMinimap::NativePaint(const FPaintArgs& args, const FGeometry& geometry, const FSlateRect& culling,
                                 FSlateWindowElementList& elements, int32 layer, const FWidgetStyle& style,
                                 bool enabled) const
{
    layer = Super::NativePaint(args, geometry, culling, elements, layer, style, enabled);
    const auto SIZE = geometry.GetLocalSize();
    const FVector2D CENTER = SIZE * .5;
    const auto* brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    FSlateDrawElement::MakeBox(elements, ++layer, geometry.ToPaintGeometry(), brush, ESlateDrawEffect::None,
                               FLinearColor(.018f, .017f, .015f, .98f));
    elements.PushClip(FSlateClippingZone(geometry));
    auto point = [&](FVector2D p)
    {
        return CENTER + p * SIZE;
    };

    for (int32 i = 0; i + 1 < this->snapshot.lines.Num(); i += 2)
    {
        TArray<FVector2D> edge{point(this->snapshot.lines[i]), point(this->snapshot.lines[i + 1])};
        FSlateDrawElement::MakeLines(elements, layer + 1, geometry.ToPaintGeometry(), edge, ESlateDrawEffect::None,
                                     FLinearColor(.54f, .48f, .36f, .7f), true, 1);
    }

    auto markers = [&](const TArray<FVector2D>& points, FLinearColor color, float width)
    {
        for (const auto P : points)
        {
            if (FMath::Abs(P.X) < .49 && FMath::Abs(P.Y) < .49)
            {
                FSlateDrawElement::MakeBox(
                    elements, layer + 2,
                    geometry.ToPaintGeometry(FVector2D(width), FSlateLayoutTransform(point(P) - FVector2D(width / 2))),
                    brush, ESlateDrawEffect::None, color);
            }

        }

    };

    markers(this->snapshot.enemies, FLinearColor(.95f, .15f, .14f), 5);
    markers(this->snapshot.npcs, FLinearColor(1, .75f, .18f), 6);
    markers(this->snapshot.players, FLinearColor(.3f, .75f, 1), 5);
    markers(this->snapshot.exits, FLinearColor(.2f, 1, .65f), 7);
    const float ANGLE = FMath::DegreesToRadians(this->snapshot.heading);
    const FVector2D FORWARD(FMath::Sin(ANGLE), -FMath::Cos(ANGLE)), RIGHT(FORWARD.Y, -FORWARD.X);
    TArray<FVector2D> arrow{CENTER + FORWARD * 9, CENTER - FORWARD * 6 + RIGHT * 5, CENTER - FORWARD * 3,
                            CENTER - FORWARD * 6 - RIGHT * 5, CENTER + FORWARD * 9};
    FSlateDrawElement::MakeLines(elements, layer + 3, geometry.ToPaintGeometry(), arrow, ESlateDrawEffect::None,
                                 FLinearColor::White, true, 2);
    elements.PopClip();
    FSlateDrawElement::MakeText(
        elements, layer + 4,
        geometry.ToPaintGeometry(FVector2D(SIZE.X - 16, 20), FSlateLayoutTransform(FVector2D(8, 5))),
        RpgUI::Caption(this->snapshot.title) + TEXT("  북 N"), RpgUI::Font(12), ESlateDrawEffect::None,
        FLinearColor::White);
    return layer + 4;
}

