#pragma once
#include "CoreMinimal.h"
#include "Components/Border.h"
#include "RpgArtBorder.generated.h"
class UButton;

// Keep corners and center ornaments at one uniform scale; stretch only the
// undecorated strips. Still a UBorder, with the ordinary UMG content/slot API.
UCLASS()

class RPG_API URpgArtBorder : public UBorder
{
    GENERATED_BODY()
public:
    void SetStyleOwner(UButton* button);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void SynchronizeProperties() override;
    TWeakObjectPtr<UButton> styleOwner;
};

