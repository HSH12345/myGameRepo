#pragma once
#include "WorldPartition/WorldPartitionBuilder.h"
#include "RpgWorldPartitionBuilder.generated.h"

UCLASS()

class URpgWorldPartitionBuilder : public UWorldPartitionBuilder
{
    GENERATED_BODY()
public:
    virtual bool RequiresCommandletRendering() const override
    {
        return false;
    }

    virtual ELoadingMode GetLoadingMode() const override
    {
        return ELoadingMode::Custom;
    }

protected:
    virtual bool PreRun(UWorld* world, FPackageSourceControlHelper& helper) override;
    virtual bool RunInternal(UWorld* world, const FCellInfo& cell, FPackageSourceControlHelper& helper) override;
};

