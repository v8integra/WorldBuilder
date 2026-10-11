#include "WorldEnvironmentTools.h"

#include "GameToolUtils.h"

#include "AGBClimate.h"
#include "AGBWorldEnvironment.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderWorld"

using namespace AIWorldBuilder;

namespace
{
	FWorldEnvironmentResult Fail(const FString& Message)
	{
		FWorldEnvironmentResult R;
		R.Message = Message;
		return R;
	}

	AAGBWorldEnvironment* FindEnvironment(UWorld* World)
	{
		for (TActorIterator<AAGBWorldEnvironment> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	const TCHAR* MobilityName(EComponentMobility::Type Mobility)
	{
		return Mobility == EComponentMobility::Movable ? TEXT("Movable") : (Mobility == EComponentMobility::Stationary ? TEXT("Stationary") : TEXT("Static"));
	}

	void Describe(UWorld* World, FWorldEnvironmentResult& R)
	{
		if (const AAGBWorldEnvironment* Environment = FindEnvironment(World))
		{
			R.Settings.Add(FString::Printf(TEXT("Day length %.0f min, night %.0f%% (%.0f min), starts at %02d:00"), Environment->DayLengthMinutes,
				Environment->NightFraction * 100.f, Environment->DayLengthMinutes * Environment->NightFraction, FMath::FloorToInt(Environment->StartHour)));
			R.Settings.Add(FString::Printf(TEXT("Nights %.0f degC colder, creatures see %.1fx farther at night, moon %.2f lux, night exposure %.1f"),
				Environment->NightCoolingC, Environment->NightAggroMultiplier, Environment->MoonIntensity, Environment->NightExposureBias));
			R.Settings.Add(FString::Printf(TEXT("Weather %s, snow below %.0f degC, rain effect %s, snow effect %s"), Environment->bWeather ? TEXT("on") : TEXT("off"),
				Environment->SnowBelowC, Environment->RainEffect.IsNull() ? TEXT("none") : *Environment->RainEffect.GetAssetName(),
				Environment->SnowEffect.IsNull() ? TEXT("none") : *Environment->SnowEffect.GetAssetName()));
			R.Settings.Add(Environment->bSeasons ? FString::Printf(TEXT("Seasons on, %d days each"), Environment->SeasonLengthDays) : FString(TEXT("Seasons off")));
			for (const FAGBWeatherSettings& Weather : Environment->Weathers)
			{
				R.Weathers.Add(FString::Printf(TEXT("%s: weight %.1f, %.0f-%.0f min, %+.0f degC, sun x%.2f, fog x%.1f%s"), *Weather.Id.ToString(), Weather.Weight,
					Weather.MinMinutes, Weather.MaxMinutes, Weather.TemperatureOffsetC, Weather.SunMultiplier, Weather.FogMultiplier,
					Weather.bPrecipitation ? TEXT(", rain/snow") : TEXT("")));
			}
		}
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
			R.Lighting.Add(FString::Printf(TEXT("Sun '%s': %s, %.1f lux%s"), *It->GetActorLabel(), MobilityName(Light->Mobility), Light->Intensity,
				Light->IsUsedAsAtmosphereSunLight() ? TEXT(", atmosphere sun") : TEXT("")));
		}
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			const USkyLightComponent* Light = It->GetLightComponent();
			R.Lighting.Add(FString::Printf(TEXT("Sky light '%s': %s, real-time capture %s"), *It->GetActorLabel(), MobilityName(Light->Mobility),
				Light->IsRealTimeCaptureEnabled() ? TEXT("on") : TEXT("off")));
		}
		for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
		{
			R.Lighting.Add(FString::Printf(TEXT("Sky atmosphere '%s'"), *It->GetActorLabel()));
		}
		for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
		{
			R.Lighting.Add(FString::Printf(TEXT("Height fog '%s': density %.3f"), *It->GetActorLabel(), It->GetComponent()->FogDensity));
		}
		for (TActorIterator<AAGBClimateZone> It(World); It; ++It)
		{
			R.ClimateZones.Add(FString::Printf(TEXT("%s: %+.0f degC within %.0f m (fades to %.0f m) at (%.0f, %.0f) m"), *It->GetActorLabel(), It->Climate->WarmthC,
				It->Climate->InnerRadius / CmPerMeter, It->Climate->Radius / CmPerMeter, It->GetActorLocation().X / CmPerMeter, It->GetActorLocation().Y / CmPerMeter));
		}
	}

