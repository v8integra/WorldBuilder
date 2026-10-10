#pragma once

#include "CoreMinimal.h"
#include "ItemTools.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "CraftingTools.generated.h"

/// Result of the crafting tools.
USTRUCT(BlueprintType)
struct FCraftingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// One line per recipe: "id: ingredients -> outputs (time, station, unlock)".
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Recipes;

	/// One line per station: "id: name (fuel, warmth, light, item)".
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Stations;
};

/// Crafting for AI Game Builder games: recipes (ingredients, outputs, time, station, how players learn them) and crafting stations (campfire, workbench, forge: mesh, fuel, warmth, light), placed by players from a placeable item or directly in the level. Players craft in the Tab screen (or with E on a station). Fuel = items tagged "Fuel" with a "BurnSeconds" stat. Run ItemTools first for the items involved.
UCLASS(BlueprintType, Hidden)
class UCraftingTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates (or updates) a crafting station kind (DA_Station_<stationId>) and the placeable item that places it. Saved.
	 * Example (campfire): CreateStation("campfire", "Campfire", "auto", 1, true, 20, 6, 60, 4, "auto")
	 * @param StationId Unique id ("campfire", "workbench", "forge").
	 * @param DisplayName Name shown to players.
	 * @param MeshPath Static mesh, "auto" (keep / placeholder block) or "none".
	 * @param MeshScale Mesh scale.
	 * @param bNeedsFuel Burns fuel items; only works, warms and lights while burning (campfire, forge).
	 * @param WarmthC Degrees C added nearby while working (campfire ~20, forge ~15, workbench 0).
	 * @param HeatRadiusM Heat reach, meters.
	 * @param LightIntensity Light while working in candelas (campfire ~60, 0 = none).
	 * @param CraftRangeM How close players must be to craft with it, meters.
	 * @param PlaceableItemId Item that places it: "auto" (item with the same id, created if missing), an existing item id, or "none".
	 * @param Folder Content folder for the station asset.
	 * @return The stations.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Crafting")
	static FCraftingResult CreateStation(const FString& StationId, const FString& DisplayName, const FString& MeshPath = TEXT("auto"), double MeshScale = 1.0,
		bool bNeedsFuel = false, double WarmthC = 0.0, double HeatRadiusM = 6.0, double LightIntensity = 0.0, double CraftRangeM = 4.0,
		const FString& PlaceableItemId = TEXT("auto"), const FString& Folder = TEXT("/Game/AIGameBuilder/Stations"));

	/**
	 * Creates (or updates) a recipe (DA_Recipe_<recipeId>). Saved.
	 * Example: CreateRecipe("stone_axe", "stone_axe", 1, [{"itemId":"wood","count":3},{"itemId":"stone","count":2}], 4, "none", "Default", "auto")
	 * Cooking: station "campfire" (needs fuel). Tier 2 tools: station "workbench".
	 * @param RecipeId Unique id (usually the output item id).
	 * @param OutputItemId Item produced.
	 * @param OutputCount How many per craft.
	 * @param Ingredients Items used per craft: [{"itemId":"wood","count":3}].
	 * @param CraftSeconds Seconds per craft.
	 * @param StationId Station needed nearby (and burning if it uses fuel), or "none" for crafting by hand.
	 * @param Unlock "Default" (known from the start), "Discover" (learned when carrying every ingredient) or "Manual" (granted by game systems).
	 * @param Category Menu group ("Tools", "Food"...), or "auto" from the output item's category.
	 * @param Folder Content folder for new recipe assets.
	 * @return The recipes.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Crafting")
	static FCraftingResult CreateRecipe(const FString& RecipeId, const FString& OutputItemId, int32 OutputCount, const TArray<FGameItemAmount>& Ingredients,
		double CraftSeconds, const FString& StationId = TEXT("none"), const FString& Unlock = TEXT("Default"), const FString& Category = TEXT("auto"),
		const FString& Folder = TEXT("/Game/AIGameBuilder/Recipes"));

	/**
	 * Lists all recipes and crafting stations.
	 * @return Recipes and stations.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Crafting")
	static FCraftingResult ListCrafting();

	/**
	 * Places a crafting station in the level (for a starting camp or testing). One undo step.
	 * @param StationId Station kind.
	 * @param XM World X, meters.
	 * @param YM World Y, meters.
	 * @param YawDeg Facing, degrees.
	 * @param FuelSeconds Fuel it starts with (stations that burn fuel).
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Crafting")
	static FCraftingResult SpawnStation(const FString& StationId, double XM, double YM, double YawDeg = 0.0, double FuelSeconds = 0.0);
};
