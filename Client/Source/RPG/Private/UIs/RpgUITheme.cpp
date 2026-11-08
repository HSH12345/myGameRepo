#include "UIs/RpgUITheme.h"
#include "UIs/RpgArtBorder.h"
#include "World/WorldCombatPresentation.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontCache.h"

namespace RpgUI
{
UTexture2D* Texture(FName name)
{
    check(IsInGameThread());
    if (name.IsNone())
    {
        return nullptr;
    }

    // Fixed, bounded theme set. Retain across screen recreation without reloading on HUD ticks.
    static TMap<FName, TStrongObjectPtr<UTexture2D>> cache;
    if (const auto* found = cache.Find(name))
    {
        return found->Get();
    }

    const FString PATH = name.ToString().StartsWith(TEXT("Skill"))
                             ? TEXT("/Game/RPG/UI/Skills/T_") + name.ToString()
                             : TEXT("/Game/RPG/UI/DarkFantasy/Textures/T_DF_") + name.ToString();
    UTexture2D* asset = LoadObject<UTexture2D>(nullptr, *PATH);
    if (asset)
    {
        cache.Add(name, TStrongObjectPtr<UTexture2D>(asset));
    }

    return asset;
}

FSlateBrush Brush(FName name, bool nineSlice)
{
    FSlateBrush result;
    if (auto* asset = Texture(name))
    {
        result.SetResourceObject(asset);
        // Logical desired sizes. Import also bounds actual texture dimensions for Slate box margins.
        const FVector2D PIXELS(asset->GetSizeX(), asset->GetSizeY());
        result.ImageSize = name == TEXT("Panel") ? FVector2D(256)
                           : name == TEXT("Primary") || name == TEXT("Secondary")
                               ? FVector2D(240, 80)
                               : PIXELS * (96.f / FMath::Max(1.f, FMath::Max(PIXELS.X, PIXELS.Y)));
        result.DrawAs = nineSlice ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
        result.Margin = name == TEXT("Panel") ? FMargin(.13f, .16f, .13f, .12f) : FMargin(.13f, .24f, .13f, .24f);
    }

    return result;
}

FSlateFontInfo Font(int32 size)
{
    check(IsInGameThread());
    static TStrongObjectPtr<UFont> typeface;
    if (!typeface)
    {
        typeface.Reset(LoadObject<UFont>(nullptr, TEXT("/Game/RPG/UI/DarkFantasy/Fonts/F_SongMyung.F_SongMyung")));
    }

    ensureMsgf(typeface.IsValid(), TEXT("Fantasy runtime font must be imported before use"));
    static bool audited = false;
    if (!audited && typeface && FSlateApplication::IsInitialized())
    {
        audited = true;
        const auto* data = typeface->GetCompositeFont();
        UE_LOG(LogTemp, Display, TEXT("UI_FONT_AUDIT source=%d default=%d fallback=%d"),
               int32(typeface->RuntimeFontSource), data ? data->DefaultTypeface.Fonts.Num() : -1,
               data ? data->FallbackTypeface.Typeface.Fonts.Num() : -1);
        if (data)
        {
            for (const auto& entry : data->FallbackTypeface.Typeface.Fonts)
            {
                UE_LOG(LogTemp, Display, TEXT("UI_FONT_AUDIT face=%s glyph=%d"),
                       *GetPathNameSafe(entry.Font.GetFontFaceAsset()),
                       FSlateApplication::Get().GetRenderer()->GetFontCache()->CanLoadCodepoint(entry.Font, 0xbdc1));
            }

        }

    }

    FSlateFontInfo result(typeface.Get(), size, TEXT("Regular"));
    result.OutlineSettings.OutlineSize = 1;
    result.OutlineSettings.OutlineColor = FLinearColor(.008f, .006f, .004f, 1);
    return result;
}

void Text(UTextBlock* label, int32 size)
{
    if (!label)
    {
        return;
    }

    label->SetFont(Font(size > 0 ? size : FMath::RoundToInt(label->GetFont().Size)));
    label->SetColorAndOpacity(FSlateColor(FLinearColor(.90f, .78f, .57f)));
    label->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .9f));
    label->SetShadowOffset(FVector2D(0, 1));
}

