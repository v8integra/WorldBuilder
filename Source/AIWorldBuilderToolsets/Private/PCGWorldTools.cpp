#include "PCGWorldTools.h"

#include "ActorFactories/ActorFactory.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIWorldBuilderPCG"

using namespace AIWorldBuilder;

namespace
{
	/** Vertical margin added above and below the terrain for spawned volumes (cm). */
	constexpr double VolumeHeightMarginCm = 10000.0;

	FWorldBuilderPCGVolumeInfo DescribeVolume(APCGVolume* Volume)
	{
		FWorldBuilderPCGVolumeInfo Info;
		Info.Label = Volume->GetActorLabel();
		const FBox Bounds = Volume->GetComponentsBoundingBox(/*bNonColliding=*/true);
		Info.BoundsMinM = Bounds.Min / CmPerMeter;
		Info.BoundsMaxM = Bounds.Max / CmPerMeter;
		if (UPCGComponent* Component = Volume->PCGComponent)
		{
			if (UPCGGraph* Graph = Component->GetGraph())
			{
				Info.GraphPath = Graph->GetPathName();
			}
			Info.Seed = Component->Seed;
			Info.bGenerated = Component->bGenerated;
		}
		return Info;
	}

	APCGVolume* FindVolume(UWorld* World, const FString& Label, FString& OutError)
	{
		TArray<FString> Labels;
		for (TActorIterator<APCGVolume> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			{
				return *It;
			}
			Labels.Add(It->GetActorLabel());
		}
		OutError = FString::Printf(TEXT("No PCG volume labelled '%s'. Volumes: %s."), *Label, Labels.IsEmpty() ? TEXT("(none)") : *FString::Join(Labels, TEXT(", ")));
		return nullptr;
	}
}

FString UPCGWorldTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderPCGResult UPCGWorldTools::ListPCGGraphs(const FString& Folder)
{
	FWorldBuilderPCGResult Result;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UPCGGraph::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	if (Folder.IsEmpty() || Folder.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.PackagePaths.Add(TEXT("/AIWorldBuilder"));
	}
	else
	{
		Filter.PackagePaths.Add(*Folder);
	}

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		Result.Graphs.Add(Asset.GetObjectPathString());
	}
	Result.Graphs.Sort();
	Result.bSuccess = true;
	Result.Message = Result.Graphs.IsEmpty()
		? TEXT("No PCG graphs found. Build one with Epic's PCGToolset (CreateGraph, AddNode, ConnectNodePins) or add one from Fab.")
		: FString::Printf(TEXT("Found %d PCG graph(s)."), Result.Graphs.Num());
	return Result;
}

FWorldBuilderPCGResult UPCGWorldTools::ListPCGVolumes()
{
	FWorldBuilderPCGResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	for (TActorIterator<APCGVolume> It(World); It; ++It)
	{
		Result.Volumes.Add(DescribeVolume(*It));
	}
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%d PCG volume(s)."), Result.Volumes.Num());
	return Result;
}

