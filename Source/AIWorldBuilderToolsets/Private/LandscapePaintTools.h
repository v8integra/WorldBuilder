#pragma once

#include "CoreMinimal.h"
#include "LandscapeSculptTools.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "LandscapePaintTools.generated.h"

/// How a paint layer's weight interacts with the other layers.
UENUM(BlueprintType)
enum class EWorldBuilderWeightBlend : uint8
{
	/// Advanced Weight Blending (recommended): layers share 100% between them and respect edit layers.
	Advanced,
	/// Legacy weight blending: normalized only after all edit layers are merged.
	Legacy,
	/// No weight blending: the layer is independent (use for overlays such as puddles).
	None,
};

/// One paint layer of a landscape.
USTRUCT(BlueprintType)
struct FWorldBuilderPaintLayerStatus
{
	GENERATED_BODY()

	/// Layer name, as used by the landscape material's Layer Blend node.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Name;

	/// True if the landscape material defines this layer.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bInMaterial = false;

	/// True if a Layer Info asset is assigned. Painting needs one; use CreateLayerInfos.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bHasLayerInfo = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LayerInfoPath;

	/// Advanced, Legacy or None (None means painting this layer does not reduce the others).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString BlendMethod;
};

/// Result of ListPaintLayers and CreateLayerInfos.
USTRUCT(BlueprintType)
struct FWorldBuilderPaintLayerListResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Landscape material asset path (empty if none).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString MaterialPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderPaintLayerStatus> Layers;
};

/// One auto-paint rule. A sample matches when its height and slope fall in the ranges; edges are soft.
USTRUCT(BlueprintType)
struct FWorldBuilderPaintRule
{
	GENERATED_BODY()

	/// Paint layer to apply where the rule matches (e.g. "Rock").
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	FString LayerName;

	/// Lowest terrain height (m) for this rule. Default: no lower limit.
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double MinHeightM = -100000.0;

	/// Highest terrain height (m) for this rule. Default: no upper limit.
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double MaxHeightM = 100000.0;

	/// Lowest slope (degrees) for this rule, e.g. 35 for rock on steep ground.
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double MinSlopeDeg = 0.0;

	/// Highest slope (degrees) for this rule.
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double MaxSlopeDeg = 90.0;

	/// Width of the soft transition at the height limits (m).
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double HeightBlendM = 10.0;

	/// Width of the soft transition at the slope limits (degrees).
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double SlopeBlendDeg = 4.0;

	/// 0-1: how strongly this rule paints over the rules before it.
	UPROPERTY(BlueprintReadWrite, Category = "AIWorldBuilder")
	double Strength = 1.0;
};

/// Result of the painting tools.
USTRUCT(BlueprintType)
struct FWorldBuilderPaintResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Edit layer the paint was written to.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString EditLayerName;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bCreatedEditLayer = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMinM = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMaxM = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 SampleCount = 0;

	/// Average weight of each paint layer in the area afterwards (0-1), e.g. {"Grass": 0.6, "Rock": 0.3}.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TMap<FString, double> LayerCoverage;
};

/// Paint landscape material layers (grass, rock, snow, sand...): list them, create missing Layer Info assets, brush-paint, and auto-paint by height and slope rules. Paint goes into the "AI Paint" edit layer and every call can be undone. Use after the terrain is shaped, then capture views to check the result. Units: meters and degrees, world space.
UCLASS(BlueprintType, Hidden)
class ULandscapePaintTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Lists the landscape's paint layers: those in its material and those already set up, whether each has a Layer Info asset
	 * (needed to paint) and its weight blend method. Call this before painting.
	 * Example: ListPaintLayers()
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return Paint layers and the material.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Paint")
	static FWorldBuilderPaintLayerListResult ListPaintLayers(const FString& LandscapeName = TEXT("auto"));

	/**
	 * Creates Layer Info assets for paint layers that don't have one (needed before they can be painted) and assigns them to the landscape.
	 * Also sets the weight blend method, by default Advanced so painting one layer reduces the others.
	 * Example: CreateLayerInfos([]) sets up every layer of the landscape material.
	 * @param LayerNames Layers to set up; an empty list means every layer of the landscape material.
	 * @param FolderPath Content folder for the new assets.
	 * @param BlendMethod Advanced (recommended), Legacy or None.
	 * @param bUpdateExisting Also apply BlendMethod to layers that already have a Layer Info.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return The paint layers afterwards.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Paint")
	static FWorldBuilderPaintLayerListResult CreateLayerInfos(const TArray<FString>& LayerNames, const FString& FolderPath = TEXT("/Game/Landscape/LayerInfos"),
		EWorldBuilderWeightBlend BlendMethod = EWorldBuilderWeightBlend::Advanced, bool bUpdateExisting = true, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Paints one layer in a circle with a soft falloff, like the editor's paint brush.
	 * Example: PaintLayer("Dirt", 600, 0, 40) paints a 40 m radius dirt patch.
	 * @param LayerName Paint layer name.
	 * @param CenterXM World X, meters.
	 * @param CenterYM World Y, meters.
	 * @param RadiusM Radius in meters.
	 * @param Strength 0-1 at the centre.
	 * @param Falloff Linear, Smooth, Sphere or Tip.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return Coverage per layer in the area afterwards.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Paint")
	static FWorldBuilderPaintResult PaintLayer(const FString& LayerName, double CenterXM, double CenterYM, double RadiusM, double Strength = 1.0,
		EWorldBuilderFalloff Falloff = EWorldBuilderFalloff::Smooth, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Auto-paints a region (or the whole landscape) from height and slope rules: the key tool for realistic texturing.
	 * Rules apply in order, each painting over the ones before where it matches, so list a base layer first
	 * (no limits), then more specific layers. Replaces earlier AI paint in the area.
	 * Example rules: [{"layerName":"Grass"}, {"layerName":"Sand","maxHeightM":5}, {"layerName":"Rock","minSlopeDeg":35},
	 * {"layerName":"Snow","minHeightM":1800}] = grass everywhere, sand up to 5 m (near water at 0 m), rock on slopes over 35 degrees, snow above 1800 m.
	 * @param Rules Ordered rules; each has layerName and optional minHeightM, maxHeightM, minSlopeDeg, maxSlopeDeg, heightBlendM, slopeBlendDeg, strength.
	 * @param CenterXM Region centre X, meters (ignored for the whole landscape).
	 * @param CenterYM Region centre Y, meters (ignored for the whole landscape).
	 * @param SizeXM Region width along X, meters; 0 (with SizeYM 0) = whole landscape.
	 * @param SizeYM Region width along Y, meters.
	 * @param EdgeNoise 0-1: breaks up the transitions between layers so they look natural.
	 * @param EdgeBlendM For regions: border width blending into the existing paint, meters.
	 * @param Seed Noise seed.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return Coverage per layer, summed over all tiles.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Paint")
	static FWorldBuilderPaintResult PaintByRules(const TArray<FWorldBuilderPaintRule>& Rules, double CenterXM = 0.0, double CenterYM = 0.0,
		double SizeXM = 0.0, double SizeYM = 0.0, double EdgeNoise = 0.3, double EdgeBlendM = 20.0, int32 Seed = 1, const FString& LandscapeName = TEXT("auto"));
};
