#include "Managers/CharacterManager.h"
#include "Managers/PacketManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "TimerManager.h"

void UCharacterManager::Deinitialize()
{
    OnDisconnected();
    Super::Deinitialize();
}

uint32 UCharacterManager::AllocateRequestId()
{
    const uint32 VALUE = this->nextRequestId++;
    if (this->nextRequestId == 0)
    {
        this->nextRequestId = 1;
    }

    return VALUE;
}

FString UCharacterManager::SaveSlot() const
{
    return TEXT("PendingCharacterCreation_") + FMD5::HashAnsiString(*this->accountUid);
}

void UCharacterManager::OnAuthenticated(const FString& uid)
{
    OnDisconnected();
    this->accountUid = uid;
    this->authenticated = !uid.IsEmpty();
    this->listRetryCount = 0;
    this->pendingSave = UGameplayStatics::DoesSaveGameExist(SaveSlot(), 0)
                            ? Cast<UCharacterPendingSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot(), 0))
                            : nullptr;
    if (this->pendingSave &&
        (this->pendingSave->accountUid != uid || !RpgProtocol::IsOperationId(this->pendingSave->operationId)))
    {
        this->pendingSave = nullptr;
    }

    SetCreationState(this->pendingSave ? ECharacterCreationState::NEEDS_CONFIRMATION : ECharacterCreationState::IDLE);
}

void UCharacterManager::OnDisconnected()
{
    ++this->connectionGeneration;
    this->authenticated = false;
    this->listLoaded = false;
    this->pendingListId = this->pendingCreateId = 0;
    this->characters.Reset();
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->listTimer);
        world->GetTimerManager().ClearTimer(this->creationTimer);
    }

    if (this->pendingSave)
    {
        SetCreationState(ECharacterCreationState::NEEDS_CONFIRMATION);
    }

    SetListState(ECharacterListState::DISCONNECTED);
}

void UCharacterManager::SetListState(ECharacterListState state)
{
    this->listState = state;
    this->onCharacterListStateChanged.Broadcast(state);
}

bool UCharacterManager::RetryCharacterList()
{
    if (!this->authenticated || this->pendingListId != 0)
    {
        return false;
    }

    this->listRetryCount = 0;
    return RequestCharacterList();
}

void UCharacterManager::SetCreationState(ECharacterCreationState state)
{
    if (this->creationState == state)
    {
        return;
    }

    this->creationState = state;
    this->onCharacterCreationStateChanged.Broadcast(state);
}

bool UCharacterManager::RequestCharacterList()
{
    if (!this->authenticated || this->pendingListId != 0 || !GetWorld())
    {
        return false;
    }

    SetListState(ECharacterListState::LOADING);
    UPacketManager* packets = GetGameInstance()->GetSubsystem<UPacketManager>();
    this->pendingListId = AllocateRequestId();
    this->listGeneration = this->connectionGeneration;
    this->listMutationVersion = this->mutationVersion;
    if (!packets || !packets->sendProcessor || !packets->sendProcessor->SendCharacterListPacket(this->pendingListId))
    {
        this->pendingListId = 0;
        GetWorld()->GetTimerManager().SetTimer(this->listTimer, this, &UCharacterManager::ListTimedOut, 2.0f, false);
        return false;
    }

    GetWorld()->GetTimerManager().SetTimer(this->listTimer, this, &UCharacterManager::ListTimedOut, 15.0f, false);
    return true;
}

void UCharacterManager::ListTimedOut()
{
    this->pendingListId = 0;
    if (this->authenticated && this->listRetryCount++ < 3)
    {
        RequestCharacterList();
    }
    else
    {
        SetListState(this->authenticated ? ECharacterListState::FAILED_VALUE : ECharacterListState::DISCONNECTED);
    }

}

bool UCharacterManager::CreateCharacter(const FCreateCharacterOptions& options)
{
    if (!this->authenticated || this->pendingSave || !GetWorld())
    {
        return false;
    }

    if (FTCHARToUTF8(*options.characterName).Length() > 96)
    {
        return false;
    }

    this->pendingSave = NewObject<UCharacterPendingSave>(this);
    this->pendingSave->accountUid = this->accountUid;
    this->pendingSave->operationId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    this->pendingSave->options = options;
    // Save before transmission. A crash after transmission must not lose the id.
    if (!UGameplayStatics::SaveGameToSlot(this->pendingSave, SaveSlot(), 0))
    {
        this->pendingSave = nullptr;
        return false;
    }

    return SendPendingCreation();
}

bool UCharacterManager::RetryPendingCreation()
{
    if (!this->authenticated || !this->pendingSave || this->pendingCreateId != 0 || !GetWorld())
    {
        return false;
    }

    return SendPendingCreation();
}

