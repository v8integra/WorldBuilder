#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AGBItemTypes.generated.h"

class UStaticMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class EAGBItemCategory : uint8
{
	Resource,
	Food,
	Tool,
	Weapon,
	Armor,
	Placeable,
	Consumable,
	Misc,
};

/** Where an item can be equipped. MainHand items are held when selected on the hotbar. */
UENUM(BlueprintType)
enum class EAGBEquipSlot : uint8
{
	None,
	MainHand,
	Head,
	Chest,
	Legs,
	Feet,
};

/** A named number on an item, read by game systems ("Hunger" = 25 on food, "Damage" = 12 on a weapon...). */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBItemStat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float Value = 0.f;
};

/**
 * One kind of item (data, no code): name, icon, world mesh, stacking, weight, equip slot, tags and stats.
 * Created by the AI's ItemTools as DA_Item_<ItemId> assets; designers can edit them like any data asset.
 */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Unique, stable id used by tools and save games ("stone_axe"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Item")
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Item")
	EAGBItemCategory Category = EAGBItemCategory::Misc;

	/** How many fit in one inventory slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "1"))
	int32 MaxStack = 1;

	/** Weight per item (kg). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "0"))
	float Weight = 0.f;

	/** Inventory icon. Empty = the item's name is shown instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Visuals")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Mesh for pickups in the world and for holding. Empty = a small placeholder cube. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Visuals")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Visuals", meta = (ClampMin = "0.01"))
	float WorldMeshScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Equip")
	EAGBEquipSlot EquipSlot = EAGBEquipSlot::None;

	/** Offset of the mesh in the hand socket when held (MainHand items). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Equip")
	FTransform HeldOffset;

	/** Free-form tags for game systems ("Tool.Axe", "Fuel"...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FName> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FAGBItemStat> Stats;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	float GetStat(FName StatName, float DefaultValue = 0.f) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	bool HasTag(FName Tag) const { return Tags.Contains(Tag); }

	/** DisplayName, or the ItemId if no name is set. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	FText GetDisplayNameOrId() const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Primary asset type of item definitions (Asset Manager). */
	static const FPrimaryAssetType PrimaryAssetType;
};

/** A number of one item, as stored in an inventory slot or a pickup. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBItemStack
{
	GENERATED_BODY()

	FAGBItemStack() = default;
	FAGBItemStack(UAGBItemDefinition* InItem, int32 InCount) : Item(InItem), Count(InCount) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TObjectPtr<UAGBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	int32 Count = 0;

	bool IsEmpty() const { return Item == nullptr || Count <= 0; }
	void Clear() { Item = nullptr; Count = 0; }

	bool operator==(const FAGBItemStack& Other) const { return Item == Other.Item && Count == Other.Count; }
};
