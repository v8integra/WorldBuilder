#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "WorldEnvironmentTools.generated.h"

/// Result of the world environment tools.
USTRUCT(BlueprintType)
struct FWorldEnvironmentResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Day/night, weather and season settings.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Settings;

	/// One line per weather type.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Weathers;

	/// The level's sky and lighting actors and whether they can change at runtime.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Lighting;

	/// Climate zones in the level.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> ClimateZones;
};

/// World systems for AI Game Builder games: day and night (moving sun, sunrise/sunset from the sky atmosphere, moonlight, dark nights), weather (clear, cloudy, rain, storm; snow when cold) changing over time, optional seasons, and climate zones that make biomes warmer or colder. All feed the survival temperature; aggressive creatures see farther at night. One environment per level, shared by all players. In Play, the console commands "AGBTime 22" and "AGBWeather Rain" help testing.
UCLASS(BlueprintType, Hidden)
class UWorldEnvironmentTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Places (or updates) the level's world environment and prepares its lighting: the sun and sky light become Movable (needed
	 * to change at runtime), the sky light captures in real time, and a missing sun, sky atmosphere, sky light or height fog is
	 * added. Save the level afterwards.
	 * @param DayLengthMinutes Real minutes for a full day and night.
	 * @param NightFraction Share of the cycle that is night (0.33 = 40 min day / 20 min night in a 60 min cycle).
	 * @param StartHour Clock time when the level starts (0-24).
	 * @param NightCoolingC How much colder nights are, degrees C.
	 * @param NightAggroMultiplier Aggressive creatures notice players this much farther at night.
	 * @param bWeather Weather changes over time.
	 * @param bSeasons Seasons shift the temperature (spring 0, summer +6, autumn -2, winter -10).
	 * @param SeasonLengthDays In-game days per season.
	 * @param bCreateMissingSky Add a sun, sky atmosphere, sky light and fog if the level lacks them.
	 * @return The environment.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|World")
	static FWorldEnvironmentResult SetupWorldEnvironment(double DayLengthMinutes = 60.0, double NightFraction = 0.333, double StartHour = 8.0,
		double NightCoolingC = 8.0, double NightAggroMultiplier = 1.5, bool bWeather = true, bool bSeasons = false, int32 SeasonLengthDays = 7,
		bool bCreateMissingSky = true);

	/**
	 * Adds or changes a weather type (or removes it). Defaults: Clear, Cloudy, Rain, Storm.
	 * Example (a dry desert): SetWeatherType("Sandstorm", 1, 2, 5, 4, 0.4, 4, false)
	 * @param WeatherId Name ("Clear", "Rain", "Fog"...).
	 * @param Weight How often it is picked relative to the others (0 = never).
	 * @param MinMinutes Shortest duration, real minutes.
	 * @param MaxMinutes Longest duration, real minutes.
	 * @param TemperatureOffsetC Added to the air temperature.
	 * @param SunMultiplier Sunlight multiplier (cloud cover).
	 * @param FogMultiplier Fog density multiplier.
	 * @param bPrecipitation Rain falls (snow below the snow temperature).
	 * @param bRemove Remove this weather type.
	 * @return The environment.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|World")
	static FWorldEnvironmentResult SetWeatherType(const FString& WeatherId, double Weight, double MinMinutes, double MaxMinutes, double TemperatureOffsetC,
		double SunMultiplier, double FogMultiplier, bool bPrecipitation, bool bRemove = false);

	/**
	 * Sets the particle effects that follow the player in rain and snow (Niagara systems, e.g. from a VFX pack).
	 * @param RainSystemPath Niagara system for rain, or "none".
	 * @param SnowSystemPath Niagara system for snow, or "none".
	 * @return The environment.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|World")
	static FWorldEnvironmentResult SetWeatherEffects(const FString& RainSystemPath = TEXT("none"), const FString& SnowSystemPath = TEXT("none"));

	/**
	 * Places a climate zone: an area warmer or colder than altitude alone makes it (desert +10, hot spring +15, snowfield -10),
	 * fading out over the edge. One undo step.
	 * @param CenterXM Centre X, m.
	 * @param CenterYM Centre Y, m.
	 * @param RadiusM Radius of the full effect, m.
	 * @param TemperatureOffsetC Degrees C added (negative = colder).
	 * @param EdgeM Width of the fade at the edge, m.
	 * @param Label Outliner label, or "auto".
	 * @return The environment.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|World")
	static FWorldEnvironmentResult CreateClimateZone(double CenterXM, double CenterYM, double RadiusM, double TemperatureOffsetC, double EdgeM = 50.0,
		const FString& Label = TEXT("auto"));

	/**
	 * Reports the environment settings, weather types, sky and lighting actors, and climate zones of the open level.
	 * @return The environment.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|World")
	static FWorldEnvironmentResult GetWorldEnvironment();
};
