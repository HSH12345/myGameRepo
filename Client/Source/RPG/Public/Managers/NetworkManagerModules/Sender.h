#pragma once

#include "CoreMinimal.h"
#include "Managers/NetworkManagerModules/PacketRingBuffer.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Sockets.h"
#include "RPG/Packet/BinaryProtocol.h"
#include "Sender.generated.h"

enum class EPacketSenderExitCode : uint32
{
    STOPPED = 0,
    SOCKET_ERROR_VALUE = 1,
    SEND_TIMEOUT = 2
};

class FPacketSenderRunnable final : public FRunnable
{
public:
    FPacketSenderRunnable(FSocket* inSocket, FPacketRingBuffer& inQueue, FEvent* inWakeEvent);
    virtual uint32 Run() override;
    virtual void Stop() override;

    bool HasStopped() const
    {
        return this->finished;
    }

    EPacketSenderExitCode GetExitCode() const
    {
        return this->exitCode;
    }

private:
    FSocket* socket;
    FPacketRingBuffer& queue;
    FEvent* wakeEvent;
    FThreadSafeBool stopping{false};
    FThreadSafeBool finished{false};
    EPacketSenderExitCode exitCode = EPacketSenderExitCode::STOPPED;
};

UCLASS()

class RPG_API USender : public UObject
{
    GENERATED_BODY()
public:
    bool Init(FSocket* newSocket);
    void Shutdown();
    virtual void BeginDestroy() override;
    bool SendPacket(RpgProtocol::EPacketType packetType, const TArray<uint8>& payload);

    bool HasStopped() const
    {
        return this->runnable == nullptr || this->runnable->HasStopped();
    }

    EPacketSenderExitCode GetExitCode() const
    {
        return this->runnable ? this->runnable->GetExitCode() : EPacketSenderExitCode::STOPPED;
    }

private:
    FSocket* socket = nullptr;
    FPacketRingBuffer sendQueue;
    FEvent* wakeEvent = nullptr;
    FPacketSenderRunnable* runnable = nullptr;
    FRunnableThread* thread = nullptr;
};

