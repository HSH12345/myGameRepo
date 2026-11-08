#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SharedWorld/Npc/NpcCatalog.h"
#include "SharedWorld/Combat/Combat.h"
#include <array>
#include "WorldPlayerPawn.generated.h"
class UStaticMeshComponent;
class UCameraComponent;
class USpringArmComponent;
class UTextRenderComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UCharacterAppearanceDefinition;
class UAnimSequence;
class UMaterialInterface;
class UMeshComponent;
class UWorldCameraOcclusion;
class UEquipmentVisualDefinition;
struct FStreamableHandle;
UCLASS()

class RPG_API AWorldPlayerPawn : public APawn
{
    GENERATED_BODY()
public:
    AWorldPlayerPawn();
    void SetCombatBodyHeight(unsigned height);
    void SetDisplayName(const FString& name);

    const FString& GetDisplayName() const
    {
        return this->displayName;
    }

    void FaceLabel(const FVector& cameraLocation);
    void UpdateCamera(float mouseX, float mouseY, float wheel);
    void SetLocalVisibility(bool enabled);
    void RefreshVisibilityMeshes();
    void SetCombatHighlighted(bool enabled);

    bool IsCombatHighlighted() const
    {
        return this->combatHighlighted;
    }

    bool HasLocalVisibility() const
    {
        return this->localVisibility;
    }

    UMaterialInterface* GetVisibilityMaterial() const
    {
        return this->visibilityMaterial;
    }

    float CameraYaw() const;
    float GetCameraZoom() const;
    void SetCameraZoom(float length, float duration = .45f);
    bool InitialCameraReady() const;
    void LoadAppearance(uint32 race, uint32 gender);
    void LoadNpcAppearance(const shared_world::npc::Appearance& definition);
    void SetPooledActive(bool active);
    void ConfigureCombatVisual(const shared_world::combat::Visual&);
    void PlayCombat(const shared_world::combat::Event&, const shared_world::combat::Ability*, double serverNow);

    bool CombatAssetsReady() const
    {
        return this->combatAssetsReady;
    }

    float CombatAnimationPosition() const;
    void ApplyServerStats(const shared_world::EntityStats& stats);
    shared_world::EntityStats GetServerStats() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 hp = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 maxHp = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 mp = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 maxMp = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 attack = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 defense = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 evasion = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 accuracy = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    bool attackable = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    bool mortal = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    bool dead = false;

    bool IsPoolActive() const
    {
        return this->poolActive;
    }

    const FString& GetAppearanceKey() const
    {
        return this->appearanceKey;
    }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 npcIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 npcRole = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 npcRoleData = 0;

    bool AppearanceReady() const
    {
        return this->appearanceReady;
    }

    bool InitialPresentationReady() const
    {
        return this->appearanceReady && this->equipmentReady &&
               (this->combatPaths.IsEmpty() || this->combatAssetsReady);
    }

    bool AppearanceFailed() const
    {
        return this->appearanceFailed;
    }

    bool IsMovementAnimationPlaying() const
    {
        return this->moving;
    }

    bool IsWalkingAnimationPlaying() const
    {
        return this->moving && this->walkingAnimation;
    }

    bool IsRunningAnimationPlaying() const
    {
        return this->moving && !this->walkingAnimation;
    }

    void AwaitInitialEquipment();
    void LoadEquipment(const std::array<std::uint32_t, 2>& ids, uint64 revision);

    bool EquipmentReady() const
    {
        return this->equipmentReady;
    }

    const std::array<std::uint32_t, 2>& GetEquipmentIds() const
    {
        return this->desiredEquipment;
    }

    bool EquipmentFailed() const
    {
        return this->equipmentFailed;
    }

    bool ConsumeEquipmentError()
    {
        if (!this->equipmentFailed || this->equipmentFailureReported)
        {
            return false;
        }

        this->equipmentFailureReported = true;
        return true;
    }

    void RetryEquipment();
    virtual void Tick(float deltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type reason) override;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> body;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UWorldCameraOcclusion> cameraOcclusion;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> camera;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USpringArmComponent> arm;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UTextRenderComponent> label;

private:
    UAnimSequence* CurrentMovementAnimation() const;
    void LoadCombatAnimations();
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequence>> combatAnimations;
    TArray<FSoftObjectPath> combatPaths;
    TMap<uint32, int32> combatSkillIndices;
    shared_world::combat::Ability pendingCombatAbilityValue;
    bool isPendingCombatAbility = false;
    TSharedPtr<FStreamableHandle> combatLoad;
    bool combatAssetsReady = false, pendingCombat = false;
    shared_world::combat::Event pendingCombatEvent;
    double pendingCombatServer = 0, pendingCombatLocal = 0, combatPoseUntil = 0;
    void RefreshPresentation();
    void LoadAppearanceParts(uint64 generation);
    void FinishAppearance(uint64 generation);
    void ResetEquipmentVisual();
    void LoadEquipmentDefinitions(uint64 generation);
    void FinishEquipment(uint64 generation);
    void FailEquipment(const TCHAR* reason);
    UPROPERTY(Transient)
    TArray<TObjectPtr<UEquipmentVisualDefinition>> equipmentDefinitions;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> equipmentParts;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> equippedWeapon;
    TSharedPtr<FStreamableHandle> equipmentLoad;
    std::array<std::uint32_t, 2> desiredEquipment{};
    uint64 equipmentGeneration = 0, equipmentRevision = 0;
    bool equipmentReady = true, awaitingInitialEquipment = false;
    bool equipmentFailed = false, equipmentFailureReported = false;
    UPROPERTY(Transient)
    TObjectPtr<UCharacterAppearanceDefinition> appearance;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> skeletalBody;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> appearanceParts;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> idleAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> baseIdleAnimation;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMesh> baseBodyMesh;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> moveAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> walkAnimation;
    bool walkingAnimation = false;
    TSharedPtr<FStreamableHandle> appearanceLoad;
    uint64 appearanceGeneration = 0;
    bool appearanceReady = false, moving = false, hasPreviousLocation = false;
    FVector previousLocation = FVector::ZeroVector;
    float displaySpeed = 0;
    bool poolActive = true;
    void RefreshAttackOutline();
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMeshComponent>> attackOutlineMeshes;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> attackOutlineMaterial;
    bool localVisibility = false, combatHighlighted = false;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> visibilityMaterial;
    bool appearanceFailed = false;
    FString appearanceKey;
    FString displayName;
};

