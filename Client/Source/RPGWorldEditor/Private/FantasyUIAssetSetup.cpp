#include "HAL/IConsoleManager.h"
#include "UIs/RpgUITheme.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
UTextBlock* Label(UWidgetTree* tree, FName name, const TCHAR* value, int32 size)
{
    auto* text = tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), name);
    text->SetText(FText::FromString(value));
    RpgUI::Text(text, size);
    text->SetVisibility(ESlateVisibility::HitTestInvisible);
    return text;
}

void Line(UVerticalBox* column, UWidget* widget, float bottom = 12)
{
    column->AddChildToVerticalBox(widget)->SetPadding(FMargin(0, 0, 0, bottom));
}

UWidgetTree* NewTree(UWidgetBlueprint* bp)
{
    bp->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
    bp->WidgetTree = NewObject<UWidgetTree>(bp, TEXT("WidgetTree"), RF_Transactional);
    return bp->WidgetTree;
}

UVerticalBox* Window(UWidgetTree* tree, bool right)
{
    auto* canvas = tree->ConstructWidget<UCanvasPanel>();
    tree->RootWidget = canvas;
    auto* panel = tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FantasyPanel"));
    RpgUI::Frame(panel);
    panel->SetPadding(FMargin(40, 44, 40, 36));
    auto* slot = canvas->AddChildToCanvas(panel);
    slot->SetAnchors(FAnchors(right ? 1.f : .5f, .5f));
    slot->SetAlignment(FVector2D(right ? 1.f : .5f, .5f));
    slot->SetPosition(FVector2D(right ? -54 : 0, 0));
    slot->SetSize(FVector2D(right ? 540 : 1040, right ? 820 : 740));
    auto* column = tree->ConstructWidget<UVerticalBox>();
    panel->SetContent(column);
    return column;
}

void Action(UWidgetTree* tree, UVerticalBox* column, const TCHAR* name, const TCHAR* value, bool primary)
{
    auto* button = tree->ConstructWidget<UButton>(UButton::StaticClass(), name);
    RpgUI::Button(button, primary);
    auto* text = Label(tree, NAME_None, value, 18);
    text->SetJustification(ETextJustify::Center);
    button->SetContent(text);
    auto* height = tree->ConstructWidget<USizeBox>();
    height->SetHeightOverride(72);
    height->SetContent(button);
    Line(column, height, 8);
}

}

