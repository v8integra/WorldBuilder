#include "PlaytestTools.h"

#include "GameToolUtils.h"

#include "AGBAudioFX.h"
#include "AGBCharacter.h"
#include "AGBCraftingTypes.h"
#include "AGBCreatureSpawnZone.h"
#include "AGBCreatureTypes.h"
#include "AGBGameFramework.h"
#include "AGBHarvestTypes.h"
#include "AGBItemTypes.h"
#include "AGBPlaytest.h"
#include "AGBResourceNode.h"
#include "AGBSurvivalConfig.h"
#include "AGBUITheme.h"
#include "AGBWorldEnvironment.h"
#include "AIWorldBuilderCore.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameMapsSettings.h"
#include "HAL/FileManager.h"
#include "LandscapeProxy.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/LevelEditorPlaySettings.h"

using namespace AIWorldBuilder;

namespace
{
	template <typename T>
	TArray<T*> LoadAll()
	{
		TArray<T*> Result;
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(T::StaticClass()))
		{
			if (T* Loaded = Cast<T>(Asset.GetAsset()))
			{
				Result.Add(Loaded);
			}
		}
		return Result;
	}

	FString Id(const UAGBItemDefinition* Item)
	{
		return Item ? Item->ItemId.ToString() : FString(TEXT("(none)"));
	}

	FString Minutes(double Seconds)
	{
		return Seconds < 0.0 ? FString(TEXT("never")) : FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Seconds / 60.0), FMath::FloorToInt(Seconds) % 60);
	}

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	UAGBPlaytestBot* FindBot()
	{
		UWorld* World = PlayWorld();
		APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
		return Player ? Player->FindComponentByClass<UAGBPlaytestBot>() : nullptr;
	}

	FPlaytestToolsResult StartPlay(const FAGBPlaytestRequest& Request)
	{
		FPlaytestToolsResult R;
		UWorld* World = GetEditorWorld();
		if (!GEditor || !World)
		{
			R.Message = TEXT("No level is open in the editor.");
			return R;
		}
		if (PlayWorld() || GEditor->IsPlaySessionInProgress() || GEditor->IsPlaySessionRequestQueued())
		{
			R.Message = TEXT("Play is already running: StopPlaytest (or stop Play) first.");
			return R;
		}
		const UClass* GameMode = GameToolUtils::GetActiveGameModeClass(World);
		if (!GameMode || !GameMode->IsChildOf(AAGBGameMode::StaticClass()))
		{
			R.Message = TEXT("The open level does not use an AI Game Builder game mode: open the game level (GameFoundationTools.SetupGameFoundation).");
			return R;
		}
		AGBPlaytest::Pending() = Request;
		ULevelEditorPlaySettings* Settings = NewObject<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
		Settings->SetPlayNumberOfClients(1);
		Settings->bLaunchSeparateServer = false;
		FRequestPlaySessionParams Params;
		Params.EditorPlaySettings = Settings;
		GEditor->RequestPlaySession(Params);
		R.bSuccess = true;
		R.Message = FString::Printf(TEXT("Playtest '%s' starting in Play-in-Editor (time x%.1f, up to %.0f min). Poll GetPlaytestStatus every minute or so; Play ends by itself when the bot is done. Ask the user not to touch the controls."),
			*Request.Scenario, Request.TimeScale, Request.MaxMinutes);
		return R;
	}

	struct FSession
	{
		bool bBot = false;
		double Seconds = 0.0;
		TMap<FString, TMap<FString, double>> Counters;
		TMap<FString, double> Milestones;
		TMap<FString, double> Empty;
		TSharedPtr<FJsonObject> Performance;
	};

	bool ReadSession(const FString& Path, FSession& Out)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
		{
			return false;
		}
		Out.bBot = Root->GetBoolField(TEXT("bot"));
		Out.Seconds = Root->GetNumberField(TEXT("seconds"));
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Root->TryGetObjectField(TEXT("counters"), Object))
		{
			for (const auto& Category : (*Object)->Values)
			{
				TMap<FString, double>& Entries = Out.Counters.FindOrAdd(FString(Category.Key));
				for (const auto& Entry : Category.Value->AsObject()->Values)
				{
					Entries.Add(FString(Entry.Key), Entry.Value->AsNumber());
				}
			}
		}
		if (Root->TryGetObjectField(TEXT("milestones"), Object))
		{
			for (const auto& Entry : (*Object)->Values)
			{
				Out.Milestones.Add(FString(Entry.Key), Entry.Value->AsNumber());
			}
		}
		if (Root->TryGetObjectField(TEXT("empty_seconds"), Object))
		{
			for (const auto& Entry : (*Object)->Values)
			{
				Out.Empty.Add(FString(Entry.Key), Entry.Value->AsNumber());
			}
		}
		if (Root->TryGetObjectField(TEXT("performance"), Object))
		{
			Out.Performance = *Object;
		}
		return true;
	}
}

