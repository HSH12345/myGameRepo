#include "World/WorldInventoryUI.h"
#include "Components/Border.h"
#include "UIs/RpgUITheme.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "World/GameSessionSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Tests/SlatePointerClick.h"

void UEconomySlotButton::Click()
{
    if (this->ownerUi)
    {
        if (this->offer)
        {
            this->ownerUi->BuyClicked(this->value);
        }
        else if (this->equipment)
        {
            this->ownerUi->EquipmentClicked(this->value);
        }
        else
        {
            this->ownerUi->SlotClicked(this->value);
        }

    }

}

void UWorldInventoryUI::NativeConstruct()
{
    Super::NativeConstruct();
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->onStatusChanged.AddUniqueDynamic(
        this, &UWorldInventoryUI::ShowTradeStatus);
    Refresh(); // The first Slate build may follow the shop snapshot's refresh.
}

void UWorldInventoryUI::NativeDestruct()
{
    StopPreviews();
    if (auto* instance = GetGameInstance())
    {
        if (auto* game = instance->GetSubsystem<UGameSessionSubsystem>())
        {
            game->onStatusChanged.RemoveDynamic(this, &UWorldInventoryUI::ShowTradeStatus);
        }

    }

    Super::NativeDestruct();
}

void UWorldInventoryUI::ShowTradeStatus(const FString& status)
{
    if (this->feedback)
    {
        this->feedback->SetText(FText::FromString(RpgUI::Caption(status)));
    }

    if (this->shopFeedback)
    {
        this->shopFeedback->SetText(FText::FromString(RpgUI::Caption(status)));
    }

}

