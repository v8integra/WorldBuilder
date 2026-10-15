#include "AGBProgression.h"

#include "AGBAudioFX.h"
#include "AGBCharacter.h"
#include "AGBMultiplayer.h"
#include "GameFramework/Controller.h"
#include "AGBPlaytest.h"
#include "AGBVitalsComponent.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	FAGBAttributeConfig MakeAttribute(const TCHAR* Id, const FText& Name, const FText& Description, std::initializer_list<FAGBAttributeEffect> Effects)
	{
		FAGBAttributeConfig Attribute;
		Attribute.Id = Id;
		Attribute.DisplayName = Name;
		Attribute.Description = Description;
		Attribute.Effects = Effects;
		return Attribute;
	}

	FAGBAttributeEffect Effect(EAGBAttributeTarget Target, float PerPoint)
	{
		FAGBAttributeEffect Out;
		Out.Target = Target;
		Out.PerPoint = PerPoint;
		return Out;
	}

	bool IsPercent(EAGBAttributeTarget Target)
	{
		return Target != EAGBAttributeTarget::MaxHealth && Target != EAGBAttributeTarget::MaxStamina && Target != EAGBAttributeTarget::Armor;
	}
}

// ---------------------------------------------------------------- config

UAGBProgressionConfig::UAGBProgressionConfig()
{
	Attributes = MakeDefaultAttributes();
}

TArray<FAGBAttributeConfig> UAGBProgressionConfig::MakeDefaultAttributes()
{
	using T = EAGBAttributeTarget;
	return {
		MakeAttribute(TEXT("Strength"), LOCTEXT("Strength", "Strength"), LOCTEXT("StrengthDesc", "Hit harder and gather faster."),
			{ Effect(T::MeleeDamage, 3.f), Effect(T::HarvestPower, 2.f) }),
		MakeAttribute(TEXT("Agility"), LOCTEXT("Agility", "Agility"), LOCTEXT("AgilityDesc", "Move faster, shoot better, more stamina."),
			{ Effect(T::MoveSpeed, 1.f), Effect(T::RangedDamage, 3.f), Effect(T::MaxStamina, 3.f) }),
		MakeAttribute(TEXT("Vitality"), LOCTEXT("Vitality", "Vitality"), LOCTEXT("VitalityDesc", "More health, tougher skin."),
			{ Effect(T::MaxHealth, 8.f), Effect(T::Armor, 1.f) }),
		MakeAttribute(TEXT("Intelligence"), LOCTEXT("Intelligence", "Intelligence"), LOCTEXT("IntelligenceDesc", "Craft faster."),
			{ Effect(T::CraftSpeed, 4.f) }),
	};
}

int32 UAGBProgressionConfig::GetXPToNext(int32 InLevel) const
{
	if (InLevel >= MaxLevel)
	{
		return 0;
	}
	return FMath::Max(1, FMath::RoundToInt(BaseXP * FMath::Pow(static_cast<float>(FMath::Max(1, InLevel)), Exponent)));
}

const FAGBAttributeConfig* UAGBProgressionConfig::FindAttribute(FName Id) const
{
	return Attributes.FindByPredicate([Id](const FAGBAttributeConfig& Attribute) { return Attribute.Id == Id; });
}

// ---------------------------------------------------------------- component

UAGBProgressionComponent::UAGBProgressionComponent()
{
	SetIsReplicatedByDefault(true);
}

const UAGBProgressionConfig* UAGBProgressionComponent::GetConfig() const
{
	if (Config)
	{
		return Config;
	}
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	if (const AAGBCharacter* Character = State ? Cast<AAGBCharacter>(State->GetPawn()) : Cast<AAGBCharacter>(GetOwner()))
	{
		if (Character->ProgressionConfig)
		{
			return Character->ProgressionConfig;
		}
	}
	return GetDefault<UAGBProgressionConfig>();
}

void UAGBProgressionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		InitAttributes();
		ApplyBonuses();
	}
}

void UAGBProgressionComponent::InitAttributes()
{
	// Every configured attribute present (new attributes start at their start value).
	for (const FAGBAttributeConfig& Attribute : GetConfig()->Attributes)
	{
		if (!Attributes.ContainsByPredicate([&Attribute](const FAGBAttributeValue& Value) { return Value.Id == Attribute.Id; }))
		{
			Attributes.Add({ Attribute.Id, Attribute.StartValue });
		}
	}
	Attributes.RemoveAll([this](const FAGBAttributeValue& Value) { return !GetConfig()->FindAttribute(Value.Id); });
}

void UAGBProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAGBProgressionComponent, Level);
	DOREPLIFETIME_CONDITION(UAGBProgressionComponent, XP, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAGBProgressionComponent, UnspentPoints, COND_OwnerOnly);
	DOREPLIFETIME(UAGBProgressionComponent, Attributes);
}

void UAGBProgressionComponent::OnRep_Progress()
{
}

int32 UAGBProgressionComponent::GetXPToNext() const
{
	return GetConfig()->GetXPToNext(Level);
}

int32 UAGBProgressionComponent::GetAttribute(FName Id) const
{
	const FAGBAttributeValue* Found = Attributes.FindByPredicate([Id](const FAGBAttributeValue& Value) { return Value.Id == Id; });
	return Found ? Found->Value : 0;
}

float UAGBProgressionComponent::GetMultiplier(EAGBAttributeTarget Target) const
{
	if (!IsPercent(Target))
	{
		return 1.f;
	}
	float Percent = 0.f;
	for (const FAGBAttributeValue& Value : Attributes)
	{
		const FAGBAttributeConfig* Attribute = GetConfig()->FindAttribute(Value.Id);
		for (const FAGBAttributeEffect& Effect : Attribute ? Attribute->Effects : TArray<FAGBAttributeEffect>())
		{
			if (Effect.Target == Target)
			{
				Percent += Effect.PerPoint * FMath::Max(0, Value.Value - Attribute->StartValue);
			}
		}
	}
	return FMath::Max(0.1f, 1.f + Percent / 100.f);
}

float UAGBProgressionComponent::GetFlat(EAGBAttributeTarget Target) const
{
	if (IsPercent(Target))
	{
		return 0.f;
	}
	const UAGBProgressionConfig* Rules = GetConfig();
	float Total = 0.f;
	for (const FAGBAttributeValue& Value : Attributes)
	{
		const FAGBAttributeConfig* Attribute = Rules->FindAttribute(Value.Id);
		for (const FAGBAttributeEffect& Effect : Attribute ? Attribute->Effects : TArray<FAGBAttributeEffect>())
		{
			if (Effect.Target == Target)
			{
				Total += Effect.PerPoint * FMath::Max(0, Value.Value - Attribute->StartValue);
			}
		}
	}
	if (Target == EAGBAttributeTarget::MaxHealth)
	{
		Total += Rules->HealthPerLevel * (Level - 1);
	}
	else if (Target == EAGBAttributeTarget::MaxStamina)
	{
		Total += Rules->StaminaPerLevel * (Level - 1);
	}
	return Total;
}

UAGBProgressionComponent* UAGBProgressionComponent::Find(const AActor* Actor)
{
	const APlayerState* State = Cast<APlayerState>(Actor);
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		State = Pawn->GetPlayerState();
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		State = Controller->PlayerState;
	}
	return State ? State->FindComponentByClass<UAGBProgressionComponent>() : nullptr;
}

float UAGBProgressionComponent::Multiplier(const AActor* Actor, EAGBAttributeTarget Target)
{
	const UAGBProgressionComponent* Progression = Find(Actor);
	return Progression ? Progression->GetMultiplier(Target) : 1.f;
}

float UAGBProgressionComponent::Flat(const AActor* Actor, EAGBAttributeTarget Target)
{
	const UAGBProgressionComponent* Progression = Find(Actor);
	return Progression ? Progression->GetFlat(Target) : 0.f;
}

void UAGBProgressionComponent::ApplyBonuses(bool bRefill)
{
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	APawn* Body = State ? State->GetPawn() : nullptr;
	UAGBVitalsComponent* Vitals = Body ? Body->FindComponentByClass<UAGBVitalsComponent>() : nullptr;
	if (!Vitals || !GetOwner()->HasAuthority())
	{
		return;
	}
	Vitals->SetMaxBonus(UAGBVitalsComponent::HealthStat, GetFlat(EAGBAttributeTarget::MaxHealth));
	Vitals->SetMaxBonus(UAGBVitalsComponent::StaminaStat, GetFlat(EAGBAttributeTarget::MaxStamina));
	if (bRefill)
	{
		Vitals->ModifyStat(UAGBVitalsComponent::HealthStat, 100000.f);
		Vitals->ModifyStat(UAGBVitalsComponent::StaminaStat, 100000.f);
	}
}

