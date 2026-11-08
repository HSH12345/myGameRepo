#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CharacterAppearanceDefinition.generated.h"

class UAnimSequence;
class USkeletalMesh;

// Client-only mapping from the stable server appearance IDs to presentation assets.
// Asset paths never cross the network or enter the character database.
UCLASS(BlueprintType)

class RPG_API UCharacterAppearanceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
    uint8 race = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
    uint8 gender = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
    TSoftObjectPtr<USkeletalMesh> previewMesh;

    // Eyes, eyebrows, hair, armor, and other pieces can follow the main mesh pose.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
    TArray<TSoftObjectPtr<USkeletalMesh>> additionalMeshes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
    TSoftObjectPtr<UAnimSequence> previewIdle;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Gameplay")
    TSoftObjectPtr<UAnimSequence> movementAnimation = TSoftObjectPtr<UAnimSequence>(
        FSoftObjectPath(TEXT("/Game/Characters/Quaternius/Animations/UAL1/UAL1_Standard/SkeletalMeshes/"
                             "UAL1_StandardSprint_Loop.UAL1_StandardSprint_Loop")));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Gameplay", meta = (ClampMin = "1"))
    float animationMovementSpeed = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Gameplay")
    TSoftObjectPtr<UAnimSequence> walkingAnimation = TSoftObjectPtr<UAnimSequence>(
        FSoftObjectPath(TEXT("/Game/Characters/Quaternius/Animations/UAL1/UAL1_Standard/SkeletalMeshes/"
                             "UAL1_StandardWalk_Loop.UAL1_StandardWalk_Loop")));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Gameplay", meta = (ClampMin = "1"))
    float walkingAnimationSpeed = 120.f;

    // Imported meshes face +Y; gameplay yaw uses +X. Keep lobby framing independent.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Gameplay")
    float gameplayYawOffset = -90.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
    FTransform meshTransform = FTransform::Identity;
};

