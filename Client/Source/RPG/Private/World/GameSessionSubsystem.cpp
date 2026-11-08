#include "World/GameSessionSubsystem.h"
#include "SharedWorld/Dungeon/DungeonMaze.h"
#include "GameClientWorld.h"
#include "WorldLoadingScreen.h"
#include "ContentStreaming.h"
#include "ShaderPipelineCache.h"
#include "ShaderCompiler.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "World/WorldPlayerPawn.h"
#include "World/WorldCameraOcclusion.h"
#include "World/WorldMoveMarker.h"
#include "World/WorldControlHUD.h"
#include "World/WorldCoordinateAdapter.h"
#include "SharedWorld/Map/MapDataProvider.h"
#include "SharedWorld/Map/ChunkResidency.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Components/TextRenderComponent.h"
#include "SharedWorld/Movement/MovementController.h"
#include "SharedWorld/Movement/GroundPath.h"
#include "SharedWorld/Movement/PredictionHistory.h"
#include "Managers/NetworkManager.h"
#include "Managers/NetworkManagerModules/Sender.h"
#include "RPG/Packet/BinaryProtocol.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "UObject/UObjectGlobals.h"
#include "Async/Async.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#if PLATFORM_ANDROID
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/Material.h"
#endif
#include "Managers/LoginManager.h"
#include "Managers/CharacterManager.h"
#include "Managers/UIManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"
#include "World/WorldEntityPool.h"
#include "World/WorldExitZones.h"

void UGameSessionSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);
    collection.InitializeDependency<UNetworkManager>();
    this->connectionClosedHandle = GetGameInstance()->GetSubsystem<UNetworkManager>()->onConnectionClosed.AddUObject(
        this, &UGameSessionSubsystem::ConnectionClosed);
#if !UE_BUILD_SHIPPING
    this->smoke = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClient")) &&
                  FParse::Param(FCommandLine::Get(), TEXT("RpgEphemeralGuest"));
    this->controlSmoke = this->smoke && (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeMaze")) ||
                                         FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeControls")) ||
                                         FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeNpcWarp")) ||
                                         FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeEconomy")) ||
                                         FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeCombat")) ||
                                         FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeAction")));
    this->smokeStarted = FPlatformTime::Seconds();
#endif
    this->tickHandle =
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UGameSessionSubsystem::Tick));
    this->mapLoadedHandle =
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UGameSessionSubsystem::LoadedWorld);
}

void UGameSessionSubsystem::Deinitialize()
{
    if (auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>())
    {
        network->onConnectionClosed.Remove(this->connectionClosedHandle);
    }

    ++this->generation;
    FTSTicker::GetCoreTicker().RemoveTicker(this->tickHandle);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(this->mapLoadedHandle);
    this->loadingScreen.Reset();
    ClearPlayers();
    this->map.reset();
    Super::Deinitialize();
}

bool UGameSessionSubsystem::Send(const shared_world::net::Message& message)
{
    auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>();
    if (!network || !network->GetSender())
    {
        return false;
    }

    try
    {
        const auto BYTES = message.Encode();
        TArray<uint8> payload;
        payload.Append(BYTES.data(), static_cast<int32>(BYTES.size()));
        return network->GetSender()->SendPacket(RpgProtocol::EPacketType::WORLD_MESSAGE, payload);
    }

    catch (const std::exception&)
    {
        return false;
    }

}

bool UGameSessionSubsystem::EnterCharacter(int64 characterId)
{
    if (this->state != 0 || characterId <= 0)
    {
        return false;
    }

    shared_world::net::Message message;
    message.kind = shared_world::net::Kind::SELECT;
    message.request = ++this->request;
    message.character = static_cast<uint64>(characterId);
    if (!Send(message))
    {
        return false;
    }

    ++this->generation;
    this->state = 1;
    this->deadline = FPlatformTime::Seconds() + 20;
    this->onStatusChanged.Broadcast(TEXT("Checking character ownership..."));
    return true;
}

void UGameSessionSubsystem::ClearPlayers()
{
    ClearCombat();
    ClearEconomy();
    this->dungeonMatch = {};
    this->entityParties.Empty();
    this->walkMode = false;
    if (auto* world = GetWorld())
    {
        world->GetSubsystem<UWorldExitPool>()->ReleaseAll();
    }

    this->dungeonExits = {};
    this->exitWarping = false;
    this->exitDeadline = 0;
    ClearNpcDialogue(false);
    CancelClickMove();
    for (const auto& marker : this->moveMarkerPool)
    {
        if (IsValid(marker))
        {
            marker->Destroy();
        }

    }

    this->moveMarkerPool.Empty();
    this->nextMoveMarker = 0;
    if (this->controlHud)
    {
        this->controlHud->RemoveFromParent();
        this->controlHud = nullptr;
    }

    if (IsValid(this->portalActor))
    {
        this->portalActor->Destroy();
    }

    this->portalActor = nullptr;
    this->portalLabel = nullptr;
    if (IsValid(this->streamingActor))
    {
        this->streamingActor->Destroy();
    }

    this->streamingActor = nullptr;
    this->streamingSource = nullptr;
    this->levelLoaded = false;
    this->streamingFrames = 0;
    if (auto* world = GetWorld())
    {
        if (auto* pool = world->GetSubsystem<UWorldEntityPool>())
        {
            for (auto& entry : this->players)
            {
                pool->Release(entry.Value);
            }

            for (auto& entry : this->npcs)
            {
                pool->Release(entry.Value);
            }

        }

    }

    this->npcs.Empty();
    this->players.Empty();
    this->remoteTargets.Empty();
    this->pendingInputs.clear();
    this->pendingPositions.clear();
    this->nextSequence = 0;
}

bool UGameSessionSubsystem::UsePortal()
{
    if (this->state != 5 || this->prepared.entity != 1 || EconomyBusy())
    {
        return false;
    }

    shared_world::net::Message message;
    message.kind = shared_world::net::Kind::PORTAL;
    message.epoch = this->prepared.epoch;
    message.request = this->prepared.request;
    message.entity = 1;
    return Send(message);
}

void UGameSessionSubsystem::Interact()
{
    if (this->state == 5 && !this->dungeonMatch.room && !this->currentNpc && !InventoryVisible() &&
        !this->shopOpening && !this->exitWarping && (!this->loadingScreen || !this->loadingScreen->IsVisible()) &&
        !TryUseDungeonExit() && !TalkToNearestNpc())
    {
        UsePortal();
    }

}

void UGameSessionSubsystem::ConnectionClosed()
{
    // Invalidate asynchronous completion immediately, rather than on the next
    // ticker frame. A connection can close and reopen between two ticks.
    if (this->state)
    {
        Fail(TEXT("World connection lost"), false);
    }

}

void UGameSessionSubsystem::Fail(const FString& reason, bool disconnect)
{
    this->pendingExit = {};
    ++this->generation;
    this->state = 0;
    if (this->loadingScreen)
    {
        this->loadingScreen->Hide();
    }

    ClearPlayers();
    this->map.reset();
    this->onStatusChanged.Broadcast(reason);
    UE_LOG(LogTemp, Warning, TEXT("World session: %s"), *reason);
    if (disconnect)
    {
        if (auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>())
        {
            network->DisconnectLoginServer();
        }

    }

    // Reuse the lobby controls over the current world; opening a new level here
    // would destroy the world's pending network reconnect timer.
    if (auto* ui = GetGameInstance()->GetSubsystem<UUIManager>(); ui && ui->GetUIController())
    {
        ui->StartTitle();
        if (auto* pc = GetGameInstance()->GetFirstLocalPlayerController())
        {
            pc->SetInputMode(FInputModeUIOnly());
            pc->bShowMouseCursor = true;
        }

    }

}

