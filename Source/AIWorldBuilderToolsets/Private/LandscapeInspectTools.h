#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "LandscapeInspectTools.generated.h"

/// One landscape edit layer.
USTRUCT(BlueprintType)
struct FWorldBuilderEditLayerInfo
{
	GENERATED_BODY()

	/// Edit layer name as shown in the Landscape editor.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Name;

	/// True if this is the layer the Landscape editor is currently editing.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bIsEditingLayer = false;

	/// True if the layer contributes to the final terrain.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bVisible = true;

	/// True if the layer is locked against edits.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bLocked = false;
};

/// Summary of one landscape. All distances in meters.
USTRUCT(BlueprintType)
struct FWorldBuilderLandscapeSummary
{
	GENERATED_BODY()

	/// Outliner label. Pass this as LandscapeName to other tools.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Name;

	/// Full world bounds in meters (minimum corner), including unloaded World Partition regions.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMinM = FVector::ZeroVector;

	/// Full world bounds in meters (maximum corner).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMaxM = FVector::ZeroVector;

	/// Width along X in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SizeXM = 0.0;

	/// Width along Y in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SizeYM = 0.0;

	/// Height samples along X (vertices, e.g. 2017).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ResolutionX = 0;

	/// Height samples along Y.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ResolutionY = 0;

	/// Distance between height samples in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SampleSpacingM = 1.0;

	/// Total landscape components.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ComponentCount = 0;

	/// Components currently loaded in the editor. Lower than ComponentCount when World Partition regions are unloaded.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 LoadedComponentCount = 0;

	/// Quads per component side (e.g. 63, 127, 255).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ComponentSizeQuads = 0;

	/// Sections per component side (1 or 2).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 SectionsPerComponent = 0;

	/// Actor location in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector LocationM = FVector::ZeroVector;

	/// Actor scale (Unreal units; X/Y = sample spacing in cm, Z = vertical scale).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector Scale = FVector::OneVector;

	/// Lowest world height this landscape can represent, in meters. Depends on Z scale.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinPossibleHeightM = 0.0;

	/// Highest world height this landscape can represent, in meters. Terrain above this is clipped.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxPossibleHeightM = 0.0;

	/// Edit layers, in stack order.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderEditLayerInfo> EditLayers;

	/// True if the level uses World Partition.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bUsesWorldPartition = false;

	/// Number of landscape streaming proxies (World Partition tiles).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 StreamingProxyCount = 0;
};

/// Result of ListLandscapes.
USTRUCT(BlueprintType)
struct FWorldBuilderLandscapeListResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderLandscapeSummary> Landscapes;
};

/// A paint (weight) layer available on a landscape.
USTRUCT(BlueprintType)
struct FWorldBuilderPaintLayerInfo
{
	GENERATED_BODY()

	/// Layer name from the landscape material (e.g. "Grass").
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Name;

	/// True if a Landscape Layer Info asset is assigned. Painting needs one.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasLayerInfo = false;

	/// Asset path of the Layer Info, if any.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LayerInfoPath;
};

/// Result of GetLandscapeInfo.
USTRUCT(BlueprintType)
struct FWorldBuilderLandscapeDetailResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FWorldBuilderLandscapeSummary Summary;

	/// Asset path of the landscape material, or empty if none.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString MaterialPath;

	/// Paint layers available for PaintLayer / PaintByRules.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderPaintLayerInfo> PaintLayers;
};

/// Result of SampleHeight.
USTRUCT(BlueprintType)
struct FWorldBuilderHeightSampleResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// Landscape that was sampled.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Sample X in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double XM = 0.0;

	/// Sample Y in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double YM = 0.0;

	/// Ground height (world Z) in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double HeightM = 0.0;
};

/// Result of SampleHeightGrid.
USTRUCT(BlueprintType)
struct FWorldBuilderHeightGridResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Samples per side.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 GridCount = 0;

	/// Distance between grid points in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SpacingM = 0.0;

	/// World X of the first column, in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double OriginXM = 0.0;

	/// World Y of the first row, in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double OriginYM = 0.0;

	/// Heights in meters, row-major: Heights[row * GridCount + col] is at (OriginXM + col*SpacingM, OriginYM + row*SpacingM).
	/// Points with no landscape data hold MissingValue.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<double> Heights;

	/// Placeholder used in Heights where there is no data (outside the landscape or not loaded).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MissingValue = -1000000.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 MissingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxHeightM = 0.0;
};

