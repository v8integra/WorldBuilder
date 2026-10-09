#include "SurvivalTools.h"

#include "GameToolUtils.h"

#include "AGBCharacter.h"
#include "AGBClimate.h"
#include "AGBSurvivalConfig.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderSurvival"

using namespace AIWorldBuilder;

namespace
{
	constexpr double KeepTemperature = -999.0;
	const FName HealthId = TEXT("Health");

	FSurvivalResult Fail(const FString& Message)
	{
		FSurvivalResult R;
		R.Message = Message;
		return R;
	}

	AAGBCharacter* GetCharacterDefaults(UBlueprint*& OutBlueprint, FString& OutError)
	{
		OutBlueprint = GameToolUtils::GetCharacterBlueprint(GetEditorWorld());
		if (!OutBlueprint || !OutBlueprint->GeneratedClass)
		{
			OutError = TEXT("The game does not use an AI Game Builder character Blueprint yet: run GameFoundationTools.SetupGameFoundation first.");
			return nullptr;
		}
		return CastChecked<AAGBCharacter>(OutBlueprint->GeneratedClass->GetDefaultObject());
	}

	/** The character's config, or nullptr with an error telling the AI to run SetupSurvival. */
	UAGBSurvivalConfig* GetEditableConfig(FString& OutError)
	{
		UBlueprint* Blueprint = nullptr;
		AAGBCharacter* Character = GetCharacterDefaults(Blueprint, OutError);
		if (Character && !Character->SurvivalConfig)
		{
			OutError = TEXT("No survival config yet: run SetupSurvival first.");
		}
		return Character ? Character->SurvivalConfig.Get() : nullptr;
	}

	void Save(UObject* Asset)
	{
		Asset->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, /*bOnlyDirty=*/false);
	}

	FSurvivalResult Describe(const UAGBSurvivalConfig* Config, const FString& Message)
	{
		FSurvivalResult R;
		R.bSuccess = true;
		R.ConfigPath = Config->HasAnyFlags(RF_ClassDefaultObject) ? FString(TEXT("(built-in defaults: no config assigned)")) : Config->GetPathName();
		for (const FAGBStatConfig& Stat : Config->Stats)
		{
			FSurvivalStatInfo& Info = R.Stats.AddDefaulted_GetRef();
			Info.StatId = Stat.Id.ToString();
			Info.DisplayName = Stat.DisplayName.ToString();
			Info.MaxValue = Stat.MaxValue;
			Info.StartValue = Stat.StartValue < 0.f ? Stat.MaxValue : Stat.StartValue;
			Info.ChangePerSecond = Stat.ChangePerSecond;
			Info.MinutesToEmpty = Stat.ChangePerSecond < 0.f ? Stat.MaxValue / -Stat.ChangePerSecond / 60.f : 0.f;
			Info.RegenDelaySeconds = Stat.RegenDelaySeconds;
			for (const FName& Required : Stat.RegenRequires)
			{
				Info.RegenRequires.Add(Required.ToString());
			}
			Info.DamagePerSecondWhenEmpty = Stat.DamagePerSecondWhenEmpty;
			Info.bShowOnHUD = Stat.bShowOnHUD;
		}
		auto Rule = [&R](const TCHAR* Name, double Value) { R.Rules.Add(FString::Printf(TEXT("%s=%g"), Name, Value)); };
		Rule(TEXT("SprintStaminaPerSecond"), Config->SprintStaminaPerSecond);
		Rule(TEXT("JumpStaminaCost"), Config->JumpStaminaCost);
		Rule(TEXT("BaseTemperatureC"), Config->BaseTemperatureC);
		Rule(TEXT("SeaLevelM"), Config->SeaLevelM);
		Rule(TEXT("CoolingPer100m"), Config->CoolingPer100m);
		Rule(TEXT("ComfortMinC"), Config->ComfortMinC);
		Rule(TEXT("ComfortMaxC"), Config->ComfortMaxC);
		Rule(TEXT("ExposureDamagePerDegree"), Config->ExposureDamagePerDegree);
		Rule(TEXT("WaterDrinkAmount"), Config->WaterDrinkAmount);
		Rule(TEXT("UseCooldownSeconds"), Config->UseCooldownSeconds);
		Rule(TEXT("FallDamageMinSpeed"), Config->FallDamageMinSpeed);
		Rule(TEXT("FallDamagePerSpeed"), Config->FallDamagePerSpeed);
		Rule(TEXT("RespawnDelaySeconds"), Config->RespawnDelaySeconds);
		Rule(TEXT("DropItemsOnDeath"), Config->bDropItemsOnDeath ? 1.0 : 0.0);
		Rule(TEXT("LootBagLifetimeMinutes"), Config->LootBagLifetimeMinutes);
		R.Message = Message;
		return R;
	}

	bool ParseHexColor(const FString& Hex, FLinearColor& OutColor)
	{
		FString Digits = Hex.TrimStartAndEnd();
		Digits.RemoveFromStart(TEXT("#"));
		if (Digits.Len() != 6)
		{
			return false;
		}
		for (const TCHAR Char : Digits)
		{
			if (!FChar::IsHexDigit(Char))
			{
				return false;
			}
		}
		OutColor = FLinearColor(FColor::FromHex(Digits));
		return true;
	}
}

