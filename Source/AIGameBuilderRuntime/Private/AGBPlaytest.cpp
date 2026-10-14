#include "AGBPlaytest.h"

#include "AGBCharacter.h"
#include "AGBCraftingComponent.h"
#include "AGBCraftingStation.h"
#include "AGBCraftingTypes.h"
#include "AGBCreature.h"
#include "AGBCreatureTypes.h"
#include "AGBGameDataSubsystem.h"
#include "AGBGameFramework.h"
#include "AGBHarvestSubsystem.h"
#include "AGBHarvestTypes.h"
#include "AGBInteractionComponent.h"
#include "AGBInventoryComponent.h"
#include "AGBItemTypes.h"
#include "AGBMenus.h"
#include "AGBSaveGame.h"
#include "AGBVitalsComponent.h"
#include "AGBWorldEnvironment.h"
#include "AIGameBuilderRuntime.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"
#include "Algo/Accumulate.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace AGBPlaytest
{
	TOptional<FAGBPlaytestRequest>& Pending()
	{
		static TOptional<FAGBPlaytestRequest> Request;
		return Request;
	}

	FString& LastReport()
	{
		static FString Report;
		return Report;
	}

	FString& LastReportPath()
	{
		static FString Path;
		return Path;
	}
}

namespace
{
	const FName WaterProfile(TEXT("WaterBodyCollision"));
	const FName HungerStat(TEXT("Hunger"));

	FString Minutes(float Seconds)
	{
		return FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Seconds / 60.f), FMath::FloorToInt(Seconds) % 60);
	}

	FString Name(const UAGBItemDefinition* Item)
	{
		return Item ? Item->GetDisplayNameOrId().ToString() : FString(TEXT("?"));
	}

	/** Collects errors logged during a playtest (smoke test). */
	class FErrorCapture : public FOutputDevice
	{
	public:
		TArray<FString> Errors;
		int32 Warnings = 0;

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal)
			{
				if (Errors.Num() < 20)
				{
					Errors.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), Message));
				}
			}
			else if (Verbosity == ELogVerbosity::Warning)
			{
				++Warnings;
			}
		}
	};
	FErrorCapture ErrorCapture;
}

// ---------------------------------------------------------------- telemetry

UAGBTelemetrySubsystem* UAGBTelemetrySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAGBTelemetrySubsystem>() : nullptr;
}

bool UAGBTelemetrySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
#if UE_BUILD_SHIPPING
	return false;
#else
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && GetDefault<UAGBQASettings>()->bRecordTelemetry
		&& Super::ShouldCreateSubsystem(Outer);
#endif
}

void UAGBTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartTime = InWorld.GetTimeSeconds();
	// Only game levels (with an AI Game Builder game mode) are recorded, not the main menu.
	bStarted = InWorld.GetAuthGameMode() == nullptr || InWorld.GetAuthGameMode()->IsA<AAGBGameMode>();
}

void UAGBTelemetrySubsystem::Deinitialize()
{
	if (bStarted && GetSessionSeconds() > 20.f)
	{
		Write();
	}
	Super::Deinitialize();
}

TStatId UAGBTelemetrySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAGBTelemetrySubsystem, STATGROUP_Tickables);
}

bool UAGBTelemetrySubsystem::IsActiveWorld() const
{
	return bStarted && GetWorld() && GetWorld()->HasBegunPlay();
}

float UAGBTelemetrySubsystem::GetSessionSeconds() const
{
	return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - StartTime) : 0.f;
}

void UAGBTelemetrySubsystem::Record(const FString& Category, const FString& Key, float Amount)
{
	if (!IsActiveWorld())
	{
		return;
	}
	TMap<FString, float>& Entries = Counters.FindOrAdd(Category);
	if (Entries.IsEmpty())
	{
		Milestone(TEXT("first_") + Category.ToLower());
	}
	Entries.FindOrAdd(Key) += Amount;
}

void UAGBTelemetrySubsystem::Milestone(const FString& Name)
{
	if (IsActiveWorld() && !Milestones.Contains(Name))
	{
		Milestones.Add(Name, GetSessionSeconds());
	}
}

void UAGBTelemetrySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!IsActiveWorld())
	{
		return;
	}

	// Performance (machines that draw the game).
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		const float Frame = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
		FrameMs.Add(Frame);
		GameMsSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
		RenderMsSum += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		GpuMsSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		++Samples;
		PeakMemoryMB = FMath::Max(PeakMemoryMB, static_cast<float>(FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0)));
		const float HitchMs = FMath::Max(60.f, GetDefault<UAGBQASettings>()->GetFrameBudgetMs() * 3.f);
		if (Frame > HitchMs && GetSessionSeconds() > 5.f && Hitches.Num() < 200)
		{
			const APawn* Pawn = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
			Hitches.Add({ GetSessionSeconds(), Frame, Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector });
		}
	}

	// Player state (server): starving time, distance, surviving the night.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		const UAGBVitalsComponent* Vitals = Pawn ? Pawn->FindComponentByClass<UAGBVitalsComponent>() : nullptr;
		if (!Vitals || Vitals->IsDead())
		{
			continue;
		}
		for (const FName Stat : { HungerStat, UAGBVitalsComponent::ThirstStat })
		{
			if (Vitals->HasStat(Stat) && Vitals->GetFraction(Stat) <= 0.001f)
			{
				EmptySeconds.FindOrAdd(Stat.ToString()) += DeltaTime;
			}
		}
		if (It.GetIndex() == 0)
		{
			const FVector Location = Pawn->GetActorLocation();
			if (bHasLastLocation)
			{
				const float Step = static_cast<float>(FVector::Dist2D(Location, LastPlayerLocation));
				DistanceMeters += Step < 2000.f ? Step / 100.f : 0.f; // Ignore teleports.
			}
			LastPlayerLocation = Location;
			bHasLastLocation = true;
		}
	}
	if (const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(World))
	{
		const bool bNight = Environment->IsNight();
		if (bWasNight && !bNight)
		{
			Milestone(TEXT("first_night_survived"));
			Counters.FindOrAdd(TEXT("Nights")).FindOrAdd(TEXT("survived")) += 1.f;
		}
		bWasNight = bNight;
	}
}

void UAGBTelemetrySubsystem::Describe(TArray<FString>& OutLines) const
{
	const float Seconds = GetSessionSeconds();
	OutLines.Add(FString::Printf(TEXT("Session %s (game time), %.0f m walked%s"), *Minutes(Seconds), DistanceMeters, bBot ? TEXT(", played by the bot") : TEXT("")));
	TArray<FString> Keys;
	Milestones.GetKeys(Keys);
	Keys.Sort([this](const FString& A, const FString& B) { return Milestones[A] < Milestones[B]; });
	TArray<FString> Parts;
	for (const FString& Key : Keys)
	{
		Parts.Add(FString::Printf(TEXT("%s %s"), *Key, *Minutes(Milestones[Key])));
	}
	if (Parts.Num() > 0)
	{
		OutLines.Add(TEXT("Milestones: ") + FString::Join(Parts, TEXT(", ")));
	}
	for (const TPair<FString, TMap<FString, float>>& Category : Counters)
	{
		Parts.Reset();
		for (const TPair<FString, float>& Entry : Category.Value)
		{
			Parts.Add(FString::Printf(TEXT("%s %g"), *Entry.Key, FMath::RoundToFloat(Entry.Value * 10.f) / 10.f));
		}
		OutLines.Add(FString::Printf(TEXT("%s: %s"), *Category.Key, *FString::Join(Parts, TEXT(", "))));
	}
	for (const TPair<FString, float>& Empty : EmptySeconds)
	{
		OutLines.Add(FString::Printf(TEXT("%s empty for %s (%.0f%% of the session)"), *Empty.Key, *Minutes(Empty.Value), 100.f * Empty.Value / FMath::Max(1.f, Seconds)));
	}
	if (FrameMs.Num() > 0)
	{
		const UAGBQASettings* QA = GetDefault<UAGBQASettings>();
		TArray<float> Sorted = FrameMs;
		Sorted.Sort();
		const float Average = static_cast<float>(Algo::Accumulate(Sorted, 0.0) / Sorted.Num());
		const float P95 = Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt(Sorted.Num() * 0.95f))];
		const float P99 = Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt(Sorted.Num() * 0.99f))];
		const float Budget = QA->GetFrameBudgetMs();
		OutLines.Add(FString::Printf(TEXT("Performance (target %s, %.0f fps = %.1f ms): frame avg %.1f ms (%.0f fps), 95%% %.1f ms, 1%% low %.0f fps; game %.1f, render %.1f, GPU %.1f ms; peak memory %.0f MB -> %s"),
			*QA->TargetHardware, QA->TargetFPS, Budget, Average, 1000.f / FMath::Max(0.1f, Average), P95, 1000.f / FMath::Max(0.1f, P99),
			GameMsSum / Samples, RenderMsSum / Samples, GpuMsSum / Samples, PeakMemoryMB,
			P95 <= Budget ? TEXT("within budget") : (Average <= Budget ? TEXT("average OK, spikes over budget") : TEXT("OVER BUDGET"))));
		if (Hitches.Num() > 0)
		{
			Parts.Reset();
			for (int32 Index = 0; Index < Hitches.Num() && Index < 8; ++Index)
			{
				Parts.Add(FString::Printf(TEXT("%.0f ms at %s near (%.0f, %.0f) m"), Hitches[Index].Ms, *Minutes(Hitches[Index].Time), Hitches[Index].Location.X / 100.0, Hitches[Index].Location.Y / 100.0));
			}
			OutLines.Add(FString::Printf(TEXT("Hitches: %d (%s)"), Hitches.Num(), *FString::Join(Parts, TEXT("; "))));
		}
		if (GIsEditor)
		{
			OutLines.Add(TEXT("(Measured in Play-in-Editor: the editor adds cost; a Standalone or packaged game is faster.)"));
		}
	}
}

