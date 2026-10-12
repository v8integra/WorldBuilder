#include "AGBWorldEnvironment.h"

#include "AGBCharacter.h"
#include "AGBClimate.h"
#include "AGBVitalsComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	constexpr float WeatherBlendSeconds = 60.f;
	constexpr float SkyRecaptureSeconds = 20.f;
	const TCHAR* SeasonNames[] = { TEXT("Spring"), TEXT("Summer"), TEXT("Autumn"), TEXT("Winter") };

	FAGBWeatherSettings MakeWeather(const TCHAR* Id, float Weight, float MinMinutes, float MaxMinutes, float Temperature, float Sun, float Fog, bool bPrecipitation)
	{
		FAGBWeatherSettings Weather;
		Weather.Id = Id;
		Weather.Weight = Weight;
		Weather.MinMinutes = MinMinutes;
		Weather.MaxMinutes = MaxMinutes;
		Weather.TemperatureOffsetC = Temperature;
		Weather.SunMultiplier = Sun;
		Weather.FogMultiplier = Fog;
		Weather.bPrecipitation = bPrecipitation;
		return Weather;
	}
}

AAGBWorldEnvironment::AAGBWorldEnvironment()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	SetNetUpdateFrequency(1.f);

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(GetRootComponent());
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetIntensity(0.f);
	Moon->SetLightColor(FLinearColor(0.55f, 0.65f, 1.f));
	Moon->SetAtmosphereSunLight(false);

	Exposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
	Exposure->SetupAttachment(GetRootComponent());
	Exposure->bUnbound = true;
	Exposure->Priority = 5.f;
	Exposure->Settings.bOverride_AutoExposureBias = true;
	Exposure->Settings.AutoExposureBias = 0.f;

	Weathers = MakeDefaultWeathers();
}

TArray<FAGBWeatherSettings> AAGBWorldEnvironment::MakeDefaultWeathers()
{
	return {
		MakeWeather(TEXT("Clear"), 5.f, 6.f, 14.f, 0.f, 1.f, 1.f, false),
		MakeWeather(TEXT("Cloudy"), 3.f, 4.f, 10.f, -1.f, 0.6f, 1.5f, false),
		MakeWeather(TEXT("Rain"), 2.f, 3.f, 8.f, -4.f, 0.35f, 2.5f, true),
		MakeWeather(TEXT("Storm"), 0.7f, 2.f, 5.f, -6.f, 0.2f, 3.5f, true),
	};
}

AAGBWorldEnvironment* AAGBWorldEnvironment::Get(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AAGBWorldEnvironment> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

float AAGBWorldEnvironment::ServerTime() const
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
}

// ---------------------------------------------------------------- clock

float AAGBWorldEnvironment::GetCycleFraction() const
{
	const float Cycle = FMath::Max(1.f, DayLengthMinutes) * 60.f;
	return FMath::Frac((ServerTime() - CycleOrigin) / Cycle);
}

float AAGBWorldEnvironment::HourToFraction(float Hour) const
{
	// Day 06:00-18:00 takes (1 - NightFraction) of the cycle, night 18:00-06:00 the rest.
	const float Day = 1.f - NightFraction;
	float H = FMath::Fmod(Hour, 24.f);
	H = H < 6.f ? H + 24.f : H;
	return H < 18.f ? (H - 6.f) / 12.f * Day : Day + (H - 18.f) / 12.f * NightFraction;
}

float AAGBWorldEnvironment::GetHour() const
{
	const float F = GetCycleFraction();
	const float Day = 1.f - NightFraction;
	const float Hour = F < Day ? 6.f + 12.f * F / Day : 18.f + 12.f * (F - Day) / FMath::Max(0.01f, NightFraction);
	return FMath::Fmod(Hour, 24.f);
}

int32 AAGBWorldEnvironment::GetDay() const
{
	const float Cycle = FMath::Max(1.f, DayLengthMinutes) * 60.f;
	// Day 1 starts at 06:00 of the first cycle.
	return FMath::FloorToInt((ServerTime() - CycleOrigin) / Cycle) + 1;
}

