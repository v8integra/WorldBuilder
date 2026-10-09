#include "AGBVitalsComponent.h"

#include "AGBCharacter.h"
#include "AGBCharacterMovementComponent.h"
#include "AGBClimate.h"
#include "AGBInventoryComponent.h"
#include "AGBItemTypes.h"
#include "AGBSurvivalConfig.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

const FName UAGBVitalsComponent::HealthStat = TEXT("Health");
const FName UAGBVitalsComponent::StaminaStat = TEXT("Stamina");
const FName UAGBVitalsComponent::ThirstStat = TEXT("Thirst");

namespace
{
	constexpr double ReplicateInterval = 0.25;
	const FName InsulationStat = TEXT("Insulation");
	const FName CoolingStat = TEXT("Cooling");
}

UAGBVitalsComponent::UAGBVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

const UAGBSurvivalConfig* UAGBVitalsComponent::GetConfig() const
{
	if (Config)
	{
		return Config;
	}
	if (const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwner()))
	{
		if (Character->SurvivalConfig)
		{
			return Character->SurvivalConfig;
		}
	}
	return GetDefault<UAGBSurvivalConfig>();
}

int32 UAGBVitalsComponent::FindStatIndex(FName Stat) const
{
	return GetConfig()->Stats.IndexOfByPredicate([Stat](const FAGBStatConfig& Entry) { return Entry.Id == Stat; });
}

float UAGBVitalsComponent::GetValue(FName Stat) const
{
	const int32 Index = FindStatIndex(Stat);
	const TArray<float>& Source = (GetOwner() && GetOwner()->HasAuthority()) ? ServerValues : Values;
	return Source.IsValidIndex(Index) ? Source[Index] : 0.f;
}

float UAGBVitalsComponent::GetMaxValue(FName Stat) const
{
	const int32 Index = FindStatIndex(Stat);
	return Index != INDEX_NONE ? GetConfig()->Stats[Index].MaxValue : 0.f;
}

float UAGBVitalsComponent::GetFraction(FName Stat) const
{
	const float Max = GetMaxValue(Stat);
	return Max > 0.f ? GetValue(Stat) / Max : 0.f;
}

bool UAGBVitalsComponent::CanSprint() const
{
	return !bDead && (!HasStat(StaminaStat) || GetValue(StaminaStat) > 1.f);
}

void UAGBVitalsComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		ResetStats();
	}
}

void UAGBVitalsComponent::ResetStats()
{
	const TArray<FAGBStatConfig>& Stats = GetConfig()->Stats;
	ServerValues.SetNum(Stats.Num());
	LastReducedTime.Init(-1000.0, Stats.Num());
	for (int32 Index = 0; Index < Stats.Num(); ++Index)
	{
		ServerValues[Index] = Stats[Index].StartValue < 0.f ? Stats[Index].MaxValue : FMath::Min(Stats[Index].StartValue, Stats[Index].MaxValue);
	}
	bDead = false;
	DeathCause.Reset();
	Replicate();
}

void UAGBVitalsComponent::Replicate()
{
	Values = ServerValues;
	NextReplicateTime = GetWorld()->GetTimeSeconds() + ReplicateInterval;
}

void UAGBVitalsComponent::SetValue(int32 Index, float NewValue)
{
	if (!ServerValues.IsValidIndex(Index))
	{
		return;
	}
	const float Clamped = FMath::Clamp(NewValue, 0.f, GetConfig()->Stats[Index].MaxValue);
	if (Clamped < ServerValues[Index])
	{
		LastReducedTime[Index] = GetWorld()->GetTimeSeconds();
	}
	ServerValues[Index] = Clamped;
}

void UAGBVitalsComponent::ModifyStat(FName Stat, float Delta)
{
	const int32 Index = FindStatIndex(Stat);
	if (bDead || Index == INDEX_NONE || !GetOwner()->HasAuthority())
	{
		return;
	}
	SetValue(Index, ServerValues[Index] + Delta);
	Replicate();
	if (Stat == HealthStat && ServerValues[Index] <= 0.f)
	{
		Die(TEXT("Died"));
	}
}

void UAGBVitalsComponent::ApplyDamage(float Amount, const FString& Cause)
{
	const int32 Index = FindStatIndex(HealthStat);
	if (bDead || Amount <= 0.f || Index == INDEX_NONE || !GetOwner()->HasAuthority())
	{
		return;
	}
	SetValue(Index, ServerValues[Index] - Amount);
	Replicate();
	OnDamaged.Broadcast(Amount, Cause);
	if (ServerValues[Index] <= 0.f)
	{
		Die(Cause);
	}
}

void UAGBVitalsComponent::Kill(const FString& Cause)
{
	if (!bDead && GetOwner()->HasAuthority())
	{
		const int32 Index = FindStatIndex(HealthStat);
		if (Index != INDEX_NONE)
		{
			SetValue(Index, 0.f);
		}
		Die(Cause);
	}
}

void UAGBVitalsComponent::Die(const FString& Cause)
{
	bDead = true;
	DeathCause = Cause;
	Replicate();
	OnDied.Broadcast(Cause);
}

void UAGBVitalsComponent::OnRep_Dead()
{
	if (bDead)
	{
		OnDied.Broadcast(DeathCause);
	}
}

