#include "GateServer.h"
#include <ws2tcpip.h>

using namespace RpgProtocol;

GateServer::GateServer(unsigned short port, unsigned short login, unsigned short game, bool dev)
    : loginPort(login), gamePort(game), development(dev)
{
    for (auto& worker : this->workers)
    {
        worker = std::make_unique<BoundedWorkerPool>(2048);
    }

    ServerOptions external;
    // Fail closed: TLS/certificate deployment is required before a public listener.
    external.bindAddress = "127.0.0.1";
    external.onSessionConnected = [this](const SessionPtr& session)
    {
        Connected(session);
    };

    external.onSessionDisconnected = [this](const SessionPtr& session, const std::string&)
    {
        std::lock_guard lock(this->mutex);
        auto it = this->clients.find(session->GetId());
        if (it != this->clients.end())
        {
            it->second->closeRequested = true;
        }

    };

    this->front = std::make_unique<Server>(
        port, 32, 4,
        [this](const SessionPtr& s, const char* p, DWORD n)
        {
            return this->frontDispatcher.Dispatch(s, p, n);
        },

        external);
    ServerOptions internal;
    internal.outboundOnly = true;
    internal.maxSessions = 32;
    internal.sessionLimits.maxPacketsPerSecond = 100000;
    internal.sessionLimits.maxReceivedBytesPerSecond = 64 * 1024 * 1024;
    this->peers = std::make_unique<Server>(
        static_cast<unsigned short>(0), 1, 2,
        [this](const SessionPtr& s, const char* p, DWORD n)
        {
            return this->peerDispatcher.Dispatch(s, p, n);
        },

        internal);
    const std::pair<PacketType, std::size_t> requests[] = {
        {PacketType::GUEST_LOGIN_REQUEST, 2189}, {PacketType::FIREBASE_LOGIN_REQUEST, 8 + MAX_FIREBASE_ID_TOKEN_SIZE},
        {PacketType::CHARACTER_LIST_REQUEST, 4}, {PacketType::CREATE_CHARACTER_REQUEST, 142},
        {PacketType::HEARTBEAT_REQUEST, 0},      {PacketType::WORLD_MESSAGE, 1024}};
    for (auto [type, limit] : requests)
    {
        this->frontDispatcher.RegisterHandler(type, limit, OPEN_SESSION_STATES,
                                              [this, type](const SessionPtr& s, const std::uint8_t* p, std::size_t n)
                                              {
                                                  return Forward(s, type, p, n);
                                              });

    }

    if (this->development)
    {
        this->frontDispatcher.RegisterHandler(PacketType::TEST_MESSAGE, 1028, OPEN_SESSION_STATES,
                                              [this](const SessionPtr& s, const std::uint8_t* p, std::size_t n)
                                              {
                                                  return Forward(s, PacketType::TEST_MESSAGE, p, n);
                                              });

    }

    this->frontDispatcher.Seal();
    this->peerDispatcher.RegisterHandler(PacketType::GATE_FRAME, MAX_GATE_FRAME + 9, OPEN_SESSION_STATES,
                                         [this](const SessionPtr& s, const std::uint8_t* p, std::size_t n)
                                         {
                                             return Reply(s, p, n);
                                         });

    this->peerDispatcher.Seal();
}

bool GateServer::Start()
{
    if (this->running.exchange(true))
    {
        return false;
    }

    for (auto& worker : this->workers)
    {
        if (!worker->Start(1))
        {
            Stop();
            return false;
        }

    }

    if (!this->peers->Start())
    {
        Stop();
        return false;
    }

    try
    {
        this->maintenance = std::thread(
            [this]
            {
                Maintain();
            });

    }

    catch (...)
    {
        Stop();
        return false;
    }

    if (!this->front->Start())
    {
        Stop();
        return false;
    }

    return true;
}