void Button(UButton* widget, bool primary, bool slot)
{
    auto style = widget->GetStyle();
    const FName NAME = slot ? TEXT("Slot") : primary ? TEXT("Primary") : TEXT("Secondary");
    style.Normal = style.Hovered = style.Pressed = style.Disabled = Brush(NAME, true);
    style.Hovered.TintColor = FSlateColor(FLinearColor(1.35f, 1.2f, .9f));
    style.Pressed.TintColor = FSlateColor(FLinearColor(.6f, .5f, .4f));
    style.Disabled.TintColor = FSlateColor(FLinearColor(.35f, .32f, .28f, .8f));
    style.NormalPadding = slot ? FMargin(5) : FMargin(12, 6);
    style.PressedPadding = style.NormalPadding + FMargin(0, 1, 0, -1);
    widget->SetStyle(style);
    widget->SetBackgroundColor(FLinearColor::White);
    widget->SetColorAndOpacity(FLinearColor::White);
}

void Tooltip(UWidgetTree* tree, UWidget* widget, const FString& value)
{
    auto* width = tree->ConstructWidget<USizeBox>();
    width->SetWidthOverride(300);
    auto* border = tree->ConstructWidget<URpgArtBorder>();
    border->SetBrush(Brush(TEXT("Secondary"), true));
    border->SetPadding(FMargin(20, 18));
    width->SetContent(border);
    auto* label = tree->ConstructWidget<UTextBlock>();
    label->SetText(FText::FromString(value));
    Text(label, 14);
    label->SetAutoWrapText(true);
    border->SetContent(label);
    widget->SetToolTip(width);
}

