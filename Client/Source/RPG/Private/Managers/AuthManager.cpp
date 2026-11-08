#include "Managers/AuthManager.h"
#include "GameMain.h"
#include "Misc/ConfigCacheIni.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RPG/Packet/BinaryPackets.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"
#endif

namespace
{
void LogAuth(UAuthManager* manager, const FString& message, EMyLogType authLogType)
{
    // A GameInstanceSubsystem already owns the correct GameInstance. Using the
    // global viewport here is unsafe during startup and ambiguous in multi-PIE.
    if (manager)
    {
        if (UGameMain* gameMain = Cast<UGameMain>(manager->GetGameInstance()))
        {
            gameMain->Log(message, authLogType);
            return;
        }

    }

    switch (authLogType)
    {
    case EMyLogType::WARNING:
        UE_LOG(LogTemp, Warning, TEXT("[Auth] %s"), *message);
        break;
    case EMyLogType::ERROR_VALUE:
        UE_LOG(LogTemp, Error, TEXT("[Auth] %s"), *message);
        break;
    default:
        UE_LOG(LogTemp, Log, TEXT("[Auth] %s"), *message);
        break;
    }

}

}

void UAuthManager::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);
    this->shuttingDown = false;
}

void UAuthManager::Deinitialize()
{
    CancelAuth();
    this->shuttingDown = true;
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->loginCheckHandle);
        world->GetTimerManager().ClearTimer(this->tokenCheckHandle);
    }

    this->authInProgress = false;
    delete this->auth;
    this->auth = nullptr;
    delete this->firebaseApp;
    this->firebaseApp = nullptr;
    Super::Deinitialize();
}

// 인증
void UAuthManager::StartAuth(const FString& email, const FString& password)
{
    this->naverFlow = false;
    LogAuth(this, TEXT("StartAuth"), EMyLogType::LOG);

    if (this->authInProgress)
    {
        FailAuth(TEXT("Authentication is already in progress"));
        return;
    }

    if (!this->auth && !InitializeFirebaseApp())
    {
        FailAuth(TEXT("Firebase initialization failed"));
        return;
    }

    this->signInFuture = this->auth->SignInWithEmailAndPassword(TCHAR_TO_UTF8(*email), TCHAR_TO_UTF8(*password));

    this->authInProgress = true;
    this->operationStartSeconds = FPlatformTime::Seconds();
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().SetTimer(this->loginCheckHandle, this, &UAuthManager::WaitForSignIn, 0.05f, true);
    }
    else
    {
        FailAuth(TEXT("World context is unavailable"));
    }

}

bool UAuthManager::InitializeFirebaseApp()
{
    if (this->firebaseApp && this->auth)
    {
        return true;
    }

    bool isInitSucess = false;

    FString apiKey;
    FString projectId;
    FString appId;
    GConfig->GetString(TEXT("RPG.Firebase"), TEXT("ApiKey"), apiKey, GGameIni);
    GConfig->GetString(TEXT("RPG.Firebase"), TEXT("ProjectId"), projectId, GGameIni);
    GConfig->GetString(TEXT("RPG.Firebase"), TEXT("AppId"), appId, GGameIni);
    if (apiKey.IsEmpty() || projectId.IsEmpty() || appId.IsEmpty())
    {
        // This public Firebase client configuration is already staged as UFS.
        FString text;
        TSharedPtr<FJsonObject> root;
        if (FFileHelper::LoadFileToString(
                text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("google/google-services.json"))) &&
            text.Len() <= 65536 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(text), root) &&
            root.IsValid())
        {
            const TSharedPtr<FJsonObject>* project = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* clients = nullptr;
            if (root->TryGetObjectField(TEXT("project_info"), project) &&
                root->TryGetArrayField(TEXT("client"), clients))
            {
                (*project)->TryGetStringField(TEXT("project_id"), projectId);
                for (const auto& value : *clients)
                {
                    const auto CLIENT = value->AsObject();
                    if (!CLIENT.IsValid())
                    {
                        continue;
                    }

                    const TSharedPtr<FJsonObject>* info = nullptr;
                    const TSharedPtr<FJsonObject>* android = nullptr;
                    const TArray<TSharedPtr<FJsonValue>>* keys = nullptr;
                    FString package;
                    if (CLIENT->TryGetObjectField(TEXT("client_info"), info) &&
                        (*info)->TryGetObjectField(TEXT("android_client_info"), android) &&
                        (*android)->TryGetStringField(TEXT("package_name"), package) &&
                        package == TEXT("com.hsh.myrpg") && CLIENT->TryGetArrayField(TEXT("api_key"), keys) &&
                        !keys->IsEmpty())
                    {
                        (*info)->TryGetStringField(TEXT("mobilesdk_app_id"), appId);
                        const auto KEY = (*keys)[0]->AsObject();
                        if (KEY.IsValid())
                        {
                            KEY->TryGetStringField(TEXT("current_key"), apiKey);
                        }

                        break;
                    }

                }

            }

        }

    }

    if (apiKey.IsEmpty() || projectId.IsEmpty() || appId.IsEmpty())
    {
        LogAuth(this, TEXT("Firebase configuration is incomplete"), EMyLogType::ERROR_VALUE);
        return false;
    }

    firebase::AppOptions options;
    options.set_api_key(TCHAR_TO_UTF8(*apiKey));
    options.set_project_id(TCHAR_TO_UTF8(*projectId));
    options.set_app_id(TCHAR_TO_UTF8(*appId));

