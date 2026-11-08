#pragma once
#if !UE_BUILD_SHIPPING
#include "Components/Button.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"

// Real Slate hit testing and down/up routing, scoped to a smoke-test window.
inline bool ClickSlateButtonForTest(UButton* button, FVector2D positionFraction = FVector2D(.5f, .5f))
{
    if (!IsValid(button) || !button->GetIsEnabled() || !FSlateApplication::IsInitialized())
    {
        return false;
    }

    const auto WIDGET = button->GetCachedWidget();
    if (!WIDGET.IsValid())
    {
        return false;
    }

    auto& slate = FSlateApplication::Get();
    const auto WINDOW = slate.FindWidgetWindow(WIDGET.ToSharedRef());
    if (!WINDOW.IsValid())
    {
        return false;
    }

    const auto POSITION =
        button->GetCachedGeometry().LocalToAbsolute(button->GetCachedGeometry().GetLocalSize() * positionFraction);
    TArray<TSharedRef<SWindow>> windows;
    windows.Add(WINDOW.ToSharedRef());
    const auto PATH = slate.LocateWindowUnderMouse(POSITION, windows, false, 0);
    bool hitsButton = false;
    for (int32 i = 0; i < PATH.Widgets.Num(); ++i)
    {
        hitsButton |= PATH.Widgets[i].Widget == WIDGET;
    }

    if (!hitsButton)
    {
        UE_LOG(LogTemp, Error, TEXT("UI_POINTER_TEST button obscured"));
        return false;
    }

    slate.GetCursorUser()->ReleaseCursorCapture();
    const bool PREVIOUS_BACKGROUND_INPUT = slate.GetHandleDeviceInputWhenApplicationNotActive();
    slate.SetHandleDeviceInputWhenApplicationNotActive(true);
    TSet<FKey> pressed;
    slate.RoutePointerMoveEvent(
        PATH, FPointerEvent(0, POSITION, POSITION, pressed, EKeys::Invalid, 0, FModifierKeysState()), true);
    pressed.Add(EKeys::LeftMouseButton);
    const auto DOWN = slate.RoutePointerDownEvent(
        PATH, FPointerEvent(0, POSITION, POSITION, pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    const bool CAPTURED = WIDGET->HasMouseCapture();
    pressed.Empty();
    const auto UP = slate.RoutePointerUpEvent(
        PATH, FPointerEvent(0, POSITION, POSITION, pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    slate.SetHandleDeviceInputWhenApplicationNotActive(PREVIOUS_BACKGROUND_INPUT);
    UE_LOG(LogTemp, Display, TEXT("UI_POINTER_TEST down=%d captured=%d up=%d"), DOWN.IsEventHandled(), CAPTURED,
           UP.IsEventHandled());
    return DOWN.IsEventHandled() && CAPTURED && UP.IsEventHandled();
}

#endif