void GateServer::Stop()
{
    if (!this->running.exchange(false))
    {
        return;
    }

    this->front->Stop();
    this->wake.notify_all();
    if (this->maintenance.joinable())
    {
        this->maintenance.join();
    }

    for (auto& worker : this->workers)
    {
        worker->Stop(true);
    }

    std::vector<std::shared_ptr<Client>> remaining;
    {
        std::lock_guard lock(this->mutex);
        for (auto& [id, c] : this->clients)
        {
            remaining.push_back(c);
        }

    }

    for (auto& c : remaining)
    {
        Cleanup(c);
    }

    this->peers->Stop();
}

bool GateServer::Enqueue(std::uint64_t id, std::function<void()> work, std::size_t bytes)
{
    struct Reservation
    {
        Reservation(std::shared_ptr<SendQueueBudget> owner, std::size_t size) : budget(std::move(owner)), bytes(size)
        {
        }

        std::shared_ptr<SendQueueBudget> budget;
        std::size_t bytes;

        ~Reservation()
        {
            this->budget->Release(this->bytes);
        }

    };

    if (!this->workBudget->TryReserve(bytes))
    {
        return false;
    }

    std::shared_ptr<Reservation> reservation;
    try
    {
        reservation = std::make_shared<Reservation>(this->workBudget, bytes);
    }

    catch (...)
    {
        this->workBudget->Release(bytes);
        throw;
    }

    Job job;
    job.task = [reservation, work = std::move(work)]
    {
        work();
    };

    return this->workers[id % WORKER_COUNT]->Submit(std::move(job));
}

void GateServer::Connected(const SessionPtr& session)
{
    auto client = std::make_shared<Client>();
    client->external = session;
    {
        std::lock_guard lock(this->mutex);
        client->login = this->loginLanes[session->GetId() % LANE_COUNT];
        client->game = this->gameLanes[session->GetId() % LANE_COUNT];
        if (!client->login || !client->login->IsOpen())
        {
            throw std::runtime_error("Login tunnel not ready");
        }

        if (this->clients.size() >= 10000)
        {
            throw std::runtime_error("Gate user limit reached");
        }

        this->clients.emplace(session->GetId(), client);
    }

}

bool GateServer::Send(const SessionPtr& peer, GatePacket packet)
{
    ByteBuffer payload;
    return peer && peer->IsOpen() && packet.Serialize(payload) &&
           this->peers->SendToClient(peer, PacketType::GATE_FRAME, payload);
}

