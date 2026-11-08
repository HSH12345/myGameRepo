#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>

#include <cstdint>
#include <functional>
#include <array>
#include <atomic>
#include <mutex>

#include "BinaryProtocol.h"
#include "Session.h"

class PacketRecvDispatcher
{
public:
    using PacketId = RpgProtocol::PacketType;
    using HandlerFunc =
        std::function<bool(const SessionPtr& session, const std::uint8_t* payload, std::size_t payloadSize)>;

    struct HandlerEntry
    {
        std::size_t maxPayloadSize = 0;
        SessionStateMask allowedStates = 0;
        HandlerFunc handler;
    };

    static PacketRecvDispatcher& GetInstance();
    PacketRecvDispatcher() = default;

    bool RegisterHandler(PacketId packetId, std::size_t maxPayloadSize, SessionStateMask allowedStates,
                         HandlerFunc handler);
    bool Seal();
    bool Dispatch(const SessionPtr& session, const char* buffer, DWORD bytesTransferred);

private:
    static constexpr std::size_t HANDLER_COUNT = static_cast<std::size_t>(RpgProtocol::PacketType::COUNT);
    const HandlerEntry* GetHandler(PacketId packetId) const;
    std::array<HandlerEntry, HANDLER_COUNT> handlers{};
    std::mutex registrationMutex;
    std::atomic<bool> sealed{false};
};

