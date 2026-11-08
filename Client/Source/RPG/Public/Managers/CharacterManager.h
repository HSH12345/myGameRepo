#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Managers/CharacterTypes.h"
#include "Managers/LoginManagerModules/Struct_LoginManager.h"
#include "RPG/Packet/CreateCharacterPackets.h"
#include "CharacterManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCharacterListChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCharacterListStateChanged, ECharacterListState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCharacterCreationCompleted, const FString&, OperationId,
                                               ECharacterCreationResult, Result, const FCharacterSummary&, Character);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCharacterCreationStateChanged, ECharacterCreationState, State);

UCLASS()

class RPG_API UCharacterManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable)
    bool RequestCharacterList();
    UFUNCTION(BlueprintCallable)
    bool CreateCharacter(const FCreateCharacterOptions& options);
    UFUNCTION(BlueprintCallable)
    bool RetryPendingCreation();
    UFUNCTION(BlueprintPure)

    const TArray<FCharacterSummary>& GetCharacters() const
    {
        return this->characters;
    }

    UFUNCTION(BlueprintPure)

    bool HasCharacterList() const
    {
        return this->listLoaded;
    }

    UFUNCTION(BlueprintPure)

    bool IsAuthenticated() const
    {
        return this->authenticated;
    }

    UFUNCTION(BlueprintPure)

    ECharacterListState GetListState() const
    {
        return this->listState;
    }

    UFUNCTION(BlueprintCallable)
    bool RetryCharacterList();
    UFUNCTION(BlueprintPure)

    ECharacterCreationState GetCreationState() const
    {
        return this->creationState;
    }

    FCreateCharacterOptions GetPendingOptions() const
    {
        return this->pendingSave ? this->pendingSave->options : FCreateCharacterOptions{};
    }

    UFUNCTION(BlueprintPure)

    FString GetPendingOperationId() const
    {
        return this->pendingSave ? this->pendingSave->operationId : FString();
    }

    UPROPERTY(BlueprintAssignable)
    FCharacterListChanged onCharacterListChanged;
    UPROPERTY(BlueprintAssignable)
    FCharacterListStateChanged onCharacterListStateChanged;
    UPROPERTY(BlueprintAssignable)
    FCharacterCreationCompleted onCharacterCreationCompleted;
    UPROPERTY(BlueprintAssignable)
    FCharacterCreationStateChanged onCharacterCreationStateChanged;

    void OnAuthenticated(const FString& uid);
    void OnDisconnected();
    void HandleList(const RpgProtocol::FCharacterListResponsePacket& packet);
    void HandleCreation(const RpgProtocol::FCreateCharacterResponsePacket& packet);

private:
    friend class FCharacterCreationStateTest;
    friend class FLobbyPresentationTest;
    bool SendPendingCreation();
    void CreationTimedOut();
    void ListTimedOut();
    void SetCreationState(ECharacterCreationState state);
    void SetListState(ECharacterListState state);
    uint32 AllocateRequestId();
    FString SaveSlot() const;
    static FCharacterSummary Convert(const RpgProtocol::FCharacterSummaryPacket& source);
    UPROPERTY(Transient)
    TArray<FCharacterSummary> characters;
    UPROPERTY(Transient)
    TObjectPtr<UCharacterPendingSave> pendingSave;
    FString accountUid;
    bool authenticated = false;
    bool listLoaded = false;
    ECharacterListState listState = ECharacterListState::DISCONNECTED;
    uint32 nextRequestId = 1;
    uint32 pendingListId = 0;
    uint32 pendingCreateId = 0;
    uint64 connectionGeneration = 0;
    uint64 listGeneration = 0;
    uint64 createGeneration = 0;
    uint64 mutationVersion = 0;
    uint64 listMutationVersion = 0;
    int32 listRetryCount = 0;
    FTimerHandle listTimer;
    FTimerHandle creationTimer;
    ECharacterCreationState creationState = ECharacterCreationState::IDLE;
};

