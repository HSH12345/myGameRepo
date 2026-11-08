#pragma once
#include "InternalRpcClient.h"
#include "../Protocol/WorldDataPackets.h"

class WorldDataClient final
{
public:
    bool Configure(unsigned short port)
    {
        return this->client.Configure("127.0.0.1", port);
    }

    RpgProtocol::WorldDataResult Execute(const RpgProtocol::WorldDataRequest& request) const
    {
        RpgProtocol::ByteBuffer payload, reply;
        RpgProtocol::WorldDataResponse response;
        if (request.Serialize(payload) &&
            this->client.Exchange(RpgProtocol::PacketType::INTERNAL_WORLD_DATA_REQUEST,
                                  RpgProtocol::PacketType::INTERNAL_WORLD_DATA_RESPONSE, payload, reply,
                                  std::chrono::steady_clock::now() + std::chrono::seconds(2), 1) &&
            response.Deserialize(reply.data(), reply.size()) && response.requestId == request.requestId &&
            response.operation == request.operation)
        {
            return response.result;
        }

        return RpgProtocol::WorldDataResult::UNAVAILABLE;
    }

    bool Available(unsigned short gamePort, std::uint64_t instance) const
    {
        RpgProtocol::WorldDataRequest request;
        request.gamePort = gamePort;
        request.instance = instance;
        return Execute(request) == RpgProtocol::WorldDataResult::SUCCESS;
    }

private:
    InternalRpcClient client;
};

