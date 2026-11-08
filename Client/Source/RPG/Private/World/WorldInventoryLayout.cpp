#include "World/WorldInventoryUI.h"
#include "UIs/RpgUITheme.h"
#include "UIs/RpgArtBorder.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

namespace
{
struct FInventoryLayout
{
    UWidgetTree* tree;

    UTextBlock* Text(const FString& value, int32 size = 14)
    {
        auto* label = this->tree->ConstructWidget<UTextBlock>();
        label->SetText(FText::FromString(value));
        auto font = label->GetFont();
        font.Size = size;
        label->SetFont(font);
        label->SetAutoWrapText(true);
        RpgUI::Text(label, size);
        label->SetClipping(EWidgetClipping::ClipToBounds);
        label->SetVisibility(ESlateVisibility::HitTestInvisible);
        return label;
    }

    UBorder* Frame(UWidget* content, FMargin padding = FMargin(8))
    {
        auto* border = this->tree->ConstructWidget<URpgArtBorder>();
        RpgUI::Frame(border);
        border->SetPadding(padding);
        if (content)
        {
            border->SetContent(content);
        }

        return border;
    }

    USizeBox* Size(UWidget* content, float width, float height)
    {
        auto* box = this->tree->ConstructWidget<USizeBox>();
        if (width > 0)
        {
            box->SetWidthOverride(width);
        }

        if (height > 0)
        {
            box->SetHeightOverride(height);
        }

        box->SetContent(content);
        return box;
    }

    void Line(UVerticalBox* column, UWidget* content, float bottom = 6, bool fill = false)
    {
        auto* slot = column->AddChildToVerticalBox(content);
        slot->SetPadding(FMargin(0, 0, 0, bottom));
        if (fill)
        {
            slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }

    }

};

}

