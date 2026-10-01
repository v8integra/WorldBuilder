#pragma once

#include "CoreMinimal.h"

/**
 * Pure terrain math: noise, falloffs, shape profiles and blend rules. No engine state, so it is unit-tested.
 * Distances and heights are in centimeters unless a name says otherwise.
 */
namespace AIWorldBuilder::TerrainMath
{
	enum class EFalloff : uint8 { Linear, Smooth, Sphere, Tip };
	enum class EHeightBlendMode : uint8 { Add, Max, Min, Replace, Blend };
	enum class ERadialShape : uint8 { Mountain, Volcano, Crater, Hill, Plateau, Mesa };
	enum class ELineShape : uint8 { Valley, Ridge };

	// ---------------------------------------------------------------- noise

	/** Gradient noise in [-1, 1], decorrelated per seed. */
	AIWORLDBUILDERCORE_API double Noise(const FVector2D& P, int32 Seed);
	/** Fractal Brownian motion in [-1, 1]. */
	AIWORLDBUILDERCORE_API double FBM(const FVector2D& P, int32 Octaves, int32 Seed);
	/** Ridged multifractal in [0, 1]; 1 on sharp ridge lines. */
	AIWORLDBUILDERCORE_API double Ridged(const FVector2D& P, int32 Octaves, int32 Seed);

	// ---------------------------------------------------------------- falloff and blending

	/** Weight for normalized distance T (0 = centre, 1 = edge): 1 at the centre, 0 at and beyond the edge. */
	AIWORLDBUILDERCORE_API double Falloff(EFalloff Type, double T);

	/** Weight that is 1 inside (1 - EdgeFraction) of the radius and falls off over the outer EdgeFraction. */
	AIWORLDBUILDERCORE_API double EdgeWeight(EFalloff Type, double T, double EdgeFraction);

	AIWORLDBUILDERCORE_API double SmoothStep(double Edge0, double Edge1, double X);

	/**
	 * Combines the current height with a shape.
	 * @param Current  Current ground height.
	 * @param Reference Ground reference the shape sits on (Max/Min/Replace/Blend use Reference + ShapeHeight).
	 * @param ShapeHeight Shape height relative to Reference (Add adds this directly).
	 * @param Weight   Shape footprint mask (0..1), tapering to 0 at the edge.
	 * @param Alpha    Strength for Add and Blend (and Max/Min); ignored by Replace.
	 */
	AIWORLDBUILDERCORE_API double BlendHeight(EHeightBlendMode Mode, double Current, double Reference, double ShapeHeight, double Weight, double Alpha);

	// ---------------------------------------------------------------- shapes

	struct FShapeSample
	{
		double Height = 0.0;	// relative to the shape's reference ground
		double Weight = 0.0;	// footprint mask, 0..1
	};

	struct FRadialShapeParams
	{
		double RadiusCm = 10000.0;		// base radius (rim radius for Crater)
		double HeightCm = 10000.0;		// peak height (depth for Crater)
		double NoiseAmount = 0.3;		// 0 = perfectly smooth, 1 = very rough
		int32 Seed = 1;
		double CraterRadiusCm = 0.0;	// Volcano: crater radius (0 = 12% of base radius)
		double CraterDepthCm = 0.0;		// Volcano: crater depth below the rim (0 = 20% of height)
		double RimSharpness = 0.5;		// 0..1: crater rim / cliff edge sharpness
		int32 LavaChannels = 0;			// Volcano: gaps in the rim with grooves running down the flank
		double TopFraction = 0.6;		// Plateau/Mesa: flat top radius as a fraction of the base radius
	};

	/** How far from the centre a radial shape reaches, in cm. Crater rims spill past the radius. */
	AIWORLDBUILDERCORE_API double GetFootprintRadiusCm(ERadialShape Shape, const FRadialShapeParams& Params);

	/** Evaluates a radial shape at Offset (cm) from its centre. */
	AIWORLDBUILDERCORE_API FShapeSample EvaluateRadialShape(ERadialShape Shape, const FRadialShapeParams& Params, const FVector2D& OffsetCm);

	struct FLineShapeParams
	{
		FVector2D StartCm = FVector2D::ZeroVector;
		FVector2D EndCm = FVector2D::ZeroVector;
		double HalfWidthCm = 5000.0;
		double HeightCm = 5000.0;		// ridge height or valley depth (positive)
		double NoiseAmount = 0.3;
		int32 Seed = 1;
	};

	/** Evaluates a valley or ridge along a segment. OutAlong is the 0..1 position along the segment (for the reference ground). */
	AIWORLDBUILDERCORE_API FShapeSample EvaluateLineShape(ELineShape Shape, const FLineShapeParams& Params, const FVector2D& PointCm, double& OutAlong);

	// ---------------------------------------------------------------- geometry helpers

	/**
	 * Distance from P to a polyline. OutAlong is the distance along the polyline (cm) of the closest point,
	 * so callers can interpolate per-vertex values.
	 */
	AIWORLDBUILDERCORE_API double DistanceToPolyline(const FVector2D& P, TConstArrayView<FVector2D> Points, double& OutAlong);

	/** Cumulative length at each polyline vertex (first is 0). */
	AIWORLDBUILDERCORE_API TArray<double> PolylineCumulativeLengths(TConstArrayView<FVector2D> Points);

	/**
	 * In-place separable box blur over a Width x Height grid, repeated Passes times (3 passes approximates a Gaussian).
	 * Edges clamp. RadiusSamples is the box half-width in samples.
	 */
	AIWORLDBUILDERCORE_API void BoxBlur(TArray<double>& Grid, int32 Width, int32 Height, int32 RadiusSamples, int32 Passes);

	// ---------------------------------------------------------------- preview colours

	/** Slope colour ramp: green (flat) > yellow-green > yellow > orange > red > purple (55 degrees and steeper). */
	AIWORLDBUILDERCORE_API FColor SlopeToColor(double SlopeDegrees);

	/** Human-readable legend matching SlopeToColor. */
	AIWORLDBUILDERCORE_API FString SlopeLegend();

	/** Height mapped linearly to grey: Min = black, Max = white. */
	AIWORLDBUILDERCORE_API FColor HeightToGrey(double Height, double Min, double Max);
}