FString UPlaytestTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FPlaytestToolsResult UPlaytestTools::ValidateGame()
{
	FPlaytestToolsResult R;
	UWorld* World = GetEditorWorld();
	int32 Fails = 0;
	int32 Warns = 0;
	auto Fail = [&](const FString& Text) { R.Lines.Add(TEXT("FAIL ") + Text); ++Fails; };
	auto Warn = [&](const FString& Text) { R.Lines.Add(TEXT("WARN ") + Text); ++Warns; };
	auto Info = [&](const FString& Text) { R.Lines.Add(TEXT("INFO ") + Text); };

	const TArray<UAGBItemDefinition*> Items = LoadAll<UAGBItemDefinition>();
	const TArray<UAGBRecipeDefinition*> Recipes = LoadAll<UAGBRecipeDefinition>();
	const TArray<UAGBResourceDefinition*> Resources = LoadAll<UAGBResourceDefinition>();
	const TArray<UAGBCreatureDefinition*> Creatures = LoadAll<UAGBCreatureDefinition>();
	const TArray<UAGBStationDefinition*> Stations = LoadAll<UAGBStationDefinition>();
	Info(FString::Printf(TEXT("%d items, %d recipes, %d resources, %d creatures, %d stations"), Items.Num(), Recipes.Num(), Resources.Num(), Creatures.Num(), Stations.Num()));

	// What exists in the level.
	TSet<FSoftObjectPath> PlacedMeshes;
	TSet<const UAGBResourceDefinition*> PlacedNodes;
	TSet<const UAGBCreatureDefinition*> SpawnedCreatures;
	bool bPlayerStart = false;
	bool bEnvironment = false;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (const AAGBResourceNode* Node = Cast<AAGBResourceNode>(*It))
			{
				PlacedNodes.Add(Node->Resource);
			}
			if (const AAGBCreatureSpawnZone* Zone = Cast<AAGBCreatureSpawnZone>(*It))
			{
				SpawnedCreatures.Add(Zone->Creature);
			}
			bPlayerStart |= It->IsA<APlayerStart>();
			bEnvironment |= It->IsA<AAGBWorldEnvironment>();
			TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
			for (const UStaticMeshComponent* Mesh : Meshes)
			{
				if (Mesh->GetStaticMesh() && (!Mesh->IsA<UInstancedStaticMeshComponent>() || Cast<UInstancedStaticMeshComponent>(Mesh)->GetInstanceCount() > 0))
				{
					PlacedMeshes.Add(FSoftObjectPath(Mesh->GetStaticMesh()));
				}
			}
		}
	}

	// Which items can be obtained: starting items, resource yields and creature loot found in the level, then recipes.
	TSet<const UAGBItemDefinition*> Obtainable;
	const UBlueprint* CharacterBlueprint = World ? GameToolUtils::GetCharacterBlueprint(World) : nullptr;
	const AAGBCharacter* CharacterDefaults = CharacterBlueprint && CharacterBlueprint->GeneratedClass ? Cast<AAGBCharacter>(CharacterBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (CharacterDefaults)
	{
		for (const FAGBItemStack& Stack : CharacterDefaults->StartingItems)
		{
			Obtainable.Add(Stack.Item);
		}
	}
	TSet<FName> AvailableTags;
	auto AddYields = [&Obtainable](const TArray<FAGBResourceYield>& Yields) { for (const FAGBResourceYield& Yield : Yields) { Obtainable.Add(Yield.Item); } };
	for (const UAGBResourceDefinition* Resource : Resources)
	{
		const bool bPlaced = PlacedNodes.Contains(Resource) || Resource->Meshes.ContainsByPredicate([&PlacedMeshes](const TSoftObjectPtr<UStaticMesh>& Mesh) { return PlacedMeshes.Contains(Mesh.ToSoftObjectPath()); });
		if (Resource->Meshes.IsEmpty() && !PlacedNodes.Contains(Resource))
		{
			Warn(FString::Printf(TEXT("resource %s has no meshes and no resource node in the level"), *Resource->ResourceId.ToString()));
		}
		else if (!bPlaced)
		{
			Warn(FString::Printf(TEXT("resource %s: none of its meshes are in this level (foliage/PCG/nodes)"), *Resource->ResourceId.ToString()));
		}
		if (Resource->YieldPerHit.IsEmpty() && Resource->YieldWhenDepleted.IsEmpty())
		{
			Fail(FString::Printf(TEXT("resource %s gives nothing"), *Resource->ResourceId.ToString()));
		}
		if (bPlaced)
		{
			AddYields(Resource->YieldPerHit);
			AddYields(Resource->YieldWhenDepleted);
		}
	}
	for (const UAGBCreatureDefinition* Creature : Creatures)
	{
		if (!SpawnedCreatures.Contains(Creature))
		{
			Warn(FString::Printf(TEXT("creature %s has no spawn zone in this level"), *Creature->CreatureId.ToString()));
			continue;
		}
		AddYields(Creature->Loot);
	}
	// Recipes, repeated until nothing new becomes craftable.
	TSet<const UAGBRecipeDefinition*> Craftable;
	for (bool bChanged = true; bChanged;)
	{
		bChanged = false;
		for (const UAGBRecipeDefinition* Recipe : Recipes)
		{
			if (Craftable.Contains(Recipe))
			{
				continue;
			}
			const bool bIngredients = !Recipe->Ingredients.ContainsByPredicate([&Obtainable](const FAGBItemAmount& Amount) { return !Obtainable.Contains(Amount.Item); });
			const bool bStation = !Recipe->Station || (Items.ContainsByPredicate([&Recipe](const UAGBItemDefinition* Item) { return Item->PlacesStation == Recipe->Station; })
				&& Items.ContainsByPredicate([&](const UAGBItemDefinition* Item) { return Item->PlacesStation == Recipe->Station && Obtainable.Contains(Item); }));
			if (bIngredients && bStation)
			{
				Craftable.Add(Recipe);
				for (const FAGBItemAmount& Output : Recipe->Outputs)
				{
					bChanged |= !Obtainable.Contains(Output.Item);
					Obtainable.Add(Output.Item);
				}
			}
		}
	}
	for (const UAGBItemDefinition* Item : Obtainable)
	{
		if (Item)
		{
			for (const FName& Tag : Item->Tags)
			{
				AvailableTags.Add(Tag);
			}
		}
	}

	for (const UAGBItemDefinition* Item : Items)
	{
		if (!Obtainable.Contains(Item))
		{
			Fail(FString::Printf(TEXT("item %s cannot be obtained (no placed resource, spawned creature, starting item or reachable recipe gives it)"), *Id(Item)));
		}
		if (Item->Icon.IsNull())
		{
			++Warns;
		}
		if ((Item->Category == EAGBItemCategory::Food || Item->Category == EAGBItemCategory::Consumable) && Item->Stats.IsEmpty())
		{
			Warn(FString::Printf(TEXT("food %s has no effect stats (Hunger, Thirst, Health...)"), *Id(Item)));
		}
	}
	const int32 NoIcons = Items.FilterByPredicate([](const UAGBItemDefinition* Item) { return Item->Icon.IsNull(); }).Num();
	if (NoIcons > 0)
	{
		R.Lines.Add(FString::Printf(TEXT("WARN %d item(s) without icons (the HUD shows their names)"), NoIcons));
	}
	for (const UAGBRecipeDefinition* Recipe : Recipes)
	{
		if (Recipe->Outputs.IsEmpty() || Recipe->Outputs.ContainsByPredicate([](const FAGBItemAmount& Amount) { return !Amount.Item; }))
		{
			Fail(FString::Printf(TEXT("recipe %s has a missing output"), *Recipe->RecipeId.ToString()));
		}
		if (Recipe->Ingredients.ContainsByPredicate([](const FAGBItemAmount& Amount) { return !Amount.Item; }))
		{
			Fail(FString::Printf(TEXT("recipe %s has a missing ingredient"), *Recipe->RecipeId.ToString()));
		}
		if (Recipe->Station && !Items.ContainsByPredicate([&Recipe](const UAGBItemDefinition* Item) { return Item->PlacesStation == Recipe->Station; }))
		{
			Fail(FString::Printf(TEXT("recipe %s needs station %s but no item places it"), *Recipe->RecipeId.ToString(), *Recipe->Station->StationId.ToString()));
		}
		if (!Craftable.Contains(Recipe))
		{
			Fail(FString::Printf(TEXT("recipe %s can never be crafted (an ingredient or its station cannot be obtained)"), *Recipe->RecipeId.ToString()));
		}
	}
	const bool bFuel = Items.ContainsByPredicate([&Obtainable](const UAGBItemDefinition* Item) { return Item->HasTag(TEXT("Fuel")) && Obtainable.Contains(Item); });
	for (const UAGBStationDefinition* Station : Stations)
	{
		if (Station->bNeedsFuel && !bFuel)
		{
			Fail(FString::Printf(TEXT("station %s needs fuel but no obtainable item has the Fuel tag"), *Station->StationId.ToString()));
		}
	}
	for (const UAGBResourceDefinition* Resource : Resources)
	{
		if (!Resource->ToolTags.IsEmpty() && !Resource->bAllowHands && !Resource->ToolTags.ContainsByPredicate([&AvailableTags](FName Tag) { return AvailableTags.Contains(Tag); }))
		{
			Fail(FString::Printf(TEXT("resource %s needs a tool (%s) that no obtainable item has"), *Resource->ResourceId.ToString(), *FString::JoinBy(Resource->ToolTags, TEXT(", "), [](FName Tag) { return Tag.ToString(); })));
		}
	}

	// Survival pacing.
	const TArray<UAGBSurvivalConfig*> Configs = LoadAll<UAGBSurvivalConfig>();
	const UAGBSurvivalConfig* Survival = CharacterDefaults && CharacterDefaults->SurvivalConfig ? CharacterDefaults->SurvivalConfig.Get() : (Configs.Num() > 0 ? Configs[0] : nullptr);
	const AAGBWorldEnvironment* Environment = nullptr;
	if (World)
	{
		for (TActorIterator<AAGBWorldEnvironment> It(World); It; ++It)
		{
			Environment = *It;
		}
	}
	if (Survival)
	{
		for (const FAGBStatConfig& Stat : Survival->Stats)
		{
			if (Stat.ChangePerSecond < 0.f)
			{
				const float EmptyMinutes = Stat.MaxValue / -Stat.ChangePerSecond / 60.f;
				Info(FString::Printf(TEXT("%s empties in %.0f min from full%s"), *Stat.Id.ToString(), EmptyMinutes,
					Environment ? *FString::Printf(TEXT(" (a day lasts %.0f min)"), Environment->DayLengthMinutes) : TEXT("")));
			}
		}
	}

	// Setup.
	if (World)
	{
		const UClass* GameMode = GameToolUtils::GetActiveGameModeClass(World);
		if (!GameMode || !GameMode->IsChildOf(AAGBGameMode::StaticClass()))
		{
			Fail(TEXT("the level's game mode is not an AI Game Builder game mode (GameFoundationTools.SetupGameFoundation)"));
		}
		if (!bPlayerStart)
		{
			Warn(TEXT("no Player Start in the level"));
		}
		if (!bEnvironment)
		{
			Warn(TEXT("no day/night environment (WorldEnvironmentTools.SetupWorldEnvironment)"));
		}
	}
	const UAGBUISettings* UI = GetDefault<UAGBUISettings>();
	if (UI->MainMenuMap.IsNull())
	{
		Warn(TEXT("no main menu (UITools.SetupUI)"));
	}
	if (UI->AudioFX.IsNull())
	{
		Warn(TEXT("audio not set up: placeholder sounds everywhere (AudioFXTools.SetupAudioFX)"));
	}
	R.bSuccess = Fails == 0;
	R.Message = FString::Printf(TEXT("%d problem(s), %d warning(s)."), Fails, Warns);
	return R;
}