#if PLATFORM_ANDROID
    JNIEnv* my_jni_env = AndroidJavaEnv::GetJavaEnv();
    jobject my_activity = AndroidJavaEnv::GetGameActivityThis();
    this->firebaseApp = firebase::App::Create(options, my_jni_env, my_activity);
#else
    this->firebaseApp = firebase::App::Create(options);
#endif
    if (this->firebaseApp == nullptr)
    {
        LogAuth(this, TEXT("Firebase App Create Fail"), EMyLogType::ERROR_VALUE);
    }
    else
    {
        // Firebase Auth 객체 가져오기
        this->auth = firebase::auth::Auth::GetAuth(this->firebaseApp);
        if (!this->auth)
        {
            delete this->firebaseApp;
            this->firebaseApp = nullptr;
            LogAuth(this, TEXT("Firebase Auth Instance Is Null"), EMyLogType::ERROR_VALUE);
        }
        else
        {
            isInitSucess = true;
        }

    }

    return isInitSucess;
}

void UAuthManager::WaitForSignIn()
{
    if (this->shuttingDown || !this->authInProgress)
    {
        return;
    }

    if (FPlatformTime::Seconds() - this->operationStartSeconds > 10.0)
    {
        FailAuth(TEXT("Sign-in timed out"));
        return;
    }

    if (this->signInFuture.status() != firebase::kFutureStatusComplete)
    {
        return;
    }

    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->loginCheckHandle);
    }

    HandleSignInResult();
}

void UAuthManager::HandleSignInResult()
{
    int signInErrorNum = this->signInFuture.error();

    if (signInErrorNum != 0)
    {
        FString errorMsg = FString::Printf(TEXT("signInError: %d"), signInErrorNum);
        FailAuth(errorMsg);
        return;
    }

    const firebase::auth::AuthResult* authResult = this->signInFuture.result();
    if (!authResult)
    {
        FailAuth(TEXT("Firebase returned no authentication result"));
        return;
    }

    const firebase::auth::User* user = &authResult->user;
    if (this->naverFlow && UTF8_TO_TCHAR(user->uid().c_str()) != this->expectedNaverUid)
    {
        FailAuth(TEXT("Firebase 계정이 네이버 인증 결과와 일치하지 않습니다."));
        return;
    }

    LogAuth(this, TEXT("Auth Success"), EMyLogType::LOG);

    this->tokenFuture = const_cast<firebase::auth::User*>(user)->GetToken(this->naverFlow);

    this->operationStartSeconds = FPlatformTime::Seconds();
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().SetTimer(this->tokenCheckHandle, this, &UAuthManager::WaitForToken, 0.05f, true);
    }
    else
    {
        FailAuth(TEXT("World context is unavailable"));
    }

}

