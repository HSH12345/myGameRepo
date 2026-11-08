#include "InternalRpcClient.h"
#include "../Protocol/ServicePressure.h"
#include <ws2tcpip.h>

namespace
{
using Deadline = std::chrono::steady_clock::time_point;

struct WorkerConnection
{
    SOCKET socket = INVALID_SOCKET;
    unsigned short port = 0;

    ~WorkerConnection()
    {
        Reset();
    }

    void Reset()
    {
        if (this->socket != INVALID_SOCKET)
        {
            closesocket(this->socket);
        }

        this->socket = INVALID_SOCKET;
    }

};

bool Wait(SOCKET socket, bool write, Deadline deadline)
{
    const auto REMAINING =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (REMAINING <= 0)
    {
        return false;
    }

    timeval timeout{static_cast<long>(REMAINING / 1000000), static_cast<long>(REMAINING % 1000000)};
    fd_set ready, error;
    FD_ZERO(&ready);
    FD_SET(socket, &ready);
    FD_ZERO(&error);
    FD_SET(socket, &error);
    const int COUNT = select(0, write ? nullptr : &ready, write ? &ready : nullptr, &error, &timeout);
    return COUNT > 0 && !FD_ISSET(socket, &error) && FD_ISSET(socket, &ready);
}

bool Connect(WorkerConnection& connection, unsigned short port, Deadline deadline)
{
    if (connection.socket != INVALID_SOCKET && connection.port == port)
    {
        // No reply is outstanding here. Detect an idle-closed or desynchronized
        // connection before sending a mutation; this does not replay a request.
        char byte = 0;
        const int AVAILABLE = recv(connection.socket, &byte, 1, MSG_PEEK);
        if (AVAILABLE == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return true;
        }

    }

    connection.Reset();
    connection.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection.socket == INVALID_SOCKET)
    {
        return false;
    }

    u_long nonblocking = 1;
    if (ioctlsocket(connection.socket, FIONBIO, &nonblocking) != 0)
    {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int CONNECTED = connect(connection.socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (CONNECTED != 0 && (WSAGetLastError() != WSAEWOULDBLOCK || !Wait(connection.socket, true, deadline)))
    {
        return false;
    }

    int error = 0, length = sizeof(error);
    if (getsockopt(connection.socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 ||
        error != 0)
    {
        return false;
    }

    BOOL noDelay = TRUE;
    setsockopt(connection.socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    connection.port = port;
    return true;
}

bool Transfer(SOCKET socket, std::uint8_t* data, std::size_t size, bool write, Deadline deadline)
{
    while (size != 0)
    {
        if (!Wait(socket, write, deadline))
        {
            return false;
        }

        const int COUNT = write ? send(socket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0)
                                : recv(socket, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
        if (COUNT < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            continue;
        }

        if (COUNT <= 0)
        {
            return false;
        }

        data += COUNT;
        size -= static_cast<std::size_t>(COUNT);
    }

    return true;
}

}

bool InternalRpcClient::Configure(const std::string& host, unsigned short newPort)
{
    // Internal service currently has a loopback trust boundary; no DNS/blocking connect.
    if (host != "127.0.0.1" && host != "localhost")
    {
        return false;
    }

    this->port = newPort;
    return this->port != 0;
}

bool InternalRpcClient::IsDrained(Deadline deadline) const
{
    RpgProtocol::ByteBuffer reply;
    RpgProtocol::ServicePressureResponse pressure;
    return Exchange(RpgProtocol::PacketType::SERVICE_PRESSURE_REQUEST,
                    RpgProtocol::PacketType::SERVICE_PRESSURE_RESPONSE, {}, reply, deadline, 1) &&
           pressure.Deserialize(reply.data(), reply.size()) && pressure.outstanding == 0;
}

bool InternalRpcClient::Exchange(RpgProtocol::PacketType requestType, RpgProtocol::PacketType responseType,
                                 const RpgProtocol::ByteBuffer& request, RpgProtocol::ByteBuffer& response,
                                 Deadline deadline, int maxAttempts) const
{
    static thread_local WorkerConnection connection;
    RpgProtocol::ByteBuffer frame;
    if (!RpgProtocol::BuildFrame(requestType, request, frame))
    {
        return false;
    }

    for (int attempt = 0; attempt < maxAttempts && attempt < 2 && std::chrono::steady_clock::now() < deadline;
         ++attempt)
    {
        std::uint8_t bytes[RpgProtocol::HEADER_SIZE]{};
        RpgProtocol::FrameHeader header;
        bool ok = Connect(connection, this->port, deadline) &&
                  Transfer(connection.socket, frame.data(), frame.size(), true, deadline) &&
                  Transfer(connection.socket, bytes, sizeof(bytes), false, deadline) &&
                  RpgProtocol::DecodeHeader(bytes, sizeof(bytes), header) && header.packetType == responseType &&
                  header.payloadSize <= 8192;
        response.resize(ok ? header.payloadSize : 0);
        if (ok)
        {
            ok = Transfer(connection.socket, response.data(), response.size(), false, deadline);
        }

        if (ok)
        {
            return true;
        }

        connection.Reset();
    }

    return false;
}

