#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameMain.generated.h"

UENUM(BlueprintType)
enum class EMyLogType : uint8
{
    LOG UMETA(DisplayName = "Log"),
    WARNING UMETA(DisplayName = "Warning"),
    ERROR_VALUE UMETA(DisplayName = "Error")
};

UENUM(BlueprintType)
enum class EMyLogLocation : uint8
{
    NONE UMETA(DisplayName = "None"),
    INIT UMETA(DisplayName = "Init"),
    LOGIN UMETA(DisplayName = "Login"),
    RECEIVE_DISPATCHER UMETA(DisplayName = "ReceiveDispatcher"),
};

UCLASS()

class RPG_API UGameMain : public UGameInstance
{
    GENERATED_BODY()

public:
public:
    virtual void Init() override;

    static UGameMain* GetInstance();

    UFUNCTION(BlueprintCallable, Category = "GameMain")
    void Log(const FString& message, EMyLogType logType, EMyLogLocation logLocation = EMyLogLocation::NONE);

    UFUNCTION(BlueprintCallable, Category = "GameMain")
    void StartLoginPorcess();
};

