#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldMoveMarker.h"
#include "World/WorldControlHUD.h"
#include "GameClientWorld.h"
#include "SharedWorld/Dungeon/DungeonMaze.h"
#include "SharedWorld/Movement/GroundPathSmoothing.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Async/Async.h"

void UGameSessionSubsystem::UpdateBossCamera(bool inside)
{
    auto* pawn = this->players.FindRef(this->prepared.epoch).Get();
    if (!IsValid(pawn))
    {
        return;
    }

    if (inside && this->savedBossCameraDistance == 0)
    {
        // Zero means outside/unvisited; -1 records an existing wide view so a
        // later wheel zoom is never mistaken for first entry into this arena.
        this->savedBossCameraDistance = pawn->GetCameraZoom() < 2000 ? pawn->GetCameraZoom() : -1.f;
        if (this->savedBossCameraDistance > 0)
        {
            pawn->SetCameraZoom(2000);
        }

    }
    else if (!inside && this->savedBossCameraDistance != 0)
    {
        // A wheel adjustment during the encounter belongs to the player.
        if (this->savedBossCameraDistance > 0 && FMath::IsNearlyEqual(pawn->GetCameraZoom(), 2000.f, 1.f))
        {
            pawn->SetCameraZoom(this->savedBossCameraDistance);
        }

        this->savedBossCameraDistance = 0;
    }

}

void UGameSessionSubsystem::CancelClickMove()
{
    ++this->clickRequest;
    if (this->clickCancellation)
    {
        this->clickCancellation->store(true);
    }

    this->clickCancellation.reset();
    this->clickPath.clear();
    this->clickWaypoint = 0;
    this->clickPending = false;
    this->clickBlockedTicks = 0;
    if (IsValid(this->activeMoveMarker))
    {
        this->activeMoveMarker->Release();
    }

    this->activeMoveMarker = nullptr;
    this->clickFirstMove = false;
    this->clickPlanRetries = 0;
}

void UGameSessionSubsystem::PrepareMoveMarkers()
{
    for (int32 i = 0; i < 2; ++i)
    {
        if (auto* marker = GetWorld()->SpawnActor<AWorldMoveMarker>())
        {
            this->moveMarkerPool.Add(marker);
        }

    }

    this->nextMoveMarker = 0;
    UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL marker_pool_ready count=%d"), this->moveMarkerPool.Num());
}

void UGameSessionSubsystem::ShowMoveMarker(FVector location, bool confirmed)
{
    if (!IsValid(this->activeMoveMarker))
    {
        if (this->moveMarkerPool.IsEmpty())
        {
            return;
        }

        this->activeMoveMarker = this->moveMarkerPool[this->nextMoveMarker];
        this->nextMoveMarker = (this->nextMoveMarker + 1) % this->moveMarkerPool.Num();
    }

    if (!IsValid(this->activeMoveMarker))
    {
        return;
    }

    FVector normal = FVector::UpVector;
    FHitResult hit;
    if (GetWorld()->LineTraceSingleByChannel(hit, location + FVector(0, 0, 35), location - FVector(0, 0, 65),
                                             ECC_Pawn) &&
        hit.ImpactNormal.Z > .5)
    {
        location = hit.ImpactPoint;
        normal = hit.ImpactNormal;
    }

    this->activeMoveMarker->Activate(location, normal, confirmed);
    UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL marker_%s slot=%d pool=%d"),
           confirmed ? TEXT("confirmed") : TEXT("pending"),
           (this->nextMoveMarker + this->moveMarkerPool.Num() - 1) % this->moveMarkerPool.Num(),
           this->moveMarkerPool.Num());
}

bool UGameSessionSubsystem::RequestMoveTo(FVector location)
{
    return RequestMoveToInternal(location, false);
}

