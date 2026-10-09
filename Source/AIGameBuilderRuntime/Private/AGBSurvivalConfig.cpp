#include "AGBSurvivalConfig.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

UAGBSurvivalConfig::UAGBSurvivalConfig()
{
	Stats = MakeDefaultStats();
}

const FAGBStatConfig* UAGBSurvivalConfig::FindStat(FName Id) const
{
	return Stats.FindByPredicate([Id](const FAGBStatConfig& Stat) { return Stat.Id == Id; });
}

TArray<FAGBStatConfig> UAGBSurvivalConfig::MakeDefaultStats(float DrainScale, float DamageScale)
{
	TArray<FAGBStatConfig> Result;

	FAGBStatConfig& Health = Result.AddDefaulted_GetRef();
	Health.Id = TEXT("Health");
	Health.DisplayName = LOCTEXT("Health", "Health");
	Health.ChangePerSecond = 0.5f;
	Health.RegenDelaySeconds = 10.f;
	Health.RegenRequires = { TEXT("Hunger"), TEXT("Thirst") };
	Health.Color = FLinearColor(0.8f, 0.15f, 0.15f);

	FAGBStatConfig& Stamina = Result.AddDefaulted_GetRef();
	Stamina.Id = TEXT("Stamina");
	Stamina.DisplayName = LOCTEXT("Stamina", "Stamina");
	Stamina.ChangePerSecond = 10.f;
	Stamina.RegenDelaySeconds = 1.f;
	Stamina.Color = FLinearColor(0.85f, 0.8f, 0.2f);

	FAGBStatConfig& Hunger = Result.AddDefaulted_GetRef();
	Hunger.Id = TEXT("Hunger");
	Hunger.DisplayName = LOCTEXT("Hunger", "Food");
	Hunger.ChangePerSecond = -100.f / (90.f * 60.f) * DrainScale; // Empty in 90 minutes.
	Hunger.DamagePerSecondWhenEmpty = 1.f * DamageScale;
	Hunger.Color = FLinearColor(0.85f, 0.5f, 0.15f);

	FAGBStatConfig& Thirst = Result.AddDefaulted_GetRef();
	Thirst.Id = TEXT("Thirst");
	Thirst.DisplayName = LOCTEXT("Thirst", "Water");
	Thirst.ChangePerSecond = -100.f / (60.f * 60.f) * DrainScale; // Empty in 60 minutes.
	Thirst.DamagePerSecondWhenEmpty = 1.f * DamageScale;
	Thirst.Color = FLinearColor(0.2f, 0.5f, 0.9f);

	return Result;
}

#undef LOCTEXT_NAMESPACE
