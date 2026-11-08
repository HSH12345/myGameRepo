#include "WorldLoadingScreen.h"
#include "UIs/RpgUITheme.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "MoviePlayer.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"

namespace
{
class SWorldLoadingProgress : public SProgressBar
{
public:
    SLATE_BEGIN_ARGS(SWorldLoadingProgress) : _Value(0.f)
    {
    }

    SLATE_ARGUMENT(float, Value)
    SLATE_END_ARGS()

    void Construct(const FArguments& args)
    {
        this->skin.BackgroundImage = FSlateColorBrush(FLinearColor(.035f, .025f, .015f, 1));
        this->skin.FillImage = FSlateColorBrush(FLinearColor::White);
        SProgressBar::Construct(SProgressBar::FArguments()
                                    .Style(&this->skin)
                                    .Percent(args._Value)
                                    .FillColorAndOpacity(FLinearColor(.85f, .55f, .15f)));
    }

private:
    FProgressBarStyle skin;
};

}

FWorldLoadingScreen::FWorldLoadingScreen(UGameInstance* game) : owner(game), art(RpgUI::Texture(TEXT("Title")))
{
    this->background.SetResourceObject(this->art.Get());
    if (this->art)
    {
        this->background.ImageSize = FVector2D(this->art->GetSizeX(), this->art->GetSizeY());
    }

}

FWorldLoadingScreen::~FWorldLoadingScreen()
{
    Hide();
}

TSharedRef<SWidget> FWorldLoadingScreen::Build(bool movie)
{
    // MoviePlayer's Slate thread sees immutable values only; no UObject delegates.
    auto artBrush = MakeShared<FSlateBrush>(this->background);
    auto progress = SNew(SWorldLoadingProgress).Value(movie ? .35f : this->progressValue);
    auto label = SNew(STextBlock)
                     .Text(FText::FromString(this->destination + TEXT(" · 여정을 준비하고 있습니다")))
                     .Font(RpgUI::Font(20))
                     .ColorAndOpacity(FLinearColor(.96f, .83f, .58f));
    if (!movie)
    {
        this->bar = progress;
        this->caption = label;
    }

    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor::Black)
        .Padding(
            0)[SNew(SOverlay) +
               SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[SNew(SImage).Image_Lambda(
                   [artBrush]() -> const FSlateBrush*
                   {
                       return &artBrush.Get();
                   })]] +

               SOverlay::Slot()
                   .HAlign(HAlign_Fill)
                   .VAlign(VAlign_Bottom)
                       [SNew(SBorder)
                            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(FLinearColor(0, 0, 0, .88f))
                            .Padding(FMargin(80, 30, 80, 44))
                                [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)[label] +
                                 SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(10)[progress]]]]];
}

void FWorldLoadingScreen::Show(uint32 map)
{
    Hide();
    this->destination = map == 301 ? TEXT("시험의 던전") : TEXT("평화의 대지");
    this->progressValue = 0;
    this->quietTime = 0;
    this->quietFrames = 0;
    this->fencePending = false;
    this->completeFrame = false;
    if (auto* game = this->owner.Get(); game && game->GetGameViewportClient())
    {
        this->screen = Build(false);
        game->GetGameViewportClient()->AddViewportWidgetContent(this->screen.ToSharedRef(), 10000);
    }

}

void FWorldLoadingScreen::PrepareTravel()
{
    if (!IsMoviePlayerEnabled())
    {
        return;
    }

    FLoadingScreenAttributes attributes;
    attributes.bAutoCompleteWhenLoadingCompletes = true;
    attributes.bMoviesAreSkippable = false;
    attributes.WidgetLoadingScreen = Build(true);
    GetMoviePlayer()->SetupLoadingScreen(attributes);
}

void FWorldLoadingScreen::Attach()
{
    if (this->screen)
    {
        if (auto* game = this->owner.Get(); game && game->GetGameViewportClient())
        {
            game->GetGameViewportClient()->RemoveViewportWidgetContent(this->screen.ToSharedRef());
            game->GetGameViewportClient()->AddViewportWidgetContent(this->screen.ToSharedRef(), 10000);
        }

    }

}

void FWorldLoadingScreen::Update(float progress, const FString& stage)
{
    if (progress < this->progressValue)
    {
        return;
    }

    this->progressValue = FMath::Max(this->progressValue, FMath::Clamp(progress, 0.f, 1.f));
    if (this->bar)
    {
        this->bar->SetPercent(this->progressValue);
    }

    if (this->caption)
    {
        this->caption->SetText(FText::FromString(this->destination + TEXT(" · ") + stage));
    }

}

bool FWorldLoadingScreen::WarmFrame(bool ready, float delta)
{
    if (!ready || delta > .05f)
    {
        this->quietTime = 0;
        this->quietFrames = 0;
        this->fencePending = false;
        this->completeFrame = false;
        return false;
    }

    this->quietTime += delta;
    ++this->quietFrames;
    Update(.9f + .09f * FMath::Min(float(this->quietTime / .75), 1.f), TEXT("화면을 준비하고 있습니다"));
    if (this->quietTime < .75 || this->quietFrames < 12)
    {
        return false;
    }

    if (!this->fencePending)
    {
        this->fence.BeginFence();
        this->fencePending = true;
        return false;
    }

    if (!this->fence.IsFenceComplete())
    {
        return false;
    }

    if (!this->completeFrame)
    {
        this->completeFrame = true;
        Update(1.f, TEXT("준비 완료"));
        return false;
    }

    return true;
}

void FWorldLoadingScreen::Hide()
{
    if (this->screen)
    {
        if (auto* game = this->owner.Get(); game && game->GetGameViewportClient())
        {
            game->GetGameViewportClient()->RemoveViewportWidgetContent(this->screen.ToSharedRef());
        }

    }

    this->screen.Reset();
    this->bar.Reset();
    this->caption.Reset();
}

