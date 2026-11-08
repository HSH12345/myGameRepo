#include "World/GameSessionSubsystem.h"
#if !UE_BUILD_SHIPPING
#include "Managers/UIManager.h"
#include "Managers/CharacterManager.h"
#include "Managers/NetworkManager.h"
#include "UIs/Title/TitleUIController.h"
#include "Tests/SlatePointerClick.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

void UGameSessionSubsystem::TickLobbySmoke()
{
    const double NOW = FPlatformTime::Seconds();
    if (NOW - this->smokeProgressTime < (this->smokeTerrainStage == 10 ? .15 : 1.5))
    {
        return;
    }

    auto* lobby = GetGameInstance()->GetSubsystem<UUIManager>()->GetUIController();
    if (!lobby)
    {
        return;
    }

    auto* login = Cast<ULobbyLoginScreen>(lobby->GetWidgetFromName(TEXT("LoginScreen")));
    auto* selection = Cast<ULobbyCharacterSelectScreen>(lobby->GetWidgetFromName(TEXT("CharacterSelectScreen")));
    auto* creation = Cast<ULobbyCharacterCreateScreen>(lobby->GetWidgetFromName(TEXT("CharacterCreateScreen")));
    auto* characters = GetGameInstance()->GetSubsystem<UCharacterManager>();
    if (!login || !selection || !creation)
    {
        return;
    }

    const auto CHECK = [&](bool value, const TCHAR* name)
    {
        UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE %s %s"), name, value ? TEXT("passed") : TEXT("failed"));
        if (!value)
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return value;
    };

    FString path;
    FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), path);
    const auto CAPTURE = [&](const TCHAR* suffix)
    {
        if (!path.IsEmpty())
        {
            FScreenshotRequest::RequestScreenshot(
                FPaths::GetPath(path) / FPaths::GetBaseFilename(path) + suffix + TEXT(".png"), true, false);
        }

    };

    switch (this->smokeTerrainStage)
    {
    case 0:
        if (!lobby->IsInViewport())
        {
            return;
        }

        CAPTURE(TEXT("-login"));
        break;
    case 1:
        if (!CHECK(ClickSlateButtonForTest(login->startButton, FVector2D(.03f, .04f)), TEXT("corner_start")))
        {
            return;
        }

        if (!CHECK(!login->startButton->GetIsEnabled(), TEXT("duplicate_start_blocked")))
        {
            return;
        }

        break;
    case 2:
        // Fixture rejects the first auth request; the existing TCP session remains reusable.
        if (characters->IsAuthenticated() || !login->startButton->GetIsEnabled())
        {
            return;
        }

        CAPTURE(TEXT("-retry"));
        break;
    case 3:
        if (!CHECK(ClickSlateButtonForTest(login->startButton, FVector2D(.97f, .96f)), TEXT("opposite_corner_retry")))
        {
            return;
        }

        break;
    case 4:
        if (!characters->HasCharacterList() || characters->GetCharacters().IsEmpty())
        {
            return;
        }

        if (!CHECK(!IsBusy(), TEXT("selection_does_not_enter")))
        {
            return;
        }

        CAPTURE(TEXT("-selection"));
        break;
    case 5:
        if (!CHECK(ClickSlateButtonForTest(selection->createButton), TEXT("open_creation")))
        {
            return;
        }

        break;
    case 6:
    {
        static bool capturedCreation = false;
        if (!capturedCreation)
        {
            CAPTURE(TEXT("-creation"));
            capturedCreation = true;
            this->smokeProgressTime = NOW;
            return;
        }

        if (!CHECK(ClickSlateButtonForTest(creation->backButton), TEXT("return_selection")))
        {
            return;
        }

    }

    break;
    case 7:
        GetGameInstance()->GetSubsystem<UNetworkManager>()->SimulateConnectionLossForTest();
        if (!CHECK(selection->characterList->GetChildrenCount() == 0, TEXT("disconnect_clears_entries")))
        {
            return;
        }

        break;
    case 8:
        if (!characters->HasCharacterList() || characters->GetCharacters().IsEmpty())
        {
            return;
        }

        if (!CHECK(!IsBusy(), TEXT("reconnect_restores_selection")))
        {
            return;
        }

        break;
    case 9:
        GetGameInstance()->GetSubsystem<UNetworkManager>()->SimulateConnectionLossForTest();
        break;
    case 10:
        // Click during the 0.5-second backoff. It must cancel the scheduled auto attempt.
        if (!CHECK(ClickSlateButtonForTest(login->startButton, FVector2D(.1f, .9f)),
                   TEXT("manual_retry_during_backoff")))
        {
            return;
        }

        break;
    case 11:
        if (!characters->HasCharacterList() || characters->GetCharacters().IsEmpty())
        {
            return;
        }

        if (!CHECK(!IsBusy(), TEXT("manual_retry_restores_selection")))
        {
            return;
        }

        break;
    case 12:
    {
        auto* slot = Cast<UHeroSlot>(selection->characterList->GetChildAt(0));
        auto* button = slot ? Cast<UButton>(slot->GetWidgetFromName(TEXT("SelectButton"))) : nullptr;
        if (!CHECK(ClickSlateButtonForTest(button) && !IsBusy(), TEXT("card_select_only")))
        {
            return;
        }

        break;
    }

    case 13:
    {
        // Enter a different card from the selected one to verify the button's own ID is used.
        auto* slot =
            Cast<UHeroSlot>(selection->characterList->GetChildAt(selection->characterList->GetChildrenCount() - 1));
        const auto* summary = slot ? characters->GetCharacters().FindByPredicate(
                                         [&](const FCharacterSummary& item)
                                         {
                                             return item.characterUnique == slot->GetCharacterId();
                                         })

                                   : nullptr;
        if (summary)
        {
            this->smokeLobbyCharacter = summary->characterUnique;
        }

        auto* button = slot ? Cast<UButton>(slot->GetWidgetFromName(TEXT("EnterGameButton"))) : nullptr;
        if (!CHECK(ClickSlateButtonForTest(button) && IsBusy(), TEXT("card_entry")))
        {
            return;
        }

        for (int32 i = 0; i < selection->characterList->GetChildrenCount(); ++i)
        {
            auto* entry = selection->characterList->GetChildAt(i);
            auto* entryButton = Cast<UButton>(Cast<UUserWidget>(entry)->GetWidgetFromName(TEXT("EnterGameButton")));
            if (!CHECK(entryButton && !entryButton->GetIsEnabled(), TEXT("entry_busy_blocked")))
            {
                return;
            }

        }

        break;
    }

    case 14:
        if (this->state != 5)
        {
            return;
        }

        if (!CHECK(this->economyCharacter == this->smokeLobbyCharacter, TEXT("entry_identity")))
        {
            return;
        }

        CAPTURE(TEXT("-world"));
        break;
    case 15:
        UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE completed passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return;
    default:
        return;
    }

    ++this->smokeTerrainStage;
    this->smokeProgressTime = NOW;
}

#endif
