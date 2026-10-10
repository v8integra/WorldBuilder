#pragma once

#include "CoreMinimal.h"
#include "AGBItemTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ItemTools.generated.h"

/// One item definition.
USTRUCT(BlueprintType)
struct FGameItemInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString ItemId;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString DisplayName;

	/// Content path of the item asset.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Category;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	int32 MaxStack = 1;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float Weight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString EquipSlot;

	/// World/held mesh, empty = placeholder cube.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString MeshPath;

	/// Icon texture, empty = name shown instead.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString IconPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Tags;

	/// "Name=Value".
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Stats;

	/// Grip transform in the hand (translation | rotation | scale).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString HeldOffset;

	/// Animation played on use, empty = character default swing.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString UseAnimation;
};

/// An item and a count, as tool input.
USTRUCT(BlueprintType)
struct FGameItemAmount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIGameBuilder")
	FString ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIGameBuilder")
	int32 Count = 1;
};

/// Result of the item tools.
USTRUCT(BlueprintType)
struct FGameItemResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FGameItemInfo> Items;
};

/// Items for AI Game Builder games: create and edit item definitions (data assets: name, category, stack size, weight, mesh, icon, equip slot, tags, stats), list them, place pickups in the level, and set the player's starting items. Items are picked up with E, shown on the hotbar (1-0) and in the inventory screen (Tab). Run GameFoundationTools.SetupGameFoundation first. Units: meters.
UCLASS(BlueprintType, Hidden)
class UItemTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates an item definition (DA_Item_<itemId>), or updates the item with this id if it exists. Saves it.
	 * Example: CreateItem("stone_axe", "Stone Axe", "Tool", 1, 2.5, "A crude axe for chopping wood.", "auto", "auto", "MainHand", ["Tool.Axe"], [{"name":"Damage","value":8}])
	 * @param ItemId Unique id: lowercase letters, digits and underscores ("wood", "stone_axe"). Used by save games: never rename.
	 * @param DisplayName Name shown to players.
	 * @param Category Resource, Food, Tool, Weapon, Armor, Placeable, Consumable or Misc.
	 * @param MaxStack How many fit in one slot (resources 100, food 20, tools 1 are typical).
	 * @param Weight Weight per item, kg.
	 * @param Description Tooltip text, or "none".
	 * @param MeshPath Static mesh for pickups and holding, "auto" (keep current / placeholder cube) or "none". Skeletal meshes: run MeshConversionTools.ConvertSkeletalToStaticMesh first.
	 * @param IconPath Texture for the inventory icon, "auto" (keep current) or "none" (name shown).
	 * @param EquipSlot None, MainHand (held when selected on the hotbar: tools, weapons, torches), Head, Chest, Legs or Feet.
	 * @param Tags Free-form tags read by game systems, e.g. ["Tool.Axe"], ["Fuel"]. [] for none.
	 * @param Stats Named numbers read by game systems, e.g. [{"name":"Hunger","value":25}]. [] for none.
	 * @param Folder Content folder for new item assets.
	 * @return The item.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Items")
	static FGameItemResult CreateItem(const FString& ItemId, const FString& DisplayName, const FString& Category, int32 MaxStack, double Weight,
		const FString& Description, const FString& MeshPath, const FString& IconPath, const FString& EquipSlot,
		const TArray<FString>& Tags, const TArray<FAGBItemStat>& Stats, const FString& Folder = TEXT("/Game/AIGameBuilder/Items"));

	/**
	 * Lists the project's item definitions.
	 * @param Category Only this category, or "all".
	 * @return The items.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Items")
	static FGameItemResult ListItems(const FString& Category = TEXT("all"));

	/**
	 * Places items on the ground as a pickup (players press E to take them). One undo step.
	 * @param ItemId Item to place.
	 * @param Count How many.
	 * @param XM World X, meters.
	 * @param YM World Y, meters.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Items")
	static FGameItemResult SpawnItemPickup(const FString& ItemId, int32 Count, double XM, double YM);

	/**
	 * Sets the items every player starts with (on the game's player character Blueprint) and saves it. [] = nothing.
	 * Example: SetStartingItems([{"itemId":"torch","count":1}])
	 * @param Items Items and counts.
	 * @return The starting items.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Items")
	static FGameItemResult SetStartingItems(const TArray<FGameItemAmount>& Items);

	/**
	 * Sets how a held (MainHand) item sits in the hand and which animation plays when it is used. The offset is relative to
	 * the hand socket (UE mannequin: hand_r). Tune it in Play: if the tool points the wrong way, rotate by 90/180 degrees on
	 * one axis at a time. Saved.
	 * Example: SetItemHandling("stone_axe", 0, 0, 0, 0, 0, 180, 1, "auto")
	 * @param ItemId Item to change.
	 * @param OffsetXCm Grip offset along the socket X axis, cm.
	 * @param OffsetYCm Grip offset along the socket Y axis, cm.
	 * @param OffsetZCm Grip offset along the socket Z axis, cm.
	 * @param PitchDeg Rotation around the socket Y axis, degrees.
	 * @param YawDeg Rotation around the socket Z axis, degrees.
	 * @param RollDeg Rotation around the socket X axis, degrees.
	 * @param Scale Size multiplier in the hand (on top of the item's mesh scale).
	 * @param AnimationPath Animation (sequence or montage) played on use: chop, mine, swing. "auto" keeps it, "none" uses the character's default swing.
	 * @return The item.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Items")
	static FGameItemResult SetItemHandling(const FString& ItemId, double OffsetXCm, double OffsetYCm, double OffsetZCm, double PitchDeg, double YawDeg,
		double RollDeg, double Scale = 1.0, const FString& AnimationPath = TEXT("auto"));
};