void UAGBTelemetrySubsystem::Write() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("map"), GetWorld()->GetMapName());
	Root->SetStringField(TEXT("date"), FDateTime::Now().ToIso8601());
	Root->SetBoolField(TEXT("bot"), bBot);
	Root->SetNumberField(TEXT("seconds"), GetSessionSeconds());
	Root->SetNumberField(TEXT("distance_m"), DistanceMeters);
	TSharedRef<FJsonObject> CounterObject = MakeShared<FJsonObject>();
	for (const TPair<FString, TMap<FString, float>>& Category : Counters)
	{
		TSharedRef<FJsonObject> Entries = MakeShared<FJsonObject>();
		for (const TPair<FString, float>& Entry : Category.Value)
		{
			Entries->SetNumberField(Entry.Key, Entry.Value);
		}
		CounterObject->SetObjectField(Category.Key, Entries);
	}
	Root->SetObjectField(TEXT("counters"), CounterObject);
	TSharedRef<FJsonObject> MilestoneObject = MakeShared<FJsonObject>();
	for (const TPair<FString, float>& Entry : Milestones)
	{
		MilestoneObject->SetNumberField(Entry.Key, Entry.Value);
	}
	Root->SetObjectField(TEXT("milestones"), MilestoneObject);
	TSharedRef<FJsonObject> EmptyObject = MakeShared<FJsonObject>();
	for (const TPair<FString, float>& Entry : EmptySeconds)
	{
		EmptyObject->SetNumberField(Entry.Key, Entry.Value);
	}
	Root->SetObjectField(TEXT("empty_seconds"), EmptyObject);
	if (FrameMs.Num() > 0)
	{
		TArray<float> Sorted = FrameMs;
		Sorted.Sort();
		TSharedRef<FJsonObject> Perf = MakeShared<FJsonObject>();
		Perf->SetNumberField(TEXT("frame_avg_ms"), Algo::Accumulate(Sorted, 0.0) / Sorted.Num());
		Perf->SetNumberField(TEXT("frame_p95_ms"), Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt(Sorted.Num() * 0.95f))]);
		Perf->SetNumberField(TEXT("game_ms"), GameMsSum / Samples);
		Perf->SetNumberField(TEXT("render_ms"), RenderMsSum / Samples);
		Perf->SetNumberField(TEXT("gpu_ms"), GpuMsSum / Samples);
		Perf->SetNumberField(TEXT("peak_memory_mb"), PeakMemoryMB);
		Perf->SetNumberField(TEXT("hitches"), Hitches.Num());
		Perf->SetBoolField(TEXT("editor"), GIsEditor);
		Root->SetObjectField(TEXT("performance"), Perf);
	}
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AGBTelemetry") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + (bBot ? TEXT("_bot") : TEXT("")) + TEXT(".json");
	FFileHelper::SaveStringToFile(Text, *Path);
	UE_LOG(LogAIGameBuilder, Log, TEXT("Telemetry written: %s"), *Path);
}

// ---------------------------------------------------------------- bot: setup and reporting

UAGBPlaytestBot::UAGBPlaytestBot()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

double UAGBPlaytestBot::Now() const
{
	return GetWorld()->GetRealTimeSeconds();
}

void UAGBPlaytestBot::BeginPlay()
{
	Super::BeginPlay();
	StartRealTime = Now();
	ErrorCapture.Errors.Reset();
	ErrorCapture.Warnings = 0;
	GLog->AddOutputDevice(&ErrorCapture);
	BuildGoals();
	Log(FString::Printf(TEXT("Playtest '%s' started: %d goal(s), time x%.1f, limit %.0f min."), *Request.Scenario, Goals.Num(), Request.TimeScale, Request.MaxMinutes));
}

void UAGBPlaytestBot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!bFinished)
	{
		Finish(false, TEXT("Play stopped before the playtest finished"));
	}
	GLog->RemoveOutputDevice(&ErrorCapture);
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	Super::EndPlay(EndPlayReason);
}

void UAGBPlaytestBot::Log(const FString& Line)
{
	const FString Stamped = FString::Printf(TEXT("[%s] %s"), *Minutes(static_cast<float>(Now() - StartRealTime)), *Line);
	LogLines.Add(Stamped);
	UE_LOG(LogAIGameBuilder, Log, TEXT("Playtest %s"), *Stamped);
}

void UAGBPlaytestBot::Describe(TArray<FString>& OutLines) const
{
	OutLines.Add(FString::Printf(TEXT("Scenario %s: %s, %s elapsed"), *Request.Scenario,
		bFinished ? (bPassed ? TEXT("PASSED") : TEXT("FAILED")) : TEXT("running"), *Minutes(static_cast<float>(Now() - StartRealTime))));
	for (int32 Index = 0; Index < Goals.Num(); ++Index)
	{
		const FGoal& Goal = Goals[Index];
		OutLines.Add(FString::Printf(TEXT("  %s %s%s: %s"), *Goal.Type, *Goal.Arg, Goal.Count > 1 ? *FString::Printf(TEXT(" x%d"), Goal.Count) : TEXT(""),
			Goal.Status.IsEmpty() ? (Index == GoalIndex && !bFinished ? TEXT("in progress") : TEXT("not reached")) : *Goal.Status));
	}
	for (int32 Index = FMath::Max(0, LogLines.Num() - 25); Index < LogLines.Num(); ++Index)
	{
		OutLines.Add(LogLines[Index]);
	}
}

void UAGBPlaytestBot::Finish(bool bInPassed, const FString& Reason)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	bPassed = bInPassed;
	StopMoving();
	Log(FString::Printf(TEXT("Finished: %s. %s"), bPassed ? TEXT("PASSED") : TEXT("FAILED"), *Reason));
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);

	TArray<FString> Lines;
	Describe(Lines);
	if (SweepResults.Num() > 0)
	{
		Lines.Add(TEXT("Performance sweep (slowest first):"));
		SweepScores.Sort([](const TPair<float, FString>& A, const TPair<float, FString>& B) { return A.Key > B.Key; });
		for (int32 Index = 0; Index < SweepScores.Num() && Index < 8; ++Index)
		{
			Lines.Add(TEXT("  ") + SweepScores[Index].Value);
		}
	}
	if (const UAGBTelemetrySubsystem* Telemetry = UAGBTelemetrySubsystem::Get(this))
	{
		Lines.Add(TEXT("Telemetry:"));
		Telemetry->Describe(Lines);
	}
	Lines.Add(FString::Printf(TEXT("Log: %d error(s), %d warning(s) during the run%s"), ErrorCapture.Errors.Num(), ErrorCapture.Warnings,
		ErrorCapture.Errors.IsEmpty() ? TEXT("") : *(TEXT(": ") + FString::Join(ErrorCapture.Errors, TEXT(" | ")))));
	Lines.Add(FString::Printf(TEXT("Full log in this report; earlier lines: %d"), LogLines.Num()));
	FString Report = FString::Join(Lines, TEXT("\n")) + TEXT("\n\nFull bot log:\n") + FString::Join(LogLines, TEXT("\n"));
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AGBPlaytests") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_")) + Request.Scenario + TEXT(".txt");
	FFileHelper::SaveStringToFile(Report, *Path);
	AGBPlaytest::LastReport() = Report;
	AGBPlaytest::LastReportPath() = Path;
	QuitAt = Request.bQuitWhenDone ? Now() + 3.0 : 0.0;
}

