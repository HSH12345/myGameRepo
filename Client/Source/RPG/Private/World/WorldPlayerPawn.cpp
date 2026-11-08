#include "World/WorldPlayerPawn.h"
#include "World/WorldCameraOcclusion.h"
#include "World/WorldCameraArm.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Characters/CharacterAppearanceDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSingleNodeInstance.h"

AWorldPlayerPawn::AWorldPlayerPawn()
{
    this->PrimaryActorTick.bCanEverTick = true;
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    this->body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    this->body->SetupAttachment(this->RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (mesh.Succeeded())
    {
        this->body->SetStaticMesh(mesh.Object);
    }

    this->body->SetRelativeLocation(FVector(0, 0, 90));
    this->body->SetRelativeScale3D(FVector(.68, .68, 1.8));
    this->body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    this->body->SetReceivesDecals(false);
    this->arm = CreateDefaultSubobject<UWorldCameraArm>(TEXT("CameraArm"));
    this->arm->SetupAttachment(this->RootComponent);
    this->arm->TargetArmLength = 650;
    this->arm->SetRelativeRotation(FRotator(-40, 0, 0));
    this->arm->bDoCollisionTest = false;
    this->arm->ProbeChannel = ECC_Camera;
    this->arm->ProbeSize = 18;
    this->arm->PrimaryComponentTick.bStartWithTickEnabled = false;
    this->arm->SetRelativeLocation(FVector(0, 0, 100));
    this->arm->SetUsingAbsoluteRotation(true);
    this->cameraOcclusion = CreateDefaultSubobject<UWorldCameraOcclusion>(TEXT("CameraOcclusion"));
    this->arm->AddTickPrerequisiteActor(this);
    this->camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    this->camera->SetupAttachment(this->arm);
    this->label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Name"));
    this->label->SetupAttachment(this->RootComponent);
    this->label->SetRelativeLocation(FVector(0, 0, 210));
    this->label->SetWorldSize(28);
    this->label->SetHorizontalAlignment(EHTA_Center);
}

void AWorldPlayerPawn::SetCombatBodyHeight(unsigned height)
{
    this->label->SetRelativeLocation(FVector(0, 0, FMath::Max(210U, height + 40)));
}

void AWorldPlayerPawn::SetDisplayName(const FString& name)
{
    this->displayName = name;
    this->label->SetText(FText::FromString(this->attackable && this->npcIndex
                                               ? FString::Printf(TEXT("%s\nHP %d / %d"), *name, this->hp, this->maxHp)
                                               : name));
}

void AWorldPlayerPawn::FaceLabel(const FVector& cameraLocation)
{
    this->label->SetWorldRotation((cameraLocation - this->label->GetComponentLocation()).Rotation());
}

void AWorldPlayerPawn::UpdateCamera(float mouseX, float mouseY, float wheel)
{
    auto rotation = this->arm->GetComponentRotation();
    rotation.Yaw = FMath::UnwindDegrees(rotation.Yaw + mouseX * 2.25f);
    rotation.Pitch = FMath::Clamp(rotation.Pitch + mouseY * 2.25f, -75.f, -15.f);
    this->arm->SetWorldRotation(rotation);
    if (wheel)
    {
        SetCameraZoom(GetCameraZoom() - wheel * 80.f, .2f);
    }

}

float AWorldPlayerPawn::GetCameraZoom() const
{
    return CastChecked<UWorldCameraArm>(this->arm.Get())->RequestedZoom();
}

void AWorldPlayerPawn::SetCameraZoom(float length, float duration)
{
    CastChecked<UWorldCameraArm>(this->arm.Get())->RequestZoom(length, duration);
}

bool AWorldPlayerPawn::InitialCameraReady() const
{
    return CastChecked<UWorldCameraArm>(this->arm.Get())->PresentationReady() &&
           this->cameraOcclusion->PresentationReady();
}

float AWorldPlayerPawn::CameraYaw() const
{
    return this->camera->GetComponentRotation().Yaw;
}

void AWorldPlayerPawn::RefreshPresentation()
{
    // Do not reveal the base body while the outfit/weapon is still streaming.
    SetActorHiddenInGame(!this->poolActive || !this->appearanceReady || !this->equipmentReady || this->equipmentFailed);
}

void AWorldPlayerPawn::LoadAppearance(uint32 race, uint32 gender)
{
    const FString KEY = FString::Printf(TEXT("Player:%u:%u"), race, gender);
    if (this->appearanceKey == KEY && this->appearanceReady)
    {
        return;
    }

    this->appearanceKey = KEY;
    const uint64 GENERATION = ++this->appearanceGeneration;
    if (this->appearanceLoad)
    {
        this->appearanceLoad->CancelHandle();
    }

    this->appearanceLoad.Reset();
    this->appearanceReady = false;
    this->appearanceFailed = false;
    RefreshPresentation();
    this->body->SetVisibility(false);
    if (this->skeletalBody)
    {
        this->skeletalBody->SetVisibility(false, true);
    }

    if (race != 0 || gender > 1)
    {
        this->appearanceFailed = true;
        UE_LOG(LogTemp, Warning, TEXT("Unsupported in-game appearance %u/%u"), race, gender);
        return;
    }

    const FSoftObjectPath PATH(gender == 0 ? TEXT("/Game/RPG/Characters/Human/Data/DA_Human_Male.DA_Human_Male")
                                           : TEXT("/Game/RPG/Characters/Human/Data/DA_Human_Female.DA_Human_Female"));
    this->appearanceLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        PATH, FStreamableDelegate::CreateWeakLambda(this,
                                                    [this, PATH, GENERATION]
                                                    {
                                                        if (GENERATION != this->appearanceGeneration)
                                                        {
                                                            return;
                                                        }

                                                        this->appearance =
                                                            Cast<UCharacterAppearanceDefinition>(PATH.ResolveObject());
                                                        if (!this->appearance)
                                                        {
                                                            this->appearanceFailed = true;
                                                            UE_LOG(LogTemp, Error,
                                                                   TEXT("In-game appearance definition load failed"));
                                                            return;
                                                        }

                                                        LoadAppearanceParts(GENERATION);
                                                    }));

}

