#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Characters/CharacterAppearanceDefinition.h"
#include "Characters/CharacterPreviewActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterPreviewLoadingTest, "RPG.Characters.PreviewAsyncLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterPreviewLoadingTest::RunTest(const FString&)
{
    UGameInstance* game = NewObject<UGameInstance>(GEngine);
    game->AddToRoot();
    game->InitializeStandalone();
    UWorld* world = game->GetWorld();
    if (!TestNotNull(TEXT("Preview test world"), world))
    {
        game->Shutdown();
        game->RemoveFromRoot();
        return false;
    }

    TArray<ACharacterPreviewActor*> actors;
    for (int32 index = 0; index < 3; ++index)
    {
        ACharacterPreviewActor* actor = world->SpawnActor<ACharacterPreviewActor>();
        if (!TestNotNull(TEXT("Preview actor"), actor))
        {
            continue;
        }

        actors.Add(actor);
        for (const TCHAR* path : {TEXT("/Game/RPG/Characters/Human/Data/DA_Human_Male.DA_Human_Male"),
                                  TEXT("/Game/RPG/Characters/Human/Data/DA_Human_Female.DA_Human_Female")})
        {
            auto* definition = LoadObject<UCharacterAppearanceDefinition>(nullptr, path);
            if (TestNotNull(TEXT("Definition"), definition))
            {
                actor->appearances.Add(definition);
            }

        }

        // Load only definitions: mesh/Idle soft references are left to the real async path.
        actor->DispatchBeginPlay();
        actor->ShowAppearance(0, 0);
        if (index != 0)
        {
            actor->ShowAppearance(0, 1);
        }

        if (index == 2)
        {
            actor->HidePreview();
        }

    }

    const double STARTED = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, game, world, actors, STARTED]()
        {
            world->Tick(LEVELTICK_All, 1.0f / 60.0f);
            bool complete = actors.Num() == 3;
            for (const auto* actor : actors)
            {
                complete &= actor->GetPreparedVariantCount() == 2;
            }

            if (!complete && FPlatformTime::Seconds() - STARTED < 30.0)
            {
                return false;
            }

            TestTrue(TEXT("All variants prepared before timeout"), complete);
            if (complete)
            {
                TestEqual(TEXT("Initial male displays without another click"), actors[0]->GetPreviewState(),
                          ECharacterPreviewLoadState::READY);
                TestEqual(TEXT("Initial male selection"), actors[0]->GetDisplayedGender(), uint8(0));
                TestEqual(TEXT("Latest loading-time selection displays"), actors[1]->GetDisplayedGender(), uint8(1));
                TestEqual(TEXT("Completion cannot reopen a hidden preview"), actors[2]->GetPreviewState(),
                          ECharacterPreviewLoadState::HIDDEN);
                for (uint8 gender = 0; gender < 2; ++gender)
                {
                    const auto* body = actors[2]->GetVariantBody(0, gender);
                    TestTrue(TEXT("Hidden variant is invisible and does not tick"),
                             body && !body->IsVisible() && !body->IsComponentTickEnabled());
                    if (body && body->GetSkeletalMeshAsset())
                    {
                        const auto POSE = const_cast<USkeletalMeshComponent*>(body)->GetBoneSpaceTransforms();
                        const auto& reference = body->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
                        bool animatedPose = false;
                        for (int32 bone = 1; bone < FMath::Min(POSE.Num(), reference.Num()); ++bone)
                        {
                            animatedPose |= !POSE[bone].GetRotation().Equals(reference[bone].GetRotation(), 0.001);
                        }

                        TestTrue(TEXT("Prepared Idle differs from the reference pose"), animatedPose);
                    }

                }

                actors[2]->ShowAppearance(0, 0);
                TestEqual(TEXT("Prepared reopen displays immediately"), actors[2]->GetPreviewState(),
                          ECharacterPreviewLoadState::READY);
                const auto* male = actors[2]->GetVariantBody(0, 0);
                const auto* female = actors[2]->GetVariantBody(0, 1);
                for (int32 index = 0; index < 20; ++index)
                {
                    actors[2]->ShowAppearance(0, index % 2);
                }

                TestTrue(TEXT("Rapid switches reuse both bodies"),
                         male == actors[2]->GetVariantBody(0, 0) && female == actors[2]->GetVariantBody(0, 1));
                TestTrue(TEXT("Only selected body ticks"),
                         !male->IsComponentTickEnabled() && female->IsComponentTickEnabled());
            }

            game->Shutdown();
            GEngine->DestroyWorldContext(world);
            world->DestroyWorld(false);
            game->RemoveFromRoot();
            return true;
        }));

    return true;
}

#endif
