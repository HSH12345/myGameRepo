#include "World/WorldControlHUD.h"
#include "World/WorldDungeonQueueUI.h"
#include "UIs/RpgUITheme.h"
#include "UIs/RpgArtBorder.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ProgressBar.h"
#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "Tests/SlatePointerClick.h"
#include "World/WorldMinimap.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#if PLATFORM_ANDROID
#include "Blueprint/SlateBlueprintLibrary.h"
#endif

TSharedRef<SWidget> UWorldControlHUD::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    this->quickButtons.Empty();
    this->quickLabels.Empty();
    this->quickIcons.Empty();
    this->quickSelection.Empty();
    this->registerButtons.Empty();
    this->bindingButtons.Empty();
    this->trainButtons.Empty();
    this->skillLabels.Empty();
    this->trainLabels.Empty();
    this->skillGlows.Empty();
    this->potionIcons.Empty();
    this->potionLabels.Empty();
    this->pointerRegions.Empty();
    auto* canvas = this->WidgetTree->ConstructWidget<UCanvasPanel>();
    this->WidgetTree->RootWidget = canvas;
    auto hudText = [&](const FString& value, int32 size)
    {
        auto* label = this->WidgetTree->ConstructWidget<UTextBlock>();
        label->SetText(FText::FromString(value));
        auto font = label->GetFont();
        font.Size = size;
        label->SetFont(font);
        label->SetAutoWrapText(false);
        label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        label->SetClipping(EWidgetClipping::ClipToBounds);
        RpgUI::Text(label, size);
        label->SetVisibility(ESlateVisibility::HitTestInvisible);
        return label;
    };

    auto place = [&](UWidget* widget, FVector2D anchor, FVector2D alignment, FVector2D position, FVector2D size)
    {
        auto* canvasPosition = canvas->AddChildToCanvas(widget);
        canvasPosition->SetAnchors(FAnchors(anchor.X, anchor.Y));
        canvasPosition->SetAlignment(alignment);
        canvasPosition->SetPosition(position);
        canvasPosition->SetSize(size);
        return canvasPosition;
    };

    auto orb = [&](float size, FVector2D offset, FLinearColor color, TObjectPtr<UTextBlock>& label,
                   TObjectPtr<UProgressBar>& bar)
    {
        auto* overlay = this->WidgetTree->ConstructWidget<UOverlay>();
        place(overlay, {0, 1}, {0, 1}, offset, {size, size});
        this->pointerRegions.Add(overlay);
        bar = this->WidgetTree->ConstructWidget<UProgressBar>();
        FProgressBarStyle style;
        style.BackgroundImage = RpgUI::Brush(size > 100 ? TEXT("Orb") : TEXT("ManaOrb"));
        style.BackgroundImage.TintColor = FSlateColor(FLinearColor(.18f, .18f, .18f));
        style.FillImage = RpgUI::Brush(size > 100 ? TEXT("Orb") : TEXT("ManaOrb"));
        bar->SetWidgetStyle(style);
        bar->SetFillColorAndOpacity(FLinearColor::White);
        bar->SetBarFillType(EProgressBarFillType::BottomToTop);
        bar->SetBarFillStyle(EProgressBarFillStyle::Mask);
        auto* fillPosition = overlay->AddChildToOverlay(bar);
        fillPosition->SetPadding(FMargin(3));
        fillPosition->SetHorizontalAlignment(HAlign_Fill);
        fillPosition->SetVerticalAlignment(VAlign_Fill);
        label = hudText(TEXT(""), size > 100 ? 14 : 12);
        label->SetJustification(ETextJustify::Center);
        label->SetAutoWrapText(false);
        auto* labelSlot = overlay->AddChildToOverlay(label);
        labelSlot->SetHorizontalAlignment(HAlign_Fill);
        labelSlot->SetVerticalAlignment(VAlign_Center);
        labelSlot->SetPadding(FMargin(6));
    };

    this->dungeonQueue = CreateWidget<UWorldDungeonQueueUI>(GetOwningPlayer());
    place(this->dungeonQueue, {0, 0}, {0, 0}, {20, 20}, {440, 340});
    this->dungeonQueue->SetVisibility(ESlateVisibility::Collapsed);
    this->pointerRegions.Add(this->dungeonQueue);