void UGameSessionSubsystem::BeginMapLoad(const shared_world::net::Message& prepare)
{
    this->prepared = prepare;
    this->state = 3;
    this->deadline = FPlatformTime::Seconds() + 110;
    if (!this->loadingScreen)
    {
        this->loadingScreen = MakeShared<FWorldLoadingScreen>(GetGameInstance());
    }

    this->loadingScreen->Show(prepare.map.id);
    this->loadingScreen->Update(.1f, TEXT("월드 데이터를 불러오고 있습니다"));
    this->onStatusChanged.Broadcast(TEXT("Loading world content..."));
    TArray<FString> filenames;
    GConfig->GetArray(TEXT("RPG.World"), TEXT("Manifests"), filenames, GGameIni);
    const FString CONTENT_ROOT = FPaths::ProjectContentDir() / TEXT("RPG/WorldData");
#if PLATFORM_ANDROID
    const FString& ROOT = CONTENT_ROOT;
#else
    const FString ROOT = FPaths::ConvertRelativePathToFull(CONTENT_ROOT);
#endif
    FString combatRoot = FPaths::Combine(ROOT, TEXT("Combat"));
    FString npcRoot = FPaths::Combine(ROOT, TEXT("Npcs"));
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeMaze")) ||
        FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeCombat")) ||
        FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeAction")) ||
        FParse::Param(FCommandLine::Get(), TEXT("RpgSkillSmoke")) ||
        (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgCombatPresentationSmoke"))))
    {
        FParse::Value(FCommandLine::Get(), TEXT("RpgCombatFixtureRoot="), combatRoot);
    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgSkillSmoke")))
    {
        FParse::Value(FCommandLine::Get(), TEXT("RpgNpcFixtureRoot="), npcRoot);
    }

#endif
    const uint64 CURRENT_GENERATION = this->generation;
    float testDelay = 0;
#if !UE_BUILD_SHIPPING
    if (this->smoke && !this->smokeRecoveryDropped && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeRecovery")))
    {
        testDelay = 3;
    }

#endif
    // At most two active/loading/retiring worlds, including stale callbacks.
    const auto BUDGET = this->mapLoadBudget;
    if (BUDGET->fetch_add(1) >= 2)
    {
        BUDGET->fetch_sub(1);
        Fail(TEXT("Previous world is still closing; retry shortly."));
        return;
    }

    TWeakObjectPtr<UGameSessionSubsystem> weak(this);
    Async(EAsyncExecution::ThreadPool,
          [weak, CURRENT_GENERATION, ROOT, combatRoot, npcRoot, filenames, prepare, BUDGET, testDelay]
          {
              // Final deletion, including loader join, is always off the game thread.
              std::shared_ptr<FGameClientWorld> result(new FGameClientWorld,
                                                       [BUDGET](FGameClientWorld* value)
                                                       {
                                                           Async(EAsyncExecution::ThreadPool,
                                                                 [value, BUDGET]
                                                                 {
                                                                     delete value;
                                                                     BUDGET->fetch_sub(1);
                                                                 });

                                                       });

              FString error;
              if (testDelay > 0)
              {
                  FPlatformProcess::Sleep(testDelay);
              }

              try
              {
                  if (filenames.Num() > 32)
                  {
                      throw std::invalid_argument("client registry limit");
                  }

                  for (const auto& filename : filenames)
                  {
                      if (!FPaths::IsRelative(filename) || filename.Contains(TEXT("..")) ||
                          filename.Contains(TEXT(":")))
                      {
                          throw std::invalid_argument("invalid client map registry");
                      }

                      const auto PATH = std::filesystem::u8path(TCHAR_TO_UTF8(*FPaths::Combine(ROOT, filename)));
                      auto content = shared_world::WorldContent::Load(PATH);
                      if (content->identity.id != prepare.map.id)
                      {
                          continue;
                      }

                      if (!(content->identity == prepare.map))
                      {
                          throw std::invalid_argument("map build mismatch");
                      }

                      content->npcs = std::make_shared<const shared_world::npc::Catalog>(
                          shared_world::npc::Catalog::Load(std::filesystem::u8path(TCHAR_TO_UTF8(*npcRoot))));
                      content->combat = std::make_shared<const shared_world::combat::Catalog>(
                          shared_world::combat::Catalog::Load(std::filesystem::u8path(TCHAR_TO_UTF8(*combatRoot))));
                      result->content = std::move(content);
                      result->residency = std::make_unique<shared_world::ChunkResidency>(*result->content);
                      break;
                  }

                  if (!result->content)
                  {
                      throw std::invalid_argument("map is not installed in client registry");
                  }

              }

              catch (const std::exception& e)
              {
                  error = UTF8_TO_TCHAR(e.what());
                  result.reset();
              }

              AsyncTask(ENamedThreads::GameThread,
                        [weak, CURRENT_GENERATION, result, error]
                        {
                            auto* self = weak.Get();
                            if (!self)
                            {
                                return;
                            }

                            if (self->generation != CURRENT_GENERATION || self->state != 3)
                            {
#if !UE_BUILD_SHIPPING
                                if (self->smoke)
                                {
                                    self->smokeStaleLoadRejected = true;
                                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE stale_load_rejected"));
                                }

#endif
                                return;
                            }

                            if (!result)
                            {
                                self->Fail(error);
                                return;
                            }

                            self->map = result;
                            const FString PATH = UTF8_TO_TCHAR(result->content->manifest.contentPath.c_str());
                            UWidgetLayoutLibrary::RemoveAllWidgets(self->GetGameInstance());
                            self->loadingScreen->Attach();
                            self->loadingScreen->Update(.35f, TEXT("풍경을 불러오고 있습니다"));
                            self->loadingScreen->PrepareTravel();
                            UGameplayStatics::OpenLevel(self->GetGameInstance(), FName(*PATH));
                        });

          });

}

#if PLATFORM_ANDROID
void UGameSessionSubsystem::InstallMobileSky(UWorld& world)
{
    TActorIterator<ASkyAtmosphere> atmosphere(&world);
    if (!atmosphere)
    {
        return;
    }

    for (TActorIterator<AStaticMeshActor> actor(&world); actor; ++actor)
    {
        const auto* component = actor->GetStaticMeshComponent();
        for (int32 i = 0; i < component->GetNumMaterials(); ++i)
        {
            const auto* material = component->GetMaterial(i);
            if (component->IsVisible() && material && material->GetMaterial()->bIsSky)
            {
                return;
            }

        }

    }

    auto* mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
    auto* material = LoadObject<UMaterial>(
        nullptr, TEXT("/Engine/EngineSky/SkyAtmosphere/SkyAtmosphere_MaterialSkyDome.SkyAtmosphere_MaterialSkyDome"));
    if (!mesh || !material || mesh->GetBounds().SphereRadius <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("MOBILE_SKY dome assets unavailable"));
        return;
    }

    FActorSpawnParameters params;
    params.ObjectFlags |= RF_Transient;
    params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (auto* sky = world.SpawnActor<AStaticMeshActor>(atmosphere->GetActorLocation(), FRotator::ZeroRotator, params))
    {
        constexpr double DOME_RADIUS_CM = 1000000;
        auto* component = sky->GetStaticMeshComponent();
        component->SetMobility(EComponentMobility::Movable);
        component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        component->SetCanEverAffectNavigation(false);
        component->SetCastShadow(false);
        component->SetStaticMesh(mesh);
        component->SetMaterial(0, material);
        sky->SetActorScale3D(FVector(DOME_RADIUS_CM / mesh->GetBounds().SphereRadius));
        UE_LOG(LogTemp, Display, TEXT("MOBILE_SKY dome installed"));
    }

}

#endif
void UGameSessionSubsystem::LoadedWorld(UWorld* world)
{
    if (this->state != 3 || !this->map || !world || world->GetGameInstance() != GetGameInstance())
    {
        return;
    }

    FString name = world->GetOutermost()->GetName();
    // PIE package prefixes are editor-local and never enter the map contract.
    name = UWorld::RemovePIEPrefix(name);
    if (name != UTF8_TO_TCHAR(this->map->content->manifest.contentPath.c_str()))
    {
        Fail(TEXT("Unexpected loaded map"));
        return;
    }

    this->levelLoaded = true;
    this->streamingFrames = 0;
    this->deadline = FPlatformTime::Seconds() + 110;
    if (this->loadingScreen)
    {
        this->loadingScreen->Attach();
        this->loadingScreen->Update(.55f, TEXT("주변 지형을 준비하고 있습니다"));
    }

    PrepareMoveMarkers();
#if PLATFORM_ANDROID
    InstallMobileSky(*world);
#endif
    // Match editor-authored replacement collision for cursor picking too.
    // The server still validates movement against the baked replacement.
    for (TActorIterator<AActor> actor(world); actor; ++actor)
    {
        TInlineComponentArray<UPrimitiveComponent*> components;
        actor->GetComponents(components);
        for (auto* component : components)
        {
            // The v3 maze uses tagged axis-aligned 200 cm wall strips. Keep
            // its outer strips camera-solid while interior walls only fade.
            if (this->prepared.map.id == 301 && this->prepared.map.version >= 3 &&
                (actor->ActorHasTag(TEXT("RPG.CameraFade")) || component->ComponentHasTag(TEXT("RPG.CameraFade"))))
            {
                const auto& limits = this->map->content->collision->Map().Definition().bounds;
                const FVector ORIGIN(world->OriginLocation);
                const FBox BOX = component->Bounds.GetBox();
                const FVector SIZE = BOX.GetSize();
                const bool EDGE_X = SIZE.X <= 205 && (FMath::Abs(BOX.Min.X + ORIGIN.X - limits.min.x) < 5 ||
                                                      FMath::Abs(BOX.Max.X + ORIGIN.X - limits.max.x) < 5);
                const bool EDGE_Y = SIZE.Y <= 205 && (FMath::Abs(BOX.Min.Y + ORIGIN.Y - limits.min.y) < 5 ||
                                                      FMath::Abs(BOX.Max.Y + ORIGIN.Y - limits.max.y) < 5);
                if (EDGE_X || EDGE_Y)
                {
                    component->ComponentTags.AddUnique(TEXT("RPG.CameraBoundary"));
                }

            }

            UWorldCameraOcclusion::ConfigureGeometry(component);
            if (actor->ActorHasTag(FName(*(FString(TEXT("RPGCollisionIgnore:")) + component->GetName()))))
            {
                component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
            }

        }

    }

    if (this->prepared.map.id == 301 && this->prepared.map.version >= 3)
    {
        // Maze wall meshes merge whole horizontal strips. A strip can contain
        // both outer and interior walls, so its response cannot protect only
        // its outer edge. Four invisible camera-only planes close that gap.
        const auto& limits = this->map->content->collision->Map().Definition().bounds;
        const FVector LOW = FVector(limits.min.x, limits.min.y, limits.min.z - 300) - FVector(world->OriginLocation);
        const FVector HIGH = FVector(limits.max.x, limits.max.y, limits.max.z + 2800) - FVector(world->OriginLocation);
        const FVector MID = (LOW + HIGH) * .5, HALF = (HIGH - LOW) * .5;
        auto* boundary = world->SpawnActor<AActor>();
        if (boundary)
        {
            boundary->Tags.Add(TEXT("RPG.CameraBoundary"));
            for (int32 side = 0; side < 4; ++side)
            {
                auto* box = NewObject<UBoxComponent>(boundary);
                boundary->AddInstanceComponent(box);
                if (!boundary->GetRootComponent())
                {
                    boundary->SetRootComponent(box);
                }
                else
                {
                    box->SetupAttachment(boundary->GetRootComponent());
                }

                const bool X_VALUE = side < 2;
                FVector center = MID;
                if (X_VALUE)
                {
                    center.X = (side == 0 ? LOW.X : HIGH.X);
                }
                else
                {
                    center.Y = (side == 2 ? LOW.Y : HIGH.Y);
                }

                box->SetBoxExtent(X_VALUE ? FVector(1, HALF.Y, HALF.Z) : FVector(HALF.X, 1, HALF.Z));
                box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                box->SetCollisionResponseToAllChannels(ECR_Ignore);
                box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
                box->SetGenerateOverlapEvents(false);
                box->SetCanEverAffectNavigation(false);
                box->SetHiddenInGame(true);
                box->RegisterComponent();
                box->SetWorldLocation(center);
            }

        }

    }

    if (this->prepared.entity == 1)
    {
        this->portalActor = world->SpawnActor<AActor>();
        if (!this->portalActor)
        {
            Fail(TEXT("Could not create portal presentation"));
            return;
        }

        this->portalLabel = NewObject<UTextRenderComponent>(this->portalActor);
        this->portalActor->SetRootComponent(this->portalLabel);
        this->portalActor->AddInstanceComponent(this->portalLabel);
#if PLATFORM_ANDROID
        this->portalLabel->SetText(NSLOCTEXT("WorldControl", "MobilePortal", "Portal / Return"));
#else
        this->portalLabel->SetText(FText::FromString(TEXT("[ E ] Portal / Return")));
#endif
        this->portalLabel->SetWorldSize(36);
        this->portalLabel->SetHorizontalAlignment(EHTA_Center);
        this->portalLabel->SetTextRenderColor(FColor::Cyan);
        this->portalLabel->RegisterComponent();
        const auto POINT = this->map->content->manifest.spawns.front().feet;
        this->portalActor->SetActorLocation(FVector(POINT.x, POINT.y, POINT.z + 300) - FVector(world->OriginLocation));
    }

    if (this->map->content->chunks)
    {
        if (!world->GetWorldPartition())
        {
            Fail(TEXT("Chunked world requires World Partition"));
            return;
        }

        this->streamingActor = world->SpawnActor<AActor>();
        if (!this->streamingActor)
        {
            Fail(TEXT("Could not create streaming source"));
            return;
        }

        auto* root = NewObject<USceneComponent>(this->streamingActor);
        this->streamingActor->SetRootComponent(root);
        this->streamingActor->AddInstanceComponent(root);
        root->RegisterComponent();
        const auto SPAWN = this->map->content->manifest.spawns.front().feet;
        this->streamingActor->SetActorLocation(FVector(SPAWN.x, SPAWN.y, SPAWN.z) - FVector(world->OriginLocation));
        this->streamingSource = NewObject<UWorldPartitionStreamingSourceComponent>(this->streamingActor);
        this->streamingActor->AddInstanceComponent(this->streamingSource);
        this->streamingSource->TargetState = EStreamingSourceTargetState::Activated;
        // Preserve visual grid range and guarantee a wider preload envelope
        // than the local readiness query (including UE source quantization).
        this->streamingSource->Shapes.Add(FStreamingSourceShape());
        FStreamingSourceShape safetyLoad;
        safetyLoad.bUseGridLoadingRange = false;
        safetyLoad.Radius = 6400;
        this->streamingSource->Shapes.Add(safetyLoad);
        this->streamingSource->RegisterComponent();
        this->streamingSource->EnableStreamingSource();
    }

}

bool UGameSessionSubsystem::StreamingReady() const
{
    if (!this->levelLoaded || !this->map)
    {
        return false;
    }

    if (!this->map->content->chunks)
    {
        return true;
    }

    if (!IsValid(this->streamingSource) || this->streamingFrames < 2)
    {
        return false;
    }

    auto* partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
    if (!partition)
    {
        return false;
    }

    // Query local safety, not the moving outer preload rim. UE caches streaming
    // updates by quantized source position; waiting for the entire fresh rim can
    // deadlock movement before the next cached-source update is triggered.
    struct FLocalReadiness final : IWorldPartitionStreamingSourceProvider
    {
        FWorldPartitionStreamingSource source;

        bool GetStreamingSource(FWorldPartitionStreamingSource& out) const override
        {
            out = this->source;
            return true;
        }

    } query;

    if (!this->streamingSource->GetStreamingSource(query.source))
    {
        return false;
    }

    query.source.Shapes.Reset();
    FStreamingSourceShape safety;
    safety.bUseGridLoadingRange = false;
    safety.Radius = 1200;
    query.source.Shapes.Add(safety);
    return partition->IsStreamingCompleted(&query);
}

void UGameSessionSubsystem::Receive(const uint8* data, int32 size)
{
    using namespace shared_world::net;
    try
    {
        const auto MESSAGE = shared_world::net::Message::Decode(data, static_cast<std::size_t>(size));
        if (MESSAGE.kind == Kind::SELECTED)
        {
            if (this->state != 1 || MESSAGE.request != this->request)
            {
                return;
            }

            if (MESSAGE.error)
            {
                Fail(FString::Printf(TEXT("Character entry rejected (%u)"), MESSAGE.error));
                return;
            }

            auto enter = MESSAGE;
            enter.kind = Kind::ENTER;
            this->state = 2;
            this->deadline = FPlatformTime::Seconds() + 15;
            if (!Send(enter))
            {
                Fail(TEXT("Could not request game entry"));
            }

            return;
        }

        if (MESSAGE.kind == Kind::ERROR_VALUE)
        {
            if (this->state)
            {
                Fail(FString::Printf(TEXT("World request rejected (%u)"), MESSAGE.error));
            }

            return;
        }

        if (MESSAGE.kind == Kind::PREPARE)
        {
            if (this->state == 2)
            {
                BeginMapLoad(MESSAGE);
            }
            else if (this->state == 5 && MESSAGE.epoch == this->prepared.epoch &&
                     MESSAGE.request == this->prepared.request + 1)
            {
                ++this->generation;
                ClearPlayers();
                this->map.reset();
                BeginMapLoad(MESSAGE);
            }

            return;
        }

        if (!this->map || MESSAGE.epoch != this->prepared.epoch || MESSAGE.request != this->prepared.request ||
            (this->state != 4 && this->state != 5))
        {
            return;
        }

        if (MESSAGE.kind == Kind::MATCH_STATE)
        {
            if (MESSAGE.room && this->dungeonMatch.room == MESSAGE.room &&
                MESSAGE.revision < this->dungeonMatch.revision)
            {
                return;
            }

            this->dungeonMatch = MESSAGE;
            this->matchReceivedAt = FPlatformTime::Seconds();
            ClearNpcDialogue(false);
            CancelClickMove();
            CancelCombatIntent(true);
            if (MESSAGE.error)
            {
                this->onStatusChanged.Broadcast(TEXT("던전 입장을 예약하지 못했습니다. 준비 상태를 다시 확인하세요."));
            }

            return;
        }

        if (MESSAGE.kind == Kind::PARTY)
        {
            this->entityParties.Add(MESSAGE.entity, MESSAGE.partyId);
            if (!IsHostileEntity(this->combatTarget))
            {
                this->combatTarget = 0;
            }

            return;
        }

        if (ReceiveEconomy(MESSAGE) || ReceiveCombat(MESSAGE))
        {
            return;
        }

        if (MESSAGE.kind == Kind::DUNGEON_STATE)
        {
            if (this->prepared.map.id != 301 || this->prepared.map.version < 2 ||
                (this->map->dungeonRun && this->map->dungeonRun != MESSAGE.dungeonRun))
            {
                Fail(TEXT("Dungeon state mismatch"));
                return;
            }

            const bool B_WAS_BOSS = this->map->activeRoom == 6;
            this->map->dungeonRun = MESSAGE.dungeonRun;
            this->map->activeRoom = MESSAGE.activeRoom;
            if (!B_WAS_BOSS && this->map->activeRoom == 6 && shared_world::dungeon::RoomAt(this->predicted) == 6)
            {
                UpdateBossCamera(true);
            }

            GetWorld()->GetSubsystem<UWorldExitPool>()->PresentDoors(this->map->activeRoom);
            this->onStatusChanged.Broadcast(
                this->map->activeRoom
                    ? TEXT("몬스터가 나타났습니다. 입구는 열려 있습니다. 전투를 마치면 다음 방을 진행하세요.")
                    : TEXT("방에 들어가면 몬스터가 나타납니다. 던전을 탐험하세요."));
            return;
        }

        if (MESSAGE.kind == Kind::EXIT_LAYOUT)
        {
            if (MESSAGE.instance != this->prepared.instance)
            {
                Fail(TEXT("Dungeon exit instance mismatch"));
                return;
            }

            if (this->dungeonExits.run)
            {
                if (this->dungeonExits.run != MESSAGE.exits.run)
                {
                    Fail(TEXT("Unexpected dungeon run change"));
                    return;
                }

            }

            this->dungeonExits = MESSAGE.exits;
            if (!GetWorld()->GetSubsystem<UWorldExitPool>()->Present(this->dungeonExits))
            {
                Fail(TEXT("Could not prepare dungeon exits"));
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("DUNGEON_EXITS layout instance=%llu run=%llu count=%u"), MESSAGE.instance,
                   this->dungeonExits.run, this->dungeonExits.count);
            return;
        }

        if (MESSAGE.kind == Kind::EXIT_RESULT)
        {
            const auto& receipt = MESSAGE.exitReceipt;
            if (!this->pendingExit.Valid() || receipt.sourceInstance != this->pendingExit.sourceInstance ||
                receipt.run != this->pendingExit.run || receipt.exitId != this->pendingExit.exitId ||
                receipt.sequence != this->pendingExit.sequence)
            {
                return;
            }

            if (MESSAGE.error)
            {
                this->pendingExit = {};
                this->exitWarping = false;
                this->exitDeadline = 0;
                static const TCHAR* reasons[] = {TEXT(""),
                                                 TEXT("No dungeon exit is available."),
                                                 TEXT("This dungeon run has ended."),
                                                 TEXT("Unknown exit."),
                                                 TEXT("Step inside the exit zone and press E."),
                                                 TEXT("Town return unavailable. Try again.")};
                this->onStatusChanged.Broadcast(reasons[MESSAGE.error]);
                return;
            }

            if (this->state != 5 || this->prepared.instance == receipt.sourceInstance ||
                receipt.type != this->pendingExit.type || receipt.outcome != this->pendingExit.outcome)
            {
                Fail(TEXT("Dungeon exit result mismatch"));
                return;
            }

            this->lastDungeonExitType = static_cast<int32>(receipt.type);
            this->lastDungeonExitOutcome = static_cast<int32>(receipt.outcome);
            this->pendingExit = {};
            this->exitWarping = false;
            this->exitDeadline = 0;
            this->onDungeonExited.Broadcast(this->lastDungeonExitType, this->lastDungeonExitOutcome);
            this->onStatusChanged.Broadcast(FString::Printf(TEXT("Returned to town through %s."),
                                                            UTF8_TO_TCHAR(shared_world::dungeon::Name(receipt.type))));
            UE_LOG(LogTemp, Display, TEXT("DUNGEON_EXITS returned type=%d outcome=%d"), this->lastDungeonExitType,
                   this->lastDungeonExitOutcome);
            return;
        }

        if (MESSAGE.kind == Kind::NPC_DIALOGUE || MESSAGE.kind == Kind::NPC_RESULT)
        {
            if (this->state != 5 || MESSAGE.entity != this->currentNpc || MESSAGE.sequence != this->npcSequence)
            {
                return;
            }

            if (MESSAGE.kind == Kind::NPC_RESULT)
            {
                UE_LOG(LogTemp, Display, TEXT("WORLD_NPC result=%u"), MESSAGE.error);
                ClearNpcDialogue(false);
                static const TCHAR* reasons[] = {TEXT(""),
                                                 TEXT("NPC unavailable."),
                                                 TEXT("Move closer to the NPC."),
                                                 TEXT("Dialogue expired. Talk again."),
                                                 TEXT("Dungeon unavailable or full. Try again."),
                                                 TEXT("This option is unavailable."),
                                                 TEXT("Please wait before talking again.")};
                this->onStatusChanged.Broadcast(reasons[MESSAGE.error]);
                return;
            }

            if (!this->npcAwaitingTalk)
            {
                return;
            }

            auto* pawn = this->npcs.Find(this->currentNpc);
            if (!pawn || !IsValid(*pawn))
            {
                CloseNpcDialogue();
                return;
            }

            this->npcAwaitingTalk = false;
            this->dialogueId = MESSAGE.interaction;
            this->npcDeadline = FPlatformTime::Seconds() + 30;
            if (this->controlHud)
            {
                this->controlHud->ShowNpcDialogue(
                    UTF8_TO_TCHAR(this->map->content->npcs->definitions.at((*pawn)->npcIndex).name.c_str()),
                    UTF8_TO_TCHAR(MESSAGE.text.c_str()), UTF8_TO_TCHAR(MESSAGE.optionLabel.c_str()));
            }

            this->onStatusChanged.Broadcast(TEXT("Choose a destination or close the dialogue."));
            UE_LOG(LogTemp, Display, TEXT("WORLD_NPC dialogue target_map=%u"), MESSAGE.targetMap);
            return;
        }

        if (MESSAGE.kind == Kind::DESPAWN)
        {
            this->entityParties.Remove(MESSAGE.entity);
#if !UE_BUILD_SHIPPING
            if (this->smoke && this->players.Contains(MESSAGE.entity))
            {
                this->smokeDespawn = true;
                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE remote_despawn"));
            }

#endif
            auto* pool = GetWorld()->GetSubsystem<UWorldEntityPool>();
            if (auto* found = this->players.Find(MESSAGE.entity))
            {
                pool->Release(*found);
                this->players.Remove(MESSAGE.entity);
            }

            if (auto* found = this->npcs.Find(MESSAGE.entity))
            {
                pool->Release(*found);
                this->npcs.Remove(MESSAGE.entity);
            }

            this->remoteTargets.Remove(MESSAGE.entity);
            return;
        }

        if (MESSAGE.kind == Kind::ENTITY_STATS)
        {
            auto* found = (MESSAGE.entity & shared_world::npc::ENTITY_BIT) ? this->npcs.Find(MESSAGE.entity)
                                                                           : this->players.Find(MESSAGE.entity);
            if (!found || !IsValid(*found))
            {
                return;
            }

            if ((MESSAGE.entity & shared_world::npc::ENTITY_BIT) &&
                MESSAGE.stats.CanDie() != ((*found)->npcRole == static_cast<int32>(shared_world::npc::Role::GUARD)))
            {
                Fail(TEXT("Invalid NPC stat policy"));
                return;
            }

#if !UE_BUILD_SHIPPING
            if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokePotion")) &&
                MESSAGE.entity == this->prepared.epoch && !(*found)->dead &&
                MESSAGE.stats.hp > static_cast<uint32>((*found)->hp))
            {
                UE_LOG(LogTemp, Display, TEXT("POTION_SMOKE authoritative_heal before=%d after=%u max=%u"),
                       (*found)->hp, MESSAGE.stats.hp, MESSAGE.stats.maxHp);
            }

            if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeHud")) &&
                MESSAGE.entity == this->prepared.epoch && !(*found)->dead &&
                MESSAGE.stats.mp > static_cast<uint32>((*found)->mp))
            {
                UE_LOG(LogTemp, Display, TEXT("MANA_SMOKE authoritative_restore before=%d after=%u max=%u"),
                       (*found)->mp, MESSAGE.stats.mp, MESSAGE.stats.maxMp);
            }

#endif
            (*found)->ApplyServerStats(MESSAGE.stats);
            if (MESSAGE.entity == this->prepared.epoch)
            {
                CancelUnaffordableSkill();
            }

            if (MESSAGE.entity == this->prepared.epoch && this->controlHud)
            {
                this->controlHud->ShowStats(MESSAGE.stats);
            }

            return;
        }

        if (MESSAGE.kind == Kind::NPC_SPAWN)
        {
            const auto& catalog = this->map->content->npcs;
            if (!catalog || catalog->checksum != MESSAGE.catalogChecksum)
            {
                Fail(TEXT("NPC data build mismatch"));
                return;
            }

            const auto DEFINITION = catalog->definitions.find(MESSAGE.npcIndex);
            if (DEFINITION == catalog->definitions.end() || DEFINITION->second.appearance != MESSAGE.appearance ||
                DEFINITION->second.role != MESSAGE.role || DEFINITION->second.roleData != MESSAGE.roleData ||
                DEFINITION->second.name != MESSAGE.name)
            {
                Fail(TEXT("Invalid server NPC definition"));
                return;
            }

            const auto& base = DEFINITION->second.stats;
            if (MESSAGE.stats.maxHp != base.maxHp || MESSAGE.stats.maxMp != base.maxMp ||
                MESSAGE.stats.attack != base.attack || MESSAGE.stats.defense != base.defense ||
                MESSAGE.stats.evasion != base.evasion || MESSAGE.stats.accuracy != base.accuracy ||
                MESSAGE.stats.flags != base.flags)
            {
                Fail(TEXT("Invalid server NPC base stats"));
                return;
            }

            const auto PLACEMENT = std::find_if(
                catalog->placements.begin(), catalog->placements.end(),
                [&](const auto& p)
                {
                    return p.Entity() == MESSAGE.entity && p.map == this->prepared.map.id &&
                           p.npc == MESSAGE.npcIndex &&
                           (DEFINITION->second.combatType == 1 || (p.feet == MESSAGE.feet && p.yaw == MESSAGE.yaw));
                });

            if (PLACEMENT == catalog->placements.end())
            {
                Fail(TEXT("Invalid server NPC placement"));
                return;
            }

            if (this->npcs.Contains(MESSAGE.entity))
            {
                return;
            }

            if (this->npcs.Num() >= static_cast<int32>(shared_world::npc::MAX_PER_MAP))
            {
                Fail(TEXT("NPC limit exceeded"));
                return;
            }

            auto* pawn = GetWorld()->GetSubsystem<UWorldEntityPool>()->Acquire(
                FString::Printf(TEXT("Npc:%u"), MESSAGE.appearance));
            if (!pawn)
            {
                Fail(TEXT("Could not acquire NPC presentation"));
                return;
            }

            this->npcs.Add(MESSAGE.entity, pawn);
            pawn->npcIndex = MESSAGE.npcIndex;
            pawn->npcRole = MESSAGE.role;
            pawn->npcRoleData = MESSAGE.roleData;
            pawn->ApplyServerStats(MESSAGE.stats);
            pawn->SetDisplayName(UTF8_TO_TCHAR(MESSAGE.name.c_str()));
            pawn->SetActorLocation(FVector(MESSAGE.feet.x, MESSAGE.feet.y, MESSAGE.feet.z) -
                                   FVector(GetWorld()->OriginLocation));
            pawn->SetActorRotation(FRotator(0, MESSAGE.yaw / 1000.0, 0));
            pawn->LoadNpcAppearance(catalog->appearances.at(MESSAGE.appearance));
            if (const auto BODY = this->map->content->combat->monsters.find(MESSAGE.npcIndex);
                BODY != this->map->content->combat->monsters.end())
            {
                pawn->SetCombatBodyHeight(BODY->second.bodyHeight);
            }

            if (const auto VISUAL = this->map->content->combat->visuals.find(MESSAGE.appearance);
                VISUAL != this->map->content->combat->visuals.end())
            {
                pawn->ConfigureCombatVisual(VISUAL->second);
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_NPC spawn index=%u appearance=%u role=%u feet=%d,%d,%d"),
                   MESSAGE.npcIndex, MESSAGE.appearance, MESSAGE.role, MESSAGE.feet.x, MESSAGE.feet.y, MESSAGE.feet.z);
            return;
        }

        if (MESSAGE.kind == Kind::SPAWN)
        {
            if (MESSAGE.entity == this->prepared.epoch)
            {
                this->economyCharacter = MESSAGE.character;
                if (this->pendingEconomyCharacter && this->pendingEconomyCharacter != this->economyCharacter)
                {
                    this->economyUnknown = false;
                    this->pendingEconomy = {};
                }

            }

            if (this->players.Contains(MESSAGE.entity))
            {
                return;
            }

            if (this->players.Num() >= 32)
            {
                Fail(TEXT("World entity limit exceeded"));
                return;
            }

            auto* pawn = GetWorld()->GetSubsystem<UWorldEntityPool>()->Acquire(
                FString::Printf(TEXT("Player:%u:%u"), MESSAGE.race, MESSAGE.gender));
            if (!pawn)
            {
                Fail(TEXT("Could not create player presentation"));
                return;
            }

            this->players.Add(MESSAGE.entity, pawn);
            pawn->SetDisplayName(UTF8_TO_TCHAR(MESSAGE.name.c_str()));
            pawn->ApplyServerStats(MESSAGE.stats);
            if (MESSAGE.entity == this->prepared.epoch)
            {
                const auto* characters = GetGameInstance()->GetSubsystem<UCharacterManager>();
                const auto* summary = characters->GetCharacters().FindByPredicate(
                    [&](const FCharacterSummary& item)
                    {
                        return item.characterUnique == MESSAGE.character;
                    });

                if (summary && !summary->equipment.IsEmpty())
                {
                    pawn->AwaitInitialEquipment();
                }

            }

            pawn->LoadAppearance(MESSAGE.race, MESSAGE.gender);
            pawn->ConfigureCombatVisual(this->map->content->combat->visuals.at(0));
#if !UE_BUILD_SHIPPING
            if (this->smoke && MESSAGE.entity != this->prepared.epoch)
            {
                this->smokeRemote = true;
                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE remote_spawn"));
            }

#endif
            pawn->SetActorLocation(FVector(MESSAGE.feet.x, MESSAGE.feet.y, MESSAGE.feet.z) -
                                   FVector(GetWorld()->OriginLocation));
            if (MESSAGE.entity == this->prepared.epoch)
            {
                this->predicted = MESSAGE.feet;
                this->state = 5;
                this->accumulator = 0;
                this->presentationDeadline = FPlatformTime::Seconds() + 300;
#if !UE_BUILD_SHIPPING
                if (this->smoke)
                {
                    this->smokeReady = FPlatformTime::Seconds();
                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE own_spawn"));
                }

                if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeHub")))
                {
                    if (this->prepared.map.id != 201 || this->prepared.map.version != 11 ||
                        this->predicted != shared_world::WorldPosition{-1000, 0, 65})
                    {
                        FPlatformMisc::RequestExitWithStatus(false, 1);
                        return;
                    }

                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE hub_central_spawn"));
                }

#endif
                if (auto* pc = GetGameInstance()->GetFirstLocalPlayerController())
                {
                    if (APawn* old = pc->GetPawn(); old && old != pawn)
                    {
                        old->Destroy();
                    }

                    pc->Possess(pawn);
                    pc->SetViewTarget(pawn);
                    pawn->SetLocalVisibility(true);
                    FInputModeGameAndUI mode;
                    mode.SetHideCursorDuringCapture(false);
                    pc->SetInputMode(mode);
                    pc->bShowMouseCursor = true;
                    this->controlHud = CreateWidget<UWorldControlHUD>(pc, UWorldControlHUD::StaticClass());
                    if (this->controlHud)
                    {
                        this->controlHud->AddToViewport();
                        this->controlHud->ShowStats(MESSAGE.stats);
                    }

                }

                this->onStatusChanged.Broadcast(TEXT(
                    "World ready. LMB: move / attack / selected skill. RMB drag: camera. 1-0: skills. K: skill book."));
            }

        }

        if (MESSAGE.kind == Kind::STATE || MESSAGE.kind == Kind::SPAWN)
        {
            auto* found = (MESSAGE.entity & shared_world::npc::ENTITY_BIT) ? this->npcs.Find(MESSAGE.entity)
                                                                           : this->players.Find(MESSAGE.entity);
            if (!found || !IsValid(*found))
            {
                return;
            }

            if (MESSAGE.entity == this->prepared.epoch)
            {
                if (MESSAGE.sequence > this->nextSequence)
                {
                    Fail(TEXT("Invalid movement acknowledgement"));
                    return;
                }

#if !UE_BUILD_SHIPPING
                const auto PREVIOUS_PREDICTION = this->predicted;
#endif
                shared_world::ReconcilePrediction(this->pendingInputs, this->pendingPositions, MESSAGE.sequence,
                                                  MESSAGE.feet, this->predicted,
                                                  [&](auto position, int dx, int dy)
                                                  {
                                                      return PredictBodyMove(position, dx, dy);
                                                  });

#if !UE_BUILD_SHIPPING
                if (this->smoke && this->smokeInjected && !this->smokeCorrected &&
                    FMath::Abs(PREVIOUS_PREDICTION.y - this->predicted.y) >= 100)
                {
                    this->smokeCorrected = true;
                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE prediction_corrected"));
                }

#endif
                (*found)->SetActorLocation(FVector(this->predicted.x, this->predicted.y, this->predicted.z) -
                                           FVector(GetWorld()->OriginLocation));
            }
            else
            {
                this->remoteTargets.Add(MESSAGE.entity, FVector(MESSAGE.feet.x, MESSAGE.feet.y, MESSAGE.feet.z) -
                                                            FVector(GetWorld()->OriginLocation));
#if !UE_BUILD_SHIPPING
                if (this->smoke && MESSAGE.sequence > 0 && !this->smokeMoved)
                {
                    this->smokeMoved = true;
                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE remote_movement"));
                }

#endif
            }

            (*found)->SetActorRotation(FRotator(0, MESSAGE.yaw / 1000.0, 0));
        }

    }

    catch (const std::exception&)
    {
        Fail(TEXT("Invalid world protocol response"));
    }

}