UImage* SelectionGlow(UWidgetTree* tree)
{
    check(IsInGameThread());
    static TStrongObjectPtr<UMaterialInterface> material;
    if (!material)
    {
        material.Reset(LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Game/RPG/UI/DarkFantasy/Materials/M_SelectionGlow.M_SelectionGlow")));
    }

    auto* image = tree->ConstructWidget<UImage>();
    image->SetBrushFromMaterial(material.Get());
    image->SetVisibility(ESlateVisibility::Collapsed);
    return image;
}

UScaleBox* FitImage(UWidgetTree* tree, UImage* image)
{
    auto* fit = tree->ConstructWidget<UScaleBox>();
    fit->SetStretch(EStretch::ScaleToFit);
    fit->SetStretchDirection(EStretchDirection::Both);
    fit->SetVisibility(ESlateVisibility::HitTestInvisible);
    fit->SetContent(image);
    return fit;
}

void FitButton(UWidgetTree* tree, UButton* widget)
{
    if (!tree || !widget || !widget->GetContent() || Cast<URpgArtBorder>(widget->GetContent()))
    {
        return;
    }

    widget->SetClipping(EWidgetClipping::ClipToBounds);
    auto* content = widget->GetContent();
    widget->SetContent(nullptr);
    if (auto* label = Cast<UTextBlock>(content))
    {
        label->SetAutoWrapText(false);
        label->SetMargin(FMargin(0));
        label->SetClipping(EWidgetClipping::ClipToBounds);
    }

    UScaleBox* fit = Cast<UScaleBox>(content);
    if (!fit)
    {
        fit = tree->ConstructWidget<UScaleBox>();
        fit->SetStretch(EStretch::ScaleToFit);
        fit->SetStretchDirection(EStretchDirection::DownOnly);
        fit->SetContent(content);
    }

    fit->SetVisibility(ESlateVisibility::HitTestInvisible);
    const auto SKIN = widget->GetStyle();
    auto* art = tree->ConstructWidget<URpgArtBorder>();
    art->SetBrush(SKIN.Normal);
    art->SetPadding(SKIN.NormalPadding);
    art->SetStyleOwner(widget);
    art->SetVisibility(ESlateVisibility::HitTestInvisible);
    art->SetContent(fit);
    widget->SetContent(art);
    if (auto* slot = Cast<UButtonSlot>(art->Slot))
    {
        slot->SetHorizontalAlignment(HAlign_Fill);
        slot->SetVerticalAlignment(VAlign_Fill);
    }

    art->SetHorizontalAlignment(HAlign_Fill);
    art->SetVerticalAlignment(VAlign_Fill);
    auto style = SKIN;
    style.NormalPadding = style.PressedPadding = FMargin(0);
    style.Normal.DrawAs = style.Hovered.DrawAs = style.Pressed.DrawAs = style.Disabled.DrawAs =
        ESlateBrushDrawType::NoDrawType;
    widget->SetStyle(style);
}

void FitTree(UUserWidget* root)
{
    // Upgrade baked lobby widgets without replacing named bindings or their input handlers.
    TArray<UUserWidget*> pending;
    TSet<UUserWidget*> visited;
    pending.Add(root);
    while (!pending.IsEmpty())
    {
        auto* screen = pending.Pop();
        if (!screen || !screen->WidgetTree || visited.Contains(screen))
        {
            continue;
        }

        visited.Add(screen);
        auto* tree = screen->WidgetTree.Get();
        TArray<UBorder*> panels;
        TArray<UButton*> buttons;
        tree->ForEachWidget(
            [&](UWidget* widget)
            {
                if (auto* nested = Cast<UUserWidget>(widget))
                {
                    pending.Add(nested);
                }

                if (auto* border = Cast<UBorder>(widget))
                {
                    if (!Cast<URpgArtBorder>(border) && !Cast<URpgArtBorder>(border->GetContent()) &&
                        GetNameSafe(border->Background.GetResourceObject()).EndsWith(TEXT("Panel")))
                    {
                        panels.Add(border);
                    }

                }

                if (auto* button = Cast<UButton>(widget))
                {
                    if (button->GetStyle().Normal.DrawAs != ESlateBrushDrawType::NoDrawType &&
                        GetNameSafe(button->GetStyle().Normal.GetResourceObject()).StartsWith(TEXT("T_DF_")))
                    {
                        buttons.Add(button);
                    }

                }

            });

        // Mutate only after traversal; preserve each original border and its outer slot.
        for (auto* panel : panels)
        {
            auto* content = panel->GetContent();
            panel->SetContent(nullptr);
            auto* art = tree->ConstructWidget<URpgArtBorder>();
            art->SetBrush(panel->Background);
            art->SetBrushColor(panel->GetBrushColor());
            art->SetPadding(panel->GetPadding());
            art->SetHorizontalAlignment(panel->GetHorizontalAlignment());
            art->SetVerticalAlignment(panel->GetVerticalAlignment());
            art->SetContent(content);
            panel->SetContent(art);
            panel->SetPadding(FMargin(0));
            panel->SetHorizontalAlignment(HAlign_Fill);
            panel->SetVerticalAlignment(VAlign_Fill);
            auto skin = panel->Background;
            skin.DrawAs = ESlateBrushDrawType::NoDrawType;
            panel->SetBrush(skin);
        }

        for (auto* button : buttons)
        {
            FitButton(tree, button);
        }

    }

}

void Frame(UBorder* widget)
{
    widget->SetBrush(Brush(TEXT("Panel"), true));
    widget->SetBrushColor(FLinearColor::White);
}

FName ItemIcon(uint32 type)
{
    switch (type)
    {
    case 1001:
        return TEXT("Sword");
    case 1002:
        return TEXT("Cloth");
    case 1003:
        return TEXT("Ranger");
    case 1101:
        return TEXT("Material");
    case 1201:
        return TEXT("Health");
    case 1202:
        return TEXT("Mana");
    default:
        return NAME_None;
    }

}

FName SkillIcon(uint32 skill)
{
    return RPGCombatArt::Get(skill).icon;
}

FString ItemName(uint32 type, const FString& fallback)
{
    switch (type)
    {
    case 1001:
        return TEXT("수련용 검");
    case 1002:
        return TEXT("평민의 의복");
    case 1003:
        return TEXT("순찰자의 의복");
    case 1101:
        return TEXT("수련 재료");
    case 1201:
        return TEXT("생명 물약");
    case 1202:
        return TEXT("마나 물약");
    default:
        return fallback;
    }

}

FString Caption(const FString& value)
{
    static const TMap<FString, FString> TRANSLATIONS = {
        {TEXT("Attack"), TEXT("공격")},
        {TEXT("Bolt"), TEXT("번개")},
        {TEXT("Burst"), TEXT("화염 폭발")},
        {TEXT("HP Potion"), TEXT("생명 물약")},
        {TEXT("MP Potion"), TEXT("마나 물약")},
        {TEXT("Elf Village Guide"), TEXT("엘프 마을 안내인")},
        {TEXT("Village Merchant"), TEXT("마을 상인")},
        {TEXT("Peace World"), TEXT("평화의 대지")},
        {TEXT("Dungeon"), TEXT("시험의 던전")},
        {TEXT("Warp to Test Dungeon"), TEXT("시험의 던전으로 이동")},
        {TEXT("Return to Peace World"), TEXT("평화의 대지로 귀환")},
        {TEXT("Recovering"), TEXT("회복 중")},
        {TEXT("Confirmed."), TEXT("확인되었습니다.")},
        {TEXT("Mana potion registered to slot 4."), TEXT("4번 단축키에 마나 물약을 등록했습니다.")},
        {TEXT("Health potion registered to slot 3."), TEXT("3번 단축키에 생명 물약을 등록했습니다.")},
        {TEXT("Close"), TEXT("닫기")},
        {TEXT("Ready"), TEXT("준비 완료")},
        {TEXT("Travelling..."), TEXT("이동 중...")}};
    if (const auto* found = TRANSLATIONS.Find(value))
    {
        return *found;
    }

    return value;
}

}

