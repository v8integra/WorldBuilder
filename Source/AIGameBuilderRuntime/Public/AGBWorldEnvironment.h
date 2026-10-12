#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGBWorldEnvironment.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;
class UDirectionalLightComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPostProcessComponent;

/** One kind of weather. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBWeatherSettings
{
	GENERATED_BODY()

	/** "Clear", "Cloudy", "Rain", "Storm"... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FName Id;

	/** How often it is picked, relative to the others. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.5"))
	float MinMinutes = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.5"))
	float MaxMinutes = 10.f;

	/** Added to the air temperature, degrees C. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float TemperatureOffsetC = 0.f;

	/** Sunlight multiplier (clouds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float SunMultiplier = 1.f;

	/** Fog density multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float FogMultiplier = 1.f;

	/** Falls from the sky (rain; becomes snow below the snow temperature). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bPrecipitation = false;
};

/**
 * Day and night, weather and seasons for a level (one per level, replicated: every player shares the clock and the
 * weather). Moves the level's sun (sky atmosphere colours sunrise and sunset), adds moonlight and keeps nights dark,
 * thickens fog and dims the sun in bad weather, and feeds night, weather and season into the air temperature.
 * Placed and configured by the AI's WorldEnvironmentTools.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBWorldEnvironment : public AActor
{
	GENERATED_BODY()

public:
	AAGBWorldEnvironment();

	// ---- Day and night

	/** Real minutes for a full day and night. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night", meta = (ClampMin = "1"))
	float DayLengthMinutes = 60.f;

	/** Share of the cycle that is night (18:00-06:00). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float NightFraction = 0.333f;

	/** Clock time when the level starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHour = 8.f;

	/** Compass direction of the midday sun (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night")
	float SunYaw = 30.f;

	/** Moonlight at night (lux). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night", meta = (ClampMin = "0"))
	float MoonIntensity = 0.4f;

	/** Exposure compensation at night (negative = darker nights despite eye adaptation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night")
	float NightExposureBias = -1.5f;

	/** How much colder the air gets at night (degrees C). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night")
	float NightCoolingC = 8.f;

	/** Aggressive creatures notice players this much farther at night. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Day and night", meta = (ClampMin = "0.1"))
	float NightAggroMultiplier = 1.5f;

	// ---- Weather

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	bool bWeather = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TArray<FAGBWeatherSettings> Weathers;

	/** Precipitation turns to snow below this air temperature (degrees C). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	float SnowBelowC = 2.f;

	/** Optional particle systems following the player (until the VFX phase provides them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TSoftObjectPtr<UNiagaraSystem> RainEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TSoftObjectPtr<UNiagaraSystem> SnowEffect;

	// ---- Seasons

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seasons")
	bool bSeasons = false;

	/** Days per season (spring, summer, autumn, winter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seasons", meta = (ClampMin = "1"))
	int32 SeasonLengthDays = 7;

	/** Temperature offsets for spring, summer, autumn, winter (degrees C). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seasons")
	TArray<float> SeasonOffsetsC = { 0.f, 6.f, -2.f, -10.f };

	// ---- Queries (any machine)

	/** Clock time 0-24. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	float GetHour() const;

	/** Day number, starting at 1. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	int32 GetDay() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	bool IsNight() const;

	/** 0 by day, 1 deep in the night (smooth at dusk and dawn). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	float GetNightAmount() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	FName GetWeather() const;

	/** 0-1 how hard it is raining/snowing right now (blends in with the weather). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	float GetPrecipitation() const;

	/** "Spring", "Summer", "Autumn", "Winter", or empty without seasons. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|World")
	FString GetSeasonName() const;

	/** Clock and weather for the HUD, e.g. "Day 2  21:40  Rain". Precipitation reads as snow below the snow temperature. */
	FString DescribeForPlayer(float AirTemperatureC) const;

	// ---- Changes (server)

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|World")
	void SetHour(float Hour);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|World")
	bool SetWeather(FName WeatherId);

	/** Save games: sets the day number and clock (server). */
	void RestoreClock(int32 Day, float Hour);

	/** The level's environment, or null. */
	static AAGBWorldEnvironment* Get(const UWorld* World);

	/** Standard weather table: clear, cloudy, rain, storm. */
	static TArray<FAGBWeatherSettings> MakeDefaultWeathers();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "AI Game Builder")
	TObjectPtr<UDirectionalLightComponent> Moon;

	UPROPERTY(VisibleAnywhere, Category = "AI Game Builder")
	TObjectPtr<UPostProcessComponent> Exposure;

	/** Server world time at which the cycle was at fraction 0 (06:00 of day 1). */
	UPROPERTY(Replicated)
	float CycleOrigin = 0.f;

	UPROPERTY(Replicated)
	int32 WeatherIndex = 0;

	UPROPERTY(Replicated)
	int32 PreviousWeatherIndex = 0;

	/** Server world time of the last weather change (visuals blend over a minute). */
	UPROPERTY(Replicated)
	float WeatherChangedAt = -1000.f;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Precipitation;

private:
	float ServerTime() const;
	float GetCycleFraction() const;
	float HourToFraction(float Hour) const;
	float GetSunElevation() const;
	float GetWeatherBlend() const;
	const FAGBWeatherSettings* WeatherAt(int32 Index) const;
	float BlendWeather(TFunctionRef<float(const FAGBWeatherSettings&)> Value, float Default) const;
	void PickNextWeather();
	void ApplyLighting(float DeltaSeconds);
	void UpdatePrecipitation();
	float GetSeasonOffset() const;

	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ASkyLight> Sky;
	TWeakObjectPtr<AExponentialHeightFog> Fog;
	float BaseSunIntensity = 10.f;
	float BaseFogDensity = 0.02f;
	float BaseFogFalloff = 0.2f;
	double BaseFogZ = 0.0;
	double NextWeatherTime = 0.0;
	double NextSkyRecapture = 0.0;
	int32 ShownPrecipitation = 0; // 0 none, 1 rain, 2 snow
};