#if PLATFORM_ANDROID
    auto* interact = this->WidgetTree->ConstructWidget<UButton>();
    RpgUI::Button(interact);
    interact->AddChild(hudText(TEXT("상호작용"), 20));
    interact->OnClicked.AddDynamic(this, &UWorldControlHUD::InteractClicked);
    place(interact, {.5f, 1}, {.5f, 1}, {0, -36}, {160, 64});
    this->pointerRegions.Add(interact);
#endif

    orb(144, {16, -44}, FLinearColor(.65f, .04f, .08f), this->hpText, this->hpBar);
    orb(100, {112, -18}, FLinearColor(.02f, .36f, .62f), this->mpText, this->mpBar);
    BuildQuickSlots(canvas);
    this->combatActionText = nullptr;
    this->combatSlotsText = nullptr;
    this->castBar = this->WidgetTree->ConstructWidget<UProgressBar>();
    this->castBar->SetFillColorAndOpacity(FLinearColor(.05f, .7f, 1));
    place(this->castBar, {0, 1}, {0, 1}, {220, -104}, {256, 4});
    this->castBar->SetVisibility(ESlateVisibility::Collapsed);
    this->baseStatsText = hudText(TEXT(""), 12);
    this->baseStatsText->SetVisibility(ESlateVisibility::Collapsed);
    canvas->AddChild(this->baseStatsText);
    this->enemyPanel = this->WidgetTree->ConstructWidget<URpgArtBorder>();
    RpgUI::Frame(this->enemyPanel);
    this->enemyPanel->SetPadding(FMargin(12, 7));
    place(this->enemyPanel, {.5f, 0}, {.5f, 0}, {0, 18}, {420, 58});
    this->pointerRegions.Add(this->enemyPanel);
    auto* enemyColumn = this->WidgetTree->ConstructWidget<UVerticalBox>();
    this->enemyPanel->SetContent(enemyColumn);
    this->combatTargetText = hudText(TEXT(""), 15);
    this->combatTargetText->SetJustification(ETextJustify::Center);
    enemyColumn->AddChildToVerticalBox(this->combatTargetText)->SetPadding(FMargin(0, 0, 0, 5));
    this->enemyBar = this->WidgetTree->ConstructWidget<UProgressBar>();
    this->enemyBar->SetFillColorAndOpacity(FLinearColor(.7f, .04f, .08f));
    auto* enemySize = this->WidgetTree->ConstructWidget<USizeBox>();
    enemySize->SetHeightOverride(10);
    enemySize->SetContent(this->enemyBar);
    enemyColumn->AddChild(enemySize);
    this->enemyPanel->SetVisibility(ESlateVisibility::Collapsed);
    auto* mapFrame = this->WidgetTree->ConstructWidget<URpgArtBorder>();
    RpgUI::Frame(mapFrame);
    mapFrame->SetPadding(FMargin(20, 26, 20, 20));
    place(mapFrame, {1, 0}, {1, 0}, {-18, 18}, {250, 250});
    this->pointerRegions.Add(mapFrame);
    this->minimap = this->WidgetTree->ConstructWidget<UWorldMinimap>();
    mapFrame->SetContent(this->minimap);
    this->dialoguePanel = this->WidgetTree->ConstructWidget<URpgArtBorder>();
    RpgUI::Frame(this->dialoguePanel);
    this->dialoguePanel->SetPadding(FMargin(24, 32, 24, 24));
    auto* dialogueSlot = canvas->AddChildToCanvas(this->dialoguePanel);
    dialogueSlot->SetAnchors(FAnchors(1, .5f));
    dialogueSlot->SetAlignment(FVector2D(1, .5f));
    dialogueSlot->SetPosition(FVector2D(-24, 0));
    dialogueSlot->SetSize(FVector2D(620, 390));
    auto* dialogueBody = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    this->dialoguePanel->SetContent(dialogueBody);
    auto* portrait = this->WidgetTree->ConstructWidget<UImage>();
    portrait->SetBrush(RpgUI::Brush(TEXT("Guide")));
    portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* portraitSize = this->WidgetTree->ConstructWidget<USizeBox>();
    portraitSize->SetWidthOverride(170);
    portraitSize->SetContent(RpgUI::FitImage(this->WidgetTree, portrait));
    dialogueBody->AddChildToHorizontalBox(portraitSize)->SetPadding(FMargin(0, 0, 20, 0));
    auto* choices = this->WidgetTree->ConstructWidget<UVerticalBox>();
    dialogueBody->AddChildToHorizontalBox(choices)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto text = [&](int32 size)
    {
        auto* line = this->WidgetTree->ConstructWidget<UTextBlock>();
        auto font = line->GetFont();
        font.Size = size;
        line->SetFont(font);
        line->SetColorAndOpacity(FSlateColor(FLinearColor(.95f, .97f, 1.f)));
        line->SetAutoWrapText(true);
        RpgUI::Text(line, size);
        choices->AddChildToVerticalBox(line)->SetPadding(FMargin(0, 0, 0, 16));
        return line;
    };

    this->dialogueName = text(20);
    auto* textArea = this->WidgetTree->ConstructWidget<USizeBox>();
    textArea->SetHeightOverride(120);
    choices->AddChildToVerticalBox(textArea)->SetPadding(FMargin(0, 0, 0, 16));
    auto* scroll = this->WidgetTree->ConstructWidget<UScrollBox>();
    textArea->AddChild(scroll);
    this->dialogueText = this->WidgetTree->ConstructWidget<UTextBlock>();
    this->dialogueText->SetAutoWrapText(true);
    auto dialogueFont = this->dialogueText->GetFont();
    dialogueFont.Size = 15;
    this->dialogueText->SetFont(dialogueFont);
    this->dialogueText->SetColorAndOpacity(FSlateColor(FLinearColor(.95f, .97f, 1.f)));
    RpgUI::Text(this->dialogueText, 16);
    scroll->AddChild(this->dialogueText);
    auto button = [&](UButton*& outButton, const FString& label)
    {
        outButton = this->WidgetTree->ConstructWidget<UButton>();
        outButton->SetBackgroundColor(FLinearColor(.12f, .28f, .32f));
        auto* line = this->WidgetTree->ConstructWidget<UTextBlock>();
        line->SetText(FText::FromString(label));
        auto font = line->GetFont();
        font.Size = 14;
        line->SetFont(font);
        line->SetAutoWrapText(true);
        line->SetJustification(ETextJustify::Center);
        line->SetMargin(FMargin(10));
        RpgUI::Button(outButton, label != TEXT("닫기"));
        RpgUI::Text(line, 16);
        outButton->AddChild(line);
        choices->AddChildToVerticalBox(outButton)->SetPadding(FMargin(0, 4));
        return line;
    };

    UButton* warp = nullptr;
    this->warpLabel = button(warp, TEXT("이동"));
    this->warpButton = warp;
    UButton* close = nullptr;
    button(close, TEXT("닫기"));
    this->closeButton = close;
    this->warpButton->OnClicked.AddDynamic(this, &UWorldControlHUD::WarpClicked);
    this->closeButton->OnClicked.AddDynamic(this, &UWorldControlHUD::CloseClicked);
    this->dialoguePanel->SetVisibility(ESlateVisibility::Collapsed);
    TArray<UButton*> themeButtons;
    this->WidgetTree->ForEachWidget(
        [&](UWidget* widget)
        {
            if (auto* button = Cast<UButton>(widget))
            {
                themeButtons.Add(button);
            }

        });

    for (auto* themeButton : themeButtons)
    {
        RpgUI::FitButton(this->WidgetTree, themeButton);
    }

    return Super::RebuildWidget();
}

