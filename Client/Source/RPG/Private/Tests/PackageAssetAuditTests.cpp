#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPackageAssetAuditTest, "RPG.Package.AssetAudit",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FPackageAssetAuditTest::RunTest(const FString&)
{
    FString manifest;
    FString text;
    TSharedPtr<FJsonObject> document;
    if (!FParse::Value(FCommandLine::Get(), TEXT("RpgAuditManifest="), manifest) ||
        !FFileHelper::LoadFileToString(text, *manifest) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(text), document))
    {
        this->AddError(TEXT("Provide a valid -RpgAuditManifest JSON from the package audit inventory"));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* assets = nullptr;
    if (!document->TryGetArrayField(TEXT("assets"), assets) || !assets || assets->IsEmpty())
    {
        this->AddError(TEXT("Package audit manifest contains no assets"));
        return false;
    }

    int32 loaded = 0;
    for (const auto& value : *assets)
    {
        const FString PATH = value->AsString();
        if (this->TestNotNull(*PATH, LoadObject<UObject>(nullptr, *PATH)))
        {
            ++loaded;
        }

        if (loaded % 32 == 0)
        {
            FlushAsyncLoading();
            CollectGarbage(RF_NoFlags);
        }

    }

    auto* sword = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Game/Characters/Dark_Knight/Dark_Knight_Male/Meshes/SM_DKM_Sword.SM_DKM_Sword"));
    if (this->TestNotNull(TEXT("Packaged sword mesh"), sword))
    {
        auto* material = sword->GetMaterial(0);
        if (this->TestNotNull(TEXT("Sword has its migrated material"), material))
        {
            this->TestEqual(TEXT("Sword uses its intended material"), material->GetName(), FString(TEXT("M_DKM_Sword")));
            TArray<UTexture*> textures;
            material->GetUsedTextures(textures);
            for (const TCHAR* name : {TEXT("M_DKM_Sword_ALB"), TEXT("M_DKM_Sword_ARM"), TEXT("M_DKM_Sword_NRM")})
            {
                this->TestTrue(name, textures.ContainsByPredicate(
                                   [&](const auto* texture)
                                   {
                                       return texture && texture->GetName() == name;
                                   }));

            }

        }

    }

    UE_LOG(LogTemp, Display, TEXT("PACKAGE_ASSET_AUDIT loaded=%d requested=%d"), loaded, assets->Num());
    this->TestEqual(TEXT("All audited runtime objects load"), loaded, assets->Num());
    return !this->HasAnyErrors();
}

#endif
