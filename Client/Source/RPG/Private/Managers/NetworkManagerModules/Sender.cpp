#include "Managers/NetworkManagerModules/Sender.h"
#include "GameMain.h"
#include "HAL/PlatformProcess.h"

FPacketSenderRunnable::FPacketSenderRunnable(FSocket* inSocket, FPacketRingBuffer& inQueue, FEvent* inWakeEvent)
    : socket(inSocket), queue(inQueue), wakeEvent(inWakeEvent)
{
}

uint32 FPacketSenderRunnable::Run()
{
    struct FFinishGuard
    {
        FThreadSafeBool& finished;

        ~FFinishGuard()
        {
            this->finished = true;
        }

    } guard{this->finished};

    while (!this->stopping)
    {
        this->wakeEvent->Wait(100);
        TArray<uint8> frame;
        if (!this->queue.Dequeue(frame))
        {
            continue;
        }

        int32 totalSent = 0;
        const double SEND_DEADLINE = FPlatformTime::Seconds() + 10.0;
        while (!this->stopping && totalSent < frame.Num())
        {
            if (FPlatformTime::Seconds() >= SEND_DEADLINE)
            {
                this->exitCode = EPacketSenderExitCode::SEND_TIMEOUT;
                break;
            }

            if (!this->socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(100)))
            {
                if (this->socket->GetConnectionState() == SCS_ConnectionError)
                {
                    this->exitCode = EPacketSenderExitCode::SOCKET_ERROR_VALUE;
                    break;
                }

                continue;
            }

            int32 sentNow = 0;
            if (!this->socket->Send(frame.GetData() + totalSent, frame.Num() - totalSent, sentNow) || sentNow <= 0)
            {
                if (this->socket->GetConnectionState() == SCS_ConnectionError)
                {
                    this->exitCode = EPacketSenderExitCode::SOCKET_ERROR_VALUE;
                    break;
                }

                continue;
            }

            totalSent += sentNow;
        }

        this->queue.Complete(frame.Num());
        if (this->exitCode != EPacketSenderExitCode::STOPPED)
        {
            return static_cast<uint32>(this->exitCode);
        }

    }

    return 0;
}

void FPacketSenderRunnable::Stop()
{
    this->stopping = true;
    if (this->wakeEvent)
    {
        this->wakeEvent->Trigger();
    }

}

bool USender::Init(FSocket* newSocket)
{
    Shutdown();
    this->socket = newSocket;
    this->wakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
    if (!this->socket || !this->wakeEvent)
    {
        Shutdown();
        return false;
    }

    this->runnable = new FPacketSenderRunnable(this->socket, this->sendQueue, this->wakeEvent);
    this->thread = FRunnableThread::Create(this->runnable, TEXT("PacketSenderThread"), 0, TPri_Normal);
    if (!this->thread)
    {
        Shutdown();
        return false;
    }

    return true;
}

void USender::Shutdown()
{
    if (this->runnable)
    {
        this->runnable->Stop();
    }

    if (this->thread)
    {
        this->thread->WaitForCompletion();
        delete this->thread;
        this->thread = nullptr;
    }

    delete this->runnable;
    this->runnable = nullptr;
    if (this->wakeEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(this->wakeEvent);
        this->wakeEvent = nullptr;
    }

    this->sendQueue.Clear();
    this->socket = nullptr;
}

void USender::BeginDestroy()
{
    Shutdown();
    Super::BeginDestroy();
}

bool USender::SendPacket(RpgProtocol::EPacketType packetType, const TArray<uint8>& payload)
{
    if (!this->socket || !this->thread)
    {
        return false;
    }

    TArray<uint8> frame;
    if (!RpgProtocol::BuildFrame(packetType, payload, frame))
    {
        return false;
    }

    if (!this->sendQueue.Enqueue(MoveTemp(frame)))
    {
        return false;
    }

    this->wakeEvent->Trigger();
    return true;
}

