#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "PCGWorldTools.generated.h"

/// One PCG volume in the level.
USTRUCT(BlueprintType)
struct FWorldBuilderPCGVolumeInfo
{
	GENERATED_BODY()

	/// Outliner label; pass it to GeneratePCG / CleanupPCG.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Label;

	/// Graph asset path (empty if none).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString GraphPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMinM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMaxM = FVector::ZeroVector;

	/// True if the component has generated output.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bGenerated = false;
};

/// Result of the PCG tools.
USTRUCT(BlueprintType)
struct FWorldBuilderPCGResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// PCG graph asset paths (ListPCGGraphs).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FString> Graphs;

	/// PCG volumes (ListPCGVolumes, SpawnPCGVolume).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderPCGVolumeInfo> Volumes;
};

/// Place and run PCG graphs over areas of the world, in meters: find graphs, spawn a PCG volume over a region with a graph and seed, generate, and clean up. The graph is the stored recipe; regenerate any time. To build or edit graphs and override graph parameters, use Epic's PCGToolset (CreateGraph, AddNode, SetGraphInstanceParams on the volume).
UCLASS(BlueprintType, Hidden)
class UPCGWorldTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Lists PCG graph assets.
	 * Example: ListPCGGraphs()
	 * @param Folder Content folder to search, or "auto" for /Game and the AI World Builder plugin content.
	 * @return Graph asset paths.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|PCG")
	static FWorldBuilderPCGResult ListPCGGraphs(const FString& Folder = TEXT("auto"));

	/**
	 * Lists PCG volumes in the level with their graph, seed, bounds and whether they have generated output.
	 * Example: ListPCGVolumes()
	 * @return The volumes.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|PCG")
	static FWorldBuilderPCGResult ListPCGVolumes();

	/**
	 * Spawns a PCG volume covering a region of the landscape (full terrain height), assigns a graph and seed, and optionally generates.
	 * One undo step. Generation runs over the next frames; capture after a moment to see the result.
	 * Example: SpawnPCGVolume("/Game/PCG/PCG_Forest.PCG_Forest", 0, 0, 1500, 1500, Label="Forest_Volcano")
	 * @param GraphPath PCG graph asset path.
	 * @param CenterXM Region centre X, meters.
	 * @param CenterYM Region centre Y, meters.
	 * @param SizeXM Region width along X, meters.
	 * @param SizeYM Region width along Y, meters.
	 * @param Seed PCG seed (change for a different variation).
	 * @param Label Outliner label, or "auto".
	 * @param bGenerate Generate immediately.
	 * @return The spawned volume.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|PCG")
	static FWorldBuilderPCGResult SpawnPCGVolume(const FString& GraphPath, double CenterXM, double CenterYM, double SizeXM, double SizeYM,
		int32 Seed = 42, const FString& Label = TEXT("auto"), bool bGenerate = true);

	/**
	 * (Re)generates a PCG volume, e.g. after changing its graph, parameters or the terrain under it.
	 * Example: GeneratePCG("Forest_Volcano")
	 * @param VolumeLabel Outliner label of the PCG volume.
	 * @param Seed New seed, or -1 to keep the current one.
	 * @return The volume.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|PCG")
	static FWorldBuilderPCGResult GeneratePCG(const FString& VolumeLabel, int32 Seed = -1);

	/**
	 * Removes a PCG volume's generated output, and optionally the volume itself.
	 * Example: CleanupPCG("Forest_Volcano", bDeleteVolume=true)
	 * @param VolumeLabel Outliner label of the PCG volume.
	 * @param bDeleteVolume Also delete the volume actor.
	 * @return What was done.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|PCG")
	static FWorldBuilderPCGResult CleanupPCG(const FString& VolumeLabel, bool bDeleteVolume = false);
};
