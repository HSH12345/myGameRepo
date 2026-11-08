#include "World/WorldCombatEffect.h"
#include "World/WorldDamageNumber.h"
#include "CombatRibbonGeometry.h"
#include "Components/DecalComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

AWorldCombatEffect::AWorldCombatEffect()
{
    this->PrimaryActorTick.bCanEverTick = false;
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    this->decal = CreateDefaultSubobject<UDecalComponent>(TEXT("Indicator"));
    this->decal->SetupAttachment(this->RootComponent);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> indicatorAsset(
        TEXT("/Game/RPG/Combat/M_SkillIndicatorV2.M_SkillIndicatorV2"));
    if (indicatorAsset.Succeeded())
    {
        this->decal->SetDecalMaterial(indicatorAsset.Object);
    }

    this->decal->SetFadeScreenSize(.001f);
    this->decal->SortOrder = 10;
    this->ribbon = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ribbon"));
    this->ribbon->SetupAttachment(this->RootComponent);
    this->ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->ribbon->SetCastShadow(false);
    this->ribbon->SetReceivesDecals(false);
    this->ribbon->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ribbonAsset(
        TEXT("/Game/RPG/Combat/M_CombatRibbon.M_CombatRibbon"));
    if (ribbonAsset.Succeeded())
    {
        this->ribbon->SetMaterial(0, ribbonAsset.Object);
    }

    this->damage = CreateDefaultSubobject<UWidgetComponent>(TEXT("Damage"));
    this->damage->SetupAttachment(this->RootComponent);
    this->damage->SetWidgetSpace(EWidgetSpace::Screen);
    this->damage->SetDrawSize(FVector2D(260, 90));
    this->damage->SetPivot(FVector2D(.5, .5));
    this->damage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->damage->SetGenerateOverlapEvents(false);
    this->damage->SetCastShadow(false);
    Release();
}

void AWorldCombatEffect::Release()
{
    this->active = false;
    this->fill = 0;
    this->event = {};
    SetActorHiddenInGame(true);
    this->decal->SetVisibility(false);
    this->ribbon->SetVisibility(false);
    this->damage->SetVisibility(false);
    this->damage->SetComponentTickEnabled(false);
    SetActorScale3D(FVector::OneVector);
    SetActorRotation(FRotator::ZeroRotator);
    this->ribbon->SetRelativeScale3D(FVector::OneVector);
}

void AWorldCombatEffect::Indicator(FVector center, float radius, float yaw, float angle, float halfLength)
{
    // Project through steps/ramps. Tagged walls and characters opt out of
    // receiving decals, while step risers remain continuous receiving terrain.
    SetActorLocation(center + FVector(0, 0, 200));
    SetActorRotation(FRotator(0, yaw, 0));
    this->decal->SetRelativeRotation(FRotator(-90, 0, 0));
    const FVector SIZE(1200, radius, halfLength > 0 ? halfLength : radius);
    if (!this->decal->DecalSize.Equals(SIZE))
    {
        this->decal->DecalSize = SIZE;
        this->decal->UpdateBounds();
        this->decal->MarkRenderTransformDirty();
    }

    if (!this->indicatorMaterial)
    {
        this->indicatorMaterial = this->decal->CreateDynamicMaterialInstance();
    }

    if (this->indicatorMaterial)
    {
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Rectangle"), halfLength > 0 ? 1 : 0);
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Angle"), angle);
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Progress"), 0);
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Opacity"), 1);
    }

    this->decal->SetVisibility(true);
}