bool UGameSessionSubsystem::RequestMoveToInternal(FVector location, bool combatRoute)
{
    if (this->state != 5 || !this->map || this->currentNpc || this->exitWarping || location.ContainsNaN())
    {
        return false;
    }

    location += FVector(GetWorld()->OriginLocation);
    if (FMath::Abs(location.X) > shared_world::COORDINATE_LIMIT ||
        FMath::Abs(location.Y) > shared_world::COORDINATE_LIMIT ||
        FMath::Abs(location.Z) > shared_world::COORDINATE_LIMIT)
    {
        return false;
    }

    if (FMath::Abs(location.X - this->predicted.x) > 4000 || FMath::Abs(location.Y - this->predicted.y) > 4000)
    {
        this->onStatusChanged.Broadcast(TEXT("Destination is too far away. Choose a nearer point."));
        return false;
    }

    if (!combatRoute)
    {
        CancelCombatIntent(true);
        this->combatMoveOverride = true;
    }

    CancelClickMove();
    this->clickGoal = {static_cast<int32>(FMath::RoundToInt(location.X)),
                       static_cast<int32>(FMath::RoundToInt(location.Y)),
                       static_cast<int32>(FMath::RoundToInt(location.Z))};
    this->clickPending = true;
    this->clickStarted = FPlatformTime::Seconds();
    this->clickDeadline = this->clickStarted + 3;
    this->clickFirstMove = true;
    this->clickOrigin = this->predicted;
    ShowMoveMarker(location - FVector(GetWorld()->OriginLocation), false);
    this->onStatusChanged.Broadcast(TEXT("Checking the route..."));
    // Start on the input frame rather than waiting for another ticker pass.
    StartClickPath();
    return true;
}

#if PLATFORM_ANDROID
UGameSessionSubsystem::FMobileCameraGesture UGameSessionSubsystem::UpdateMobileTouch(
    const APlayerController& controller, bool worldInputAllowed)
{
    auto& touch = this->mobileTouch;
    FVector2D first, second;
    bool firstDown = false, secondDown = false;
    controller.GetInputTouchState(ETouchIndex::Touch1, first.X, first.Y, firstDown);
    controller.GetInputTouchState(ETouchIndex::Touch2, second.X, second.Y, secondDown);
    const bool FIRST_PRESSED = firstDown || controller.WasInputKeyJustPressed(EKeys::TouchKeys[0]);
    const bool SECOND_PRESSED = secondDown || controller.WasInputKeyJustPressed(EKeys::TouchKeys[1]);
    const auto overHud = [this](const FVector2D& position)
    {
        return this->controlHud && this->controlHud->IsScreenPositionOverHud(position);
    };

    int32 width = 0, height = 0;
    controller.GetViewportSize(width, height);
    const double SHORT_EDGE = FMath::Max(1, FMath::Min(width, height));
    const double TAP_RADIUS = SHORT_EDGE * .025;
    touch.tap = false;
    if (FIRST_PRESSED && !touch.tracking)
    {
        touch.tracking = true;
        touch.start = first;
        touch.consumed |= overHud(first);
    }

    // Once a second finger appears, suppress the tap until both fingers lift.
    if (touch.tracking && (SECOND_PRESSED || !worldInputAllowed ||
                           FVector2D::DistSquared(first, touch.start) > FMath::Square(TAP_RADIUS)))
    {
        touch.consumed = true;
    }

    FMobileCameraGesture camera;
    if (firstDown && secondDown && worldInputAllowed &&
        (touch.cameraDragging || (!overHud(first) && !overHud(second))))
    {
        const FVector2D CENTER = (first + second) * .5;
        const double SPAN = FMath::Max(FVector2D::Distance(first, second), TAP_RADIUS);
        if (touch.cameraDragging)
        {
            camera.drag = (CENTER - touch.cameraCenter) * (80.0 / SHORT_EDGE);
            camera.zoomRatio = static_cast<float>(touch.cameraSpan / SPAN);
        }

        touch.cameraCenter = CENTER;
        touch.cameraSpan = SPAN;
        touch.cameraDragging = true;
    }
    else
    {
#if !UE_BUILD_SHIPPING
        if (touch.cameraDragging)
        {
            if (const auto* pawn = this->players.FindRef(this->prepared.epoch).Get())
            {
                UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL mobile_camera yaw=%.1f zoom=%.1f"),
                       pawn->CameraYaw(), pawn->GetCameraZoom());
            }

        }

#endif
        touch.cameraDragging = false;
    }

    if (!firstDown && !secondDown)
    {
        touch.tap = touch.tracking && !touch.consumed && !overHud(first);
        touch.position = first;
        touch.tracking = false;
        touch.consumed = false;
    }

    return camera;
}

