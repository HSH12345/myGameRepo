#include "World/GameSessionSubsystem.h"

#if !UE_BUILD_SHIPPING

// Keep the historical flag as an alias of the current input acceptance suite.

void UGameSessionSubsystem::TickActionSmoke()

{

    TickQuickSlotSmoke();
}

#endif