void UWorldControlHUD::ShowStats(const shared_world::EntityStats& stats)
{
    if (this->hpText)
    {
        this->hpText->SetText(FText::FromString(FString::Printf(TEXT("HP\n%u / %u"), stats.hp, stats.maxHp)));
    }

    if (this->hpBar)
    {
        this->hpBar->SetPercent(stats.maxHp ? static_cast<float>(stats.hp) / stats.maxHp : 0);
    }

    if (this->mpText)
    {
        this->mpText->SetText(FText::FromString(FString::Printf(TEXT("MP\n%u / %u"), stats.mp, stats.maxMp)));
    }

    if (this->mpBar)
    {
        this->mpBar->SetPercent(stats.maxMp ? static_cast<float>(stats.mp) / stats.maxMp : 0);
    }

}

void UWorldControlHUD::ShowNpcDialogue(const FString& name, const FString& text, const FString& option)
{
    if (!this->dialoguePanel)
    {
        return;
    }

    this->dialogueName->SetText(FText::FromString(RpgUI::Caption(name)));
    this->dialogueText->SetText(
        FText::FromString(text == TEXT("I can send you to the dungeon test area. Would you like to travel there?")
                              ? TEXT("시험의 던전으로 보내드릴 수 있습니다. 지금 떠나시겠습니까?")
                              : text));
    this->warpLabel->SetText(FText::FromString(RpgUI::Caption(option)));
    this->warpButton->SetIsEnabled(true);
    this->closeButton->SetIsEnabled(true);
    this->dialoguePanel->SetVisibility(ESlateVisibility::Visible);
}

