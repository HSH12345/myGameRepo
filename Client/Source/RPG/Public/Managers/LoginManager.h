#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameMain.h"
#include "LoginManagerModules/Struct_LoginManager.h"
#include "LoginManager.generated.h"
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoginStatus, const FString&, Status);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoginFailed, const FString&, Reason);

UCLASS()

class RPG_API ULoginManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;
    static ULoginManager* GetInstance(UObject* worldContext);

    UFUNCTION()
    bool StartLoginPorcess();
    bool StartNaverLoginProcess();
    bool CompleteNaverLogin(const FString& uid);
    void ReportLoginFailure(const FString& reason);
    void Logout();
    UPROPERTY(BlueprintAssignable)
    FLoginStatus onLoginStatus;
    UPROPERTY(BlueprintAssignable)
    FLoginFailed onLoginFailed;
    void StartCharacterSelectionProcess();
    void OnAuthenticated(const FString& uid);

    FAccountInfo accountInfo;

private:
    UFUNCTION()
    void HandleAuthToken(const FString& token);
    UFUNCTION()
    void HandleAuthFailed(const FString& reason);
    UFUNCTION()
    void HandleAuthStatus(const FString& status);
    void HandleNetworkClosed();
    uint64 tokenConnectionGeneration = 0;
    bool usingNaver = false;
    UFUNCTION()
    void HandleCharacterListChanged();
};