float AAGBWorldEnvironment::GetSunElevation() const
{
	const float Hour = GetHour();
	if (Hour >= 6.f && Hour < 18.f)
	{
		return 70.f * FMath::Sin(PI * (Hour - 6.f) / 12.f);
	}
	const float NightHour = Hour < 6.f ? Hour + 6.f : Hour - 18.f; // 0..12 through the night
	return -30.f * FMath::Sin(PI * NightHour / 12.f);
}

bool AAGBWorldEnvironment::IsNight() const
{
	return GetSunElevation() < -2.f;
}

float AAGBWorldEnvironment::GetNightAmount() const
{
	return FMath::Clamp((2.f - GetSunElevation()) / 8.f, 0.f, 1.f);
}

void AAGBWorldEnvironment::RestoreClock(int32 Day, float Hour)
{
	const float Cycle = FMath::Max(1.f, DayLengthMinutes) * 60.f;
	CycleOrigin = ServerTime() - (static_cast<float>(FMath::Max(1, Day) - 1) + HourToFraction(Hour)) * Cycle;
}

void AAGBWorldEnvironment::SetHour(float Hour)
{
	const float Cycle = FMath::Max(1.f, DayLengthMinutes) * 60.f;
	const float DayStart = CycleOrigin + FMath::FloorToFloat((ServerTime() - CycleOrigin) / Cycle) * Cycle;
	// Keep the day number; jump to that clock time (forward to the next day if it is earlier than now).
	float Target = DayStart + HourToFraction(Hour) * Cycle;
	if (Target < ServerTime())
	{
		Target += Cycle;
	}
	CycleOrigin -= Target - ServerTime();
}

// ---------------------------------------------------------------- weather

const FAGBWeatherSettings* AAGBWorldEnvironment::WeatherAt(int32 Index) const
{
	return Weathers.IsValidIndex(Index) ? &Weathers[Index] : nullptr;
}

FName AAGBWorldEnvironment::GetWeather() const
{
	const FAGBWeatherSettings* Weather = bWeather ? WeatherAt(WeatherIndex) : nullptr;
	return Weather ? Weather->Id : FName(TEXT("Clear"));
}

float AAGBWorldEnvironment::GetWeatherBlend() const
{
	return FMath::Clamp((ServerTime() - WeatherChangedAt) / WeatherBlendSeconds, 0.f, 1.f);
}

float AAGBWorldEnvironment::BlendWeather(TFunctionRef<float(const FAGBWeatherSettings&)> Value, float Default) const
{
	if (!bWeather)
	{
		return Default;
	}
	const FAGBWeatherSettings* Current = WeatherAt(WeatherIndex);
	const FAGBWeatherSettings* Previous = WeatherAt(PreviousWeatherIndex);
	const float To = Current ? Value(*Current) : Default;
	const float From = Previous ? Value(*Previous) : To;
	return FMath::Lerp(From, To, GetWeatherBlend());
}

