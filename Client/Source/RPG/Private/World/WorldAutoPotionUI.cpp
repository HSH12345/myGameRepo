#include "World/WorldControlHUD.h"
#include "World/GameSessionSubsystem.h"
#include "UIs/RpgUITheme.h"
#include "UIs/RpgArtBorder.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Tests/SlatePointerClick.h"

void UWorldControlHUD::BuildAutoPotions(UCanvasPanel* canvas)
{
    auto* frame = this->WidgetTree->ConstructWidget<URpgArtBorder>();
    RpgUI::Frame(frame);
    frame->SetPadding(FMargin(8));
    auto* position = canvas->AddChildToCanvas(frame);
    position->SetAnchors(FAnchors(0, 1));
    position->SetAlignment(FVector2D(0, 1));
    position->SetPosition(FVector2D(570, -16));
    position->SetSize(FVector2D(172, 82));
    this->pointerRegions.Add(frame);
    auto* row = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    frame->SetContent(row);
    for (int32 i = 0; i < 2; ++i)
    {
        auto* button = this->WidgetTree->ConstructWidget<UWorldQuickSlotButton>();
        button->ownerHud = this;
        button->potionSlot = true;
        button->index = i;
        RpgUI::Button(button, false, true);
        button->OnClicked.AddDynamic(button, &UWorldQuickSlotButton::Activate);
        row->AddChildToHorizontalBox(button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* body = this->WidgetTree->ConstructWidget<UVerticalBox>();
        button->SetContent(body);
        auto* icon = this->WidgetTree->ConstructWidget<UImage>();
        icon->SetBrush(RpgUI::Brush(i ? TEXT("Mana") : TEXT("Health")));
        auto* iconSize = this->WidgetTree->ConstructWidget<USizeBox>();
        iconSize->SetHeightOverride(32);
        iconSize->SetContent(RpgUI::FitImage(this->WidgetTree, icon));
        body->AddChild(iconSize);
        this->potionIcons.Add(icon);
        auto* label = this->WidgetTree->ConstructWidget<UTextBlock>();
        RpgUI::Text(label, 10);
        label->SetJustification(ETextJustify::Center);
        label->SetVisibility(ESlateVisibility::HitTestInvisible);
        body->AddChild(label);
        this->potionLabels.Add(label);
    }

    this->potionPicker = this->WidgetTree->ConstructWidget<URpgArtBorder>();
    RpgUI::Frame(this->potionPicker);
    this->potionPicker->SetPadding(FMargin(20, 26));
    auto* pickerPosition = canvas->AddChildToCanvas(this->potionPicker);
    pickerPosition->SetAnchors(FAnchors(0, 1));
    pickerPosition->SetAlignment(FVector2D(0, 1));
    pickerPosition->SetPosition(FVector2D(570, -108));
    pickerPosition->SetSize(FVector2D(340, 280));
    pickerPosition->SetZOrder(40);
    this->pointerRegions.Add(this->potionPicker);
    auto* scroll = this->WidgetTree->ConstructWidget<UScrollBox>();
    this->potionPicker->SetContent(scroll);
    this->potionChoices = this->WidgetTree->ConstructWidget<UVerticalBox>();
    scroll->AddChild(this->potionChoices);
    this->potionPicker->SetVisibility(ESlateVisibility::Collapsed);
    RefreshSkillBook();
}

void UWorldControlHUD::TogglePotionPicker(int32 index)
{
    if (!this->potionPicker || index < 0 || index > 1)
    {
        return;
    }

    if (this->potionPicker->IsVisible() && this->pickerPotion == index)
    {
        this->potionPicker->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    this->pickerPotion = index;
    this->potionChoices->ClearChildren();
    this->registerButtons.Empty();
    auto text = [&](const FString& value)
    {
        auto* label = this->WidgetTree->ConstructWidget<UTextBlock>();
        label->SetText(FText::FromString(value));
        RpgUI::Text(label, 14);
        label->SetAutoWrapText(true);
        label->SetVisibility(ESlateVisibility::HitTestInvisible);
        return label;
    };

    this->potionChoices
        ->AddChildToVerticalBox(
            text(index ? TEXT("MP 자동 포션 · 50% 미만 사용") : TEXT("HP 자동 포션 · 50% 이하 사용")))
        ->SetPadding(FMargin(0, 0, 0, 12));
    auto add = [&](uint32 type, const FString& caption)
    {
        auto* button = this->WidgetTree->ConstructWidget<UWorldQuickSlotButton>();
        button->ownerHud = this;
        button->potionRegister = true;
        button->binding = type;
        RpgUI::Button(button);
        button->SetContent(text(caption));
        button->OnClicked.AddDynamic(button, &UWorldQuickSlotButton::Activate);
        this->potionChoices->AddChildToVerticalBox(button)->SetPadding(FMargin(0, 3));
        this->registerButtons.Add(button);
        RpgUI::FitButton(this->WidgetTree, button);
    };

    auto* session = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    const auto* catalog = session->GetItemCatalog();
    int32 count = 0;
    if (catalog)
    {
        for (const auto& pair : catalog->consumables)
        {
            if (bool(pair.second.mana) != (index == 1))
            {
                continue;
            }

            uint32 quantity = 0;
            for (const auto& item : session->GetEconomyState().items)
            {
                if (item.type == pair.first)
                {
                    quantity += item.quantity;
                }

            }

            if (!quantity)
            {
                continue;
            }

            add(pair.first, FString::Printf(TEXT("%s ×%u"), *RpgUI::ItemName(pair.first, TEXT("포션")), quantity));
            ++count;
        }

    }

    if (!count)
    {
        this->potionChoices->AddChild(text(TEXT("소지한 포션이 없습니다. 상인에게 구매한 뒤 등록하세요.")));
    }

    add(0, TEXT("등록 해제 · 자동 사용 끄기"));
    add(UINT32_MAX, TEXT("닫기"));
    this->potionPicker->SetVisibility(ESlateVisibility::Visible);
}

void UWorldControlHUD::RegisterPotionFromPicker(uint32 type)
{
    if (type != UINT32_MAX &&
        !GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->RegisterPotionType(this->pickerPotion, type))
    {
        return;
    }

    if (this->potionPicker)
    {
        this->potionPicker->SetVisibility(ESlateVisibility::Collapsed);
    }

    RefreshSkillBook();
}

#if !UE_BUILD_SHIPPING
bool UWorldControlHUD::ClickPotionRegisterForTest(int32 index, uint32 type)
{
    if (!this->potionPicker || !this->potionPicker->IsVisible() || this->pickerPotion != index)
    {
        TogglePotionPicker(index);
    }

    for (auto button : this->registerButtons)
    {
        if (button->binding == type)
        {
            return ClickSlateButtonForTest(button);
        }

    }

    return false;
}

#endif