void AWorldPlayerPawn::LoadNpcAppearance(const shared_world::npc::Appearance& definition)
{
    SetActorTickEnabled(false); // Stationary NPC only needs the skeletal animation component's tick.
    const FString KEY = FString::Printf(TEXT("Npc:%u"), definition.index);
    if (this->appearanceKey == KEY && this->appearanceReady)
    {
        return;
    }

    this->appearanceKey = KEY;
    const uint64 GENERATION = ++this->appearanceGeneration;
    if (this->appearanceLoad)
    {
        this->appearanceLoad->CancelHandle();
    }

    this->appearanceLoad.Reset();
    this->appearanceReady = false;
    this->appearanceFailed = false;
    RefreshPresentation();
    this->body->SetVisibility(false);
    if (this->skeletalBody)
    {
        this->skeletalBody->SetVisibility(false, true);
    }

    this->appearance = NewObject<UCharacterAppearanceDefinition>(this);
    this->appearance->previewMesh =
        TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(UTF8_TO_TCHAR(definition.mesh.c_str())));
    this->appearance->previewIdle =
        TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(UTF8_TO_TCHAR(definition.idle.c_str())));
    this->appearance->movementAnimation = this->appearance->previewIdle;
    this->appearance->walkingAnimation.Reset(); // NPCs keep their own locomotion assets and speed.
    this->appearance->gameplayYawOffset = definition.yaw / 1000.f;
    this->appearance->meshTransform =
        FTransform(FQuat::Identity, FVector::ZeroVector, FVector(definition.scaleMilli / 1000.f));
    LoadAppearanceParts(GENERATION);
}

void AWorldPlayerPawn::ApplyServerStats(const shared_world::EntityStats& stats)
{
    check(IsInGameThread());
    check(stats.Valid());
    this->hp = stats.hp;
    this->maxHp = stats.maxHp;
    this->mp = stats.mp;
    this->maxMp = stats.maxMp;
    this->attack = stats.attack;
    this->defense = stats.defense;
    this->evasion = stats.evasion;
    this->accuracy = stats.accuracy;
    this->attackable = (stats.flags & shared_world::EntityStats::ATTACKABLE) != 0;
    this->mortal = stats.CanDie();
    this->dead = stats.IsDead();
    if (this->dead)
    {
        SetCombatHighlighted(false);
    }

    SetDisplayName(this->displayName);
}