FString USurvivalTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FSurvivalResult USurvivalTools::SetupSurvival(const FString& Difficulty, const FString& Folder)
{
	float DrainScale = 1.f;
	float DamageScale = 1.f;
	if (Difficulty.Equals(TEXT("Easy"), ESearchCase::IgnoreCase))
	{
		DrainScale = DamageScale = 0.6f;
	}
	else if (Difficulty.Equals(TEXT("Hard"), ESearchCase::IgnoreCase))
	{
		DrainScale = DamageScale = 1.5f;
	}
	else if (!Difficulty.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
	{
		return Fail(FString::Printf(TEXT("Unknown difficulty '%s': use Easy, Normal or Hard."), *Difficulty));
	}

	FString Error;
	UBlueprint* Blueprint = nullptr;
	AAGBCharacter* Character = GetCharacterDefaults(Blueprint, Error);
	if (!Character)
	{
		return Fail(Error);
	}

	UAGBSurvivalConfig* Config = Character->SurvivalConfig;
	if (!Config)
	{
		FString Root = Folder;
		Root.RemoveFromEnd(TEXT("/"));
		if (!Root.StartsWith(TEXT("/Game")))
		{
			return Fail(FString::Printf(TEXT("Folder must be under /Game (got '%s')."), *Folder));
		}
		const FString AssetName = TEXT("DA_SurvivalConfig");
		const FString ObjectPath = Root / AssetName + TEXT(".") + AssetName;
		Config = FindObject<UAGBSurvivalConfig>(nullptr, *ObjectPath);
		if (!Config && FPackageName::DoesPackageExist(Root / AssetName))
		{
			Config = LoadObject<UAGBSurvivalConfig>(nullptr, *ObjectPath);
		}
		if (!Config)
		{
			UPackage* Package = CreatePackage(*(Root / AssetName));
			Config = NewObject<UAGBSurvivalConfig>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Config);
		}
		Character->Modify();
		Character->SurvivalConfig = Config;
		Save(Blueprint);
	}

	// Reset everything to the preset.
	Config->Modify();
	UAGBSurvivalConfig* Defaults = GetMutableDefault<UAGBSurvivalConfig>();
	for (TFieldIterator<FProperty> It(UAGBSurvivalConfig::StaticClass()); It; ++It)
	{
		if (It->GetOwnerClass() == UAGBSurvivalConfig::StaticClass())
		{
			It->CopyCompleteValue_InContainer(Config, Defaults);
		}
	}
	Config->Stats = UAGBSurvivalConfig::MakeDefaultStats(DrainScale, DamageScale);
	Save(Config);
	return Describe(Config, FString::Printf(TEXT("Survival rules set to %s and assigned to %s. Tune with SetVitalStat, SetTemperatureRules and SetSurvivalRules."),
		*Difficulty, *Blueprint->GetName()));
}

