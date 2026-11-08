#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharedWorld/Combat/Combat.h"
#include "World/WorldCombatPresentation.h"
#include "WorldCombatEffect.generated.h"
class UDecalComponent;
class UProceduralMeshComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;
UCLASS()

class RPG_API AWorldCombatEffect : public AActor
{
    GENERATED_BODY()
public:
    AWorldCombatEffect();
    void Activate(const shared_world::combat::Event&, const shared_world::combat::Ability*, double serverNow,
                  bool enemy, int32 newKind);
    void ShowMarker(FVector localPoint, float radius, bool target, bool valid = true);
    void ShowAbilityMarker(FVector origin, FVector point, const shared_world::combat::Ability&,
                           shared_world::skills::Aim, bool valid);
    void Release();
    void Update(double serverNow, const FVector& camera);

    bool Active() const
    {
        return this->active;
    }

    uint64 Action() const
    {
        return this->event.action;
    }

    uint64 Source() const
    {
        return this->event.source;
    }

    int32 EffectKind() const
    {
        return this->kind;
    }

    float Progress() const
    {
        return this->fill;
    }

private:
    void Indicator(FVector center, float radius, float yaw, float angle = 360, float halfLength = 0);
    UPROPERTY()
    TObjectPtr<UDecalComponent> decal;
    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> ribbon;
    UPROPERTY()
    TObjectPtr<UWidgetComponent> damage;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> indicatorMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> ribbonMaterial;
    shared_world::combat::Event event;
    FCombatArt art;
    double begin = 0, end = 0;
    bool active = false;
    int32 kind = 0;
    float fill = 0;
};