void UAGBProgressionComponent::AddXP(int32 Amount, const FString& Reason)
{
	if (Amount <= 0 || !GetOwner()->HasAuthority())
	{
		return;
	}
	const UAGBProgressionConfig* Rules = GetConfig();
	if (Level >= Rules->MaxLevel)
	{
		return;
	}
	XP += Amount;
	if (UAGBTelemetrySubsystem* Telemetry = UAGBTelemetrySubsystem::Get(this))
	{
		Telemetry->Record(TEXT("XP"), Reason, static_cast<float>(Amount));
	}
	const int32 StartLevel = Level;
	while (Level < Rules->MaxLevel && XP >= Rules->GetXPToNext(Level))
	{
		XP -= Rules->GetXPToNext(Level);
		++Level;
		UnspentPoints += Rules->PointsPerLevel;
	}
	if (Level >= Rules->MaxLevel)
	{
		XP = 0;
	}
	if (Level != StartLevel)
	{
		// Level up: health and stamina refill.
		ApplyBonuses(/*bRefill=*/true);
		const APlayerState* State = Cast<APlayerState>(GetOwner());
		AAGBCharacter* Character = State ? Cast<AAGBCharacter>(State->GetPawn()) : nullptr;
		if (Character)
		{
			Character->ClientShowMessage(FText::Format(LOCTEXT("LevelUp", "Level {0}! {1} attribute point(s) to spend (inventory screen)."),
				Level, UnspentPoints), false);
			Character->MulticastPlayCue(TEXT("Event.LevelUp"), Character->GetActorLocation());
		}
		if (UAGBTelemetrySubsystem* Telemetry = UAGBTelemetrySubsystem::Get(this))
		{
			Telemetry->Record(TEXT("LevelUps"), FString::Printf(TEXT("level_%d"), Level));
			Telemetry->Milestone(FString::Printf(TEXT("level_%d"), Level));
		}
		OnLevelUp.Broadcast(Level);
	}
}

void UAGBProgressionComponent::RequestSpendPoint(FName AttributeId)
{
	if (GetOwner()->HasAuthority())
	{
		ServerSpendPoint_Implementation(AttributeId);
	}
	else
	{
		ServerSpendPoint(AttributeId);
	}
}

void UAGBProgressionComponent::ServerSpendPoint_Implementation(FName AttributeId)
{
	FAGBAttributeValue* Value = Attributes.FindByPredicate([AttributeId](const FAGBAttributeValue& Entry) { return Entry.Id == AttributeId; });
	if (!Value || UnspentPoints <= 0)
	{
		return;
	}
	++Value->Value;
	--UnspentPoints;
	ApplyBonuses();
}

void UAGBProgressionComponent::Respec()
{
	for (FAGBAttributeValue& Value : Attributes)
	{
		const FAGBAttributeConfig* Attribute = GetConfig()->FindAttribute(Value.Id);
		const int32 Start = Attribute ? Attribute->StartValue : 0;
		UnspentPoints += FMath::Max(0, Value.Value - Start);
		Value.Value = Start;
	}
	ApplyBonuses();
}

void UAGBProgressionComponent::ApplyDeathPenalty()
{
	const float Loss = GetConfig()->DeathXPLoss;
	if (Loss > 0.f && GetOwner()->HasAuthority())
	{
		XP = FMath::Max(0, XP - FMath::RoundToInt(GetXPToNext() * Loss));
	}
}

void UAGBProgressionComponent::Restore(int32 InLevel, int32 InXP, int32 InPoints, const TMap<FName, int32>& InAttributes)
{
	Level = FMath::Clamp(InLevel, 1, GetConfig()->MaxLevel);
	XP = FMath::Max(0, InXP);
	UnspentPoints = FMath::Max(0, InPoints);
	for (FAGBAttributeValue& Value : Attributes)
	{
		if (const int32* Saved = InAttributes.Find(Value.Id))
		{
			Value.Value = FMath::Max(0, *Saved);
		}
	}
	ApplyBonuses();
}

TMap<FName, int32> UAGBProgressionComponent::GetSavedAttributes() const
{
	TMap<FName, int32> Out;
	for (const FAGBAttributeValue& Value : Attributes)
	{
		Out.Add(Value.Id, Value.Value);
	}
	return Out;
}

#undef LOCTEXT_NAMESPACE
