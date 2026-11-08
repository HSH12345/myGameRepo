#include "Managers/PacketManager.h"
#include "GameMain.h"
#include "Managers/NetworkManager.h"
#include "Kismet/GameplayStatics.h"

void UPacketManager::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);

    this->receiveDispatcher = NewObject<UReceiveDispatcher>(this);
    this->receiveDispatcher->Init();
    this->sendProcessor = NewObject<USendProcessor>(this);
}

void UPacketManager::Deinitialize()
{
    this->receiveDispatcher = nullptr;
    this->sendProcessor = nullptr;
    Super::Deinitialize();
}

