#include "UIs/Lobby/LobbyScreens.h"
#include "UIs/RpgUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"

namespace
{
UButton* Action(UWidgetTree* tree, UVerticalBox* panel, FName name, const TCHAR* caption)
{
    auto* size = tree->ConstructWidget<USizeBox>();
    size->SetWidthOverride(280);
    size->SetHeightOverride(48);
    panel->AddChildToVerticalBox(size)->SetPadding(FMargin(0, 6));
    auto* button = tree->ConstructWidget<UButton>(UButton::StaticClass(), name);
    size->AddChild(button);
    auto* text = tree->ConstructWidget<UTextBlock>();
    text->SetText(FText::FromString(caption));
    text->SetJustification(ETextJustify::Center);
    button->AddChild(text);
    RpgUI::Text(text, 18);
    RpgUI::Button(button, true);
    return button;
}

UVerticalBox* ActionPanel(UWidgetTree* tree, FName name, EHorizontalAlignment alignment, float padding,
                          EVerticalAlignment vertical = VAlign_Bottom)
{
    auto* oldRoot = tree->RootWidget.Get();
    auto* overlay = tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), name);
    tree->RootWidget = overlay;
    auto* contentSlot = overlay->AddChildToOverlay(oldRoot);
    contentSlot->SetHorizontalAlignment(HAlign_Fill);
    contentSlot->SetVerticalAlignment(VAlign_Fill);
    auto* panel = tree->ConstructWidget<UVerticalBox>();
    auto* slot = overlay->AddChildToOverlay(panel);
    slot->SetHorizontalAlignment(alignment);
    slot->SetVerticalAlignment(vertical);
    slot->SetPadding(vertical == VAlign_Top ? FMargin(24, padding, 24, 0) : FMargin(24, 0, 24, padding));
    return panel;
}

}

TSharedRef<SWidget> ULobbyLoginScreen::RebuildWidget()
{
    if (this->WidgetTree && this->WidgetTree->RootWidget && !this->naverLoginButton)
    {
        auto* panel = ActionPanel(this->WidgetTree, TEXT("NativeLoginActions"), HAlign_Center, 100);
        this->naverLoginButton = Action(this->WidgetTree, panel, TEXT("NaverLoginButton"), TEXT("네이버 로그인"));
        this->cancelAuthButton = Action(this->WidgetTree, panel, TEXT("CancelAuthButton"), TEXT("로그인 취소"));
        this->cancelAuthButton->SetVisibility(ESlateVisibility::Collapsed);
    }

    return Super::RebuildWidget();
}

TSharedRef<SWidget> ULobbyCharacterSelectScreen::RebuildWidget()
{
    if (this->WidgetTree && this->WidgetTree->RootWidget && !this->logoutButton)
    {
        this->logoutButton = Action(
            this->WidgetTree, ActionPanel(this->WidgetTree, TEXT("NativeAccountActions"), HAlign_Right, 24, VAlign_Top),
            TEXT("LogoutButton"), TEXT("로그아웃"));
    }

    return Super::RebuildWidget();
}