FPlaytestToolsResult UPlaytestTools::StartPlaytest(const FString& Scenario, const FString& Goals, double TimeScale, double MaxMinutes, bool bSkipToNight)
{
	static const TArray<FString> Scenarios = { TEXT("Survive"), TEXT("Gather"), TEXT("Smoke"), TEXT("Explore"), TEXT("Custom") };
	const FString* Known = Scenarios.FindByPredicate([&Scenario](const FString& Name) { return Name.Equals(Scenario, ESearchCase::IgnoreCase); });
	if (!Known)
	{
		FPlaytestToolsResult R;
		R.Message = TEXT("Scenario must be Survive, Gather, Smoke, Explore or Custom.");
		return R;
	}
	FAGBPlaytestRequest Request;
	Request.Scenario = *Known;
	Request.Goals = Goals;
	Request.TimeScale = static_cast<float>(FMath::Clamp(TimeScale, 0.5, 8.0));
	Request.MaxMinutes = static_cast<float>(FMath::Clamp(MaxMinutes, 1.0, 120.0));
	Request.bSkipToNight = bSkipToNight;
	return StartPlay(Request);
}

FPlaytestToolsResult UPlaytestTools::RunPerformanceSweep(int32 Points)
{
	FAGBPlaytestRequest Request;
	Request.Scenario = TEXT("PerfSweep");
	Request.SweepPoints = FMath::Clamp(Points, 4, 100);
	Request.MaxMinutes = 2.f + Request.SweepPoints * 0.1f;
	if (UWorld* World = GetEditorWorld())
	{
		FBox Bounds(ForceInit);
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			Bounds += It->GetComponentsBoundingBox();
		}
		if (Bounds.IsValid)
		{
			// Stay a little inside the edges.
			const FVector Margin = Bounds.GetExtent() * 0.08;
			Request.SweepArea = FBox2D(FVector2D(Bounds.Min + Margin), FVector2D(Bounds.Max - Margin));
		}
	}
	FPlaytestToolsResult R = StartPlay(Request);
	if (R.bSuccess)
	{
		R.Message = FString::Printf(TEXT("Performance sweep over %d points starting (about %.0f s). Poll GetPlaytestStatus."), Request.SweepPoints, Request.SweepPoints * 3.2f);
	}
	return R;
}

