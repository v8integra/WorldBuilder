#pragma once

#include "CoreMinimal.h"
#include "LandscapeSculptTools.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "LandscapeCreateTools.generated.h"

/// Procedural base terrain for GenerateTerrain.
UENUM(BlueprintType)
enum class EWorldBuilderTerrainPreset : uint8
{
	/// Gentle rounded hills, 0 to AmplitudeM.
	RollingHills,
	/// Sharp ridged mountain ranges on a broad uplift, 0 to AmplitudeM.
	Mountains,
	/// Land in the middle of the region fading to sea floor at the edges (base height = sea level).
	Islands,
	/// Terraced plateau cut by winding canyons, 0 to AmplitudeM.
	Canyons,
	/// Nearly flat ground with small undulations (about 10% of AmplitudeM).
	Plains,
};

/// How 16-bit heightmap values map to heights.
UENUM(BlueprintType)
enum class EWorldBuilderHeightEncoding : uint8
{
	/// Unreal landscape encoding of the target landscape: 32768 = the landscape's base Z, scaled by its Z scale. Round-trips ExportHeightmap.
	Native,
	/// Linear: 0 = MinHeightM, 65535 = MaxHeightM.
	Range,
};

/// Result of CreateLandscape. Distances in meters.
USTRUCT(BlueprintType)
struct FWorldBuilderCreateLandscapeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// Outliner label of the new landscape.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Actual size along X (the nearest valid Unreal landscape size to the request).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SizeXM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SizeYM = 0.0;

	/// Height samples along X.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ResolutionX = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ResolutionY = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ComponentCountX = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ComponentCountY = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double SampleSpacingM = 1.0;

	/// Vertical scale chosen to fit MaxHeightM.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double ScaleZ = 100.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinPossibleHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxPossibleHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMinM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMaxM = FVector::ZeroVector;

	/// True if the landscape was split into World Partition streaming proxies.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bWorldPartition = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 StreamingProxyCount = 0;
};

/// Result of ExportHeightmap.
USTRUCT(BlueprintType)
struct FWorldBuilderHeightmapExportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Absolute path of the written file.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString FilePath;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 WidthPx = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 HeightPx = 0;

	/// World area covered, minimum corner (m). Pixel (0,0) is here; columns run along +X, rows along +Y.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMinM = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMaxM = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxHeightM = 0.0;

	/// How to turn a pixel value back into meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Encoding;
};