void UAGBPlaytestBot::BuildGoals()
{
	Goals.Reset();
	auto Add = [this](const FString& Type, const FString& Arg = FString(), int32 Count = 1)
	{
		FGoal& Goal = Goals.AddDefaulted_GetRef();
		Goal.Type = Type;
		Goal.Arg = Arg;
		Goal.Count = Count;
	};
	const FString Scenario = Request.Scenario.ToLower();
	const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
	if (Scenario == TEXT("smoke"))
	{
		Add(TEXT("smoke"));
	}
	else if (Scenario == TEXT("perfsweep"))
	{
		Add(TEXT("perfsweep"));
	}
	else if (Scenario == TEXT("photos"))
	{
		Add(TEXT("photos"), FString(), Request.PhotoCameras.Num());
	}
	else if (Scenario == TEXT("explore"))
	{
		Add(TEXT("explore"), FString(), 500);
	}
	else if (Scenario == TEXT("gather"))
	{
		// The first yield of each kind of resource.
		const UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
		TSet<const UAGBItemDefinition*> Seen;
		for (const UAGBResourceDefinition* Resource : Harvesting ? Harvesting->GetResources() : TArray<TObjectPtr<UAGBResourceDefinition>>())
		{
			const UAGBItemDefinition* Item = Resource && Resource->YieldPerHit.Num() > 0 ? Resource->YieldPerHit[0].Item.Get() : nullptr;
			if (Item && !Seen.Contains(Item) && Seen.Num() < 5)
			{
				Seen.Add(Item);
				Add(TEXT("gather"), Item->ItemId.ToString(), 10);
			}
		}
	}
	else if (Scenario == TEXT("custom"))
	{
		TArray<FString> Parts;
		Request.Goals.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& Part : Parts)
		{
			FString Type = Part.TrimStartAndEnd(), Arg, CountText;
			Type.Split(TEXT(":"), &Type, &Arg);
			if (Arg.Split(TEXT("="), &Arg, &CountText))
			{
				Add(Type.ToLower(), Arg.TrimStartAndEnd(), FMath::Max(1, FCString::Atoi(*CountText)));
			}
			else
			{
				Add(Type.ToLower(), Arg.TrimStartAndEnd(), Type.ToLower() == TEXT("explore") ? FMath::Max(1, FCString::Atoi(*Arg)) : 1);
			}
		}
	}
	else
	{
		// Survive: the first tool, a fire, then a night.
		if (Data)
		{
			for (const UAGBRecipeDefinition* Recipe : Data->GetRecipes())
			{
				const UAGBItemDefinition* Output = Recipe && Recipe->Outputs.Num() > 0 ? Recipe->Outputs[0].Item.Get() : nullptr;
				if (Output && Output->Category == EAGBItemCategory::Tool)
				{
					Add(TEXT("craft"), Recipe->RecipeId.ToString());
					break;
				}
			}
			for (const UAGBItemDefinition* Item : Data->GetItems())
			{
				if (Item && Item->PlacesStation && Item->PlacesStation->WarmthC > 0.f)
				{
					Add(TEXT("place"), Item->ItemId.ToString());
					break;
				}
			}
		}
		Add(TEXT("survive_night"));
	}
	if (Goals.IsEmpty())
	{
		Add(TEXT("wait"), TEXT("10"));
	}
}

// ---------------------------------------------------------------- bot: main loop

void UAGBPlaytestBot::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bFinished)
	{
		if (QuitAt > 0.0 && Now() >= QuitAt)
		{
			QuitAt = 0.0;
			UKismetSystemLibrary::QuitGame(this, Cast<APlayerController>(GetOwner()), EQuitPreference::Quit, false);
		}
		return;
	}
	if (Now() - StartRealTime > Request.MaxMinutes * 60.0)
	{
		Finish(false, FString::Printf(TEXT("Time limit of %.0f minutes reached"), Request.MaxMinutes));
		return;
	}
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	AAGBCharacter* Me = Player ? Cast<AAGBCharacter>(Player->GetPawn()) : nullptr;
	if (!Me || Me->IsDead())
	{
		if (LastWarning != TEXT("dead"))
		{
			LastWarning = TEXT("dead");
			if (Me)
			{
				Log(TEXT("Died; waiting to respawn."));
			}
		}
		bMoving = false;
		return;
	}
	if (LastWarning == TEXT("dead"))
	{
		LastWarning.Reset();
		Harvest = FHarvestTarget();
	}
	if (!bSetup)
	{
		bSetup = true;
		Me->SetCameraMode(EAGBCameraMode::FirstPerson); // Aiming follows the camera exactly.
		if (UAGBTelemetrySubsystem* Telemetry = UAGBTelemetrySubsystem::Get(this))
		{
			Telemetry->bBot = true;
		}
		UGameplayStatics::SetGlobalTimeDilation(this, FMath::Clamp(Request.TimeScale, 0.5f, 8.f));
	}

	if (bMoving)
	{
		DriveMovement(Me, DeltaTime);
	}
	if (Goals.IsValidIndex(GoalIndex) && Goals[GoalIndex].Type == TEXT("perfsweep"))
	{
		// Sample every frame during the sweep.
		const float Frame = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
		SweepFrameSum += Frame;
		SweepFrameMax = FMath::Max(SweepFrameMax, Frame);
		++SweepFrames;
	}
	DecisionTimer -= DeltaTime / FMath::Max(0.1f, static_cast<float>(UGameplayStatics::GetGlobalTimeDilation(this)));
	if (DecisionTimer > 0.f)
	{
		return;
	}
	DecisionTimer = 0.2f;

	if (!Goals.IsValidIndex(GoalIndex))
	{
		const bool bAllPassed = !Goals.ContainsByPredicate([](const FGoal& Goal) { return !Goal.Status.StartsWith(TEXT("PASS")); });
		Finish(bAllPassed, bAllPassed ? TEXT("All goals reached") : TEXT("Some goals failed"));
		return;
	}
	const bool bSweep = Goals[GoalIndex].Type == TEXT("perfsweep") || Goals[GoalIndex].Type == TEXT("photos");
	if (!bSweep && (HandleThreats(Me) || HandleNeeds(Me)))
	{
		return;
	}
	FGoal& Goal = Goals[GoalIndex];
	if (Goal.StartTime == 0.0)
	{
		Goal.StartTime = Now();
		Log(FString::Printf(TEXT("Goal: %s %s%s"), *Goal.Type, *Goal.Arg, Goal.Count > 1 ? *FString::Printf(TEXT(" x%d"), Goal.Count) : TEXT("")));
	}
	const EStep Step = RunGoal(Goal, Me);
	if (Step == EStep::Running)
	{
		return;
	}
	if (Step == EStep::Done && Goal.Status.IsEmpty())
	{
		Goal.Status = FString::Printf(TEXT("PASS in %s"), *Minutes(static_cast<float>(Now() - Goal.StartTime)));
	}
	Log(FString::Printf(TEXT("%s %s: %s"), *Goal.Type, *Goal.Arg, *Goal.Status));
	StopMoving();
	Harvest = FHarvestTarget();
	FailedSearches = 0;
	Attempts = 0;
	++GoalIndex;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::RunGoal(FGoal& Goal, AAGBCharacter* Me)
{
	FString Reason;
	EStep Step = EStep::Running;
	const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
	auto FindItem = [Data](const FString& Id) -> UAGBItemDefinition*
	{
		for (UAGBItemDefinition* Item : Data ? Data->GetItems() : TArray<TObjectPtr<UAGBItemDefinition>>())
		{
			if (Item && Item->ItemId.ToString().Equals(Id, ESearchCase::IgnoreCase))
			{
				return Item;
			}
		}
		return nullptr;
	};
	if (Goal.Type == TEXT("gather") || Goal.Type == TEXT("get"))
	{
		UAGBItemDefinition* Item = FindItem(Goal.Arg);
		Step = Item ? Obtain(Me, Item, Goal.Count, 0, Reason) : EStep::Failed;
		Reason = Item ? Reason : FString::Printf(TEXT("no item '%s'"), *Goal.Arg);
	}
	else if (Goal.Type == TEXT("craft"))
	{
		UAGBRecipeDefinition* Recipe = nullptr;
		for (UAGBRecipeDefinition* Candidate : Data ? Data->GetRecipes() : TArray<TObjectPtr<UAGBRecipeDefinition>>())
		{
			if (Candidate && Candidate->RecipeId.ToString().Equals(Goal.Arg, ESearchCase::IgnoreCase))
			{
				Recipe = Candidate;
			}
		}
		if (!Recipe)
		{
			Step = EStep::Failed;
			Reason = FString::Printf(TEXT("no recipe '%s'"), *Goal.Arg);
		}
		else
		{
			// Done when the player holds Count more of the output than at the start of the goal.
			UAGBItemDefinition* Output = Recipe->Outputs.Num() > 0 ? Recipe->Outputs[0].Item.Get() : nullptr;
			if (Goal.Step == 0)
			{
				Goal.Step = 1 + (Output ? Me->CountItem(Output) : 0);
			}
			const int32 Target = Goal.Step - 1 + Goal.Count * (Recipe->Outputs.Num() > 0 ? Recipe->Outputs[0].Count : 1);
			Step = Output && Me->CountItem(Output) >= Target ? EStep::Done : CraftRecipe(Me, Recipe, 1, 0, Reason);
		}
	}
	else if (Goal.Type == TEXT("place"))
	{
		UAGBItemDefinition* Item = FindItem(Goal.Arg);
		Step = Item ? PlaceItem(Me, Item, 0, Reason) : EStep::Failed;
		Reason = Item ? Reason : FString::Printf(TEXT("no item '%s'"), *Goal.Arg);
	}
	else if (Goal.Type == TEXT("survive_night"))
	{
		Step = SurviveNight(Goal, Me, Reason);
	}
	else if (Goal.Type == TEXT("explore"))
	{
		Step = Explore(Goal, Me);
	}
	else if (Goal.Type == TEXT("smoke"))
	{
		Step = Smoke(Goal, Me, Reason);
	}
	else if (Goal.Type == TEXT("perfsweep"))
	{
		Step = PerfSweep(Goal, Me);
	}
	else if (Goal.Type == TEXT("photos"))
	{
		Step = Photos(Goal, Me);
	}
	else if (Goal.Type == TEXT("wait"))
	{
		Step = Now() - Goal.StartTime >= FCString::Atof(*Goal.Arg) ? EStep::Done : EStep::Running;
	}
	else
	{
		Step = EStep::Failed;
		Reason = TEXT("unknown goal type (gather, craft, place, survive_night, explore, wait)");
	}
	if (Step == EStep::Failed && Goal.Status.IsEmpty())
	{
		Goal.Status = TEXT("FAIL: ") + Reason;
	}
	return Step;
}