bool GateServer::Forward(const SessionPtr& session, PacketType type, const std::uint8_t* payload, std::size_t size)
{
    using namespace shared_world::net;
    Message worldRequest;
    if (type == PacketType::WORLD_MESSAGE)
    {
        try
        {
            worldRequest = Message::Decode(payload, size);
        }

        catch (const std::exception&)
        {
            return false;
        }

        if (worldRequest.kind != Kind::SELECT && worldRequest.kind != Kind::ENTER && worldRequest.kind != Kind::READY &&
            worldRequest.kind != Kind::MOVE && worldRequest.kind != Kind::PORTAL &&
            worldRequest.kind != Kind::NPC_TALK && worldRequest.kind != Kind::NPC_WARP &&
            worldRequest.kind != Kind::NPC_CANCEL && worldRequest.kind != Kind::EXIT_USE &&
            worldRequest.kind != Kind::ECONOMY_REQUEST && worldRequest.kind != Kind::SHOP_OPEN &&
            worldRequest.kind != Kind::SHOP_CLOSE && worldRequest.kind != Kind::COMBAT_REQUEST &&
            worldRequest.kind != Kind::COMBAT_CANCEL && worldRequest.kind != Kind::MATCH_ACTION)
        {
            return false;
        }

    }

    std::shared_ptr<Client> client;
    {
        std::lock_guard lock(this->mutex);
        auto it = this->clients.find(session->GetId());
        if (it == this->clients.end())
        {
            return false;
        }

        client = it->second;
    }

    ByteBuffer frame;
    if (!BuildFrame(type, ByteBuffer(payload, payload + size), frame))
    {
        return false;
    }

    if (!session->TryAcquirePendingCommand(64))
    {
        return false;
    }

    std::shared_ptr<PendingSessionWork> quota;
    try
    {
        quota = std::make_shared<PendingSessionWork>(session);
    }

    catch (...)
    {
        session->CompletePendingCommand();
        throw;
    }

    const auto FRAME_BYTES = frame.size();
    return Enqueue(
        session->GetId(),
        [this, client, type, quota, worldRequest, frame = std::move(frame)]
        {
            if (!client->external->IsOpen() || client->closeRequested)
            {
                Cleanup(client);
                return;
            }

            const bool WORLD = type == PacketType::WORLD_MESSAGE;
            const bool GAME = type == PacketType::TEST_MESSAGE || (WORLD && worldRequest.kind != Kind::SELECT);
            if ((GAME || WORLD) && client->external->GetState() != SessionState::AUTHENTICATED)
            {
                this->front->Disconnect(client->external, "Unauthenticated game request");
                return;
            }

            if (WORLD && worldRequest.kind == Kind::SELECT)
            {
                if (client->gameAuthorized)
                {
                    this->front->Disconnect(client->external, "Already entered game");
                    return;
                }

                client->selectionRequest = worldRequest.request;
                client->authorization = Message{};
            }

            if (WORLD && GAME)
            {
                if (worldRequest.kind == Kind::ENTER)
                {
                    if (client->gameAuthorized || client->authorization.kind != Kind::AUTHORIZE ||
                        worldRequest.ticket != client->authorization.ticket ||
                        std::chrono::steady_clock::now() > client->ticketDeadline)
                    {
                        this->front->Disconnect(client->external, "Invalid or expired game ticket");
                        return;
                    }

                }
                else if (!client->gameAuthorized)
                {
                    this->front->Disconnect(client->external, "Game entry required");
                    return;
                }

            }

            // A lobby session can outlive a GameServer restart. Bind its first
            // authorized entry to the current tunnel; never migrate live players.
            if (WORLD && worldRequest.kind == Kind::ENTER && !client->gameAuthorized &&
                (!client->game || !client->game->IsOpen()))
            {
                std::lock_guard lock(this->mutex);
                client->game = this->gameLanes[client->external->GetId() % LANE_COUNT];
                client->gameOpened = false;
            }

            auto peer = GAME ? client->game : client->login;
            auto& opened = GAME ? client->gameOpened : client->loginOpened;
            const auto ID = client->external->GetId();
            if (!opened)
            {
                if (!Send(peer, {GateOperation::OPEN, ID, {}}))
                {
                    this->front->Disconnect(client->external, "Backend unavailable");
                    return;
                }

                opened = true;
            }

            if (WORLD && worldRequest.kind == Kind::ENTER)
            {
                ByteBuffer proof;
                if (!BuildFrame(PacketType::WORLD_AUTHORIZE, client->authorization.Encode(), proof) ||
                    !Send(peer, {GateOperation::DATA, ID, proof}))
                {
                    this->front->Disconnect(client->external, "Game authorization unavailable");
                    return;
                }

                client->authorization = Message{};
                client->gameAuthorized = true;
            }

            if (!Send(peer, {GateOperation::DATA, ID, frame}))
            {
                this->front->Disconnect(client->external, "Backend send rejected");
            }

        },

        FRAME_BYTES);
}

