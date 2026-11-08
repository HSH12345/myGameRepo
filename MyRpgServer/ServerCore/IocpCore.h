#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class IocpCore
{
public:
    using CompletionCallback = std::function<void(DWORD, DWORD, ULONG_PTR, LPOVERLAPPED)>;
    IocpCore(DWORD concurrency, CompletionCallback callback);
    ~IocpCore();
    IocpCore(const IocpCore&) = delete;
    IocpCore& operator=(const IocpCore&) = delete;

    HANDLE GetHandle() const
    {
        return this->iocpHandle;
    }

    bool IsValid() const
    {
        return this->iocpHandle != nullptr;
    }

    bool Start(unsigned int threadCount);
    void Stop();

private:
    void WorkerThread();
    HANDLE iocpHandle = nullptr;
    std::vector<std::thread> workerThreads;
    std::atomic<bool> running{false};
    CompletionCallback completionCallback;
};

