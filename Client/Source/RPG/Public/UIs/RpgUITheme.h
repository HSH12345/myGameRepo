#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
class UButton;
class UUserWidget;
class UWidgetTree;
class UWidget;
class UBorder;
class UTextBlock;
class UTexture2D;
class UImage;
class UScaleBox;

// Presentation only. All entry points run on the game thread; resources are GC traced.
namespace RpgUI
{
RPG_API UTexture2D* Texture(FName name);
RPG_API FSlateBrush Brush(FName name, bool nineSlice = false);
RPG_API FSlateFontInfo Font(int32 size);
RPG_API void Text(UTextBlock* label, int32 size = 0);
RPG_API void Button(UButton* widget, bool primary = false, bool slot = false);
RPG_API void Tooltip(UWidgetTree* tree, UWidget* widget, const FString& value);
RPG_API UImage* SelectionGlow(UWidgetTree* tree);
RPG_API UScaleBox* FitImage(UWidgetTree* tree, UImage* image);
RPG_API void FitButton(UWidgetTree* tree, UButton* widget);
RPG_API void FitTree(UUserWidget* root);
RPG_API void Frame(UBorder* widget);
RPG_API FName ItemIcon(uint32 type);
RPG_API FName SkillIcon(uint32 skill);
RPG_API FString ItemName(uint32 type, const FString& fallback);
RPG_API FString Caption(const FString& value);
}