bool AAGBWorldEnvironment::SetWeather(FName WeatherId)
{
	const int32 Index = Weathers.IndexOfByPredicate([WeatherId](const FAGBWeatherSettings& Weather) { return Weather.Id == WeatherId; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	PreviousWeatherIndex = WeatherIndex;
	WeatherIndex = Index;
	WeatherChangedAt = ServerTime() - WeatherBlendSeconds * 0.9f; // Commands switch almost at once.
	const FAGBWeatherSettings& Weather = Weathers[Index];
	NextWeatherTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(Weather.MinMinutes, Weather.MaxMinutes) * 60.0;
	return true;
}

void AAGBWorldEnvironment::PickNextWeather()
{
	float Total = 0.f;
	for (const FAGBWeatherSettings& Weather : Weathers)
	{
		Total += Weather.Weight;
	}
	if (Total <= 0.f)
	{
		return;
	}
	float Roll = FMath::FRandRange(0.f, Total);
	int32 Picked = 0;
	for (int32 Index = 0; Index < Weathers.Num(); ++Index)
	{
		Roll -= Weathers[Index].Weight;
		if (Roll <= 0.f)
		{
			Picked = Index;
			break;
		}
	}
	PreviousWeatherIndex = WeatherIndex;
	WeatherIndex = Picked;
	WeatherChangedAt = ServerTime();
	NextWeatherTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(Weathers[Picked].MinMinutes, Weathers[Picked].MaxMinutes) * 60.0;
}

// ---------------------------------------------------------------- seasons

float AAGBWorldEnvironment::GetSeasonOffset() const
{
	if (!bSeasons || SeasonOffsetsC.Num() == 0)
	{
		return 0.f;
	}
	const int32 Season = ((GetDay() - 1) / FMath::Max(1, SeasonLengthDays)) % 4;
	return SeasonOffsetsC.IsValidIndex(Season) ? SeasonOffsetsC[Season] : 0.f;
}

float AAGBWorldEnvironment::GetPrecipitation() const
{
	return BlendWeather([](const FAGBWeatherSettings& Weather) { return Weather.bPrecipitation ? 1.f : 0.f; }, 0.f);
}

FString AAGBWorldEnvironment::GetSeasonName() const
{
	if (!bSeasons)
	{
		return FString();
	}
	return SeasonNames[((GetDay() - 1) / FMath::Max(1, SeasonLengthDays)) % 4];
}

FString AAGBWorldEnvironment::DescribeForPlayer(float AirTemperatureC) const
{
	const float Hour = GetHour();
	FString Weather = GetWeather().ToString();
	const FAGBWeatherSettings* Current = bWeather ? WeatherAt(WeatherIndex) : nullptr;
	if (Current && Current->bPrecipitation && AirTemperatureC < SnowBelowC)
	{
		Weather = TEXT("Snow");
	}
	const FString Season = GetSeasonName();
	return FString::Printf(TEXT("%sDay %d  %02d:%02d  %s"), Season.IsEmpty() ? TEXT("") : *(Season + TEXT("  ")), GetDay(),
		FMath::FloorToInt(Hour), FMath::FloorToInt(FMath::Frac(Hour) * 60.f), *Weather);
}

// ---------------------------------------------------------------- tick

void AAGBWorldEnvironment::BeginPlay()
{
	Super::BeginPlay();

	// The level's sun (the atmosphere sun light), sky light and fog.
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (Light && (!Sun.IsValid() || Light->IsUsedAsAtmosphereSunLight()))
		{
			Sun = *It;
			BaseSunIntensity = Light->Intensity;
		}
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		Sky = *It;
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		Fog = *It;
		BaseFogDensity = It->GetComponent()->FogDensity;
		BaseFogFalloff = It->GetComponent()->FogHeightFalloff;
		BaseFogZ = It->GetActorLocation().Z;
		break;
	}

	if (HasAuthority())
	{
		const float Cycle = FMath::Max(1.f, DayLengthMinutes) * 60.f;
		CycleOrigin = ServerTime() - HourToFraction(StartHour) * Cycle;
		WeatherIndex = PreviousWeatherIndex = 0;
		WeatherChangedAt = ServerTime() - WeatherBlendSeconds;
		NextWeatherTime = GetWorld()->GetTimeSeconds() + (Weathers.IsValidIndex(0) ? FMath::FRandRange(Weathers[0].MinMinutes, Weathers[0].MaxMinutes) * 60.0 : 600.0);
	}
}

void AAGBWorldEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bWeather && Weathers.Num() > 0 && GetWorld()->GetTimeSeconds() >= NextWeatherTime)
	{
		PickNextWeather();
	}

	// Air temperature: night, weather and season (every machine computes the same).
	if (UAGBClimateSubsystem* Climate = GetWorld()->GetSubsystem<UAGBClimateSubsystem>())
	{
		Climate->WorldOffsetC = -NightCoolingC * GetNightAmount()
			+ BlendWeather([](const FAGBWeatherSettings& Weather) { return Weather.TemperatureOffsetC; }, 0.f)
			+ GetSeasonOffset();
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplyLighting(DeltaSeconds);
		UpdatePrecipitation();
	}
}

