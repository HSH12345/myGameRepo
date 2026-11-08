#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include <Blueprint/UserWidget.h>
#include <UIs/Title/TitleUIController.h>
#include "UIManager.generated.h"

UENUM(BlueprintType)
enum class UIControllerType : uint8
{
    TITLE UMETA(DisplayName = "Title")
};

UCLASS()

class RPG_API UUIManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIManager")
    TMap<UIControllerType, UUserWidget*> uiControllers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIManager")
    TMap<UIControllerType, TSubclassOf<UUserWidget>> uiControllerClasses;

public:
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;
    void StartTitle();

    UFUNCTION(BlueprintCallable, Category = "UIManager")
    void SetUIControllers(TMap<UIControllerType, TSubclassOf<UUserWidget>> classes);
    UTitleUIController* GetUIController();

private:
    void AddUIController(UIControllerType uiControllerType, UUserWidget* uiController);
    void CreateMyWidget(UIControllerType uiControllerType);
};

