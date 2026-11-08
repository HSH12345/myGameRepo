#include "UIs/Title/TitleUIController.h"
#include "UIs/RpgUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Managers/LoginManager.h"
#include "Managers/NetworkManager.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "World/GameSessionSubsystem.h"

UCharacterManager* UTitleUIController::Characters() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCharacterManager>() : nullptr;
}

void UTitleUIController::NativeConstruct()
{
    Super::NativeConstruct();
    this->screenTearingDown = false;
    RpgUI::FitTree(this);
    if (!this->screenSwitcher || !this->loginScreen || !this->characterSelectScreen || !this->characterCreateScreen)
    {
        UE_LOG(LogTemp, Error, TEXT("Lobby widget bindings are missing. Check WBP_LobbyRoot."));
        return;
    }

    this->characterSelectScreen->WidgetTree->ForEachWidget(
        [](UWidget* widget)
        {
            if (auto* text = Cast<UTextBlock>(widget))
            {
                if (text->GetText().ToString().Contains(TEXT("미확정 결과 확인")))
                {
                    text->SetText(FText::FromString(TEXT("생성")));
                }

                if (text->GetText().ToString() == TEXT("목록 새로고침"))
                {
                    text->SetText(FText::FromString(TEXT("다시 시도")));
                }

            }

        });

    this->characterSelectScreen->refreshButton->SetVisibility(ESlateVisibility::Collapsed);
    this->characterSelectScreen->selectedCharacterText->SetVisibility(ESlateVisibility::Collapsed);
    if (auto* size = Cast<USizeBox>(this->characterSelectScreen->createButton->GetParent()))
    {
        size->SetWidthOverride(144);
        size->SetHeightOverride(46);
        if (auto* position = Cast<UVerticalBoxSlot>(size->Slot))
        {
            position->SetHorizontalAlignment(HAlign_Right);
        }

    }

    BindButtons(true);
    auto* loginManager = GetGameInstance()->GetSubsystem<ULoginManager>();
    loginManager->onLoginStatus.AddUniqueDynamic(this, &UTitleUIController::HandleLoginStatus);
    loginManager->onLoginFailed.AddUniqueDynamic(this, &UTitleUIController::HandleLoginFailed);
    if (UCharacterManager* manager = Characters())
    {
        manager->onCharacterListChanged.AddUniqueDynamic(this, &UTitleUIController::RebuildCharacterList);
        manager->onCharacterListStateChanged.AddUniqueDynamic(this, &UTitleUIController::HandleListState);
        manager->onCharacterCreationStateChanged.AddUniqueDynamic(this, &UTitleUIController::HandleCreationState);
        manager->onCharacterCreationCompleted.AddUniqueDynamic(this, &UTitleUIController::HandleCreationCompleted);
        if (manager->IsAuthenticated())
        {
            HandleListState(manager->GetListState());
        }
        else
        {
            SetScreen(this->loginScreen);
            if (!this->loginPending)
            {
                this->loginScreen->startButton->SetIsEnabled(true);
                SetLoginStatus(
                    NSLOCTEXT("Lobby", "AccountStartPrompt", "네이버로 로그인하거나 화면을 눌러 게스트로 시작하세요."));
            }

        }

    }

    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->onStatusChanged.AddUniqueDynamic(
        this, &UTitleUIController::HandleWorldStatus);
    UpdateEntryControls();
    // ClearSelection retains SelectedOption in UE. Re-adding the same options can leave
    // the Slate caption empty when SetSelectedIndex considers the string unchanged.
    if (this->characterCreateScreen->genderChoice->GetOptionCount() != 2 ||
        this->characterCreateScreen->genderChoice->GetOptionAtIndex(0) != TEXT("남성") ||
        this->characterCreateScreen->genderChoice->GetOptionAtIndex(1) != TEXT("여성"))
    {
        this->characterCreateScreen->genderChoice->ClearOptions();
        this->characterCreateScreen->genderChoice->AddOption(TEXT("남성"));
        this->characterCreateScreen->genderChoice->AddOption(TEXT("여성"));
    }

    this->characterCreateScreen->genderChoice->SetSelectedIndex(1);
    this->characterCreateScreen->genderChoice->SetSelectedIndex(0);
    UpdateCreationControls();
}