bool UCharacterManager::SendPendingCreation()
{
    RpgProtocol::FCreateCharacterRequestPacket request;
    request.requestId = AllocateRequestId();
    request.operationId = this->pendingSave->operationId;
    request.characterName = this->pendingSave->options.characterName;
    request.race = this->pendingSave->options.race;
    request.gender = this->pendingSave->options.gender;
    this->pendingCreateId = request.requestId;
    this->createGeneration = this->connectionGeneration;
    UPacketManager* packets = GetGameInstance()->GetSubsystem<UPacketManager>();
    if (!packets || !packets->sendProcessor || !packets->sendProcessor->SendCreateCharacterPacket(request))
    {
        this->pendingCreateId = 0;
        SetCreationState(ECharacterCreationState::NEEDS_CONFIRMATION);
        return false;
    }

    SetCreationState(ECharacterCreationState::IN_FLIGHT);
    GetWorld()->GetTimerManager().SetTimer(this->creationTimer, this, &UCharacterManager::CreationTimedOut, 15.0f,
                                           false);
    return true;
}

void UCharacterManager::CreationTimedOut()
{
    this->pendingCreateId = 0;
    SetCreationState(ECharacterCreationState::NEEDS_CONFIRMATION);
}

FCharacterSummary UCharacterManager::Convert(const RpgProtocol::FCharacterSummaryPacket& source)
{
    FCharacterSummary value;
    value.characterUnique = static_cast<int64>(source.characterId);
    value.name = source.characterName;
    value.race = source.race;
    value.gender = source.gender;
    value.level = static_cast<int32>(source.level);
    for (const auto& item : source.equipment)
    {
        FEquipmentSummary equipment;
        equipment.slot = item.slot;
        equipment.itemTemplateId = static_cast<int32>(item.itemTemplateId);
        equipment.appearanceId = static_cast<int32>(item.appearanceId);
        value.equipment.Add(equipment);
    }

    return value;
}

void UCharacterManager::HandleList(const RpgProtocol::FCharacterListResponsePacket& packet)
{
    if (!this->authenticated || packet.requestId != this->pendingListId || this->pendingListId == 0 ||
        this->listGeneration != this->connectionGeneration)
    {
        return;
    }

    this->pendingListId = 0;
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->listTimer);
    }

    if (packet.result != RpgProtocol::ECharacterListResult::SUCCESS)
    {
        if (GetWorld() && this->listRetryCount < 3)
        {
            GetWorld()->GetTimerManager().SetTimer(this->listTimer, this, &UCharacterManager::ListTimedOut, 2.0f,
                                                   false);
        }
        else
        {
            SetListState(ECharacterListState::FAILED_VALUE);
        }

        return;
    }

    if (this->listMutationVersion != this->mutationVersion)
    {
        RequestCharacterList();
        return;
    }

    this->listRetryCount = 0;
    this->characters.Reset(packet.characters.Num());
    this->listLoaded = true;
    for (const auto& source : packet.characters)
    {
        this->characters.Add(Convert(source));
    }

    SetListState(ECharacterListState::READY);
    this->onCharacterListChanged.Broadcast();
}

void UCharacterManager::HandleCreation(const RpgProtocol::FCreateCharacterResponsePacket& packet)
{
    if (!this->authenticated || !this->pendingSave || this->pendingCreateId == 0 ||
        packet.requestId != this->pendingCreateId || packet.operationId != this->pendingSave->operationId ||
        this->createGeneration != this->connectionGeneration)
    {
        return;
    }

    if (packet.result == RpgProtocol::ECreateCharacterResult::SUCCESS && packet.characters.Num() != 1)
    {
        return;
    }

    this->pendingCreateId = 0;
    if (UWorld* world = GetWorld())
    {
        world->GetTimerManager().ClearTimer(this->creationTimer);
    }

    const bool B_UNCERTAIN = packet.result == RpgProtocol::ECreateCharacterResult::UNKNOWN_OUTCOME ||
                             packet.result == RpgProtocol::ECreateCharacterResult::UNAVAILABLE ||
                             packet.result == RpgProtocol::ECreateCharacterResult::BUSY;
    if (B_UNCERTAIN)
    {
        SetCreationState(ECharacterCreationState::NEEDS_CONFIRMATION);
        return;
    }

    FCharacterSummary created;
    if (packet.result == RpgProtocol::ECreateCharacterResult::SUCCESS && packet.characters.Num() == 1)
    {
        created = Convert(packet.characters[0]);
        ++this->mutationVersion;
        const int32 INDEX = this->characters.IndexOfByPredicate(
            [&created](const FCharacterSummary& item)
            {
                return item.characterUnique == created.characterUnique;
            });

        if (INDEX == INDEX_NONE)
        {
            this->characters.Add(created);
        }
        else
        {
            this->characters[INDEX] = created;
        }

    }

    // Do not allow a new operation while the old durable record cannot be removed.
    if (!UGameplayStatics::DeleteGameInSlot(SaveSlot(), 0))
    {
        SetCreationState(ECharacterCreationState::NEEDS_CONFIRMATION);
        return;
    }

    this->pendingSave = nullptr;
    SetCreationState(ECharacterCreationState::COMPLETED);
    if (packet.result == RpgProtocol::ECreateCharacterResult::SUCCESS)
    {
        this->onCharacterListChanged.Broadcast();
    }

    this->onCharacterCreationCompleted.Broadcast(packet.operationId,
                                                 static_cast<ECharacterCreationResult>(packet.result), created);
}