bool UAGBVitalsComponent::ApplyItemEffects(const UAGBItemDefinition* Item)
{
	if (!Item || bDead)
	{
		return false;
	}
	bool bApplied = false;
	for (const FAGBItemStat& Stat : Item->Stats)
	{
		if (HasStat(Stat.Name))
		{
			if (Stat.Name == HealthStat && Stat.Value < 0.f)
			{
				ApplyDamage(-Stat.Value, FString::Printf(TEXT("Ate %s"), *Item->GetDisplayNameOrId().ToString()));
			}
			else
			{
				ModifyStat(Stat.Name, Stat.Value);
			}
			bApplied = true;
		}
	}
	return bApplied;
}

bool UAGBVitalsComponent::TryStartUse()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (bDead || Now - LastUseTime < GetConfig()->UseCooldownSeconds)
	{
		return false;
	}
	LastUseTime = Now;
	return true;
}

bool UAGBVitalsComponent::DrinkWater()
{
	if (!HasStat(ThirstStat) || !TryStartUse())
	{
		return false;
	}
	ModifyStat(ThirstStat, GetConfig()->WaterDrinkAmount);
	return true;
}

void UAGBVitalsComponent::UpdateTemperature(float DeltaTime)
{
	const UAGBSurvivalConfig* Rules = GetConfig();
	const UAGBClimateSubsystem* Climate = GetWorld()->GetSubsystem<UAGBClimateSubsystem>();
	AirTemperatureC = Climate ? Climate->GetTemperatureAt(GetOwner()->GetActorLocation(), Rules) : Rules->BaseTemperatureC;

	// Worn clothing widens the comfortable range.
	float Insulation = 0.f;
	float Cooling = 0.f;
	if (const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwner()))
	{
		for (const FAGBItemStack& Stack : Character->Equipment->GetSlots())
		{
			if (!Stack.IsEmpty())
			{
				Insulation += Stack.Item->GetStat(InsulationStat);
				Cooling += Stack.Item->GetStat(CoolingStat);
			}
		}
	}
	ComfortMinC = Rules->ComfortMinC - Insulation;
	ComfortMaxC = Rules->ComfortMaxC + Cooling;

	const float Outside = FMath::Max3(ComfortMinC - AirTemperatureC, AirTemperatureC - ComfortMaxC, 0.f);
	if (Outside > 0.f && Rules->ExposureDamagePerDegree > 0.f)
	{
		ApplyDamage(Outside * Rules->ExposureDamagePerDegree * DeltaTime, AirTemperatureC < ComfortMinC ? TEXT("Froze") : TEXT("Overheated"));
	}
}

void UAGBVitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bDead)
	{
		return;
	}
	const UAGBSurvivalConfig* Rules = GetConfig();
	const TArray<FAGBStatConfig>& Stats = Rules->Stats;
	if (ServerValues.Num() != Stats.Num())
	{
		ResetStats();
	}
	const double Now = GetWorld()->GetTimeSeconds();

	// Sprinting costs stamina.
	bool bSprinting = false;
	if (const AAGBCharacter* Character = Cast<AAGBCharacter>(Owner))
	{
		bSprinting = Character->IsSprinting() && Character->GetVelocity().Size2D() > 10.0;
	}
	const int32 StaminaIndex = FindStatIndex(StaminaStat);
	if (bSprinting && StaminaIndex != INDEX_NONE)
	{
		SetValue(StaminaIndex, ServerValues[StaminaIndex] - Rules->SprintStaminaPerSecond * DeltaTime);
	}

	float EmptyDamage = 0.f;
	FString EmptyCause;
	for (int32 Index = 0; Index < Stats.Num(); ++Index)
	{
		const FAGBStatConfig& Stat = Stats[Index];
		float Change = Stat.ChangePerSecond * DeltaTime;
		if (Change > 0.f)
		{
			const bool bDelayed = Now - LastReducedTime[Index] < Stat.RegenDelaySeconds;
			const bool bBlocked = Stat.RegenRequires.ContainsByPredicate([this](FName Required) { return HasStat(Required) && GetFraction(Required) <= 0.5f; });
			if (bDelayed || bBlocked || (Index == StaminaIndex && bSprinting))
			{
				Change = 0.f;
			}
			if (Change > 0.f)
			{
				ServerValues[Index] = FMath::Min(ServerValues[Index] + Change, Stat.MaxValue); // Not a reduction.
			}
		}
		else if (Change < 0.f)
		{
			ServerValues[Index] = FMath::Max(ServerValues[Index] + Change, 0.f);
		}

		if (ServerValues[Index] <= 0.f && Stat.DamagePerSecondWhenEmpty > 0.f)
		{
			EmptyDamage += Stat.DamagePerSecondWhenEmpty * DeltaTime;
			EmptyCause = FString::Printf(TEXT("Ran out of %s"), *Stat.DisplayName.ToString().ToLower());
		}
	}
	if (EmptyDamage > 0.f)
	{
		ApplyDamage(EmptyDamage, EmptyCause);
	}
	if (!bDead)
	{
		UpdateTemperature(DeltaTime);
	}
	if (!bDead && Now >= NextReplicateTime)
	{
		Replicate();
	}
}

void UAGBVitalsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAGBVitalsComponent, Values);
	DOREPLIFETIME(UAGBVitalsComponent, bDead);
	DOREPLIFETIME(UAGBVitalsComponent, DeathCause);
	DOREPLIFETIME(UAGBVitalsComponent, AirTemperatureC);
	DOREPLIFETIME(UAGBVitalsComponent, ComfortMinC);
	DOREPLIFETIME(UAGBVitalsComponent, ComfortMaxC);
}