// ---------------------------------------------------------------- bot: planning (obtain / craft / gather / place)

UAGBRecipeDefinition* UAGBPlaytestBot::FindRecipeFor(const UAGBItemDefinition* Item) const
{
	const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
	for (UAGBRecipeDefinition* Recipe : Data ? Data->GetRecipes() : TArray<TObjectPtr<UAGBRecipeDefinition>>())
	{
		if (Recipe && Recipe->Outputs.ContainsByPredicate([Item](const FAGBItemAmount& Output) { return Output.Item == Item; }))
		{
			return Recipe;
		}
	}
	return nullptr;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::Obtain(AAGBCharacter* Me, UAGBItemDefinition* Item, int32 Count, int32 Depth, FString& OutReason)
{
	if (Me->CountItem(Item) >= Count)
	{
		return EStep::Done;
	}
	if (Depth > 6)
	{
		OutReason = FString::Printf(TEXT("%s needs too many steps (recipe loop?)"), *Name(Item));
		return EStep::Failed;
	}
	// Gathering first (most survival materials come from resources), then crafting.
	const UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
	const bool bResource = Harvesting && Harvesting->GetResources().ContainsByPredicate([Item](const UAGBResourceDefinition* Resource)
	{
		auto Yields = [Item](const TArray<FAGBResourceYield>& List) { return List.ContainsByPredicate([Item](const FAGBResourceYield& Y) { return Y.Item == Item; }); };
		return Resource && (Yields(Resource->YieldPerHit) || Yields(Resource->YieldWhenDepleted));
	});
	if (bResource)
	{
		return Gather(Me, Item, Count, Depth, OutReason);
	}
	if (UAGBRecipeDefinition* Recipe = FindRecipeFor(Item))
	{
		return CraftRecipe(Me, Recipe, 1, Depth + 1, OutReason);
	}
	OutReason = FString::Printf(TEXT("no resource or recipe gives %s (only loot/starting items?)"), *Name(Item));
	return EStep::Failed;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::CraftRecipe(AAGBCharacter* Me, UAGBRecipeDefinition* Recipe, int32 Count, int32 Depth, FString& OutReason)
{
	UAGBCraftingComponent* Crafting = Me->Crafting;
	if (!Crafting)
	{
		OutReason = TEXT("the character has no crafting component");
		return EStep::Failed;
	}
	// A craft in progress: wait for it.
	if (Crafting->GetQueue().Num() > 0)
	{
		if (Now() - ActionTime > Recipe->CraftSeconds * 3.0 + 30.0 && ActionTime > 0.0)
		{
			OutReason = FString::Printf(TEXT("crafting %s never finished"), *Recipe->GetDisplayNameOrId().ToString());
			return EStep::Failed;
		}
		return EStep::Running;
	}
	// Ingredients.
	for (const FAGBItemAmount& Ingredient : Recipe->Ingredients)
	{
		if (Ingredient.Item && Me->CountItem(Ingredient.Item) < Ingredient.Count * Count)
		{
			const EStep Step = Obtain(Me, Ingredient.Item, Ingredient.Count * Count, Depth + 1, OutReason);
			if (Step != EStep::Done)
			{
				if (Step == EStep::Failed)
				{
					OutReason = FString::Printf(TEXT("%s: %s"), *Recipe->GetDisplayNameOrId().ToString(), *OutReason);
				}
				return Step;
			}
		}
	}
	// Recipes unlocked by discovery appear once the ingredients are carried.
	if (!Crafting->KnowsRecipe(Recipe))
	{
		if (++Attempts > 25)
		{
			OutReason = FString::Printf(TEXT("recipe %s never became known (unlock rule?)"), *Recipe->GetDisplayNameOrId().ToString());
			return EStep::Failed;
		}
		return EStep::Running;
	}
	// Station.
	if (Recipe->Station)
	{
		AAGBCraftingStation* Station = FindStation(Me, Recipe->Station, 6000.f);
		if (!Station)
		{
			const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
			UAGBItemDefinition* StationItem = Data ? Data->FindItemPlacing(Recipe->Station) : nullptr;
			if (!StationItem)
			{
				OutReason = FString::Printf(TEXT("%s needs a %s but no item places one"), *Recipe->GetDisplayNameOrId().ToString(), *Recipe->Station->GetDisplayNameOrId().ToString());
				return EStep::Failed;
			}
			return PlaceItem(Me, StationItem, Depth + 1, OutReason);
		}
		if (!Station->IsInRange(Me))
		{
			MoveTo(Station->GetActorLocation(), 150.f);
			return EStep::Running;
		}
		EStep FuelStep = EStep::Done;
		if (FuelStation(Me, Station, Depth, OutReason, FuelStep))
		{
			return FuelStep;
		}
	}
	if (Crafting->GetMaxCraftable(Recipe) <= 0)
	{
		return EStep::Running; // Inventory updates arrive a moment later.
	}
	StopMoving();
	Crafting->RequestCraft(Recipe, 1);
	ActionTime = Now();
	Attempts = 0;
	Log(FString::Printf(TEXT("Crafting %s"), *Recipe->GetDisplayNameOrId().ToString()));
	return EStep::Running;
}

bool UAGBPlaytestBot::FuelStation(AAGBCharacter* Me, AAGBCraftingStation* Station, int32 Depth, FString& OutReason, EStep& OutStep)
{
	if (!Station->Definition || !Station->Definition->bNeedsFuel || Station->GetFuelSeconds() > 45.f)
	{
		return false;
	}
	const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
	UAGBItemDefinition* Fuel = nullptr;
	for (UAGBItemDefinition* Item : Data ? Data->GetItems() : TArray<TObjectPtr<UAGBItemDefinition>>())
	{
		if (Item && Item->HasTag(TEXT("Fuel")) && (Me->CountItem(Item) > 0 || !Fuel))
		{
			Fuel = Item;
			if (Me->CountItem(Item) > 0)
			{
				break;
			}
		}
	}
	if (!Fuel)
	{
		OutReason = FString::Printf(TEXT("%s needs fuel but no item has the Fuel tag"), *Station->Definition->GetDisplayNameOrId().ToString());
		OutStep = EStep::Failed;
		return true;
	}
	if (Me->CountItem(Fuel) < 1)
	{
		OutStep = Obtain(Me, Fuel, 3, Depth + 1, OutReason);
		return true;
	}
	if (!Station->IsInRange(Me))
	{
		MoveTo(Station->GetActorLocation(), 150.f);
		OutStep = EStep::Running;
		return true;
	}
	Me->Crafting->RequestAddFuel(Station);
	Log(FString::Printf(TEXT("Fueling %s with %s"), *Station->Definition->GetDisplayNameOrId().ToString(), *Name(Fuel)));
	OutStep = EStep::Running;
	return true;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::Gather(AAGBCharacter* Me, UAGBItemDefinition* Item, int32 Count, int32 Depth, FString& OutReason)
{
	if (Me->CountItem(Item) >= Count)
	{
		Harvest = FHarvestTarget();
		return EStep::Done;
	}
	UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Harvest.Component.Get());
	const UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
	const bool bGone = Harvest.IsValid() && ((Instanced && Harvesting->IsHarvested(Instanced, Harvest.Instance))
		|| !Harvesting->FindResource(Harvest.Component.Get(), Harvest.Instance));
	if (bGone || (Harvest.IsValid() && Harvest.Swings > 20 && Me->CountItem(Item) == Harvest.CountAtStart))
	{
		if (!bGone)
		{
			Log(FString::Printf(TEXT("No %s from this %s after %d swings; trying another"), *Name(Item), *Harvest.Resource->GetDisplayNameOrId().ToString(), Harvest.Swings));
		}
		Blacklist.Add(FString::Printf(TEXT("%s_%d"), *Harvest.Component->GetPathName(), Harvest.Instance));
		Harvest = FHarvestTarget();
	}
	if (!Harvest.IsValid())
	{
		if (!FindHarvestTarget(Me, Item, Harvest))
		{
			// Maybe a tool is missing: craft one that works on a resource giving the item.
			for (const UAGBResourceDefinition* Resource : Harvesting->GetResources())
			{
				auto Gives = [Item](const TArray<FAGBResourceYield>& List) { return List.ContainsByPredicate([Item](const FAGBResourceYield& Y) { return Y.Item == Item; }); };
				if (!Resource || !(Gives(Resource->YieldPerHit) || Gives(Resource->YieldWhenDepleted)) || SelectBestTool(Me, Resource))
				{
					continue;
				}
				const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
				for (UAGBItemDefinition* Tool : Data ? Data->GetItems() : TArray<TObjectPtr<UAGBItemDefinition>>())
				{
					float Power = 0.f;
					if (Tool && Resource->GetHarvestPower(Tool, Power) && FindRecipeFor(Tool))
					{
						if (LastWarning != Tool->ItemId.ToString())
						{
							LastWarning = Tool->ItemId.ToString();
							Log(FString::Printf(TEXT("%s needs a tool: getting %s"), *Resource->GetDisplayNameOrId().ToString(), *Name(Tool)));
						}
						return Obtain(Me, Tool, 1, Depth + 1, OutReason);
					}
				}
			}
			if (++FailedSearches > 4)
			{
				OutReason = FString::Printf(TEXT("no harvestable source of %s found within 250 m (or no usable tool)"), *Name(Item));
				return EStep::Failed;
			}
			// Look around somewhere else.
			FVector Ground;
			const FVector Random = Me->GetActorLocation() + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal() * 5000.f;
			if (FindGround(Random, Ground))
			{
				MoveTo(Ground, 300.f);
			}
			return EStep::Running;
		}
		Harvest.CountAtStart = Me->CountItem(Item);
		Log(FString::Printf(TEXT("Going to a %s for %s (%.0f m)"), *Harvest.Resource->GetDisplayNameOrId().ToString(), *Name(Item),
			FVector::Dist(Me->GetActorLocation(), Harvest.Point) / 100.0));
	}
	if (!SelectBestTool(Me, Harvest.Resource))
	{
		Harvest = FHarvestTarget();
		return EStep::Running;
	}
	const float Reach = Me->GetMeleeReach();
	const float Distance = static_cast<float>(FVector::Dist(Me->GetPawnViewLocation(), Harvest.Point));
	if (Distance > Reach * 0.85f)
	{
		MoveTo(Harvest.Point, FMath::Max(60.f, Reach * 0.5f));
		return EStep::Running;
	}
	StopMoving();
	AimAt(Harvest.Point);
	Me->Swing();
	++Harvest.Swings;
	return EStep::Running;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::PlaceItem(AAGBCharacter* Me, UAGBItemDefinition* Item, int32 Depth, FString& OutReason)
{
	if (Item->PlacesStation && FindStation(Me, Item->PlacesStation, 2500.f))
	{
		return EStep::Done;
	}
	if (Me->CountItem(Item) < 1)
	{
		return Obtain(Me, Item, 1, Depth + 1, OutReason);
	}
	if (!SelectItem(Me, Item))
	{
		OutReason = FString::Printf(TEXT("could not put %s on the hotbar"), *Name(Item));
		return EStep::Failed;
	}
	StopMoving();
	if (++Attempts > 30)
	{
		OutReason = FString::Printf(TEXT("could not find a place for %s"), *Name(Item));
		return EStep::Failed;
	}
	// Look at the ground ahead, turning a little each try.
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	Player->SetControlRotation(FRotator(-40.0, Player->GetControlRotation().Yaw + (Attempts % 4 == 0 ? 45.0 : 0.0), 0.0));
	const int32 Before = Me->CountItem(Item);
	if (Attempts % 2 == 0)
	{
		Me->PlaceSelectedItem();
		Log(FString::Printf(TEXT("Placing %s (try %d)"), *Name(Item), Attempts / 2));
	}
	return Me->CountItem(Item) < Before && !Item->PlacesStation ? EStep::Done : EStep::Running;
}

// ---------------------------------------------------------------- bot: survival goals

UAGBPlaytestBot::EStep UAGBPlaytestBot::SurviveNight(FGoal& Goal, AAGBCharacter* Me, FString& OutReason)
{
	AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld());
	if (!Environment)
	{
		OutReason = TEXT("the level has no day/night cycle (WorldEnvironmentTools.SetupWorldEnvironment)");
		return EStep::Failed;
	}
	// A warm, burning fire to stay at.
	const UAGBGameDataSubsystem* Data = UAGBGameDataSubsystem::Get(this);
	UAGBItemDefinition* FireItem = nullptr;
	for (UAGBItemDefinition* Item : Data ? Data->GetItems() : TArray<TObjectPtr<UAGBItemDefinition>>())
	{
		if (Item && Item->PlacesStation && Item->PlacesStation->WarmthC > 0.f)
		{
			FireItem = Item;
			break;
		}
	}
	AAGBCraftingStation* Fire = FireItem ? FindStation(Me, FireItem->PlacesStation, 5000.f) : nullptr;
	if (FireItem && !Fire)
	{
		return PlaceItem(Me, FireItem, 0, OutReason);
	}
	if (Fire)
	{
		EStep FuelStep = EStep::Done;
		if (FuelStation(Me, Fire, 0, OutReason, FuelStep))
		{
			return FuelStep == EStep::Failed ? EStep::Failed : EStep::Running;
		}
		if (FVector::Dist2D(Me->GetActorLocation(), Fire->GetActorLocation()) > 350.f)
		{
			MoveTo(Fire->GetActorLocation(), 250.f);
			return EStep::Running;
		}
	}
	StopMoving();
	// Goal.Step: 0 = waiting for night, 1 = night seen.
	const bool bNight = Environment->IsNight();
	if (Goal.Step == 0)
	{
		if (bNight)
		{
			Goal.Step = 1;
			Log(FString::Printf(TEXT("Night fell (%02d:00); staying by the fire"), FMath::FloorToInt(Environment->GetHour())));
		}
		else if (Request.bSkipToNight && Me->HasAuthority() && Environment->GetHour() < 18.f)
		{
			Environment->SetHour(19.f);
			Log(TEXT("Skipped ahead to 19:00"));
		}
		return EStep::Running;
	}
	if (!bNight)
	{
		const UAGBVitalsComponent* Vitals = Me->Vitals;
		Goal.Status = FString::Printf(TEXT("PASS: survived the night (health %.0f%%)"), Vitals && Vitals->HasStat(UAGBVitalsComponent::HealthStat) ? 100.f * Vitals->GetFraction(UAGBVitalsComponent::HealthStat) : 100.f);
		return EStep::Done;
	}
	return EStep::Running;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::Explore(FGoal& Goal, AAGBCharacter* Me)
{
	// Goal.Step counts metres walked.
	static FVector Last = FVector::ZeroVector;
	if (Goal.Step > 0)
	{
		Goal.Step += FMath::RoundToInt(FVector::Dist2D(Me->GetActorLocation(), Last) / 100.0);
	}
	else
	{
		Goal.Step = 1;
	}
	Last = Me->GetActorLocation();
	if (Goal.Step >= Goal.Count)
	{
		return EStep::Done;
	}
	if (!bMoving || bStuck)
	{
		FVector Ground;
		const FVector Random = Me->GetActorLocation() + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal() * 6000.f;
		if (FindGround(Random, Ground))
		{
			MoveTo(Ground, 300.f);
		}
	}
	return EStep::Running;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::PerfSweep(FGoal& Goal, AAGBCharacter* Me)
{
	// Goal.Step = sample index * 4 + view direction.
	FBox2D Area = Request.SweepArea;
	if (!Area.bIsValid)
	{
		const FVector Here = Me->GetActorLocation();
		Area = FBox2D(FVector2D(Here) - FVector2D(20000.0), FVector2D(Here) + FVector2D(20000.0));
	}
	const int32 Side = FMath::Max(2, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(4, Request.SweepPoints)))));
	const int32 Point = Goal.Step / 4;
	const int32 View = Goal.Step % 4;
	if (Point >= Side * Side)
	{
		Goal.Status = FString::Printf(TEXT("PASS: %d points measured"), SweepResults.Num());
		return EStep::Done;
	}
	if (Now() - ActionTime < 0.8)
	{
		return EStep::Running;
	}
	const FVector2D Size = Area.GetSize();
	const FVector2D At = Area.Min + FVector2D((Point % Side + 0.5) / Side * Size.X, (Point / Side + 0.5) / Side * Size.Y);
	if (View == 0 && SweepFrames > 0 && Goal.Step > 0)
	{
		// Finished the previous point.
	}
	if (View == 0)
	{
		FVector Ground;
		if (FindGround(FVector(At, 0.0), Ground))
		{
			Me->SetActorLocation(Ground + FVector(0.0, 0.0, 120.0), false, nullptr, ETeleportType::TeleportPhysics);
		}
		SweepFrameSum = SweepFrameMax = 0.f;
		SweepFrames = 0;
	}
	else
	{
		// Skip the first view (streaming after the teleport); measure the other three.
		if (View == 3 && SweepFrames > 0)
		{
			const float Average = SweepFrameSum / SweepFrames;
			const FString Line = FString::Printf(TEXT("(%.0f, %.0f) m: avg %.1f ms (%.0f fps), worst %.1f ms"), At.X / 100.0, At.Y / 100.0, Average, 1000.f / FMath::Max(0.1f, Average), SweepFrameMax);
			SweepResults.Add(Line);
			SweepScores.Add({ Average, Line });
		}
	}
	if (View == 1)
	{
		SweepFrameSum = SweepFrameMax = 0.f;
		SweepFrames = 0;
	}
	Cast<APlayerController>(GetOwner())->SetControlRotation(FRotator(-5.0, View * 90.0, 0.0));
	ActionTime = Now();
	++Goal.Step;
	return EStep::Running;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::Photos(FGoal& Goal, AAGBCharacter* Me)
{
	// Per spot: 0 = place the camera and set the time, 1 = take the shot once the light settled, 2 = file it.
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	const int32 Shot = Goal.Step / 3;
	const int32 Phase = Goal.Step % 3;
	const double Wait = Phase == 1 ? 4.0 : (Phase == 2 ? 2.0 : 0.2);
	if (Now() - ActionTime < Wait)
	{
		return EStep::Running;
	}
	if (Shot >= Request.PhotoCameras.Num())
	{
		if (Player->GetHUD())
		{
			Player->GetHUD()->bShowHUD = true;
		}
		Player->SetViewTargetWithBlend(Me, 0.f);
		if (PhotoCamera.IsValid())
		{
			PhotoCamera->Destroy();
		}
		Goal.Status = FString::Printf(TEXT("PASS: %d screenshot(s) in %s"), PhotosSaved, *Request.PhotoFolder);
		return PhotosSaved > 0 ? EStep::Done : EStep::Failed;
	}
	const FString Label = Request.PhotoLabels.IsValidIndex(Shot) ? Request.PhotoLabels[Shot] : FString::Printf(TEXT("shot%d"), Shot + 1);
	if (Phase == 0)
	{
		ACameraActor* Camera = Cast<ACameraActor>(PhotoCamera.Get());
		if (!Camera)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Request.PhotoCameras[Shot], Params);
			Camera->GetCameraComponent()->SetFieldOfView(70.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PhotoCamera = Camera;
			if (AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld()))
			{
				Environment->SetWeather(TEXT("Clear"));
			}
		}
		Camera->SetActorTransform(Request.PhotoCameras[Shot]);
		// The player waits out of view behind the camera.
		FVector Ground;
		const FVector Behind = Request.PhotoCameras[Shot].GetLocation() - Request.PhotoCameras[Shot].GetRotation().GetForwardVector() * 600.0;
		if (FindGround(Behind, Ground))
		{
			Me->SetActorLocation(Ground + FVector(0.0, 0.0, 120.0), false, nullptr, ETeleportType::TeleportPhysics);
		}
		const float Hour = Request.PhotoHours.IsValidIndex(Shot) ? Request.PhotoHours[Shot] : -1.f;
		AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld());
		if (Environment && Hour >= 0.f)
		{
			Environment->SetHour(Hour);
		}
		if (Player->GetHUD())
		{
			Player->GetHUD()->bShowHUD = false;
		}
		Player->SetViewTargetWithBlend(Camera, 0.f);
		Log(FString::Printf(TEXT("Photo %d/%d: %s%s"), Shot + 1, Request.PhotoCameras.Num(), *Label, Hour >= 0.f ? *FString::Printf(TEXT(" at %.1f h"), Hour) : TEXT("")));
	}
	else if (Phase == 1)
	{
		PhotoRequestTime = FDateTime::Now();
		Player->ConsoleCommand(TEXT("HighResShot ") + Request.PhotoResolution);
	}
	else
	{
		// High-res shots land in the engine's screenshot folder: move the new one to the store kit.
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *FPaths::ScreenShotDir(), TEXT("*.png"), true, false);
		FString Newest;
		FDateTime NewestTime = PhotoRequestTime - FTimespan::FromSeconds(2.0);
		for (const FString& File : Files)
		{
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*File);
			if (Stamp > NewestTime)
			{
				NewestTime = Stamp;
				Newest = File;
			}
		}
		if (Newest.IsEmpty())
		{
			Log(TEXT("The screenshot did not appear (HighResShot failed?)"));
		}
		else
		{
			const FString Target = Request.PhotoFolder / FString::Printf(TEXT("%02d_%s.png"), Shot + 1, *Label);
			IFileManager::Get().Move(*Target, *Newest, true, true);
			++PhotosSaved;
		}
	}
	ActionTime = Now();
	++Goal.Step;
	return EStep::Running;
}

