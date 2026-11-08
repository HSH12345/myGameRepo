#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RPG/Packet/BinaryProtocol.h"
#include "ReceiveDispatcher.generated.h"
using HandlerFunc = TFunction<void(const uint8* payload, int32 payloadSize)>;

UCLASS()

class RPG_API UReceiveDispatcher : public UObject
{
    GENERATED_BODY()

public:
    void Init();
    void HandlePacket(const TArray<uint8>& rawPacket);

private:
    TMap<RpgProtocol::EPacketType, HandlerFunc> handlers;
    HandlerFunc GetHandler(RpgProtocol::EPacketType packetType) const;

    void RegisterHandler();

    void HandleLogin(const uint8* payload, int32 payloadSize);
    void HandleFirebaseLogin(const uint8* payload, int32 payloadSize);
    void HandleCharacterList(const uint8* payload, int32 payloadSize);
    void HandleCreateCharacter(const uint8* payload, int32 payloadSize);
};

