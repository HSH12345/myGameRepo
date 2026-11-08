#include "World/WorldDungeonQueueUI.h"
#include "World/GameSessionSubsystem.h"
#include "UIs/RpgUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Tests/SlatePointerClick.h"

TSharedRef<SWidget> UWorldDungeonQueueUI::RebuildWidget()
{
    this->memberNames.Empty();
    this->memberStates.Empty();
    this->memberPortraits.Empty();
    auto* frame = this->WidgetTree->ConstructWidget<UBorder>();
    frame->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f, .03f, .04f, .97f), 14.f));
    frame->SetPadding(FMargin(14));
    this->WidgetTree->RootWidget = frame;
    auto* body = this->WidgetTree->ConstructWidget<UVerticalBox>();
    frame->SetContent(body);
    auto label = [&](const FString& value, int32 size)
    {
        auto* text = this->WidgetTree->ConstructWidget<UTextBlock>();
        text->SetText(FText::FromString(value));
        RpgUI::Text(text, size);
        text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        text->SetVisibility(ESlateVisibility::HitTestInvisible);
        return text;
    };

    auto* heading = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    body->AddChildToVerticalBox(heading)->SetPadding(FMargin(0, 0, 0, 8));
    heading->AddChildToHorizontalBox(label(TEXT("던전 입장 대기"), 18))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    this->countText = label(TEXT("1 / 4"), 15);
    heading->AddChildToHorizontalBox(this->countText);
    auto* band = this->WidgetTree->ConstructWidget<UBorder>();
    band->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.22f, .07f, .43f), 6.f));
    band->SetPadding(FMargin(10, 6));
    body->AddChildToVerticalBox(band)->SetPadding(FMargin(0, 0, 0, 10));
    auto* bandRow = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    band->SetContent(bandRow);
    this->titleText = label(TEXT("파티 모집"), 16);
    bandRow->AddChildToHorizontalBox(this->titleText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    this->countdownText = label(TEXT("준비 대기"), 16);
    bandRow->AddChildToHorizontalBox(this->countdownText);
    auto* slots = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    body->AddChildToVerticalBox(slots);
    for (unsigned i = 0; i < shared_world::party::MAX_MEMBERS; ++i)
    {
        auto* card = this->WidgetTree->ConstructWidget<UBorder>();
        card->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.09f, .065f, .17f), 6.f));
        card->SetPadding(FMargin(6));
        auto* position = slots->AddChildToHorizontalBox(card);
        position->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        position->SetPadding(FMargin(3, 0));
        auto* column = this->WidgetTree->ConstructWidget<UVerticalBox>();
        card->SetContent(column);
        auto* portrait = this->WidgetTree->ConstructWidget<UImage>();
        portrait->SetBrush(RpgUI::Brush(TEXT("Male")));
        auto* portraitSize = this->WidgetTree->ConstructWidget<USizeBox>();
        portraitSize->SetWidthOverride(70);
        portraitSize->SetHeightOverride(62);
        portraitSize->SetContent(RpgUI::FitImage(this->WidgetTree, portrait));
        column->AddChildToVerticalBox(portraitSize)->SetHorizontalAlignment(HAlign_Center);
        this->memberPortraits.Add(portrait);
        auto* name = label(TEXT("빈 자리"), 13);
        name->SetJustification(ETextJustify::Center);
        column->AddChildToVerticalBox(name)->SetPadding(FMargin(0, 4));
        this->memberNames.Add(name);
        auto* state = label(TEXT("모집 중"), 12);
        state->SetJustification(ETextJustify::Center);
        column->AddChildToVerticalBox(state);
        this->memberStates.Add(state);
    }

    this->hintText = label(TEXT("모두 준비하면 5초 뒤 자동으로 입장합니다."), 12);
    this->hintText->SetJustification(ETextJustify::Center);
    body->AddChildToVerticalBox(this->hintText)->SetPadding(FMargin(0, 10));
    auto* actions = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    body->AddChildToVerticalBox(actions);
    auto action = [&](const TCHAR* caption, TObjectPtr<UTextBlock>& outLabel)
    {
        auto* button = this->WidgetTree->ConstructWidget<UButton>();
        outLabel = label(caption, 15);
        button->SetContent(outLabel);
        RpgUI::Button(button, true);
        auto* height = this->WidgetTree->ConstructWidget<USizeBox>();
        height->SetHeightOverride(38);
        height->SetContent(button);
        auto* slot = actions->AddChildToHorizontalBox(height);
        slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        slot->SetPadding(FMargin(3, 0));
        return button;
    };

    this->readyButton = action(TEXT("준비"), this->readyText);
    this->readyButton->OnClicked.AddUniqueDynamic(this, &UWorldDungeonQueueUI::ToggleReady);
    TObjectPtr<UTextBlock> leaveText;
    auto* leave = action(TEXT("대기 취소"), leaveText);
    leave->OnClicked.AddUniqueDynamic(this, &UWorldDungeonQueueUI::Leave);
    return Super::RebuildWidget();
}

