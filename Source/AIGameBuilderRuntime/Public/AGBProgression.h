#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"

#include "AGBProgression.generated.h"

/** What an attribute point improves. Percent targets add per-point percent; the others add flat amounts. */
UENUM(BlueprintType)
enum class EAGBAttributeTarget : uint8
{
	/** Flat: maximum health. */
	MaxHealth,
	/** Flat: maximum stamina. */
	MaxStamina,
	/** Flat: armor (damage reduction). */
	Armor,
	/** Percent: melee damage. */
	MeleeDamage,
	/** Percent: bow / thrown damage. */
	RangedDamage,
	/** Percent: harvesting power (fewer hits per tree or rock). */
	HarvestPower,
	/** Percent: crafting speed. */
	CraftSpeed,
	/** Percent: movement speed. */
	MoveSpeed,
};

USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBAttributeEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression")
	EAGBAttributeTarget Target = EAGBAttributeTarget::MaxHealth;

	/** Per attribute point: flat amount, or percent for percent targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression")
	float PerPoint = 1.f;
};

USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBAttributeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression")
	FText Description;

	/** Points a new character starts with (bonuses count only points above this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression", meta = (ClampMin = "0"))
	int32 StartValue = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression")
	TArray<FAGBAttributeEffect> Effects;
};

/** Levels, XP and attributes of a game (one asset, set on the player character). */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBProgressionConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UAGBProgressionConfig();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Levels", meta = (ClampMin = "1", ClampMax = "500"))
	int32 MaxLevel = 50;

	/** XP from level L to L+1 = BaseXP * L ^ Exponent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Levels", meta = (ClampMin = "1"))
	float BaseXP = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Levels", meta = (ClampMin = "0", ClampMax = "4"))
	float Exponent = 1.5f;

	/** Attribute points given per level up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Levels", meta = (ClampMin = "0"))
	int32 PointsPerLevel = 3;

	/** Extra maximum health and stamina per level (on top of attributes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Levels", meta = (ClampMin = "0"))
	float HealthPerLevel = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Levels", meta = (ClampMin = "0"))
	float StaminaPerLevel = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	TArray<FAGBAttributeConfig> Attributes;

	/** XP per point of a killed creature's health (unless the creature sets its own XP). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP", meta = (ClampMin = "0"))
	float XPPerCreatureHealth = 0.5f;

	/** XP per item gathered from resources. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP", meta = (ClampMin = "0"))
	float XPPerGathered = 1.f;

	/** XP per craft (plus one per second of crafting time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP", meta = (ClampMin = "0"))
	float XPPerCraft = 4.f;

	/** XP per building piece or station placed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP", meta = (ClampMin = "0"))
	float XPPerBuild = 3.f;

	/** Share of the current level's XP lost on death (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP", meta = (ClampMin = "0", ClampMax = "1"))
	float DeathXPLoss = 0.f;

	/** XP needed to go from Level to Level + 1 (0 at max level). */
	int32 GetXPToNext(int32 Level) const;
	const FAGBAttributeConfig* FindAttribute(FName Id) const;

	/** Strength, Agility, Vitality, Intelligence. */
	static TArray<FAGBAttributeConfig> MakeDefaultAttributes();
};

USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Progression")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "Progression")
	int32 Value = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAGBLevelUpSignature, int32, NewLevel);

/**
 * A player's level, XP and attributes (server authority, replicated). Lives on the player state so it survives
 * death and respawns. Other systems ask it for bonuses (GetMultiplier / GetFlat, or the static helpers with a pawn).
 * XP comes from kills, gathering, crafting, building and quests.
 */
UCLASS(ClassGroup = (AIGameBuilder), meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBProgressionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBProgressionComponent();

	/** Levels and attributes; empty = the owner character's ProgressionConfig, else built-in defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Progression")
	TObjectPtr<UAGBProgressionConfig> Config;

	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Progression")
	FAGBLevelUpSignature OnLevelUp;

	const UAGBProgressionConfig* GetConfig() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Progression")
	int32 GetLevel() const { return Level; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Progression")
	int32 GetXP() const { return XP; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Progression")
	int32 GetXPToNext() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Progression")
	int32 GetUnspentPoints() const { return UnspentPoints; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Progression")
	int32 GetAttribute(FName Id) const;

	const TArray<FAGBAttributeValue>& GetAttributes() const { return Attributes; }

	/** 1 + percent bonus for a percent target (e.g. 1.12 = +12% melee damage). */
	float GetMultiplier(EAGBAttributeTarget Target) const;

	/** Flat bonus (MaxHealth, MaxStamina, Armor), including per-level gains. */
	float GetFlat(EAGBAttributeTarget Target) const;

	/** The progression of a pawn, controller or player state (null for creatures). */
	static UAGBProgressionComponent* Find(const AActor* Actor);

	/** Null-safe helpers for any actor (1 / 0 when it has no progression). */
	static float Multiplier(const AActor* Actor, EAGBAttributeTarget Target);
	static float Flat(const AActor* Actor, EAGBAttributeTarget Target);

	/** Server: pushes max health / stamina bonuses into the current body (bRefill: start full, e.g. after respawn). */
	void ApplyBonuses(bool bRefill = false);

	/** Server: gives XP (levels up as needed). Reason is for telemetry ("Kill", "Gather"...). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Progression")
	void AddXP(int32 Amount, const FString& Reason);

	/** Owning client or server: spends an unspent point on an attribute. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Progression")
	void RequestSpendPoint(FName AttributeId);

	/** Server: gives all spent points back. */
	void Respec();

	/** Server: death XP loss. */
	void ApplyDeathPenalty();

	/** Save games. */
	void Restore(int32 InLevel, int32 InXP, int32 InPoints, const TMap<FName, int32>& InAttributes);
	TMap<FName, int32> GetSavedAttributes() const;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION(Server, Reliable)
	void ServerSpendPoint(FName AttributeId);

	UFUNCTION()
	void OnRep_Progress();

	/** Pushes the flat bonuses into the vitals (max health / stamina). */
	void InitAttributes();

	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	int32 Level = 1;

	UPROPERTY(Replicated)
	int32 XP = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	int32 UnspentPoints = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	TArray<FAGBAttributeValue> Attributes;
};
