#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "LandscapeSculptTools.generated.h"

/// Terrain feature for ApplyShape.
UENUM(BlueprintType)
enum class EWorldBuilderShape : uint8
{
	/// Peak with radial falloff and noise ridges.
	Mountain,
	/// Cone with a crater, rim sharpness, eroded flanks and optional lava channel gaps.
	Volcano,
	/// Impact crater: bowl below the ground with a raised rim. HeightM is the depth.
	Crater,
	/// Smooth rounded hill.
	Hill,
	/// Flat top with a cliff edge.
	Plateau,
	/// Flat top, near-vertical cliffs and a talus slope at the base.
	Mesa,
};

/// Terrain feature for ApplyLineShape.
UENUM(BlueprintType)
enum class EWorldBuilderLineShape : uint8
{
	/// Trough along the line. HeightM is the depth.
	Valley,
	/// Raised crest along the line.
	Ridge,
};

/// How a shape combines with the existing terrain.
UENUM(BlueprintType)
enum class EWorldBuilderBlendMode : uint8
{
	/// Add the shape on top of the existing terrain (keeps underlying bumps).
	Add,
	/// Raise terrain up to the shape, never lower it.
	Max,
	/// Lower terrain down to the shape, never raise it.
	Min,
	/// Replace the terrain with the shape, blending at the edge.
	Replace,
	/// Blend between terrain and shape by BlendAlpha.
	Blend,
};

/// Brush falloff from centre (full effect) to edge (no effect).
UENUM(BlueprintType)
enum class EWorldBuilderFalloff : uint8
{
	Linear,
	/// Smoothstep: soft at both ends. Good default.
	Smooth,
	/// Rounded: stays strong until near the edge.
	Sphere,
	/// Sharp point at the centre.
	Tip,
};

/// Result of every sculpt tool. Heights and distances in meters.
USTRUCT(BlueprintType)
struct FWorldBuilderSculptResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Edit layer the change was written to (AI changes stay separate from hand sculpting).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString EditLayerName;

	/// True if this call created the edit layer.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bCreatedEditLayer = false;

	/// Area written, minimum corner (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMinM = FVector2D::ZeroVector;

	/// Area written, maximum corner (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMaxM = FVector2D::ZeroVector;

	/// Height samples in the area.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 SampleCount = 0;

	/// Samples whose height changed by more than 1 cm.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ChangedSampleCount = 0;

	/// Largest lowering (negative, m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinChangeM = 0.0;

	/// Largest raising (positive, m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxChangeM = 0.0;

	/// Lowest terrain height in the area afterwards (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double NewMinHeightM = 0.0;

	/// Highest terrain height in the area afterwards (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double NewMaxHeightM = 0.0;

	/// Samples that hit the landscape's height limits and were clamped.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 ClippedSampleCount = 0;

	/// True if the collision surface was re-read after the edit and matched the new terrain.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bCollisionVerified = false;

	/// Point checked for collision (the point that changed most), in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector VerifyPointM = FVector::ZeroVector;

	/// Collision height read back at VerifyPointM (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double CollisionHeightM = 0.0;
};