void AAGBWorldEnvironment::ApplyLighting(float DeltaSeconds)
{
	const float Elevation = GetSunElevation();
	const float Hour = GetHour();
	const float Night = GetNightAmount();
	const float SunWeather = BlendWeather([](const FAGBWeatherSettings& Weather) { return Weather.SunMultiplier; }, 1.f);

	if (ADirectionalLight* SunActor = Sun.Get())
	{
		// East in the morning, SunYaw at noon, west in the evening.
		SunActor->SetActorRotation(FRotator(-Elevation, SunYaw + (Hour - 12.f) * 15.f, 0.f));
		SunActor->GetLightComponent()->SetIntensity(BaseSunIntensity * SunWeather);
	}
	Moon->SetWorldRotation(FRotator(-45.f, SunYaw + 180.f, 0.f));
	Moon->SetIntensity(MoonIntensity * Night * FMath::Lerp(1.f, 0.4f, 1.f - SunWeather));
	Exposure->Settings.AutoExposureBias = NightExposureBias * Night;

	if (AExponentialHeightFog* FogActor = Fog.Get())
	{
		// Height fog thins out above the fog actor, so bad weather (a) adds real density, (b) stretches the fog upward and
		// (c) centres it a little below the local player, wherever they are on the terrain. Cosmetic, local to each player.
		const float Thickness = BlendWeather([](const FAGBWeatherSettings& Weather) { return Weather.FogMultiplier; }, 1.f);
		const float Extra = FMath::Max(0.f, Thickness - 1.f);
		UExponentialHeightFogComponent* FogComponent = FogActor->GetComponent();
		FogComponent->SetFogDensity(Thickness <= 1.f ? BaseFogDensity * Thickness : BaseFogDensity + Extra * 0.02f);
		FogComponent->SetFogHeightFalloff(BaseFogFalloff / (1.f + Extra * 2.f));
		const APlayerController* Player = GetWorld()->GetFirstPlayerController();
		const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
		double TargetZ = BaseFogZ;
		if (Pawn && Extra > 0.01f)
		{
			TargetZ = FMath::Lerp(BaseFogZ, Pawn->GetActorLocation().Z - 1500.0, FMath::Min(1.f, Extra));
		}
		FVector FogLocation = FogActor->GetActorLocation();
		FogLocation.Z = FMath::FInterpTo(FogLocation.Z, TargetZ, DeltaSeconds, 0.5f);
		FogActor->SetActorLocation(FogLocation);
	}
	// Sky lighting follows the sun: real-time capture does it by itself; otherwise recapture now and then.
	if (ASkyLight* SkyActor = Sky.Get())
	{
		USkyLightComponent* SkyComponent = SkyActor->GetLightComponent();
		const double Now = GetWorld()->GetTimeSeconds();
		if (SkyComponent && !SkyComponent->IsRealTimeCaptureEnabled() && Now >= NextSkyRecapture)
		{
			NextSkyRecapture = Now + SkyRecaptureSeconds;
			SkyComponent->RecaptureSky();
		}
	}
}

void AAGBWorldEnvironment::UpdatePrecipitation()
{
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const FAGBWeatherSettings* Current = bWeather ? WeatherAt(WeatherIndex) : nullptr;
	int32 Wanted = 0;
	if (Pawn && Current && Current->bPrecipitation && GetWeatherBlend() > 0.3f)
	{
		// The player's own air temperature (their game's survival rules) decides rain or snow.
		const AAGBCharacter* Character = Cast<AAGBCharacter>(Pawn);
		const float Air = Character && Character->Vitals ? Character->Vitals->GetAirTemperature() : 15.f;
		Wanted = Air < SnowBelowC ? 2 : 1;
	}
	if (Wanted == ShownPrecipitation && (Wanted == 0 || (Precipitation && Precipitation->GetAttachParent() == (Pawn ? Pawn->GetRootComponent() : nullptr))))
	{
		return;
	}
	if (Precipitation)
	{
		Precipitation->DestroyComponent();
		Precipitation = nullptr;
	}
	ShownPrecipitation = Wanted;
	UNiagaraSystem* System = Wanted == 1 ? RainEffect.LoadSynchronous() : (Wanted == 2 ? SnowEffect.LoadSynchronous() : nullptr);
	if (System && Pawn)
	{
		Precipitation = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Pawn->GetRootComponent(), NAME_None, FVector(0.0, 0.0, 600.0),
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/false);
	}
}

void AAGBWorldEnvironment::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBWorldEnvironment, CycleOrigin);
	DOREPLIFETIME(AAGBWorldEnvironment, WeatherIndex);
	DOREPLIFETIME(AAGBWorldEnvironment, PreviousWeatherIndex);
	DOREPLIFETIME(AAGBWorldEnvironment, WeatherChangedAt);
}