UAGBPlaytestBot::EStep UAGBPlaytestBot::Smoke(FGoal& Goal, AAGBCharacter* Me, FString& OutReason)
{
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	auto Check = [this](bool bOk, const FString& What, bool bWarnOnly = false)
	{
		Log(FString::Printf(TEXT("%s %s"), bOk ? TEXT("PASS") : (bWarnOnly ? TEXT("WARN") : TEXT("FAIL")), *What));
		return bOk || bWarnOnly;
	};
	static int32 Failures = 0;
	static int32 StartCount = 0;
	static float StartHour = 0.f;
	switch (Goal.Step)
	{
	case 0:
	{
		Failures = 0;
		StartCount = 0;
		Failures += !Check(Player && Player->GetHUD<AAGBHUD>() != nullptr, TEXT("HUD is the AI Game Builder HUD"));
		Failures += !Check(Me->Inventory && Me->Inventory->GetNumSlots() > 0 && Me->Hotbar && Me->Hotbar->GetNumSlots() > 0, TEXT("inventory and hotbar have slots"));
		Failures += !Check(Me->Vitals && Me->Vitals->HasStat(UAGBVitalsComponent::HealthStat), TEXT("vitals with health"));
		Failures += !Check(Me->Input.MappingContext != nullptr, TEXT("input is set up"));
		const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld());
		Check(Environment != nullptr, TEXT("day/night environment in the level"), true);
		StartHour = Environment ? Environment->GetHour() : 0.f;
		int32 Creatures = 0;
		for (TActorIterator<AAGBCreature> It(GetWorld()); It; ++It)
		{
			++Creatures;
		}
		Check(Creatures > 0, FString::Printf(TEXT("%d creature(s) in the world"), Creatures), true);
		Goal.Step = 1;
		ActionTime = Now();
		return EStep::Running;
	}
	case 1:
	{
		// Gather one of anything harvestable nearby.
		const UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
		UAGBItemDefinition* Item = nullptr;
		for (const UAGBResourceDefinition* Resource : Harvesting ? Harvesting->GetResources() : TArray<TObjectPtr<UAGBResourceDefinition>>())
		{
			if (Resource && Resource->YieldPerHit.Num() > 0 && (Resource->bAllowHands || Resource->ToolTags.IsEmpty() || SelectBestTool(Me, Resource)))
			{
				Item = Resource->YieldPerHit[0].Item.Get();
				break;
			}
		}
		if (!Item)
		{
			Check(false, TEXT("a resource that can be harvested with what the player has"), true);
			Goal.Step = 2;
			return EStep::Running;
		}
		if (StartCount == 0 && Harvest.CountAtStart == 0 && !Harvest.IsValid())
		{
			StartCount = Me->CountItem(Item) + 1;
		}
		FString Reason;
		const EStep Step = Gather(Me, Item, StartCount, 0, Reason);
		if (Step == EStep::Running && Now() - ActionTime < 120.0)
		{
			return EStep::Running;
		}
		Failures += !Check(Step == EStep::Done, FString::Printf(TEXT("harvested %s%s"), *Name(Item), Step == EStep::Done ? TEXT("") : *(TEXT(": ") + Reason)));
		StartCount = 0;
		Goal.Step = 2;
		ActionTime = Now();
		return EStep::Running;
	}
	case 2:
	{
		// Save and load keep the inventory.
		StopMoving();
		AAGBGameMode* GameMode = GetWorld()->GetAuthGameMode<AAGBGameMode>();
		UAGBSaveSubsystem* Saving = GetWorld()->GetSubsystem<UAGBSaveSubsystem>();
		if (!GameMode || !Saving)
		{
			Check(false, TEXT("save system available"));
			++Failures;
			Goal.Step = 4;
			return EStep::Running;
		}
		int32 Total = 0;
		for (UAGBInventoryComponent* Inventory : { Me->Inventory.Get(), Me->Hotbar.Get() })
		{
			for (int32 Slot = 0; Inventory && Slot < Inventory->GetNumSlots(); ++Slot)
			{
				Total += Inventory->GetSlot(Slot).Count;
			}
		}
		StartCount = Total;
		Failures += !Check(GameMode->SaveGameNow(false), TEXT("game saved"));
		FString Message;
		Failures += !Check(Saving->LoadWorld(Message), TEXT("game loaded: ") + Message);
		Goal.Step = 3;
		ActionTime = Now();
		return EStep::Running;
	}
	case 3:
	{
		if (Now() - ActionTime < 1.5)
		{
			return EStep::Running;
		}
		AAGBCharacter* Restored = Cast<AAGBCharacter>(Player->GetPawn());
		int32 Total = 0;
		for (UAGBInventoryComponent* Inventory : { Restored ? Restored->Inventory.Get() : nullptr, Restored ? Restored->Hotbar.Get() : nullptr })
		{
			for (int32 Slot = 0; Inventory && Slot < Inventory->GetNumSlots(); ++Slot)
			{
				Total += Inventory->GetSlot(Slot).Count;
			}
		}
		Failures += !Check(Total == StartCount, FString::Printf(TEXT("inventory after load: %d items (before: %d)"), Total, StartCount));
		StartCount = 0;
		Goal.Step = 4;
		return EStep::Running;
	}
	case 4:
	{
		// Pause menu opens and closes.
		UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(Player);
		if (Menus)
		{
			Menus->OpenPauseMenu();
			const bool bOpen = Menus->IsMenuOpen();
			Menus->CloseAll();
			Failures += !Check(bOpen && !Menus->IsMenuOpen(), TEXT("pause menu opens and closes"));
		}
		const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld());
		if (Environment)
		{
			Check(!FMath::IsNearlyEqual(Environment->GetHour(), StartHour, 0.001f), FString::Printf(TEXT("the clock runs (%.2f -> %.2f h)"), StartHour, Environment->GetHour()), true);
		}
		Failures += !Check(ErrorCapture.Errors.IsEmpty(), FString::Printf(TEXT("no errors in the log (%d)"), ErrorCapture.Errors.Num()));
		Goal.Status = Failures == 0 ? FString(TEXT("PASS: all smoke checks")) : FString::Printf(TEXT("FAIL: %d smoke check(s) failed"), Failures);
		OutReason = Goal.Status;
		return Failures == 0 ? EStep::Done : EStep::Failed;
	}
	default:
		return EStep::Done;
	}
}

