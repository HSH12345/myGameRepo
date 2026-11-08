#pragma once
#include "Blueprint/UserWidget.h"
#include "World/WorldHudTypes.h"
#include "WorldMinimap.generated.h"
UCLASS()

class RPG_API UWorldMinimap : public UUserWidget
{
    GENERATED_BODY()
public:
    const FWorldMinimapSnapshot& GetSnapshot() const
    {
        return this->snapshot;
    }

protected:
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& args, const FGeometry& geometry, const FSlateRect& culling,
                              FSlateWindowElementList& elements, int32 layer, const FWidgetStyle& style,
                              bool enabled) const override;

private:
    FWorldMinimapSnapshot snapshot;
    float elapsed = 1;
};