#endif
void UGameSessionSubsystem::TickControl(float)
{
#if !UE_BUILD_SHIPPING
    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeCombat")))
    {
        TickCombatControl(0);
        return;
    }

#endif
#if !UE_BUILD_SHIPPING
    // Keep the test overview camera above the whole arena; normal controls clamp zoom to 1200cm.
    if (this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeNpcWarp")) &&
        (this->controlSmokeStage == 70 || this->controlSmokeStage == 71))
    {
        return;
    }

#endif
    auto* pc = GetGameInstance()->GetFirstLocalPlayerController();
    if (!pc)
    {
        return;
    }

    const bool WORLD_INPUT_BLOCKED = this->currentNpc || this->exitWarping || InventoryVisible() || this->shopOpening;
#if PLATFORM_ANDROID
    const auto TOUCH_CAMERA = UpdateMobileTouch(
        *pc, !WORLD_INPUT_BLOCKED && !this->dungeonMatch.room &&
                 (!this->controlHud || !this->controlHud->IsSkillBookVisible()));
#endif
    PollActionInput();
    if (this->dungeonMatch.room)
    {
        this->rightGestureActive = false;
        this->rightSkillTap = false;
        CancelCombatIntent(true);
        return;
    }

    if (this->controlHud && this->controlHud->IsPointerOverHud())
    {
        this->actionPressedBits &= ~(PRIMARY_INPUT | SECONDARY_INPUT);
        this->actionReleasedBits &= ~(PRIMARY_INPUT | SECONDARY_INPUT);
        this->rightGestureActive = false;
        this->rightSkillTap = false;
    }

    if (WORLD_INPUT_BLOCKED)
    {
        this->rightGestureActive = false;
        this->rightSkillTap = false;
        TickCombatControl(0);
        return;
    }

    this->rightSkillTap = false;
    if (((this->actionPressedBits & SECONDARY_INPUT) != 0))
    {
        this->rightGestureActive = true;
        this->rightCameraDrag = false;
        this->rightClickConsumed = false;
        this->rightDragDistance = 0;
        this->rightGestureX = 0;
        this->rightGestureY = 0;
    }

    float mouseX = 0, mouseY = 0;
    if (pc->IsInputKeyDown(EKeys::MiddleMouseButton))
    {
        pc->GetInputMouseDelta(mouseX, mouseY);
    }
    else if (this->rightGestureActive && pc->IsInputKeyDown(EKeys::RightMouseButton))
    {
        float dx = 0, dy = 0;
        pc->GetInputMouseDelta(dx, dy);
        this->rightDragDistance += FMath::Abs(dx) + FMath::Abs(dy);
        this->rightGestureX += dx;
        this->rightGestureY += dy;
        const bool WAS_DRAGGING = this->rightCameraDrag;
        if (this->rightDragDistance >= 6)
        {
            this->rightCameraDrag = true;
        }

        // Apply the buffered deltas once a drag is confirmed; do not discard its first few pixels.
        if (this->rightCameraDrag)
        {
            mouseX = WAS_DRAGGING ? dx : this->rightGestureX;
            mouseY = WAS_DRAGGING ? dy : this->rightGestureY;
        }

    }

#if PLATFORM_ANDROID
    mouseX += static_cast<float>(TOUCH_CAMERA.drag.X);
    mouseY -= static_cast<float>(TOUCH_CAMERA.drag.Y);
#endif
    if (this->rightGestureActive && ((this->actionReleasedBits & SECONDARY_INPUT) != 0))
    {
        this->rightSkillTap = !this->rightCameraDrag && !this->rightClickConsumed;
        this->rightGestureActive = false;
    }

    if (this->rightGestureActive && !pc->IsInputKeyDown(EKeys::RightMouseButton) &&
        !((this->actionPressedBits & SECONDARY_INPUT) != 0))
    {
        this->rightGestureActive = false;
    }

    const float WHEEL = float(pc->WasInputKeyJustPressed(EKeys::MouseScrollUp)) -
                        float(pc->WasInputKeyJustPressed(EKeys::MouseScrollDown));
#if !UE_BUILD_SHIPPING
    // The maze smoke overview must retain its capture distance beyond the normal zoom limit.
    const bool MAZE_OVERVIEW = this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeMaze")) &&
                               (this->controlSmokeStage == 2 || this->controlSmokeStage == 3);
    if (!MAZE_OVERVIEW)
    {
#endif
        if (auto* pawn = this->players.Find(this->prepared.epoch); pawn && IsValid(*pawn))
        {
            (*pawn)->UpdateCamera(mouseX, mouseY, WHEEL);
#if PLATFORM_ANDROID
            if (!FMath::IsNearlyEqual(TOUCH_CAMERA.zoomRatio, 1.f))
            {
                (*pawn)->SetCameraZoom((*pawn)->GetCameraZoom() * TOUCH_CAMERA.zoomRatio, .08f);
            }

#endif
        }

#if !UE_BUILD_SHIPPING
    }

#endif
    if (this->map && this->prepared.map.id == 301 && this->prepared.map.version >= 3)
    {
        UpdateBossCamera(shared_world::dungeon::RoomAt(this->predicted) == 6);
    }

    if (TickCombatControl(0))
    {
        return;
    }

    if (this->currentNpc || this->exitWarping || InventoryVisible() || this->shopOpening)
    {
        return; // Interactions and inventory input cannot also submit movement.
    }

    if (((this->actionPressedBits & WALK_MODE_INPUT) != 0))
    {
        this->walkMode = !this->walkMode;
        this->onStatusChanged.Broadcast(this->walkMode ? TEXT("Walk mode: 120 cm/s. Caps Lock switches to run.")
                                                       : TEXT("Run mode: 600 cm/s. Caps Lock switches to walk."));
        UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL gait=%s"), this->walkMode ? TEXT("walk") : TEXT("run"));
    }

    // Check every rendered frame, even while streaming blocks simulation input.
    if (pc->IsInputKeyDown(EKeys::W) || pc->IsInputKeyDown(EKeys::A) || pc->IsInputKeyDown(EKeys::S) ||
        pc->IsInputKeyDown(EKeys::D))
    {
        if (this->clickPending || !this->clickPath.empty() || this->clickCancellation)
        {
            CancelClickMove();
        }

    }

    if (this->clickPending)
    {
        if (FPlatformTime::Seconds() > this->clickDeadline)
        {
            CancelClickMove();
            this->onStatusChanged.Broadcast(TEXT("Terrain or path was not ready. Choose another destination."));
        }
        else if (this->clickTaskBudget->load() == 0)
        {
            StartClickPath();
        }

    }

}