void UWorldInventoryUI::Refresh()
{
    auto* game = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    const auto* catalog = game->GetItemCatalog();
    if (!this->summary || !catalog)
    {
        return;
    }

    const auto& state = game->GetEconomyState();
    this->shopPanel->SetVisibility(game->GetShopOffers().empty() ? ESlateVisibility::Collapsed
                                                                 : ESlateVisibility::Visible);
    this->shopGold->SetText(FText::FromString(FString::Printf(TEXT("소지금 %llu"), state.gold)));
    UpdateCharacterDetails();
    this->summary->SetText(FText::FromString(FString::Printf(TEXT("소지금 %llu  |  가방 %llu / 32%s"), state.gold,
                                                             static_cast<uint64>(state.items.size()),
                                                             game->EconomyBusy() ? TEXT("  확인 중...") : TEXT(""))));
    for (int32 i = 0; i < this->slots.Num(); ++i)
    {
        FString label;
        this->slotIcons[i]->SetVisibility(ESlateVisibility::Hidden);
        this->slots[i]->SetToolTip(nullptr);
        for (const auto& item : state.items)
        {
            if (item.bag == i)
            {
                const auto* templateValue = catalog->Find(item.type);
                label = FString::Printf(TEXT("%s%u"), item.equipped != 255 ? TEXT("◆ ") : TEXT(""), item.quantity);
                this->slotIcons[i]->SetBrush(RpgUI::Brush(RpgUI::ItemIcon(item.type)));
                this->slotIcons[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
                RpgUI::Tooltip(this->WidgetTree, this->slots[i],
                               RpgUI::ItemName(item.type, templateValue ? UTF8_TO_TCHAR(templateValue->name.c_str())
                                                                        : TEXT("알 수 없음")));
            }

        }

        this->labels[i]->SetText(FText::FromString(label));
        this->slots[i]->SetIsEnabled(!game->EconomyBusy());
        this->slotSelection[i]->SetVisibility(i == this->selected ? ESlateVisibility::HitTestInvisible
                                                                  : ESlateVisibility::Collapsed);
    }

    FString details = this->selected < 0 ? TEXT("아이템을 선택하세요.")
                                         : FString::Printf(TEXT("%d번 칸이 비어 있습니다."), this->selected + 1);
    for (const auto& item : state.items)
    {
        if (item.bag == this->selected)
        {
            const auto* templateValue = catalog->Find(item.type);
            details = FString::Printf(
                TEXT("%s ×%u | %d번 칸%s"),
                *RpgUI::ItemName(item.type,
                                 templateValue ? UTF8_TO_TCHAR(templateValue->name.c_str()) : TEXT("알 수 없음")),
                item.quantity, this->selected + 1, item.equipped != 255 ? TEXT(" | 장착 중") : TEXT(""));
            if (const auto EFFECT = catalog->consumables.find(item.type); EFFECT != catalog->consumables.end())
            {
                details +=
                    FString::Printf(TEXT(" | %s +%u | 재사용 %.1f초"), EFFECT->second.mana ? TEXT("MP") : TEXT("HP"),
                                    EFFECT->second.mana ? EFFECT->second.mana : EFFECT->second.heal,
                                    EFFECT->second.cooldownMs / 1000.f);
            }

            break;
        }

    }

    this->selection->SetText(FText::FromString(details));
    RpgUI::Tooltip(this->WidgetTree, this->selection, details);
    this->actions[4]->SetIsEnabled(true);
    const shared_world::economy::Item* selectedItem = nullptr;
    for (const auto& item : state.items)
    {
        if (item.bag == this->selected)
        {
            selectedItem = &item;
            break;
        }

    }

    this->actions[3]->SetIsEnabled(!game->EconomyBusy() && selectedItem &&
                                   catalog->consumables.count(selectedItem->type));
    this->actions[5]->SetIsEnabled(!game->EconomyBusy() && selectedItem &&
                                   catalog->consumables.count(selectedItem->type));
    const auto* selectedTemplate = selectedItem ? catalog->Find(selectedItem->type) : nullptr;
    this->actions[0]->SetIsEnabled(!game->EconomyBusy() && selectedTemplate && selectedTemplate->slot < 2 &&
                                   selectedItem->equipped == 255);
    this->actions[1]->SetIsEnabled(!game->EconomyBusy() && selectedItem && selectedItem->equipped < 2);
    this->actions[2]->SetIsEnabled(!game->EconomyBusy() && selectedItem && selectedItem->quantity > 1 &&
                                   selectedItem->equipped == 255);
    for (int32 i = 0; i < this->equipmentSlots.Num(); ++i)
    {
        FString label = i ? TEXT("의복\n비어 있음") : TEXT("주무기\n비어 있음");
        int32 bag = -1;
        for (const auto& item : state.items)
        {
            if (item.equipped == i)
            {
                const auto* templateValue = catalog->Find(item.type);
                label = (i ? TEXT("의복\n") : TEXT("주무기\n")) +
                        RpgUI::ItemName(item.type, templateValue ? UTF8_TO_TCHAR(templateValue->name.c_str())
                                                                 : TEXT("알 수 없음"));
                bag = item.bag;
                break;
            }

        }

        this->equipmentLabels[i]->SetText(FText::FromString(label));
        this->equipmentSlots[i]->value = bag;
        this->gearIcons[i]->SetVisibility(ESlateVisibility::Hidden);
        for (const auto& item : state.items)
        {
            if (item.equipped == i)
            {
                this->gearIcons[i]->SetBrush(RpgUI::Brush(RpgUI::ItemIcon(item.type)));
                this->gearIcons[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
                break;
            }

        }

        this->equipmentSlots[i]->SetIsEnabled(!game->EconomyBusy() && bag >= 0);
    }

    this->offers->ClearChildren();
    for (const auto& offer : game->GetShopOffers())
    {
        auto* button = this->WidgetTree->ConstructWidget<UEconomySlotButton>();
        button->ownerUi = this;
        button->value = offer.id;
        button->offer = true;
        button->OnClicked.AddDynamic(button, &UEconomySlotButton::Click);
        button->SetIsEnabled(!game->EconomyBusy());
        const auto* item = catalog->Find(offer.item);
        auto* row = this->WidgetTree->ConstructWidget<UVerticalBox>();
        auto line = [&](const FString& text, int32 size)
        {
            auto* label = this->WidgetTree->ConstructWidget<UTextBlock>();
            label->SetText(FText::FromString(text));
            auto font = label->GetFont();
            font.Size = size;
            label->SetFont(font);
            RpgUI::Text(label, size);
            label->SetAutoWrapText(false);
            label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            row->AddChildToVerticalBox(label)->SetPadding(FMargin(4, 2));
        };

        line(RpgUI::ItemName(offer.item, item ? UTF8_TO_TCHAR(item->name.c_str()) : TEXT("알 수 없음")), 15);
        FString effect = item && item->slot < 2
                             ? FString::Printf(TEXT("공격 +%u  방어 +%u"), item->attack, item->defense)
                             : TEXT("중첩 가능한 재료");
        if (const auto IT = catalog->consumables.find(offer.item); IT != catalog->consumables.end())
        {
            effect =
                FString::Printf(TEXT("%s +%u  |  %.1fs"), IT->second.mana ? TEXT("MP") : TEXT("HP"),
                                IT->second.mana ? IT->second.mana : IT->second.heal, IT->second.cooldownMs / 1000.f);
        }

        line(effect, 12);
        auto* height = this->WidgetTree->ConstructWidget<USizeBox>();
        height->SetMinDesiredHeight(76);
        auto* offerRow = this->WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* icon = this->WidgetTree->ConstructWidget<UImage>();
        icon->SetBrush(RpgUI::Brush(RpgUI::ItemIcon(offer.item)));
        icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* iconSize = this->WidgetTree->ConstructWidget<USizeBox>();
        iconSize->SetWidthOverride(56);
        iconSize->SetHeightOverride(56);
        iconSize->SetContent(RpgUI::FitImage(this->WidgetTree, icon));
        offerRow->AddChildToHorizontalBox(iconSize)->SetVerticalAlignment(VAlign_Center);
        offerRow->AddChildToHorizontalBox(row)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* buyLabel = this->WidgetTree->ConstructWidget<UTextBlock>();
        buyLabel->SetText(FText::FromString(FString::Printf(TEXT("%llu 금화\n구매"), offer.price)));
        RpgUI::Text(buyLabel, 12);
        buyLabel->SetAutoWrapText(false);
        buyLabel->SetJustification(ETextJustify::Center);
        auto* buySize = this->WidgetTree->ConstructWidget<USizeBox>();
        buySize->SetWidthOverride(64);
        buySize->SetContent(buyLabel);
        offerRow->AddChildToHorizontalBox(buySize)->SetVerticalAlignment(VAlign_Center);
        RpgUI::Tooltip(this->WidgetTree, button,
                       RpgUI::ItemName(offer.item, item ? UTF8_TO_TCHAR(item->name.c_str()) : TEXT("알 수 없음")) +
                           TEXT("\n") + effect);
        height->SetContent(offerRow);
        RpgUI::Button(button);
        auto style = button->GetStyle();
        style.NormalPadding = style.PressedPadding = FMargin(8, 4);
        button->SetStyle(style);
        button->SetContent(height);
        RpgUI::FitButton(this->WidgetTree, button);
        this->offers->AddChildToVerticalBox(button)->SetPadding(FMargin(0, 0, 4, 8));
    }

}

void UWorldInventoryUI::EquipmentClicked(int32 bag)
{
    if (bag >= 0)
    {
        this->selected = bag;
        Refresh();
    }

}

void UWorldInventoryUI::SlotClicked(int32 value)
{
    auto* game = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    if (this->selected >= 0 && this->selected != value)
    {
        if (game->MoveBagItem(this->selected, value))
        {
            this->selected = -1;
        }
        else
        {
            this->selected = value;
        }

    }
    else
    {
        this->selected = this->selected == value ? -1 : value;
    }

    Refresh();
}

void UWorldInventoryUI::BuyClicked(int32 value)
{
    const bool SHIFT = GetOwningPlayer() && (GetOwningPlayer()->IsInputKeyDown(EKeys::LeftShift) ||
                                             GetOwningPlayer()->IsInputKeyDown(EKeys::RightShift));
    auto* game = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    int32 quantity = 1;
    for (const auto& offer : game->GetShopOffers())
    {
        if (offer.id == value)
        {
            quantity = SHIFT ? FMath::Min(10, static_cast<int32>(offer.limit)) : 1;
        }

    }

    game->BuyOffer(value, quantity);
}

void UWorldInventoryUI::EquipClicked()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->EquipBagItem(this->selected);
}

void UWorldInventoryUI::UseClicked()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->UseBagItem(this->selected);
}

