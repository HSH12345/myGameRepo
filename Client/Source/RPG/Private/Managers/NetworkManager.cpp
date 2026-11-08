#include "Managers/NetworkManager.h"
#include "Managers/CharacterManager.h"
#include "UObject/NoExportTypes.h"
#include "Networking.h"
#include <GameMain.h>
#include "Managers/PacketManager.h"
#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "Serialization/ArrayWriter.h"
#include "TimerManager.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
const TCHAR* DescribeSenderExit(const EPacketSenderExitCode code)
{
    switch (code)
    {
    case EPacketSenderExitCode::SOCKET_ERROR_VALUE:
        return TEXT("socket error");
    case EPacketSenderExitCode::SEND_TIMEOUT:
        return TEXT("send timeout");
    default:
        return TEXT("stopped");
    }

}

const TCHAR* DescribeReceiverExit(const EPacketReceiverExitCode code)
{
    switch (code)
    {
    case EPacketReceiverExitCode::SOCKET_ERROR_VALUE:
        return TEXT("socket error");
    case EPacketReceiverExitCode::PROTOCOL_ERROR:
        return TEXT("protocol error");
    case EPacketReceiverExitCode::QUEUE_OVERFLOW:
        return TEXT("receive queue overflow");
    case EPacketReceiverExitCode::PEER_CLOSED:
        return TEXT("server closed connection");
    default:
        return TEXT("stopped");
    }

}

}

void UNetworkManager::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);
    GConfig->GetString(TEXT("RPG.Network"), TEXT("LoginServerAddress"), this->serverAddress, GGameIni);
    int32 configuredPort = this->port;
    if (GConfig->GetInt(TEXT("RPG.Network"), TEXT("LoginServerPort"), configuredPort, GGameIni) && configuredPort > 0 &&
        configuredPort <= 65535)
    {
        this->port = static_cast<uint16>(configuredPort);
    }

}

void UNetworkManager::Deinitialize()
{
    DisconnectLoginServer();
    Super::Deinitialize();
}

void UNetworkManager::ConnectLoginSever(TFunction<void()> onConnectedUiCallback)
{
    ConnectLoginServer(MoveTemp(onConnectedUiCallback));
}

void UNetworkManager::ConnectLoginServer(TFunction<void()> onConnectedUiCallback)
{
    // A manual retry supersedes a previously scheduled automatic attempt.
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->reconnectTimerHandle);
    }

    this->reconnectEnabled = true;
    this->reconnectAttempt = 0;
    this->connectedCallback = MoveTemp(onConnectedUiCallback);
    StartConnectionAttempt();
}

void UNetworkManager::StartConnectionAttempt()
{
    CloseConnection();
    this->connectionState = ENetworkConnectionState::CONNECTING;
    this->socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(TEXT("Stream"), TEXT("Client Socket"));
    if (!this->socket)
    {
        HandleConnectionLost(TEXT("Failed to create client socket"));
        return;
    }

    FIPv4Address ip;
    if (!FIPv4Address::Parse(this->serverAddress, ip))
    {
        HandleConnectionLost(TEXT("Invalid login server IP address"));
        return;
    }

    TSharedRef<FInternetAddr> addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    addr->SetIp(ip.Value);
    addr->SetPort(this->port);

    UGameMain::GetInstance()->Log(TEXT("Try Connect Gate Server"), EMyLogType::LOG);
    this->socket->SetNonBlocking(true);
    this->socket->SetNoDelay(true);
    if (!this->socket->Connect(*addr) && this->socket->GetConnectionState() == SCS_ConnectionError)
    {
        HandleConnectionLost(TEXT("Login server connection failed immediately"));
        return;
    }

    this->connectStartSeconds = FPlatformTime::Seconds();

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(this->connectTimerHandle, this, &UNetworkManager::CheckConnectStatus,
                                               0.1f, true);
    }
    else
    {
        HandleConnectionLost(TEXT("World context is unavailable"));
    }

}

void UNetworkManager::CheckConnectStatus()
{
    if (!this->socket)
    {
        return;
    }

    ESocketConnectionState state = this->socket->GetConnectionState();

    const bool B_WRITABLE = this->socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::Zero());
    if (state == SCS_Connected && B_WRITABLE)
    {
        // 연결 성공 시 타이머 종료 및 후속 처리
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(this->connectTimerHandle);
        }

        UGameMain::GetInstance()->Log(TEXT("Gate Server Connect Success"), EMyLogType::LOG);

        this->sender = NewObject<USender>(this);
        this->receiver = NewObject<UReceiver>(this);
        if (!this->sender->Init(this->socket) || !this->receiver->Init(this->socket))
        {
            HandleConnectionLost(TEXT("Failed to create network worker threads"));
            return;
        }

        this->isReceiverConnected = true;
        this->connectionState = ENetworkConnectionState::CONNECTED;
        this->lastHeartbeatResponseSeconds = FPlatformTime::Seconds();
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().SetTimer(this->packetProcessTimerHandle, this,
                                                   &UNetworkManager::ProcessPendingPackets, 0.01f, true);
            GetWorld()->GetTimerManager().SetTimer(this->heartbeatTimerHandle, this, &UNetworkManager::SendHeartbeat,
                                                   15.0f, true);
        }

        if (this->connectedCallback)
        {
            this->connectedCallback();
        }

    }
    else if (state == SCS_ConnectionError)
    {
        // 연결 오류 발생 시 타이머 종료 및 에러 처리
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(this->connectTimerHandle);
        }

        HandleConnectionLost(TEXT("Login server connection failed"));
    }
    else if (FPlatformTime::Seconds() - this->connectStartSeconds > 10.0)
    {
        HandleConnectionLost(TEXT("Login server connection timed out"));
    }

}