static FAutoConsoleCommand _setupFantasy(
    TEXT("RPG.ConfigureFantasyUI"), TEXT("Explicit backed-up UI art migration; Python saves the compiled assets."),
    FConsoleCommandDelegate::CreateLambda(
        []
        {
            const FString FONT_PATH = TEXT("/Game/RPG/UI/DarkFantasy/Fonts/");
            TArray<uint8> bytes;
            if (!FFileHelper::LoadFileToArray(
                    bytes, *(FPaths::ProjectDir() / TEXT("SourceArt/DarkFantasy/Fonts/SongMyung-Regular.ttf"))))
            {
                return;
            }

            auto* facePackage = CreatePackage(*(FONT_PATH + TEXT("FF_SongMyung")));
            facePackage->FullyLoad();
            auto* face = FindObject<UFontFace>(facePackage, TEXT("FF_SongMyung"));
            if (!face)
            {
                face = NewObject<UFontFace>(facePackage, TEXT("FF_SongMyung"), RF_Public | RF_Standalone);
                FAssetRegistryModule::AssetCreated(face);
            }

            face->SourceFilename = FPaths::ProjectDir() / TEXT("SourceArt/DarkFantasy/Fonts/SongMyung-Regular.ttf");
            face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(bytes));
            face->LoadingPolicy = EFontLoadingPolicy::Inline;
            face->CacheSubFaces();
            face->MarkPackageDirty();
            auto* fontPackage = CreatePackage(*(FONT_PATH + TEXT("F_SongMyung")));
            fontPackage->FullyLoad();
            auto* font = FindObject<UFont>(fontPackage, TEXT("F_SongMyung"));
            if (!font)
            {
                font = NewObject<UFont>(fontPackage, TEXT("F_SongMyung"), RF_Public | RF_Standalone);
                FAssetRegistryModule::AssetCreated(font);
            }

            font->FontCacheType = EFontCacheType::Runtime;
            font->RuntimeFontSource = ERuntimeFontSource::Asset;
            auto& fonts = font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts;
            fonts.Empty();
            FTypefaceEntry entry(TEXT("Regular"));
            entry.Font = FFontData(face);
            fonts.Add(entry);
            TArray<uint8> fallbackBytes;
            if (!FFileHelper::LoadFileToArray(
                    fallbackBytes, *(FPaths::ProjectDir() / TEXT("SourceArt/DarkFantasy/Fonts/DroidSansFallback.ttf"))))
            {
                return;
            }

            auto* fallbackPackage = CreatePackage(*(FONT_PATH + TEXT("FF_KoreanFallback")));
            fallbackPackage->FullyLoad();
            auto* fallbackFace = FindObject<UFontFace>(fallbackPackage, TEXT("FF_KoreanFallback"));
            if (!fallbackFace)
            {
                fallbackFace =
                    NewObject<UFontFace>(fallbackPackage, TEXT("FF_KoreanFallback"), RF_Public | RF_Standalone);
                FAssetRegistryModule::AssetCreated(fallbackFace);
            }

            fallbackFace->SourceFilename =
                FPaths::ProjectDir() / TEXT("SourceArt/DarkFantasy/Fonts/DroidSansFallback.ttf");
            fallbackFace->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(fallbackBytes));
            fallbackFace->LoadingPolicy = EFontLoadingPolicy::Inline;
            fallbackFace->CacheSubFaces();
            fallbackFace->MarkPackageDirty();
            FTypefaceEntry fallbackEntry(TEXT("Regular"));
            fallbackEntry.Font = FFontData(fallbackFace);
            auto& fallbackFonts = font->GetMutableInternalCompositeFont().FallbackTypeface.Typeface.Fonts;
            fallbackFonts.Reset();
            fallbackFonts.Add(fallbackEntry);
            font->GetMutableInternalCompositeFont().MakeDirty();
            font->MarkPackageDirty();

            const FString BASE = TEXT("/Game/RPG/UI/Lobby/");
            for (const TCHAR* name : {TEXT("WBP_Login"), TEXT("WBP_CharacterSelect"), TEXT("WBP_CharacterCreate"),
                                      TEXT("WBP_CharacterSlot"), TEXT("WBP_LobbyRoot")})
            {
                auto* bp = LoadObject<UWidgetBlueprint>(nullptr, *(BASE + name));
                check(bp);
                UWidgetTree* tree = bp->WidgetTree.Get();
                if (FString(name) == TEXT("WBP_CharacterSelect"))
                {
                    tree = NewTree(bp);
                    auto* column = Window(tree, false);
                    Line(column, Label(tree, TEXT("TitleText"), TEXT("영웅 선택"), 30), 16);
                    Line(column, Label(tree, TEXT("CountText"), TEXT("캐릭터: 0 / 8"), 16));
                    auto* list = tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CharacterList"));
                    column->AddChildToVerticalBox(list)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                    Line(column, Label(tree, TEXT("SelectedCharacterText"), TEXT("모험을 떠날 영웅을 선택하세요."), 15),
                         8);
                    Line(column, Label(tree, TEXT("StatusText"), TEXT(""), 14), 8);
                    Action(tree, column, TEXT("CreateButton"), TEXT("영웅 생성 / 미확정 결과 확인"), true);
                    Action(tree, column, TEXT("RefreshButton"), TEXT("목록 새로고침"), false);
                }

                if (FString(name) == TEXT("WBP_CharacterCreate"))
                {
                    tree = NewTree(bp);
                    auto* column = Window(tree, true);
                    Line(column, Label(tree, TEXT("TitleText"), TEXT("새로운 영웅"), 28), 20);
                    Line(column, Label(tree, TEXT("AppearanceNote"), TEXT("인간 · 성별을 선택하여 외형 확인"), 15), 20);
                    Line(column, Label(tree, TEXT("NameLabel"), TEXT("영웅의 이름"), 17), 6);
                    auto* input = tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),
                                                                          TEXT("CharacterNameInput"));
                    input->SetHintText(FText::FromString(TEXT("이름을 입력하세요")));
                    auto inputStyle = input->GetWidgetStyle();
                    inputStyle.SetFont(RpgUI::Font(18));
                    inputStyle.Padding = FMargin(20, 16);
                    inputStyle.BackgroundImageNormal = inputStyle.BackgroundImageHovered =
                        inputStyle.BackgroundImageFocused = RpgUI::Brush(TEXT("Secondary"), true);
                    inputStyle.ForegroundColor = FSlateColor(FLinearColor(.9f, .8f, .6f));
                    input->SetWidgetStyle(inputStyle);
                    Line(column, input, 16);
                    auto* gender =
                        tree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("GenderChoice"));
                    gender->AddOption(TEXT("남성"));
                    gender->AddOption(TEXT("여성"));
                    gender->SetSelectedIndex(0);
                    // Defaults are serialized into UMG before Slate construction; no runtime mutation.
                    if (auto* prop = FindFProperty<FStructProperty>(gender->GetClass(), TEXT("Font")))
                    {
                        *prop->ContainerPtrToValuePtr<FSlateFontInfo>(gender) = RpgUI::Font(18);
                    }

                    auto comboStyle = gender->GetWidgetStyle();
                    comboStyle.ComboButtonStyle.ButtonStyle.Normal = RpgUI::Brush(TEXT("Secondary"), true);
                    comboStyle.ComboButtonStyle.ButtonStyle.Hovered = RpgUI::Brush(TEXT("Primary"), true);
                    comboStyle.ComboButtonStyle.ButtonStyle.NormalPadding =
                        comboStyle.ComboButtonStyle.ButtonStyle.PressedPadding = FMargin(18, 16);
                    gender->SetWidgetStyle(comboStyle);
                    gender->SetContentPadding(FMargin(18, 16));
                    if (auto* prop = FindFProperty<FStructProperty>(gender->GetClass(), TEXT("ForegroundColor")))
                    {
                        *prop->ContainerPtrToValuePtr<FSlateColor>(gender) = FSlateColor(FLinearColor(.9f, .78f, .57f));
                    }

                    auto itemStyle = gender->GetItemStyle();
                    itemStyle.TextColor = FSlateColor(FLinearColor(.9f, .78f, .57f));
                    itemStyle.SelectedTextColor = itemStyle.TextColor;
                    itemStyle.ActiveBrush = itemStyle.ActiveHoveredBrush = RpgUI::Brush(TEXT("Primary"), true);
                    itemStyle.InactiveBrush = itemStyle.InactiveHoveredBrush = RpgUI::Brush(TEXT("Secondary"), true);
                    gender->SetItemStyle(itemStyle);
                    Line(column, gender, 16);
                    auto* status = Label(tree, TEXT("StatusText"), TEXT("이름은 한글, 영문, 숫자로 2~24자"), 14);
                    status->SetAutoWrapText(true);
                    auto* statusHeight = tree->ConstructWidget<USizeBox>();
                    statusHeight->SetHeightOverride(90);
                    statusHeight->SetContent(status);
                    Line(column, statusHeight, 16);
                    Action(tree, column, TEXT("SubmitButton"), TEXT("영웅 생성"), true);
                    Action(tree, column, TEXT("ConfirmPendingButton"), TEXT("이전 요청 결과 확인"), false);
                    Action(tree, column, TEXT("BackButton"), TEXT("돌아가기"), false);
                }

                if (FString(name) == TEXT("WBP_CharacterSlot") && !tree->FindWidget(TEXT("PortraitImage")))
                {
                    auto* pick = CastChecked<UButton>(tree->FindWidget(TEXT("SelectButton")));
                    auto* detail = pick->GetContent();
                    pick->SetContent(nullptr);
                    auto* row = tree->ConstructWidget<UHorizontalBox>();
                    pick->SetContent(row);
                    auto* portrait = tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PortraitImage"));
                    portrait->SetBrush(RpgUI::Brush(TEXT("Male")));
                    portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
                    auto* size = tree->ConstructWidget<USizeBox>();
                    size->SetWidthOverride(90);
                    size->SetHeightOverride(102);
                    size->SetContent(portrait);
                    row->AddChild(size);
                    row->AddChildToHorizontalBox(detail)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                }

                if (FString(name) == TEXT("WBP_Login") && !tree->FindWidget(TEXT("FantasyLogo")))
                {
                    auto* title = tree->FindWidget(TEXT("TitleText"));
                    auto* stack = CastChecked<UVerticalBox>(title->GetParent());
                    title->SetVisibility(ESlateVisibility::Collapsed);
                    auto* logo = tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FantasyLogo"));
                    logo->SetBrush(RpgUI::Brush(TEXT("Logo")));
                    logo->SetVisibility(ESlateVisibility::HitTestInvisible);
                    auto* size = tree->ConstructWidget<USizeBox>();
                    size->SetWidthOverride(580);
                    size->SetHeightOverride(194);
                    size->SetContent(logo);
                    stack->InsertChildAt(0, size);
                }

                tree->ForEachWidget(
                    [&](UWidget* widget)
                    {
                        if (auto* text = Cast<UTextBlock>(widget))
                        {
                            RpgUI::Text(text);
                        }

                        if (auto* button = Cast<UButton>(widget); button && button->GetFName() != TEXT("StartButton"))
                        {
                            RpgUI::Button(button, button->GetFName() == TEXT("EnterGameButton") ||
                                                      button->GetFName() == TEXT("CreateButton") ||
                                                      button->GetFName() == TEXT("SubmitButton"));
                        }

                        if (auto* border = Cast<UBorder>(widget))
                        {
                            if (border->GetFName() == TEXT("ScreenBackground"))
                            {
                                border->SetBrush(RpgUI::Brush(TEXT("Title")));
                                border->SetBrushColor(FLinearColor::White);
                            }
                            else
                            {
                                RpgUI::Frame(border);
                            }

                        }

                        if (auto* text = Cast<UTextBlock>(widget);
                            text && text->GetFName() == TEXT("TitleText") && FString(name) == TEXT("WBP_Login"))
                        {
                            RpgUI::Text(text, 96);
                        }

                    });

                TArray<UButton*> buttons;
                tree->ForEachWidget(
                    [&](UWidget* widget)
                    {
                        if (auto* button = Cast<UButton>(widget); button && button->GetFName() != TEXT("StartButton"))
                        {
                            buttons.Add(button);
                        }

                    });

                for (auto* button : buttons)
                {
                    RpgUI::FitButton(tree, button);
                }

                TSet<FName> names;
                tree->ForEachWidget(
                    [&](UWidget* widget)
                    {
                        names.Add(widget->GetFName());
                        if (!bp->WidgetVariableNameToGuidMap.Contains(widget->GetFName()))
                        {
                            bp->OnVariableAdded(widget->GetFName());
                        }

                    });

                TArray<FName> old;
                bp->WidgetVariableNameToGuidMap.GetKeys(old);
                for (auto n : old)
                {
                    if (!names.Contains(n))
                    {
                        bp->OnVariableRemoved(n);
                    }

                }

                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(bp);
                FKismetEditorUtilities::CompileBlueprint(bp);
                bp->MarkPackageDirty();
            }

            UE_LOG(LogTemp, Display, TEXT("FANTASY_UI_SETUP_OK"));
        }));