void UGameSessionSubsystem::StartClickPath()
{
    if (this->state != 5 || !this->map || !this->clickPending || this->clickTaskBudget->load() != 0)
    {
        return;
    }

    using namespace shared_world;
    GroundCollision lookup;
    if (!this->map->content->chunks)
    {
        auto collision = this->map->content->collision;
        lookup = [collision](WorldPosition)
        {
            return collision;
        };

    }
    else
    {
        const auto LAYOUT = this->map->content->chunks->layout;
        auto clamp = [&](int x, int y)
        {
            return WorldPosition{FMath::Clamp(x, LAYOUT.world.bounds.min.x, LAYOUT.world.bounds.max.x - 1),
                                 FMath::Clamp(y, LAYOUT.world.bounds.min.y, LAYOUT.world.bounds.max.y - 1),
                                 this->predicted.z};
        };

        const auto FIRST = LAYOUT.Coordinate(clamp(this->predicted.x - 4000, this->predicted.y - 4000));
        const auto LAST = LAYOUT.Coordinate(clamp(this->predicted.x + 4000, this->predicted.y + 4000));
        std::map<TileCoord, std::shared_ptr<const StaticWorld>> snapshots;
        bool waiting = false;
        for (int x = FIRST.x; x <= LAST.x; ++x)
        {
            for (int y = FIRST.y; y <= LAST.y; ++y)
            {
                const TileCoord COORDINATE{x, y};
                const auto& references = this->map->content->manifest.chunks;
                if (std::none_of(references.begin(), references.end(),
                                 [&](const auto& c)
                                 {
                                     return c.x == x && c.y == y;
                                 }))

                {
                    continue;
                }

                const auto BOUNDS = LAYOUT.Bounds(COORDINATE, false);
                const auto SAMPLE = clamp(BOUNDS.min.x + 1, BOUNDS.min.y + 1);
                auto collision = this->map->residency->Get(SAMPLE);
                if (collision)
                {
                    snapshots.emplace(COORDINATE, std::move(collision));
                }
                else if (!this->map->residency->Failed(SAMPLE))
                {
                    waiting = true;
                }

            }

        }

        if (waiting)
        {
            return;
        }

        lookup = [LAYOUT, snapshots = std::move(snapshots)](WorldPosition p) -> std::shared_ptr<const StaticWorld>
        {
            if (!LAYOUT.world.bounds.Contains(p))
            {
                return {};
            }

            auto found = snapshots.find(LAYOUT.Coordinate(p));
            return found == snapshots.end() ? nullptr : found->second;
        };

    }

    struct BodySnapshot
    {
        WorldPosition feet;
        unsigned radius, height;
    };

    std::vector<BodySnapshot> bodies;
    if (this->map->content->combat)
    {
        for (const auto& n : this->npcs)
        {
            if (IsValid(n.Value) && !n.Value->dead)
            {
                const auto IT = this->map->content->combat->monsters.find(n.Value->npcIndex);
                if (IT == this->map->content->combat->monsters.end() || !IT->second.solidRadius)
                {
                    continue;
                }

                const FVector P =
                    (this->remoteTargets.Contains(n.Key) ? this->remoteTargets[n.Key] : n.Value->GetActorLocation()) +
                    FVector(GetWorld()->OriginLocation);
                bodies.push_back({{FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y), FMath::RoundToInt32(P.Z)},
                                  IT->second.solidRadius + combat::PLAYER_RADIUS,
                                  IT->second.bodyHeight});
            }

        }

    }

    const unsigned ROOM = this->map->activeRoom;
    GroundEdgeClear edgeClear = [bodies = std::move(bodies), ROOM](WorldPosition from, WorldPosition to)
    {
        if (!dungeon::DoorClear(from, to, ROOM))
        {
            return false;
        }

        for (const auto& body : bodies)
        {
            if (!combat::BodyMoveClear(from, to, body.feet, body.radius, body.height))
            {
                return false;
            }

        }

        return true;
    };

    this->clickPending = false;
    const auto START = this->clickOrigin, GOAL = this->clickGoal;
    bool delayForTest = false;
