#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "World/WorldDungeonQueueUI.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Engine/Texture2D.h"
#include "AssetCompilingManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonWaitingRoomTest, "RPG.Dungeon.WaitingRoom",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDungeonWaitingRoomTest::RunTest(const FString&)
{
    UGameInstance* game = NewObject<UGameInstance>(GEngine);
    game->AddToRoot();
    game->InitializeStandalone();
    UWorldDungeonQueueUI* widget = CreateWidget<UWorldDungeonQueueUI>(game);
    if (!TestNotNull(TEXT("Waiting room widget"), widget))
    {
        game->Shutdown();
        game->RemoveFromRoot();
        return false;
    }

    widget->AddToRoot();
    const auto SLATE = widget->TakeWidget();
    shared_world::net::Message state;
    state.kind = shared_world::net::Kind::MATCH_STATE;
    state.epoch = 11;
    state.room = 1;
    state.revision = 2;
    state.targetMap = 301;
    state.matchPhase = shared_world::party::MatchPhase::WAITING;
    state.members = {{11, 101, "First", 0, 0, true}, {12, 102, "Second", 0, 1, false}, {13, 103, "Third", 0, 0, true}};
    const auto WIRE = state.Encode();
    const auto DECODED = shared_world::net::Message::Decode(WIRE.data(), WIRE.size());
    TestEqual(TEXT("UE decodes room members"), static_cast<int32>(DECODED.members.size()), 3);
    TestEqual(TEXT("Match state wire ID"), static_cast<int32>(WIRE[3]), 34);
    widget->Refresh(state, 11, 0);
    TestEqual(TEXT("Exactly four slots"), widget->memberNames.Num(), 4);
    TestEqual(TEXT("Ready is server-confirmed"), widget->readyText->GetText().ToString(), FString(TEXT("준비 취소")));
    TestEqual(TEXT("Unused slot stays open"), widget->memberPortraits[3]->GetVisibility(), ESlateVisibility::Hidden);
    TestEqual(TEXT("Waiting participant"), widget->memberStates[1]->GetText().ToString(), FString(TEXT("준비 대기")));
    for (auto& member : state.members)
    {
        member.ready = true;
    }

    state.matchPhase = shared_world::party::MatchPhase::COUNTDOWN;
    state.countdownMs = 5000;
    widget->Refresh(state, 11, 4.2);
    TestEqual(TEXT("Countdown rounds up"), widget->countdownText->GetText().ToString(), FString(TEXT("05초 후 입장")));
    if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
    {
        FWidgetRenderer renderer(false);
        FAssetCompilingManager::Get().FinishAllCompilation();
        for (const auto& portrait : widget->memberPortraits)
        {
            if (auto* texture = Cast<UTexture2D>(portrait->GetBrush().GetResourceObject()))
            {
                texture->SetForceMipLevelsToBeResident(10);
                texture->WaitForStreaming();
            }

        }

        auto* target = FWidgetRenderer::CreateTargetFor(FVector2D(440, 340), TF_Bilinear, false);
        renderer.DrawWidget(target, SLATE, FVector2D(440, 340), 0);
        renderer.DrawWidget(target, SLATE, FVector2D(440, 340), 0);
        FlushRenderingCommands();
        for (int32 i = 0; i < 3; ++i)
        {
            const FVector2D SIZE = widget->memberPortraits[i]->GetCachedGeometry().GetLocalSize();
            TestTrue(FString::Printf(TEXT("Occupied portrait %d has visible area"), i), SIZE.X > 0 && SIZE.Y > 0);
            UE_LOG(LogTemp, Display, TEXT("WAITING_ROOM portrait=%d size=%.1f,%.1f texture=%s"), i, SIZE.X, SIZE.Y,
                *GetNameSafe(widget->memberPortraits[i]->GetBrush().GetResourceObject()));
        }

        TestTrue(TEXT("Ready button remains inside panel"),
                 widget->readyButton->GetCachedGeometry().GetAbsolutePosition().Y +
                         widget->readyButton->GetCachedGeometry().GetAbsoluteSize().Y <=
                     340);
        FBufferArchive bytes;
        const FString FOLDER = FPaths::ProjectSavedDir() / TEXT("PartyDungeonVerification");
        IFileManager::Get().MakeDirectory(*FOLDER, true);
        if (TestTrue(TEXT("Encode real UMG panel"), FImageUtils::ExportRenderTarget2DAsPNG(target, bytes)))
        {
            TestTrue(TEXT("Save panel screenshot"),
                     FFileHelper::SaveArrayToFile(bytes, *(FOLDER / TEXT("WaitingRoom.png"))));
        }

    }

    widget->Refresh({}, 11, 0);
    TestEqual(TEXT("Leaving hides room"), widget->GetVisibility(), ESlateVisibility::Collapsed);
    widget->RemoveFromRoot();
    game->Shutdown();
    game->RemoveFromRoot();
    return true;
}

#endif