void AWorldCombatEffect::Activate(const shared_world::combat::Event& e, const shared_world::combat::Ability* ability,
                                  double now, bool enemy, int32 inKind)
{
    using namespace shared_world::combat;
    Release();
    this->event = e;
    this->kind = inKind;
    this->art = RPGCombatArt::Get(e.skill);
    this->active = true;
    SetActorHiddenInGame(false);
    const FVector REBASE(GetWorld()->OriginLocation), ORIGIN = FVector(e.origin.x, e.origin.y, e.origin.z) - REBASE,
                                                      POINT = FVector(e.point.x, e.point.y, e.point.z) - REBASE;
    const float YAW = e.yaw / 1000.f;
    this->begin = double(e.startMs) / 1000;
    this->end = double(e.endMs) / 1000;
    const float RADIUS = ability ? FMath::Max(45U, ability->radius) : 65, RANGE = ability ? ability->range : 150,
                ANGLE = ability ? ability->angle : 90;
    if (this->kind == 0 && ability)
    {
        const bool LINE = ability->shape == Shape::LINE;
        const FVector CENTER = LINE ? ORIGIN + FRotator(0, YAW, 0).Vector() * (RANGE * .5) : POINT;
        Indicator(CENTER, RADIUS, YAW, ability->shape == Shape::CONE ? ANGLE : 360, LINE ? RANGE * .5 : 0);
        if (this->indicatorMaterial)
        {
            this->indicatorMaterial->SetVectorParameterValue(TEXT("Color"), enemy ? FLinearColor(1, .055, .02)
                                                                                  : FLinearColor(.03, .7, 1));
        }

        return;
    }

    if (this->kind == 3)
    {
        this->begin = now;
        this->end = now + .9;
        SetActorLocation(POINT);
        if (!this->damage->GetUserWidgetObject())
        {
            this->damage->SetWidgetClass(UWorldDamageNumber::StaticClass());
        }

        this->damage->InitWidget();
        this->damage->SetComponentTickEnabled(true);
        this->damage->SetVisibility(true);
        if (auto* widget = Cast<UWorldDamageNumber>(this->damage->GetUserWidgetObject()))
        {
            widget->Present(e.value, FMath::Lerp(this->art.core, FLinearColor(1, .93, .75), .55f), e.action + e.target,
                            enemy);
        }

        return;
    }

    if (this->kind != 1)
    {
        this->begin = now;
        this->end = now + (this->kind == 5 ? .48 : this->art.life);
    }

    const bool DIRECTIONAL =
        this->kind == 2 && (this->art.style == ECombatArt::BEAM || this->art.style == ECombatArt::RUSH ||
                            this->art.style == ECombatArt::SHADOW || (ability && ability->shape == Shape::CONE));
    SetActorLocation(this->kind == 1 ? ORIGIN : DIRECTIONAL ? ORIGIN : POINT);
    SetActorRotation(FRotator(0, YAW, 0));
    if (this->kind == 2 && ability &&
        (ability->shape == Shape::CIRCLE || ability->shape == Shape::CONE || ability->shape == Shape::LINE))
    {
        const bool LINE = ability->shape == Shape::LINE;
        const auto LOCATION = GetActorLocation();
        Indicator(LINE ? ORIGIN + FRotator(0, YAW, 0).Vector() * RANGE * .5 : POINT, RADIUS, YAW,
                  ability->shape == Shape::CONE ? ANGLE : 360, LINE ? RANGE * .5 : 0);
        this->ribbon->SetRelativeLocation(GetActorTransform().InverseTransformPosition(LOCATION));
        if (this->indicatorMaterial)
        {
            this->indicatorMaterial->SetScalarParameterValue(TEXT("Progress"), 1);
            this->indicatorMaterial->SetVectorParameterValue(TEXT("Color"), this->art.color);
        }

    }
    else
    {
        this->ribbon->SetRelativeLocation(FVector::ZeroVector);
    }

    if (!this->ribbonMaterial)
    {
        this->ribbonMaterial = this->ribbon->CreateDynamicMaterialInstance(0);
    }

    if (this->ribbonMaterial)
    {
        this->ribbonMaterial->SetVectorParameterValue(TEXT("Color"), this->art.color);
        this->ribbonMaterial->SetVectorParameterValue(TEXT("Core"), this->art.core);
        this->ribbonMaterial->SetScalarParameterValue(TEXT("Power"), this->art.power);
        this->ribbonMaterial->SetScalarParameterValue(TEXT("Flame"), this->art.style == ECombatArt::FIRE ? 1 : 0);
        this->ribbonMaterial->SetScalarParameterValue(TEXT("Age"), .1f);
        this->ribbonMaterial->SetScalarParameterValue(TEXT("Projectile"), this->kind == 1 ? 1 : 0);
    }

    FCombatRibbonGeometry geometry;
    geometry.Build(this->ribbon, this->art, this->kind, RADIUS, RANGE, ANGLE, uint32(e.action));
    this->ribbon->SetVisibility(true);
}

