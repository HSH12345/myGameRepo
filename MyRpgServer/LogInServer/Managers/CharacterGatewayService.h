#pragma once
#include "../../ServerCore/DependencyExecutor.h"
#include "../../Protocol/CreateCharacterPackets.h"
#include "../../ServerCore/InternalRpcClient.h"
#include "../../Protocol/WorldPackets.h"
#include "../../ServerCore/WorldDataClient.h"

class CharacterGatewayService final
{
public:
    ~CharacterGatewayService()
    {
        Stop();
    }

    static CharacterGatewayService& GetInstance();
    bool Start(unsigned int count, const std::string& host, unsigned short port);

    void Stop()
    {
        this->workers.Stop();
    }

    DependencyAdmission::Snapshot Pressure()
    {
        return this->workers.Admission().Inspect();
    }

    bool Request(const SessionPtr& session, const RpgProtocol::CharacterListRequestPacket& packet);
    bool Create(const SessionPtr& session, const RpgProtocol::CreateCharacterRequestPacket& packet);
    bool Select(const SessionPtr& session, const shared_world::net::Message& packet);

private:
    bool Probe();
    DependencyExecutor workers;
    InternalRpcClient client;
    WorldDataClient worldClient;
    bool worldDataEnabled = false;
    unsigned short gamePort = 9200;
    std::uint64_t worldInstance = 1;
};