void UAuthManager::WaitForToken()
{
    if (this->shuttingDown || !this->authInProgress)
    {
        return;
    }

    if (FPlatformTime::Seconds() - this->operationStartSeconds > 10.0)
    {
        FailAuth(TEXT("Token request timed out"));
        return;
    }

    if (this->tokenFuture.status() != firebase::kFutureStatusComplete)
    {
        return;
    }

    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->tokenCheckHandle);
    }

    HandleTokenResult();
}

void UAuthManager::HandleTokenResult()
{
    int tokenErrorNum = this->tokenFuture.error();

    if (tokenErrorNum != 0)
    {
        FString errorMsg = FString::Printf(TEXT("tokenError: %d"), tokenErrorNum);
        FailAuth(errorMsg);
        return;
    }

    const std::string* tokenResult = this->tokenFuture.result();
    if (!tokenResult)
    {
        FailAuth(TEXT("Firebase returned no token"));
        return;
    }

    std::string idToken = *tokenResult;
    FString tokenString = FString(idToken.c_str());
    LogAuth(this, TEXT("ID Token acquired successfully"), EMyLogType::LOG);
    this->authInProgress = false;
    this->onAuthTokenReady.Broadcast(tokenString);
}

void UAuthManager::FailAuth(const FString& reason)
{
    CancelAuth(this->naverFlow);
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->loginCheckHandle);
        world->GetTimerManager().ClearTimer(this->tokenCheckHandle);
    }

    this->authInProgress = false;
    LogAuth(this, reason, EMyLogType::ERROR_VALUE);
    this->onAuthFailed.Broadcast(reason);
}

namespace
{
bool IsHandoffKey(const FString& value)
{
    if (value.Len() != 43)
    {
        return false;
    }

    for (const TCHAR C : value)
    {
        if (!FChar::IsAlnum(C) && C != '_' && C != '-')
        {
            return false;
        }

    }

    return true;
}

}

void UAuthManager::CancelAuth(bool signOut)
{
    ++this->authGeneration;
    if (this->activeHttp)
    {
        this->activeHttp->OnProcessRequestComplete().Unbind();
        this->activeHttp->CancelRequest();
        this->activeHttp.Reset();
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(this->naverPollTimer);
        GetWorld()->GetTimerManager().ClearTimer(this->loginCheckHandle);
        GetWorld()->GetTimerManager().ClearTimer(this->tokenCheckHandle);
    }

    if (IsHandoffKey(this->naverRequestId) && IsHandoffKey(this->exchangeSecret) && !this->authBaseUrl.IsEmpty())
    {
        const auto REQUEST = FHttpModule::Get().CreateRequest();
        REQUEST->SetURL(this->authBaseUrl + TEXT("/auth/naver/cancel"));
        REQUEST->SetVerb(TEXT("POST"));
        REQUEST->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        REQUEST->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + this->exchangeSecret);
        REQUEST->SetContentAsString(TEXT("{\"requestId\":\"") + this->naverRequestId + TEXT("\"}"));
        REQUEST->SetTimeout(5);
        REQUEST->ProcessRequest();
    }

    this->naverRequestId.Empty();
    this->exchangeSecret.Empty();
    this->expectedNaverUid.Empty();
    this->authInProgress = false;
    if (signOut)
    {
        this->validatedNaverUid.Empty();
        if (this->auth)
        {
            this->auth->SignOut();
        }

    }

}

bool UAuthManager::MarkNaverAuthenticated(const FString& uid)
{
    if (this->auth && RpgProtocol::IsNaverFirebaseUid(uid) && uid == this->expectedNaverUid &&
        FString(UTF8_TO_TCHAR(this->auth->current_user().uid().c_str())) == uid)
    {
        this->validatedNaverUid = uid;
        return true;
    }

    return false;
}

