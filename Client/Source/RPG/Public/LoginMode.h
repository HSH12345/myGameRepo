#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UIs/Title/TitleUIController.h"
#include "LoginMode.generated.h"

UCLASS()

class RPG_API ALoginMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALoginMode();

protected:
    virtual void BeginPlay() override;
    UPROPERTY(EditDefaultsOnly, Category = "Lobby")
    TSubclassOf<UTitleUIController> lobbyWidgetClass;
};