bool GateServer::Reply(const SessionPtr& peer, const std::uint8_t* payload, std::size_t size)
{
    GatePacket packet;
    if (!packet.Deserialize(payload, size))
    {
        return false;
    }

    if (packet.operation == GateOperation::PONG)
    {
        return true;
    }

    if (packet.operation != GateOperation::DATA && packet.operation != GateOperation::CLOSE)
    {
        return false;
    }

    std::shared_ptr<Client> client;
    {
        std::lock_guard lock(this->mutex);
        auto it = this->clients.find(packet.clientId);
        if (it == this->clients.end())
        {
            return true;
        }

        client = it->second;
    }

    if (peer != client->login && peer != client->game)
    {
        return false;
    }

    const auto FRAME_BYTES = packet.frame.size();
    if (!Enqueue(
            packet.clientId,
            [this, client, peer, packet = std::move(packet)]
            {
                if (client->cleaned || !client->external->IsOpen())
                {
                    return;
                }

                if (packet.operation == GateOperation::CLOSE)
                {
                    this->front->Disconnect(client->external, "Backend rejected user");
                    return;
                }

                FrameHeader header;
                if (!DecodeHeader(packet.frame.data(), packet.frame.size(), header))
                {
                    return;
                }

                if (header.packetType == PacketType::WORLD_MESSAGE)
                {
                    using namespace shared_world::net;
                    ByteBuffer body(packet.frame.begin() + HEADER_SIZE, packet.frame.end());
                    try
                    {
                        auto message = Message::Decode(body.data(), body.size());
                        if (peer == client->login)
                        {
                            if (message.kind != Kind::SELECTED)
                            {
                                throw std::invalid_argument("invalid selection reply");
                            }

                            if (message.request != client->selectionRequest)
                            {
                                return;
                            }

                            if (!message.error)
                            {
                                auto auth = message;
                                auth.kind = Kind::AUTHORIZE;
                                auth.uid = client->external->GetAccountId();
                                client->authorization = std::move(auth);
                                client->ticketDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                            }

                        }
                        else if (!client->gameAuthorized ||
                                 (message.kind != Kind::PREPARE && message.kind != Kind::SPAWN &&
                                  message.kind != Kind::STATE && message.kind != Kind::DESPAWN &&
                                  message.kind != Kind::NPC_SPAWN && message.kind != Kind::NPC_DIALOGUE &&
                                  message.kind != Kind::NPC_RESULT && message.kind != Kind::EXIT_LAYOUT &&
                                  message.kind != Kind::EXIT_RESULT && message.kind != Kind::ENTITY_STATS &&
                                  message.kind != Kind::ERROR_VALUE && message.kind != Kind::ECONOMY_SNAPSHOT &&
                                  message.kind != Kind::SHOP_SNAPSHOT && message.kind != Kind::EQUIPMENT &&
                                  message.kind != Kind::COMBAT_EVENT && message.kind != Kind::DUNGEON_STATE &&
                                  message.kind != Kind::MATCH_STATE && message.kind != Kind::PARTY))
                        {
                            throw std::invalid_argument("invalid game reply");
                        }

                        this->front->SendToClient(client->external, PacketType::WORLD_MESSAGE, body);
                    }

                    catch (const std::exception&)
                    {
                        this->front->Disconnect(client->external, "Invalid world reply");
                    }

                    return;
                }

                if (peer != client->login)
                {
                    this->front->Disconnect(client->external, "Unexpected game reply");
                    return;
                }

                const auto TYPE = header.packetType;
                if (TYPE != PacketType::GUEST_LOGIN_RESPONSE && TYPE != PacketType::FIREBASE_LOGIN_RESPONSE &&
                    TYPE != PacketType::CHARACTER_LIST_RESPONSE && TYPE != PacketType::CREATE_CHARACTER_RESPONSE &&
                    TYPE != PacketType::HEARTBEAT_RESPONSE)
                {
                    this->front->Disconnect(client->external, "Unexpected backend reply");
                    return;
                }

                ByteBuffer body(packet.frame.begin() + HEADER_SIZE, packet.frame.end());
                if (TYPE == PacketType::GUEST_LOGIN_RESPONSE)
                {
                    GuestLoginResponsePacket login;
                    if (!login.Deserialize(body.data(), body.size()))
                    {
                        this->front->Disconnect(client->external, "Invalid auth reply");
                        return;
                    }

                    if (login.result == PacketSendResult::SUCCESS)
                    {
                        client->external->BeginAuthentication();
                        client->external->CompleteAuthentication(login.uid);
                    }

                }

                if (TYPE == PacketType::FIREBASE_LOGIN_RESPONSE)
                {
                    FirebaseLoginResponsePacket login;
                    if (!login.Deserialize(body.data(), body.size()))
                    {
                        this->front->Disconnect(client->external, "Invalid Firebase auth reply");
                        return;
                    }

                    if (login.result == PacketSendResult::SUCCESS)
                    {
                        client->external->BeginAuthentication();
                        client->external->CompleteAuthentication(login.uid);
                    }

                }

                this->front->SendToClient(client->external, TYPE, body);
            },

            FRAME_BYTES))
    {
        client->closeRequested = true;
    }

    return true; // A saturated user route must not close the shared tunnel.
}

