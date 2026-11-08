#pragma once
#include "CoreMinimal.h"

enum class ECombatArt : uint8
{
    ARCANE,
    FIRE,
    STEEL,
    RUSH,
    SHADOW,
    LANCE,
    NOVA,
    BEAM,
    SHOCK,
    ACID,
    ROAR
};

struct FCombatArt
{
    ECombatArt style = ECombatArt::ARCANE;
    FLinearColor color = FLinearColor(.04f, .5f, 1), core = FLinearColor(.65f, .9f, 1);
    FName icon = NAME_None;
    float life = .65f, width = 1.f, power = 2.f;
};

// Client presentation data is deliberately outside the authoritative combat checksum.
namespace RPGCombatArt
{
const FCombatArt& Get(uint32 skill);
bool CatalogReady();
const TMap<uint32, FCombatArt>& All();
}

