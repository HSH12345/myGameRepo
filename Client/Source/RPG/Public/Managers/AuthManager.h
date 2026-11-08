#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include <string>
THIRD_PARTY_INCLUDES_START
#include "firebase/app.h"
#include "firebase/auth.h"
THIRD_PARTY_INCLUDES_END
#include "TimerManager.h"
#include "HttpFwd.h"

#include "AuthManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAuthTokenReady, const FString&, IdToken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAuthFailed, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAuthStatus, const FString&, Status);

UCLASS()

class RPG_API UAuthManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;
    static UAuthManager* GetInstance(UObject* worldContext);

    void StartAuth(const FString& email, const FString& password);
    bool StartNaverLogin();
    bool RefreshNaverToken();
    void CancelAuth(bool signOut = false);
    bool MarkNaverAuthenticated(const FString& uid);

    bool HasNaverSession() const
    {
        return !this->validatedNaverUid.IsEmpty();
    }

    UPROPERTY(BlueprintAssignable)
    FAuthStatus onAuthStatus;
    UPROPERTY(BlueprintAssignable)
    FAuthTokenReady onAuthTokenReady;
    UPROPERTY(BlueprintAssignable)
    FAuthFailed onAuthFailed;

private:
    void WaitForSignIn();
    bool InitializeFirebaseApp();
    void HandleSignInResult();
    void WaitForToken();
    void HandleTokenResult();
    void FailAuth(const FString& reason);
    void PollNaver();
    void RequestNaver(const FString& route, TFunction<void(const TSharedPtr<class FJsonObject>&)> complete);
    void ExchangeNaver();
    FString authBaseUrl;
    FString naverRequestId;
    FString exchangeSecret;
    FString expectedNaverUid;
    FString validatedNaverUid;
    FHttpRequestPtr activeHttp;
    FTimerHandle naverPollTimer;
    uint64 authGeneration = 0;
    double naverDeadline = 0;
    bool naverFlow = false;

    firebase::App* firebaseApp = nullptr;
    firebase::auth::Auth* auth = nullptr;
    FThreadSafeBool shuttingDown{false};
    bool authInProgress = false;
    double operationStartSeconds = 0.0;
    firebase::Future<firebase::auth::AuthResult> signInFuture;
    firebase::Future<std::string> tokenFuture;
    FTimerHandle loginCheckHandle;
    FTimerHandle tokenCheckHandle;
};