void AWorldCombatEffect::ShowMarker(FVector point, float radius, bool target, bool valid)
{
    if (!this->active || this->kind != 4)
    {
        Release();
    }

    this->active = true;
    this->kind = 4;
    SetActorHiddenInGame(false);
    Indicator(point, radius, 0);
    if (this->indicatorMaterial)
    {
        this->indicatorMaterial->SetVectorParameterValue(TEXT("Color"), !valid   ? FLinearColor(1, .08, .02)
                                                                        : target ? FLinearColor(1, .55, .04)
                                                                                 : FLinearColor(.025, .65, 1));
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Progress"), target ? 0 : .04);
    }

}

void AWorldCombatEffect::ShowAbilityMarker(FVector origin, FVector point, const shared_world::combat::Ability& a,
                                           shared_world::skills::Aim aim, bool valid)
{
    using namespace shared_world::combat;
    using namespace shared_world::skills;
    if (aim == Aim::GROUND)
    {
        ShowMarker(point, a.radius, false, valid);
        return;
    }

    if (aim == Aim::SELF)
    {
        ShowMarker(origin, a.radius, false, valid);
        return;
    }

    const FVector D = (point - origin).GetSafeNormal2D();
    const float YAW = D.Rotation().Yaw;
    ShowMarker(origin, a.shape == Shape::CONE ? a.radius : a.range, false, valid);
    Indicator(a.shape == Shape::LINE ? origin + D * (a.range * .5) : origin,
              a.shape == Shape::LINE   ? a.radius
              : a.shape == Shape::CONE ? a.radius
                                       : a.range,
              YAW,
              a.shape == Shape::CONE   ? a.angle
              : a.shape == Shape::LINE ? 360
                                       : 8,
              a.shape == Shape::LINE ? a.range * .5 : 0);
    if (this->indicatorMaterial)
    {
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Progress"), .04);
    }

}

void AWorldCombatEffect::Update(double now, const FVector&)
{
    if (!this->active || this->kind == 4)
    {
        return;
    }

    this->fill = FMath::Clamp(float((now - this->begin) / FMath::Max(.001, this->end - this->begin)), 0.f, 1.f);
    if (this->kind == 0 && this->indicatorMaterial)
    {
        this->indicatorMaterial->SetScalarParameterValue(TEXT("Progress"), this->fill);
    }

    if (this->kind == 1)
    {
        const FVector FROM(this->event.origin.x, this->event.origin.y, this->event.origin.z),
            TO(this->event.point.x, this->event.point.y, this->event.point.z);
        SetActorLocation(FMath::Lerp(FROM, TO, this->fill) - FVector(GetWorld()->OriginLocation));
        SetActorRotation((TO - FROM).Rotation());
    }

    if (this->kind == 3)
    {
        if (auto* widget = Cast<UWorldDamageNumber>(this->damage->GetUserWidgetObject()))
        {
            widget->Animate(this->fill);
        }

    }
    else if (this->kind != 0)
    {
        if (this->ribbonMaterial)
        {
            this->ribbonMaterial->SetScalarParameterValue(TEXT("Age"),
                                                          this->kind == 1 ? .18 + this->fill * .18 : this->fill);
        }

        if (this->kind == 2 || this->kind == 5)
        {
            const float SCALE =
                this->kind == 5 ? FMath::Lerp(.5f, 1.3f, this->fill) : FMath::Lerp(.68f, 1.f, FMath::Sqrt(this->fill));
            const bool LINEAR = this->art.style == ECombatArt::BEAM || this->art.style == ECombatArt::RUSH ||
                                this->art.style == ECombatArt::SHADOW;
            this->ribbon->SetRelativeScale3D(LINEAR ? FVector(1, SCALE, SCALE) : FVector(SCALE));
            if (this->indicatorMaterial && this->decal->IsVisible())
            {
                this->indicatorMaterial->SetScalarParameterValue(TEXT("Opacity"), .28f * (1 - this->fill));
            }

        }

    }

    if (now >= this->end && this->kind != 1)
    {
        Release();
    }

    if (this->kind == 1 && now > this->end + .3)
    {
        Release();
    }

}

