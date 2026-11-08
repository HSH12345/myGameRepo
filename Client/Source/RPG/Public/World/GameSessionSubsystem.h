#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "SharedWorld/Network/WorldProtocol.h"
#include "World/WorldHudTypes.h"
#include <deque>
#include <memory>
#include <atomic>
#include "GameSessionSubsystem.generated.h"
class FWorldLoadingScreen;
class AWorldPlayerPawn;
class AWorldMoveMarker;
class AWorldCombatEffect;
class APlayerController;
class UWorldPartitionStreamingSourceComponent;
class UTextRenderComponent;
class UWorldControlHUD;
class UWorldInventoryUI;
struct FGameClientWorld;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldStatusChanged, const FString&, Status);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDungeonExited, int32, ExitType, int32, Outcome);

UCLASS()

class RPG_API UGameSessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable)
    bool EnterCharacter(int64 characterId);
    UFUNCTION(BlueprintCallable)
    bool UsePortal();
    void Interact();
    UFUNCTION(BlueprintCallable)
    bool TryUseDungeonExit();
    UPROPERTY(BlueprintAssignable)
    FDungeonExited onDungeonExited;
    UPROPERTY(BlueprintReadOnly, Transient)
    int32 lastDungeonExitType = 0;
    UPROPERTY(BlueprintReadOnly, Transient)
    int32 lastDungeonExitOutcome = 0;
    UFUNCTION(BlueprintCallable)
    bool TalkToNearestNpc();
    UFUNCTION(BlueprintCallable)
    bool ChooseNpcWarp();
    bool SetDungeonReady(bool ready);
    bool LeaveDungeonMatch();

    const shared_world::net::Message& GetDungeonMatch() const
    {
        return this->dungeonMatch;
    }

    uint64 GetOwnEntity() const
    {
        return this->prepared.epoch;
    }

    double DungeonCountdown() const;
    bool IsPartyMember(uint64 entity) const;
    bool IsHostileEntity(uint64 entity) const;
    AWorldPlayerPawn* CombatPawn(uint64 entity) const;
    UFUNCTION(BlueprintCallable)
    void CloseNpcDialogue();
    UFUNCTION(BlueprintCallable)
    void ToggleInventory();
    UFUNCTION(BlueprintCallable)
    void CloseShop();
    UFUNCTION(BlueprintCallable)
    bool BuyOffer(int32 offer, int32 quantity = 1);
    UFUNCTION(BlueprintCallable)
    bool EquipBagItem(int32 bag);
    UFUNCTION(BlueprintCallable)
    bool UseBagItem(int32 bag);
    UFUNCTION(BlueprintCallable)
    bool UseHealthPotion();
    UFUNCTION(BlueprintCallable)
    bool UseManaPotion();
    bool RegisterPotion(int32 bag);
    void ActivateQuickSlot(int32 slot, bool fromHotkey = false);
    const shared_world::skills::Catalog* GetSkillCatalog() const;
    shared_world::skills::State GetSkillState() const;
    uint32 GetSkillRank(uint32 skill) const;
    shared_world::combat::Ability GetAbility(uint32 skill) const;
    bool TrainSkill(uint32 skill);
    bool RegisterPotionType(int32 index, uint32 type);
    TArray<FWorldQuickSlotState> GetPotionStates() const;

    uint32 GetRegisteredPotion(int32 index) const
    {
        return index >= 0 && index < 2 ? this->registeredPotions[index] : 0;
    }

    bool SkillTrainingReady() const
    {
        return this->economyReady && !EconomyBusy();
    }

    bool BindQuickSlot(int32 slot, uint32 binding);

    int32 GetSelectedQuickSlot() const
    {
        return this->selectedQuickSlot;
    }

    uint32 GetQuickSlotBinding(int32 slot) const
    {
        return slot >= 0 && slot < 10 ? this->quickSlotBindings[slot] : 0;
    }

    TArray<FWorldQuickSlotState> GetQuickSlotStates() const;
    void BuildMinimap(FWorldMinimapSnapshot& snapshot) const;
    UFUNCTION(BlueprintCallable)
    bool UnequipBagItem(int32 bag);
    UFUNCTION(BlueprintCallable)
    bool MoveBagItem(int32 bag, int32 destination);
    UFUNCTION(BlueprintCallable)
    bool SplitBagItem(int32 bag, int32 quantity, int32 destination);

    const shared_world::economy::State& GetEconomyState() const
    {
        return this->economyState;
    }

    const shared_world::economy::Catalog* GetItemCatalog() const
    {
        return this->itemCatalog.get();
    }

    const std::vector<shared_world::economy::Offer>& GetShopOffers() const
    {
        return this->shopOffers;
    }

    AWorldPlayerPawn* GetInventoryCharacter() const;
    AWorldPlayerPawn* GetShopMerchant() const;

    bool EconomyBusy() const
    {
        return this->economyPending || this->economyUnknown;
    }

    UFUNCTION(BlueprintCallable)
    bool RequestMoveTo(FVector worldLocation);
    void Receive(const uint8* data, int32 size);
    UFUNCTION(BlueprintPure)

    bool IsBusy() const
    {
        return this->state != 0;
    }

    UPROPERTY(BlueprintAssignable)
    FWorldStatusChanged onStatusChanged;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FSkillManaInputTest;
