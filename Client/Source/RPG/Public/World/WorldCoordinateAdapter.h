#pragma once
#include "CoreMinimal.h"
#include "SharedWorld/Core/CoordinateSystem.h"

namespace RPGWorld
{
// OriginLocation is the Unreal rebased world's global offset, not MapDefinition::origin.
inline std::optional<shared_world::WorldPosition> ToCanonical(const FVector& position, const FIntVector& originLocation)
{
    return shared_world::WorldToCanonical(position.X + static_cast<double>(originLocation.X),
                                          position.Y + static_cast<double>(originLocation.Y),
                                          position.Z + static_cast<double>(originLocation.Z));
}

inline FVector ToUnreal(shared_world::WorldPosition position, const FIntVector& originLocation)
{
    const auto VALUE = shared_world::CanonicalToWorld(position);
    return FVector(VALUE[0] - originLocation.X, VALUE[1] - originLocation.Y, VALUE[2] - originLocation.Z);
}

}