void UTitleUIController::NativeDestruct()
{
    this->screenTearingDown = true;
    if (GetGameInstance())
    {
        auto* login = GetGameInstance()->GetSubsystem<ULoginManager>();
        login->onLoginStatus.RemoveDynamic(this, &UTitleUIController::HandleLoginStatus);
        login->onLoginFailed.RemoveDynamic(this, &UTitleUIController::HandleLoginFailed);
        if (this->loginPending)
        {
            login->Logout();
        }

    }

    if (GetGameInstance())
    {
        GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->onStatusChanged.RemoveDynamic(
            this, &UTitleUIController::HandleWorldStatus);
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(this->loginTimer);
    }

    HideCharacterPreview();
    BindButtons(false);
    if (UCharacterManager* manager = Characters())
    {
        manager->onCharacterListChanged.RemoveDynamic(this, &UTitleUIController::RebuildCharacterList);
        manager->onCharacterListStateChanged.RemoveDynamic(this, &UTitleUIController::HandleListState);
        manager->onCharacterCreationStateChanged.RemoveDynamic(this, &UTitleUIController::HandleCreationState);
        manager->onCharacterCreationCompleted.RemoveDynamic(this, &UTitleUIController::HandleCreationCompleted);
    }

    for (UHeroSlot* entry : this->characterSlots)
    {
        if (entry)
        {
            entry->onSelected.RemoveDynamic(this, &UTitleUIController::SelectCharacter);
            entry->onEnterRequested.RemoveDynamic(this, &UTitleUIController::EnterGame);
        }

    }

    Super::NativeDestruct();
}

void UTitleUIController::BindButtons(bool bind)
{
    // Keep registration and removal adjacent so repeated viewport entry is safe.
#define LOBBY_BUTTON(screen, button, handler)                                                                          \
    if (screen && screen->button)                                                                                      \
    {                                                                                                                  \
        screen->button->OnClicked.RemoveDynamic(this, &UTitleUIController::handler);                                   \
        if (bind)                                                                                                      \
        {                                                                                                              \
            screen->button->OnClicked.AddUniqueDynamic(this, &UTitleUIController::handler);                            \
        }                                                                                                              \
\
    }

    LOBBY_BUTTON(this->loginScreen, startButton, Connect)
    LOBBY_BUTTON(this->loginScreen, naverLoginButton, NaverLogin)
    LOBBY_BUTTON(this->loginScreen, cancelAuthButton, CancelLogin)
    LOBBY_BUTTON(this->characterSelectScreen, logoutButton, Logout)
    LOBBY_BUTTON(this->characterSelectScreen, createButton, ShowCreateScreen)
    LOBBY_BUTTON(this->characterSelectScreen, refreshButton, RefreshCharacters)
    LOBBY_BUTTON(this->characterCreateScreen, submitButton, SubmitCreation)
    LOBBY_BUTTON(this->characterCreateScreen, confirmPendingButton, ConfirmPendingCreation)
    LOBBY_BUTTON(this->characterCreateScreen, backButton, BackToSelection)
#undef LOBBY_BUTTON
    if (this->characterCreateScreen && this->characterCreateScreen->genderChoice)
    {
        this->characterCreateScreen->genderChoice->OnSelectionChanged.RemoveDynamic(
            this, &UTitleUIController::HandleGenderSelectionChanged);
        if (bind)
        {
            this->characterCreateScreen->genderChoice->OnSelectionChanged.AddUniqueDynamic(
                this, &UTitleUIController::HandleGenderSelectionChanged);
        }

    }

}

