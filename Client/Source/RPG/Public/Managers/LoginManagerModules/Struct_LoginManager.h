#pragma once

#include "CoreMinimal.h"
#include "Struct_LoginManager.generated.h"

#define MAX_CHARACTER_COUNT 8

USTRUCT(BlueprintType)

struct FEquipmentSummary
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    uint8 slot = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 itemTemplateId = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 appearanceId = 0;
};

USTRUCT(BlueprintType)

struct FCharacterSummary
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int64 characterUnique = 0;

    UPROPERTY()
    int32 race = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 gender = 0;

    UPROPERTY()
    FString name = "";

    UPROPERTY()
    int32 level = 0;

    UPROPERTY(BlueprintReadOnly)
    TArray<FEquipmentSummary> equipment;
};

USTRUCT(BlueprintType)

struct FAccountInfo
{
    GENERATED_BODY()

public:
    UPROPERTY()
    uint64 accountUnique = 0;

    UPROPERTY()
    TArray<FCharacterSummary> characterSlots;
};

