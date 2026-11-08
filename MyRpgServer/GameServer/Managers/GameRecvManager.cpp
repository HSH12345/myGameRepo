#include "GameRecvManager.h"

#include "../Packet/GamePackets.h"
#include "../../ServerCore/PacketRecvDispatcher.h"
#include "../../ServerCore/StructuredLogger.h"

GameRecvManager::GameRecvManager(MapScheduler& newRuntime, GameSendManager& newSendManager,
                                 std::uint32_t newMaxPendingCommandsPerSession, bool enableDevelopmentPackets)
    : runtime(newRuntime), sendManager(newSendManager), maxPendingCommandsPerSession(newMaxPendingCommandsPerSession)
{
    auto& dispatcher = PacketRecvDispatcher::GetInstance();
    bool worldValid = true;
    for (auto type : {RpgProtocol::PacketType::WORLD_MESSAGE, RpgProtocol::PacketType::WORLD_AUTHORIZE})
    {
        worldValid =
            worldValid &&
            dispatcher.RegisterHandler(
                type, 1024, SessionStateBit(SessionState::CONNECTED),
                [this, type](const SessionPtr& session, const std::uint8_t* data, std::size_t size)
                {
                    using namespace shared_world::net;
                    try
                    {
                        auto message = Message::Decode(data, size);
                        if (type == RpgProtocol::PacketType::WORLD_AUTHORIZE
                                ? (message.kind != Kind::AUTHORIZE || session->GetRemoteAddress() != "gate-user")
                                : (message.kind != Kind::ENTER && message.kind != Kind::READY &&
                                   message.kind != Kind::MOVE && message.kind != Kind::PORTAL &&
                                   message.kind != Kind::NPC_TALK && message.kind != Kind::NPC_WARP &&
                                   message.kind != Kind::NPC_CANCEL && message.kind != Kind::EXIT_USE &&
                                   message.kind != Kind::ECONOMY_REQUEST && message.kind != Kind::SHOP_OPEN &&
                                   message.kind != Kind::SHOP_CLOSE && message.kind != Kind::COMBAT_REQUEST &&
                                   message.kind != Kind::COMBAT_CANCEL && message.kind != Kind::MATCH_ACTION))
                        {
                            return false;
                        }

                        if (!session->TryAcquirePendingCommand(this->maxPendingCommandsPerSession))
                        {
                            return false;
                        }

                        GameCommand command;
                        command.session = session;
                        command.sessionId = session->GetId();
                        command.payload = WorldMessageCommand{std::move(message)};
                        try
                        {
                            if (!this->runtime.TryEnqueueGameplay(std::move(command)))
                            {
                                session->CompletePendingCommand();
                                return false;
                            }

                        }

                        catch (...)
                        {
                            session->CompletePendingCommand();
                            throw;
                        }

                        return true;
                    }

                    catch (const std::exception&)
                    {
                        return false;
                    }

                });

    }

    this->valid =
        worldValid && dispatcher.RegisterHandler(
                          RpgProtocol::PacketType::HEARTBEAT_REQUEST, 0, OPEN_SESSION_STATES,
                          [this](const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
                          {
                              return HandleHeartbeat(session, payload, payloadSize);
                          });

    if (this->valid && enableDevelopmentPackets)
    {
        this->valid = dispatcher.RegisterHandler(
            RpgProtocol::PacketType::TEST_MESSAGE, 1024 + sizeof(std::uint32_t),
            SessionStateBit(SessionState::CONNECTED),
            [this](const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
            {
                return HandleTestMessage(session, payload, payloadSize);
            });

    }

}

bool GameRecvManager::HandleTestMessage(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)
{
    RpgProtocol::GameTestMessagePacket packet;
    if (!session || !packet.Deserialize(payload, payloadSize))
    {
        return false;
    }

    if (!session->TryAcquirePendingCommand(this->maxPendingCommandsPerSession))
    {
        RPG_LOG_WARNING("Game", "Per-session game command limit exceeded. sessionId=" << session->GetId());
        return false;
    }

    GameCommand command;
    command.session = session;
    command.sessionId = session->GetId();
    command.payload = TestMessageCommand{std::move(packet.message)};
    if (!this->runtime.TryEnqueueGameplay(std::move(command)))
    {
        session->CompletePendingCommand();
        RPG_LOG_WARNING("Game",
                        "Game command rejected: queue capacity or dependency draining. sessionId=" << session->GetId());
        return false;
    }

    return true;
}

bool GameRecvManager::HandleHeartbeat(const SessionPtr& session, const std::uint8_t*, std::size_t payloadSize)
{
    // Heartbeat does not read or mutate world state, so it remains an inline
    // transport-level operation instead of consuming game-tick capacity.
    return payloadSize == 0 && this->sendManager.SendHeartbeat(session);
}