shared_world::EntityStats AWorldPlayerPawn::GetServerStats() const
{
    return {static_cast<uint32>(this->hp),
            static_cast<uint32>(this->maxHp),
            static_cast<uint32>(this->mp),
            static_cast<uint32>(this->maxMp),
            static_cast<uint32>(this->attack),
            static_cast<uint32>(this->defense),
            static_cast<uint32>(this->evasion),
            static_cast<uint32>(this->accuracy),
            (this->attackable ? shared_world::EntityStats::ATTACKABLE : 0) |
                (this->mortal ? shared_world::EntityStats::MORTAL : 0)};
}

void AWorldPlayerPawn::SetPooledActive(bool active)
{
    this->poolActive = active;
    if (!active)
    {
        SetCombatHighlighted(false);
        SetLocalVisibility(false);
        if (this->combatLoad)
        {
            this->combatLoad->CancelHandle();
        }

        this->combatLoad.Reset();
        this->combatPoseUntil = 0;
        this->pendingCombat = false;
        this->combatPaths.Empty();
        this->combatSkillIndices.Empty();
        this->combatAnimations.Empty();
        this->combatAssetsReady = false;
        ++this->equipmentGeneration;
        if (this->equipmentLoad)
        {
            this->equipmentLoad->CancelHandle();
        }

        this->equipmentLoad.Reset();
        this->desiredEquipment = {};
        this->equipmentRevision = 0;
        this->equipmentReady = true;
        this->awaitingInitialEquipment = false;
        ResetEquipmentVisual();
        this->equipmentFailed = false;
        this->equipmentFailureReported = false;
        ++this->appearanceGeneration;
        if (this->appearanceLoad)
        {
            this->appearanceLoad->CancelHandle();
        }

        this->appearanceLoad.Reset();
        if (!this->appearanceReady)
        {
            this->appearanceKey.Empty();
        }

        this->npcIndex = 0;
        this->npcRole = 0;
        this->npcRoleData = 0;
        SetCombatBodyHeight(160);
        SetDisplayName(TEXT(""));
        ApplyServerStats({});
    }

    this->moving = false;
    this->walkingAnimation = false;
    this->hasPreviousLocation = false;
    this->displaySpeed = 0;
    RefreshPresentation();
    SetActorTickEnabled(active);
    SetActorEnableCollision(false);
    TInlineComponentArray<USkeletalMeshComponent*> components;
    GetComponents(components);
    for (auto* component : components)
    {
        component->SetComponentTickEnabled(active);
    }

    if (active)
    {
        this->arm->SetWorldRotation(FRotator(-40, 0, 0));
        this->arm->TargetArmLength = 650;
        CastChecked<UWorldCameraArm>(this->arm.Get())->ResetPresentation();
        if (this->appearanceReady && this->skeletalBody)
        {
            this->skeletalBody->PlayAnimation(this->idleAnimation, true);
            this->skeletalBody->SetPlayRate(1.f);
        }

    }

}

void AWorldPlayerPawn::LoadAppearanceParts(uint64 generation)
{
    TArray<FSoftObjectPath> assets = {this->appearance->previewMesh.ToSoftObjectPath(),
                                      this->appearance->previewIdle.ToSoftObjectPath(),
                                      this->appearance->movementAnimation.ToSoftObjectPath()};
    if (!this->appearance->walkingAnimation.IsNull())
    {
        assets.AddUnique(this->appearance->walkingAnimation.ToSoftObjectPath());
    }

    for (const auto& part : this->appearance->additionalMeshes)
    {
        assets.AddUnique(part.ToSoftObjectPath());
    }

    this->appearanceLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        assets, FStreamableDelegate::CreateWeakLambda(this,
                                                      [this, generation]
                                                      {
                                                          FinishAppearance(generation);
                                                      }));

}

