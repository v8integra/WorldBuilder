#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/WorldSubsystem.h"

#include "AGBPlaytest.generated.h"

class AAGBCharacter;
class AAGBCraftingStation;
class APlayerController;
class UAGBInventoryComponent;
class UAGBItemDefinition;
class UAGBRecipeDefinition;
class UAGBResourceDefinition;
class UPrimitiveComponent;

/** Performance and balance targets the playtests are checked against (Project Settings > Game > AI Game Builder QA). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "AI Game Builder QA"))
class AIGAMEBUILDERRUNTIME_API UAGBQASettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Description of the target machine ("Mid-range PC (GTX 1660 / RX 5600)", "Steam Deck"...). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	FString TargetHardware = TEXT("Mid-range PC");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "20", ClampMax = "240"))
	float TargetFPS = 60.f;

	/** A new player should have their first tool within (minutes). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = "0"))
	float FirstToolMinutes = 5.f;

	/** ... their first crafting station / campfire within (minutes). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = "0"))
	float FirstStationMinutes = 10.f;

	/** ... their first building piece within (minutes). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = "0"))
	float FirstBuildMinutes = 30.f;

	/** Deaths per hour of play above this are too punishing. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = "0"))
	float MaxDeathsPerHour = 2.f;

	/** Share of play time with hunger or thirst empty above this is too harsh (0.05 = 5%). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxStarvingShare = 0.05f;

	/** Record play sessions (Saved/AGBTelemetry) in editor and development builds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Telemetry")
	bool bRecordTelemetry = true;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	float GetFrameBudgetMs() const { return 1000.f / FMath::Max(1.f, TargetFPS); }
};

/** What a playtest bot should do (set by PlaytestTools before Play, or -AGBPlaytest=<scenario> on the command line). */
struct AIGAMEBUILDERRUNTIME_API FAGBPlaytestRequest
{
	/** Survive, Gather, Smoke, Explore, PerfSweep, Custom. */
	FString Scenario = TEXT("Survive");
	/** Custom goals: "gather:wood=10; craft:stone_axe; place:campfire; survive_night; explore:300; wait:30". */
	FString Goals;
	float TimeScale = 1.f;
	float MaxMinutes = 15.f;
	bool bSkipToNight = true;
	bool bQuitWhenDone = true;
	/** PerfSweep area (cm) and number of sample points. */
	FBox2D SweepArea = FBox2D(ForceInit);
	int32 SweepPoints = 25;
};

namespace AGBPlaytest
{
	/** The playtest the next Play session runs (consumed when play starts). */
	AIGAMEBUILDERRUNTIME_API TOptional<FAGBPlaytestRequest>& Pending();
	/** Report of the last finished playtest (also saved in Saved/AGBPlaytests). */
	AIGAMEBUILDERRUNTIME_API FString& LastReport();
	/** Report file of the last playtest. */
	AIGAMEBUILDERRUNTIME_API FString& LastReportPath();
}

/**
 * Records a play session: what players gathered, crafted, built, killed, ate; deaths and their causes; damage by
 * source; time to milestones (first tool, first station, first night survived...); time spent starving; frame,
 * game/render/GPU times and hitches. Written to Saved/AGBTelemetry/<time>.json when the session ends.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBTelemetrySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAGBTelemetrySubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Server: counts an event ("Gathered", "wood", 3). The first event of a category is a milestone. */
	void Record(const FString& Category, const FString& Key, float Amount = 1.f);

	/** Server: time of the first time something happened ("first_tool"). */
	void Milestone(const FString& Name);

	/** The session was played by the playtest bot. */
	bool bBot = false;

	/** Readable summary lines (events, milestones, performance against the budget). */
	void Describe(TArray<FString>& OutLines) const;

	/** Average and worst frame time (ms) since a moment (for the performance sweep). */
	float GetSessionSeconds() const;

private:
	bool IsActiveWorld() const;
	void Write() const;

	TMap<FString, TMap<FString, float>> Counters;
	TMap<FString, float> Milestones;
	TMap<FString, float> EmptySeconds;
	double StartTime = 0.0;
	bool bStarted = false;
	bool bWasNight = false;

	// Performance.
	TArray<float> FrameMs;
	double GameMsSum = 0.0;
	double RenderMsSum = 0.0;
	double GpuMsSum = 0.0;
	int32 Samples = 0;
	float PeakMemoryMB = 0.f;
	struct FHitch
	{
		float Time = 0.f;
		float Ms = 0.f;
		FVector Location = FVector::ZeroVector;
	};
	TArray<FHitch> Hitches;
	float DistanceMeters = 0.f;
	FVector LastPlayerLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
};

