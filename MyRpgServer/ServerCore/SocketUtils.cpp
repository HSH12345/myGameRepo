#include "SocketUtils.h"
#include "StructuredLogger.h"

#include <iostream>

SOCKET SocketUtils::CreateListenSocket(const char* ip, unsigned short port)
{
    SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (sock == INVALID_SOCKET)
    {
        RPG_LOG_ERROR("Network", "WSASocket failed. error=" << WSAGetLastError());
        return INVALID_SOCKET;
    }

    SOCKADDR_IN addr = {};
    addr.sin_family = AF_INET;
    if (!ip || InetPtonA(AF_INET, ip, &addr.sin_addr) != 1)
    {
        RPG_LOG_ERROR("Network", "Invalid IPv4 bind address.");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<SOCKADDR*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        RPG_LOG_ERROR("Network", "bind failed. error=" << WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
    {
        RPG_LOG_ERROR("Network", "listen failed. error=" << WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

SOCKET SocketUtils::CreateClientSocket()
{
    SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
    {
        RPG_LOG_ERROR("Network", "Accept socket creation failed. error=" << WSAGetLastError());
    }

    return socket;
}

void SocketUtils::CloseSocket(SOCKET socket)
{
    if (socket != INVALID_SOCKET)
    {
        closesocket(socket);
    }

}

bool SocketUtils::UpdateAcceptContext(SOCKET acceptSocket, SOCKET listenSocket)
{
    if (setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&listenSocket),
                   sizeof(listenSocket)) == SOCKET_ERROR)
    {
        RPG_LOG_ERROR("Network", "SO_UPDATE_ACCEPT_CONTEXT failed. error=" << WSAGetLastError());
        return false;
    }

    return true;
}

bool SocketUtils::ConfigureConnectedSocket(SOCKET socket)
{
    const BOOL ENABLED = TRUE;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ENABLED), sizeof(ENABLED)) ==
        SOCKET_ERROR)
    {
        RPG_LOG_ERROR("Network", "TCP_NODELAY failed. error=" << WSAGetLastError());
        return false;
    }

    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&ENABLED), sizeof(ENABLED)) ==
        SOCKET_ERROR)
    {
        RPG_LOG_ERROR("Network", "SO_KEEPALIVE failed. error=" << WSAGetLastError());
        return false;
    }

    return true;
}

std::string SocketUtils::GetRemoteAddress(SOCKET socket)
{
    SOCKADDR_STORAGE address = {};
    int addressLength = sizeof(address);
    if (getpeername(socket, reinterpret_cast<SOCKADDR*>(&address), &addressLength) == SOCKET_ERROR)
    {
        return "unknown";
    }

    char buffer[INET6_ADDRSTRLEN] = {};
    const void* source = nullptr;
    if (address.ss_family == AF_INET)
    {
        source = &reinterpret_cast<const SOCKADDR_IN*>(&address)->sin_addr;
    }
    else if (address.ss_family == AF_INET6)
    {
        source = &reinterpret_cast<const SOCKADDR_IN6*>(&address)->sin6_addr;
    }

    return source && inet_ntop(address.ss_family, source, buffer, sizeof(buffer)) ? buffer : "unknown";
}

bool SocketUtils::LoadAcceptEx(SOCKET listenSocket, LPFN_ACCEPTEX& acceptExPtr)
{
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;

    int result = WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx),
                          &acceptExPtr, sizeof(acceptExPtr), &bytes, nullptr, nullptr);

    if (result == SOCKET_ERROR)
    {
        RPG_LOG_ERROR("Network", "WSAIoctl for AcceptEx failed. error=" << WSAGetLastError());
        return false;
    }

    return true;
}