FPlaytestToolsResult UPlaytestTools::GetPlaytestStatus()
{
	FPlaytestToolsResult R;
	if (const UAGBPlaytestBot* Bot = FindBot())
	{
		Bot->Describe(R.Lines);
		if (const UAGBTelemetrySubsystem* Telemetry = UAGBTelemetrySubsystem::Get(PlayWorld()))
		{
			R.Lines.Add(TEXT("Telemetry so far:"));
			Telemetry->Describe(R.Lines);
		}
		R.bSuccess = true;
		R.Message = Bot->IsFinished() ? TEXT("Finished; Play ends in a moment.") : TEXT("Running.");
		return R;
	}
	if (AGBPlaytest::Pending().IsSet())
	{
		R.bSuccess = true;
		R.Message = TEXT("Starting (Play is loading).");
		return R;
	}
	if (!AGBPlaytest::LastReport().IsEmpty())
	{
		AGBPlaytest::LastReport().ParseIntoArrayLines(R.Lines);
		R.bSuccess = true;
		R.Message = TEXT("No playtest running. Last report: ") + AGBPlaytest::LastReportPath();
		return R;
	}
	R.Message = PlayWorld() ? TEXT("Play is running without a bot.") : TEXT("No playtest has run yet.");
	return R;
}

FPlaytestToolsResult UPlaytestTools::StopPlaytest()
{
	FPlaytestToolsResult R;
	AGBPlaytest::Pending().Reset();
	if (PlayWorld())
	{
		if (UAGBPlaytestBot* Bot = FindBot())
		{
			Bot->Describe(R.Lines);
		}
		GEditor->RequestEndPlayMap();
		R.Message = TEXT("Play stopping; the full report is saved in Saved/AGBPlaytests (GetPlaytestStatus shows it).");
	}
	else
	{
		R.Message = TEXT("No playtest running.");
	}
	R.bSuccess = true;
	return R;
}

