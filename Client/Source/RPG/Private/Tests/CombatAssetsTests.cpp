#include "Misc/AutomationTest.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "SharedWorld/Combat/Combat.h"
#include "Misc/Paths.h"
#include "Characters/CharacterAppearanceDefinition.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAssetsTest, "RPG.World.CombatAssets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatAssetsTest::RunTest(const FString&)
{
    const auto CATALOG = shared_world::combat::Catalog::Load(std::filesystem::u8path(
        TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Combat")))));
    TestTrue(TEXT("Player combat locomotion is sprint"),
             CATALOG.visuals.at(0).move.find("Sprint_Loop") != std::string::npos);
    for (const TCHAR* gender : {TEXT("Male"), TEXT("Female")})
    {
        const FString PATH =
            FString::Printf(TEXT("/Game/RPG/Characters/Human/Data/DA_Human_%s.DA_Human_%s"), gender, gender);
        auto* appearance = LoadObject<UCharacterAppearanceDefinition>(nullptr, *PATH);
        if (!TestNotNull(TEXT("Human appearance"), appearance))
        {
            return false;
        }

        auto* mesh = appearance->previewMesh.LoadSynchronous();
        auto* walk = appearance->walkingAnimation.LoadSynchronous();
        auto* run = LoadObject<UAnimSequence>(nullptr, UTF8_TO_TCHAR(CATALOG.visuals.at(0).move.c_str()));
        if (!TestNotNull(TEXT("Walking animation"), walk) || !TestNotNull(TEXT("Sprint animation"), run) ||
            !TestNotNull(TEXT("Human mesh"), mesh))
        {
            return false;
        }

        TestTrue(TEXT("Human walk compatible"), walk->GetSkeleton() && walk->GetSkeleton()->IsCompatibleMesh(mesh));
        TestTrue(TEXT("Human sprint compatible"), run->GetSkeleton() && run->GetSkeleton()->IsCompatibleMesh(mesh));
        TestEqual(TEXT("Walking animation reference speed"), appearance->walkingAnimationSpeed, 120.f);
        TestEqual(TEXT("Running animation reference speed"), appearance->animationMovementSpeed, 600.f);
    }

    for (const TCHAR* model : {TEXT("Soldier"), TEXT("Veteran")})
    {
        const FString ROOT = FString::Printf(TEXT("/Game/Characters/GoblinLegionnaire/%s/"), model);
        auto* mesh = LoadObject<USkeletalMesh>(nullptr, *(ROOT + TEXT("SK_") + model + TEXT(".SK_") + model));
        if (!TestNotNull(TEXT("Goblin mesh"), mesh))
        {
            return false;
        }

        for (const TCHAR* name : {TEXT("AS_Idle"), TEXT("AS_Run"), TEXT("AS_Attack"), TEXT("AS_Hit"), TEXT("AS_Death")})
        {
            auto* anim = LoadObject<UAnimSequence>(nullptr, *(ROOT + name + TEXT(".") + name));
            if (!TestNotNull(TEXT("Combat animation"), anim))
            {
                return false;
            }

            TestTrue(TEXT("Compatible baked skeleton"),
                     anim->GetSkeleton() && anim->GetSkeleton()->IsCompatibleMesh(mesh));
            TestTrue(TEXT("Nonzero animation duration"), anim->GetPlayLength() > .1);
        }

        auto* death = LoadObject<UAnimSequence>(nullptr, *(ROOT + TEXT("AS_Death.AS_Death")));
        auto* world = UWorld::CreateWorld(EWorldType::Game, false);
        world->AddToRoot();
        TStrongObjectPtr<USkeletalMeshComponent> component(NewObject<USkeletalMeshComponent>(world));
        component->RegisterComponentWithWorld(world);
        component->SetSkeletalMesh(mesh);
        component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        component->PlayAnimation(death, false);
        component->TickAnimation(0, false);
        component->RefreshBoneTransforms();
        const FVector BEFORE = component->GetBoneLocation(TEXT("Head"), EBoneSpaces::ComponentSpace);
        component->SetPosition(death->GetPlayLength() - .01f, false);
        component->TickAnimation(0, false);
        component->RefreshBoneTransforms();
        const FVector AFTER = component->GetBoneLocation(TEXT("Head"), EBoneSpaces::ComponentSpace);
        AddInfo(FString::Printf(TEXT("%s death head before=%.1f after=%.1f displacement=%.1f"), model, BEFORE.Z,
                                AFTER.Z, FVector::Dist(BEFORE, AFTER)));
        TestTrue(TEXT("Death has a materially different end pose"), FVector::Dist(BEFORE, AFTER) > 30);
        component->UnregisterComponent();
        component.Reset();
        world->DestroyWorld(false);
        world->RemoveFromRoot();
    }

    TestEqual(TEXT("Seventeen combat definitions"), CATALOG.abilities.size(), std::size_t(17));
    TestEqual(TEXT("Boss has no basic attack"), CATALOG.monsters.at(2201).basic, 0U);
    auto* bossMesh = LoadObject<USkeletalMesh>(
        nullptr, TEXT("/Game/Characters/Megaspikan/Meshes/SK_Megaspikan_Body.SK_Megaspikan_Body"));
    if (!TestNotNull(TEXT("Megaspikan mesh"), bossMesh))
    {
        return false;
    }

    TestTrue(TEXT("Boss has a gameplay material"),
             !bossMesh->GetMaterials().IsEmpty() && bossMesh->GetMaterials()[0].MaterialInterface != nullptr);
    AddInfo(FString::Printf(TEXT("Megaspikan unscaled bounds=%s"), *bossMesh->GetBounds().BoxExtent.ToString()));
    const auto& boss = CATALOG.visuals.at(4);
    std::vector<std::string> paths = {boss.move, boss.attack, boss.cast, boss.hit, boss.death};
    for (const auto& skill : boss.skills)
    {
        paths.push_back(skill.second);
    }

    TestEqual(TEXT("Four boss skill animations"), boss.skills.size(), std::size_t(4));
    for (const auto& path : paths)
    {
        auto* anim = LoadObject<UAnimSequence>(nullptr, UTF8_TO_TCHAR(path.c_str()));
        if (!TestNotNull(TEXT("Megaspikan animation"), anim))
        {
            return false;
        }

        TestTrue(TEXT("Boss skeleton compatible"),
                 anim->GetSkeleton() && anim->GetSkeleton()->IsCompatibleMesh(bossMesh));
        TestTrue(TEXT("Boss animation duration"), anim->GetPlayLength() > .1);
        TestFalse(TEXT("Boss uses in-place animation"), anim->bEnableRootMotion);
    }

    auto* bossWorld = UWorld::CreateWorld(EWorldType::Game, false);
    bossWorld->AddToRoot();
    TStrongObjectPtr<USkeletalMeshComponent> bossComponent(NewObject<USkeletalMeshComponent>(bossWorld));
    bossComponent->RegisterComponentWithWorld(bossWorld);
    bossComponent->SetSkeletalMesh(bossMesh);
    bossComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    for (const auto& skill : boss.skills)
    {
        auto* anim = LoadObject<UAnimSequence>(nullptr, UTF8_TO_TCHAR(skill.second.c_str()));
        bossComponent->PlayAnimation(anim, false);
        bossComponent->SetPosition(0, false);
        bossComponent->TickAnimation(0, false);
        bossComponent->RefreshBoneTransforms();
        const auto BEFORE = bossComponent->GetBoneSpaceTransforms();
        bossComponent->SetPosition(anim->GetPlayLength() * .5f, false);
        bossComponent->TickAnimation(0, false);
        bossComponent->RefreshBoneTransforms();
        const auto& after = bossComponent->GetBoneSpaceTransforms();
        bool changed = false;
        for (int32 bone = 0; bone < FMath::Min(BEFORE.Num(), after.Num()); ++bone)
        {
            changed |= !BEFORE[bone].GetRotation().Equals(after[bone].GetRotation(), .001) ||
                       !BEFORE[bone].GetTranslation().Equals(after[bone].GetTranslation(), 1);
        }

        TestTrue(TEXT("Boss skill contains changing bone poses"), changed);
    }

    bossComponent->UnregisterComponent();
    bossComponent.Reset();
    bossWorld->DestroyWorld(false);
    bossWorld->RemoveFromRoot();
    return true;
}

#endif