#if !UE_BUILD_SHIPPING
    delayForTest = this->smoke && FParse::Param(FCommandLine::Get(), TEXT("RpgWorldSmokeClickDelay"));
#endif
    const uint64 OWNER_GENERATION = this->generation, REQUEST_GENERATION = this->clickRequest;
    this->clickCancellation = std::make_shared<std::atomic<bool>>(false);
    const auto CANCELLATION = this->clickCancellation;
    const auto BUDGET = this->clickTaskBudget;
    BUDGET->fetch_add(1);
    // The slot includes the queued game-thread completion, not just computation.
    auto slot = std::shared_ptr<int>(new int(0),
                                     [BUDGET](int* value)
                                     {
                                         delete value;
                                         BUDGET->fetch_sub(1);
                                     });

    TWeakObjectPtr<UGameSessionSubsystem> weak(this);
    // One bounded dedicated task avoids waiting behind asset/streaming pool work.
    Async(EAsyncExecution::Thread,
          [lookup = std::move(lookup), edgeClear = std::move(edgeClear), START, GOAL, delayForTest, OWNER_GENERATION,
           REQUEST_GENERATION, CANCELLATION, slot, weak]
          {
              // Delayed-planner smoke: no speculative movement may escape before a route.
              if (delayForTest)
              {
                  for (int i = 0; i < 100 && !CANCELLATION->load(); ++i)
                  {
                      FPlatformProcess::SleepNoStats(.005f);
                  }

              }

              const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(2);
              GroundPathResult result;
              try
              {
                  auto cancelled = [&]
                  {
                      return CANCELLATION->load() || std::chrono::steady_clock::now() > DEADLINE;
                  };

                  result = FindGroundPath(lookup, START, GOAL, 1024, cancelled, edgeClear);
                  if (result.status == GroundPathStatus::FOUND)
                  {
                      // Publish a complete obstacle-aware route before its first input.
                      // Local prediction still applies immediately; no server round trip.
                      const auto BEFORE = result.points.size();
                      const auto SMOOTH_DEADLINE =
                          std::min(DEADLINE, std::chrono::steady_clock::now() + std::chrono::milliseconds(30));
                      result.points = SmoothGroundPath(
                          lookup, result.points, 4096,
                          [&]
                          {
                              return CANCELLATION->load() || std::chrono::steady_clock::now() > SMOOTH_DEADLINE;
                          },

                          edgeClear);
                      UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL path_smoothed before=%llu after=%llu"),
                             static_cast<uint64>(BEFORE), static_cast<uint64>(result.points.size()));
                  }

              }

              catch (const std::exception&)
              {
                  result.status = GroundPathStatus::BUDGET_EXCEEDED;
              }

              AsyncTask(
                  ENamedThreads::GameThread,
                  [weak, OWNER_GENERATION, REQUEST_GENERATION, delayForTest, START, result = std::move(result), slot]
                  {
                      auto* self = weak.Get();
                      if (!self || self->generation != OWNER_GENERATION || self->clickRequest != REQUEST_GENERATION ||
                          self->state != 5)
                      {
                          return;
                      }

#if !UE_BUILD_SHIPPING
                      if (delayForTest && !self->smokeInjected)
                      {
                          // Exercise completion racing a corrected position. The real
                          // server snapshot must subsequently repair this synthetic error.
                          self->smokeInjected = true;
                          self->predicted.y += 200;
                          // With no speculative walking there is no outstanding movement
                          // ack. Request one using a test-only zero-displacement input.
                          shared_world::net::Message probe;
                          probe.kind = shared_world::net::Kind::MOVE;
                          probe.epoch = self->prepared.epoch;
                          probe.request = self->prepared.request;
                          probe.sequence = ++self->nextSequence;
                          if (!self->Send(probe))
                          {
                              self->Fail(TEXT("Smoke correction probe failed"));
                              return;
                          }

                          self->pendingInputs.push_back(probe);
                          self->pendingPositions.push_back(self->predicted);
                          UE_LOG(LogTemp, Display, TEXT("WORLD_SMOKE clicks_correction_injected"));
                      }

#endif
                      const bool SAME_START = START == self->predicted;
                      if (!SAME_START && self->clickPlanRetries < 2 && FPlatformTime::Seconds() < self->clickDeadline)
                      {
                          // A correction changed the origin while planning. Discard the
                          // stale route and retry after this completion releases its slot.
                          ++self->clickPlanRetries;
                          ++self->clickRequest;
                          self->clickCancellation.reset();
                          self->clickOrigin = self->predicted;
                          self->clickPending = true;
                          UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL click_replan_after_correction"));
                          return;
                      }

                      if (result.status != GroundPathStatus::FOUND || !SAME_START)
                      {
                          self->CancelClickMove();
                          self->onStatusChanged.Broadcast(TEXT("No safe local path found."));
                          return;
                      }

                      self->clickPath = result.points;
                      self->clickWaypoint = 1;
                      const auto DESTINATION = result.points.back();
                      self->ShowMoveMarker(FVector(DESTINATION.x, DESTINATION.y, DESTINATION.z) -
                                               FVector(self->GetWorld()->OriginLocation),
                                           true);
                      self->onStatusChanged.Broadcast(TEXT("Click movement active. WASD cancels it."));
                      UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL path_ready points=%llu expanded=%llu delay_ms=%.3f"),
                             static_cast<uint64>(result.points.size()), static_cast<uint64>(result.expanded),
                             (FPlatformTime::Seconds() - self->clickStarted) * 1000);
                  });

          });

}

