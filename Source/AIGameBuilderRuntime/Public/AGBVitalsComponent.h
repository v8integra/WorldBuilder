#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "AGBVitalsComponent.generated.h"

class UAGBItemDefinition;
class UAGBSurvivalConfig;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAGBDiedSignature, const FString&, Cause);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAGBDamagedSignature, float, Amount, const FString&, Cause);

/**
 * Vital stats (health, stamina, hunger, thirst and any custom ones from the survival config), air temperature
 * exposure, damage and death. Simulated on the server, replicated to everyone (4 times a second, immediately on
 * damage). Rules come from Config, else the owner character's SurvivalConfig, else the standard survival defaults.
 */
UCLASS(ClassGroup = "AI Game Builder", meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBVitalsComponent();

	static const FName HealthStat;
	static const FName StaminaStat;
	static const FName ThirstStat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Vitals")
	TObjectPtr<UAGBSurvivalConfig> Config;

	/** Server and clients. */
	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Vitals")
	FAGBDiedSignature OnDied;

	/** Server only. */
	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Vitals")
	FAGBDamagedSignature OnDamaged;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	const UAGBSurvivalConfig* GetConfig() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	bool HasStat(FName Stat) const { return FindStatIndex(Stat) != INDEX_NONE; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	float GetValue(FName Stat) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	float GetMaxValue(FName Stat) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	float GetFraction(FName Stat) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	FString GetDeathCause() const { return DeathCause; }

	/** Air temperature where the owner stands (°C). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	float GetAirTemperature() const { return AirTemperatureC; }

	/** Comfortable range after clothing (Insulation / Cooling stats on worn items). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Vitals")
	FVector2D GetComfortRange() const { return FVector2D(ComfortMinC, ComfortMaxC); }

	/** Whether there is stamina to sprint (always true without a Stamina stat). */
	bool CanSprint() const;

	// ---- Server only

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Vitals")
	void ModifyStat(FName Stat, float Delta);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Vitals")
	void ApplyDamage(float Amount, const FString& Cause);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Vitals")
	void Kill(const FString& Cause);

	/** Applies an item's stats that match vital stats (food "Hunger" = 25 restores 25). Returns whether any applied. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Vitals")
	bool ApplyItemEffects(const UAGBItemDefinition* Item);

	/** One drink of water (thirst). Returns false if there is no Thirst stat or it is too soon after the last use. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Vitals")
	bool DrinkWater();

	/** Use cooldown for eating/drinking. */
	bool TryStartUse();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Vitals")
	void ResetStats();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Index-aligned with the config's Stats. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "AI Game Builder|Vitals")
	TArray<float> Values;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(Replicated)
	FString DeathCause;

	UPROPERTY(Replicated)
	float AirTemperatureC = 0.f;

	UPROPERTY(Replicated)
	float ComfortMinC = 0.f;

	UPROPERTY(Replicated)
	float ComfortMaxC = 0.f;

	UFUNCTION()
	void OnRep_Dead();

private:
	int32 FindStatIndex(FName Stat) const;
	void SetValue(int32 Index, float NewValue);
	void Replicate();
	void Die(const FString& Cause);
	void UpdateTemperature(float DeltaTime);

	/** Authoritative values on the server (Values is the replicated copy). */
	TArray<float> ServerValues;
	TArray<double> LastReducedTime;
	double NextReplicateTime = 0.0;
	double LastUseTime = -1000.0;
};