void UWorldInventoryUI::RegisterClicked()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->RegisterPotion(this->selected);
}

void UWorldInventoryUI::UnequipClicked()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->UnequipBagItem(this->selected);
}

void UWorldInventoryUI::SplitClicked()
{
    auto* game = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    const auto& state = game->GetEconomyState();
    for (const auto& item : state.items)
    {
        if (item.bag == this->selected && item.quantity > 1)
        {
            for (int32 bagIndex = 0; bagIndex < 32; ++bagIndex)
            {
                bool used = false;
                for (const auto& other : state.items)
                {
                    if (other.bag == bagIndex)
                    {
                        used = true;
                    }

                }

                if (!used)
                {
                    game->SplitBagItem(this->selected, item.quantity / 2, bagIndex);
                    return;
                }

            }

        }

    }

}

void UWorldInventoryUI::CloseClicked()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->CloseShop();
}

#if !UE_BUILD_SHIPPING
bool UWorldInventoryUI::ClickOfferForTest(int32 offer)
{
    for (auto* child : this->offers->GetAllChildren())
    {
        if (auto* button = Cast<UEconomySlotButton>(child); button && button->value == offer && button->GetIsEnabled())
        {
            return ClickSlateButtonForTest(button) &&
                   GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->EconomyBusy();
        }

    }

    UE_LOG(LogTemp, Error, TEXT("ECONOMY_SMOKE offer_missing id=%d buttons=%d"), offer,
           this->offers->GetChildrenCount());
    return false;
}

