#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EquipmentVisualDefinition.generated.h"
class USkeletalMesh;
class UStaticMesh;
class UAnimSequence;

// Local presentation data only. Server-authorized appearance IDs select this asset.
UCLASS(BlueprintType)

class RPG_API UEquipmentVisualDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere)
    int32 appearanceId = 0;
    UPROPERTY(EditAnywhere)
    int32 gender = 0;
    UPROPERTY(EditAnywhere)
    TSoftObjectPtr<USkeletalMesh> outfitHead;
    UPROPERTY(EditAnywhere)
    TArray<TSoftObjectPtr<USkeletalMesh>> outfitParts;
    UPROPERTY(EditAnywhere)
    bool hideHair = false;
    UPROPERTY(EditAnywhere)
    TSoftObjectPtr<UStaticMesh> weapon;
    UPROPERTY(EditAnywhere)
    TSoftObjectPtr<UAnimSequence> weaponIdle;
    UPROPERTY(EditAnywhere)
    FName weaponBone = TEXT("hand_r");
    UPROPERTY(EditAnywhere)
    FTransform weaponTransform = FTransform::Identity;
};

