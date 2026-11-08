#pragma once
#include "CoreMinimal.h"
#include "RenderCommandFence.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
class UGameInstance;
class UTexture2D;
class SWidget;
class SProgressBar;
class STextBlock;

// Game-instance lifetime, independent of widgets destroyed by OpenLevel.
class FWorldLoadingScreen
{
public:
    explicit FWorldLoadingScreen(UGameInstance* game);
    ~FWorldLoadingScreen();
    void Show(uint32 map);
    void PrepareTravel();
    void Attach();
    void Update(float progress, const FString& stage);
    bool WarmFrame(bool ready, float delta);
    void Hide();

    bool IsVisible() const
    {
        return this->screen.IsValid();
    }

private:
    TSharedRef<SWidget> Build(bool movie);
    TWeakObjectPtr<UGameInstance> owner;
    TStrongObjectPtr<UTexture2D> art;
    FSlateBrush background;
    TSharedPtr<SWidget> screen;
    TSharedPtr<SProgressBar> bar;
    TSharedPtr<STextBlock> caption;
    FRenderCommandFence fence;
    bool fencePending = false;
    bool completeFrame = false;
    double quietTime = 0;
    int32 quietFrames = 0;
    float progressValue = 0;
    FString destination;
};

