#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "SurvivalTools.generated.h"

/// One vital stat as configured.
USTRUCT(BlueprintType)
struct FSurvivalStatInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString StatId;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float MaxValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float StartValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float ChangePerSecond = 0.f;

	/// For decaying stats: minutes from full to empty (0 = does not decay).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float MinutesToEmpty = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float RegenDelaySeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> RegenRequires;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float DamagePerSecondWhenEmpty = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bShowOnHUD = true;
};

/// Result of the survival tools.
USTRUCT(BlueprintType)
struct FSurvivalResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Survival config asset in use.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString ConfigPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FSurvivalStatInfo> Stats;

	/// All other rules as "Name=Value" lines (temperature, stamina, falling, drinking, death).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Rules;
};

/// Survival rules for AI Game Builder games: vital stats (health, stamina, hunger, thirst and custom ones like oxygen), how fast they drain or regenerate, damage when empty, temperature by altitude with clothing and heat sources, drinking from water bodies, eating (food items' stats restore vitals with the same name), fall damage, death bags and respawn. Stored in a survival config data asset on the player character. Run GameFoundationTools.SetupGameFoundation first.
UCLASS(BlueprintType, Hidden)
class USurvivalTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates the survival config (DA_SurvivalConfig) with the standard survival rules for a difficulty and assigns it to the
	 * player character. Health 100 (regenerates when fed), Stamina 100 (sprint 15/s, regen 10/s), Food empty in 90 min,
	 * Water in 60 min (Easy x0.6, Hard x1.5 drain and starvation damage). RESETS an existing config to the preset.
	 * @param Difficulty Easy, Normal or Hard.
	 * @param Folder Content folder for the asset.
	 * @return The config.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult SetupSurvival(const FString& Difficulty = TEXT("Normal"), const FString& Folder = TEXT("/Game/AIGameBuilder/Core"));

	/**
	 * Reports the survival config: every stat and rule.
	 * @return The config (or the built-in defaults if none is assigned).
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult GetSurvivalConfig();

	/**
	 * Adds or changes one vital stat. Items restore it through a stat of the same name; the HUD shows a bar.
	 * Example (oxygen that drains in 2 min and hurts when empty): SetVitalStat("Oxygen", "Oxygen", 100, -1, 0, -1, -1, 5, "none", "#66CCFF", true)
	 * @param StatId Stat id ("Health", "Stamina", "Hunger", "Thirst" or a new one).
	 * @param DisplayName HUD name, or "auto" (keep / use the id).
	 * @param MaxValue Maximum, or -1 to keep.
	 * @param StartValue Value at spawn, -1 = full.
	 * @param ChangePerSecond Change per second (negative drains, positive regenerates), or 0 for none. Use MinutesToEmpty for drains instead.
	 * @param MinutesToEmpty If > 0: drains from full to empty in this many minutes (overrides ChangePerSecond). -1 = ignore.
	 * @param RegenDelaySeconds Pause in regeneration after the stat drops, or -1 to keep.
	 * @param DamagePerSecondWhenEmpty Health lost per second while empty, or -1 to keep.
	 * @param RegenRequires Comma-separated stats that must be above half for this one to regenerate ("Hunger,Thirst"), "none", or "auto" to keep.
	 * @param ColorHex Bar colour "#RRGGBB", or "auto" to keep.
	 * @param bShowOnHUD Show a bar on the HUD.
	 * @return The updated config.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult SetVitalStat(const FString& StatId, const FString& DisplayName, double MaxValue, double StartValue, double ChangePerSecond,
		double MinutesToEmpty = -1.0, double RegenDelaySeconds = -1.0, double DamagePerSecondWhenEmpty = -1.0, const FString& RegenRequires = TEXT("auto"),
		const FString& ColorHex = TEXT("auto"), bool bShowOnHUD = true);

	/**
	 * Removes a vital stat (not Health).
	 * @param StatId Stat to remove.
	 * @return The updated config.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult RemoveVitalStat(const FString& StatId);

	/**
	 * Sets the temperature rules. Air = base - cooling per 100 m above sea level + heat sources (and later day/night, weather).
	 * Players lose health outside the comfort range; clothing with "Insulation"/"Cooling" stats widens it. -999 keeps a value.
	 * @param BaseTemperatureC Air temperature at sea level (°C).
	 * @param SeaLevelM World height of sea level (m).
	 * @param CoolingPer100m Degrees colder per 100 m of altitude (real air about 0.65).
	 * @param ComfortMinC Coldest comfortable air (°C).
	 * @param ComfortMaxC Hottest comfortable air (°C).
	 * @param ExposureDamagePerDegree Health lost per second per degree outside comfort.
	 * @return The updated config.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult SetTemperatureRules(double BaseTemperatureC = -999.0, double SeaLevelM = -999.0, double CoolingPer100m = -999.0,
		double ComfortMinC = -999.0, double ComfortMaxC = -999.0, double ExposureDamagePerDegree = -999.0);

	/**
	 * Sets stamina costs, drinking, fall damage, death and respawn rules. -1 keeps a value.
	 * @param SprintStaminaPerSecond Stamina used per second of sprinting.
	 * @param JumpStaminaCost Stamina per jump.
	 * @param WaterDrinkAmount Thirst restored per drink from a water body.
	 * @param FallDamageMinSpeed Landing speed (m/s) where falls start to hurt (11 m/s is about a 6 m drop).
	 * @param FallDamagePerSpeed Health lost per m/s above that.
	 * @param RespawnDelaySeconds Seconds before respawning.
	 * @param DropItemsOnDeath 1 = drop everything in a bag, 0 = items are lost, -1 = keep.
	 * @param LootBagLifetimeMinutes Minutes before an unclaimed bag disappears.
	 * @return The updated config.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult SetSurvivalRules(double SprintStaminaPerSecond = -1.0, double JumpStaminaCost = -1.0, double WaterDrinkAmount = -1.0,
		double FallDamageMinSpeed = -1.0, double FallDamagePerSpeed = -1.0, double RespawnDelaySeconds = -1.0, int32 DropItemsOnDeath = -1,
		double LootBagLifetimeMinutes = -1.0);

	/**
	 * Places a placeholder fire that warms the air around it (for testing temperature; real campfires come with crafting). One undo step.
	 * @param XM World X, meters.
	 * @param YM World Y, meters.
	 * @param WarmthC Degrees added at the centre.
	 * @param RadiusM Reach, meters.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Survival")
	static FSurvivalResult SpawnHeatSource(double XM, double YM, double WarmthC = 20.0, double RadiusM = 6.0);
};
