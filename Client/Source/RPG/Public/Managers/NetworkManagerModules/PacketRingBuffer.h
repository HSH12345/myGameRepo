#pragma once

#include "CoreMinimal.h"

// Fixed-slot, byte-bounded MPSC-safe ring. A dequeued packet remains accounted
// until Complete() so an in-flight send cannot bypass the memory limit.
class FPacketRingBuffer final
{
public:
    explicit FPacketRingBuffer(int32 inCapacity = 4096, int64 inMaxBytes = 4 * 1024 * 1024)
        : capacity(inCapacity), maxBytes(inMaxBytes)
    {
        this->slots.SetNum(inCapacity);
    }

    bool Enqueue(TArray<uint8>&& packet)
    {
        FScopeLock lock(&this->mutex);
        if (packet.IsEmpty() || this->count >= this->capacity || this->bytes + packet.Num() > this->maxBytes)
        {
            return false;
        }

        this->bytes += packet.Num();
        this->slots[this->tail] = MoveTemp(packet);
        this->tail = (this->tail + 1) % this->capacity;
        ++this->count;
        return true;
    }

    bool Dequeue(TArray<uint8>& outPacket)
    {
        FScopeLock lock(&this->mutex);
        if (this->count == 0)
        {
            return false;
        }

        outPacket = MoveTemp(this->slots[this->head]);
        this->head = (this->head + 1) % this->capacity;
        --this->count;
        return true;
    }

    void Complete(int32 packetBytes)
    {
        FScopeLock lock(&this->mutex);
        this->bytes = FMath::Max<int64>(0, this->bytes - packetBytes);
    }

    void Clear()
    {
        FScopeLock lock(&this->mutex);
        for (TArray<uint8>& slot : this->slots)
        {
            slot.Reset();
        }

        this->head = this->tail = this->count = 0;
        this->bytes = 0;
    }

    bool IsEmpty() const
    {
        FScopeLock lock(&this->mutex);
        return this->count == 0;
    }

private:
    mutable FCriticalSection mutex;
    TArray<TArray<uint8>> slots;
    const int32 capacity;
    const int64 maxBytes;
    int32 head = 0;
    int32 tail = 0;
    int32 count = 0;
    int64 bytes = 0;
};