void UWorldDungeonQueueUI::Refresh(const shared_world::net::Message& state, uint64 ownEntity, double countdown)
{
    SetVisibility(state.room ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!state.room || this->memberNames.Num() != 4)
    {
        return;
    }

    this->ownReady = false;
    this->titleText->SetText(
        FText::FromString(state.targetMap == 301 ? TEXT("미로 던전 · 파티 모집") : TEXT("던전 · 파티 모집")));
    this->countText->SetText(
        FText::FromString(FString::Printf(TEXT("%u / 4"), static_cast<unsigned>(state.members.size()))));
    this->countdownText->SetText(
        FText::FromString(state.matchPhase == shared_world::party::MatchPhase::COUNTDOWN
                              ? FString::Printf(TEXT("%02d초 후 입장"), FMath::CeilToInt(countdown))
                              : TEXT("준비 대기")));
    for (int32 i = 0; i < 4; ++i)
    {
        const bool OCCUPIED = i < static_cast<int32>(state.members.size());
        const auto* member = OCCUPIED ? &state.members[i] : nullptr;
        this->memberPortraits[i]->SetVisibility(OCCUPIED ? ESlateVisibility::HitTestInvisible
                                                         : ESlateVisibility::Hidden);
        if (member)
        {
            this->memberPortraits[i]->SetBrush(RpgUI::Brush(member->gender ? TEXT("Female") : TEXT("Male")));
            if (member->entity == ownEntity)
            {
                this->ownReady = member->ready;
            }

        }

        this->memberNames[i]->SetText(
            FText::FromString(member ? UTF8_TO_TCHAR(member->name.c_str()) : TEXT("빈 자리")));
        this->memberStates[i]->SetText(FText::FromString(!member         ? TEXT("모집 중")
                                                         : member->ready ? TEXT("준비 완료")
                                                                         : TEXT("준비 대기")));
        this->memberStates[i]->SetColorAndOpacity(
            FSlateColor(member && member->ready ? FLinearColor(.35f, 1.f, .65f) : FLinearColor(.6f, .6f, .7f)));
    }

    this->readyText->SetText(FText::FromString(this->ownReady ? TEXT("준비 취소") : TEXT("준비")));
    this->hintText->SetText(FText::FromString(state.error ? TEXT("입장 자리가 없습니다. 다시 준비해 주세요.")
                                                          : TEXT("현재 인원이 모두 준비하면 5초 뒤 입장합니다.")));
}

void UWorldDungeonQueueUI::ToggleReady()
{
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->SetDungeonReady(!this->ownReady);
    }

}

#if !UE_BUILD_SHIPPING
bool UWorldDungeonQueueUI::ClickReadyForTest()
{
    if (!IsVisible() || !this->readyButton || !this->readyButton->GetCachedGeometry().GetLocalSize().Y)
    {
        return false;
    }

    return ClickSlateButtonForTest(this->readyButton);
}

#endif

void UWorldDungeonQueueUI::Leave()
{
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->LeaveDungeonMatch();
    }

}