void UTitleUIController::EnterGame(int64 characterId)
{
    const auto* manager = Characters();
    if (!manager || manager->GetListState() != ECharacterListState::READY ||
        !manager->GetCharacters().ContainsByPredicate(
            [characterId](const FCharacterSummary& item)
            {
                return item.characterUnique == characterId;
            }))

    {
        return;
    }

    auto* session = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    if (session->IsBusy())
    {
        return;
    }

    SelectCharacter(characterId);
    if (!session->EnterCharacter(this->selectedCharacterId))
    {
        HandleWorldStatus(TEXT("Game entry is not available. Check the connection."));
    }

}

void UTitleUIController::UpdateEntryControls()
{
    const auto* manager = Characters();
    const bool B_AVAILABLE = manager && manager->IsAuthenticated() &&
                             manager->GetListState() == ECharacterListState::READY &&
                             !GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->IsBusy();
    for (UHeroSlot* entry : this->characterSlots)
    {
        if (entry)
        {
            entry->SetEntryEnabled(B_AVAILABLE);
        }

    }

}

void UTitleUIController::HandleWorldStatus(const FString& status)
{
    if (!this->characterSelectScreen)
    {
        return;
    }

    this->characterSelectScreen->statusText->SetText(FText::FromString(status));
    UpdateEntryControls();
}

ACharacterPreviewActor* UTitleUIController::FindCharacterPreview()
{
    if (this->characterPreview.IsValid())
    {
        return this->characterPreview.Get();
    }

    UWorld* world = GetWorld();
    if (!world)
    {
        return nullptr;
    }

    TActorIterator<ACharacterPreviewActor> it(world);
    if (it)
    {
        this->characterPreview = *it;
        this->characterPreview->onPreviewStateChanged.AddUniqueDynamic(this,
                                                                       &UTitleUIController::HandlePreviewStateChanged);
        return this->characterPreview.Get();
    }

    return nullptr;
}

void UTitleUIController::ShowSelectedCharacterPreview()
{
    if (!this->screenSwitcher || this->screenSwitcher->GetActiveWidget() != this->characterCreateScreen ||
        !this->characterCreateScreen || !this->characterCreateScreen->genderChoice)
    {
        return;
    }

    ACharacterPreviewActor* preview = FindCharacterPreview();
    if (!preview)
    {
        this->characterCreateScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "MissingPreviewActor", "캐릭터 미리보기가 설정되지 않았습니다."));
        return;
    }

    preview->ActivateForPlayer(GetOwningPlayer());
    const int32 SELECTED_GENDER = FMath::Max(0, this->characterCreateScreen->genderChoice->GetSelectedIndex());
    preview->ShowAppearance(0, static_cast<uint8>(SELECTED_GENDER));
}

void UTitleUIController::HideCharacterPreview()
{
    if (ACharacterPreviewActor* preview = this->characterPreview.Get())
    {
        preview->onPreviewStateChanged.RemoveDynamic(this, &UTitleUIController::HandlePreviewStateChanged);
        preview->DeactivateForPlayer();
    }

    this->characterPreview.Reset();
}

void UTitleUIController::HandleGenderSelectionChanged(FString selectedItem, ESelectInfo::Type selectionType)
{
    ShowSelectedCharacterPreview();
}

void UTitleUIController::HandlePreviewStateChanged(ECharacterPreviewLoadState state)
{
    UpdateCreationControls();
}

void UTitleUIController::SetScreen(UUserWidget* screen)
{
    if (this->screenSwitcher && screen)
    {
        this->screenSwitcher->SetActiveWidget(screen);
    }

}

void UTitleUIController::SetLoginStatus(const FText& text)
{
    if (this->loginScreen && this->loginScreen->statusText)
    {
        this->loginScreen->statusText->SetText(text);
    }

}

void UTitleUIController::Connect()
{
    if (this->loginPending)
    {
        return;
    }

    this->naverSelected = false;
    ConnectSelected();
}

void UTitleUIController::NaverLogin()
{
    if (this->loginPending)
    {
        return;
    }

    this->naverSelected = true;
    ConnectSelected();
}

