#include "World/WorldCombatEffect.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "World/WorldCombatPresentation.h"
#include "UIs/RpgUITheme.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Components/DecalComponent.h"
#include "Components/WidgetComponent.h"
#include "ProceduralMeshComponent.h"
#include "UObject/GarbageCollection.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPresentationTest, "RPG.World.CombatPresentation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatPresentationTest::RunTest(const FString&)
{
    TestTrue(TEXT("Validated presentation document"), RPGCombatArt::CatalogReady());
    TestEqual(TEXT("All combat abilities have art"), RPGCombatArt::All().Num(), 17);
    TSet<UTexture2D*> icons;
    for (uint32 id : {3, 4, 10, 11, 12, 13, 14, 15, 16, 17})
    {
        auto* icon = RpgUI::Texture(RpgUI::SkillIcon(id));
        TestNotNull(TEXT("Generated skill icon"), icon);
        icons.Add(icon);
    }

    TestEqual(TEXT("Every learned skill has a unique texture"), icons.Num(), 10);
    auto* ground = LoadObject<UMaterial>(nullptr, TEXT("/Game/RPG/Combat/M_SkillIndicatorV2.M_SkillIndicatorV2"));
    if (!TestNotNull(TEXT("Terrain projected indicator"), ground))
    {
        return false;
    }

    TestEqual(TEXT("Projection follows actual receiving geometry"), int32(ground->MaterialDomain),
              int32(MD_DeferredDecal));
    auto* energy = LoadObject<UMaterial>(nullptr, TEXT("/Game/RPG/Combat/M_CombatRibbon.M_CombatRibbon"));
    if (!TestNotNull(TEXT("Ribbon shader"), energy))
    {
        return false;
    }

    TestEqual(TEXT("Energy uses additive blending"), energy->GetBlendMode(), BLEND_Additive);
    auto* world = UWorld::CreateWorld(EWorldType::Game, false);
    world->AddToRoot();
    auto* fx = world->SpawnActor<AWorldCombatEffect>();
    const auto CATALOG = shared_world::combat::Catalog::Load(std::filesystem::u8path(
        TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Combat")))));
    const FVector ORIGIN(120, -240, 80);
    fx->ShowMarker(ORIGIN, 100, false);
    fx->ShowMarker(ORIGIN, 350, false);
    const auto* resized = fx->FindComponentByClass<UDecalComponent>();
    TestTrue(TEXT("Changing skill radius at the same cursor point refreshes culling bounds"),
             FMath::IsNearlyEqual(resized->Bounds.BoxExtent.X, 350., .1) &&
                 FMath::IsNearlyEqual(resized->Bounds.BoxExtent.Y, 350., .1));
    for (float yaw : {0.f, 90.f, 180.f, 270.f})
    {
        auto a = CATALOG.abilities.at(16);
        const FVector DIRECTION = FRotator(0, yaw, 0).Vector();
        fx->ShowAbilityMarker(ORIGIN, ORIGIN + DIRECTION * 1000, a, shared_world::skills::Aim::DIRECTION, true);
        const auto* decal = fx->FindComponentByClass<UDecalComponent>();
        const FVector HEAD = decal->GetComponentTransform().TransformPosition(FVector(0, 0, decal->DecalSize.Z));
        const FVector TAIL = decal->GetComponentTransform().TransformPosition(FVector(0, 0, -decal->DecalSize.Z));
        TestTrue(TEXT("UV +X points at the skill endpoint at every yaw"),
                 FVector::Dist2D(HEAD, ORIGIN + DIRECTION * a.range) < 1);
        TestTrue(TEXT("UV -X starts at the caster"), FVector::Dist2D(TAIL, ORIGIN) < 1);
        TestTrue(TEXT("Projection spans uneven floor elevation"), decal->DecalSize.X >= 1000);
    }

    for (const auto& row : CATALOG.abilities)
    {
        shared_world::combat::Event e;
        e.skill = row.first;
        e.action = row.first;
        e.endMs = 1000;
        e.origin = {10, 20, 100};
        e.point = {1010, 20, 200};
        for (int32 kind : {1, 2, 5})
        {
            fx->Activate(e, &row.second, 0, false, kind);
            auto* mesh = fx->FindComponentByClass<UProceduralMeshComponent>();
            const auto* section = mesh->GetProcMeshSection(0);
            TestTrue(TEXT("Per-effect vertex budget"),
                     section && section->ProcVertexBuffer.Num() > 0 && section->ProcVertexBuffer.Num() <= 1536);
            TestEqual(TEXT("Cosmetic ribbons cannot block attacks or movement"), mesh->GetCollisionEnabled(),
                      ECollisionEnabled::NoCollision);
            if (section)
            {
                for (const auto& v : section->ProcVertexBuffer)
                {
                    if (v.Position.ContainsNaN())
                    {
                        AddError(TEXT("Non-finite visual vertex"));
                        break;
                    }

                }

            }

            if (kind == 1)
            {
                fx->Update(.5, FVector::ZeroVector);
                TestTrue(TEXT("Projectile uses authoritative timeline with cosmetic endpoints"),
                         fx->GetActorLocation().Equals(FVector(510, 20, 150), .1));
            }

            fx->Release();
            TestFalse(TEXT("Released effect inactive"), fx->Active());
        }

    }

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Pooled actor/materials survive GC"), IsValid(fx));
    TestEqual(TEXT("Damage numerals keep readable screen size"),
              fx->FindComponentByClass<UWidgetComponent>()->GetWidgetSpace(), EWidgetSpace::Screen);
    world->DestroyWorld(false);
    world->RemoveFromRoot();
    return true;
}

#endif