void AWorldPlayerPawn::FinishAppearance(uint64 generation)
{
    if (generation != this->appearanceGeneration || !this->appearance)
    {
        return;
    }

    auto* mesh = this->appearance->previewMesh.Get();
    this->idleAnimation = this->appearance->previewIdle.Get();
    this->moveAnimation = this->appearance->movementAnimation.Get();
    this->walkAnimation = this->appearance->walkingAnimation.Get();
    this->baseBodyMesh = mesh;
    this->baseIdleAnimation = this->idleAnimation;
    if (!mesh || !this->idleAnimation || !this->moveAnimation || !this->idleAnimation->GetSkeleton() ||
        !this->moveAnimation->GetSkeleton() || !this->idleAnimation->GetSkeleton()->IsCompatibleMesh(mesh) ||
        !this->moveAnimation->GetSkeleton()->IsCompatibleMesh(mesh))
    {
        this->appearanceFailed = true;
        UE_LOG(LogTemp, Error, TEXT("Appearance failed key=%s mesh=%s idle=%s move=%s: missing or incompatible assets"),
               *this->appearanceKey, *this->appearance->previewMesh.ToSoftObjectPath().ToString(),
               *this->appearance->previewIdle.ToSoftObjectPath().ToString(),
               *this->appearance->movementAnimation.ToSoftObjectPath().ToString());
        return;
    }

    if (!this->appearance->walkingAnimation.IsNull() && (!this->walkAnimation || !this->walkAnimation->GetSkeleton() ||
                                                         !this->walkAnimation->GetSkeleton()->IsCompatibleMesh(mesh)))
    {
        this->appearanceFailed = true;
        UE_LOG(LogTemp, Error, TEXT("In-game walking animation missing/incompatible"));
        return;
    }

    for (const auto& part : this->appearance->additionalMeshes)
    {
        if (!part.Get())
        {
            this->appearanceFailed = true;
            return;
        }

    }

    ResetEquipmentVisual();
    for (const auto& part : this->appearanceParts)
    {
        if (IsValid(part))
        {
            part->DestroyComponent();
        }

    }

    this->appearanceParts.Reset();
    if (IsValid(this->skeletalBody))
    {
        this->skeletalBody->DestroyComponent();
    }

    auto createMesh = [this](USceneComponent* parent)
    {
        auto* component = NewObject<USkeletalMeshComponent>(this);
        component->SetReceivesDecals(false);
        AddInstanceComponent(component);
        component->SetupAttachment(parent);
        component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        component->SetGenerateOverlapEvents(false);
        component->SetVisibility(false);
        component->RegisterComponent();
        return component;
    };

    this->skeletalBody = createMesh(this->RootComponent);
    this->skeletalBody->SetSkeletalMesh(mesh);
    this->skeletalBody->SetRelativeTransform(this->appearance->meshTransform);
    this->skeletalBody->AddRelativeRotation(FRotator(0, this->appearance->gameplayYawOffset, 0));
    this->skeletalBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    this->skeletalBody->PlayAnimation(this->idleAnimation, true);
    for (const auto& part : this->appearance->additionalMeshes)
    {
        auto* component = createMesh(this->skeletalBody);
        component->SetSkeletalMesh(part.Get());
        component->SetLeaderPoseComponent(this->skeletalBody);
        this->appearanceParts.Add(component);
    }

    this->skeletalBody->TickAnimation(0, false);
    this->skeletalBody->RefreshBoneTransforms();
    this->skeletalBody->SetVisibility(true, true);
    this->body->SetVisibility(false);
    this->appearanceReady = true;
    this->moving = false;
    this->walkingAnimation = false;
    this->displaySpeed = 0;
    this->previousLocation = GetActorLocation();
    this->hasPreviousLocation = true;
    if (this->desiredEquipment[0] || this->desiredEquipment[1])
    {
        this->equipmentReady = false;
    }

    this->appearanceLoad.Reset();
    RefreshVisibilityMeshes();
    RefreshPresentation();
    if (!this->combatPaths.IsEmpty())
    {
        LoadCombatAnimations();
    }

    UE_LOG(LogTemp, Display, TEXT("WORLD_CONTROL appearance_ready race=%u gender=%u"), this->appearance->race,
           this->appearance->gender);
    // An empty equipment snapshot may arrive before appearance. Complete it too;
    // only a roster that explicitly awaits the initial snapshot must stay hidden.
    if (!this->awaitingInitialEquipment)
    {
        LoadEquipmentDefinitions(++this->equipmentGeneration);
    }

}

UAnimSequence* AWorldPlayerPawn::CurrentMovementAnimation() const
{
    return this->walkingAnimation && this->walkAnimation ? this->walkAnimation.Get() : this->moveAnimation.Get();
}