void UTitleUIController::ConnectSelected()
{
    if (!GetGameInstance() || !this->loginScreen || this->loginPending ||
        this->screenSwitcher->GetActiveWidget() != this->loginScreen)
    {
        return;
    }

    UNetworkManager* network = GetGameInstance()->GetSubsystem<UNetworkManager>();
    if (!network || network->GetConnectionState() == ENetworkConnectionState::CONNECTING ||
        network->GetConnectionState() == ENetworkConnectionState::CLOSING)
    {
        return;
    }

    if (network->GetConnectionState() == ENetworkConnectionState::CONNECTED)
    {
        GuestLogin();
        return;
    }

    TWeakObjectPtr<UTitleUIController> weakThis(this);
    network->ConnectLoginServer(
        [weakThis]()
        {
            if (weakThis.IsValid() && !weakThis->screenTearingDown)
            {
                weakThis->ShowLoginUI();
            }

        });

    if (network->GetConnectionState() == ENetworkConnectionState::CONNECTING)
    {
        this->loginScreen->startButton->SetIsEnabled(false);
        if (this->loginScreen->naverLoginButton)
        {
            this->loginScreen->naverLoginButton->SetIsEnabled(false);
        }

        if (this->loginScreen->cancelAuthButton)
        {
            this->loginScreen->cancelAuthButton->SetVisibility(ESlateVisibility::Visible);
        }

        SetLoginStatus(NSLOCTEXT("Lobby", "ConnectingGame", "게임 서버에 연결 중..."));
    }

}

void UTitleUIController::ShowLoginUI()
{
    if (!this->loginScreen)
    {
        return;
    }

    SetScreen(this->loginScreen);
    GuestLogin();
}

void UTitleUIController::GuestLogin()
{
    if (this->loginPending || !GetGameInstance())
    {
        return;
    }

    this->loginPending = true;
    SetGuestLoginEnabled(false);
    SetLoginStatus(this->naverSelected ? NSLOCTEXT("Lobby", "SigningInNaver", "네이버 로그인 준비 중...")
                                       : NSLOCTEXT("Lobby", "SigningInGuest", "게스트 계정으로 로그인 중..."));
    auto* login = GetGameInstance()->GetSubsystem<ULoginManager>();
    if (!(this->naverSelected ? login->StartNaverLoginProcess() : login->StartLoginPorcess()))
    {
        SetGuestLoginEnabled(true);
    }
    else if (GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(this->loginTimer, this, &UTitleUIController::LoginTimedOut,
                                               this->naverSelected ? 340.0f : 20.0f, false);
    }

}

void UTitleUIController::LoginTimedOut()
{
    // Reconnect rather than sending a second auth request into an uncertain session.
    if (GetGameInstance())
    {
        GetGameInstance()->GetSubsystem<UNetworkManager>()->DisconnectLoginServer();
    }

    SetLoginStatus(
        NSLOCTEXT("Lobby", "StartTimeout", "로그인 시간이 초과되었습니다. 화면을 클릭하여 다시 접속하세요."));
}

void UTitleUIController::SetGuestLoginEnabled(bool enabled)
{
    this->loginPending = !enabled;
    if (enabled && GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(this->loginTimer);
    }

    if (this->loginScreen && this->loginScreen->startButton)
    {
        this->loginScreen->startButton->SetIsEnabled(enabled);
    }

    if (this->loginScreen && this->loginScreen->naverLoginButton)
    {
        this->loginScreen->naverLoginButton->SetIsEnabled(enabled);
    }

    if (this->loginScreen && this->loginScreen->cancelAuthButton)
    {
        this->loginScreen->cancelAuthButton->SetVisibility(enabled ? ESlateVisibility::Collapsed
                                                                   : ESlateVisibility::Visible);
    }

    if (enabled)
    {
        SetLoginStatus(NSLOCTEXT("Lobby", "StartRetry", "로그인하지 못했습니다. 화면을 클릭하여 다시 시도하세요."));
    }

}

void UTitleUIController::ShowHeroSelectionUI()
{
    HideCharacterPreview();
    if (UCharacterManager* manager = Characters(); manager && manager->HasCharacterList())
    {
        SetScreen(this->characterSelectScreen);
        RebuildCharacterList();
    }

}