bool UGameSessionSubsystem::Tick(float delta)
{
#if !UE_BUILD_SHIPPING
    if (this->smoke)
    {
        TickSmoke();
    }

#endif
    if (!this->state)
    {
        return true;
    }

    auto* network = GetGameInstance()->GetSubsystem<UNetworkManager>();
    if (!network || network->GetConnectionState() != ENetworkConnectionState::CONNECTED)
    {
        Fail(TEXT("World connection lost"), false);
        return true;
    }

    if (this->map && this->levelLoaded)
    {
        ++this->streamingFrames;
        const auto FEET = this->state == 5 ? this->predicted : this->map->content->manifest.spawns.front().feet;
        this->map->residency->BeginFrame({FEET});
        if (IsValid(this->streamingActor))
        {
            this->streamingActor->SetActorLocation(FVector(FEET.x, FEET.y, FEET.z) -
                                                   FVector(GetWorld()->OriginLocation));
        }

        if (this->map->residency->Failed(FEET))
        {
            Fail(TEXT("Required world chunk failed validation"));
            return true;
        }

        if (this->state == 3 && StreamingReady())
        {
            auto ready = this->prepared;
            ready.kind = shared_world::net::Kind::READY;
            this->state = 4;
            this->deadline = FPlatformTime::Seconds() + 15;
            if (this->loadingScreen)
            {
                this->loadingScreen->Update(.7f, TEXT("캐릭터를 준비하고 있습니다"));
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_STREAM ready map=%u"), this->prepared.map.id);
            if (!Send(ready))
            {
                Fail(TEXT("Could not confirm map readiness"));
                return true;
            }

        }

    }

    if (this->state != 5)
    {
        if (FPlatformTime::Seconds() > this->deadline)
        {
            Fail(TEXT("World entry timed out"));
        }

        return true;
    }

    TickEconomy();
    if (this->exitWarping && FPlatformTime::Seconds() > this->exitDeadline)
    {
        Fail(TEXT("Dungeon exit confirmation timed out. Reconnect to confirm your location."));
        return true;
    }

    if (this->currentNpc && FPlatformTime::Seconds() > this->npcDeadline)
    {
        if (this->npcWarping)
        {
            Fail(TEXT("Travel confirmation timed out. Reconnect to confirm your location."));
            return true;
        }

        CloseNpcDialogue();
        this->onStatusChanged.Broadcast(TEXT("Dialogue timed out. Talk to the NPC again."));
    }

    for (const auto& entry : this->players)
    {
        if (IsValid(entry.Value) && entry.Value->AppearanceFailed())
        {
            Fail(TEXT("Player appearance assets are unavailable"));
            return true;
        }

    }

    for (const auto& entry : this->npcs)
    {
        if (IsValid(entry.Value) && entry.Value->AppearanceFailed())
        {
            Fail(TEXT("NPC appearance assets are unavailable"));
            return true;
        }

    }

    if (TickLoadingScreen(delta))
    {
        return true;
    }

    TickAutoPotions();
    if (auto* controller = GetGameInstance()->GetFirstLocalPlayerController();
        controller && controller->PlayerCameraManager)
    {
        for (const auto& entry : this->players)
        {
            if (IsValid(entry.Value))
            {
                entry.Value->FaceLabel(controller->PlayerCameraManager->GetCameraLocation());
            }

        }

        for (const auto& entry : this->npcs)
        {
            if (IsValid(entry.Value))
            {
                entry.Value->FaceLabel(controller->PlayerCameraManager->GetCameraLocation());
            }

        }

        if (IsValid(this->portalActor))
        {
            this->portalActor->SetActorRotation(
                (controller->PlayerCameraManager->GetCameraLocation() - this->portalActor->GetActorLocation())
                    .Rotation());
        }

        GetWorld()->GetSubsystem<UWorldExitPool>()->FaceLabels(controller->PlayerCameraManager->GetCameraLocation());
        if (controller->WasInputKeyJustPressed(EKeys::E))
        {
            Interact();
        }

        if (controller->WasInputKeyJustPressed(EKeys::I))
        {
            ToggleInventory();
        }

    }

    for (const auto& target : this->remoteTargets)
    {
        if (auto* pawn = (target.Key & shared_world::npc::ENTITY_BIT) ? this->npcs.Find(target.Key)
                                                                      : this->players.Find(target.Key);
            pawn && IsValid(*pawn))
        {
            (*pawn)->SetActorLocation(FMath::VInterpTo((*pawn)->GetActorLocation(), target.Value, delta, 12));
        }

    }

    TickControl(delta);
    this->accumulator = FMath::Min(this->accumulator + delta, 0.1);
    if (this->accumulator < 1.0 / 30.0 || !this->map)
    {
        return true;
    }

    this->accumulator -= 1.0 / 30.0;
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!pc)
    {
        return true;
    }

    if (this->controlHud && this->controlHud->IsSkillBookVisible())
    {
        return true;
    }

    int x = int(pc->IsInputKeyDown(EKeys::W)) - int(pc->IsInputKeyDown(EKeys::S));
    int y = int(pc->IsInputKeyDown(EKeys::D)) - int(pc->IsInputKeyDown(EKeys::A));
#if !UE_BUILD_SHIPPING
    if (this->smoke && this->smokeRemote && !this->controlSmoke)
    {
        // Separate the two test players before approaching the same wall.
        const bool OFFSET = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeOffset"));
        y = OFFSET && this->predicted.y < -2800 ? 1 : 0;
        x = y ? 0 : 1;
        if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeTerrain")) && this->smokeCorrected)
        {
            static const FIntPoint route[] = {
                {-2500, -3000}, {-2500, 1500}, {-2300, 1500}, {-1000, 1500}, {-2300, 1500},  {-2500, 1500},
                {-2500, 800},   {0, 800},      {0, 2200},     {0, 800},      {2600, 800},    {2600, 1500},
                {4100, 1500},   {2600, 1500},  {2600, 800},   {2600, -3500}, {-3000, -3500}, {-3000, -3000}};
            if (this->smokeTerrainStage < UE_ARRAY_COUNT(route))
            {
                const auto TARGET = route[this->smokeTerrainStage];
                x = FMath::Sign(TARGET.X - this->predicted.x);
                y = x ? 0 : FMath::Sign(TARGET.Y - this->predicted.y);
                if (!x && !y)
                {
                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE terrain_stage=%d z=%d"), this->smokeTerrainStage,
                           this->predicted.z);
                    if (!OFFSET && (this->smokeTerrainStage == 3 || this->smokeTerrainStage == 12))
                    {
                        FString screenshot;
                        if (FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot))
                        {
                            const FString SUFFIX =
                                this->smokeTerrainStage == 3 ? TEXT("-bridge.png") : TEXT("-ramp.png");
                            FScreenshotRequest::RequestScreenshot(
                                FPaths::Combine(FPaths::GetPath(screenshot),
                                                FPaths::GetBaseFilename(screenshot) + SUFFIX),
                                false, false);
                        }

                    }

                    ++this->smokeTerrainStage;
                    if (this->smokeTerrainStage == UE_ARRAY_COUNT(route))
                    {
                        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE terrain_passed checks=%d"), this->smokeGroundChecks);
                    }

                }

                if (this->nextSequence > 8)
                {
                    const FVector FOOT = FVector(this->predicted.x, this->predicted.y, this->predicted.z) -
                                         FVector(GetWorld()->OriginLocation);
                    // Compare the same body footprint used by MoveOnGround.
                    // A zero-width ray exactly on a Chaos stair edge may miss;
                    // the body still has support on both adjacent treads.
                    int32 hits = 0;
                    double highest = -DBL_MAX;
                    for (int32 sampleX : {-34, 0, 34})
                    {
                        for (int32 sampleY : {-34, 0, 34})
                        {
                            FHitResult hit;
                            const FVector SAMPLE = FOOT + FVector(sampleX, sampleY, 0);
                            if (GetWorld()->LineTraceSingleByChannel(hit, SAMPLE + FVector(0, 0, 5),
                                                                     SAMPLE - FVector(0, 0, 60), ECC_Pawn))
                            {
                                ++hits;
                                highest = FMath::Max(highest, hit.ImpactPoint.Z);
                            }

                        }

                    }

                    if (!hits || FMath::Abs(highest - FOOT.Z) > 3)
                    {
                        UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE terrain_mismatch stage=%d feet=%d,%d,%d"),
                               this->smokeTerrainStage, this->predicted.x, this->predicted.y, this->predicted.z);
                        UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE footprint hits=%d highest=%.3f"), hits, highest);
                        FPlatformMisc::RequestExitWithStatus(false, 1);
                        return true;
                    }

                    ++this->smokeGroundChecks;
                }

            }

        }

    }

