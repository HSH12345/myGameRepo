#include "HAL/IConsoleManager.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
UTextBlock* Text(UWidgetTree* tree, const TCHAR* name, const TCHAR* value, int32 size)
{
    auto* block = tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), name);
    block->SetText(FText::FromString(value));
    auto font = block->GetFont();
    font.Size = size;
    block->SetFont(font);
    block->SetVisibility(ESlateVisibility::HitTestInvisible);
    return block;
}

UWidgetTree* ReplaceTree(UWidgetBlueprint* blueprint)
{
    // The caller backs up the three assets first. No level or vendor assets are touched.
    blueprint->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
    blueprint->WidgetTree = NewObject<UWidgetTree>(blueprint, TEXT("WidgetTree"), RF_Transactional);
    return blueprint->WidgetTree;
}

}

// Explicit offline migration only; Python validates compilation and saves the named packages.
static FAutoConsoleCommand _configureLobbyEntry(
    TEXT("RPG.ConfigureLobbyEntry"),
    TEXT("Configure click-to-start and per-character entry UMG layouts; does not save."),
    FConsoleCommandDelegate::CreateLambda(
        []
        {
            const FString BASE = TEXT("/Game/RPG/UI/Lobby/");
            auto* login = LoadObject<UWidgetBlueprint>(nullptr, *(BASE + TEXT("WBP_Login.WBP_Login")));
            auto* select =
                LoadObject<UWidgetBlueprint>(nullptr, *(BASE + TEXT("WBP_CharacterSelect.WBP_CharacterSelect")));
            auto* slot = LoadObject<UWidgetBlueprint>(nullptr, *(BASE + TEXT("WBP_CharacterSlot.WBP_CharacterSlot")));
            if (!login || !select || !slot)
            {
                UE_LOG(LogTemp, Error, TEXT("LOBBY_ENTRY_SETUP missing blueprint"));
                return;
            }

            auto* tree = ReplaceTree(login);
            auto* background = tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScreenBackground"));
            background->SetBrushColor(FLinearColor(.018f, .026f, .040f, 1));
            background->SetPadding(FMargin(0));
            tree->RootWidget = background;
            auto* start = tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("StartButton"));
            auto style = start->GetStyle();
            style.Normal.DrawAs = style.Hovered.DrawAs = style.Pressed.DrawAs = ESlateBrushDrawType::NoDrawType;
            style.NormalPadding = style.PressedPadding = FMargin(24);
            start->SetStyle(style);
            background->SetContent(start);
            auto* width = tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ContentWidth"));
            width->SetWidthOverride(640); // Keep status changes from shrinking the auto-wrap measurement.
            width->SetMaxDesiredWidth(640);
            start->AddChild(width);
            auto* stack = tree->ConstructWidget<UVerticalBox>();
            width->AddChild(stack);
            auto* title = Text(tree, TEXT("TitleText"), TEXT("RPG"), 40);
            title->SetJustification(ETextJustify::Center);
            stack->AddChildToVerticalBox(title)->SetPadding(FMargin(0, 0, 0, 20));
            auto* status = Text(tree, TEXT("StatusText"), TEXT("화면 아무 곳이나 클릭하여 시작"), 20);
            status->SetJustification(ETextJustify::Center);
            status->SetAutoWrapText(true);
            stack->AddChildToVerticalBox(status);
            // Content stays visible/hit-test transparent. Button alone owns the entire click surface.
            width->SetVisibility(ESlateVisibility::HitTestInvisible);

            if (auto* old = select->WidgetTree->FindWidget(TEXT("EnterGameButton")))
            {
                select->WidgetTree->RemoveWidget(old);
            }

            tree = ReplaceTree(slot);
            auto* card = tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
            card->SetBrushColor(FLinearColor(.05f, .075f, .1f, 1));
            card->SetPadding(FMargin(10));
            tree->RootWidget = card;
            auto* row = tree->ConstructWidget<UHorizontalBox>();
            card->SetContent(row);
            auto* pick = tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SelectButton"));
            pick->SetBackgroundColor(FLinearColor(.08f, .15f, .22f, 1));
            auto* pickSlot = row->AddChildToHorizontalBox(pick);
            pickSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* detail = tree->ConstructWidget<UVerticalBox>();
            pick->AddChild(detail);
            auto* name = Text(tree, TEXT("CharacterNameText"), TEXT("Character"), 24);
            detail->AddChildToVerticalBox(name)->SetPadding(FMargin(14, 8, 14, 4));
            auto* info = Text(tree, TEXT("CharacterDetailText"), TEXT(""), 16);
            info->SetAutoWrapText(true);
            detail->AddChildToVerticalBox(info)->SetPadding(FMargin(14, 0, 14, 4));
            auto* selected = Text(tree, TEXT("SelectionText"), TEXT(""), 16);
            detail->AddChildToVerticalBox(selected)->SetPadding(FMargin(14, 0, 14, 8));
            auto* entryWidth = tree->ConstructWidget<USizeBox>();
            entryWidth->SetWidthOverride(110);
            auto* entrySlot = row->AddChildToHorizontalBox(entryWidth);
            entrySlot->SetPadding(FMargin(12, 0, 0, 0));
            auto* enter = tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("EnterGameButton"));
            enter->SetBackgroundColor(FLinearColor(.08f, .28f, .32f, 1));
            entryWidth->AddChild(enter);
            enter->AddChild(Text(tree, TEXT("EnterText"), TEXT("접속"), 20));
            for (auto* blueprint : {login, select, slot})
            {
                TSet<FName> names;
                blueprint->WidgetTree->ForEachWidget(
                    [&](UWidget* widget)
                    {
                        names.Add(widget->GetFName());
                        if (!blueprint->WidgetVariableNameToGuidMap.Contains(widget->GetFName()))
                        {
                            blueprint->OnVariableAdded(widget->GetFName());
                        }

                    });

                TArray<FName> oldNames;
                blueprint->WidgetVariableNameToGuidMap.GetKeys(oldNames);
                for (const auto VARIABLE_NAME : oldNames)
                {
                    if (!names.Contains(VARIABLE_NAME))
                    {
                        blueprint->OnVariableRemoved(VARIABLE_NAME);
                    }

                }

                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(blueprint);
                FKismetEditorUtilities::CompileBlueprint(blueprint);
                blueprint->MarkPackageDirty();
            }

            UE_LOG(LogTemp, Display, TEXT("LOBBY_ENTRY_SETUP configured"));
        }));

