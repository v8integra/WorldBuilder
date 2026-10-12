#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "UITools.generated.h"

/// Result of the UI tools.
USTRUCT(BlueprintType)
struct FUIToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Current UI setup: maps, theme values, menus, volume groups.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Status;

	/// Assets created or changed.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Assets;
};

/// User interface for AI Game Builder games: main menu level (Continue / New Game / Settings / Quit), pause menu (Escape or P in game), settings (graphics, audio volumes, mouse, key rebinding, field of view, HUD size), and one UI theme (colors, fonts, textures) that styles the menus and the in-game HUD. Re-skin = SetUITheme (presets or a UI pack's fonts and textures).
UCLASS(BlueprintType, Hidden)
class UUITools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Sets up the menus: the UI theme asset, volume groups (sound classes + mix), a MainMenu level, and the game level
	 * that New Game / Continue open (the level open in the editor). Safe to run again.
	 * @param GameTitle Title on the main menu (empty = keep / project name).
	 * @param bSetStartupMap Make the main menu the level the packaged game starts in.
	 * @return Status and assets.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|UI")
	static FUIToolsResult SetupUI(const FString& GameTitle = TEXT(""), bool bSetStartupMap = true);

	/**
	 * Changes the look of all menus and the HUD. Empty text = unchanged.
	 * @param Preset Start from a style: "Survival" (amber on charcoal), "Frost", "Forest", "Desert", "SciFi", "Horror"; empty = keep.
	 * @param GameTitle Main menu title.
	 * @param Subtitle Line under the title ("none" clears).
	 * @param TextColor Hex "#RRGGBB" or "#RRGGBBAA".
	 * @param AccentColor Titles, selection, highlights.
	 * @param PanelColor Windows and panels (alpha = see-through).
	 * @param ButtonColor Buttons.
	 * @param ButtonHoverColor Hovered buttons.
	 * @param FontPath Font asset for text, e.g. from a UI pack ("none" = engine font).
	 * @param TitleFontPath Font asset for titles and buttons ("none" = same as text).
	 * @param PanelTexturePath Texture for panels (tinted by PanelColor; "none" clears).
	 * @param SlotTexturePath Texture for inventory slots ("none" clears).
	 * @param ButtonTexturePath Texture for buttons ("none" clears).
	 * @param MenuBackgroundPath Full-screen image behind the main menu ("none" clears).
	 * @param LogoPath Image shown instead of the title text ("none" clears).
	 * @param HUDScale Size of the HUD (0.5-2; 0 = unchanged).
	 * @return The theme now.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|UI")
	static FUIToolsResult SetUITheme(const FString& Preset = TEXT(""), const FString& GameTitle = TEXT(""), const FString& Subtitle = TEXT(""),
		const FString& TextColor = TEXT(""), const FString& AccentColor = TEXT(""), const FString& PanelColor = TEXT(""), const FString& ButtonColor = TEXT(""),
		const FString& ButtonHoverColor = TEXT(""), const FString& FontPath = TEXT(""), const FString& TitleFontPath = TEXT(""),
		const FString& PanelTexturePath = TEXT(""), const FString& SlotTexturePath = TEXT(""), const FString& ButtonTexturePath = TEXT(""),
		const FString& MenuBackgroundPath = TEXT(""), const FString& LogoPath = TEXT(""), double HUDScale = 0.0);

	/**
	 * Uses your own Widget Blueprints instead of the built-in menus (they call the "AI Game Builder|Menu" functions:
	 * ContinueGame, NewGame, OpenSettings, QuitToMainMenu, QuitGame, SaveGame, CloseMenu). Empty = unchanged, "default" = built-in.
	 * @param MainMenuWidget Widget Blueprint path for the main menu.
	 * @param PauseMenuWidget Widget Blueprint path for the pause menu.
	 * @param SettingsWidget Widget Blueprint path for settings.
	 * @return Status.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|UI")
	static FUIToolsResult SetMenuWidgets(const FString& MainMenuWidget = TEXT(""), const FString& PauseMenuWidget = TEXT(""), const FString& SettingsWidget = TEXT(""));

	/**
	 * Shows the UI setup: maps, theme, menus, volume groups, startup map.
	 * @return Status.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|UI")
	static FUIToolsResult GetUIStatus();
};