// ---------------------------------------------------------------- bot: needs and threats

bool UAGBPlaytestBot::HandleThreats(AAGBCharacter* Me)
{
	AAGBCreature* Threat = nullptr;
	float Closest = 900.f;
	for (TActorIterator<AAGBCreature> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->Definition && It->Definition->Behaviour == EAGBCreatureBehaviour::Aggressive)
		{
			const float Distance = static_cast<float>(FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()));
			if (Distance < Closest)
			{
				Closest = Distance;
				Threat = *It;
			}
		}
	}
	if (!Threat)
	{
		return false;
	}
	if (LastWarning != Threat->GetName())
	{
		LastWarning = Threat->GetName();
		Log(FString::Printf(TEXT("Fighting %s"), *Threat->Definition->GetDisplayNameOrId().ToString()));
	}
	SelectWeapon(Me);
	const FVector Target = Threat->GetActorLocation();
	if (FVector::Dist(Me->GetPawnViewLocation(), Target) > Me->GetMeleeReach() * 0.9f)
	{
		MoveTo(Target, 80.f);
	}
	else
	{
		StopMoving();
	}
	AimAt(Target);
	Me->Swing();
	return true;
}

bool UAGBPlaytestBot::HandleNeeds(AAGBCharacter* Me)
{
	const UAGBVitalsComponent* Vitals = Me->Vitals;
	if (!Vitals || Now() - LastNeedTime < 2.0)
	{
		return bAimedAtWater;
	}
	const bool bHungry = Vitals->HasStat(HungerStat) && Vitals->GetFraction(HungerStat) < 0.35f;
	const bool bThirsty = Vitals->HasStat(UAGBVitalsComponent::ThirstStat) && Vitals->GetFraction(UAGBVitalsComponent::ThirstStat) < 0.35f;
	if (!bHungry && !bThirsty)
	{
		bAimedAtWater = false;
		return false;
	}
	UAGBInventoryComponent* Inventory = nullptr;
	int32 Slot = INDEX_NONE;
	const FName Stat = bThirsty ? UAGBVitalsComponent::ThirstStat : HungerStat;
	if (FindStack(Me, [Stat](const UAGBItemDefinition* Item) { return Item->GetStat(Stat, 0.f) > 0.f && Item->GetStat(UAGBVitalsComponent::HealthStat, 0.f) >= 0.f; }, Inventory, Slot))
	{
		Me->RequestUseItem(Inventory, Slot);
		LastNeedTime = Now();
		Log(FString::Printf(TEXT("%s: using %s"), bThirsty ? TEXT("Thirsty") : TEXT("Hungry"), *Name(Inventory->GetSlot(Slot).Item)));
		return true;
	}
	if (bThirsty)
	{
		// Drink from the nearest water: walk to it, look at it, interact.
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBBotWater), false, Me);
		GetWorld()->OverlapMultiByChannel(Overlaps, Me->GetActorLocation(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(15000.f), Params);
		FVector Best = FVector::ZeroVector;
		double BestDistance = TNumericLimits<double>::Max();
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UPrimitiveComponent* Component = Overlap.GetComponent();
			FVector Point;
			if (Component && Component->GetCollisionProfileName() == WaterProfile && Component->GetClosestPointOnCollision(Me->GetActorLocation(), Point) >= 0.f)
			{
				const double Distance = FVector::Dist2D(Point, Me->GetActorLocation());
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Point;
				}
			}
		}
		if (BestDistance < TNumericLimits<double>::Max())
		{
			if (BestDistance > 250.0)
			{
				MoveTo(Best, 200.f);
				return true;
			}
			StopMoving();
			AimAt(Best);
			if (Me->Interaction && Me->Interaction->IsFocusingWater())
			{
				Me->Interaction->Interact();
				LastNeedTime = Now();
				bAimedAtWater = false;
				Log(TEXT("Thirsty: drank from water"));
			}
			else
			{
				bAimedAtWater = true;
			}
			return true;
		}
	}
	if (LastWarning != TEXT("needs"))
	{
		LastWarning = TEXT("needs");
		Log(FString::Printf(TEXT("%s with nothing to eat or drink nearby"), bThirsty ? TEXT("Thirsty") : TEXT("Hungry")));
	}
	return false;
}

