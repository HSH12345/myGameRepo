#include "Managers/NetworkManagerModules/Receiver.h"
#include "RPG/Packet/BinaryProtocol.h"
#include "SocketSubsystem.h"

FPacketReceiverRunnable::FPacketReceiverRunnable(FSocket* inSocket, FPacketRingBuffer& inQueue)
    : socket(inSocket), packetQueue(inQueue)
{
}

bool FPacketReceiverRunnable::Init()
{
    return this->socket != nullptr;
}

uint32 FPacketReceiverRunnable::Run()
{
    struct FFinishGuard
    {
        FThreadSafeBool& finished;

        ~FFinishGuard()
        {
            this->finished = true;
        }

    } finishGuard{this->finished};

    constexpr int32 BUFFER_SIZE = 16 * 1024;
    TArray<uint8> buffer;
    buffer.SetNumUninitialized(BUFFER_SIZE);
    TArray<uint8> streamBuffer;
    streamBuffer.Reserve(BUFFER_SIZE * 2);
    int32 readOffset = 0;

    while (!this->stopping)
    {
        if (!this->socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
        {
            continue;
        }

        int32 read = 0;
        if (!this->socket->Recv(buffer.GetData(), BUFFER_SIZE, read))
        {
            const ESocketErrors ERROR_CODE = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
            if (ERROR_CODE == SE_EWOULDBLOCK)
            {
                continue;
            }

            this->exitCode = ERROR_CODE == SE_NO_ERROR ? EPacketReceiverExitCode::PEER_CLOSED
                                                       : EPacketReceiverExitCode::SOCKET_ERROR_VALUE;
            return static_cast<uint32>(this->exitCode);
        }

        if (read == 0)
        {
            this->exitCode = EPacketReceiverExitCode::PEER_CLOSED;
            return static_cast<uint32>(this->exitCode);
        }

        if (readOffset > 0 && (readOffset >= streamBuffer.Num() / 2 || streamBuffer.GetSlack() < read))
        {
            streamBuffer.RemoveAt(0, readOffset, EAllowShrinking::No);
            readOffset = 0;
        }

        streamBuffer.Append(buffer.GetData(), read);

        while (streamBuffer.Num() - readOffset >= RpgProtocol::HEADER_SIZE)
        {
            RpgProtocol::FFrameHeader header;
            if (!RpgProtocol::DecodeHeader(streamBuffer.GetData() + readOffset, streamBuffer.Num() - readOffset,
                                           header))
            {
                this->exitCode = EPacketReceiverExitCode::PROTOCOL_ERROR;
                return static_cast<uint32>(this->exitCode);
            }

            const int32 FRAME_SIZE = RpgProtocol::HEADER_SIZE + static_cast<int32>(header.payloadSize);
            if (streamBuffer.Num() - readOffset < FRAME_SIZE)
            {
                break;
            }

            TArray<uint8> frame;
            frame.Append(streamBuffer.GetData() + readOffset, FRAME_SIZE);
            if (!this->packetQueue.Enqueue(MoveTemp(frame)))
            {
                this->exitCode = EPacketReceiverExitCode::QUEUE_OVERFLOW;
                return static_cast<uint32>(this->exitCode);
            }

            readOffset += FRAME_SIZE;
        }

        if (readOffset == streamBuffer.Num())
        {
            streamBuffer.Reset();
            readOffset = 0;
        }

        if (streamBuffer.Num() - readOffset >
            RpgProtocol::HEADER_SIZE + static_cast<int32>(RpgProtocol::MAX_PAYLOAD_SIZE))
        {
            this->exitCode = EPacketReceiverExitCode::PROTOCOL_ERROR;
            return static_cast<uint32>(this->exitCode);
        }

    }

    return 0;
}

void FPacketReceiverRunnable::Stop()
{
    this->stopping = true;
}

bool UReceiver::Init(FSocket* inSocket)
{
    Shutdown();
    if (!inSocket)
    {
        return false;
    }

    this->receiverRunnable = new FPacketReceiverRunnable(inSocket, this->packetQueue);
    this->receiverThread =
        FRunnableThread::Create(this->receiverRunnable, TEXT("PacketReceiverThread"), 0, TPri_Normal);
    if (!this->receiverThread)
    {
        Shutdown();
        return false;
    }

    return true;
}

void UReceiver::Shutdown()
{
    if (this->receiverRunnable)
    {
        this->receiverRunnable->Stop();
    }

    if (this->receiverThread)
    {
        this->receiverThread->WaitForCompletion();
        delete this->receiverThread;
        this->receiverThread = nullptr;
    }

    delete this->receiverRunnable;
    this->receiverRunnable = nullptr;
    this->packetQueue.Clear();
}

void UReceiver::BeginDestroy()
{
    Shutdown();
    Super::BeginDestroy();
}

bool UReceiver::DequeueRawPacket(TArray<uint8>& outPayload)
{
    if (!this->packetQueue.Dequeue(outPayload))
    {
        return false;
    }

    this->packetQueue.Complete(outPayload.Num());
    return true;
}

bool UReceiver::IsQueueEmpty() const
{
    return this->packetQueue.IsEmpty();
}

