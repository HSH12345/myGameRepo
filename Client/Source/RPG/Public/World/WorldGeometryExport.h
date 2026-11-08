#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
#include "SharedWorld/Tools/MapBakeTool/SourceGeometry.h"
class UWorld;

namespace RPGWorld
{
// Caller owns actor loading policy. This function never loads the whole world
// or saves assets. Empty loaded regions return an empty geometry collection.
RPG_API shared_world::SourceGeometry ExportLoadedGeometry(UWorld* world, int32 mapId, int32 mapVersion,
                                                          const FString& mapPath, int32 verticalPadding = 512);
}

#endif
