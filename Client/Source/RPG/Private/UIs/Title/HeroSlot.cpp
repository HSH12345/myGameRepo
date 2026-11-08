#include "UIs/Title/HeroSlot.h"
#include "UIs/RpgUITheme.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ButtonSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"

TSharedRef<SWidget> UHeroSlot::RebuildWidget()
{
    if (!this->selectionOutline)
    {
        BuildRow();
    }

    return Super::RebuildWidget();
}

void UHeroSlot::BuildRow()
{
    // Retain bound buttons/IDs; replace the nested card with a single left-aligned row.
    if (!this->selectButton || !this->enterGameButton || !this->characterNameText)
    {
        return;
    }

    this->selectButton->RemoveFromParent();
    this->enterGameButton->RemoveFromParent();
    this->characterNameText->RemoveFromParent();
    if (this->portraitImage)
    {
        this->portraitImage->RemoveFromParent();
    }
    else
    {
        this->portraitImage = this->WidgetTree->ConstructWidget<UImage>();
    }

    if (this->selectionText)
    {
        this->selectionText->SetVisibility(ESlateVisibility::Collapsed);
    }

    auto* height = this->WidgetTree->ConstructWidget<USizeBox>();
    height->SetHeightOverride(100);
    this->WidgetTree->RootWidget = height;
    auto* layers = this->WidgetTree->ConstructWidget<UOverlay>();
    height->SetContent(layers);
    this->selectionOutline = this->WidgetTree->ConstructWidget<UBorder>();
    auto* rowLayer = layers->AddChildToOverlay(this->selectionOutline);
    rowLayer->SetHorizontalAlignment(HAlign_Fill);
    rowLayer->SetVerticalAlignment(VAlign_Fill);
    this->selectionGlow = RpgUI::SelectionGlow(this->WidgetTree);
    auto* glowLayer = layers->AddChildToOverlay(this->selectionGlow);
    glowLayer->SetHorizontalAlignment(HAlign_Fill);
    glowLayer->SetVerticalAlignment(VAlign_Fill);
    this->selectionOutline->SetBrush(
        FSlateRoundedBoxBrush(FLinearColor(.025f, .023f, .02f, .65f), 2.f, FLinearColor(.70f, .54f, .28f), 1.f));
    this->selectionOutline->SetPadding(FMargin(12, 8));
    auto* row = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    this->selectionOutline->SetContent(row);
    auto style = this->selectButton->GetStyle();
    style.Normal.DrawAs = style.Hovered.DrawAs = style.Pressed.DrawAs = style.Disabled.DrawAs =
        ESlateBrushDrawType::NoDrawType;
    style.NormalPadding = style.PressedPadding = FMargin(0);
    this->selectButton->SetStyle(style);

    row->AddChildToHorizontalBox(this->selectButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* info = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    info->SetVisibility(ESlateVisibility::HitTestInvisible);
    this->selectButton->SetContent(info);
    if (auto* positionSlot = Cast<UButtonSlot>(info->Slot))
    {
        positionSlot->SetHorizontalAlignment(HAlign_Fill);
        positionSlot->SetVerticalAlignment(VAlign_Fill);
    }

    auto* portraitSize = this->WidgetTree->ConstructWidget<USizeBox>();
    portraitSize->SetWidthOverride(64);
    portraitSize->SetHeightOverride(80);
    portraitSize->SetContent(RpgUI::FitImage(this->WidgetTree, this->portraitImage));
    info->AddChildToHorizontalBox(portraitSize)->SetVerticalAlignment(VAlign_Center);
    auto column = [&](UTextBlock* text, float weight)
    {
        text->SetAutoWrapText(false);
        text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        RpgUI::Text(text, 16);
        auto* positionSlot = info->AddChildToHorizontalBox(text);
        FSlateChildSize size(ESlateSizeRule::Fill);
        size.Value = weight;
        positionSlot->SetSize(size);
        positionSlot->SetVerticalAlignment(VAlign_Center);
        positionSlot->SetPadding(FMargin(16, 0, 0, 0));
    };

    column(this->characterNameText, 2.5f);
    RpgUI::Text(this->characterNameText, 21);
    this->levelText = this->WidgetTree->ConstructWidget<UTextBlock>();
    column(this->levelText, 1.1f);
    this->raceText = this->WidgetTree->ConstructWidget<UTextBlock>();
    column(this->raceText, .8f);
    this->genderText = this->WidgetTree->ConstructWidget<UTextBlock>();
    column(this->genderText, .8f);
    auto* entrySize = this->WidgetTree->ConstructWidget<USizeBox>();
    entrySize->SetWidthOverride(88);
    entrySize->SetHeightOverride(40);
    entrySize->SetContent(this->enterGameButton);
    auto* entrySlot = row->AddChildToHorizontalBox(entrySize);
    entrySlot->SetVerticalAlignment(VAlign_Center);
    entrySlot->SetPadding(FMargin(16, 0, 0, 0));
    SetCharacterData(this->summary);
    SetSelected(this->isSelected);
}

void UHeroSlot::NativeConstruct()
{
    Super::NativeConstruct();
    RpgUI::FitTree(this);
    if (this->selectButton)
    {
        this->selectButton->OnClicked.AddUniqueDynamic(this, &UHeroSlot::HandleClick);
    }

    if (this->enterGameButton)
    {
        this->enterGameButton->OnClicked.AddUniqueDynamic(this, &UHeroSlot::HandleEnter);
    }

}

void UHeroSlot::NativeDestruct()
{
    if (this->selectButton)
    {
        this->selectButton->OnClicked.RemoveDynamic(this, &UHeroSlot::HandleClick);
    }

    if (this->enterGameButton)
    {
        this->enterGameButton->OnClicked.RemoveDynamic(this, &UHeroSlot::HandleEnter);
    }

    Super::NativeDestruct();
}

void UHeroSlot::SetCharacterData(const FCharacterSummary& character)
{
    this->summary = character;
    if (this->portraitImage)
    {
        this->portraitImage->SetBrush(RpgUI::Brush(character.gender == 1 ? TEXT("Female") : TEXT("Male")));
    }

    if (this->characterNameText)
    {
        this->characterNameText->SetText(FText::FromString(character.name));
    }

    const FString RACE = character.race == 0   ? TEXT("인간")
                         : character.race == 1 ? TEXT("오크")
                                               : FString::Printf(TEXT("Race %d"), character.race);
    const FString GENDER = character.gender == 0   ? TEXT("남성")
                           : character.gender == 1 ? TEXT("여성")
                                                   : FString::Printf(TEXT("Gender %d"), character.gender);
    if (this->levelText)
    {
        this->levelText->SetText(FText::FromString(FString::Printf(TEXT("레벨 %d"), character.level)));
    }

    if (this->raceText)
    {
        this->raceText->SetText(FText::FromString(RACE));
    }

    if (this->genderText)
    {
        this->genderText->SetText(FText::FromString(GENDER));
    }

    if (this->selectButton)
    {
        this->selectButton->SetIsEnabled(character.characterUnique != 0);
    }

}

void UHeroSlot::SetSelected(bool selected)
{
    this->isSelected = selected;
    if (this->selectionOutline)
    {
        this->selectionOutline->SetBrush(FSlateRoundedBoxBrush(
            FLinearColor(.025f, .023f, .02f, .55f), 2.f,
            selected ? FLinearColor(.78f, .60f, .28f) : FLinearColor::Transparent, selected ? 1.5f : 0.f));
    }

    if (this->selectionGlow)
    {
        this->selectionGlow->SetVisibility(selected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }

}

void UHeroSlot::HandleClick()
{
    if (this->summary.characterUnique != 0)
    {
        this->onSelected.Broadcast(this->summary.characterUnique);
    }

}

void UHeroSlot::SetEntryEnabled(bool enabled)
{
    if (this->enterGameButton)
    {
        this->enterGameButton->SetIsEnabled(enabled && this->summary.characterUnique > 0);
    }

}

void UHeroSlot::HandleEnter()
{
    if (this->summary.characterUnique > 0)
    {
        this->onEnterRequested.Broadcast(this->summary.characterUnique);
    }

}