void AWorldPlayerPawn::Tick(float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    if (this->localVisibility)
    {
        this->cameraOcclusion->Update(this->arm, deltaSeconds);
    }

    const FVector LOCATION = GetActorLocation();
    const float DISTANCE = FVector::Dist2D(LOCATION, this->previousLocation);
    this->previousLocation = LOCATION;
    if (!this->hasPreviousLocation)
    {
        this->hasPreviousLocation = true;
        return;
    }

    if (!this->appearanceReady || deltaSeconds <= 0)
    {
        return;
    }

    if (this->dead || FPlatformTime::Seconds() < this->combatPoseUntil)
    {
        return;
    }

    if (this->combatPoseUntil > 0)
    {
        this->combatPoseUntil = 0;
        this->moving = false;
        this->walkingAnimation = false;
        this->skeletalBody->PlayAnimation(this->idleAnimation, true);
        this->skeletalBody->SetPlayRate(1);
    }

    const float SPEED = DISTANCE > 200 ? 0 : DISTANCE / deltaSeconds;
    this->displaySpeed = FMath::FInterpTo(this->displaySpeed, SPEED, deltaSeconds, 12);
    const bool MOVING = this->displaySpeed > (this->moving ? 8.f : 20.f);
    // Infer gait from rendered displacement, including remote interpolation and blocked movement.
    // Hysteresis prevents mode flicker around the threshold; NPCs have no walking asset.
    const bool WALKING = MOVING && this->walkAnimation && this->displaySpeed < (this->walkingAnimation ? 300.f : 240.f);
    if (MOVING != this->moving || WALKING != this->walkingAnimation)
    {
        this->moving = MOVING;
        this->walkingAnimation = WALKING;
        this->skeletalBody->PlayAnimation(this->moving ? CurrentMovementAnimation() : this->idleAnimation.Get(), true);
    }

    const float REFERENCE_SPEED =
        this->walkingAnimation ? this->appearance->walkingAnimationSpeed : this->appearance->animationMovementSpeed;
    this->skeletalBody->SetPlayRate(
        this->moving ? FMath::Clamp(this->displaySpeed / FMath::Max(1.f, REFERENCE_SPEED), .5f, 1.5f) : 1.f);
}

void AWorldPlayerPawn::EndPlay(const EEndPlayReason::Type reason)
{
    if (this->combatLoad)
    {
        this->combatLoad->CancelHandle();
    }

    this->combatLoad.Reset();
    ++this->appearanceGeneration;
    if (this->appearanceLoad)
    {
        this->appearanceLoad->CancelHandle();
    }

    this->appearanceLoad.Reset();
    ++this->equipmentGeneration;
    if (this->equipmentLoad)
    {
        this->equipmentLoad->CancelHandle();
    }

    this->equipmentLoad.Reset();
    this->cameraOcclusion->Clear();
    Super::EndPlay(reason);
}

void AWorldPlayerPawn::ConfigureCombatVisual(const shared_world::combat::Visual& visual)
{
    this->combatPaths = {
        FSoftObjectPath(UTF8_TO_TCHAR(visual.move.c_str())), FSoftObjectPath(UTF8_TO_TCHAR(visual.attack.c_str())),
        FSoftObjectPath(UTF8_TO_TCHAR(visual.cast.c_str())), FSoftObjectPath(UTF8_TO_TCHAR(visual.hit.c_str())),
        FSoftObjectPath(UTF8_TO_TCHAR(visual.death.c_str()))};
    this->combatSkillIndices.Empty();
    for (const auto& skill : visual.skills)
    {
        this->combatSkillIndices.Add(skill.first, this->combatPaths.Num());
        this->combatPaths.Add(FSoftObjectPath(UTF8_TO_TCHAR(skill.second.c_str())));
    }

    this->combatAssetsReady = false;
    SetActorTickEnabled(true);
    if (this->appearanceReady)
    {
        LoadCombatAnimations();
    }

}

void AWorldPlayerPawn::LoadCombatAnimations()
{
    if (this->combatLoad)
    {
        this->combatLoad->CancelHandle();
    }

    const uint64 CURRENT_GENERATION = this->appearanceGeneration;
    this->combatLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        this->combatPaths,
        FStreamableDelegate::CreateWeakLambda(
            this,
            [this, CURRENT_GENERATION]
            {
                if (CURRENT_GENERATION != this->appearanceGeneration || !this->poolActive || !this->skeletalBody)
                {
                    return;
                }

                this->combatAnimations.Empty();
                for (const auto& path : this->combatPaths)
                {
                    auto* animation = Cast<UAnimSequence>(path.ResolveObject());
                    if (!animation || !animation->GetSkeleton() ||
                        !animation->GetSkeleton()->IsCompatibleMesh(this->skeletalBody->GetSkeletalMeshAsset()))
                    {
                        this->appearanceFailed = true;
                        UE_LOG(LogTemp, Error, TEXT("COMBAT animation unavailable/incompatible %s"), *path.ToString());
                        return;
                    }

                    this->combatAnimations.Add(animation);
                }

                this->moveAnimation = this->combatAnimations[0];
                this->combatAssetsReady = true;
                this->combatLoad.Reset();
                if (this->pendingCombat)
                {
                    const auto E = this->pendingCombatEvent;
                    const double NOW = this->pendingCombatServer + FPlatformTime::Seconds() - this->pendingCombatLocal;
                    this->pendingCombat = false;
                    PlayCombat(E, this->isPendingCombatAbility ? &this->pendingCombatAbilityValue : nullptr, NOW);
                }

                UE_LOG(LogTemp, Display, TEXT("COMBAT assets_ready key=%s death=%s"), *this->appearanceKey,
                       *this->combatAnimations[4]->GetName());
            }));

}

