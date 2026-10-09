#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

#include "AGBGameFramework.generated.h"

/** Game mode for AI Game Builder games: AGB character, player controller and HUD. Blueprint subclasses set the classes. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAGBGameMode();
};

/** Player controller for AI Game Builder games (menus and input modes are added in later phases). */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBPlayerController : public APlayerController
{
	GENERATED_BODY()
};

/** Minimal placeholder HUD: crosshair and the interaction prompt ("[E] Open door"). Replaced by UMG in the UI phase. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBHUD : public AHUD
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|HUD")
	bool bShowCrosshair = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|HUD")
	FLinearColor TextColor = FLinearColor::White;

	virtual void DrawHUD() override;

private:
	/** Display name of the first keyboard key bound to the interact action ("E"). */
	FString GetInteractKeyName() const;
};
