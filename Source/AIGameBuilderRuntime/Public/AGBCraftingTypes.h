#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AGBCraftingTypes.generated.h"

class UAGBItemDefinition;
class UStaticMesh;

/** An item and a count (recipe ingredients and outputs). */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBItemAmount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	TObjectPtr<UAGBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (ClampMin = "1"))
	int32 Count = 1;
};

UENUM(BlueprintType)
enum class EAGBRecipeUnlock : uint8
{
	/** Every player knows it from the start. */
	Default,
	/** Learned the first time the player carries every ingredient. */
	Discover,
	/** Only learned when a game system grants it (quests, recipe items...). */
	Manual,
};

/** A crafting station kind (campfire, workbench, forge): look, warmth, light, fuel. Placed by an item or by tools. */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBStationDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Station")
	FName StationId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	FText DisplayName;

	/** Empty = a placeholder shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0.01"))
	float MeshScale = 1.f;

	/** Burns fuel items (tag "Fuel", stat "BurnSeconds"); works, warms and lights only while burning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	bool bNeedsFuel = false;

	/** Degrees C added nearby while working (campfire ~20). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	float WarmthC = 0.f;

	/** Heat reach, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "1"))
	float HeatRadius = 600.f;

	/** Light while working, candelas (0 = no light). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0"))
	float LightIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	FLinearColor LightColor = FLinearColor(1.f, 0.55f, 0.2f);

	/** How close (cm) a player must be to craft with it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "50"))
	float CraftRange = 400.f;

	FText GetDisplayNameOrId() const { return DisplayName.IsEmpty() ? FText::FromName(StationId) : DisplayName; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AGBStation"), GetFName()); }
};

/** How to make an item: ingredients, outputs, time, the station it needs and how players learn it. */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBRecipeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Recipe")
	FName RecipeId;

	/** Empty = the first output's name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FText DisplayName;

	/** Groups recipes in the menu ("Tools", "Food", "Building"...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FText Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TArray<FAGBItemAmount> Ingredients;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TArray<FAGBItemAmount> Outputs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (ClampMin = "0"))
	float CraftSeconds = 3.f;

	/** Station needed nearby (and burning, if it uses fuel). Empty = craft anywhere by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TObjectPtr<UAGBStationDefinition> Station;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	EAGBRecipeUnlock Unlock = EAGBRecipeUnlock::Default;

	FText GetDisplayNameOrId() const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AGBRecipe"), GetFName()); }
};