void UTitleUIController::RefreshCharacters()
{
    if (UCharacterManager* manager = Characters())
    {
        manager->RetryCharacterList();
    }

}

void UTitleUIController::HandleListState(ECharacterListState state)
{
    if (this->screenTearingDown)
    {
        return;
    }

    if (!this->characterSelectScreen || !this->loginScreen)
    {
        return;
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(this->loginTimer);
    }

    this->loginPending = false;
    if (state == ECharacterListState::DISCONNECTED)
    {
        HideCharacterPreview();
        this->selectedCharacterId = 0;
        RebuildCharacterList();
        this->loginPending = false;
        SetScreen(this->loginScreen);
        this->loginScreen->startButton->SetIsEnabled(true);
        if (this->loginScreen->naverLoginButton)
        {
            this->loginScreen->naverLoginButton->SetIsEnabled(true);
        }

        if (this->loginScreen->cancelAuthButton)
        {
            this->loginScreen->cancelAuthButton->SetVisibility(ESlateVisibility::Collapsed);
        }

        SetLoginStatus(
            NSLOCTEXT("Lobby", "StartDisconnected", "연결이 끊어졌습니다. 화면을 클릭하여 다시 접속하세요."));
        UpdateCreationControls();
        return;
    }

    if (this->screenSwitcher && this->screenSwitcher->GetActiveWidget() != this->characterCreateScreen)
    {
        SetScreen(this->characterSelectScreen);
    }

    this->characterSelectScreen->refreshButton->SetIsEnabled(state == ECharacterListState::FAILED_VALUE);
    this->characterSelectScreen->refreshButton->SetVisibility(
        state == ECharacterListState::FAILED_VALUE ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    this->characterSelectScreen->statusText->SetText(
        state == ECharacterListState::LOADING ? NSLOCTEXT("Lobby", "Loading", "캐릭터를 불러오는 중...")
        : state == ECharacterListState::FAILED_VALUE
            ? NSLOCTEXT("Lobby", "ListFailed", "목록을 불러오지 못했습니다. 다시 시도하세요.")
            : FText::GetEmpty());
    RebuildCharacterList();
}

void UTitleUIController::RebuildCharacterList()
{
    UCharacterManager* manager = Characters();
    if (!manager || !this->characterSelectScreen || !this->characterSelectScreen->characterList)
    {
        return;
    }

    const TArray<FCharacterSummary>& items = manager->GetCharacters();
    // Reuse existing entries; status changes and list refreshes do not rebuild every UObject.
    while (this->characterSlots.Num() > items.Num())
    {
        UHeroSlot* entry = this->characterSlots.Last();
        entry->onSelected.RemoveDynamic(this, &UTitleUIController::SelectCharacter);
        entry->onEnterRequested.RemoveDynamic(this, &UTitleUIController::EnterGame);
        entry->RemoveFromParent();
        this->characterSlots.Pop();
    }

    if (!items.ContainsByPredicate(
            [this](const FCharacterSummary& item)
            {
                return item.characterUnique == this->selectedCharacterId;
            }))

    {
        this->selectedCharacterId = items.IsEmpty() ? 0 : items[0].characterUnique;
    }

    while (this->characterSlots.Num() < items.Num())
    {
        UHeroSlot* entry = this->characterSlotClass ? CreateWidget<UHeroSlot>(this, this->characterSlotClass) : nullptr;
        if (!entry)
        {
            this->characterSelectScreen->statusText->SetText(
                NSLOCTEXT("Lobby", "MissingSlot", "Character slot class is not configured."));
            break;
        }

        this->characterSelectScreen->characterList->AddChild(entry);
        entry->onSelected.AddUniqueDynamic(this, &UTitleUIController::SelectCharacter);
        entry->onEnterRequested.AddUniqueDynamic(this, &UTitleUIController::EnterGame);
        this->characterSlots.Add(entry);
    }

    for (int32 index = 0; index < this->characterSlots.Num(); ++index)
    {
        this->characterSlots[index]->SetCharacterData(items[index]);
        this->characterSlots[index]->onSelected.AddUniqueDynamic(this, &UTitleUIController::SelectCharacter);
        this->characterSlots[index]->onEnterRequested.AddUniqueDynamic(this, &UTitleUIController::EnterGame);
    }

    this->characterSelectScreen->countText->SetText(FText::Format(NSLOCTEXT("Lobby", "Count", "캐릭터: {0} / {1}"),
                                                                  FText::AsNumber(items.Num()),
                                                                  FText::AsNumber(MAX_CHARACTER_COUNT)));
    if (manager->GetListState() == ECharacterListState::READY && items.IsEmpty())
    {
        this->characterSelectScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "Empty", "첫 번째 영웅을 만들어 모험을 시작하세요."));
    }

    SelectCharacter(this->selectedCharacterId);
    UpdateCreationControls();
}

