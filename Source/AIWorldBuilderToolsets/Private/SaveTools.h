#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "SaveTools.generated.h"

/// Result of the save tools.
USTRUCT(BlueprintType)
struct FSaveToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Save settings of the game mode.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Settings;

	/// Save files: "name (size, date)".
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Saves;
};

/// Saving for AI Game Builder games: the world (clock, weather, buildings with storage, crafting stations, items on the ground, death bags, harvested resources) and every player (position, vitals, inventory with durability, equipment, known recipes, crafting queue, bed) are saved per level, loaded when play starts, autosaved and saved when play ends. Creatures are not saved (spawn zones repopulate). In Play: console "AGBSave", "AGBLoad", "AGBNewWorld".
UCLASS(BlueprintType, Hidden)
class USaveTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Sets the save behaviour on the game mode Blueprint and saves it.
	 * @param SlotName Save file base name ("World"); each level saves to "<SlotName>_<Map>".
	 * @param AutosaveMinutes Minutes between autosaves (0 = off).
	 * @param bLoadOnStart Load the level's save when play starts.
	 * @param bSaveOnEnd Save when play ends.
	 * @return The settings and existing saves.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Saving")
	static FSaveToolsResult SetSaveSettings(const FString& SlotName = TEXT("World"), double AutosaveMinutes = 5.0, bool bLoadOnStart = true, bool bSaveOnEnd = true);

	/**
	 * Lists the save settings and the save files in the project's Saved/SaveGames folder.
	 * @return The settings and saves.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Saving")
	static FSaveToolsResult GetSaveStatus();

	/**
	 * Deletes a save file (the level then starts fresh next time). Ask the user first: this cannot be undone.
	 * @param SaveName File name without extension, e.g. "World_TestWorld".
	 * @return What remains.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Saving")
	static FSaveToolsResult DeleteSave(const FString& SaveName);
};
