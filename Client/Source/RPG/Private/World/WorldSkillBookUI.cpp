#include "World/WorldControlHUD.h"
#include "World/GameSessionSubsystem.h"
#include "UIs/RpgUITheme.h"
#include "UIs/RpgArtBorder.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Tests/SlatePointerClick.h"
#include "Engine/GameInstance.h"

void UWorldControlHUD::BuildSkillBook(UCanvasPanel* canvas)
{
    this->quickSlotPicker = this->WidgetTree->ConstructWidget<URpgArtBorder>();
    RpgUI::Frame(this->quickSlotPicker);
    this->quickSlotPicker->SetPadding(FMargin(26, 30));
    auto* position = canvas->AddChildToCanvas(this->quickSlotPicker);
    position->SetAnchors(FAnchors(.5f, .5f));
    position->SetAlignment(FVector2D(.5f, .5f));
    position->SetPosition(FVector2D(0, -30));
    position->SetSize(FVector2D(880, 620));
    position->SetZOrder(30);
    this->pointerRegions.Add(this->quickSlotPicker);
    auto* body = this->WidgetTree->ConstructWidget<UVerticalBox>();
    this->quickSlotPicker->SetContent(body);
    auto text = [&](const FString& value, int32 size)
    {
        auto* label = this->WidgetTree->ConstructWidget<UTextBlock>();
        label->SetText(FText::FromString(value));
        RpgUI::Text(label, size);
        label->SetAutoWrapText(false);
        label->SetVisibility(ESlateVisibility::HitTestInvisible);
        return label;
    };

    this->pickerCaption = text(TEXT("스킬"), 22);
    body->AddChildToVerticalBox(this->pickerCaption)->SetPadding(FMargin(0, 0, 0, 12));
    body->AddChildToVerticalBox(text(TEXT("배운 스킬을 클릭한 뒤 하단 퀵슬롯을 클릭하여 등록합니다."), 14))
        ->SetPadding(FMargin(0, 0, 0, 14));
    auto* grid = this->WidgetTree->ConstructWidget<UUniformGridPanel>();
    grid->SetSlotPadding(FMargin(4));
    body->AddChild(grid);
    auto* game = GetGameInstance();
    const auto* catalog = game ? game->GetSubsystem<UGameSessionSubsystem>()->GetSkillCatalog() : nullptr;
    if (catalog)
    {
        for (const auto& pair : catalog->definitions)
        {
            const auto& skill = pair.second;
            const int32 I = this->bindingButtons.Num();
            auto* line = this->WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* height = this->WidgetTree->ConstructWidget<USizeBox>();
            height->SetHeightOverride(70);
            height->SetContent(line);
            grid->AddChildToUniformGrid(height, I / 2, I % 2);
            auto* cell = this->WidgetTree->ConstructWidget<UOverlay>();
            line->AddChildToHorizontalBox(cell)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* button = this->WidgetTree->ConstructWidget<UWorldQuickSlotButton>();
            button->ownerHud = this;
            button->bindingOption = true;
            button->binding = skill.id;
            RpgUI::Button(button, false, true);
            button->OnClicked.AddDynamic(button, &UWorldQuickSlotButton::Activate);
            auto* buttonPosition = cell->AddChildToOverlay(button);
            buttonPosition->SetHorizontalAlignment(HAlign_Fill);
            buttonPosition->SetVerticalAlignment(VAlign_Fill);
            auto* contents = this->WidgetTree->ConstructWidget<UHorizontalBox>();
            button->SetContent(contents);
            auto* icon = this->WidgetTree->ConstructWidget<UImage>();
            icon->SetBrush(RpgUI::Brush(RpgUI::SkillIcon(skill.id)));
            auto* iconSize = this->WidgetTree->ConstructWidget<USizeBox>();
            iconSize->SetWidthOverride(44);
            iconSize->SetHeightOverride(44);
            iconSize->SetContent(RpgUI::FitImage(this->WidgetTree, icon));
            contents->AddChild(iconSize);
            auto* label = text(UTF8_TO_TCHAR(skill.name.c_str()), 13);
            auto* captionPosition = contents->AddChildToHorizontalBox(label);
            captionPosition->SetPadding(FMargin(8, 0));
            captionPosition->SetVerticalAlignment(VAlign_Center);
            this->skillLabels.Add(label);
            auto* glow = RpgUI::SelectionGlow(this->WidgetTree);
            auto* glowPosition = cell->AddChildToOverlay(glow);
            glowPosition->SetHorizontalAlignment(HAlign_Fill);
            glowPosition->SetVerticalAlignment(VAlign_Fill);
            this->skillGlows.Add(glow);
            this->bindingButtons.Add(button);
            auto* learn = this->WidgetTree->ConstructWidget<UWorldQuickSlotButton>();
            learn->ownerHud = this;
            learn->train = true;
            learn->binding = skill.id;
            RpgUI::Button(learn);
            learn->OnClicked.AddDynamic(learn, &UWorldQuickSlotButton::Activate);
            auto* learnText = text(TEXT("배우기"), 13);
            learn->SetContent(learnText);
            this->trainLabels.Add(learnText);
            this->trainButtons.Add(learn);
            auto* learnSize = this->WidgetTree->ConstructWidget<USizeBox>();
            learnSize->SetWidthOverride(90);
            learnSize->SetContent(learn);
            line->AddChildToHorizontalBox(learnSize)->SetPadding(FMargin(4, 0, 0, 0));
        }

    }

    auto* footer = this->WidgetTree->ConstructWidget<UHorizontalBox>();
    body->AddChildToVerticalBox(footer)->SetPadding(FMargin(0, 12, 0, 0));
    for (const auto& entry :
         {TPair<uint32, FString>(0, TEXT("슬롯 비우기")), TPair<uint32, FString>(UINT32_MAX, TEXT("닫기 [K]"))})
    {
        auto* button = this->WidgetTree->ConstructWidget<UWorldQuickSlotButton>();
        button->ownerHud = this;
        button->bindingOption = true;
        button->binding = entry.Key;
        RpgUI::Button(button);
        button->SetContent(text(entry.Value, 14));
        button->OnClicked.AddDynamic(button, &UWorldQuickSlotButton::Activate);
        footer->AddChildToHorizontalBox(button)->SetPadding(FMargin(8, 0));
    }

    this->quickSlotPicker->SetVisibility(ESlateVisibility::Collapsed);
}