/// Result of GetHeightStats.
USTRUCT(BlueprintType)
struct FWorldBuilderHeightStatsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Region actually measured (clipped to the landscape), minimum corner in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMinM = FVector2D::ZeroVector;

	/// Region actually measured, maximum corner in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMaxM = FVector2D::ZeroVector;

	/// Points measured. Large regions are sampled on a grid of at most 256 x 256 points.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 SampleCount = 0;

	/// Distance between measured points in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SampleSpacingM = 0.0;

	/// Points with no data (not loaded in World Partition, or no collision).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 MissingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double AverageHeightM = 0.0;

	/// Location (meters) of the lowest point found.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector LowestPointM = FVector::ZeroVector;

	/// Location (meters) of the highest point found.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector HighestPointM = FVector::ZeroVector;

	/// Average ground slope in degrees, measured at full landscape resolution at each point.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double AverageSlopeDeg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxSlopeDeg = 0.0;

	/// Fraction (0-1) of points flatter than 5 degrees: good for building sites.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double FlatFraction = 0.0;

	/// Fraction (0-1) of points steeper than 35 degrees: cliffs and rock.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SteepFraction = 0.0;
};

/// Read-only landscape inspection: find landscapes, their size, layers and heights, and measure terrain. Use these tools to understand terrain before shaping it. Do not approximate terrain features with static meshes. All distances and heights are in meters, world space.
UCLASS(BlueprintType, Hidden)
class ULandscapeInspectTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Lists every landscape in the open level with its bounds, size, resolution, components, scale,
	 * possible height range and edit layers. Call this first to learn landscape names and extents.
	 * Example: ListLandscapes()
	 * @return All landscapes in the level. Distances are in meters.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Landscape")
	static FWorldBuilderLandscapeListResult ListLandscapes();

	/**
	 * Detailed information about one landscape: everything ListLandscapes reports plus its material
	 * and the paint layers available for painting.
	 * Example: GetLandscapeInfo("Landscape")
	 * @param LandscapeName Landscape label, or "auto" (default) when the level has exactly one landscape.
	 * @return Landscape details. Distances are in meters.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Landscape")
	static FWorldBuilderLandscapeDetailResult GetLandscapeInfo(const FString& LandscapeName = TEXT("auto"));

	/**
	 * Ground height of the landscape at one world point, read from its collision surface.
	 * Example: SampleHeight(250, -1200) -> heightM 37.5
	 * @param XM World X in meters.
	 * @param YM World Y in meters.
	 * @param LandscapeName Landscape label, or "auto" (default): the landscape containing the point is used.
	 * @return Height (world Z) in meters at that point.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Landscape")
	static FWorldBuilderHeightSampleResult SampleHeight(double XM, double YM, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Samples a square grid of ground heights so you can "feel" the shape of the terrain.
	 * Example: SampleHeightGrid(0, 0, 2000, 16) samples 16 x 16 points over a 2 km square centred on the origin.
	 * @param CenterXM World X of the grid centre, in meters.
	 * @param CenterYM World Y of the grid centre, in meters.
	 * @param SizeM Width of the square in meters (greater than 0).
	 * @param GridCount Points per side, 2 to 64.
	 * @param LandscapeName Landscape label, or "auto" (default): the landscape containing the centre is used.
	 * @return Row-major heights in meters plus min/max.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Landscape")
	static FWorldBuilderHeightGridResult SampleHeightGrid(double CenterXM, double CenterYM, double SizeM, int32 GridCount = 16, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Measures a rectangular region: min/max/average height, where the lowest and highest points are,
	 * and slope statistics (average, max, fraction flat under 5 degrees, fraction steep over 35 degrees).
	 * Leave SizeXM and SizeYM at 0 to measure the whole landscape.
	 * Example: GetHeightStats(0, 0, 1000, 1000) measures a 1 km square centred on the origin.
	 * @param CenterXM World X of the region centre, in meters.
	 * @param CenterYM World Y of the region centre, in meters.
	 * @param SizeXM Region width along X in meters; 0 = whole landscape.
	 * @param SizeYM Region width along Y in meters; 0 = whole landscape.
	 * @param LandscapeName Landscape label, or "auto" (default): the landscape containing the centre (or the only landscape) is used.
	 * @return Height and slope statistics in meters and degrees.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Landscape")
	static FWorldBuilderHeightStatsResult GetHeightStats(double CenterXM = 0.0, double CenterYM = 0.0, double SizeXM = 0.0, double SizeYM = 0.0, const FString& LandscapeName = TEXT("auto"));
};