/// Sculpt real landscape terrain: mountains, volcanoes, craters, plateaus, valleys, flattening, smoothing, noise and carved paths. Use these tools to shape terrain. Do not approximate terrain features with static meshes. Every change writes real landscape heights into the "AI Sculpt" edit layer, can be undone with Ctrl+Z, and updates collision. Units: meters, world space.
UCLASS(BlueprintType, Hidden)
class ULandscapeSculptTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Builds a terrain feature centred on a point: Mountain, Volcano, Crater, Hill, Plateau or Mesa.
	 * Heights are relative to the ground at the centre (Max/Min/Replace/Blend) or added to the existing terrain (Add).
	 * Fails without changing anything if the result would exceed the landscape's height limits, unless bAllowClipping.
	 * Example: ApplyShape(Volcano, 0, 0, 1500, 400, LavaChannels=3) builds a 400 m volcano with a 1.5 km base radius.
	 * @param ShapeType Mountain, Volcano, Crater, Hill, Plateau or Mesa.
	 * @param CenterXM World X of the centre, meters.
	 * @param CenterYM World Y of the centre, meters.
	 * @param RadiusM Base radius in meters (rim radius for Crater).
	 * @param HeightM Peak height above the reference in meters (depth for Crater).
	 * @param BlendMode How to combine with existing terrain: Add, Max, Min, Replace or Blend.
	 * @param BlendAlpha Strength 0-1 for Add, Max, Min and Blend.
	 * @param NoiseAmount Roughness 0-1: irregular outline, ridges, gullies. 0 = perfectly smooth.
	 * @param Seed Random seed for the noise; change it for a different variation.
	 * @param CraterRadiusM Volcano crater radius in meters; 0 = 12% of RadiusM.
	 * @param CraterDepthM Volcano crater depth below the rim in meters; 0 = 20% of HeightM.
	 * @param RimSharpness 0-1: sharpness of crater rims and plateau/mesa cliff edges.
	 * @param LavaChannels Volcano: number of gaps in the crater rim with grooves down the flank (0 = none).
	 * @param TopFraction Plateau/Mesa: flat top radius as a fraction of RadiusM (0.05-0.95).
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits (they are clamped).
	 * @param LandscapeName Landscape label, or "auto" (default): the landscape containing the centre.
	 * @return What changed, including collision verification.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult ApplyShape(
		EWorldBuilderShape ShapeType, double CenterXM, double CenterYM, double RadiusM, double HeightM,
		EWorldBuilderBlendMode BlendMode = EWorldBuilderBlendMode::Add, double BlendAlpha = 1.0,
		double NoiseAmount = 0.3, int32 Seed = 1,
		double CraterRadiusM = 0.0, double CraterDepthM = 0.0, double RimSharpness = 0.5, int32 LavaChannels = 0,
		double TopFraction = 0.6, bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Builds a Valley or Ridge along a straight line. For winding rivers or roads use CarvePath.
	 * Example: ApplyLineShape(Valley, -800, 0, 800, 200, 120, 30) cuts a 30 m deep, 120 m wide valley.
	 * @param ShapeType Valley or Ridge.
	 * @param StartXM Line start X, meters.
	 * @param StartYM Line start Y, meters.
	 * @param EndXM Line end X, meters.
	 * @param EndYM Line end Y, meters.
	 * @param WidthM Total width in meters.
	 * @param HeightM Ridge height or valley depth in meters (positive).
	 * @param BlendMode Add, Max, Min, Replace or Blend.
	 * @param BlendAlpha Strength 0-1 for Add, Max, Min and Blend.
	 * @param NoiseAmount 0-1: meander and height variation along the line.
	 * @param Seed Random seed for the noise.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult ApplyLineShape(
		EWorldBuilderLineShape ShapeType, double StartXM, double StartYM, double EndXM, double EndYM, double WidthM, double HeightM,
		EWorldBuilderBlendMode BlendMode = EWorldBuilderBlendMode::Add, double BlendAlpha = 1.0,
		double NoiseAmount = 0.3, int32 Seed = 1, bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Raises (positive DeltaM) or lowers (negative) terrain in a circle with a soft falloff.
	 * Example: RaiseLower(100, 200, 50, 5) raises a 50 m radius area by up to 5 m.
	 * @param CenterXM World X, meters.
	 * @param CenterYM World Y, meters.
	 * @param RadiusM Radius in meters.
	 * @param DeltaM Height change at the centre in meters (negative lowers).
	 * @param Falloff Linear, Smooth, Sphere or Tip.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult RaiseLower(double CenterXM, double CenterYM, double RadiusM, double DeltaM,
		EWorldBuilderFalloff Falloff = EWorldBuilderFalloff::Smooth, bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Flattens a circle to an exact height, e.g. building sites and town plateaus. The inner area is fully flat;
	 * the outer EdgeFraction of the radius blends back into the surrounding terrain.
	 * Example: Flatten(600, 0, 80, 120) makes an 80 m radius site at 120 m height.
	 * @param CenterXM World X, meters.
	 * @param CenterYM World Y, meters.
	 * @param RadiusM Radius in meters, including the blend edge.
	 * @param TargetHeightM Height to flatten to, meters (world Z). Use SampleHeight to pick one.
	 * @param Strength 0-1: 1 = exactly flat, lower values only move part way.
	 * @param EdgeFraction 0-1: fraction of the radius used to blend into surrounding terrain.
	 * @param Falloff Falloff used in the blend edge.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult Flatten(double CenterXM, double CenterYM, double RadiusM, double TargetHeightM,
		double Strength = 1.0, double EdgeFraction = 0.3, EWorldBuilderFalloff Falloff = EWorldBuilderFalloff::Smooth,
		bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Smooths terrain in a circle (removes bumps and sharp edges) with a Gaussian-like blur.
	 * Example: Smooth(0, 0, 300, 0.7) smooths a 300 m radius area fairly strongly.
	 * @param CenterXM World X, meters.
	 * @param CenterYM World Y, meters.
	 * @param RadiusM Radius in meters.
	 * @param Strength 0-1 blend toward the smoothed terrain at the centre.
	 * @param KernelRadiusM Blur radius in meters (size of features removed); 0 = 5% of RadiusM.
	 * @param Iterations Blur passes 1-10; more passes = smoother.
	 * @param Falloff Falloff of the effect toward the edge.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult Smooth(double CenterXM, double CenterYM, double RadiusM, double Strength = 0.5,
		double KernelRadiusM = 0.0, int32 Iterations = 3, EWorldBuilderFalloff Falloff = EWorldBuilderFalloff::Smooth,
		const FString& LandscapeName = TEXT("auto"));

	/**
	 * Adds fractal (fBm) noise detail to a rectangle, fading out near its border. Use for natural roughness.
	 * Example: AddNoise(0, 0, 1000, 1000, 4, 150) adds +/-4 m bumps about 150 m across.
	 * @param CenterXM World X of the region centre, meters.
	 * @param CenterYM World Y of the region centre, meters.
	 * @param SizeXM Region width along X, meters.
	 * @param SizeYM Region width along Y, meters.
	 * @param AmplitudeM Maximum height change in meters.
	 * @param WavelengthM Size of the largest bumps in meters (frequency = 1 / wavelength).
	 * @param Octaves Detail layers 1-8; each adds features half the size.
	 * @param Seed Random seed.
	 * @param bRidged Ridged noise (sharp crests, raises only) instead of smooth bumps.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult AddNoise(double CenterXM, double CenterYM, double SizeXM, double SizeYM,
		double AmplitudeM, double WavelengthM = 200.0, int32 Octaves = 4, int32 Seed = 1, bool bRidged = false,
		bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Carves a channel along a polyline for rivers and roads. The bed follows the ground at each point minus DepthM,
	 * interpolated between points, so order points downhill for rivers.
	 * Example: CarvePath([{"x":-900,"y":300},{"x":-200,"y":100},{"x":600,"y":-400}], 20, 4) carves a 20 m wide, 4 m deep river.
	 * @param PointsM Polyline points in meters, world XY, at least 2.
	 * @param WidthM Width of the flat bed in meters.
	 * @param DepthM Bed depth below the ground at each point, meters (0 = a road at ground level).
	 * @param BankWidthM Width of the sloped bank on each side in meters; 0 = half of WidthM.
	 * @param Falloff Bank profile.
	 * @param bLowerOnly True (river): only cut. False (road): cut and fill to the bed height.
	 * @param bAllowClipping Apply even if some heights exceed the landscape's height limits.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return What changed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Sculpt")
	static FWorldBuilderSculptResult CarvePath(const TArray<FVector2D>& PointsM, double WidthM, double DepthM,
		double BankWidthM = 0.0, EWorldBuilderFalloff Falloff = EWorldBuilderFalloff::Smooth, bool bLowerOnly = true,
		bool bAllowClipping = false, const FString& LandscapeName = TEXT("auto"));
};