void AWorldPlayerPawn::PlayCombat(const shared_world::combat::Event& e, const shared_world::combat::Ability* ability,
                                  double serverNow)
{
    using namespace shared_world::combat;
    if (!this->combatAssetsReady || !this->skeletalBody)
    {
        this->pendingCombatEvent = e;
        this->isPendingCombatAbility = ability != nullptr;
        if (ability)
        {
            this->pendingCombatAbilityValue = *ability;
        }

        this->pendingCombatServer = serverNow;
        this->pendingCombatLocal = FPlatformTime::Seconds();
        this->pendingCombat = true;
        return;
    }

    auto play = [&](int index, bool loop, float rate, double duration)
    {
        this->skeletalBody->PlayAnimation(this->combatAnimations[index], loop);
        this->skeletalBody->SetPlayRate(rate);
        this->combatPoseUntil = FPlatformTime::Seconds() + duration;
        this->moving = false;
    };

    if (e.phase == Phase::DEATH)
    {
        play(4, false, 1, 1e9);
        const double ELAPSED = FMath::Max(0., serverNow - double(e.startMs) / 1000);
        this->skeletalBody->SetPosition(FMath::Min(float(ELAPSED), this->combatAnimations[4]->GetPlayLength()), false);
    }
    else if (e.phase == Phase::RESPAWN || e.phase == Phase::CANCELLED)
    {
        this->combatPoseUntil = 0;
        this->moving = false;
        this->walkingAnimation = false;
        this->skeletalBody->PlayAnimation(this->idleAnimation, true);
        this->skeletalBody->SetPlayRate(1);
    }
    else if (e.phase == Phase::STARTED)
    {
        SetActorRotation(FRotator(0, e.yaw / 1000.f, 0));
        const double REMAINING = FMath::Max(.01, double(e.endMs) / 1000 - serverNow);
        if (const int32* index = this->combatSkillIndices.Find(e.skill))
        {
            const double RECOVERY = ability ? ability->recoveryMs / 1000. : .6;
            const double DURATION = FMath::Max(.01, double(e.endMs - e.startMs) / 1000 + RECOVERY);
            const float RATE = this->combatAnimations[*index]->GetPlayLength() / float(DURATION);
            play(*index, false, RATE, REMAINING + RECOVERY);
            this->skeletalBody->SetPosition(FMath::Clamp(float(serverNow - double(e.startMs) / 1000) * RATE, 0.f,
                                                         this->combatAnimations[*index]->GetPlayLength()),
                                            false);
        }
        else if (e.skill >= 3)
        {
            play(2, true, 1, REMAINING);
        }
        else
        {
            play(1, false, this->combatAnimations[1]->GetPlayLength() / float(REMAINING + .4), REMAINING + .4);
        }

    }
    else if (e.phase == Phase::FIRED && e.skill >= 3)
    {
        if (this->combatSkillIndices.Contains(e.skill))
        {
            this->combatPoseUntil = FPlatformTime::Seconds() + (ability ? ability->recoveryMs / 1000. : .6);
        }
        else
        {
            play(1, false, 2, .35);
        }

    }
    else if (e.phase == Phase::HIT && e.value && !this->dead && FPlatformTime::Seconds() >= this->combatPoseUntil)
    {
        play(3, false, 1, .35);
    }

    (void)ability;
}

float AWorldPlayerPawn::CombatAnimationPosition() const
{
    return this->skeletalBody && this->skeletalBody->GetSingleNodeInstance()
               ? this->skeletalBody->GetSingleNodeInstance()->GetCurrentTime()
               : 0;
}