void GateServer::Cleanup(const std::shared_ptr<Client>& client)
{
    if (client->cleaned)
    {
        return;
    }

    client->cleaned = true;
    const auto ID = client->external->GetId();
    if (client->loginOpened)
    {
        Send(client->login, {GateOperation::CLOSE, ID, {}});
    }

    if (client->gameOpened)
    {
        Send(client->game, {GateOperation::CLOSE, ID, {}});
    }

    this->front->Disconnect(client->external, "Gate route closed");
    std::lock_guard lock(this->mutex);
    this->clients.erase(ID);
}

SOCKET GateServer::Connect(unsigned short port)
{
    SOCKET socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
    {
        return socket;
    }

    u_long nonblocking = 1;
    ioctlsocket(socket, FIONBIO, &nonblocking);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int RESULT = connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    bool ok = RESULT == 0;
    if (!ok && WSAGetLastError() == WSAEWOULDBLOCK)
    {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(socket, &writable);
        timeval timeout{1, 0};
        if (select(0, nullptr, &writable, nullptr, &timeout) > 0)
        {
            int error = 0, length = sizeof(error);
            ok = getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0 && !error;
        }

    }

    if (!ok)
    {
        closesocket(socket);
        return INVALID_SOCKET;
    }

    return socket;
}

void GateServer::Maintain()
{
    auto nextConnect = std::chrono::steady_clock::now(), nextPing = nextConnect;
    while (this->running)
    {
        const auto NOW = std::chrono::steady_clock::now();
        if (NOW >= nextConnect)
        {
            for (std::size_t i = 0; i < LANE_COUNT * 2 && this->running; ++i)
            {
                const bool GAME = i >= LANE_COUNT;
                const auto INDEX = i % LANE_COUNT;
                SessionPtr previous;
                {
                    std::lock_guard lock(this->mutex);
                    previous = GAME ? this->gameLanes[INDEX] : this->loginLanes[INDEX];
                }

                if (previous && previous->IsOpen())
                {
                    continue;
                }

                SOCKET socket = Connect(GAME ? this->gamePort : this->loginPort);
                if (socket == INVALID_SOCKET)
                {
                    continue;
                }

                auto peer = this->peers->AdoptConnectedSocket(socket);
                {
                    std::lock_guard lock(this->mutex);
                    (GAME ? this->gameLanes[INDEX] : this->loginLanes[INDEX]) = peer;
                }

            }

            nextConnect = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }

        std::vector<std::shared_ptr<Client>> snapshot;
        std::vector<SessionPtr> links;
        {
            std::lock_guard lock(this->mutex);
            for (auto& [id, c] : this->clients)
            {
                snapshot.push_back(c);
            }

            for (auto& p : this->loginLanes)
            {
                if (p)
                {
                    links.push_back(p);
                }

            }

            for (auto& p : this->gameLanes)
            {
                if (p)
                {
                    links.push_back(p);
                }

            }

        }

        for (auto& c : snapshot)
        {
            if (c->closeRequested || !c->external->IsOpen() || !c->login->IsOpen())
            {
                Enqueue(c->external->GetId(),
                        [this, c]
                        {
                            Cleanup(c);
                        });

            }

        }

        if (NOW >= nextPing)
        {
            for (auto& p : links)
            {
                Send(p, {GateOperation::PING, 0, {}});
            }

            nextPing = NOW + std::chrono::seconds(5);
        }

        std::unique_lock lock(this->waitMutex);
        this->wake.wait_for(lock, std::chrono::milliseconds(100),
                            [this]
                            {
                                return !this->running;
                            });

    }

}