FPlaytestToolsResult UPlaytestTools::SetQATargets(const FString& TargetHardware, double TargetFPS, double FirstToolMinutes, double FirstStationMinutes,
	double FirstBuildMinutes, double MaxDeathsPerHour, double MaxStarvingShare)
{
	UAGBQASettings* QA = GetMutableDefault<UAGBQASettings>();
	if (!TargetHardware.IsEmpty())
	{
		QA->TargetHardware = TargetHardware;
		if (TargetFPS <= 0.0)
		{
			// Sensible frame rates by device.
			const FString Lower = TargetHardware.ToLower();
			QA->TargetFPS = Lower.Contains(TEXT("deck")) || Lower.Contains(TEXT("low")) ? 30.f : 60.f;
		}
	}
	if (TargetFPS > 0.0)
	{
		QA->TargetFPS = static_cast<float>(FMath::Clamp(TargetFPS, 20.0, 240.0));
	}
	auto Set = [](double Value, float& Target, double Max) { if (Value >= 0.0) { Target = static_cast<float>(FMath::Min(Value, Max)); } };
	Set(FirstToolMinutes, QA->FirstToolMinutes, 600.0);
	Set(FirstStationMinutes, QA->FirstStationMinutes, 600.0);
	Set(FirstBuildMinutes, QA->FirstBuildMinutes, 600.0);
	Set(MaxDeathsPerHour, QA->MaxDeathsPerHour, 100.0);
	Set(MaxStarvingShare, QA->MaxStarvingShare, 1.0);
	QA->TryUpdateDefaultConfigFile();

	FPlaytestToolsResult R;
	R.Lines.Add(FString::Printf(TEXT("Target: %s at %.0f fps (%.1f ms per frame)"), *QA->TargetHardware, QA->TargetFPS, QA->GetFrameBudgetMs()));
	R.Lines.Add(FString::Printf(TEXT("Balance: first tool within %.0f min, first station %.0f min, first building %.0f min; at most %.1f deaths/hour; starving at most %.0f%% of the time"),
		QA->FirstToolMinutes, QA->FirstStationMinutes, QA->FirstBuildMinutes, QA->MaxDeathsPerHour, QA->MaxStarvingShare * 100.f));
	R.bSuccess = true;
	R.Message = TEXT("QA targets saved.");
	return R;
}

