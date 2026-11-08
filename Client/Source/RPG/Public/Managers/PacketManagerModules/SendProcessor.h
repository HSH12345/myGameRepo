#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SendProcessor.generated.h"

namespace RpgProtocol
{
struct FCreateCharacterRequestPacket;
}

UCLASS()

class RPG_API USendProcessor : public UObject
{
    GENERATED_BODY()

public:
    void SendTestPacket(const FString& msg);
    bool SendGuestLoginPacket(const FString& uid, const FString& token);
    bool SendFirebaseLoginPacket(const FString& idToken);

    uint32 GetPendingLoginRequestId() const
    {
        return this->pendingLoginRequestId;
    }

    bool CompleteLoginRequest(uint32 requestId, bool firebase = false);
    bool SendCharacterListPacket(uint32 requestId);
    bool SendCreateCharacterPacket(const RpgProtocol::FCreateCharacterRequestPacket& packet);

    void CancelLoginRequest()
    {
        this->pendingLoginRequestId = 0;
    }

private:
    uint32 nextRequestId = 1;
    uint32 pendingLoginRequestId = 0;
    bool pendingFirebase = false;
};