FWorldBuilderPCGResult UPCGWorldTools::SpawnPCGVolume(const FString& GraphPath, double CenterXM, double CenterYM, double SizeXM, double SizeYM,
	int32 Seed, const FString& Label, bool bGenerate)
{
	FWorldBuilderPCGResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		Result.Message = TEXT("Stop Play-In-Editor first.");
		return Result;
	}
	if (SizeXM <= 0.0 || SizeYM <= 0.0)
	{
		Result.Message = TEXT("SizeXM and SizeYM must be greater than 0.");
		return Result;
	}
	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Graph)
	{
		Result.Message = FString::Printf(TEXT("PCG graph '%s' not found. Use ListPCGGraphs."), *GraphPath);
		return Result;
	}

	// Cover the full terrain height under the region (falls back to +/- 1 km around 0).
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	double MinZ = -100000.0, MaxZ = 100000.0;
	FString Ignored;
	if (const ALandscape* Landscape = LandscapeUtils::Resolve(World, TEXT("auto"), &CenterCm, Ignored))
	{
		const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
		if (Bounds.IsValid)
		{
			MinZ = Bounds.Min.Z - VolumeHeightMarginCm;
			MaxZ = Bounds.Max.Z + VolumeHeightMarginCm;
		}
	}
	const FVector Location(CenterCm.X, CenterCm.Y, (MinZ + MaxZ) * 0.5);

	FString VolumeLabel = Label;
	if (VolumeLabel.IsEmpty() || VolumeLabel.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		VolumeLabel = TEXT("PCG_") + Graph->GetName();
	}

	FScopedTransaction Transaction(LOCTEXT("SpawnPCGVolume", "AI: Spawn PCG Volume"));
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.InitialActorLabel = VolumeLabel;
	APCGVolume* Volume = World->SpawnActor<APCGVolume>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Volume)
	{
		Transaction.Cancel();
		Result.Message = TEXT("Failed to spawn the PCG volume.");
		return Result;
	}
	Volume->SetActorLabel(VolumeLabel);

	// A valid brush gives the volume real bounds (same approach as Epic's PCGToolset).
	UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(GetTransientPackage());
	CubeBuilder->X = MetersToCm(SizeXM);
	CubeBuilder->Y = MetersToCm(SizeYM);
	CubeBuilder->Z = MaxZ - MinZ;
	CubeBuilder->Hollow = false;
	UActorFactory::CreateBrushForVolumeActor(Volume, CubeBuilder);
	if (UBrushComponent* Brush = Volume->GetBrushComponent())
	{
		Brush->ReregisterComponent();
		Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Brush->SetCanEverAffectNavigation(false);
	}

	UPCGComponent* Component = Volume->PCGComponent;
	if (!Component)
	{
		Result.Message = TEXT("The spawned PCG volume has no PCG component.");
		return Result;
	}
	Component->Modify();
	Component->SetGraph(Graph);
	Component->Seed = Seed;
	// Generate only when asked: a heavy graph set to "Generate on Load" regenerates every time the level opens,
	// and if it overloads the GPU the level crashes on every open. Output generated on demand is saved with the level.
	Component->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
	if (bGenerate)
	{
		Component->Generate(/*bForce=*/true);
	}

	Result.Volumes.Add(DescribeVolume(Volume));
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Spawned PCG volume '%s' (%.0f x %.0f m) with graph %s, seed %d.%s"),
		*VolumeLabel, SizeXM, SizeYM, *Graph->GetName(), Seed,
		bGenerate ? TEXT(" Generation started; it completes over the next frames, so capture after a moment.") : TEXT(" Call GeneratePCG to generate."));
	return Result;
}

FWorldBuilderPCGResult UPCGWorldTools::GeneratePCG(const FString& VolumeLabel, int32 Seed)
{
	FWorldBuilderPCGResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	APCGVolume* Volume = FindVolume(World, VolumeLabel, Result.Message);
	if (!Volume || !Volume->PCGComponent)
	{
		return Result;
	}
	UPCGComponent* Component = Volume->PCGComponent;
	if (!Component->GetGraph())
	{
		Result.Message = FString::Printf(TEXT("PCG volume '%s' has no graph assigned."), *VolumeLabel);
		return Result;
	}
	if (Seed >= 0 && Seed != Component->Seed)
	{
		FScopedTransaction Transaction(LOCTEXT("SetPCGSeed", "AI: Set PCG Seed"));
		Component->Modify();
		Component->Seed = Seed;
	}
	Component->Generate(/*bForce=*/true);

	Result.Volumes.Add(DescribeVolume(Volume));
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Generation of '%s' started (seed %d); it completes over the next frames."), *VolumeLabel, Component->Seed);
	return Result;
}

FWorldBuilderPCGResult UPCGWorldTools::CleanupPCG(const FString& VolumeLabel, bool bDeleteVolume)
{
	FWorldBuilderPCGResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	APCGVolume* Volume = FindVolume(World, VolumeLabel, Result.Message);
	if (!Volume)
	{
		return Result;
	}

	FScopedTransaction Transaction(LOCTEXT("CleanupPCG", "AI: Clean Up PCG"));
	if (UPCGComponent* Component = Volume->PCGComponent)
	{
		Component->Cleanup(/*bRemoveComponents=*/true);
	}
	if (bDeleteVolume)
	{
		Volume->Modify();
		World->EditorDestroyActor(Volume, /*bShouldModifyLevel=*/true);
	}
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Cleaned up PCG output of '%s'%s."), *VolumeLabel, bDeleteVolume ? TEXT(" and deleted the volume") : TEXT(""));
	return Result;
}

#undef LOCTEXT_NAMESPACE
