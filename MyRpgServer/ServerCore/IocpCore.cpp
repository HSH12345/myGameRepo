#include "IocpCore.h"
#include "StructuredLogger.h"

#include <iostream>

IocpCore::IocpCore(DWORD concurrency, CompletionCallback callback) : completionCallback(std::move(callback))
{
    this->iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, concurrency);
    if (this->iocpHandle == nullptr)
    {
        RPG_LOG_CRITICAL("IOCP", "CreateIoCompletionPort failed. error=" << GetLastError());
    }

}

IocpCore::~IocpCore()
{
    Stop();
    if (this->iocpHandle != nullptr)
    {
        CloseHandle(this->iocpHandle);
        this->iocpHandle = nullptr;
    }

}

bool IocpCore::Start(unsigned int threadCount)
{
    if (this->iocpHandle == nullptr || threadCount == 0)
    {
        return false;
    }

    if (this->running.exchange(true))
    {
        return true;
    }

    try
    {
        this->workerThreads.reserve(threadCount);
        for (unsigned int i = 0; i < threadCount; ++i)
        {
            this->workerThreads.emplace_back(&IocpCore::WorkerThread, this);
        }

    }

    catch (const std::exception& exception)
    {
        RPG_LOG_CRITICAL("IOCP", "Failed to create IOCP worker pool. error=" << exception.what());
        this->running = false;
        for (std::size_t i = 0; i < this->workerThreads.size(); ++i)
        {
            PostQueuedCompletionStatus(this->iocpHandle, 0, 0, nullptr);
        }

        for (auto& thread : this->workerThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }

        }

        this->workerThreads.clear();
        return false;
    }

    return true;
}

void IocpCore::Stop()
{
    if (!this->running.exchange(false))
    {
        return;
    }

    for (size_t i = 0; i < this->workerThreads.size(); ++i)
    {
        PostQueuedCompletionStatus(this->iocpHandle, 0, 0, nullptr);
    }

    for (auto& thread : this->workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }

    }

    this->workerThreads.clear();
}

void IocpCore::WorkerThread()
{
    while (true)
    {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL result =
            GetQueuedCompletionStatus(this->iocpHandle, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (overlapped == nullptr)
        {
            if (!this->running)
            {
                break;
            }

            if (!result)
            {
                RPG_LOG_ERROR("IOCP", "GetQueuedCompletionStatus failed. error=" << GetLastError());
            }

            continue;
        }

        const DWORD COMPLETION_ERROR = result ? ERROR_SUCCESS : GetLastError();
        if (!result && COMPLETION_ERROR != ERROR_OPERATION_ABORTED)
        {
            RPG_LOG_ERROR("IOCP", "I/O completion failed. error=" << COMPLETION_ERROR);
        }

        if (this->completionCallback)
        {
            try
            {
                this->completionCallback(bytesTransferred, COMPLETION_ERROR, completionKey, overlapped);
            }

            catch (const std::exception& exception)
            {
                RPG_LOG_CRITICAL("IOCP", "Completion callback failed. error=" << exception.what());
            }

            catch (...)
            {
                RPG_LOG_CRITICAL("IOCP", "Completion callback failed with an unknown exception.");
            }

        }

    }

}