bool UAuthManager::StartNaverLogin()
{
    if (this->shuttingDown || this->authInProgress || this->signInFuture.status() == firebase::kFutureStatusPending ||
        this->tokenFuture.status() == firebase::kFutureStatusPending)
    {
        return false;
    }

    CancelAuth(true);
    this->naverFlow = true;
    if (!InitializeFirebaseApp())
    {
        FailAuth(TEXT("Firebase 설정을 확인하세요."));
        return false;
    }

    this->authBaseUrl = TEXT("http://localhost:3000");
    GConfig->GetString(TEXT("RPG.Auth"), TEXT("WebBaseUrl"), this->authBaseUrl, GGameIni);
    this->authBaseUrl.RemoveFromEnd(TEXT("/"));
    // Native configuration only; allow cleartext strictly on the local machine.
    if (!this->authBaseUrl.StartsWith(TEXT("https://")) && this->authBaseUrl != TEXT("http://localhost:3000") &&
        this->authBaseUrl != TEXT("http://127.0.0.1:3000"))
    {
        FailAuth(TEXT("인증 서버 주소는 HTTPS 또는 로컬 개발 주소여야 합니다."));
        return false;
    }

    if (!GetWorld())
    {
        FailAuth(TEXT("인증 화면이 준비되지 않았습니다."));
        return false;
    }

    this->authInProgress = true;
    this->naverDeadline = FPlatformTime::Seconds() + 300;
    this->onAuthStatus.Broadcast(TEXT("네이버 로그인 준비 중..."));
    RequestNaver(TEXT("start"),
                 [this](const TSharedPtr<FJsonObject>& json)
                 {
                     FString loginUrl;
                     if (!json->TryGetStringField(TEXT("requestId"), this->naverRequestId) ||
                         !IsHandoffKey(this->naverRequestId) ||
                         !json->TryGetStringField(TEXT("exchangeSecret"), this->exchangeSecret) ||
                         !IsHandoffKey(this->exchangeSecret) || !json->TryGetStringField(TEXT("loginUrl"), loginUrl) ||
                         loginUrl != this->authBaseUrl + TEXT("/auth/naver/login?requestId=") + this->naverRequestId)
                     {
                         FailAuth(TEXT("인증 서버 응답을 확인할 수 없습니다."));
                         return;
                     }

                     FString error;
                     FPlatformProcess::LaunchURL(*loginUrl, nullptr, &error);
                     if (!error.IsEmpty())
                     {
                         FailAuth(TEXT("브라우저를 열지 못했습니다."));
                         return;
                     }

                     this->onAuthStatus.Broadcast(TEXT("브라우저에서 네이버 로그인을 완료하세요."));
                     GetWorld()->GetTimerManager().SetTimer(this->naverPollTimer, this, &UAuthManager::PollNaver, 1.5f,
                                                            false);
                 });

    return true;
}

void UAuthManager::RequestNaver(const FString& route, TFunction<void(const TSharedPtr<FJsonObject>&)> complete)
{
    const uint64 GENERATION = this->authGeneration;
    const auto REQUEST = FHttpModule::Get().CreateRequest();
    this->activeHttp = REQUEST;
    REQUEST->SetURL(this->authBaseUrl + TEXT("/auth/naver/") + route);
    REQUEST->SetVerb(TEXT("POST"));
    REQUEST->SetTimeout(10);
    REQUEST->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    REQUEST->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    TSharedRef<FJsonObject> body = MakeShared<FJsonObject>();
    if (!this->naverRequestId.IsEmpty())
    {
        body->SetStringField(TEXT("requestId"), this->naverRequestId);
    }

    if (!this->exchangeSecret.IsEmpty())
    {
        REQUEST->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + this->exchangeSecret);
    }

    FString content;
    const auto WRITER = TJsonWriterFactory<>::Create(&content);
    FJsonSerializer::Serialize(body, WRITER);
    REQUEST->SetContentAsString(content);
    const TWeakObjectPtr<UAuthManager> WEAK(this);
    REQUEST->OnProcessRequestComplete().BindLambda(
        [WEAK, GENERATION, complete = MoveTemp(complete)](FHttpRequestPtr, FHttpResponsePtr response, bool success)
        {
            if (!WEAK.IsValid() || WEAK->shuttingDown || GENERATION != WEAK->authGeneration || !WEAK->authInProgress)
            {
                return;
            }

            WEAK->activeHttp.Reset();
            TSharedPtr<FJsonObject> json;
            if (!success || !response.IsValid() ||
                (response->GetResponseCode() != 200 && response->GetResponseCode() != 201) ||
                response->GetContent().Num() > 32768 ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(response->GetContentAsString()), json) ||
                !json.IsValid())
            {
                WEAK->FailAuth(TEXT("인증 서버 연결에 실패했습니다. 다시 시도하세요."));
                return;
            }

            complete(json);
        });

    if (!REQUEST->ProcessRequest())
    {
        FailAuth(TEXT("인증 요청을 시작하지 못했습니다."));
    }

}

