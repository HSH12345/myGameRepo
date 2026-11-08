#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WorldDamageNumber.generated.h"
class UTextBlock;
UCLASS()

class RPG_API UWorldDamageNumber : public UUserWidget
{
    GENERATED_BODY()
public:
    void Present(uint32 value, const FLinearColor& color, uint64 lane, bool incoming);
    void Animate(float progress);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> number;
    float side = 0;
};

