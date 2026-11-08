#pragma once
#include "Commandlets/Commandlet.h"
#include "MapSyncTestCommandlet.generated.h"

// Creates a new, saved non-partitioned acceptance-test level. Never overwrites a level.
UCLASS()

class RPG_API UMapSyncTestCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UMapSyncTestCommandlet();
    virtual int32 Main(const FString& params) override;
};