#endif
#if !UE_BUILD_SHIPPING
    if (this->smoke && this->smokeRemote && this->smokeCorrected &&
        FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokePartition")))
    {
        const bool OFFSET = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeOffset"));
        const int LANE = OFFSET ? 700 : 500, WALL_LANE = OFFSET ? -2800 : -3000;
        const FIntPoint route[] = {{-3000, LANE}, {30000, LANE}, {-3000, LANE}, {-3000, WALL_LANE}, {-45, WALL_LANE}};
        x = y = 0;
        if (this->smokeTerrainStage < UE_ARRAY_COUNT(route))
        {
            const auto TARGET = route[this->smokeTerrainStage];
            x = FMath::Sign(TARGET.X - this->predicted.x);
            y = x ? 0 : FMath::Sign(TARGET.Y - this->predicted.y);
            if (!x && !y)
            {
                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE partition_stage=%d x=%d y=%d z=%d"), this->smokeTerrainStage,
                       this->predicted.x, this->predicted.y, this->predicted.z);
                ++this->smokeTerrainStage;
                if (this->smokeTerrainStage == UE_ARRAY_COUNT(route))
                {
                    if (this->map->residency->Loads() < 3)
                    {
                        FPlatformMisc::RequestExitWithStatus(false, 1);
                        return true;
                    }

                    UE_LOG(LogTemp, Display,
                           TEXT("WORLD_SMOKE partition_passed loads=%llu resident=%llu bytes=%llu ground=%d"),
                           this->map->residency->Loads(), static_cast<uint64>(this->map->residency->ResidentCount()),
                           static_cast<uint64>(this->map->residency->ResidentBytes()), this->smokeGroundChecks);
                }

            }

            if (StreamingReady())
            {
                const FVector FOOT = FVector(this->predicted.x, this->predicted.y, this->predicted.z) -
                                     FVector(GetWorld()->OriginLocation);
                FHitResult hit;
                if (!GetWorld()->LineTraceSingleByChannel(hit, FOOT + FVector(0, 0, 5), FOOT - FVector(0, 0, 60),
                                                          ECC_Pawn) ||
                    FMath::Abs(hit.ImpactPoint.Z - FOOT.Z) > 3)
                {
                    UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE partition_ground_mismatch"));
                    FPlatformMisc::RequestExitWithStatus(false, 1);
                    return true;
                }

                ++this->smokeGroundChecks;
            }

        }

    }