/// Create landscapes and whole terrains: new landscapes of a chosen size, procedural base terrain (hills, mountains, islands, canyons, plains), and 16-bit heightmap import/export. Use these tools to make terrain. Do not approximate terrain with static meshes. Large areas are processed in tiles. Units: meters, world space.
UCLASS(BlueprintType, Hidden)
class ULandscapeCreateTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates a new flat landscape in the open level, picking the valid Unreal landscape size closest to the request,
	 * and splits it into World Partition streaming proxies when the level uses World Partition. One undo step.
	 * Choose MaxHeightM for the tallest terrain you plan (it sets the Z scale; it can't be raised later without rescaling heights).
	 * Example: CreateLandscape(4, 4, MaxHeightM=600) creates a ~4 x 4 km landscape that can hold terrain up to ~600 m.
	 * @param SizeXKm Requested size along X in kilometers.
	 * @param SizeYKm Requested size along Y in kilometers.
	 * @param CenterXM World X of the landscape centre, meters.
	 * @param CenterYM World Y of the landscape centre, meters.
	 * @param BaseHeightM World Z of the flat landscape (and of height 0 in Native encoding), meters.
	 * @param MaxHeightM Highest terrain above BaseHeightM the landscape must hold (the same depth below is also available), meters.
	 * @param SampleSpacingM Distance between height samples in meters (1 = standard detail; 2-4 for very large maps).
	 * @param QuadsPerSection Section size: 7, 15, 31, 63, 127 or 255.
	 * @param SectionsPerComponent 1 or 2 (sections per component side).
	 * @param WorldPartitionGridSize Components per World Partition streaming proxy side.
	 * @param MaterialPath Landscape material asset path, "auto" (copy the existing landscape's material) or "none".
	 * @param Label Outliner label, or "auto" for a unique "Landscape" name.
	 * @return The new landscape's real size, resolution and height range.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Create")
	static FWorldBuilderCreateLandscapeResult CreateLandscape(double SizeXKm, double SizeYKm, double CenterXM = 0.0, double CenterYM = 0.0,
		double BaseHeightM = 0.0, double MaxHeightM = 256.0, double SampleSpacingM = 1.0, int32 QuadsPerSection = 63, int32 SectionsPerComponent = 2,
		int32 WorldPartitionGridSize = 2, const FString& MaterialPath = TEXT("auto"), const FString& Label = TEXT("auto"));

	/**
	 * Generates procedural base terrain over a region (or the whole landscape): RollingHills, Mountains, Islands, Canyons or Plains,
	 * using fBm and ridged noise, with optional thermal erosion. Combine several calls on different regions to compose a world,
	 * then refine with the sculpt tools. Writes into the "AI Sculpt" edit layer; each tile is one undo step.
	 * Example: GenerateTerrain(Mountains, 0, 1000, 4000, 2000, AmplitudeM=450) puts mountains across the northern half of a 4 km map.
	 * @param Preset RollingHills, Mountains, Islands, Canyons or Plains.
	 * @param CenterXM Region centre X, meters (ignored for the whole landscape).
	 * @param CenterYM Region centre Y, meters (ignored for the whole landscape).
	 * @param SizeXM Region width along X, meters; 0 (with SizeYM 0) = whole landscape.
	 * @param SizeYM Region width along Y, meters.
	 * @param BaseHeightM Height the terrain is built up from (sea level for Islands), meters.
	 * @param AmplitudeM Height range of the terrain above BaseHeightM, meters.
	 * @param WavelengthM Size of the largest features, meters (e.g. 800 = hills about 800 m apart).
	 * @param Seed Random seed.
	 * @param ErosionIterations Thermal erosion passes (0 = off; 20-100 typical). Only applied when the region fits in one tile (4 km at 1 m spacing).
	 * @param ErosionTalusDeg Slopes steeper than this crumble during erosion, degrees.
	 * @param EdgeBlendM Width of the border over which the new terrain blends into the existing terrain, meters (not used for the whole landscape).
	 * @param BlendMode Replace (default: new base terrain), Add (layer on top), Max, Min or Blend.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed, summed over all tiles.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Create")
	static FWorldBuilderSculptResult GenerateTerrain(EWorldBuilderTerrainPreset Preset, double CenterXM = 0.0, double CenterYM = 0.0,
		double SizeXM = 0.0, double SizeYM = 0.0, double BaseHeightM = 0.0, double AmplitudeM = 150.0, double WavelengthM = 800.0, int32 Seed = 1,
		int32 ErosionIterations = 0, double ErosionTalusDeg = 35.0, double EdgeBlendM = 100.0,
		EWorldBuilderBlendMode BlendMode = EWorldBuilderBlendMode::Replace, bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Imports a 16-bit heightmap (PNG, or RAW/R16 little-endian) onto a region of an existing landscape, resampling it to fit.
	 * Tiled sets named like name_x0_y0.png are detected and stitched automatically. Pixel columns run along +X and rows along +Y.
	 * Writes into the "AI Sculpt" edit layer; each tile is one undo step.
	 * Example: ImportHeightmap("C:/Maps/island.png", Encoding=Range, MinHeightM=-20, MaxHeightM=400) fits the image to the whole landscape.
	 * @param FilePath Absolute path of the .png, .r16 or .raw file (for a tiled set, any one tile).
	 * @param CenterXM Region centre X, meters (ignored for the whole landscape).
	 * @param CenterYM Region centre Y, meters (ignored for the whole landscape).
	 * @param SizeXM Region width along X, meters; 0 (with SizeYM 0) = whole landscape.
	 * @param SizeYM Region width along Y, meters.
	 * @param Encoding Native (Unreal landscape values; matches ExportHeightmap) or Range (0 = MinHeightM, 65535 = MaxHeightM).
	 * @param MinHeightM Height of value 0 for Range encoding, meters.
	 * @param MaxHeightM Height of value 65535 for Range encoding, meters.
	 * @param BlendMode Replace (default), Add, Max, Min or Blend.
	 * @param EdgeBlendM Border width blending into existing terrain, meters (0 = hard edge).
	 * @param bFlipY Flip the image vertically.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed, summed over all tiles.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Create")
	static FWorldBuilderSculptResult ImportHeightmap(const FString& FilePath, double CenterXM = 0.0, double CenterYM = 0.0, double SizeXM = 0.0, double SizeYM = 0.0,
		EWorldBuilderHeightEncoding Encoding = EWorldBuilderHeightEncoding::Native, double MinHeightM = 0.0, double MaxHeightM = 256.0,
		EWorldBuilderBlendMode BlendMode = EWorldBuilderBlendMode::Replace, double EdgeBlendM = 0.0, bool bFlipY = false,
		bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Exports the landscape's final heights (all visible edit layers) for a region or the whole landscape as a 16-bit file
	 * in Native encoding: one pixel per height sample, columns along +X, rows along +Y.
	 * Example: ExportHeightmap() writes the whole landscape to Saved/AIWorldBuilder/Heightmaps/.
	 * @param FilePath Output path ending in .png, .r16 or .raw; "auto" = a new file in Saved/AIWorldBuilder/Heightmaps/. Relative paths go there too.
	 * @param CenterXM Region centre X, meters (ignored for the whole landscape).
	 * @param CenterYM Region centre Y, meters (ignored for the whole landscape).
	 * @param SizeXM Region width along X, meters; 0 (with SizeYM 0) = whole landscape.
	 * @param SizeYM Region width along Y, meters.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return The file path, size, height range and how to decode values.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Create")
	static FWorldBuilderHeightmapExportResult ExportHeightmap(const FString& FilePath = TEXT("auto"), double CenterXM = 0.0, double CenterYM = 0.0,
		double SizeXM = 0.0, double SizeYM = 0.0, const FString& LandscapeName = TEXT("auto"));
};
