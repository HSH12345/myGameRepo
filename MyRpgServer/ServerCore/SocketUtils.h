#pragma once

#ifndef WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN

#endif

#ifndef NOMINMAX

#define NOMINMAX

#endif

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>

#include <mswsock.h>

#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <string>

class SocketUtils

{

public:
    static SOCKET CreateListenSocket(const char* ip, unsigned short port);

    static SOCKET CreateClientSocket();

    static void CloseSocket(SOCKET socket);

    static bool UpdateAcceptContext(SOCKET acceptSocket, SOCKET listenSocket);

    static bool ConfigureConnectedSocket(SOCKET socket);

    static std::string GetRemoteAddress(SOCKET socket);

    static bool LoadAcceptEx(SOCKET listenSocket, LPFN_ACCEPTEX& acceptExPtr);
};

