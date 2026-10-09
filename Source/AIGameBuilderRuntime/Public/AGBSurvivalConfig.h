#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AGBSurvivalConfig.generated.h"

/** One vital stat (Health, Stamina, Hunger, Thirst, or a custom one like Oxygen or Sanity). */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBStatConfig
{
	GENERATED_BODY()

	/** Stat id. Items restore it through a stat with the same name (food: "Hunger" = 25). "Health" is required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat", meta = (ClampMin = "1"))
	float MaxValue = 100.f;

	/** Value at spawn; negative = MaxValue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	float StartValue = -1.f;

	/** Change per second: negative decays (hunger), positive regenerates (stamina). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	float ChangePerSecond = 0.f;

	/** Positive change pauses this long after the stat was reduced (stamina after sprinting, health after a hit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat", meta = (ClampMin = "0"))
	float RegenDelaySeconds = 0.f;

	/** Positive change only while these stats are above half (health regenerates only when fed and watered). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	TArray<FName> RegenRequires;

	/** Health lost per second while this stat is empty (starving, dehydrated). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat", meta = (ClampMin = "0"))
	float DamagePerSecondWhenEmpty = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	bool bShowOnHUD = true;
};

/**
 * Survival rules for a game (data): vital stats, sprint/jump costs, temperature, drinking, fall damage, death and
 * respawn. Assigned to the player character (SurvivalConfig); the AI's SurvivalTools create and tune it.
 * The class defaults are the standard survival values used when no config is assigned.
 */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBSurvivalConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UAGBSurvivalConfig();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	TArray<FAGBStatConfig> Stats;

	// ---- Stamina

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0"))
	float SprintStaminaPerSecond = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0"))
	float JumpStaminaCost = 8.f;

	// ---- Temperature (°C). Phase 19 adds day/night, weather and biomes on top.

	/** Air temperature at sea level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature")
	float BaseTemperatureC = 18.f;

	/** World height of sea level (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature")
	float SeaLevelM = 0.f;

	/** Temperature drop per 100 m of altitude (real air: about 0.65). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0"))
	float CoolingPer100m = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature")
	float ComfortMinC = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature")
	float ComfortMaxC = 30.f;

	/** Health lost per second per degree outside the comfort range (after clothing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperature", meta = (ClampMin = "0"))
	float ExposureDamagePerDegree = 0.05f;

	// ---- Drinking

	/** Thirst restored per drink from a lake, river or other water body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drinking", meta = (ClampMin = "0"))
	float WaterDrinkAmount = 20.f;

	/** Seconds between eating, drinking or using consumables. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drinking", meta = (ClampMin = "0"))
	float UseCooldownSeconds = 0.75f;

	// ---- Falling

	/** Landing speed (m/s) above which falls hurt (about a 6 m drop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Falling", meta = (ClampMin = "0"))
	float FallDamageMinSpeed = 11.f;

	/** Health lost per m/s above FallDamageMinSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Falling", meta = (ClampMin = "0"))
	float FallDamagePerSpeed = 10.f;

	// ---- Death

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death", meta = (ClampMin = "0"))
	float RespawnDelaySeconds = 5.f;

	/** Drop everything in a bag at death (otherwise items are lost). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death")
	bool bDropItemsOnDeath = true;

	/** Minutes before an unclaimed death bag disappears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death", meta = (ClampMin = "0.1"))
	float LootBagLifetimeMinutes = 30.f;

	const FAGBStatConfig* FindStat(FName Id) const;

	/** Standard survival stats (Health, Stamina, Hunger 90 min, Thirst 60 min) scaled by difficulty (1 = Normal). */
	static TArray<FAGBStatConfig> MakeDefaultStats(float DrainScale = 1.f, float DamageScale = 1.f);
};