void UTitleUIController::SelectCharacter(int64 characterId)
{
    this->selectedCharacterId = characterId;
    for (UHeroSlot* entry : this->characterSlots)
    {
        if (entry)
        {
            entry->SetSelected(entry->GetCharacterId() == characterId);
        }

    }

    if (!this->characterSelectScreen)
    {
        return;
    }

    UpdateEntryControls();
    this->characterSelectScreen->selectedCharacterText->SetVisibility(ESlateVisibility::Collapsed);
}

void UTitleUIController::ShowCreateScreen()
{
    if (UCharacterManager* manager = Characters(); manager && manager->IsAuthenticated())
    {
        SetScreen(this->characterCreateScreen);
        UpdateCreationControls();
        ShowSelectedCharacterPreview();
    }

}

void UTitleUIController::BackToSelection()
{
    HideCharacterPreview();
    SetScreen(this->characterSelectScreen);
    RebuildCharacterList();
}

void UTitleUIController::SubmitCreation()
{
    UCharacterManager* manager = Characters();
    if (!manager || !this->characterCreateScreen)
    {
        return;
    }

    FCreateCharacterOptions options;
    options.characterName = this->characterCreateScreen->characterNameInput->GetText().ToString().TrimStartAndEnd();
    options.race = 0; // Current server default supports Human only; do not advertise Orc yet.
    options.gender = static_cast<uint8>(this->characterCreateScreen->genderChoice->GetSelectedIndex());
    if (options.characterName.Len() < 2 || options.characterName.Len() > 24)
    {
        this->characterCreateScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "NameLength", "이름은 2~24자로 입력하세요."));
        return;
    }

    const bool B_SENT = manager->CreateCharacter(options);
    UpdateCreationControls();
    if (!B_SENT && manager->GetCreationState() != ECharacterCreationState::NEEDS_CONFIRMATION)
    {
        this->characterCreateScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "SubmitFailed", "요청하지 못했습니다. 연결과 입력을 확인하세요."));
    }

}

void UTitleUIController::ConfirmPendingCreation()
{
    if (UCharacterManager* manager = Characters())
    {
        manager->RetryPendingCreation(); // Never creates a new operation ID or changes the saved input.
        UpdateCreationControls();
    }

}

void UTitleUIController::HandleCreationState(ECharacterCreationState state)
{
    UpdateCreationControls();
}