	template <typename T>
	T* SpawnLabelled(UWorld* World, const FString& Label, const FTransform& Transform = FTransform::Identity)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		T* Actor = World->SpawnActor<T>(T::StaticClass(), Transform, Params);
		if (Actor)
		{
			Actor->SetActorLabel(GameToolUtils::UniqueLabel(World, Label, Label));
		}
		return Actor;
	}
}

FString UWorldEnvironmentTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldEnvironmentResult UWorldEnvironmentTools::SetupWorldEnvironment(double DayLengthMinutes, double NightFraction, double StartHour, double NightCoolingC,
	double NightAggroMultiplier, bool bWeather, bool bSeasons, int32 SeasonLengthDays, bool bCreateMissingSky)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	const FScopedTransaction Transaction(LOCTEXT("SetupWorldEnvironment", "AI Game Builder: Setup World Environment"));
	TArray<FString> Notes;

	// Lighting that can change at runtime.
	ADirectionalLight* Sun = nullptr;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!Sun || (Light && Light->IsUsedAsAtmosphereSunLight()))
		{
			Sun = *It;
		}
	}
	if (!Sun && bCreateMissingSky)
	{
		Sun = SpawnLabelled<ADirectionalLight>(World, TEXT("Sun"), FTransform(FRotator(-45.0, 30.0, 0.0)));
		if (Sun)
		{
			Notes.Add(TEXT("Added a sun."));
		}
	}
	if (Sun)
	{
		UDirectionalLightComponent* Light = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
		Sun->Modify();
		Light->Modify();
		if (Light->Mobility != EComponentMobility::Movable)
		{
			Notes.Add(FString::Printf(TEXT("Sun '%s' changed from %s to Movable."), *Sun->GetActorLabel(), MobilityName(Light->Mobility)));
			Light->SetMobility(EComponentMobility::Movable);
		}
		Light->SetAtmosphereSunLight(true);
		if (Light->Intensity < 1.f)
		{
			Light->SetIntensity(10.f);
		}
	}
	if (bCreateMissingSky && !TActorIterator<ASkyAtmosphere>(World))
	{
		if (SpawnLabelled<ASkyAtmosphere>(World, TEXT("SkyAtmosphere")))
		{
			Notes.Add(TEXT("Added a sky atmosphere."));
		}
	}
	ASkyLight* Sky = nullptr;
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Sky = *It;
		break;
	}
	if (!Sky && bCreateMissingSky)
	{
		Sky = SpawnLabelled<ASkyLight>(World, TEXT("SkyLight"));
		if (Sky)
		{
			Notes.Add(TEXT("Added a sky light."));
		}
	}
	if (Sky)
	{
		USkyLightComponent* Light = Sky->GetLightComponent();
		Sky->Modify();
		Light->Modify();
		if (Light->Mobility != EComponentMobility::Movable)
		{
			Notes.Add(FString::Printf(TEXT("Sky light changed from %s to Movable."), MobilityName(Light->Mobility)));
			Light->SetMobility(EComponentMobility::Movable);
		}
		if (!Light->IsRealTimeCaptureEnabled())
		{
			Light->SetRealTimeCapture(true);
			Notes.Add(TEXT("Sky light now captures in real time (follows the sun)."));
		}
	}
	if (bCreateMissingSky && !TActorIterator<AExponentialHeightFog>(World))
	{
		if (SpawnLabelled<AExponentialHeightFog>(World, TEXT("HeightFog")))
		{
			Notes.Add(TEXT("Added height fog (weather thickens it)."));
		}
	}

	// The environment itself.
	AAGBWorldEnvironment* Environment = FindEnvironment(World);
	const bool bCreated = Environment == nullptr;
	if (bCreated)
	{
		Environment = SpawnLabelled<AAGBWorldEnvironment>(World, TEXT("WorldEnvironment"));
		if (!Environment)
		{
			return Fail(TEXT("Could not place the world environment."));
		}
	}
	Environment->Modify();
	Environment->DayLengthMinutes = static_cast<float>(FMath::Max(1.0, DayLengthMinutes));
	Environment->NightFraction = static_cast<float>(FMath::Clamp(NightFraction, 0.05, 0.95));
	Environment->StartHour = static_cast<float>(FMath::Clamp(StartHour, 0.0, 24.0));
	Environment->NightCoolingC = static_cast<float>(NightCoolingC);
	Environment->NightAggroMultiplier = static_cast<float>(FMath::Max(0.1, NightAggroMultiplier));
	Environment->bWeather = bWeather;
	Environment->bSeasons = bSeasons;
	Environment->SeasonLengthDays = FMath::Max(1, SeasonLengthDays);
	if (Environment->Weathers.Num() == 0)
	{
		Environment->Weathers = AAGBWorldEnvironment::MakeDefaultWeathers();
	}

	FWorldEnvironmentResult R;
	Describe(World, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s the world environment. %s Save the level. In Play: console \"AGBTime 22\" / \"AGBWeather Rain\" to test."),
		bCreated ? TEXT("Placed") : TEXT("Updated"), *FString::Join(Notes, TEXT(" ")));
	return R;
}

