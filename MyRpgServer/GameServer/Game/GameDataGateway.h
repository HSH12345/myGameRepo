#pragma once
#include "../../ServerCore/DependencyExecutor.h"
#include "../../ServerCore/InternalRpcClient.h"
#include "../../Protocol/AccountDataPackets.h"
#include "../../Protocol/CreateCharacterPackets.h"
#include "../../Protocol/EconomyPackets.h"

// No game-thread blocking. Domain callers must serialize an explicit operation,
// validate the reply, and apply it via a pre-reserved owner completion slot.
// Domain packets are whitelisted; SQL belongs to the data server.
class GameDataGateway final
{
public:
    enum class Target
    {
        ACCOUNT,
        CHARACTER
    };

    using Reply = std::function<bool(bool, const RpgProtocol::ByteBuffer&)>;

    explicit GameDataGateway(DependencyAdmission::Options options = {}) : executor(options)
    {
    }

    bool Start(unsigned short accountPort, unsigned short characterPort, bool economy = false)
    {
        this->requireEconomy = economy;
        return this->account.Configure("127.0.0.1", accountPort) &&
               this->character.Configure("127.0.0.1", characterPort) &&
               this->executor.Start(1, 2,
                                    [this]
                                    {
                                        return Probe();
                                    });

    }

    void Stop()
    {
        this->executor.Stop();
    }

    DependencyAdmission& Admission()
    {
        return this->executor.Admission();
    }

    bool Submit(Target target, RpgProtocol::PacketType requestType, RpgProtocol::PacketType responseType,
                RpgProtocol::ByteBuffer request, bool important, Reply completion)
    {
        if (!completion)
        {
            return false;
        }

        using namespace RpgProtocol;
        const bool VALID_PAIR =
            (target == Target::ACCOUNT && requestType == PacketType::INTERNAL_ACCOUNT_REQUEST &&
             responseType == PacketType::INTERNAL_ACCOUNT_RESPONSE) ||
            (target == Target::CHARACTER && requestType == PacketType::INTERNAL_CHARACTER_LIST_REQUEST &&
             responseType == PacketType::INTERNAL_CHARACTER_LIST_RESPONSE) ||
            (target == Target::CHARACTER && requestType == PacketType::INTERNAL_CREATE_CHARACTER_REQUEST &&
             responseType == PacketType::INTERNAL_CREATE_CHARACTER_RESPONSE) ||
            (target == Target::CHARACTER && requestType == PacketType::INTERNAL_ECONOMY_REQUEST &&
             responseType == PacketType::INTERNAL_ECONOMY_RESPONSE);
        if (!VALID_PAIR || request.size() > 2322)
        {
            return false;
        }

        // A caller cannot accidentally route a mutation through the read lane.
        if (requestType == PacketType::INTERNAL_ECONOMY_REQUEST)
        {
            shared_world::economy::RpcRequest decoded;
            if (!decoded.Deserialize(request.data(), request.size()))
            {
                return false;
            }

            important = important || decoded.request.Mutation() ||
                        decoded.request.action == shared_world::economy::Action::ACQUIRE;
        }
        else if (requestType == PacketType::INTERNAL_ACCOUNT_REQUEST)
        {
            AccountDataRequest decoded;
            if (!decoded.Deserialize(request.data(), request.size()))
            {
                return false;
            }

            important = important || decoded.operation != AccountOperation::LOOKUP_TOKEN;
        }
        else if (requestType == PacketType::INTERNAL_CREATE_CHARACTER_REQUEST)
        {
            InternalCreateCharacterRequestPacket decoded;
            if (!decoded.Deserialize(request.data(), request.size()))
            {
                return false;
            }

            important = true;
        }
        else
        {
            InternalCharacterListRequestPacket decoded;
            if (!decoded.Deserialize(request.data(), request.size()))
            {
                return false;
            }

        }

        const auto BYTES = request.size() + RpgProtocol::HEADER_SIZE;
        return this->executor.Submit(
            BYTES, important,
            [this, target, requestType, responseType, request = std::move(request), completion = std::move(completion)]
            {
                RpgProtocol::ByteBuffer response;
                bool received = false;
                bool healthy = false;
                try
                {
                    received = (target == Target::ACCOUNT ? this->account : this->character)
                                   .Exchange(requestType, responseType, request, response,
                                             std::chrono::steady_clock::now() + std::chrono::seconds(10), 1);
                    healthy = received && Validate(requestType, request, response);
                }

                catch (...)
                {
                }

                // No generic replay of writes. An ambiguous result requires operation-ID reconciliation.
                const bool DELIVERED = completion(received, response);
                return healthy && DELIVERED;
            });

    }

private:
    static bool Validate(RpgProtocol::PacketType type, const RpgProtocol::ByteBuffer& input,
                         const RpgProtocol::ByteBuffer& output)
    {
        using namespace RpgProtocol;
        if (type == PacketType::INTERNAL_ECONOMY_REQUEST)
        {
            shared_world::economy::RpcRequest request;
            shared_world::economy::RpcResponse response;
            return request.Deserialize(input.data(), input.size()) &&
                   response.Deserialize(output.data(), output.size()) && request.requestId == response.requestId &&
                   request.request.operation == response.operation &&
                   response.result != shared_world::economy::Result::UNAVAILABLE &&
                   (response.result != shared_world::economy::Result::UNKNOWN_OUTCOME ||
                    request.request.action == shared_world::economy::Action::QUERY);
        }

        if (type == PacketType::INTERNAL_ACCOUNT_REQUEST)
        {
            AccountDataRequest request;
            AccountDataResponse response;
            return request.Deserialize(input.data(), input.size()) &&
                   response.Deserialize(output.data(), output.size()) && request.requestId == response.requestId &&
                   request.operation == response.operation && response.result != AccountResult::UNAVAILABLE &&
                   response.result != AccountResult::QUERY_ERROR;
        }

        if (type == PacketType::INTERNAL_CHARACTER_LIST_REQUEST)
        {
            InternalCharacterListRequestPacket request;
            CharacterListResponsePacket response;
            return request.Deserialize(input.data(), input.size()) &&
                   response.Deserialize(output.data(), output.size()) && request.requestId == response.requestId &&
                   response.result == CharacterListResult::SUCCESS;
        }

        InternalCreateCharacterRequestPacket request;
        CreateCharacterResponsePacket response;
        return request.Deserialize(input.data(), input.size()) && response.Deserialize(output.data(), output.size()) &&
               request.requestId == response.requestId && request.operationId == response.operationId &&
               response.result != CreateCharacterResult::UNKNOWN_OUTCOME &&
               response.result != CreateCharacterResult::UNAVAILABLE && response.result != CreateCharacterResult::BUSY;
    }

