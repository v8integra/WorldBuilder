#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"

#include "AGBUITheme.generated.h"

class UAGBAudioFXConfig;
class UFont;
class USoundClass;
class USoundMix;
class UTexture2D;
class UUserWidget;
class UWorld;

/**
 * Look of the whole user interface: menus (UMG) and the in-game HUD (canvas) read the same colors, fonts and
 * textures, so re-skinning the game is editing this one asset (e.g. with textures from a UI pack).
 */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBUITheme : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shown big on the main menu. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText GameTitle = NSLOCTEXT("AIGameBuilder", "DefaultTitle", "Survival");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText Subtitle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor TextColor = FLinearColor(0.93f, 0.91f, 0.87f, 1.f);

	/** Hints, labels, unavailable things. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor DimTextColor = FLinearColor(0.93f, 0.91f, 0.87f, 0.5f);

	/** Titles, selection, highlights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor AccentColor = FLinearColor(0.95f, 0.65f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor WarningColor = FLinearColor(1.f, 0.6f, 0.25f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor GoodColor = FLinearColor(0.55f, 0.9f, 0.45f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor BadColor = FLinearColor(1.f, 0.45f, 0.35f, 1.f);

	/** Windows and panels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor PanelColor = FLinearColor(0.05f, 0.05f, 0.06f, 0.88f);

	/** Inventory and hotbar slots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor SlotColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ButtonColor = FLinearColor(0.16f, 0.15f, 0.14f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ButtonHoverColor = FLinearColor(0.42f, 0.28f, 0.1f, 1.f);

	/** Behind menus (over the game or the menu backdrop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** Body text font (a Font asset, e.g. from a UI pack). Empty = engine default (Roboto). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	TSoftObjectPtr<UFont> Font;

	/** Titles and buttons. Empty = Font. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	TSoftObjectPtr<UFont> TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "8"))
	int32 BodySize = 18;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "8"))
	int32 HeadingSize = 26;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "8"))
	int32 TitleSize = 64;

	/** Optional image for panels (stretched as a 9-slice box), tinted with PanelColor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TSoftObjectPtr<UTexture2D> PanelTexture;

	/** Optional image for inventory slots, tinted with SlotColor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TSoftObjectPtr<UTexture2D> SlotTexture;

	/** Optional image for buttons (9-slice), tinted with ButtonColor / ButtonHoverColor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TSoftObjectPtr<UTexture2D> ButtonTexture;

	/** Optional full-screen image behind the main menu. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TSoftObjectPtr<UTexture2D> MenuBackground;

	/** Optional logo instead of the title text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TSoftObjectPtr<UTexture2D> Logo;

	/** Size of the HUD and inventory (players can scale it further in Settings). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD", meta = (ClampMin = "0.5", ClampMax = "2"))
	float HUDScale = 1.f;
};

/** Project settings for the UI (Project Settings > Game > AI Game Builder UI; written by UITools). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "AI Game Builder UI"))
class AIGAMEBUILDERRUNTIME_API UAGBUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI")
	TSoftObjectPtr<UAGBUITheme> Theme;

	/** Level with the main menu (the game's startup map). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Maps")
	TSoftObjectPtr<UWorld> MainMenuMap;

	/** Level that New Game / Continue open. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Maps")
	TSoftObjectPtr<UWorld> GameMap;

	/** Replace the built-in menus with your own Widget Blueprints (use the AGB Menu functions in them). Empty = built-in. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menus")
	TSoftClassPtr<UUserWidget> MainMenuClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menus")
	TSoftClassPtr<UUserWidget> PauseMenuClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menus")
	TSoftClassPtr<UUserWidget> SettingsMenuClass;

	/** Volume groups for the Audio settings. Sounds without a class use the project's default sound class. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundClass> MasterSoundClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundClass> MusicSoundClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundClass> EffectsSoundClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundClass> AmbientSoundClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundClass> UISoundClass;

	/** Sound mix that carries the volume settings. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundMix> VolumeMix;

	/** The game's sounds, music and effects (AudioFXTools). Empty = placeholders. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<UAGBAudioFXConfig> AudioFX;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** The theme in use (the asset, or built-in defaults). */
	static const UAGBUITheme* GetTheme();
};