#endif
#if !UE_BUILD_SHIPPING
    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeTransition")))
    {
        x = y = 0;
    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeNpc")))
    {
        x = y = 0;
    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeHub")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeStairs")))
    {
        x = y = 0;
        if (this->smokeRemote)
        {
            const FIntPoint route[] = {{-1000, -1000}, {1000, -1000}, {1000, 0}};
            if (this->smokeTerrainStage < UE_ARRAY_COUNT(route))
            {
                const auto TARGET = route[this->smokeTerrainStage];
                x = FMath::Sign(TARGET.X - this->predicted.x);
                y = x ? 0 : FMath::Sign(TARGET.Y - this->predicted.y);
                if (!x && !y)
                {
                    UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE hub_route_stage=%d feet=%d,%d,%d"),
                           this->smokeTerrainStage, this->predicted.x, this->predicted.y, this->predicted.z);
                    if (this->smokeTerrainStage == 0)
                    {
                        CancelClickMove();
                    }

                    ++this->smokeTerrainStage;
                    this->smokeProgressTime = FPlatformTime::Seconds();
                }

            }
            // Keep pressing into the statue, then let all server acknowledgements drain.
            else if (FPlatformTime::Seconds() - this->smokeProgressTime < 10)
            {
                x = -1;
            }

        }

    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClicks")))
    {
        x = y = 0;
    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeStairs")))
    {
        x = y = 0;
        if (this->smokeRemote && this->smokeTerrainStage == 0)
        {
            x = FMath::Sign(-6000 - this->predicted.x);
        }

        if (this->smokeTerrainStage == 3)
        {
            x = FMath::Sign(-2600 - this->predicted.x);
        }

    }

#endif
    if (this->currentNpc || this->exitWarping || InventoryVisible() || this->shopOpening)
    {
        return true;
    }

    if (const auto OWN = this->players.Find(this->prepared.epoch); OWN && IsValid(*OWN) && (*OWN)->dead)
    {
        return true;
    }

    const int STEP = x && y ? 14 : 20;
    shared_world::WorldPosition requestedStep{x * STEP, y * STEP, 0};
#if !UE_BUILD_SHIPPING
    if (!this->smoke || this->controlSmoke)
    {
#endif
        requestedStep = ControlStep(x, y);
#if !UE_BUILD_SHIPPING
    }

#endif
#if !UE_BUILD_SHIPPING
    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeHub")) && this->smokeTerrainStage == 0 &&
        !FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeStairs")))
    {
        requestedStep = ControlStep(0, 0);
    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeStairs")) && this->smokeTerrainStage == 2)
    {
        requestedStep = ControlStep(0, 0);
    }

    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClicks")))
    {
        requestedStep = ControlStep(0, 0);
    }

#endif
    if ((!requestedStep.x && !requestedStep.y) || this->pendingInputs.size() >= 8)
    {
        return true;
    }

    if (!StreamingReady())
    {
        return true;
    }

    shared_world::net::Message input;
    input.kind = shared_world::net::Kind::MOVE;
    input.dx = requestedStep.x;
    input.dy = requestedStep.y;
    auto predictedMove = this->map->residency->Move(this->predicted, input.dx, input.dy);
    if (predictedMove.status != shared_world::QueryStatus::BUDGET_EXCEEDED)
    {
        predictedMove.position = PredictBodyMove(this->predicted, input.dx, input.dy, &predictedMove.position);
    }

    if (predictedMove.status == shared_world::QueryStatus::BUDGET_EXCEEDED)
    {
        return true;
    }

    if (!this->clickPath.empty() && predictedMove.position == this->predicted)
    {
        if (++this->clickBlockedTicks >= 15)
        {
            CancelClickMove();
            this->onStatusChanged.Broadcast(TEXT("Click movement blocked."));
        }

        return true;
    }

    this->clickBlockedTicks = 0;
    input.epoch = this->prepared.epoch;
    input.request = this->prepared.request;
    input.sequence = ++this->nextSequence;
    input.yaw = static_cast<int32>(
        FMath::RoundToInt(FMath::RadiansToDegrees(FMath::Atan2(double(input.dy), double(input.dx))) * 1000));
    if (!Send(input))
    {
        Fail(TEXT("Movement send failed"));
        return true;
    }

#if !UE_BUILD_SHIPPING
    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeSmoothing")) &&
        this->controlSmokeStage < 3)
    {
        const double NOW = FPlatformTime::Seconds();
        if (this->smokeRouteLastStep > 0)
        {
            this->smokeRouteMaxGap = FMath::Max(this->smokeRouteMaxGap, NOW - this->smokeRouteLastStep);
        }

        this->smokeRouteLastStep = NOW;
        ++this->smokeRouteSteps;
        if (input.dx < 0)
        {
            ++this->smokeRouteBackwards;
        }

        if (this->smokeRouteSteps == 1)
        {
            const auto PLANNED = this->clickWaypoint < this->clickPath.size()
                                     ? shared_world::GroundStep(this->predicted, this->clickPath[this->clickWaypoint])
                                     : shared_world::WorldPosition{};
            if (this->clickPath.empty() || input.dx != PLANNED.x || input.dy != PLANNED.y)
            {
                UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE unplanned_start"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE route_first_input_verified"));
        }

    }