FSurvivalResult USurvivalTools::GetSurvivalConfig()
{
	FString Error;
	UBlueprint* Blueprint = nullptr;
	const AAGBCharacter* Character = GetCharacterDefaults(Blueprint, Error);
	if (!Character)
	{
		return Fail(Error);
	}
	const UAGBSurvivalConfig* Config = Character->SurvivalConfig ? Character->SurvivalConfig.Get() : GetDefault<UAGBSurvivalConfig>();
	return Describe(Config, Character->SurvivalConfig ? FString(TEXT("Current survival rules.")) : FString(TEXT("No config assigned: the game uses the built-in Normal rules. Run SetupSurvival to make them editable.")));
}

FSurvivalResult USurvivalTools::SetVitalStat(const FString& StatId, const FString& DisplayName, double MaxValue, double StartValue, double ChangePerSecond,
	double MinutesToEmpty, double RegenDelaySeconds, double DamagePerSecondWhenEmpty, const FString& RegenRequires, const FString& ColorHex, bool bShowOnHUD)
{
	FString Error;
	UAGBSurvivalConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	const FString Id = StatId.TrimStartAndEnd();
	if (Id.IsEmpty() || Id.Contains(TEXT(" ")))
	{
		return Fail(TEXT("statId must be one word, e.g. \"Oxygen\"."));
	}
	FLinearColor Color = FLinearColor::White;
	const bool bSetColor = !ColorHex.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
	if (bSetColor && !ParseHexColor(ColorHex, Color))
	{
		return Fail(FString::Printf(TEXT("colorHex '%s' is not #RRGGBB."), *ColorHex));
	}

	Config->Modify();
	FAGBStatConfig* Stat = Config->Stats.FindByPredicate([&Id](const FAGBStatConfig& Entry) { return Entry.Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
	const bool bNew = Stat == nullptr;
	if (bNew)
	{
		Stat = &Config->Stats.AddDefaulted_GetRef();
		Stat->Id = FName(*Id);
		Stat->DisplayName = FText::FromString(Id);
	}
	if (!DisplayName.Equals(TEXT("auto"), ESearchCase::IgnoreCase) && !DisplayName.IsEmpty())
	{
		Stat->DisplayName = FText::FromString(DisplayName);
	}
	if (MaxValue > 0.0)
	{
		Stat->MaxValue = static_cast<float>(MaxValue);
	}
	Stat->StartValue = static_cast<float>(StartValue);
	Stat->ChangePerSecond = MinutesToEmpty > 0.0 ? -Stat->MaxValue / static_cast<float>(MinutesToEmpty * 60.0) : static_cast<float>(ChangePerSecond);
	if (RegenDelaySeconds >= 0.0)
	{
		Stat->RegenDelaySeconds = static_cast<float>(RegenDelaySeconds);
	}
	if (DamagePerSecondWhenEmpty >= 0.0)
	{
		Stat->DamagePerSecondWhenEmpty = static_cast<float>(DamagePerSecondWhenEmpty);
	}
	if (RegenRequires.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Stat->RegenRequires.Reset();
	}
	else if (!RegenRequires.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		TArray<FString> Parts;
		RegenRequires.ParseIntoArray(Parts, TEXT(","));
		Stat->RegenRequires.Reset();
		for (const FString& Part : Parts)
		{
			if (!Part.TrimStartAndEnd().IsEmpty())
			{
				Stat->RegenRequires.Add(FName(*Part.TrimStartAndEnd()));
			}
		}
	}
	if (bSetColor)
	{
		Stat->Color = Color;
	}
	Stat->bShowOnHUD = bShowOnHUD;
	Save(Config);
	return Describe(Config, FString::Printf(TEXT("%s stat '%s'. Items restore it with a stat named '%s'."), bNew ? TEXT("Added") : TEXT("Updated"), *Id, *Stat->Id.ToString()));
}

FSurvivalResult USurvivalTools::RemoveVitalStat(const FString& StatId)
{
	FString Error;
	UAGBSurvivalConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	if (StatId.Equals(HealthId.ToString(), ESearchCase::IgnoreCase))
	{
		return Fail(TEXT("Health cannot be removed (players need it to be hurt and die). Hide it with SetVitalStat bShowOnHUD=false instead."));
	}
	Config->Modify();
	const int32 Removed = Config->Stats.RemoveAll([&StatId](const FAGBStatConfig& Entry) { return Entry.Id.ToString().Equals(StatId, ESearchCase::IgnoreCase); });
	if (Removed == 0)
	{
		return Fail(FString::Printf(TEXT("No stat '%s'."), *StatId));
	}
	Save(Config);
	return Describe(Config, FString::Printf(TEXT("Removed stat '%s'."), *StatId));
}

FSurvivalResult USurvivalTools::SetTemperatureRules(double BaseTemperatureC, double SeaLevelM, double CoolingPer100m, double ComfortMinC, double ComfortMaxC, double ExposureDamagePerDegree)
{
	FString Error;
	UAGBSurvivalConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	Config->Modify();
	auto Set = [](float& Target, double Value) { if (Value != KeepTemperature) { Target = static_cast<float>(Value); } };
	Set(Config->BaseTemperatureC, BaseTemperatureC);
	Set(Config->SeaLevelM, SeaLevelM);
	Set(Config->CoolingPer100m, CoolingPer100m);
	Set(Config->ComfortMinC, ComfortMinC);
	Set(Config->ComfortMaxC, ComfortMaxC);
	Set(Config->ExposureDamagePerDegree, ExposureDamagePerDegree);
	if (Config->ComfortMinC > Config->ComfortMaxC)
	{
		Swap(Config->ComfortMinC, Config->ComfortMaxC);
	}
	Save(Config);
	return Describe(Config, FString::Printf(TEXT("Temperature: %.1f degC at sea level (%.0f m), %.2f degC colder per 100 m; comfortable %.0f to %.0f degC."),
		Config->BaseTemperatureC, Config->SeaLevelM, Config->CoolingPer100m, Config->ComfortMinC, Config->ComfortMaxC));
}

FSurvivalResult USurvivalTools::SetSurvivalRules(double SprintStaminaPerSecond, double JumpStaminaCost, double WaterDrinkAmount, double FallDamageMinSpeed,
	double FallDamagePerSpeed, double RespawnDelaySeconds, int32 DropItemsOnDeath, double LootBagLifetimeMinutes)
{
	FString Error;
	UAGBSurvivalConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	Config->Modify();
	auto Set = [](float& Target, double Value) { if (Value >= 0.0) { Target = static_cast<float>(Value); } };
	Set(Config->SprintStaminaPerSecond, SprintStaminaPerSecond);
	Set(Config->JumpStaminaCost, JumpStaminaCost);
	Set(Config->WaterDrinkAmount, WaterDrinkAmount);
	Set(Config->FallDamageMinSpeed, FallDamageMinSpeed);
	Set(Config->FallDamagePerSpeed, FallDamagePerSpeed);
	Set(Config->RespawnDelaySeconds, RespawnDelaySeconds);
	Set(Config->LootBagLifetimeMinutes, FMath::Max(LootBagLifetimeMinutes, LootBagLifetimeMinutes >= 0.0 ? 0.1 : -1.0));
	if (DropItemsOnDeath >= 0)
	{
		Config->bDropItemsOnDeath = DropItemsOnDeath != 0;
	}
	Save(Config);
	return Describe(Config, TEXT("Survival rules updated."));
}

FSurvivalResult USurvivalTools::SpawnHeatSource(double XM, double YM, double WarmthC, double RadiusM)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m."), XM, YM));
	}
	const FScopedTransaction Transaction(LOCTEXT("SpawnHeatSource", "AI Game Builder: Spawn Heat Source"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAGBHeatSource* Fire = World->SpawnActor<AAGBHeatSource>(Hit.ImpactPoint, FRotator::ZeroRotator, Params);
	if (!Fire)
	{
		return Fail(TEXT("Could not spawn the heat source."));
	}
	Fire->Heat->WarmthC = static_cast<float>(WarmthC);
	Fire->Heat->Radius = static_cast<float>(FMath::Max(0.1, RadiusM) * CmPerMeter);
	Fire->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), TEXT("AGB_HeatSource")));

	FSurvivalResult R;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Placed '%s' at (%.1f, %.1f, %.1f) m: +%.0f degC at the centre, fading to 0 at %.1f m."),
		*Fire->GetActorLabel(), XM, YM, Hit.ImpactPoint.Z / CmPerMeter, WarmthC, RadiusM);
	return R;
}

#undef LOCTEXT_NAMESPACE
