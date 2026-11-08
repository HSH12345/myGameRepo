#pragma once

#include "../../ServerCore/Session.h"

#include <cstdint>

#include <chrono>

#include <memory>

#include <string>

#include <variant>

#include "../../Protocol/WorldPackets.h"

// Payloads are explicit value types so IOCP workers never hand game-object

// pointers to the simulation thread. Movement and combat use validated WorldMessage commands.

struct TestMessageCommand

{

    std::string message;
};

struct SessionConnectedCommand

{
};

struct WorldMessageCommand

{

    shared_world::net::Message message;
};

struct SessionDisconnectedCommand

{

    std::string reason;
};

using GameCommandPayload =

    std::variant<TestMessageCommand, SessionConnectedCommand, SessionDisconnectedCommand, WorldMessageCommand>;

struct GameCommand

{

    std::weak_ptr<Session> session;

    std::uint64_t sessionId = 0;

    std::uint64_t mapInstanceId = 1;

    std::uint64_t sequence = 0;

    std::chrono::steady_clock::time_point enqueuedAt = std::chrono::steady_clock::now();

    GameCommandPayload payload;
};