/**
 * Plays the game by itself (on the player's controller): works out what to gather and craft for its goals, walks to
 * trees and rocks, harvests with the right tool, crafts, places stations, fuels fires, eats and drinks, fights
 * back, survives a night; or runs smoke tests and a performance sweep. Reports what worked and where it got stuck.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBPlaytestBot : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBPlaytestBot();

	FAGBPlaytestRequest Request;

	/** Current state for status queries: result, goal, recent log. */
	void Describe(TArray<FString>& OutLines) const;
	bool IsFinished() const { return bFinished; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FGoal
	{
		FString Type;
		FString Arg;
		int32 Count = 1;
		FString Status;
		int32 Step = 0;
		double StartTime = 0.0;
	};

	struct FHarvestTarget
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		int32 Instance = INDEX_NONE;
		FVector Point = FVector::ZeroVector;
		const UAGBResourceDefinition* Resource = nullptr;
		int32 Swings = 0;
		int32 CountAtStart = 0;
		bool IsValid() const { return Component.IsValid(); }
	};

	enum class EStep : uint8 { Running, Done, Failed };

	void BuildGoals();
	void Log(const FString& Line);
	void Finish(bool bPassed, const FString& Reason);
	double Now() const;

	// Goals.
	EStep RunGoal(FGoal& Goal, AAGBCharacter* Me);
	EStep Obtain(AAGBCharacter* Me, UAGBItemDefinition* Item, int32 Count, int32 Depth, FString& OutReason);
	EStep CraftRecipe(AAGBCharacter* Me, UAGBRecipeDefinition* Recipe, int32 Count, int32 Depth, FString& OutReason);
	EStep Gather(AAGBCharacter* Me, UAGBItemDefinition* Item, int32 Count, int32 Depth, FString& OutReason);
	EStep PlaceItem(AAGBCharacter* Me, UAGBItemDefinition* Item, int32 Depth, FString& OutReason);
	EStep SurviveNight(FGoal& Goal, AAGBCharacter* Me, FString& OutReason);
	EStep Explore(FGoal& Goal, AAGBCharacter* Me);
	EStep Smoke(FGoal& Goal, AAGBCharacter* Me, FString& OutReason);
	EStep PerfSweep(FGoal& Goal, AAGBCharacter* Me);

	// Needs and threats.
	bool HandleThreats(AAGBCharacter* Me);
	bool HandleNeeds(AAGBCharacter* Me);
	bool FuelStation(AAGBCharacter* Me, AAGBCraftingStation* Station, int32 Depth, FString& OutReason, EStep& OutStep);

	// Actions.
	void MoveTo(const FVector& Point, float AcceptRadius);
	void StopMoving();
	void DriveMovement(AAGBCharacter* Me, float DeltaTime);
	void AimAt(const FVector& Point);
	bool FindHarvestTarget(AAGBCharacter* Me, const UAGBItemDefinition* Item, FHarvestTarget& Out);
	bool SelectBestTool(AAGBCharacter* Me, const UAGBResourceDefinition* Resource);
	bool SelectItem(AAGBCharacter* Me, const UAGBItemDefinition* Item);
	bool SelectWeapon(AAGBCharacter* Me);
	bool FindStack(AAGBCharacter* Me, TFunctionRef<bool(const UAGBItemDefinition*)> Match, UAGBInventoryComponent*& OutInventory, int32& OutSlot) const;
	AAGBCraftingStation* FindStation(AAGBCharacter* Me, const UObject* Definition, float MaxDistance) const;
	UAGBRecipeDefinition* FindRecipeFor(const UAGBItemDefinition* Item) const;
	bool FindGround(const FVector& Near, FVector& OutGround) const;

	TArray<FGoal> Goals;
	int32 GoalIndex = 0;
	TArray<FString> LogLines;
	bool bFinished = false;
	bool bPassed = false;
	bool bSetup = false;
	double StartRealTime = 0.0;
	double QuitAt = 0.0;
	float DecisionTimer = 0.f;

	// Movement.
	bool bMoving = false;
	FVector MoveGoal = FVector::ZeroVector;
	float MoveRadius = 100.f;
	FVector StuckCheckLocation = FVector::ZeroVector;
	double StuckCheckTime = 0.0;
	int32 StuckJumps = 0;
	bool bStuck = false;

	// Current work.
	FHarvestTarget Harvest;
	TSet<FString> Blacklist;
	int32 FailedSearches = 0;
	double ActionTime = 0.0;
	int32 Attempts = 0;
	double LastNeedTime = -100.0;
	bool bAimedAtWater = false;
	FString LastWarning;

	// Performance sweep.
	TArray<FString> SweepResults;
	TArray<TPair<float, FString>> SweepScores;
	float SweepFrameSum = 0.f;
	float SweepFrameMax = 0.f;
	int32 SweepFrames = 0;
};