#endif
    bool CanAffordSkill(uint32 skill) const;
    void CancelUnaffordableSkill();
    shared_world::net::Message dungeonMatch;
    double matchReceivedAt = 0;
    uint64 matchSequence = 0;
    TMap<uint64, uint64> entityParties;
    bool Send(const shared_world::net::Message& message);
    bool Tick(float delta);
    void LoadedWorld(UWorld* world);
#if PLATFORM_ANDROID
    void InstallMobileSky(UWorld& world);
#endif
    void Fail(const FString& reason, bool disconnect = true);
    void ConnectionClosed();
    void ClearPlayers();
    void ClearNpcDialogue(bool notifyServer);
    bool OpenShop(uint64 entity);
    bool RequestEconomy(shared_world::economy::Request newRequest);
    bool ReceiveEconomy(const shared_world::net::Message& message);
    void TickEconomy();
    void ClearEconomy();
    void RefreshInventory();
    bool InventoryVisible() const;
    const shared_world::economy::Item* BagItem(int32 slot) const;
    UPROPERTY(Transient)
    TObjectPtr<UWorldInventoryUI> inventoryUi;
    std::shared_ptr<const shared_world::economy::Catalog> itemCatalog;
    shared_world::economy::State economyState;
    shared_world::economy::Request pendingEconomy;
    std::vector<shared_world::economy::Offer> shopOffers;
    uint64 shopNpc = 0, shopToken = 0, shopId = 0, economyCharacter = 0, pendingEconomyCharacter = 0;
    uint32 quickSlotBindings[10] = {3, 4};
    int32 selectedQuickSlot = -1;
    uint64 activeAttackTarget = 0;
    UPROPERTY(Transient)
    TWeakObjectPtr<AWorldPlayerPawn> highlightedEnemy;
    void ClearAttackHighlight();
    uint32 registeredPotions[2] = {0, 0}; // Template IDs survive bag moves and stack replacement.
    double potionHudReadyAt = 0;
    uint32 pendingPotionCooldownMs = 0;
    bool UseRegisteredPotion(int32 slot);
    void TickAutoPotions();
    double nextAutoPotion = 0;
    uint32 economyRequest = 100, pendingEconomyRequest = 0;
    bool economyReady = false, economyPending = false, economyUnknown = false, shopOpening = false;
    double economyDeadline = 0, shopDeadline = 0;
    void BeginMapLoad(const shared_world::net::Message& prepare);
    bool StreamingReady() const;
    bool TickLoadingScreen(float delta);
    TSharedPtr<FWorldLoadingScreen> loadingScreen;
    double presentationDeadline = 0;
#if !UE_BUILD_SHIPPING
    uint64 loadingCaptureGeneration = MAX_uint64;
    bool measureReveal = false;
    TArray<float> revealFrameTimes;
#endif
    bool ReceiveCombat(const shared_world::net::Message&);
    bool TickCombatControl(float delta);
    void PollActionInput();
    bool WorldPointerPosition(const APlayerController& controller, FVector2D& position) const;
#if PLATFORM_ANDROID
    struct FMobileCameraGesture
    {
        FVector2D drag = FVector2D::ZeroVector;
        float zoomRatio = 1.f;
    };

    FMobileCameraGesture UpdateMobileTouch(const APlayerController& controller, bool worldInputAllowed);
