#include "AcceptSocketPool.h"

#include "SocketUtils.h"

AcceptSocketPool::AcceptSocketPool(int initialCount)
{
    for (int i = 0; i < initialCount; ++i)
    {
        SOCKET socket = SocketUtils::CreateClientSocket();
        if (socket != INVALID_SOCKET)
        {
            this->pool.push(socket);
        }

    }

}

SOCKET AcceptSocketPool::Acquire()
{
    std::lock_guard<std::mutex> lock(this->poolMutex);
    if (!this->pool.empty())
    {
        SOCKET socket = this->pool.front();
        this->pool.pop();
        return socket;
    }

    return SocketUtils::CreateClientSocket();
}

void AcceptSocketPool::Cleanup()
{
    std::lock_guard<std::mutex> lock(this->poolMutex);
    while (!this->pool.empty())
    {
        SOCKET socket = this->pool.front();
        this->pool.pop();
        SocketUtils::CloseSocket(socket);
    }

}