void UWorldControlHUD::TrainFromBook(uint32 skill)
{
    if (auto* game = GetGameInstance())
    {
        game->GetSubsystem<UGameSessionSubsystem>()->TrainSkill(skill);
    }

    RefreshSkillBook();
}

void UWorldControlHUD::RefreshSkillBook()
{
    auto* game = GetGameInstance();
    if (!game)
    {
        return;
    }

    auto* session = game->GetSubsystem<UGameSessionSubsystem>();
    const auto POTIONS = session->GetPotionStates();
    for (int32 i = 0; i < this->potionIcons.Num() && i < POTIONS.Num(); ++i)
    {
        this->potionIcons[i]->SetRenderOpacity(POTIONS[i].usable ? 1.f : .35f);
        this->potionLabels[i]->SetText(FText::FromString(POTIONS[i].name + TEXT("\n") + POTIONS[i].detail));
    }

    if (!IsSkillBookVisible())
    {
        return;
    }

    const auto* catalog = session->GetSkillCatalog();
    if (!catalog)
    {
        return;
    }

    const auto STATE = session->GetSkillState();
    this->pickerCaption->SetText(FText::FromString(FString::Printf(TEXT("스킬 · 남은 포인트 %u  [K]"), STATE.points)));
    for (int32 i = 0; i < this->bindingButtons.Num(); ++i)
    {
        const uint32 ID = this->bindingButtons[i]->binding;
        const auto& skill = catalog->definitions.at(ID);
        const uint32 RANK = STATE.ranks[ID];
        const auto ABILITY = session->GetAbility(ID);
        const TCHAR* aim = skill.aim == shared_world::skills::Aim::TARGET   ? TEXT("타게팅")
                           : skill.aim == shared_world::skills::Aim::GROUND ? TEXT("지면")
                           : skill.aim == shared_world::skills::Aim::SELF   ? TEXT("자기 주변")
                                                                            : TEXT("방향");
        this->skillLabels[i]->SetText(FText::FromString(
            FString::Printf(TEXT("%s  %u / %u\n%s · %u MP · %.1fs"), UTF8_TO_TCHAR(skill.name.c_str()), RANK,
                            skill.maxRank, aim, ABILITY.mp, ABILITY.cooldownMs / 1000.f)));
        this->bindingButtons[i]->SetIsEnabled(RANK > 0);
        this->skillGlows[i]->SetVisibility(this->pendingSkillBinding == ID ? ESlateVisibility::HitTestInvisible
                                                                           : ESlateVisibility::Collapsed);
        auto copy = STATE;
        const bool CAN = session->SkillTrainingReady() && catalog->Train(copy, ID, RANK + 1);
        this->trainButtons[i]->SetIsEnabled(CAN);
        const uint32 COST = RANK < skill.maxRank ? catalog->ranks.at({ID, RANK + 1}).cost : 0;
        this->trainLabels[i]->SetText(FText::FromString(
            RANK >= skill.maxRank ? TEXT("최대 등급")
                                  : FString::Printf(TEXT("%s\n%u P"), RANK ? TEXT("강화") : TEXT("배우기"), COST)));
    }

}

#if !UE_BUILD_SHIPPING
bool UWorldControlHUD::ClickTrainForTest(uint32 skill)
{
    for (auto button : this->trainButtons)
    {
        if (button->binding == skill)
        {
            return ClickSlateButtonForTest(button);
        }

    }

    return false;
}

#endif