bool UWorldInventoryUI::ClickActionForTest(int32 bag, int32 action)
{
    if (!this->slots.IsValidIndex(bag) || !this->actions.IsValidIndex(action))
    {
        return false;
    }

    if (this->selected >= 0 && !ClickSlateButtonForTest(this->slots[this->selected]))
    {
        return false;
    }

    if (!ClickSlateButtonForTest(this->slots[bag]) || !ClickSlateButtonForTest(this->actions[action]))
    {
        return false;
    }

    return action == 5 || GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->EconomyBusy();
}

bool UWorldInventoryUI::ClickMoveForTest(int32 from, int32 to)
{
    if (!this->slots.IsValidIndex(from) || !this->slots.IsValidIndex(to))
    {
        return false;
    }

    if (this->selected >= 0 && !ClickSlateButtonForTest(this->slots[this->selected]))
    {
        return false;
    }

    if (!ClickSlateButtonForTest(this->slots[from]) || !ClickSlateButtonForTest(this->slots[to]))
    {
        return false;
    }

    return GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->EconomyBusy();
}

#endif

#if !UE_BUILD_SHIPPING
void UWorldInventoryUI::LongTextForTest()
{
    this->longTextFixture = true;
    this->characterName->SetText(
        FText::FromString(TEXT("아주 긴 캐릭터 이름과 장비 설명을 표시해도 프레임 밖으로 나가지 않아야 합니다")));
    this->feedback->SetText(FText::FromString(
        TEXT("매우 긴 거래 결과 메시지도 창의 내용 영역 안에서 표시합니다. 아이템 구매와 장착 상태를 확인하세요.")));
}

bool UWorldInventoryUI::FramedTextInsideForTest() const
{
    bool fits = true;
    int32 count = 0;
    this->WidgetTree->ForEachWidget(
        [&](UWidget* widget)
        {
            auto* text = Cast<UTextBlock>(widget);
            if (!text || !text->IsVisible() || text->GetCachedGeometry().GetLocalSize().IsNearlyZero())
            {
                return;
            }

            for (auto* parent = text->GetParent(); parent; parent = parent->GetParent())
            {
                if (Cast<UBorder>(parent) || Cast<UButton>(parent))
                {
                    const auto& outer = parent->GetCachedGeometry();
                    const auto& inner = text->GetCachedGeometry();
                    const auto A = outer.AbsoluteToLocal(inner.GetAbsolutePosition()),
                               B = outer.AbsoluteToLocal(inner.LocalToAbsolute(inner.GetLocalSize()));
                    fits &= A.X >= -.5 && A.Y >= -.5 && B.X <= outer.GetLocalSize().X + .5 &&
                            B.Y <= outer.GetLocalSize().Y + .5;
                    ++count;
                    break;
                }

            }

        });

    return fits && count > 10;
}

#endif
