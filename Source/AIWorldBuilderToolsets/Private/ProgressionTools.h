#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ProgressionTools.generated.h"

/// Result of the progression tools.
USTRUCT(BlueprintType)
struct FProgressionToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Config summary, attributes, XP table.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Lines;
};

/// RPG progression for AI Game Builder games: levels and XP (from kills, gathering, crafting, building, later quests), attribute points per level, and attributes whose points improve max health, max stamina, armor, melee / ranged damage, harvesting, crafting speed and movement speed. Players spend points on the inventory screen; level and XP show under the hotbar. Console: AGBGiveXP <n>, AGBRespec.
UCLASS(BlueprintType, Hidden)
class UProgressionTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates the progression config (levels, XP rewards, the default attributes Strength, Agility, Vitality,
	 * Intelligence) and gives it to the player character. Safe to run again.
	 * @return The config.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult SetupProgression();

	/**
	 * Level curve. XP from level L to L+1 = baseXP * L ^ exponent. Negative / 0 = unchanged.
	 * @param MaxLevel Highest level.
	 * @param BaseXP XP for level 1 -> 2.
	 * @param Exponent Curve steepness (1 = linear, 1.5 = normal RPG, 2 = steep).
	 * @param PointsPerLevel Attribute points per level.
	 * @param HealthPerLevel Extra max health per level.
	 * @param StaminaPerLevel Extra max stamina per level.
	 * @return The curve and XP table.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult SetLevelCurve(int32 MaxLevel = 0, double BaseXP = 0.0, double Exponent = -1.0, int32 PointsPerLevel = -1,
		double HealthPerLevel = -1.0, double StaminaPerLevel = -1.0);

	/**
	 * Where XP comes from. Negative = unchanged.
	 * @param PerCreatureHealth XP per point of a killed creature's health (creatures can override: SetCreatureXP).
	 * @param PerGathered XP per gathered item.
	 * @param PerCraft XP per craft (plus 1 per second of crafting time).
	 * @param PerBuild XP per building piece or station placed.
	 * @param DeathXPLoss Share of the current level's XP lost on death (0-1).
	 * @return The rewards.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult SetXPRewards(double PerCreatureHealth = -1.0, double PerGathered = -1.0, double PerCraft = -1.0, double PerBuild = -1.0,
		double DeathXPLoss = -1.0);

	/**
	 * Adds or changes an attribute.
	 * @param Effects What each point gives: "MaxHealth+8", "MaxStamina+3", "Armor+1", "MeleeDamage+3%", "RangedDamage+3%", "HarvestPower+2%", "CraftSpeed+4%", "MoveSpeed+1%". [] = keep.
	 * @param AttributeId Id ("Strength").
	 * @param DisplayName Name shown ("" = id / keep).
	 * @param Description One line for players.
	 * @param StartValue Points a new character has (-1 = keep / 5).
	 * @return All attributes.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult SetAttribute(const TArray<FString>& Effects, const FString& AttributeId, const FString& DisplayName = TEXT(""),
		const FString& Description = TEXT(""), int32 StartValue = -1);

	/**
	 * Removes an attribute.
	 * @param AttributeId Id.
	 * @return All attributes.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult RemoveAttribute(const FString& AttributeId);

	/**
	 * XP for killing a creature (0 = from its health).
	 * @param CreatureId Creature id.
	 * @param XP XP reward.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult SetCreatureXP(const FString& CreatureId, int32 XP);

	/**
	 * The progression: curve, XP table, rewards, attributes, and how fast players level from recorded play sessions.
	 * @return Lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Progression")
	static FProgressionToolsResult GetProgression();
};
