#pragma once
#include "CoreMinimal.h"

struct FWorldQuickSlotState
{
    FString key, name, detail;
    FName icon;
    uint32 binding = 0;
    bool enabled = false, selected = false, usable = false;
    float cooldown = 0;
};

struct FWorldMinimapSnapshot
{
    TArray<FVector2D> lines;
    TArray<FVector2D> enemies, npcs, exits, players;
    FString title;
    float heading = 0;
};