#endif

    enum EActionInput : uint32
    {
        PRIMARY_INPUT = 1,
        SECONDARY_INPUT = 2,
        BOLT_INPUT = 4,
        AREA_INPUT = 8,
        BASIC_INPUT = 16,
        CANCEL_INPUT = 32,
        TARGET_INPUT = 64,
        MELEE_MODE_INPUT = 128,
        RANGED_MODE_INPUT = 256,
        WALK_MODE_INPUT = 512,
        POTION_INPUT = 1024,
        MANA_POTION_INPUT = 2048,
        SKILL_BOOK_INPUT = 4096,
        SLOT_5_INPUT = 8192,
        SLOT_6_INPUT = 16384,
        SLOT_7_INPUT = 32768,
        SLOT_8_INPUT = 65536,
        SLOT_9_INPUT = 131072,
        SLOT_0_INPUT = 262144
    };

    uint32 actionInputBits = 0, actionPressedBits = 0, actionReleasedBits = 0;
    bool RequestAttack(uint32 skill, FVector point = FVector::ZeroVector, uint64 targetOverride = UINT64_MAX);
    bool RequestMoveToInternal(FVector location, bool combatRoute);
    uint64 PickCombatTarget() const;
    unsigned CombatRadius(uint64 entity) const;
    shared_world::WorldPosition PredictBodyMove(shared_world::WorldPosition from, int dx, int dy,
                                                const shared_world::WorldPosition* candidate = nullptr) const;
    bool CursorGround(FVector& point) const;
    bool TracePointerScene(FHitResult& hit, const FVector& start, const FVector& end) const;
    void QueueCombatIntent(uint32 skill, uint64 target, FVector point, int32 repeatKey = 0, bool stationary = false);
    void CancelCombatIntent(bool cancelCast = false);
    void TickCombatIntent();
    void UpdateCombatFeedback();
    void UpdateBossCamera(bool inside);
    float savedBossCameraDistance = 0;
    UPROPERTY(Transient)
    TObjectPtr<AWorldCombatEffect> combatAimMarker;
    UPROPERTY(Transient)
    TObjectPtr<AWorldCombatEffect> combatTargetMarker;
    uint32 intentSkill = 0, ownSkill = 0;
    uint64 intentTarget = 0, intentLife = 0;
    FVector intentPoint = FVector::ZeroVector;
    int32 intentRepeatKey = 0;
    bool intentStationary = false, intentSent = false, attackRequestPending = false, combatMoveOverride = false;
    bool primaryAttackGesture = false, suppressPrimaryUntilRelease = false;
    bool primaryManaFallback = false;
    bool rightGestureActive = false, rightCameraDrag = false, rightSkillTap = false, rightClickConsumed = false;
    float rightDragDistance = 0, rightGestureX = 0, rightGestureY = 0;
#if PLATFORM_ANDROID
    struct FMobileWorldTouch
    {
        FVector2D start = FVector2D::ZeroVector;
        FVector2D position = FVector2D::ZeroVector;
        FVector2D cameraCenter = FVector2D::ZeroVector;
        double cameraSpan = 0;
        bool tracking = false;
        bool consumed = false;
        bool cameraDragging = false;
        bool tap = false;
    } mobileTouch;

