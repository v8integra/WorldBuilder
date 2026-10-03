#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "FoliageScatterTools.generated.h"

/// A circle on the ground, in meters.
USTRUCT(BlueprintType)
struct FWorldBuilderCircle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double XM = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double YM = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double RadiusM = 0.0;
};

/// Instance count for one foliage type.
USTRUCT(BlueprintType)
struct FWorldBuilderFoliageCount
{
	GENERATED_BODY()

	/// Foliage type asset path (or the IFA-local type name).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString FoliageType;

	/// Static mesh path.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Mesh;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 Count = 0;
};

/// Result of the foliage tools.
USTRUCT(BlueprintType)
struct FWorldBuilderFoliageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Candidate positions tested (ScatterFoliage).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 CandidateCount = 0;

	/// Instances added (ScatterFoliage) or removed (RemoveFoliage).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 InstanceCount = 0;

	/// Why candidates were rejected, e.g. {"slope": 120, "height": 40, "layer": 300, "excluded": 25, "noGround": 0}.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TMap<FString, int32> Rejected;

	/// Per foliage type counts (instances added/removed, or totals for ListFoliage).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderFoliageCount> Types;
};

/// Scatter and remove foliage instances (trees, rocks, bushes) on the landscape by density, slope, height, paint layer and exclusion zones. Instances are real foliage (instanced static meshes with collision as set on the foliage type), one undo step per call. Skeletal meshes such as Megaplant trees are converted automatically. Heavy trees (Megaplants) are millions of triangles each: start with low densities (50-150 per hectare) on small regions and capture to check performance. For rule graphs that regenerate, see PCGWorldTools. Units: meters and degrees, world space.
UCLASS(BlueprintType, Hidden)
class UFoliageScatterTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Places foliage instances of one or more meshes over a region (or the whole landscape) on a jittered grid,
	 * keeping only spots that pass the slope, height, paint-layer and exclusion rules. Meshes are picked at random per spot.
	 * A Foliage Type asset is created per mesh (reused if it exists). Save all afterwards.
	 * Example: ScatterFoliage(["/Game/Trees/SM_Pine.SM_Pine"], DensityPerHectare=300, MaxSlopeDeg=30, MaxHeightM=250,
	 *          ExcludeAreas=[{"xM":600,"yM":0,"radiusM":120}]) plants a pine forest below 250 m, avoiding a village site.
	 * @param MeshPaths Static mesh, skeletal mesh or Foliage Type asset paths, e.g. "/Game/Trees/SM_Pine.SM_Pine". Skeletal meshes (Megaplant trees) are converted to static meshes once (no wind animation).
	 * @param ExcludeAreas Circles to keep clear ([] for none), each {"xM", "yM", "radiusM"}.
	 * @param CenterXM Region centre X, meters (ignored for the whole landscape).
	 * @param CenterYM Region centre Y, meters (ignored for the whole landscape).
	 * @param SizeXM Region width along X, meters; 0 (with SizeYM 0) = whole landscape.
	 * @param SizeYM Region width along Y, meters.
	 * @param DensityPerHectare Target instances per 10,000 m² before filtering (forest ~200-600, scattered rocks ~20).
	 * @param MinSlopeDeg Lowest ground slope allowed, degrees.
	 * @param MaxSlopeDeg Steepest ground slope allowed, degrees.
	 * @param MinHeightM Lowest ground height allowed, meters (default: no limit).
	 * @param MaxHeightM Highest ground height allowed, meters (default: no limit).
	 * @param LayerName Only place where this paint layer is present (e.g. "Grass"), or "none".
	 * @param MinLayerWeight Minimum paint weight 0-1 for LayerName.
	 * @param MinScale Smallest random uniform scale.
	 * @param MaxScale Largest random uniform scale.
	 * @param bAlignToNormal Tilt instances to the ground slope (rocks, bushes); keep false for trees.
	 * @param SinkM Push instances this far into the ground, meters (hides floating trunks on slopes).
	 * @param Seed Random seed (same seed and settings = same result).
	 * @param MaxInstances Safety cap: refuse (place nothing) if more instances pass the rules. Heavy meshes like Megaplant trees: keep well under 20000 per region.
	 * @param FoliageFolder Content folder for created Foliage Type assets.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return Instances placed per type and rejection reasons.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Foliage")
	static FWorldBuilderFoliageResult ScatterFoliage(const TArray<FString>& MeshPaths, const TArray<FWorldBuilderCircle>& ExcludeAreas,
		double CenterXM = 0.0, double CenterYM = 0.0, double SizeXM = 0.0, double SizeYM = 0.0, double DensityPerHectare = 200.0,
		double MinSlopeDeg = 0.0, double MaxSlopeDeg = 30.0, double MinHeightM = -100000.0, double MaxHeightM = 100000.0,
		const FString& LayerName = TEXT("none"), double MinLayerWeight = 0.5, double MinScale = 0.8, double MaxScale = 1.2,
		bool bAlignToNormal = false, double SinkM = 0.0, int32 Seed = 1, int32 MaxInstances = 50000, const FString& FoliageFolder = TEXT("/Game/Landscape/Foliage"),
		const FString& LandscapeName = TEXT("auto"));

	/**
	 * Removes foliage instances inside a circle, e.g. to clear a village site or a crater. One undo step.
	 * Example: RemoveFoliage(600, 0, 120, []) clears all foliage within 120 m of (600, 0).
	 * @param CenterXM Circle centre X, meters.
	 * @param CenterYM Circle centre Y, meters.
	 * @param RadiusM Circle radius, meters.
	 * @param MeshPaths Only remove these meshes / foliage types ([] = all foliage).
	 * @return Instances removed per type.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Foliage")
	static FWorldBuilderFoliageResult RemoveFoliage(double CenterXM, double CenterYM, double RadiusM, const TArray<FString>& MeshPaths);

	/**
	 * Lists all foliage types in the level with their instance counts.
	 * Example: ListFoliage()
	 * @return Instance counts per foliage type.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Foliage")
	static FWorldBuilderFoliageResult ListFoliage();
};
