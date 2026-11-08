#pragma once

#ifndef NOMINMAX

#define NOMINMAX

#endif

#include <winsock2.h>

#include <mutex>

#include <queue>

// Startup socket preallocator. Accepted sockets cannot be recycled after use;

// Acquire creates replacements after the initial stock is consumed.

class AcceptSocketPool

{

public:
    explicit AcceptSocketPool(int initialCount);

    SOCKET Acquire();

    void Cleanup();

private:
    std::mutex poolMutex;

    std::queue<SOCKET> pool;
};