shared_world::WorldPosition UGameSessionSubsystem::ControlStep(int x, int y)
{
    if (x || y)
    {
        CancelClickMove();
        float yaw = 0;
        if (auto* pawn = this->players.Find(this->prepared.epoch); pawn && IsValid(*pawn))
        {
            yaw = (*pawn)->CameraYaw();
        }

        const FVector DIRECTION =
            FRotator(0, yaw, 0).RotateVector(FVector(x, y, 0)).GetSafeNormal() * (this->walkMode ? 4 : 20);
        // Round the small walking vector: truncation would reduce diagonal speed by ~30%.
        if (this->walkMode)
        {
            return {static_cast<int32>(FMath::RoundToInt(DIRECTION.X)),
                    static_cast<int32>(FMath::RoundToInt(DIRECTION.Y)), 0};
        }

        return {static_cast<int>(DIRECTION.X), static_cast<int>(DIRECTION.Y), 0};
    }

    while (this->clickWaypoint < this->clickPath.size())
    {
        const auto GOAL = this->clickPath[this->clickWaypoint];
        if (this->predicted.x == GOAL.x && this->predicted.y == GOAL.y)
        {
            ++this->clickWaypoint;
            continue;
        }

        if (this->walkMode)
        {
            const FVector REMAINING(GOAL.x - this->predicted.x, GOAL.y - this->predicted.y, 0);
            if (REMAINING.SizeSquared() <= 16)
            {
                return {GOAL.x - this->predicted.x, GOAL.y - this->predicted.y, 0};
            }

            const FVector DIRECTION = REMAINING.GetSafeNormal() * 4;
            return {static_cast<int32>(FMath::RoundToInt(DIRECTION.X)),
                    static_cast<int32>(FMath::RoundToInt(DIRECTION.Y)), 0};
        }

        return shared_world::GroundStep(this->predicted, GOAL);
    }

    if (!this->clickPath.empty())
    {
        CancelClickMove();
        this->onStatusChanged.Broadcast(TEXT("Destination reached."));
        UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL arrived"));
    }

    return {};
}

