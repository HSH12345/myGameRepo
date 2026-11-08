#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LocalSaveManager.generated.h"

UCLASS()

class RPG_API ULocalSaveManager : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FString uid;

    UPROPERTY()
    FString token;

public:
    UFUNCTION()
    static bool SaveUidAndToken(const FString& newUid, const FString& newToken);

    UFUNCTION()
    static bool LoadUidAndToken(const FString& slot, FString& loadedUid, FString& loadedToken);

    UFUNCTION()
    static bool ClearUidAndToken();

private:
};