TSharedRef<SWidget> UWorldInventoryUI::RebuildWidget()
{
    StopPreviews();
    this->slots.Empty();
    this->labels.Empty();
    this->actions.Empty();
    this->equipmentSlots.Empty();
    this->equipmentLabels.Empty();
    this->selected = -1;
    this->slotIcons.Empty();
    this->slotSelection.Empty();
    this->gearIcons.Empty();
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* scale = this->WidgetTree->ConstructWidget<UScaleBox>();
    scale->SetStretch(EStretch::ScaleToFit);
    scale->SetStretchDirection(EStretchDirection::DownOnly);
    scale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    this->WidgetTree->RootWidget = scale;
    auto* stage = this->WidgetTree->ConstructWidget<USizeBox>();
    stage->SetWidthOverride(1140);
    stage->SetHeightOverride(920);
    scale->SetContent(stage);
    auto* canvas = this->WidgetTree->ConstructWidget<UCanvasPanel>();
    stage->SetContent(canvas);
    FInventoryLayout ui{this->WidgetTree};
    auto addWindow = [&](float width, float right)
    {
        auto* column = this->WidgetTree->ConstructWidget<UVerticalBox>();
        auto* frame = ui.Frame(column, FMargin(22, 30, 22, 24));
        auto* position = canvas->AddChildToCanvas(frame);
        position->SetAnchors(FAnchors(1, .5f));
        position->SetAlignment(FVector2D(1, .5f));
        position->SetPosition(FVector2D(-right, 0));
        position->SetSize(FVector2D(width, 880));
        return column;
    };

    auto* inventory = addWindow(600, 16);
    auto* titleRow = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    ui.Line(inventory, titleRow);
    this->header = ui.Text(TEXT("소지품"), 22);
    titleRow->AddChildToHorizontalBox(this->header)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* close = this->WidgetTree->ConstructWidget<UButton>();
    close->SetContent(ui.Text(TEXT("X"), 16));
    close->SetBackgroundColor(FLinearColor(.08f, .14f, .2f));
    close->OnClicked.AddDynamic(this, &UWorldInventoryUI::CloseClicked);
    RpgUI::Button(close, true, true);
    titleRow->AddChild(close);
    this->characterName = ui.Text(TEXT("영웅"), 18);
    this->characterName->SetAutoWrapText(false);
    this->characterName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    ui.Line(inventory, this->characterName);
    auto* characterRow = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    ui.Line(inventory, ui.Size(characterRow, 0, 220), 10);
    auto* statsColumn = this->WidgetTree->ConstructWidget<UVerticalBox>();
    characterRow->AddChildToHorizontalBox(ui.Size(ui.Frame(statsColumn, FMargin(16, 28, 16, 28)), 128, 0))
        ->SetPadding(FMargin(0, 0, 8, 0));
    ui.Line(statsColumn, ui.Text(TEXT("능력치"), 15), 12);
    this->characterStats = ui.Text(TEXT(""), 12);
    this->characterStats->SetAutoWrapText(false);
    this->characterStats->SetClipping(EWidgetClipping::ClipToBounds);
    auto* statsFit = this->WidgetTree->ConstructWidget<UScaleBox>();
    statsFit->SetStretch(EStretch::ScaleToFit);
    statsFit->SetStretchDirection(EStretchDirection::DownOnly);
    statsFit->SetContent(this->characterStats);
    statsFit->SetClipping(EWidgetClipping::ClipToBounds);
    ui.Line(statsColumn, statsFit, 0, true);
    this->characterImage = this->WidgetTree->ConstructWidget<UImage>();
    this->characterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    characterRow->AddChildToHorizontalBox(ui.Frame(RpgUI::FitImage(this->WidgetTree, this->characterImage), FMargin(0)))
        ->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* equipmentColumn = this->WidgetTree->ConstructWidget<UVerticalBox>();
    auto* gearSlot = characterRow->AddChildToHorizontalBox(ui.Size(equipmentColumn, 142, 0));
    gearSlot->SetPadding(FMargin(8, 0, 0, 0));
    ui.Line(equipmentColumn, ui.Text(TEXT("장비"), 14), 10);
    for (int32 i = 0; i < 2; ++i)
    {
        auto* button = this->WidgetTree->ConstructWidget<UEconomySlotButton>();
        button->ownerUi = this;
        button->equipment = true;
        button->OnClicked.AddDynamic(button, &UEconomySlotButton::Click);
        button->SetBackgroundColor(FLinearColor(.08f, .14f, .2f));
        auto* label = ui.Text(i ? TEXT("의복\n비어 있음") : TEXT("주무기\n비어 있음"), 13);
        label->SetJustification(ETextJustify::Center);
        label->SetAutoWrapText(false);
        RpgUI::Button(button, false, true);
        auto gearStyle = button->GetStyle();
        gearStyle.NormalPadding = gearStyle.PressedPadding = FMargin(12);
        button->SetStyle(gearStyle);
        auto* gearColumn = this->WidgetTree->ConstructWidget<UVerticalBox>();
        button->SetContent(gearColumn);
        auto* icon = this->WidgetTree->ConstructWidget<UImage>();
        icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        gearColumn->AddChild(ui.Size(RpgUI::FitImage(this->WidgetTree, icon), 0, 48));
        gearColumn->AddChild(label);
        this->gearIcons.Add(icon);
        ui.Line(equipmentColumn, ui.Size(button, 0, 84), 8);
        this->equipmentSlots.Add(button);
        this->equipmentLabels.Add(label);
    }

    ui.Line(inventory, ui.Text(TEXT("가방 / 32칸"), 16));
    auto* grid = this->WidgetTree->ConstructWidget<UUniformGridPanel>();
    grid->SetSlotPadding(FMargin(2));
    ui.Line(inventory, grid);
    for (int32 i = 0; i < 32; ++i)
    {
        auto* button = this->WidgetTree->ConstructWidget<UEconomySlotButton>();
        button->ownerUi = this;
        button->value = i;
        button->OnClicked.AddDynamic(button, &UEconomySlotButton::Click);
        RpgUI::Button(button, false, true);
        auto* overlay = this->WidgetTree->ConstructWidget<UOverlay>();
        button->SetContent(overlay);
        auto* icon = this->WidgetTree->ConstructWidget<UImage>();
        icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* imagePosition = overlay->AddChildToOverlay(RpgUI::FitImage(this->WidgetTree, icon));
        imagePosition->SetPadding(FMargin(5));
        imagePosition->SetHorizontalAlignment(HAlign_Fill);
        imagePosition->SetVerticalAlignment(VAlign_Fill);
        this->slotIcons.Add(icon);
        auto* label = ui.Text(TEXT(""), 11);
        label->SetAutoWrapText(false);
        label->SetJustification(ETextJustify::Right);
        auto* captionPosition = overlay->AddChildToOverlay(label);
        captionPosition->SetHorizontalAlignment(HAlign_Fill);
        captionPosition->SetVerticalAlignment(VAlign_Bottom);
        captionPosition->SetPadding(FMargin(0, 0, 3, 3));
        auto* cell = this->WidgetTree->ConstructWidget<UOverlay>();
        auto* cellSlot = cell->AddChildToOverlay(button);
        cellSlot->SetHorizontalAlignment(HAlign_Fill);
        cellSlot->SetVerticalAlignment(VAlign_Fill);
        auto* glow = RpgUI::SelectionGlow(this->WidgetTree);
        auto* glowSlot = cell->AddChildToOverlay(glow);
        glowSlot->SetHorizontalAlignment(HAlign_Fill);
        glowSlot->SetVerticalAlignment(VAlign_Fill);
        this->slotSelection.Add(glow);
        grid->AddChildToUniformGrid(ui.Size(cell, 66, 54), i / 8, i % 8);
        this->slots.Add(button);
        this->labels.Add(label);
    }

    this->selection = ui.Text(TEXT("아이템을 선택하세요."), 13);
    this->selection->SetAutoWrapText(false);
    this->selection->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    this->selection->SetClipping(EWidgetClipping::ClipToBounds);
    ui.Line(inventory, ui.Size(this->selection, 0, 32));
    auto* actionRow = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    ui.Line(inventory, actionRow);
    auto addAction = [&](const TCHAR* label)
    {
        auto* button = this->WidgetTree->ConstructWidget<UButton>();
        auto* caption = ui.Text(label, 12);
        caption->SetJustification(ETextJustify::Center);
        button->SetBackgroundColor(FLinearColor(.08f, .14f, .2f));
        RpgUI::Button(button);
        button->SetContent(caption);
        auto* buttonPosition = actionRow->AddChildToHorizontalBox(ui.Size(button, 0, 52));
        buttonPosition->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        buttonPosition->SetPadding(FMargin(2));
        this->actions.Add(button);
        return button;
    };

    addAction(TEXT("장착"))->OnClicked.AddDynamic(this, &UWorldInventoryUI::EquipClicked);
    addAction(TEXT("해제"))->OnClicked.AddDynamic(this, &UWorldInventoryUI::UnequipClicked);
    addAction(TEXT("나누기"))->OnClicked.AddDynamic(this, &UWorldInventoryUI::SplitClicked);
    addAction(TEXT("사용"))->OnClicked.AddDynamic(this, &UWorldInventoryUI::UseClicked);
    // Keep the existing test/action indices stable; the visible close action lives in the title bar.
    this->actions.Add(close);
    addAction(TEXT("등록"))->OnClicked.AddDynamic(this, &UWorldInventoryUI::RegisterClicked);
    ui.Line(inventory, ui.Text(TEXT("아이템 → 빈칸: 이동 / 합치기 · I: 가방 닫기"), 11));
    this->feedback = ui.Text(TEXT("아이템을 선택하세요."), 12);
    this->feedback->SetAutoWrapText(false);
    this->feedback->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    ui.Line(inventory, ui.Size(this->feedback, 0, 28), 4);
    this->summary = ui.Text(TEXT("소지금 0"), 16);
    ui.Line(inventory, this->summary, 0);

    auto* shop = addWindow(480, 628);
    this->shopPanel = shop->GetParent();
    auto* shopTitle = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    ui.Line(shop, shopTitle, 12);
    auto* shopHeading = ui.Text(TEXT("마을 상점"), 22);
    shopHeading->SetAutoWrapText(false);
    shopTitle->AddChildToHorizontalBox(shopHeading)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* shopClose = this->WidgetTree->ConstructWidget<UButton>();
    shopClose->SetContent(ui.Text(TEXT("X"), 16));
    shopClose->SetBackgroundColor(FLinearColor(.08f, .14f, .2f));
    shopClose->OnClicked.AddDynamic(this, &UWorldInventoryUI::CloseClicked);
    RpgUI::Button(shopClose, true, true);
    shopTitle->AddChild(shopClose);
    auto* shopBody = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    ui.Line(shop, shopBody, 12, true);
    auto* merchant = this->WidgetTree->ConstructWidget<UVerticalBox>();
    shopBody->AddChildToHorizontalBox(ui.Size(ui.Frame(merchant), 136, 0))->SetPadding(FMargin(0, 0, 10, 0));
    this->merchantImage = this->WidgetTree->ConstructWidget<UImage>();
    this->merchantImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    ui.Line(merchant, ui.Size(RpgUI::FitImage(this->WidgetTree, this->merchantImage), 0, 220), 12);
    this->merchantName = ui.Text(TEXT("마을 상인"), 16);
    ui.Line(merchant, this->merchantName, 16);
    ui.Line(merchant, ui.Text(TEXT("장비 / 물약"), 13), 24);
    ui.Line(merchant, ui.Text(TEXT("클릭: 1개 구매\nShift + 클릭: 최대 10개"), 12), 18);
    ui.Line(merchant, ui.Text(TEXT("이 상인은 아이템을 매입하지 않습니다."), 12));
    auto* scroll = this->WidgetTree->ConstructWidget<UScrollBox>();
    shopBody->AddChildToHorizontalBox(scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    this->offers = this->WidgetTree->ConstructWidget<UVerticalBox>();
    scroll->AddChild(this->offers);
    this->shopFeedback = ui.Text(TEXT("구매할 상품을 선택하세요."), 13);
    ui.Line(shop, ui.Size(ui.Frame(this->shopFeedback), 0, 68), 10);
    this->shopGold = ui.Text(TEXT("소지금 0"), 16);
    ui.Line(shop, this->shopGold, 0);
    this->shopPanel->SetVisibility(ESlateVisibility::Collapsed);
    TArray<UButton*> themeButtons;
    this->WidgetTree->ForEachWidget(
        [&](UWidget* widget)
        {
            if (auto* button = Cast<UButton>(widget))
            {
                themeButtons.Add(button);
            }

        });

    for (auto* button : themeButtons)
    {
        RpgUI::FitButton(this->WidgetTree, button);
    }

    return Super::RebuildWidget();
}

