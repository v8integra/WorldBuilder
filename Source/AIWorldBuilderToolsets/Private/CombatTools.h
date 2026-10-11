#pragma once

#include "CoreMinimal.h"
#include "ItemTools.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "CombatTools.generated.h"

/// Combat equipment for AI Game Builder games: melee weapons (damage, attack speed, reach, durability), ranged weapons that fire ammunition (bow + arrows: projectile speed, gravity, recoverable arrows), thrown weapons (spears: the item itself is thrown and picked up again), and armor worn in the Head/Chest/Legs/Feet slots (Armor reduces damage as damage x 100 / (100 + armor); Insulation keeps players warm). Tools and weapons lose durability per hit/shot, armor per hit taken; at 0 they break. Creatures are the targets (PvE). Built on ItemTools items.
UCLASS(BlueprintType, Hidden)
class UCombatTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates (or updates) a weapon item: held in the main hand, left click attacks (melee) or fires (ranged/thrown). Saved.
	 * Examples: CreateWeapon("stone_spear", "Stone Spear", "Thrown", 25, 1.0, 0, [], 3, "none", 30) ;
	 *   CreateWeapon("bow", "Bow", "Ranged", 30, 1.2, 150, [], 0, "arrow", 55, 0.5) (the "arrow" item must exist) ;
	 *   CreateWeapon("stone_axe", "Stone Axe", "Melee", 15, 0.7, 120, ["Tool.Axe"], 2.5, "none", 0, 0, "auto", 2) (weapon and tool).
	 * @param ItemId Item id (new or existing; an existing item keeps its mesh, icon and description).
	 * @param DisplayName Name shown to players.
	 * @param WeaponType "Melee", "Ranged" (uses AmmoItemId) or "Thrown" (throws itself).
	 * @param Damage Damage per hit.
	 * @param AttackSeconds Seconds between attacks / shots.
	 * @param Durability Uses before it breaks (0 = never breaks; thrown weapons don't wear).
	 * @param ToolTags Also a tool: harvesting tags, e.g. ["Tool.Axe"]; [] for pure weapons.
	 * @param ReachM Melee reach, meters (2-3.5).
	 * @param AmmoItemId Ranged: the ammunition item id.
	 * @param ProjectileSpeedMs Ranged/thrown: launch speed, m/s (thrown spear ~30, bow ~55, crossbow ~80).
	 * @param AmmoRecoverChance Ranged: 0-1 chance an arrow can be picked up again (thrown weapons: always).
	 * @param MeshPath Static mesh, or "auto" (keep / placeholder).
	 * @param HarvestPower Tool power when ToolTags are set.
	 * @param Weight Weight, kg.
	 * @return The item.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Combat")
	static FGameItemResult CreateWeapon(const FString& ItemId, const FString& DisplayName, const FString& WeaponType, double Damage, double AttackSeconds,
		double Durability, const TArray<FString>& ToolTags, double ReachM = 2.5, const FString& AmmoItemId = TEXT("none"), double ProjectileSpeedMs = 50.0,
		double AmmoRecoverChance = 0.5, const FString& MeshPath = TEXT("auto"), double HarvestPower = 0.0, double Weight = 2.0);

	/**
	 * Creates (or updates) an armor or clothing item worn in an equipment slot. Saved.
	 * Example: CreateArmor("leather_vest", "Leather Vest", "Chest", 15, 5, 200)
	 * @param ItemId Item id.
	 * @param DisplayName Name shown to players.
	 * @param Slot Head, Chest, Legs or Feet.
	 * @param Armor Damage reduction points (leather ~10-15 per piece, iron ~30-40; 100 total halves damage).
	 * @param Insulation Degrees of cold protection (fur ~8, leather ~4).
	 * @param Durability Hits it absorbs before breaking (0 = never).
	 * @param MeshPath Static mesh, or "auto".
	 * @param Weight Weight, kg.
	 * @return The item.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Combat")
	static FGameItemResult CreateArmor(const FString& ItemId, const FString& DisplayName, const FString& Slot, double Armor, double Insulation = 0.0,
		double Durability = 150.0, const FString& MeshPath = TEXT("auto"), double Weight = 2.0);
};
