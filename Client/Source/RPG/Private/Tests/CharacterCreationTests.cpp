#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Characters/CharacterAppearanceDefinition.h"
#include "Characters/CharacterPreviewActor.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Managers/CharacterManager.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationCodecTest, "RPG.Characters.CreationCodec",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterCreationCodecTest::RunTest(const FString&)
{
    RpgProtocol::FCreateCharacterRequestPacket request;
    request.requestId = 0x01020304;
    request.operationId = TEXT("0123456789abcdef0123456789abcdef");
    request.characterName = TEXT("Hero");
    request.gender = 1;
    TArray<uint8> bytes;
    TestTrue(TEXT("Serialize request"), request.Serialize(bytes));
    TestEqual(TEXT("Golden byte count"), bytes.Num(), 50);
    if (bytes.Num() == 50)
    {
        TestEqual(TEXT("Big-endian request ID"), bytes[0], uint8(1));
        TestEqual(TEXT("Operation length"), bytes[7], uint8(32));
        TestEqual(TEXT("Name length"), bytes[43], uint8(4));
        TestEqual(TEXT("Race"), bytes[48], uint8(0));
        TestEqual(TEXT("Gender"), bytes[49], uint8(1));
    }

    request.operationId = TEXT("invalid");
    TestFalse(TEXT("Invalid operation rejected"), request.Serialize(bytes));

    RpgProtocol::FBinaryWriter writer;
    writer.WriteUInt8(0);
    writer.WriteUInt32(7);
    writer.WriteString(TEXT("0123456789abcdef0123456789abcdef"));
    writer.WriteUInt16(1);
    writer.WriteUInt64(123);
    writer.WriteString(TEXT("Hero"));
    writer.WriteUInt8(0);
    writer.WriteUInt8(1);
    writer.WriteUInt32(1);
    writer.WriteUInt8(0);
    bytes = writer.Data();
    RpgProtocol::FCreateCharacterResponsePacket response;
    TestTrue(TEXT("Read server success"), response.Deserialize(bytes.GetData(), bytes.Num()));
    for (int32 size = 0; size < bytes.Num(); ++size)
    {
        TestFalse(TEXT("Truncation rejected"), response.Deserialize(bytes.GetData(), size));
    }

    bytes[0] = 255;
    TestFalse(TEXT("Unknown result rejected"), response.Deserialize(bytes.GetData(), bytes.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationStateTest, "RPG.Characters.CreationState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterCreationStateTest::RunTest(const FString&)
{
    UGameInstance* game = NewObject<UGameInstance>();
    UCharacterManager* manager = NewObject<UCharacterManager>(game);
    const FString UID = TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    manager->OnAuthenticated(UID);
    manager->pendingSave = NewObject<UCharacterPendingSave>(manager);
    manager->pendingSave->accountUid = UID;
    manager->pendingSave->operationId = TEXT("0123456789abcdef0123456789abcdef");
    manager->pendingSave->options.characterName = TEXT("Hero");
    const FString SLOT = manager->SaveSlot();
    if (!TestTrue(TEXT("Persist pending operation"), UGameplayStatics::SaveGameToSlot(manager->pendingSave, SLOT, 0)))
    {
        return false;
    }

    manager->pendingCreateId = 7;
    manager->createGeneration = manager->connectionGeneration;
    RpgProtocol::FCreateCharacterResponsePacket response;
    response.requestId = 8;
    response.operationId = manager->pendingSave->operationId;
    response.result = RpgProtocol::ECreateCharacterResult::UNKNOWN_OUTCOME;
    manager->HandleCreation(response);
    TestEqual(TEXT("Stale response ignored"), manager->pendingCreateId, uint32(7));
    response.requestId = 7;
    manager->HandleCreation(response);
    TestEqual(TEXT("Uncertain state"), manager->GetCreationState(), ECharacterCreationState::NEEDS_CONFIRMATION);
    TestTrue(TEXT("Uncertain operation retained"), manager->pendingSave != nullptr);

    // Simulate process restart: load the account's saved operation into a new manager.
    UCharacterManager* restored = NewObject<UCharacterManager>(game);
    restored->OnAuthenticated(UID);
    TestEqual(TEXT("Restore same operation"), restored->GetPendingOperationId(), response.operationId);
    restored->pendingCreateId = 9;
    restored->createGeneration = restored->connectionGeneration;
    response.requestId = 9;
    response.result = RpgProtocol::ECreateCharacterResult::SUCCESS;
    RpgProtocol::FCharacterSummaryPacket character;
    character.characterId = 123;
    character.characterName = TEXT("Hero");
    character.level = 1;
    response.characters.Add(character);
    restored->HandleCreation(response);
    TestEqual(TEXT("Success updates cache"), restored->characters.Num(), 1);
    TestEqual(TEXT("Success completes"), restored->GetCreationState(), ECharacterCreationState::COMPLETED);
    TestFalse(TEXT("Completed operation removed"), UGameplayStatics::DoesSaveGameExist(SLOT, 0));
    restored->HandleCreation(response);
    TestEqual(TEXT("Duplicate response ignored"), restored->characters.Num(), 1);

    restored->pendingListId = 10;
    restored->listGeneration = restored->connectionGeneration;
    restored->listMutationVersion = 0; // Snapshot issued before successful creation.
    RpgProtocol::FCharacterListResponsePacket list;
    list.requestId = 10;
    list.result = RpgProtocol::ECharacterListResult::SUCCESS;
    restored->HandleList(list);
    TestEqual(TEXT("Old list cannot erase creation"), restored->characters.Num(), 1);
    restored->OnDisconnected();
    TestEqual(TEXT("Disconnect clears account cache"), restored->characters.Num(), 0);
    TestFalse(TEXT("Disconnect invalidates list"), restored->HasCharacterList());
    UGameplayStatics::DeleteGameInSlot(SLOT, 0);
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterPreviewAssetsTest, "RPG.Characters.PreviewAssets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterPreviewAssetsTest::RunTest(const FString&)
{
    const TCHAR* definitionPaths[] = {TEXT("/Game/RPG/Characters/Human/Data/DA_Human_Male.DA_Human_Male"),
                                      TEXT("/Game/RPG/Characters/Human/Data/DA_Human_Female.DA_Human_Female")};

    TArray<UCharacterAppearanceDefinition*> definitions;
    for (int32 gender = 0; gender < UE_ARRAY_COUNT(definitionPaths); ++gender)
    {
        UCharacterAppearanceDefinition* definition =
            LoadObject<UCharacterAppearanceDefinition>(nullptr, definitionPaths[gender]);
        if (!TestNotNull(TEXT("Appearance definition"), definition))
        {
            continue;
        }

        definitions.Add(definition);

        TestEqual(TEXT("Human race contract"), definition->race, uint8(0));
        TestEqual(TEXT("Gender contract"), definition->gender, uint8(gender));

        USkeletalMesh* mainMesh = definition->previewMesh.LoadSynchronous();
        UAnimSequence* idle = definition->previewIdle.LoadSynchronous();
        if (!TestNotNull(TEXT("Main preview mesh"), mainMesh) || !TestNotNull(TEXT("Preview idle"), idle))
        {
            continue;
        }

        USkeleton* mainSkeleton = mainMesh->GetSkeleton();
        USkeleton* idleSkeleton = idle->GetSkeleton();
        TestNotNull(TEXT("Main mesh skeleton"), mainSkeleton);
        TestNotNull(TEXT("Idle skeleton"), idleSkeleton);
        if (mainSkeleton && idleSkeleton)
        {
            TestTrue(TEXT("Idle is compatible with preview skeleton"),
                     mainSkeleton->IsCompatibleForEditor(idleSkeleton));
        }

        TestEqual(TEXT("Eyes, eyebrows, and default hair are configured"), definition->additionalMeshes.Num(), 3);
        for (const TSoftObjectPtr<USkeletalMesh>& partReference : definition->additionalMeshes)
        {
            USkeletalMesh* part = partReference.LoadSynchronous();
            if (TestNotNull(TEXT("Additional preview mesh"), part))
            {
                USkeleton* partSkeleton = part->GetSkeleton();
                TestNotNull(TEXT("Additional mesh skeleton"), partSkeleton);
                if (mainSkeleton && partSkeleton)
                {
                    TestTrue(TEXT("Additional mesh skeleton is compatible"),
                             partSkeleton == mainSkeleton || mainSkeleton->IsCompatibleForEditor(partSkeleton));
                }

            }

        }

        const FString EXPECTED_HAIR = gender == 0 ? TEXT("Hair_SimpleParted") : TEXT("Hair_Buns");
        if (definition->additionalMeshes.Num() == 3)
        {
            TestTrue(TEXT("Default hair selection"),
                     definition->additionalMeshes[2].ToSoftObjectPath().ToString().Contains(EXPECTED_HAIR));
        }

    }

    if (definitions.Num() == UE_ARRAY_COUNT(definitionPaths))
    {
        UGameInstance* game = NewObject<UGameInstance>(GEngine);
        game->AddToRoot();
        game->InitializeStandalone();
        UWorld* world = game->GetWorld();
        ACharacterPreviewActor* preview = world ? world->SpawnActor<ACharacterPreviewActor>() : nullptr;
        if (TestNotNull(TEXT("Preview actor can spawn"), preview))
        {
            for (UCharacterAppearanceDefinition* definition : definitions)
            {
                preview->appearances.Add(definition);
            }

            preview->DispatchBeginPlay();
            TestTrue(TEXT("Male preview request accepted"), preview->ShowAppearance(0, 0));
            FlushAsyncLoading();
            world->Tick(LEVELTICK_All, 0.0f);
            TestEqual(TEXT("Male preview becomes ready"), preview->GetPreviewState(),
                      ECharacterPreviewLoadState::READY);
            TestTrue(TEXT("Both gender variants are held in the appearance cache"),
                     preview->GetCachedAppearanceAssetCount() >= 9);
            TestEqual(TEXT("Both display variants are prepared"), preview->GetPreparedVariantCount(), 2);
            const USkeletalMeshComponent* maleBody = preview->GetVariantBody(0, 0);
            const USkeletalMeshComponent* femaleBody = preview->GetVariantBody(0, 1);
            TestTrue(TEXT("Each gender owns a different body"), maleBody && femaleBody && maleBody != femaleBody);

            TestTrue(TEXT("Female preview request accepted"), preview->ShowAppearance(0, 1));
            FlushAsyncLoading();
            world->Tick(LEVELTICK_All, 0.0f);
            TestEqual(TEXT("Female preview becomes ready"), preview->GetPreviewState(),
                      ECharacterPreviewLoadState::READY);
            TestEqual(TEXT("Latest gender owns preview"), preview->GetDisplayedGender(), uint8(1));
            TestTrue(TEXT("Male component survives a gender switch"), maleBody == preview->GetVariantBody(0, 0));
            TestTrue(TEXT("Female component survives a gender switch"), femaleBody == preview->GetVariantBody(0, 1));

            preview->HidePreview();
            TestEqual(TEXT("Preview hides cleanly"), preview->GetPreviewState(), ECharacterPreviewLoadState::HIDDEN);
            TestEqual(TEXT("Hiding retains both prepared variants"), preview->GetPreparedVariantCount(), 2);
            TestTrue(TEXT("Reopening male needs no gender click"), preview->ShowAppearance(0, 0));
            TestEqual(TEXT("Reopening is immediately ready"), preview->GetPreviewState(),
                      ECharacterPreviewLoadState::READY);
        }

        game->Shutdown();
        if (world)
        {
            GEngine->DestroyWorldContext(world);
            world->DestroyWorld(false);
        }

        game->RemoveFromRoot();
    }

    return true;
}

#endif
#endif
