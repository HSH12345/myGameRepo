#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Managers/PacketManagerModules/ReceiveDispatcher.h"
#include "Managers/PacketManagerModules/SendProcessor.h"
#include "PacketManager.generated.h"

UCLASS()

class RPG_API UPacketManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
private:
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;

public:
    // Outer alone does not keep a UObject alive. These references must be visible
    // to Unreal GC for the lifetime of the GameInstance subsystem.
    UPROPERTY(Transient)
    TObjectPtr<UReceiveDispatcher> receiveDispatcher = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USendProcessor> sendProcessor = nullptr;
};

