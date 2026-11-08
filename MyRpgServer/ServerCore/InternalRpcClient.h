#pragma once
#include "Session.h"
#include "BinaryProtocol.h"
#include <chrono>

class InternalRpcClient final
{
public:
    bool Configure(const std::string& host, unsigned short newPort);
    bool IsDrained(std::chrono::steady_clock::time_point deadline) const;
    bool Exchange(RpgProtocol::PacketType requestType, RpgProtocol::PacketType responseType,
                  const RpgProtocol::ByteBuffer& request, RpgProtocol::ByteBuffer& response,
                  std::chrono::steady_clock::time_point deadline, int maxAttempts = 2) const;

private:
    unsigned short port = 9300;
};