FWorldEnvironmentResult UWorldEnvironmentTools::SetWeatherType(const FString& WeatherId, double Weight, double MinMinutes, double MaxMinutes,
	double TemperatureOffsetC, double SunMultiplier, double FogMultiplier, bool bPrecipitation, bool bRemove)
{
	UWorld* World = GetEditorWorld();
	AAGBWorldEnvironment* Environment = World ? FindEnvironment(World) : nullptr;
	if (!Environment)
	{
		return Fail(TEXT("No world environment in this level: run SetupWorldEnvironment first."));
	}
	const FName Id(*WeatherId.TrimStartAndEnd());
	if (Id.IsNone())
	{
		return Fail(TEXT("weatherId is empty."));
	}
	const FScopedTransaction Transaction(LOCTEXT("SetWeatherType", "AI Game Builder: Set Weather Type"));
	Environment->Modify();
	if (bRemove)
	{
		Environment->Weathers.RemoveAll([Id](const FAGBWeatherSettings& Weather) { return Weather.Id == Id; });
	}
	else
	{
		FAGBWeatherSettings* Weather = Environment->Weathers.FindByPredicate([Id](const FAGBWeatherSettings& Entry) { return Entry.Id == Id; });
		if (!Weather)
		{
			Weather = &Environment->Weathers.AddDefaulted_GetRef();
			Weather->Id = Id;
		}
		Weather->Weight = static_cast<float>(FMath::Max(0.0, Weight));
		Weather->MinMinutes = static_cast<float>(FMath::Max(0.5, MinMinutes));
		Weather->MaxMinutes = static_cast<float>(FMath::Max(MinMinutes, MaxMinutes));
		Weather->TemperatureOffsetC = static_cast<float>(TemperatureOffsetC);
		Weather->SunMultiplier = static_cast<float>(FMath::Max(0.0, SunMultiplier));
		Weather->FogMultiplier = static_cast<float>(FMath::Max(0.0, FogMultiplier));
		Weather->bPrecipitation = bPrecipitation;
	}
	FWorldEnvironmentResult R;
	Describe(World, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s weather '%s'. Save the level."), bRemove ? TEXT("Removed") : TEXT("Set"), *Id.ToString());
	return R;
}

FWorldEnvironmentResult UWorldEnvironmentTools::SetWeatherEffects(const FString& RainSystemPath, const FString& SnowSystemPath)
{
	UWorld* World = GetEditorWorld();
	AAGBWorldEnvironment* Environment = World ? FindEnvironment(World) : nullptr;
	if (!Environment)
	{
		return Fail(TEXT("No world environment in this level: run SetupWorldEnvironment first."));
	}
	auto Load = [](const FString& Path, UNiagaraSystem*& Out, FString& Error) -> bool
	{
		Out = nullptr;
		if (Path.Equals(TEXT("none"), ESearchCase::IgnoreCase) || Path.IsEmpty())
		{
			return true;
		}
		Out = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (!Out)
		{
			Error = FString::Printf(TEXT("Niagara system '%s' not found."), *Path);
		}
		return Out != nullptr;
	};
	UNiagaraSystem* Rain = nullptr;
	UNiagaraSystem* Snow = nullptr;
	FString Error;
	if (!Load(RainSystemPath, Rain, Error) || !Load(SnowSystemPath, Snow, Error))
	{
		return Fail(Error);
	}
	const FScopedTransaction Transaction(LOCTEXT("SetWeatherEffects", "AI Game Builder: Set Weather Effects"));
	Environment->Modify();
	Environment->RainEffect = Rain;
	Environment->SnowEffect = Snow;
	FWorldEnvironmentResult R;
	Describe(World, R);
	R.bSuccess = true;
	R.Message = TEXT("Weather effects set. Save the level.");
	return R;
}

FWorldEnvironmentResult UWorldEnvironmentTools::CreateClimateZone(double CenterXM, double CenterYM, double RadiusM, double TemperatureOffsetC, double EdgeM,
	const FString& Label)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	FHitResult Hit;
	const double Z = GameToolUtils::TraceGround(World, CenterXM, CenterYM, Hit) ? Hit.ImpactPoint.Z : 0.0;
	const FScopedTransaction Transaction(LOCTEXT("CreateClimateZone", "AI Game Builder: Create Climate Zone"));
	AAGBClimateZone* Zone = SpawnLabelled<AAGBClimateZone>(World, Label.Equals(TEXT("auto"), ESearchCase::IgnoreCase) || Label.IsEmpty()
		? FString(TemperatureOffsetC >= 0.0 ? TEXT("ClimateZone_Warm") : TEXT("ClimateZone_Cold")) : Label,
		FTransform(FVector(CenterXM * CmPerMeter, CenterYM * CmPerMeter, Z)));
	if (!Zone)
	{
		return Fail(TEXT("Could not place the climate zone."));
	}
	Zone->Climate->WarmthC = static_cast<float>(TemperatureOffsetC);
	Zone->Climate->InnerRadius = static_cast<float>(FMath::Max(1.0, RadiusM) * CmPerMeter);
	Zone->Climate->Radius = static_cast<float>((FMath::Max(1.0, RadiusM) + FMath::Max(1.0, EdgeM)) * CmPerMeter);

	FWorldEnvironmentResult R;
	Describe(World, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Placed '%s': %+.0f degC within %.0f m, fading out over %.0f m. Save the level."), *Zone->GetActorLabel(),
		TemperatureOffsetC, RadiusM, EdgeM);
	return R;
}

FWorldEnvironmentResult UWorldEnvironmentTools::GetWorldEnvironment()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	FWorldEnvironmentResult R;
	Describe(World, R);
	R.bSuccess = true;
	R.Message = FindEnvironment(World) ? TEXT("World environment found.") : TEXT("No world environment in this level yet: run SetupWorldEnvironment.");
	return R;
}

#undef LOCTEXT_NAMESPACE