void UWorldControlHUD::HideNpcDialogue()
{
    if (this->dialoguePanel)
    {
        this->dialoguePanel->SetVisibility(ESlateVisibility::Collapsed);
    }

}

bool UWorldControlHUD::IsNpcDialogueVisible() const
{
    return this->dialoguePanel && this->dialoguePanel->GetVisibility() == ESlateVisibility::Visible;
}

#if !UE_BUILD_SHIPPING
bool UWorldControlHUD::ClickNpcWarpForTest()
{
    if (!IsNpcDialogueVisible() || !this->warpButton || !this->warpButton->GetIsEnabled())
    {
        return false;
    }

    return ClickSlateButtonForTest(this->warpButton) && !this->warpButton->GetIsEnabled();
}

bool UWorldControlHUD::ClickQuickSlotForTest(int32 index)
{
    return this->quickButtons.IsValidIndex(index) && ClickSlateButtonForTest(this->quickButtons[index]);
}

bool UWorldControlHUD::CombatHudReadyForTest() const
{
    return this->minimap && !this->minimap->GetSnapshot().lines.IsEmpty() &&
           !this->minimap->GetSnapshot().enemies.IsEmpty() && this->enemyPanel && this->enemyPanel->IsVisible() &&
           this->quickButtons.Num() == 10 && this->hpText && this->hpBar && this->mpText && this->mpBar;
}

#endif
void UWorldControlHUD::SetNpcWarpPending()
{
    if (this->warpButton)
    {
        this->warpButton->SetIsEnabled(false);
        this->closeButton->SetIsEnabled(false);
        this->warpLabel->SetText(FText::FromString(TEXT("이동 중...")));
    }

}

void UWorldControlHUD::WarpClicked()
{
    UE_LOG(LogTemp, Display, TEXT("WORLD_NPC warp_button_clicked"));
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->ChooseNpcWarp();
    }

}

void UWorldControlHUD::InteractClicked()
{
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->Interact();
    }

}

void UWorldControlHUD::CloseClicked()
{
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->CloseNpcDialogue();
    }

}

void UWorldControlHUD::ShowCombat(const FString& target, const FString& action, const FString& slots, float progress)
{
    auto set = [](UTextBlock* text, const FString& value)
    {
        if (text && text->GetText().ToString() != value)
        {
            text->SetText(FText::FromString(RpgUI::Caption(value)));
        }

    };

    set(this->combatActionText, action);
    set(this->combatSlotsText, slots);
    if (this->castBar)
    {
        this->castBar->SetPercent(FMath::Clamp(progress, 0.f, 1.f));
        this->castBar->SetVisibility(progress > 0 && progress < 1 ? ESlateVisibility::HitTestInvisible
                                                                  : ESlateVisibility::Collapsed);
    }

}