void UAuthManager::PollNaver()
{
    if (!this->authInProgress || this->shuttingDown)
    {
        return;
    }

    if (FPlatformTime::Seconds() >= this->naverDeadline)
    {
        FailAuth(TEXT("네이버 로그인 시간이 초과되었습니다."));
        return;
    }

    RequestNaver(TEXT("status"),
                 [this](const TSharedPtr<FJsonObject>& json)
                 {
                     FString status;
                     if (!json->TryGetStringField(TEXT("status"), status))
                     {
                         FailAuth(TEXT("인증 응답이 올바르지 않습니다."));
                         return;
                     }

                     if (status == TEXT("ready"))
                     {
                         ExchangeNaver();
                     }
                     else if (status == TEXT("pending"))
                     {
                         GetWorld()->GetTimerManager().SetTimer(this->naverPollTimer, this, &UAuthManager::PollNaver,
                                                                1.5f, false);
                     }
                     else
                     {
                         FailAuth(TEXT("네이버 로그인이 취소되었거나 만료되었습니다."));
                     }

                 });

}

void UAuthManager::ExchangeNaver()
{
    this->onAuthStatus.Broadcast(TEXT("Firebase 인증 중..."));
    RequestNaver(TEXT("exchange"),
                 [this](const TSharedPtr<FJsonObject>& json)
                 {
                     FString token;
                     if (!json->TryGetStringField(TEXT("uid"), this->expectedNaverUid) ||
                         !RpgProtocol::IsNaverFirebaseUid(this->expectedNaverUid) ||
                         !json->TryGetStringField(TEXT("customToken"), token) || token.IsEmpty() || token.Len() > 16384)
                     {
                         FailAuth(TEXT("Firebase 인증 응답이 올바르지 않습니다."));
                         return;
                     }

                     this->naverRequestId.Empty();
                     this->exchangeSecret.Empty();
                     this->signInFuture = this->auth->SignInWithCustomToken(TCHAR_TO_UTF8(*token));
                     this->operationStartSeconds = FPlatformTime::Seconds();
                     GetWorld()->GetTimerManager().SetTimer(this->loginCheckHandle, this, &UAuthManager::WaitForSignIn,
                                                            .05f, true);
                 });

}

bool UAuthManager::RefreshNaverToken()
{
    if (!this->auth || !HasNaverSession() || this->authInProgress || this->shuttingDown ||
        this->signInFuture.status() == firebase::kFutureStatusPending ||
        this->tokenFuture.status() == firebase::kFutureStatusPending)
    {
        return false;
    }

    if (!this->auth->current_user().is_valid() ||
        FString(UTF8_TO_TCHAR(this->auth->current_user().uid().c_str())) != this->validatedNaverUid)
    {
        return false;
    }

    ++this->authGeneration;
    this->naverFlow = true;
    this->authInProgress = true;
    this->expectedNaverUid = this->validatedNaverUid;
    this->tokenFuture = this->auth->current_user().GetToken(true);
    this->operationStartSeconds = FPlatformTime::Seconds();
    if (!GetWorld())
    {
        FailAuth(TEXT("인증 화면이 준비되지 않았습니다."));
        return false;
    }

    this->onAuthStatus.Broadcast(TEXT("계정 인증 갱신 중..."));
    GetWorld()->GetTimerManager().SetTimer(this->tokenCheckHandle, this, &UAuthManager::WaitForToken, .05f, true);
    return true;
}

