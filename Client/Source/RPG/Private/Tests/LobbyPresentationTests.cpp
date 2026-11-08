#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "UIs/Title/TitleUIController.h"
#include "UIs/RpgUITheme.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyPresentationTest, "RPG.Lobby.Presentation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLobbyPresentationTest::RunTest(const FString&)
{
    TestNotNull(TEXT("Imported fantasy font"), RpgUI::Font(16).FontObject.Get());
    UClass* rootClass =
        LoadClass<UTitleUIController>(nullptr, TEXT("/Game/RPG/UI/Lobby/WBP_LobbyRoot.WBP_LobbyRoot_C"));
    if (!TestNotNull(TEXT("Lobby root asset"), rootClass))
    {
        return false;
    }

    UGameInstance* game = NewObject<UGameInstance>(GEngine);
    game->AddToRoot();
    game->InitializeStandalone();
    UWorld* world = game->GetWorld();
    UCharacterManager* manager = game->GetSubsystem<UCharacterManager>();
    UTitleUIController* lobby = CreateWidget<UTitleUIController>(game, rootClass);
    if (!TestNotNull(TEXT("Lobby instance"), lobby) || !TestNotNull(TEXT("Character manager"), manager))
    {
        game->Shutdown();
        game->RemoveFromRoot();
        return false;
    }

    lobby->AddToRoot();
    lobby->TakeWidget();
    // The isolated test world is not playing; drive viewport construction explicitly.
    lobby->NativeConstruct();
    lobby->NativeConstruct(); // Registration must remain idempotent on repeated entry.
    TestNotNull(TEXT("Login screen binding"), lobby->loginScreen.Get());
    TestNotNull(TEXT("Full-screen start button"), lobby->loginScreen->startButton.Get());
    TestNotNull(TEXT("Native Naver login button"), lobby->loginScreen->naverLoginButton.Get());
    TestNotNull(TEXT("Native login cancellation"), lobby->loginScreen->cancelAuthButton.Get());
    TestNotNull(TEXT("Native logout button"), lobby->characterSelectScreen->logoutButton.Get());
    TestNull(TEXT("Legacy connect button removed"), lobby->loginScreen->GetWidgetFromName(TEXT("ConnectButton")));
    TestNull(TEXT("Legacy guest button removed"), lobby->loginScreen->GetWidgetFromName(TEXT("GuestLoginButton")));
    TestNotNull(TEXT("Selection screen binding"), lobby->characterSelectScreen.Get());
    TestNotNull(TEXT("Creation screen binding"), lobby->characterCreateScreen.Get());
    if (lobby->characterSelectScreen && lobby->characterCreateScreen)
    {
        TestNull(TEXT("Separate bottom entry removed"),
                 lobby->characterSelectScreen->GetWidgetFromName(TEXT("EnterGameButton")));
        manager->authenticated = true;
        manager->listLoaded = true;
        manager->listState = ECharacterListState::READY;
        for (int32 count : {0, 1, 4, 8})
        {
            manager->characters.Reset();
            for (int32 index = 0; index < count; ++index)
            {
                FCharacterSummary item;
                item.characterUnique = 100 + index;
                // Rare Hangul syllables exercise the serialized fallback face in the first visible card after reorder.
                item.name = index == 3 ? TEXT("영웅뷁쀍") : FString::Printf(TEXT("TestHero%d"), index);
                item.level = 1;
                manager->characters.Add(item);
            }

            lobby->RebuildCharacterList();
            TestEqual(TEXT("All characters represented"), lobby->characterSlots.Num(), count);
            TestEqual(TEXT("UMG entry count"), lobby->characterSelectScreen->characterList->GetChildrenCount(), count);
            TestEqual(TEXT("Creation limit button"), lobby->characterSelectScreen->createButton->GetIsEnabled(),
                      count < 8);
        }

        lobby->SelectCharacter(103);
        Swap(manager->characters[0], manager->characters[3]);
        lobby->RebuildCharacterList();
        TestEqual(TEXT("Selection survives reorder by ID"), lobby->selectedCharacterId, int64(103));
        lobby->characterSlots[0]->onSelected.Broadcast(100);
        TestEqual(TEXT("Slot event selects by ID"), lobby->selectedCharacterId, int64(100));
        for (UHeroSlot* entry : lobby->characterSlots)
        {
            TestNotNull(TEXT("Per-character entry button"), entry->enterGameButton.Get());
            TestTrue(TEXT("Ready list allows entry"), entry->enterGameButton->GetIsEnabled());
        }

        TestEqual(TEXT("Normal list hides refresh"), lobby->characterSelectScreen->refreshButton->GetVisibility(),
                  ESlateVisibility::Collapsed);
        TestEqual(TEXT("No selected caption"), lobby->characterSelectScreen->selectedCharacterText->GetVisibility(),
                  ESlateVisibility::Collapsed);
        manager->SetListState(ECharacterListState::LOADING);
        for (UHeroSlot* entry : lobby->characterSlots)
        {
            TestFalse(TEXT("Loading list blocks entry"), entry->enterGameButton->GetIsEnabled());
        }

        manager->SetListState(ECharacterListState::READY);
        lobby->SetGuestLoginEnabled(false);
        TestFalse(TEXT("Pending login blocks repeat click"), lobby->loginScreen->startButton->GetIsEnabled());
        TestFalse(TEXT("Pending login blocks Naver repeat click"),
                  lobby->loginScreen->naverLoginButton->GetIsEnabled());
        TestEqual(TEXT("Pending login permits explicit cancel"), lobby->loginScreen->cancelAuthButton->GetVisibility(),
                  ESlateVisibility::Visible);
        lobby->SetGuestLoginEnabled(true);
        TestTrue(TEXT("Failed login allows retry"), lobby->loginScreen->startButton->GetIsEnabled());
        TestTrue(TEXT("Failed login allows Naver retry"), lobby->loginScreen->naverLoginButton->GetIsEnabled());

        if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
        {
            // Render actual UMG assets, without contacting any server or modifying accounts.
            const FString FOLDER = FPaths::ProjectSavedDir() / TEXT("LobbyVerification");
            IFileManager::Get().MakeDirectory(*FOLDER, true);
            FAssetCompilingManager::Get().FinishAllCompilation();
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }

            FlushRenderingCommands();
            FWidgetRenderer renderer(false);
            const TSharedRef<SWidget> SLATE_WIDGET = lobby->TakeWidget();
            const auto CAPTURE = [&](UUserWidget* screen, const TCHAR* name)
            {
                lobby->SetScreen(screen);
                UTextureRenderTarget2D* target =
                    FWidgetRenderer::CreateTargetFor(FVector2D(1280, 720), TF_Bilinear, false);
                // Two passes settle wrapping/layout after switching a previously inactive screen.
                renderer.DrawWidget(target, SLATE_WIDGET, FVector2D(1280, 720), 0.0f);
                renderer.DrawWidget(target, SLATE_WIDGET, FVector2D(1280, 720), 0.0f);
                FlushRenderingCommands();
                FBufferArchive bytes;
                if (TestNotNull(TEXT("Rendered screen"), target) &&
                    TestTrue(TEXT("Encode screen"), FImageUtils::ExportRenderTarget2DAsPNG(target, bytes)))
                {
                    TestTrue(TEXT("Save screen"), FFileHelper::SaveArrayToFile(bytes, *(FOLDER / name)));
                }

            };

            CAPTURE(lobby->loginScreen, TEXT("Login.png"));
            CAPTURE(lobby->characterSelectScreen, TEXT("CharacterSelect.png"));
            for (const auto& row : lobby->characterSlots)
            {
                if (row->selectionGlow && row->selectionGlow->IsVisible())
                {
                    TestTrue(TEXT("Selected glow covers full character row"),
                             row->selectionGlow->GetCachedGeometry().GetLocalSize().Equals(
                                 row->selectionOutline->GetCachedGeometry().GetLocalSize(), .5f));
                }

            }

            if (!lobby->characterSlots.IsEmpty())
            {
                const UHeroSlot* entry = lobby->characterSlots[0].Get();
                const auto IMAGE_SIZE = entry->portraitImage->GetCachedGeometry().GetLocalSize();
                const auto ART_SIZE = entry->portraitImage->GetBrush().ImageSize;
                TestTrue(TEXT("Portrait preserves source aspect"),
                         IMAGE_SIZE.Y > 0 && FMath::IsNearlyEqual(double(IMAGE_SIZE.X / IMAGE_SIZE.Y),
                                                                  double(ART_SIZE.X / ART_SIZE.Y), .02));
                const auto& row = entry->GetCachedGeometry();
                double lastX = -1;
                const UWidget* fields[] = {entry->portraitImage.Get(), entry->characterNameText.Get(),
                                           entry->levelText.Get(),     entry->raceText.Get(),
                                           entry->genderText.Get(),    entry->enterGameButton.Get()};
                for (const UWidget* field : fields)
                {
                    if (!TestNotNull(TEXT("Row field exists"), field))
                    {
                        continue;
                    }

                    const auto POS = row.AbsoluteToLocal(field->GetCachedGeometry().GetAbsolutePosition());
                    TestTrue(TEXT("Portrait/name/level/race/gender/entry ordered left to right"), POS.X > lastX);
                    lastX = POS.X;
                }

            }

            manager->characters.SetNum(1);
            lobby->RebuildCharacterList();
            CAPTURE(lobby->characterSelectScreen, TEXT("CharacterSelectSingle.png"));
            CAPTURE(lobby->characterCreateScreen, TEXT("CharacterCreate.png"));
        }

        // Captures release their temporary Slate host; reconstruct after that host is gone.
        lobby->NativeConstruct();

        manager->pendingSave = NewObject<UCharacterPendingSave>(manager);
        manager->pendingSave->operationId = TEXT("0123456789abcdef0123456789abcdef");
        manager->SetCreationState(ECharacterCreationState::NEEDS_CONFIRMATION);
        TestFalse(TEXT("Uncertain result blocks new creation"),
                  lobby->characterCreateScreen->submitButton->GetIsEnabled());
        TestTrue(TEXT("Uncertain result enables confirmation"),
                 lobby->characterCreateScreen->confirmPendingButton->GetIsEnabled());
        manager->SetCreationState(ECharacterCreationState::IN_FLIGHT);
        TestFalse(TEXT("In-flight confirmation blocked"),
                  lobby->characterCreateScreen->confirmPendingButton->GetIsEnabled());

        manager->SetListState(ECharacterListState::FAILED_VALUE);
        TestTrue(TEXT("Failed list allows retry"), lobby->characterSelectScreen->refreshButton->GetIsEnabled());
        TestEqual(TEXT("Failed list reveals recovery"), lobby->characterSelectScreen->refreshButton->GetVisibility(),
                  ESlateVisibility::Visible);
        manager->OnDisconnected();
        TestEqual(TEXT("Disconnect clears slots"), lobby->characterSlots.Num(), 0);
        TestFalse(TEXT("Disconnect blocks creation"), lobby->characterCreateScreen->submitButton->GetIsEnabled());
        TestTrue(TEXT("Disconnect allows click-to-start"), lobby->loginScreen->startButton->GetIsEnabled());
        TestEqual(TEXT("Disconnect returns to login"), lobby->screenSwitcher->GetActiveWidget(),
                  static_cast<UWidget*>(lobby->loginScreen));
    }

    lobby->NativeDestruct();
    lobby->ReleaseSlateResources(true);
    lobby->RemoveFromRoot();
    game->Shutdown();
    if (world)
    {
        GEngine->DestroyWorldContext(world);
        world->DestroyWorld(false);
    }

    game->RemoveFromRoot();
    return true;
}

#endif
