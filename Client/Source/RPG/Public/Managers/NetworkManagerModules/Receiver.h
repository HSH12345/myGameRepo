#pragma once

#include "CoreMinimal.h"
#include "Managers/NetworkManagerModules/PacketRingBuffer.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Sockets.h"
#include "Receiver.generated.h"

enum class EPacketReceiverExitCode : uint32
{
    STOPPED = 0,
    SOCKET_ERROR_VALUE = 1,
    PROTOCOL_ERROR = 2,
    QUEUE_OVERFLOW = 3,
    PEER_CLOSED = 4
};

class FPacketReceiverRunnable final : public FRunnable
{
public:
    FPacketReceiverRunnable(FSocket* inSocket, FPacketRingBuffer& inQueue);
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;

    bool HasStopped() const
    {
        return this->finished;
    }

    EPacketReceiverExitCode GetExitCode() const
    {
        return this->exitCode;
    }

private:
    FSocket* socket;
    FPacketRingBuffer& packetQueue;
    FThreadSafeBool stopping{false};
    FThreadSafeBool finished{false};
    EPacketReceiverExitCode exitCode = EPacketReceiverExitCode::STOPPED;
};

UCLASS()

class RPG_API UReceiver : public UObject
{
    GENERATED_BODY()
public:
    bool Init(FSocket* inSocket);
    void Shutdown();
    virtual void BeginDestroy() override;
    bool DequeueRawPacket(TArray<uint8>& outPayload);

    bool HasStopped() const
    {
        return this->receiverRunnable == nullptr || this->receiverRunnable->HasStopped();
    }

    bool IsQueueEmpty() const;

    EPacketReceiverExitCode GetExitCode() const
    {
        return this->receiverRunnable ? this->receiverRunnable->GetExitCode() : EPacketReceiverExitCode::STOPPED;
    }

private:
    FPacketRingBuffer packetQueue;
    FPacketReceiverRunnable* receiverRunnable = nullptr;
    FRunnableThread* receiverThread = nullptr;
};