// ---------------------------------------------------------------- bot: actions

void UAGBPlaytestBot::MoveTo(const FVector& Point, float AcceptRadius)
{
	if (!bMoving || FVector::DistSquared(Point, MoveGoal) > 100.0 * 100.0)
	{
		StuckCheckTime = Now();
		StuckJumps = 0;
		bStuck = false;
		if (const APawn* Pawn = Cast<APlayerController>(GetOwner())->GetPawn())
		{
			StuckCheckLocation = Pawn->GetActorLocation();
		}
	}
	bMoving = true;
	MoveGoal = Point;
	MoveRadius = AcceptRadius;
}

void UAGBPlaytestBot::StopMoving()
{
	bMoving = false;
	if (APlayerController* Player = Cast<APlayerController>(GetOwner()))
	{
		if (AAGBCharacter* Me = Cast<AAGBCharacter>(Player->GetPawn()))
		{
			Me->SetSprinting(false);
		}
	}
}

void UAGBPlaytestBot::DriveMovement(AAGBCharacter* Me, float DeltaTime)
{
	const FVector Location = Me->GetActorLocation();
	const FVector ToGoal = MoveGoal - Location;
	if (ToGoal.Size2D() <= MoveRadius)
	{
		StopMoving();
		return;
	}
	const FVector Direction = ToGoal.GetSafeNormal2D();
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	Player->SetControlRotation(FRotator(0.0, Direction.Rotation().Yaw, 0.0));
	Me->AddMovementInput(Direction, 1.f);
	Me->SetSprinting(ToGoal.Size2D() > 2000.0 && (!Me->Vitals || !Me->Vitals->HasStat(TEXT("Stamina")) || Me->Vitals->GetFraction(TEXT("Stamina")) > 0.4f));

	// Stuck: jump, then give up on this goal point.
	if (Now() - StuckCheckTime > 1.5)
	{
		if (FVector::Dist2D(Location, StuckCheckLocation) < 60.0)
		{
			Me->Jump();
			if (++StuckJumps > 4)
			{
				bStuck = true;
				Log(TEXT("Stuck; picking another target"));
				if (Harvest.IsValid())
				{
					Blacklist.Add(FString::Printf(TEXT("%s_%d"), *Harvest.Component->GetPathName(), Harvest.Instance));
					Harvest = FHarvestTarget();
				}
				StopMoving();
			}
		}
		else
		{
			StuckJumps = 0;
		}
		StuckCheckTime = Now();
		StuckCheckLocation = Location;
	}
}