FPlaytestToolsResult UPlaytestTools::GetBalanceReport(int32 LastSessions, bool bIncludeBot)
{
	FPlaytestToolsResult R;
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("AGBTelemetry");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
	Files.Sort([](const FString& A, const FString& B) { return A > B; }); // Newest first (time-stamped names).
	TArray<FSession> Sessions;
	for (const FString& File : Files)
	{
		FSession Session;
		if (Sessions.Num() < FMath::Max(1, LastSessions) && ReadSession(Folder / File, Session) && (bIncludeBot || !Session.bBot))
		{
			Sessions.Add(MoveTemp(Session));
		}
	}
	if (Sessions.IsEmpty())
	{
		R.Message = TEXT("No play sessions recorded yet: play the game (or StartPlaytest) for at least a minute.");
		return R;
	}
	const UAGBQASettings* QA = GetDefault<UAGBQASettings>();
	double TotalSeconds = 0.0;
	int32 Bots = 0;
	TMap<FString, TMap<FString, double>> Totals;
	TMap<FString, TArray<double>> MilestoneTimes;
	TMap<FString, double> EmptyTotals;
	double FrameSum = 0.0, GpuSum = 0.0, P95Max = 0.0, Hitches = 0.0;
	int32 PerfSessions = 0;
	for (const FSession& Session : Sessions)
	{
		TotalSeconds += Session.Seconds;
		Bots += Session.bBot ? 1 : 0;
		for (const auto& Category : Session.Counters)
		{
			for (const auto& Entry : Category.Value)
			{
				Totals.FindOrAdd(Category.Key).FindOrAdd(Entry.Key) += Entry.Value;
			}
		}
		for (const auto& Entry : Session.Milestones)
		{
			MilestoneTimes.FindOrAdd(Entry.Key).Add(Entry.Value);
		}
		for (const auto& Entry : Session.Empty)
		{
			EmptyTotals.FindOrAdd(Entry.Key) += Entry.Value;
		}
		if (Session.Performance)
		{
			FrameSum += Session.Performance->GetNumberField(TEXT("frame_avg_ms"));
			GpuSum += Session.Performance->GetNumberField(TEXT("gpu_ms"));
			P95Max = FMath::Max(P95Max, Session.Performance->GetNumberField(TEXT("frame_p95_ms")));
			Hitches += Session.Performance->GetNumberField(TEXT("hitches"));
			++PerfSessions;
		}
	}
	const double Hours = FMath::Max(TotalSeconds / 3600.0, 1.0 / 60.0);
	R.Lines.Add(FString::Printf(TEXT("%d session(s) (%d by the bot), %s of play"), Sessions.Num(), Bots, *Minutes(TotalSeconds)));

	// Milestones against targets (median over sessions that reached them).
	auto Median = [](TArray<double> Values) { Values.Sort(); return Values.Num() > 0 ? Values[Values.Num() / 2] : -1.0; };
	auto CheckMilestone = [&](const TCHAR* Key, const TCHAR* Label, float TargetMinutes, const TCHAR* Advice)
	{
		const TArray<double>* Times = MilestoneTimes.Find(Key);
		const double Value = Times ? Median(*Times) : -1.0;
		const int32 Reached = Times ? Times->Num() : 0;
		const bool bOk = Value >= 0.0 && Value <= TargetMinutes * 60.0;
		R.Lines.Add(FString::Printf(TEXT("%s %s: median %s (target %.0f min), reached in %d/%d sessions%s"), bOk ? TEXT("OK  ") : TEXT("FLAG"), Label, *Minutes(Value),
			TargetMinutes, Reached, Sessions.Num(), bOk ? TEXT("") : *(TEXT(" -> ") + FString(Advice))));
	};
	CheckMilestone(TEXT("first_tool"), TEXT("First tool"), QA->FirstToolMinutes, TEXT("cheaper first tool recipe, more starting resources nearby, or hand-harvestable sticks/stones"));
	CheckMilestone(TEXT("first_stations"), TEXT("First station"), QA->FirstStationMinutes, TEXT("cheaper campfire/workbench, or more of its ingredients per hit"));
	CheckMilestone(TEXT("first_built"), TEXT("First building piece"), QA->FirstBuildMinutes, TEXT("cheaper foundation/wall recipes or faster wood gathering"));
	if (const TArray<double>* Nights = MilestoneTimes.Find(TEXT("first_night_survived")))
	{
		R.Lines.Add(FString::Printf(TEXT("INFO First night survived in %d/%d sessions (median %s)"), Nights->Num(), Sessions.Num(), *Minutes(Median(*Nights))));
	}

	// Deaths and starving.
	double Deaths = 0.0;
	TArray<FString> Causes;
	if (const TMap<FString, double>* DeathMap = Totals.Find(TEXT("Deaths")))
	{
		for (const auto& Entry : *DeathMap)
		{
			Deaths += Entry.Value;
			Causes.Add(FString::Printf(TEXT("%s %.0f"), *Entry.Key, Entry.Value));
		}
	}
	const double DeathsPerHour = Deaths / Hours;
	R.Lines.Add(FString::Printf(TEXT("%s Deaths: %.1f per hour (target <= %.1f)%s%s"), DeathsPerHour <= QA->MaxDeathsPerHour ? TEXT("OK  ") : TEXT("FLAG"), DeathsPerHour, QA->MaxDeathsPerHour,
		Causes.IsEmpty() ? TEXT("") : *(TEXT("; causes: ") + FString::Join(Causes, TEXT(", "))),
		DeathsPerHour <= QA->MaxDeathsPerHour ? TEXT("") : TEXT(" -> less creature damage/aggro range, more armor, slower hunger/thirst, or warmer nights")));
	for (const auto& Entry : EmptyTotals)
	{
		const double Share = Entry.Value / FMath::Max(1.0, TotalSeconds);
		R.Lines.Add(FString::Printf(TEXT("%s %s empty %.0f%% of the time (target <= %.0f%%)%s"), Share <= QA->MaxStarvingShare ? TEXT("OK  ") : TEXT("FLAG"), *Entry.Key, Share * 100.0,
			QA->MaxStarvingShare * 100.f, Share <= QA->MaxStarvingShare ? TEXT("") : TEXT(" -> slower drain (SurvivalTools), more food/water sources, or bigger food values")));
	}

	// Rates.
	for (const TCHAR* Category : { TEXT("Gathered"), TEXT("Crafted"), TEXT("Built"), TEXT("Stations"), TEXT("Kills"), TEXT("Consumed"), TEXT("DamageTaken") })
	{
		if (const TMap<FString, double>* Entries = Totals.Find(Category))
		{
			TArray<TPair<FString, double>> Sorted;
			for (const auto& Entry : *Entries)
			{
				Sorted.Add({ Entry.Key, Entry.Value });
			}
			Sorted.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value > B.Value; });
			TArray<FString> Parts;
			for (int32 Index = 0; Index < Sorted.Num() && Index < 10; ++Index)
			{
				Parts.Add(FString::Printf(TEXT("%s %.0f (%.0f/h)"), *Sorted[Index].Key, Sorted[Index].Value, Sorted[Index].Value / Hours));
			}
			R.Lines.Add(FString::Printf(TEXT("INFO %s: %s"), Category, *FString::Join(Parts, TEXT(", "))));
		}
	}

	// Performance.
	if (PerfSessions > 0)
	{
		const double Frame = FrameSum / PerfSessions;
		const float Budget = QA->GetFrameBudgetMs();
		R.Lines.Add(FString::Printf(TEXT("%s Performance (%s, %.0f fps = %.1f ms): avg frame %.1f ms (%.0f fps), GPU %.1f ms, worst 95%% %.1f ms, %.0f hitch(es)%s"),
			Frame <= Budget ? TEXT("OK  ") : TEXT("FLAG"), *QA->TargetHardware, QA->TargetFPS, Budget, Frame, 1000.0 / FMath::Max(0.1, Frame), GpuSum / PerfSessions, P95Max, Hitches,
			Frame <= Budget ? TEXT("") : TEXT(" -> RunPerformanceSweep to find slow areas; less foliage density/shadows, lower view distance, Nanite meshes")));
		R.Lines.Add(TEXT("INFO Measured in Play-in-Editor sessions are pessimistic; check a Standalone/packaged build before cutting content."));
	}
	R.bSuccess = true;
	R.Message = TEXT("Balance report: FLAG lines need attention.");
	return R;
}