#endif
    this->pendingInputs.push_back(input);
    this->predicted = predictedMove.position;
    this->pendingPositions.push_back(this->predicted);
    if (this->clickFirstMove && !this->clickPath.empty())
    {
        this->clickFirstMove = false;
        UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL click_first_move delay_ms=%.3f route_confirmed=1"),
               (FPlatformTime::Seconds() - this->clickStarted) * 1000);
    }

    if (auto* pawn = this->players.Find(this->prepared.epoch); pawn && IsValid(*pawn))
    {
        (*pawn)->SetActorLocation(FVector(this->predicted.x, this->predicted.y, this->predicted.z) -
                                  FVector(GetWorld()->OriginLocation));
    }

    return true;
}

#if !UE_BUILD_SHIPPING
void UGameSessionSubsystem::TickSmoke()
{
    const double NOW = FPlatformTime::Seconds();
    if (this->state == 5 && this->map && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokePartition")) &&
        NOW - this->smokeProgressTime > 5)
    {
        this->smokeProgressTime = NOW;
        UE_LOG(LogTemp, Display,
               TEXT("WORLD_SMOKE progress feet=%d,%d,%d seq=%llu pending=%llu streaming=%d resident=%llu loads=%llu "
                    "stage=%d"),
               this->predicted.x, this->predicted.y, this->predicted.z, this->nextSequence,
               static_cast<uint64>(this->pendingInputs.size()), StreamingReady(),
               static_cast<uint64>(this->map->residency->ResidentCount()), this->map->residency->Loads(),
               this->smokeTerrainStage);
    }

    if (NOW - this->smokeStarted > 300)
    {
        UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE timeout"));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }

    if (!GetWorld() || !GetWorld()->IsGameWorld() || !GetGameInstance()->GetFirstLocalPlayerController())
    {
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeLobby")))
    {
        TickLobbySmoke();
        return;
    }

    if (!this->smokeConnected)
    {
        this->smokeConnected = true;
        TWeakObjectPtr<UGameSessionSubsystem> weak(this);
        GetGameInstance()->GetSubsystem<UNetworkManager>()->ConnectLoginServer(
            [weak]()
            {
                if (auto* self = weak.Get())
                {
                    self->GetGameInstance()->GetSubsystem<ULoginManager>()->StartLoginPorcess();
                }

            });

    }

    auto* characters = GetGameInstance()->GetSubsystem<UCharacterManager>();
    const bool RECOVERY = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeRecovery"));
    if (RECOVERY && this->state == 3 && !this->smokeRecoveryDropped)
    {
        this->smokeRecoveryDropped = true;
        this->smokeSelected = false;
        GetGameInstance()->GetSubsystem<UNetworkManager>()->SimulateConnectionLossForTest();
        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE loading_connection_closed"));
        return;
    }

    if (!this->smokeSelected &&
        (!RECOVERY || !this->smokeRecoveryDropped ||
         (this->smokeStaleLoadRejected && this->mapLoadBudget->load() == 0)) &&
        characters->HasCharacterList() && !characters->GetCharacters().IsEmpty())
    {
        this->smokeSelected = EnterCharacter(characters->GetCharacters()[0].characterUnique);
    }

    if (this->state == 5 && this->loadingScreen && this->loadingScreen->IsVisible())
    {
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgSkillSmoke")))
    {
        TickSkillSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeMaze")))
    {
        TickMazeSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeAction")))
    {
        TickActionSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeCombat")))
    {
        TickCombatSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeEconomy")))
    {
        TickEconomySmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeNpcWarp")))
    {
        TickNpcWarpSmoke();
        return;
    }

    if (this->controlSmoke)
    {
        TickControlSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeNpc")))
    {
        TickNpcSmoke();
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeStairs")))
    {
        if (this->state != 5)
        {
            return;
        }

        FString screenshot;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
        if (this->smokeTerrainStage == 0 && this->predicted.x == -6000 && this->pendingInputs.empty())
        {
            if (this->predicted.z < 480 || this->predicted.z > 520)
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE stairs_up feet=%d,%d,%d"), this->predicted.x, this->predicted.y,
                   this->predicted.z);
            if (!screenshot.IsEmpty())
            {
                FScreenshotRequest::RequestScreenshot(screenshot, true, false);
            }

            this->smokeTerrainStage = 1;
            this->smokeProgressTime = NOW;
        }

        if (this->smokeTerrainStage == 1 && NOW - this->smokeProgressTime > 2)
        {
            if (!RequestMoveTo(FVector(-4000, 0, 112) - FVector(GetWorld()->OriginLocation)))
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            this->smokeTerrainStage = 2;
        }

        if (this->smokeTerrainStage == 2 && this->predicted.x == -4000 && this->predicted.y == 0 &&
            this->pendingInputs.empty())
        {
            CancelClickMove();
            this->smokeTerrainStage = 3;
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE stairs_click_down"));
        }

        if (this->smokeTerrainStage == 3 && this->predicted.x == -2600 && this->pendingInputs.empty())
        {
            if (this->predicted.z < 90 || this->predicted.z > 125)
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE stairs_ground feet=%d,%d,%d"), this->predicted.x,
                   this->predicted.y, this->predicted.z);
            if (!screenshot.IsEmpty())
            {
                FScreenshotRequest::RequestScreenshot(
                    FPaths::GetPath(screenshot) / FPaths::GetBaseFilename(screenshot) + TEXT("-ground.png"), true,
                    false);
            }

            this->smokeTerrainStage = 4;
            this->smokeProgressTime = NOW;
        }

        if (this->smokeTerrainStage == 4 && NOW - this->smokeProgressTime > 2)
        {
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE stairs_passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeSmoothing")))
    {
        if (this->state != 5 || !this->smokeRemote)
        {
            return;
        }

        if (this->controlSmokeStage == 0 && NOW - this->smokeReady > 2)
        {
            if (!RequestMoveTo(FVector(1000, 0, 66) - FVector(GetWorld()->OriginLocation)))
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            this->controlSmokeStage = 1;
            this->controlSmokeTime = NOW;
        }

        if (this->controlSmokeStage == 1 && !this->clickPath.empty())
        {
            this->controlSmokeStage = 2;
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE smoothing_route_active"));
        }

        if (this->controlSmokeStage == 2 && this->predicted.x == 1000 && this->predicted.y == 0 &&
            this->pendingInputs.empty())
        {
            FString screenshot;
            FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
            if (!screenshot.IsEmpty())
            {
                FScreenshotRequest::RequestScreenshot(screenshot, true, false);
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE route_continuity steps=%d backwards=%d max_gap_ms=%.3f"),
                   this->smokeRouteSteps, this->smokeRouteBackwards, this->smokeRouteMaxGap * 1000);
            if (this->smokeRouteSteps < 50 || this->smokeRouteBackwards || this->smokeRouteMaxGap > .25)
            {
                UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE route_continuity_failed"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE smoothing_arrived"));
            this->controlSmokeStage = 3;
            this->controlSmokeTime = NOW;
        }

        if (this->controlSmokeStage == 3 && NOW - this->controlSmokeTime > 2)
        {
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE smoothing_passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        if (this->controlSmokeStage > 0 && this->controlSmokeStage < 3 && NOW - this->controlSmokeTime > 20)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE smoothing_failed"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }

        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClicks")))
    {
        if (this->state != 5 || !this->smokeRemote)
        {
            return;
        }

        auto failClick = [&](const TCHAR* reason)
        {
            UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE clicks_failed %s"), reason);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        };

        if (this->controlSmokeStage > 0 && NOW - this->controlSmokeTime > 20)
        {
            failClick(TEXT("stage_timeout"));
            return;
        }

        auto click = [&](int32 x)
        {
            auto floor = this->map->content->collision->ProjectToWalkableSurface({x, 0, this->predicted.z}, 300);
            return floor.status == shared_world::QueryStatus::HIT &&
                   RequestMoveTo(FVector(x, 0, floor.position.z) - FVector(GetWorld()->OriginLocation));
        };

        if (this->controlSmokeStage == 0 && NOW - this->smokeReady > 2)
        {
            for (int32 i = 0; i < 10; ++i)
            {
                if (!click(i % 2 ? -600 : -400))
                {
                    failClick(TEXT("request"));
                    return;
                }

            }

            if (this->moveMarkerPool.Num() != 2 || !IsValid(this->activeMoveMarker) ||
                !this->activeMoveMarker->IsActive())
            {
                failClick(TEXT("immediate_marker"));
                return;
            }

            this->controlSmokeStage = 1;
            this->controlSmokeTime = NOW;
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_pending_pool count=%d"), this->moveMarkerPool.Num());
        }
        else if (this->controlSmokeStage == 1 && this->predicted.x > -1000)
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClickDelay")))
            {
                if (this->clickPath.empty())
                {
                    failClick(TEXT("moved_before_route"));
                    return;
                }

                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_predicted_after_route"));
            }

            FString screenshot;
            FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
            if (!screenshot.IsEmpty() && !FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClickDelay")))
            {
                FScreenshotRequest::RequestScreenshot(screenshot, true, false);
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_first_move delay_ms=%.3f"),
                   (NOW - this->controlSmokeTime) * 1000);
            this->controlSmokeStage = 2;
        }

        if (this->controlSmokeStage == 2 && !this->clickPath.empty() && !this->smokeShot &&
            FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClickDelay")))
        {
            // GPU readback can stall the game thread; capture after timing and
            // route-publication assertions, so it cannot swallow the test window.
            FString screenshot;
            FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
            if (!screenshot.IsEmpty())
            {
                FScreenshotRequest::RequestScreenshot(screenshot, true, false);
            }

            this->smokeShot = true;
        }

        if (this->controlSmokeStage == 1 && this->clickPath.empty() && NOW - this->controlSmokeTime > .35 &&
            FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClickDelay")))
        {
            if (this->predicted.x != -1000)
            {
                failClick(TEXT("unplanned_advance"));
                return;
            }

            if (!this->controlMovingShot)
            {
                this->controlMovingShot = true;
                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_no_unplanned_advance"));
            }

        }

        if (this->controlSmokeStage == 2 && this->predicted.x == -600 && this->pendingInputs.empty())
        {
            CancelClickMove();
            for (const auto& marker : this->moveMarkerPool)
            {
                if (marker->IsActive())
                {
                    failClick(TEXT("arrival_marker"));
                    return;
                }

            }

            if (!RequestMoveTo(FVector(0, 0, 55) - FVector(GetWorld()->OriginLocation)))
            {
                failClick(TEXT("blocked_request"));
                return;
            }

            this->controlSmokeStage = 3;
            this->controlSmokeTime = NOW;
        }
        else if (this->controlSmokeStage == 3 && !this->clickPending && !this->clickCancellation &&
                 this->clickPath.empty())
        {
            if (this->predicted.x != -600 || IsValid(this->activeMoveMarker))
            {
                failClick(TEXT("blocked_moved"));
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_blocked"));
            if (!click(-400))
            {
                failClick(TEXT("reuse_request"));
                return;
            }

            this->controlSmokeStage = 4;
        }
        else if (this->controlSmokeStage == 4 && this->predicted.x == -400 && this->pendingInputs.empty())
        {
            if (this->moveMarkerPool.Num() != 2)
            {
                failClick(TEXT("pool_grew"));
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_reused count=%d"), this->moveMarkerPool.Num());
            if (!click(-1800))
            {
                failClick(TEXT("cancel_request"));
                return;
            }

            ControlStep(1, 0);
            this->controlSmokeStage = 5;
            this->controlSmokeTime = NOW;
        }
        else if (this->controlSmokeStage == 5 && NOW - this->controlSmokeTime > .5)
        {
            if (IsValid(this->activeMoveMarker) || !this->clickPath.empty() || this->predicted.x != -400)
            {
                failClick(TEXT("stale_completion"));
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_cancelled"));
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeHub")))
    {
        if (this->state == 5 && this->smokeRemote && this->controlSmokeStage == 0)
        {
            if (!RequestMoveTo(FVector(-1000, -1000, 64) - FVector(GetWorld()->OriginLocation)))
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            this->controlSmokeStage = 1;
            this->controlSmokeTime = NOW;
        }
        else if (this->controlSmokeStage == 1 && this->predicted != shared_world::WorldPosition{-1000, 0, 65})
        {
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE hub_click_started delay_ms=%.3f"),
                   (NOW - this->controlSmokeTime) * 1000);
            this->controlSmokeStage = 2;
        }

        if (this->state == 5 && this->smokeTerrainStage == 3 && this->smokeMoved &&
            NOW - this->smokeProgressTime > 10 && this->pendingInputs.empty())
        {
            if (this->predicted != shared_world::WorldPosition{209, 0, 55})
            {
                UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE hub_barrier_failed"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            if (!this->smokeShot)
            {
                this->smokeShot = true;
                this->smokeShotTime = NOW;
                FString screenshot;
                if (FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot))
                {
                    FScreenshotRequest::RequestScreenshot(screenshot, true, false);
                }

                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE hub_barrier_verified feet=%d,%d,%d"), this->predicted.x,
                       this->predicted.y, this->predicted.z);
            }

            if (NOW - this->smokeShotTime > 2)
            {
                UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE hub_passed"));
                FPlatformMisc::RequestExitWithStatus(false, 0);
            }

        }

        return;
    }

    if (RECOVERY && this->state == 5 && this->smokeRecoveryDropped && this->smokeStaleLoadRejected &&
        this->mapLoadBudget->load() == 1)
    {
        if (!this->smokeRecoveryWorldDropped)
        {
            this->smokeRecoveryWorldDropped = true;
            this->smokeSelected = false;
            GetGameInstance()->GetSubsystem<UNetworkManager>()->SimulateConnectionLossForTest();
            auto* ui = GetGameInstance()->GetSubsystem<UUIManager>();
            if (!ui || !ui->GetUIController() || !ui->GetUIController()->IsInViewport())
            {
                UE_LOG(LogTemp, Error, TEXT("WORLD_SMOKE recovery_ui_missing"));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE recovery_ui_restored"));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE recovery_passed"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeTransition")))
    {
        if (this->state != 5)
        {
            return;
        }

        const auto AGE = NOW - this->smokeReady;
        FString screenshot;
        FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot);
        if (this->prepared.request == 0 && this->smokeTransitionStage == 0 && this->smokeRemote && AGE > 2)
        {
            if (UsePortal())
            {
                this->smokeTransitionStage = 1;
            }

        }
        else if (this->prepared.request == 1 && this->smokeTransitionStage == 1 && AGE > 1)
        {
            if (this->prepared.map.id != 101 || this->players.Num() != 1)
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            FScreenshotRequest::RequestScreenshot(
                FPaths::Combine(FPaths::GetPath(screenshot),
                                FPaths::GetBaseFilename(screenshot) + TEXT("-dungeon.png")),
                false, false);
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE transition_dungeon instance=%llu visit=%u"),
                   this->prepared.instance, this->prepared.request);
            this->smokeTransitionStage = 2;
        }
        else if (this->prepared.request == 1 && this->smokeTransitionStage == 2 && AGE > 3)
        {
            if (UsePortal())
            {
                this->smokeTransitionStage = 3;
            }

        }
        else if (this->prepared.request == 2 && this->smokeTransitionStage == 3 && AGE > 1)
        {
            if (this->prepared.map.id != 201 || this->predicted != shared_world::WorldPosition{-3000, -3000, 0})
            {
                FPlatformMisc::RequestExitWithStatus(false, 1);
                return;
            }

            FScreenshotRequest::RequestScreenshot(screenshot, false, false);
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE transition_return instance=%llu visit=%u"),
                   this->prepared.instance, this->prepared.request);
            this->smokeTransitionStage = 4;
        }
        else if (this->smokeTransitionStage == 4 && AGE > 3)
        {
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE transition_passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

        return;
    }

    if (this->state == 5 && this->smokeRemote && !this->smokeInjected)
    {
        // Test only: corrupt prediction, never the outgoing input/server position.
        this->smokeInjected = true;
        this->predicted.y += 200;
    }

    const bool PARTITION_DONE =
        !FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokePartition")) || this->smokeTerrainStage >= 5;
    if (this->state == 5 && this->smokeRemote && this->smokeMoved && PARTITION_DONE && this->predicted.x >= -46 &&
        this->predicted.x <= -44 && NOW - this->smokeReady > 12)
    {
        if (!this->smokeShot)
        {
            this->smokeShot = true;
            this->smokeShotTime = NOW;
            FString screenshot;
            if (FParse::Value(FCommandLine::Get(), TEXT("RpgWorldSmokeScreenshot="), screenshot))
            {
                FScreenshotRequest::RequestScreenshot(screenshot, false, false);
            }

            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE wall_verified x=%d y=%d z=%d"), this->predicted.x,
                   this->predicted.y, this->predicted.z);
        }

        const bool OBSERVER = FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeOffset"));
        if (NOW - this->smokeReady > 18 && NOW - this->smokeShotTime > 2 && this->smokeCorrected &&
            (!OBSERVER || this->smokeDespawn))
        {
            UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE passed"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }

    }

}

#endif