void UAGBPlaytestBot::AimAt(const FVector& Point)
{
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (Pawn)
	{
		Player->SetControlRotation((Point - Pawn->GetPawnViewLocation()).Rotation());
	}
}

bool UAGBPlaytestBot::FindGround(const FVector& Near, FVector& OutGround) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBBotGround), false);
	if (const APlayerController* Player = Cast<APlayerController>(GetOwner()))
	{
		Params.AddIgnoredActor(Player->GetPawn());
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, FVector(Near.X, Near.Y, 500000.0), FVector(Near.X, Near.Y, -500000.0), ECC_Pawn, Params))
	{
		OutGround = Hit.ImpactPoint;
		return true;
	}
	return false;
}

bool UAGBPlaytestBot::FindHarvestTarget(AAGBCharacter* Me, const UAGBItemDefinition* Item, FHarvestTarget& Out)
{
	const UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
	auto Gives = [Item](const UAGBResourceDefinition* Resource)
	{
		auto In = [Item](const TArray<FAGBResourceYield>& List) { return List.ContainsByPredicate([Item](const FAGBResourceYield& Y) { return Y.Item == Item; }); };
		return Resource && (In(Resource->YieldPerHit) || In(Resource->YieldWhenDepleted));
	};
	const FVector Here = Me->GetActorLocation();
	for (const float Radius : { 3000.f, 8000.f, 25000.f })
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBBotHarvest), false, Me);
		GetWorld()->OverlapMultiByObjectType(Overlaps, Here, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
		double BestDistance = TNumericLimits<double>::Max();
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UPrimitiveComponent* Component = Overlap.GetComponent();
			if (!Component)
			{
				continue;
			}
			UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
			const int32 Instance = Instanced ? Overlap.ItemIndex : INDEX_NONE;
			if (Instanced && (Instance == INDEX_NONE || Harvesting->IsHarvested(Instanced, Instance)))
			{
				continue;
			}
			const UAGBResourceDefinition* Resource = Harvesting->FindResource(Component, Instance);
			if (!Gives(Resource) || Blacklist.Contains(FString::Printf(TEXT("%s_%d"), *Component->GetPathName(), Instance)) || !SelectBestTool(Me, Resource))
			{
				continue;
			}
			FVector Point = Component->Bounds.Origin;
			if (Instanced)
			{
				FTransform Transform;
				Instanced->GetInstanceTransform(Instance, Transform, /*bWorldSpace=*/true);
				Point = Transform.GetLocation() + FVector(0.0, 0.0, 90.0);
			}
			const double Distance = FVector::DistSquared(Point, Here);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Out.Component = Component;
				Out.Instance = Instance;
				Out.Point = Point;
				Out.Resource = Resource;
				Out.Swings = 0;
			}
		}
		if (Out.IsValid())
		{
			return true;
		}
	}
	return false;
}

bool UAGBPlaytestBot::FindStack(AAGBCharacter* Me, TFunctionRef<bool(const UAGBItemDefinition*)> Match, UAGBInventoryComponent*& OutInventory, int32& OutSlot) const
{
	for (UAGBInventoryComponent* Inventory : { Me->Hotbar.Get(), Me->Inventory.Get() })
	{
		for (int32 Slot = 0; Inventory && Slot < Inventory->GetNumSlots(); ++Slot)
		{
			const FAGBItemStack Stack = Inventory->GetSlot(Slot);
			if (!Stack.IsEmpty() && Match(Stack.Item))
			{
				OutInventory = Inventory;
				OutSlot = Slot;
				return true;
			}
		}
	}
	return false;
}

bool UAGBPlaytestBot::SelectItem(AAGBCharacter* Me, const UAGBItemDefinition* Item)
{
	UAGBInventoryComponent* Inventory = nullptr;
	int32 Slot = INDEX_NONE;
	if (!Item)
	{
		// Bare hands: an empty hotbar slot.
		for (int32 Index = 0; Index < Me->Hotbar->GetNumSlots(); ++Index)
		{
			if (Me->Hotbar->GetSlot(Index).IsEmpty())
			{
				if (Me->GetSelectedHotbarSlot() != Index)
				{
					Me->SelectHotbarSlot(Index);
				}
				return true;
			}
		}
		return true; // No free slot: whatever is held works like hands for most resources.
	}
	if (!FindStack(Me, [Item](const UAGBItemDefinition* Candidate) { return Candidate == Item; }, Inventory, Slot))
	{
		return false;
	}
	if (Inventory == Me->Inventory)
	{
		// Bring it to the last hotbar slot.
		const int32 Target = Me->Hotbar->GetNumSlots() - 1;
		Me->Inventory->RequestMoveItem(Me->Inventory, Slot, Me->Hotbar, Target);
		Slot = Target;
	}
	if (Me->GetSelectedHotbarSlot() != Slot)
	{
		Me->SelectHotbarSlot(Slot);
	}
	return true;
}

bool UAGBPlaytestBot::SelectBestTool(AAGBCharacter* Me, const UAGBResourceDefinition* Resource)
{
	if (!Resource)
	{
		return false;
	}
	const UAGBItemDefinition* Best = nullptr;
	float BestPower = 0.f;
	for (UAGBInventoryComponent* Inventory : { Me->Hotbar.Get(), Me->Inventory.Get() })
	{
		for (int32 Slot = 0; Inventory && Slot < Inventory->GetNumSlots(); ++Slot)
		{
			const FAGBItemStack Stack = Inventory->GetSlot(Slot);
			float Power = 0.f;
			if (!Stack.IsEmpty() && Resource->GetHarvestPower(Stack.Item, Power) && Power > BestPower)
			{
				Best = Stack.Item;
				BestPower = Power;
			}
		}
	}
	float HandPower = 0.f;
	if (!Best && !Resource->GetHarvestPower(nullptr, HandPower))
	{
		return false;
	}
	return SelectItem(Me, Best);
}

bool UAGBPlaytestBot::SelectWeapon(AAGBCharacter* Me)
{
	const UAGBItemDefinition* Best = nullptr;
	float BestDamage = 5.f;
	for (UAGBInventoryComponent* Inventory : { Me->Hotbar.Get(), Me->Inventory.Get() })
	{
		for (int32 Slot = 0; Inventory && Slot < Inventory->GetNumSlots(); ++Slot)
		{
			const FAGBItemStack Stack = Inventory->GetSlot(Slot);
			const float Damage = Stack.IsEmpty() || Stack.Item->Ammo ? 0.f : Stack.Item->GetStat(TEXT("Damage"), 0.f);
			if (Damage > BestDamage)
			{
				Best = Stack.Item;
				BestDamage = Damage;
			}
		}
	}
	return Best ? SelectItem(Me, Best) : false;
}

AAGBCraftingStation* UAGBPlaytestBot::FindStation(AAGBCharacter* Me, const UObject* Definition, float MaxDistance) const
{
	AAGBCraftingStation* Best = nullptr;
	double BestDistance = MaxDistance;
	for (TActorIterator<AAGBCraftingStation> It(GetWorld()); It; ++It)
	{
		const double Distance = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
		if (It->Definition == Definition && Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}