void UNetworkManager::ProcessPendingPackets()
{
    if (!this->isReceiverConnected || !this->receiver)
    {
        return;
    }

    const double DEADLINE = FPlatformTime::Seconds() + 0.002;
    for (int32 count = 0; count < 64 && FPlatformTime::Seconds() < DEADLINE; ++count)
    {
        TArray<uint8> rawPacket;
        if (!this->receiver->DequeueRawPacket(rawPacket))
        {
            break;
        }

        GetGameInstance()->GetSubsystem<UPacketManager>()->receiveDispatcher->HandlePacket(rawPacket);
        // A handler may deliberately disconnect and release the receive queue.
        if (!this->isReceiverConnected || !this->receiver)
        {
            return;
        }

    }

    if (!this->socket)
    {
        HandleConnectionLost(TEXT("Login server socket is unavailable"));
    }
    else if (this->sender && this->sender->HasStopped())
    {
        HandleConnectionLost(FString::Printf(TEXT("Login sender stopped (%s, exitCode=%u)"),
                                             DescribeSenderExit(this->sender->GetExitCode()),
                                             static_cast<uint32>(this->sender->GetExitCode())));
    }
    else if (this->receiver->HasStopped() && this->receiver->IsQueueEmpty())
    {
        HandleConnectionLost(FString::Printf(TEXT("Login receiver stopped (%s, exitCode=%u)"),
                                             DescribeReceiverExit(this->receiver->GetExitCode()),
                                             static_cast<uint32>(this->receiver->GetExitCode())));
    }
    else if (this->socket->GetConnectionState() == SCS_ConnectionError)
    {
        HandleConnectionLost(TEXT("Login server socket entered an error state"));
    }

}

void UNetworkManager::SendHeartbeat()
{
    if (FPlatformTime::Seconds() - this->lastHeartbeatResponseSeconds > 45.0)
    {
        HandleConnectionLost(TEXT("Login server heartbeat timed out"));
        return;
    }

    if (this->sender && !this->sender->SendPacket(RpgProtocol::EPacketType::HEARTBEAT_REQUEST, {}))
    {
        HandleConnectionLost(TEXT("Failed to queue heartbeat"));
    }

}

void UNetworkManager::MarkHeartbeatResponse()
{
    MarkServerActivity();
}

void UNetworkManager::MarkServerActivity()
{
    this->lastHeartbeatResponseSeconds = FPlatformTime::Seconds();
    this->reconnectAttempt = 0;
}

void UNetworkManager::DisconnectLoginServer()
{
    this->reconnectEnabled = false;
    this->connectedCallback = nullptr;
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->reconnectTimerHandle);
    }

    CloseConnection();
}

void UNetworkManager::HandleConnectionLost(const FString& reason)
{
    UGameMain::GetInstance()->Log(reason, EMyLogType::ERROR_VALUE);
    if (UGameMain* game = Cast<UGameMain>(GetGameInstance()))
    {
        if (UPacketManager* packetManager = game->GetSubsystem<UPacketManager>();
            packetManager && packetManager->sendProcessor)
        {
            packetManager->sendProcessor->CancelLoginRequest();
        }

    }

    CloseConnection();
    if (!this->reconnectEnabled || this->reconnectAttempt >= 5 || !GetWorld())
    {
        return;
    }

    const float DELAY_SECONDS = FMath::Min(8.0f, 0.5f * FMath::Pow(2.0f, static_cast<float>(this->reconnectAttempt++)));
    GetWorld()->GetTimerManager().SetTimer(this->reconnectTimerHandle,
                                           FTimerDelegate::CreateWeakLambda(this,
                                                                            [this]()
                                                                            {
                                                                                StartConnectionAttempt();
                                                                            }),

                                           DELAY_SECONDS, false);
}

void UNetworkManager::CloseConnection()
{
    if (this->connectionState == ENetworkConnectionState::CLOSING)
    {
        return;
    }

    ++this->connectionGeneration;
    this->connectionState = ENetworkConnectionState::CLOSING;
    if (UGameInstance* game = GetGameInstance())
    {
        if (UPacketManager* packets = game->GetSubsystem<UPacketManager>(); packets && packets->sendProcessor)
        {
            packets->sendProcessor->CancelLoginRequest();
        }

    }

    if (UGameInstance* game = GetGameInstance())
    {
        if (UCharacterManager* characters = game->GetSubsystem<UCharacterManager>())
        {
            characters->OnDisconnected();
        }

    }

    this->onConnectionClosed.Broadcast();
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->connectTimerHandle);
        world->GetTimerManager().ClearTimer(this->packetProcessTimerHandle);
        world->GetTimerManager().ClearTimer(this->heartbeatTimerHandle);
    }

    this->isReceiverConnected = false;
    if (this->socket)
    {
        this->socket->Shutdown(ESocketShutdownMode::ReadWrite);
    }

    if (this->receiver)
    {
        this->receiver->Shutdown();
    }

    if (this->sender)
    {
        this->sender->Shutdown();
    }

    this->receiver = nullptr;
    this->sender = nullptr;
    if (this->socket)
    {
        this->socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(this->socket);
        this->socket = nullptr;
    }

    this->connectionState = ENetworkConnectionState::DISCONNECTED;
}