#endif
    double intentExpires = 0, combatRequestDeadline = 0, ownRecoveryEnd = 0, ownCastEnd = 0, nextCombatSubmit = 0;
    double nextPointerRefresh = 0, nextApproachPlan = 0, approachProgressAt = 0;
    shared_world::WorldPosition approachLastFeet{}, approachLastTarget{};
    TMap<uint32, double> abilityReadyAt;
    FString combatActionText = TEXT("Ready");
    void ClearCombat();
    AWorldCombatEffect* CombatEffect(const shared_world::combat::Event&, int32 kind);
    double CombatServerNow() const;
    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldCombatEffect>> combatEffects;
    TMap<uint64, uint64> combatLives;
    uint64 combatTarget = 0, combatSequence = 0, ownCast = 0;
    uint32 basicAbility = 1;
    bool aimingArea = false;
    double combatOffset = 0;
    bool combatClock = false;
    void TickControl(float delta);
    void CancelClickMove();
    void StartClickPath();
    void PrepareMoveMarkers();
    void ShowMoveMarker(FVector location, bool confirmed);
    UPROPERTY(Transient)
    TArray<TObjectPtr<AWorldMoveMarker>> moveMarkerPool;
    UPROPERTY(Transient)
    TObjectPtr<AWorldMoveMarker> activeMoveMarker;
    int32 nextMoveMarker = 0;
    double clickStarted = 0;
    bool clickFirstMove = false;
    uint32 clickPlanRetries = 0;
    shared_world::WorldPosition clickOrigin{};
    shared_world::WorldPosition ControlStep(int x, int y);
    std::vector<shared_world::WorldPosition> clickPath;
    std::size_t clickWaypoint = 0;
    shared_world::WorldPosition clickGoal{};
    bool clickPending = false;
    bool walkMode = false; // Default run; Caps Lock toggles both WASD and click movement.
    uint64 clickRequest = 0;
    double clickDeadline = 0;
    int32 clickBlockedTicks = 0;
    std::shared_ptr<std::atomic<bool>> clickCancellation;
    std::shared_ptr<std::atomic<int>> clickTaskBudget = std::make_shared<std::atomic<int>>(0);
    FTSTicker::FDelegateHandle tickHandle;
    FDelegateHandle mapLoadedHandle;
    FDelegateHandle connectionClosedHandle;
    std::shared_ptr<FGameClientWorld> map;
    std::shared_ptr<std::atomic<int>> mapLoadBudget = std::make_shared<std::atomic<int>>(0);
    UPROPERTY(Transient)
    TObjectPtr<AActor> streamingActor;
    UPROPERTY(Transient)
    TObjectPtr<AActor> portalActor;
    UPROPERTY(Transient)
    TObjectPtr<UWorldControlHUD> controlHud;
    UPROPERTY(Transient)
    TObjectPtr<UTextRenderComponent> portalLabel;
    UPROPERTY(Transient)
    TObjectPtr<UWorldPartitionStreamingSourceComponent> streamingSource;
    bool levelLoaded = false;
    int32 streamingFrames = 0;
    UPROPERTY(Transient)
    TMap<uint64, TObjectPtr<AWorldPlayerPawn>> players;
    UPROPERTY(Transient)
    TMap<uint64, TObjectPtr<AWorldPlayerPawn>> npcs;
    TMap<uint64, FVector> remoteTargets;
    std::deque<shared_world::net::Message> pendingInputs;
    std::deque<shared_world::WorldPosition> pendingPositions;
    shared_world::net::Message prepared;
    shared_world::WorldPosition predicted{};
    uint64 generation = 0, nextSequence = 0;
    uint32 request = 0;
    int32 state = 0; // Idle, Selecting, Entering, Loading, WaitingSpawn, Playing
    uint64 currentNpc = 0, npcSequence = 0, dialogueId = 0;
    bool npcAwaitingTalk = false, npcWarping = false;
    double npcDeadline = 0;
    shared_world::dungeon::Layout dungeonExits;
    shared_world::dungeon::Receipt pendingExit;
    uint64 exitSequence = 0;
    bool exitWarping = false;
    double exitDeadline = 0;
    double deadline = 0, accumulator = 0;
#if !UE_BUILD_SHIPPING
    void TickLobbySmoke();
    uint64 smokeLobbyCharacter = 0;
    void TickMazeSmoke();
    void TickVisibilitySmoke();
    void TickPresentationSmoke();
    void TickQuickSlotSmoke();
    void TickActionSmoke();
    void TickCombatSmoke();
    void TickSmoke();
    void TickControlSmoke();
    void TickNpcSmoke();
    void TickNpcWarpSmoke();
    void TickLoadingSmoke();
    void TickSkillSmoke();
    void TickActionBarSmoke();
    void TickEconomySmoke();
    FIntPoint smokeExitMidpoint{};
    int32 smokeExpectedExitType = 0;
    bool controlSmoke = false;
    int32 controlSmokeStage = 0;
    double controlSmokeTime = 0;
    bool controlMovingShot = false, controlWalkingShot = false;
    shared_world::WorldPosition paceSmokeOrigin{};
    uint64 paceSmokeSequence = 0;
    double economySmokeCaptureTime = 0;
    double smokeRouteLastStep = 0, smokeRouteMaxGap = 0;
    int32 smokeRouteSteps = 0, smokeRouteBackwards = 0;
    bool smoke = false, smokeConnected = false, smokeSelected = false, smokeRemote = false, smokeMoved = false,
         smokeShot = false;
    double smokeStarted = 0, smokeReady = 0, smokeShotTime = 0, smokeProgressTime = 0;
    bool smokeDespawn = false, smokeInjected = false, smokeCorrected = false;
    bool smokeRecoveryDropped = false, smokeStaleLoadRejected = false;
    bool smokeRecoveryWorldDropped = false;
    int32 smokeTerrainStage = 0, smokeGroundChecks = 0;
    int32 smokeTransitionStage = 0;
#endif
};

