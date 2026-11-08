#pragma once
#include "Commandlets/Commandlet.h"
#include "WorldExportCommandlet.generated.h"

UCLASS()

class RPG_API UWorldExportCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UWorldExportCommandlet();
    virtual int32 Main(const FString& params) override;
};

