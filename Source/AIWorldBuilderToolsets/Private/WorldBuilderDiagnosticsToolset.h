#pragma once

#include "CoreMinimal.h"
#include "FoliageScatterTools.h"
#include "PCGWorldTools.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "WorldBuilderDiagnosticsToolset.generated.h"

/// Status of the AI World Builder plugin and the currently open level.
USTRUCT(BlueprintType)
struct FWorldBuilderPluginStatus
{
	GENERATED_BODY()

	/// True if the status was read successfully.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	/// Short human-readable summary, or the reason for failure.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// AI World Builder plugin version (e.g. "0.1.0").
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Version;

	/// Name of the level currently open in the editor.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LevelName;

	/// Number of Landscape actors in the current level (streaming proxies are not counted separately).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 LandscapeCount = 0;

	/// True if the current level uses World Partition.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bWorldPartitionEnabled = false;
};

/// One landscape in a world overview. Distances and heights in meters.
USTRUCT(BlueprintType)
struct FWorldBuilderLandscapeOverview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMinM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMaxM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SampleSpacingM = 1.0;

	/// Lowest terrain found (coarse 48 x 48 scan of loaded data).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinHeightM = 0.0;

	/// Highest terrain found (coarse 48 x 48 scan of loaded data).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxHeightM = 0.0;

	/// Lowest height the landscape can represent (depends on its Z scale).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinPossibleHeightM = 0.0;

	/// Highest height the landscape can represent (depends on its Z scale).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxPossibleHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString MaterialPath;

	/// Edit layer names in stack order (AI work is in "AI Sculpt" and "AI Paint").
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FString> EditLayers;

	/// Paint layer names; "(no layer info)" marks layers that can't be painted yet.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FString> PaintLayers;
};

/// Lighting setup in a world overview.
USTRUCT(BlueprintType)
struct FWorldBuilderLightingOverview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasDirectionalLight = false;

	/// Sun elevation: 0 = horizon, -90 = overhead.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SunPitchDeg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SunYawDeg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SunIntensity = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasSkyLight = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasSkyAtmosphere = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasHeightFog = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasVolumetricClouds = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 PostProcessVolumeCount = 0;
};

/// Number of actors of one class.
USTRUCT(BlueprintType)
struct FWorldBuilderActorClassCount
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString ClassName;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 Count = 0;
};

/// Result of DescribeWorld.
USTRUCT(BlueprintType)
struct FWorldBuilderWorldDescription
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	/// Multi-line plain-text summary of everything below.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LevelName;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bWorldPartition = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderLandscapeOverview> Landscapes;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderFoliageCount> Foliage;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderPCGVolumeInfo> PCGVolumes;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FWorldBuilderLightingOverview Lighting;

	/// Player start locations (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FVector> PlayerStartsM;

	/// Most common actor classes among loaded actors (top 20).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderActorClassCount> ActorClasses;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 TotalActorCount = 0;
};

/// Result of TraceGround.
USTRUCT(BlueprintType)
struct FWorldBuilderGroundTraceResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// True if something that blocks the player was hit.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHit = false;

	/// Where the player would stand (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector HitLocationM = FVector::ZeroVector;

	/// Outliner label of the hit actor.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString HitActor;

	/// Class of the hit component (e.g. LandscapeHeightfieldCollisionComponent, InstancedStaticMeshComponent).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString HitComponentClass;

	/// True if the first blocking surface is landscape collision.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHitLandscape = false;

	/// Slope of the hit surface (degrees).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SurfaceSlopeDeg = 0.0;

	/// True if a default character can stand on it (slope at most about 44.8 degrees).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bWalkable = false;

	/// Landscape collision height at this point (m), for comparison (0 if there is no landscape here).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double LandscapeHeightM = 0.0;
};

/// Start here. Diagnostics and orientation for AI world building: plugin status, a full overview of the open world (landscapes, layers, foliage, PCG, lighting, actors), and ground/collision checks. Call DescribeWorld at the start of a session, and read the "AIWorldBuilder workflow" agent skill (AgentSkillToolset) for the recommended build process.
UCLASS(BlueprintType, Hidden)
class UWorldBuilderDiagnosticsToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Reports the AI World Builder plugin version and a quick summary of the level open in the editor:
	 * how many landscapes it contains and whether World Partition is enabled.
	 * Example: GetPluginStatus() -> { "version": "0.1.0", "landscapeCount": 1, "bWorldPartitionEnabled": true, ... }
	 * @return Plugin and level status.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Diagnostics")
	static FWorldBuilderPluginStatus GetPluginStatus();

	/**
	 * Summarizes the open world so you can orient yourself before building: landscapes (size, height range found and possible,
	 * material, edit layers, paint layers), foliage counts, PCG volumes, lighting (sun, sky, fog, clouds, post process),
	 * player starts and the most common actor classes. Only loaded World Partition data is included.
	 * Example: DescribeWorld()
	 * @return Structured overview plus a plain-text summary in message.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Diagnostics")
	static FWorldBuilderWorldDescription DescribeWorld();

	/**
	 * Traces straight down at a point the way a player's movement collides, to confirm there is solid, walkable ground
	 * (or what is in the way, e.g. a tree). Use after sculpting to verify collision, e.g. inside a crater or on a flattened site.
	 * Example: TraceGround(0, 0)
	 * @param XM World X in meters.
	 * @param YM World Y in meters.
	 * @return What was hit, where, whether it is landscape and whether a character can walk on it.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Diagnostics")
	static FWorldBuilderGroundTraceResult TraceGround(double XM, double YM);
};
