#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CharacterTypes.generated.h"

UENUM(BlueprintType)
enum class ECharacterListState : uint8
{
    DISCONNECTED,
    LOADING,
    READY,
    FAILED_VALUE
};

UENUM(BlueprintType)
enum class ECharacterCreationState : uint8
{
    IDLE,
    IN_FLIGHT,
    NEEDS_CONFIRMATION,
    COMPLETED
};

UENUM(BlueprintType)
enum class ECharacterCreationResult : uint8
{
    SUCCESS = 0,
    INVALID_NAME = 1,
    INVALID_APPEARANCE = 2,
    NAME_TAKEN = 3,
    LIMIT_REACHED = 4,
    OPERATION_CONFLICT = 5,
    BUSY = 6,
    UNAVAILABLE = 7,
    UNKNOWN_OUTCOME = 8,
    NOT_AUTHENTICATED = 9
};

USTRUCT(BlueprintType)

struct FCreateCharacterOptions
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString characterName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 race = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 gender = 0;
};

// No credentials here. Persist only enough to safely retry an uncertain write.
UCLASS()

class RPG_API UCharacterPendingSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY()
    FString accountUid;
    UPROPERTY()
    FString operationId;
    UPROPERTY()
    FCreateCharacterOptions options;
};