void UTitleUIController::UpdateCreationControls()
{
    UCharacterManager* manager = Characters();
    if (!manager || !this->characterSelectScreen || !this->characterCreateScreen)
    {
        return;
    }

    const ECharacterCreationState STATE = manager->GetCreationState();
    const bool B_PENDING = !manager->GetPendingOperationId().IsEmpty();
    if (B_PENDING)
    {
        const FCreateCharacterOptions PENDING = manager->GetPendingOptions();
        this->characterCreateScreen->characterNameInput->SetText(FText::FromString(PENDING.characterName));
        this->characterCreateScreen->genderChoice->SetSelectedIndex(PENDING.gender);
    }

    const bool B_CAN_CREATE = manager->IsAuthenticated() && manager->HasCharacterList() &&
                              manager->GetCharacters().Num() < MAX_CHARACTER_COUNT && !B_PENDING;
    this->characterSelectScreen->createButton->SetIsEnabled(B_CAN_CREATE || (manager->IsAuthenticated() && B_PENDING));
    this->characterCreateScreen->submitButton->SetIsEnabled(B_CAN_CREATE);
    this->characterCreateScreen->characterNameInput->SetIsEnabled(!B_PENDING);
    this->characterCreateScreen->genderChoice->SetIsEnabled(!B_PENDING);
    this->characterCreateScreen->confirmPendingButton->SetIsEnabled(
        manager->IsAuthenticated() && STATE == ECharacterCreationState::NEEDS_CONFIRMATION);
    this->characterCreateScreen->confirmPendingButton->SetVisibility(B_PENDING ? ESlateVisibility::Visible
                                                                               : ESlateVisibility::Collapsed);
    const ACharacterPreviewActor* preview = this->characterPreview.Get();
    if (STATE == ECharacterCreationState::IN_FLIGHT)
    {
        this->characterCreateScreen->statusText->SetText(NSLOCTEXT("Lobby", "Creating", "캐릭터를 생성하는 중..."));
    }
    else if (STATE == ECharacterCreationState::NEEDS_CONFIRMATION)
    {
        this->characterCreateScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "Uncertain", "이전 생성 결과가 미확정입니다. 같은 요청의 결과를 먼저 확인하세요."));
    }
    else if (preview && (preview->GetPreviewState() == ECharacterPreviewLoadState::LOADING ||
                         preview->GetPreviewState() == ECharacterPreviewLoadState::PREPARING))
    {
        this->characterCreateScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "PreviewLoading", "영웅의 모습을 불러오는 중..."));
    }
    else if (preview && preview->GetPreviewState() == ECharacterPreviewLoadState::FAILED_VALUE)
    {
        this->characterCreateScreen->statusText->SetText(
            NSLOCTEXT("Lobby", "PreviewFailed", "외형을 불러오지 못했습니다. 잠시 후 다시 시도하세요."));
    }
    else
    {
        this->characterCreateScreen->statusText->SetText(NSLOCTEXT(
            "Lobby", "CreateHelp", "인간 | 이름 2~24자: 한글, 영문, 숫자. 사용 가능 여부는 서버가 확인합니다."));
    }

}

void UTitleUIController::HandleCreationCompleted(const FString& operationId, ECharacterCreationResult result,
                                                 const FCharacterSummary& character)
{
    if (result == ECharacterCreationResult::SUCCESS)
    {
        this->selectedCharacterId = character.characterUnique;
        this->characterCreateScreen->characterNameInput->SetText(FText::GetEmpty());
        BackToSelection();
        RefreshCharacters();
        return;
    }

    const UEnum* resultEnum = StaticEnum<ECharacterCreationResult>();
    this->characterCreateScreen->statusText->SetText(
        FText::Format(NSLOCTEXT("Lobby", "CreationRejected", "생성 거절: {0}"),
                      resultEnum->GetDisplayNameTextByValue(static_cast<int64>(result))));
}

void UTitleUIController::HandleLoginStatus(const FString& status)
{
    if (!this->screenTearingDown)
    {
        SetLoginStatus(FText::FromString(status));
    }

}

void UTitleUIController::HandleLoginFailed(const FString& reason)
{
    if (this->screenTearingDown)
    {
        return;
    }

    SetGuestLoginEnabled(true);
    SetLoginStatus(FText::FromString(reason));
}

void UTitleUIController::CancelLogin()
{
    if (!GetGameInstance())
    {
        return;
    }

    GetGameInstance()->GetSubsystem<ULoginManager>()->Logout();
    SetGuestLoginEnabled(true);
    SetLoginStatus(NSLOCTEXT("Lobby", "AccountCancelled", "로그인을 취소했습니다. 다시 시작할 수 있습니다."));
}

void UTitleUIController::Logout()
{
    this->naverSelected = false;
    CancelLogin();
    SetLoginStatus(NSLOCTEXT("Lobby", "AccountLoggedOut", "로그아웃했습니다. 로그인 방법을 선택하세요."));
}