void UWorldQuickSlotButton::Activate()
{
    if (!this->ownerHud)
    {
        return;
    }

    if (this->train)
    {
        this->ownerHud->TrainFromBook(this->binding);
    }
    else if (this->potionSlot)
    {
        this->ownerHud->TogglePotionPicker(this->index);
    }
    else if (this->potionRegister)
    {
        this->ownerHud->RegisterPotionFromPicker(this->binding);
    }
    else if (this->bindingOption)
    {
        this->ownerHud->BindQuickSlotFromPicker(this->binding);
    }
    else if (this->registerValue)
    {
        this->ownerHud->ToggleQuickSlotPicker(this->index);
    }
    else
    {
        this->ownerHud->ActivateQuickSlot(this->index);
    }

}

void UWorldControlHUD::ActivateQuickSlot(int32 index)
{
    if (auto* game = GetGameInstance())
    {
        auto* session = game->GetSubsystem<UGameSessionSubsystem>();
        if (this->pendingSkillBinding)
        {
            if (session->BindQuickSlot(index, this->pendingSkillBinding == UINT32_MAX ? 0 : this->pendingSkillBinding))
            {
                this->pendingSkillBinding = 0;
            }

            RefreshSkillBook();
        }
        else if (!IsSkillBookVisible())
        {
            session->ActivateQuickSlot(index);
        }

    }

}

void UWorldControlHUD::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    this->hudElapsed += deltaSeconds;
    if (this->hudElapsed < .1f)
    {
        return;
    }

    this->hudElapsed = 0;
    if (auto* game = GetGameInstance())
    {
        auto* session = game->GetSubsystem<UGameSessionSubsystem>();
        if (this->dungeonQueue)
        {
            this->dungeonQueue->Refresh(session->GetDungeonMatch(), session->GetOwnEntity(),
                                        session->DungeonCountdown());
        }

        RefreshSkillBook();
        const auto STATES = session->GetQuickSlotStates();
        for (int32 i = 0; i < STATES.Num() && i < this->quickButtons.Num(); ++i)
        {
            this->quickButtons[i]->SetIsEnabled(IsSkillBookVisible() || STATES[i].enabled);
            this->quickSelection[i]->SetVisibility(STATES[i].selected ? ESlateVisibility::HitTestInvisible
                                                                      : ESlateVisibility::Collapsed);
            if (this->quickIcons[i]->GetBrush().GetResourceObject() != RpgUI::Texture(STATES[i].icon))
            {
                this->quickIcons[i]->SetBrush(RpgUI::Brush(STATES[i].icon));
            }

            this->quickIcons[i]->SetRenderOpacity(STATES[i].usable ? 1.f : .45f);
            this->quickIcons[i]->SetVisibility(STATES[i].icon.IsNone() ? ESlateVisibility::Hidden
                                                                       : ESlateVisibility::HitTestInvisible);
        }

    }

}

#if !UE_BUILD_SHIPPING
bool UWorldControlHUD::ClickDungeonReadyForTest()
{
    return this->dungeonQueue && this->dungeonQueue->ClickReadyForTest();
}

#endif

void UWorldControlHUD::ShowEnemy(const FString& name, int32 hp, int32 maxHp)
{
    if (!this->enemyPanel)
    {
        return;
    }

    this->enemyPanel->SetVisibility(maxHp > 0 && hp > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (maxHp <= 0)
    {
        return;
    }

    this->combatTargetText->SetText(FText::FromString(FString::Printf(TEXT("%s   %d / %d"), *name, hp, maxHp)));
    this->enemyBar->SetPercent(FMath::Clamp(float(hp) / maxHp, 0.f, 1.f));
}

bool UWorldControlHUD::IsPointerOverHud() const
{
    if (!FSlateApplication::IsInitialized())
    {
        return false;
    }

    return IsAbsolutePositionOverHud(FSlateApplication::Get().GetCursorPos());
}

#if PLATFORM_ANDROID
bool UWorldControlHUD::IsScreenPositionOverHud(const FVector2D& position) const
{
    FVector2D absolutePosition = position;
    USlateBlueprintLibrary::ScreenToWidgetAbsolute(this, position, absolutePosition);
    return IsAbsolutePositionOverHud(absolutePosition);
}

#endif
bool UWorldControlHUD::IsAbsolutePositionOverHud(const FVector2D& position) const
{
    for (UWidget* region : this->pointerRegions)
    {
        if (region && region->IsVisible() && region->GetCachedGeometry().IsUnderLocation(position))
        {
            return true;
        }

    }

    return false;
}