    bool Probe()
    {
        using namespace RpgProtocol;
        ByteBuffer bytes, reply;
        const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        if (!this->account.IsDrained(DEADLINE) || !this->character.IsDrained(DEADLINE))
        {
            return false;
        }

        AccountDataRequest lookup{1, AccountOperation::LOOKUP_TOKEN, {}, "__service_health_probe__", {}, false};
        AccountDataResponse accountReply;
        if (!lookup.Serialize(bytes) ||
            !this->account.Exchange(PacketType::INTERNAL_ACCOUNT_REQUEST, PacketType::INTERNAL_ACCOUNT_RESPONSE, bytes,
                                    reply, DEADLINE, 1) ||
            !accountReply.Deserialize(reply.data(), reply.size()) || accountReply.requestId != 1 ||
            accountReply.operation != AccountOperation::LOOKUP_TOKEN ||
            (accountReply.result != AccountResult::NOT_FOUND && accountReply.result != AccountResult::SUCCESS))
        {
            return false;
        }

        InternalCharacterListRequestPacket list{1, "__service_health_probe__"};
        CharacterListResponsePacket characterReply;
        const bool READY =
            list.Serialize(bytes) &&
            this->character.Exchange(PacketType::INTERNAL_CHARACTER_LIST_REQUEST,
                                     PacketType::INTERNAL_CHARACTER_LIST_RESPONSE, bytes, reply, DEADLINE, 1) &&
            characterReply.Deserialize(reply.data(), reply.size()) && characterReply.requestId == 1 &&
            characterReply.result == CharacterListResult::SUCCESS;
        if (!READY || !this->requireEconomy)
        {
            return READY;
        }

        shared_world::economy::RpcRequest q;
        q.requestId = 1;
        q.uid = "__service_health_probe__";
        q.character = UINT64_MAX;
        q.request.action = shared_world::economy::Action::HEALTH;
        shared_world::economy::RpcResponse response;
        return q.Serialize(bytes) &&
               this->character.Exchange(PacketType::INTERNAL_ECONOMY_REQUEST, PacketType::INTERNAL_ECONOMY_RESPONSE,
                                        bytes, reply, DEADLINE, 1) &&
               response.Deserialize(reply.data(), reply.size()) && response.requestId == 1 &&
               response.result == shared_world::economy::Result::SUCCESS;
    }

    InternalRpcClient account, character;
    DependencyExecutor executor;
    bool requireEconomy = false;
};

