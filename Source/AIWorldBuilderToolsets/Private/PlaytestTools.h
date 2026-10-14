#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "PlaytestTools.generated.h"

/// Result of the playtest and QA tools.
USTRUCT(BlueprintType)
struct FPlaytestToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Findings, status or report lines.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Lines;
};

/// Playtesting, QA and balance for AI Game Builder games: checks the game data for dead ends (ValidateGame), lets a bot play the game in Play-in-Editor (gather, craft, place a fire, survive a night; smoke tests; a performance sweep over the map), records telemetry of every play session (also the user's), and reports balance and performance against targets. Typical loop: ValidateGame -> StartPlaytest -> poll GetPlaytestStatus -> GetBalanceReport -> adjust items/recipes/survival rules -> repeat.
UCLASS(BlueprintType, Hidden)
class UPlaytestTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/** Starts Play-in-Editor with a bot request (used by other toolsets, e.g. store screenshots). */
	static FPlaytestToolsResult StartWithRequest(const struct FAGBPlaytestRequest& Request);

	/**
	 * Checks the game's data and level setup without playing: items nobody can get, recipes needing missing stations
	 * or fuel, resources not placed in the level or needing tools that do not exist, creatures without spawn zones,
	 * food without effects, missing icons, game mode/UI/audio setup, and how fast hunger and thirst empty vs a day.
	 * @return FAIL / WARN / INFO lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult ValidateGame();

	/**
	 * Starts Play-in-Editor (single player) with a bot playing the open level. Poll GetPlaytestStatus; Play ends by
	 * itself when the bot is done. The user should not touch the controls meanwhile.
	 * @param Scenario Survive (craft the first tool, place a fire, survive a night), Gather (10 of each basic resource), Smoke (quick checks: HUD, harvest, save/load, menus, log errors), Explore (walk 500 m, measure performance), Custom (Goals).
	 * @param Goals For Custom: "gather:wood=10; craft:stone_axe; place:campfire; survive_night; explore:300; wait:20" (ids from ListItems / recipes).
	 * @param TimeScale Game speed (1-8): faster playtests; 2-4 is still realistic.
	 * @param MaxMinutes Real minutes before the bot gives up.
	 * @param bSkipToNight Survive: jump the clock to 19:00 once the fire burns.
	 * @return Started or why not.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult StartPlaytest(const FString& Scenario = TEXT("Survive"), const FString& Goals = TEXT(""), double TimeScale = 2.0,
		double MaxMinutes = 15.0, bool bSkipToNight = true);

	/**
	 * Starts Play-in-Editor with the bot visiting a grid of points over the landscape, looking around at each and
	 * measuring frame times: finds the slowest places (dense foliage, water, effects).
	 * @param Points Number of sample points (4-100).
	 * @return Started or why not.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult RunPerformanceSweep(int32 Points = 25);

	/**
	 * Progress of the running playtest (goals, recent bot log, telemetry so far), or the report of the last one.
	 * @return Status lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult GetPlaytestStatus();

	/**
	 * Ends the running playtest (stops Play) and returns its report.
	 * @return The report.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult StopPlaytest();

	/**
	 * Sets the performance and balance targets reports are checked against. Empty / negative = unchanged.
	 * @param TargetHardware "Steam Deck", "Low-end PC", "Mid-range PC", "High-end PC" or a description.
	 * @param TargetFPS Frames per second to hold (30, 60...); 0 = unchanged.
	 * @param FirstToolMinutes Minutes until a new player has a tool.
	 * @param FirstStationMinutes Minutes until the first station/fire.
	 * @param FirstBuildMinutes Minutes until the first building piece.
	 * @param MaxDeathsPerHour Acceptable deaths per hour.
	 * @param MaxStarvingShare Acceptable share of time with hunger or thirst empty (0-1).
	 * @return The targets.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult SetQATargets(const FString& TargetHardware = TEXT(""), double TargetFPS = 0.0, double FirstToolMinutes = -1.0,
		double FirstStationMinutes = -1.0, double FirstBuildMinutes = -1.0, double MaxDeathsPerHour = -1.0, double MaxStarvingShare = -1.0);

	/**
	 * Balance and performance over recent play sessions (Saved/AGBTelemetry): time to first tool/station/build/night,
	 * deaths per hour and causes, starving time, gathering and crafting rates, damage sources, frame times; each
	 * checked against the targets with suggestions.
	 * @param LastSessions How many recent sessions.
	 * @param bIncludeBot Include sessions played by the bot.
	 * @return Report lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|QA")
	static FPlaytestToolsResult GetBalanceReport(int32 LastSessions = 10, bool bIncludeBot = true);
};
