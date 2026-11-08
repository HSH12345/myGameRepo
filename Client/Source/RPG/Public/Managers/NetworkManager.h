#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Managers/NetworkManagerModules/Receiver.h"
#include "Managers/NetworkManagerModules/Sender.h"
#include "Templates/Function.h"
#include "NetworkManager.generated.h"

UENUM()
enum class ENetworkConnectionState : uint8
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    CLOSING
};

DECLARE_MULTICAST_DELEGATE(FNetworkConnectionClosed);

UCLASS()

class RPG_API UNetworkManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

private:
    UPROPERTY()
    TObjectPtr<USender> sender = nullptr;
    UPROPERTY()
    TObjectPtr<UReceiver> receiver = nullptr;

private:
    FTimerHandle connectTimerHandle;
    class FSocket* socket = nullptr;
    FString serverAddress = TEXT("127.0.0.1");
    uint16 port = 9000;
    FTimerHandle packetProcessTimerHandle;
    FTimerHandle heartbeatTimerHandle;
    FTimerHandle reconnectTimerHandle;
    bool isReceiverConnected = false;
    double connectStartSeconds = 0.0;
    double lastHeartbeatResponseSeconds = 0.0;
    ENetworkConnectionState connectionState = ENetworkConnectionState::DISCONNECTED;
    TFunction<void()> connectedCallback;
    int32 reconnectAttempt = 0;
    bool reconnectEnabled = false;
    uint64 connectionGeneration = 0;

public:
    // Game-thread notification before any old connection can publish more state.
    FNetworkConnectionClosed onConnectionClosed;
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;

    void ConnectLoginSever(TFunction<void()> onConnectedUiCallback);
    void ConnectLoginServer(TFunction<void()> onConnectedUiCallback);
    void CheckConnectStatus();
    void DisconnectLoginServer();

    USender* GetSender() const
    {
        return this->sender;
    }

    ENetworkConnectionState GetConnectionState() const
    {
        return this->connectionState;
    }

    uint64 GetConnectionGeneration() const
    {
        return this->connectionGeneration;
    }

    void MarkServerActivity();
    void MarkHeartbeatResponse();
#if !UE_BUILD_SHIPPING
    void SimulateConnectionLossForTest()
    {
        HandleConnectionLost(TEXT("World loading recovery test"));
    }

#endif

private:
    void StartConnectionAttempt();
    void HandleConnectionLost(const FString& reason);
    void CloseConnection();
    void ProcessPendingPackets();
    void SendHeartbeat();
};

