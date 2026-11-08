#include "Managers/LocalSaveManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
const FString STORE_ID = TEXT("MyRpg");
const FString AUTH_SECTION = TEXT("Authentication");

bool StoreToken(const FString& token)
{
#if PLATFORM_WINDOWS
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(L"MyRpg.Authentication.Token");
    credential.CredentialBlobSize = token.Len() * sizeof(TCHAR);
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<TCHAR*>(*token));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"MyRpg");
    return CredWriteW(&credential, 0) != 0;
#else
    return FPlatformMisc::SetStoredValue(STORE_ID, AUTH_SECTION, TEXT("Token"), token);
#endif
}

bool LoadToken(FString& outToken)
{
#if PLATFORM_WINDOWS
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(L"MyRpg.Authentication.Token", CRED_TYPE_GENERIC, 0, &credential))
    {
        FString legacyToken;
        if (!FPlatformMisc::GetStoredValue(STORE_ID, AUTH_SECTION, TEXT("Token"), legacyToken) ||
            !StoreToken(legacyToken))
        {
            return false;
        }

        FPlatformMisc::DeleteStoredValue(STORE_ID, AUTH_SECTION, TEXT("Token"));
        outToken = MoveTemp(legacyToken);
        return true;
    }

    outToken = FString(static_cast<int32>(credential->CredentialBlobSize / sizeof(TCHAR)),
                       reinterpret_cast<const TCHAR*>(credential->CredentialBlob));
    CredFree(credential);
    return !outToken.IsEmpty();
#else
    return FPlatformMisc::GetStoredValue(STORE_ID, AUTH_SECTION, TEXT("Token"), outToken);
#endif
}

bool DeleteToken()
{
#if PLATFORM_WINDOWS
    const bool REMOVED =
        CredDeleteW(L"MyRpg.Authentication.Token", CRED_TYPE_GENERIC, 0) != 0 || GetLastError() == ERROR_NOT_FOUND;
    FPlatformMisc::DeleteStoredValue(STORE_ID, AUTH_SECTION, TEXT("Token"));
    return REMOVED;
#else
    return FPlatformMisc::DeleteStoredValue(STORE_ID, AUTH_SECTION, TEXT("Token"));
#endif
}

}

bool ULocalSaveManager::SaveUidAndToken(const FString& newUid, const FString& newToken)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgEphemeralGuest")))
    {
        return !newUid.IsEmpty() && !newToken.IsEmpty();
    }

    if (newUid.IsEmpty() || newToken.IsEmpty())
    {
        return false;
    }

    const bool SAVED_UID = FPlatformMisc::SetStoredValue(STORE_ID, AUTH_SECTION, TEXT("Uid"), newUid);
    const bool SAVED_TOKEN = StoreToken(newToken);
    if (!SAVED_UID || !SAVED_TOKEN)
    {
        ClearUidAndToken();
        return false;
    }

    if (UGameplayStatics::DoesSaveGameExist(TEXT("LoginInfo"), 0))
    {
        UGameplayStatics::DeleteGameInSlot(TEXT("LoginInfo"), 0);
    }

    return true;
}

bool ULocalSaveManager::LoadUidAndToken(const FString& slot, FString& loadedUid, FString& loadedToken)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgEphemeralGuest")))
    {
        loadedUid.Empty();
        loadedToken.Empty();
        return false;
    }

    const bool HAS_UID = FPlatformMisc::GetStoredValue(STORE_ID, AUTH_SECTION, TEXT("Uid"), loadedUid);
    const bool HAS_TOKEN = LoadToken(loadedToken);
    if (!HAS_UID)
    {
        loadedUid.Empty();
    }

    if (!HAS_TOKEN)
    {
        loadedToken.Empty();
    }

    if (HAS_UID && HAS_TOKEN)
    {
        return true;
    }

    // One-time migration from the legacy plaintext SaveGame slot.
    if (UGameplayStatics::DoesSaveGameExist(slot, 0))
    {
        if (ULocalSaveManager* legacy = Cast<ULocalSaveManager>(UGameplayStatics::LoadGameFromSlot(slot, 0)))
        {
            loadedUid = legacy->uid;
            loadedToken = legacy->token;
            const bool SAVED = SaveUidAndToken(loadedUid, loadedToken);
            UGameplayStatics::DeleteGameInSlot(slot, 0);
            return SAVED;
        }

        UGameplayStatics::DeleteGameInSlot(slot, 0);
    }

    return false;
}

bool ULocalSaveManager::ClearUidAndToken()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgEphemeralGuest")))
    {
        return true;
    }

    const bool REMOVED_UID = FPlatformMisc::DeleteStoredValue(STORE_ID, AUTH_SECTION, TEXT("Uid"));
    const bool REMOVED_TOKEN = DeleteToken();
    return REMOVED_UID && REMOVED_TOKEN;
}

