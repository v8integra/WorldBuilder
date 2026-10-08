#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "WaterTools.generated.h"

/// One water body in the level.
USTRUCT(BlueprintType)
struct FWorldBuilderWaterBodyInfo
{
	GENERATED_BODY()

	/// Outliner label; pass it to RemoveWaterBody.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Label;

	/// Ocean, Lake, River or Custom.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Type;

	/// Actor location (m). For oceans and lakes, Z is the water level.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector LocationM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMinM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector BoundsMaxM = FVector::ZeroVector;

	/// Spline points defining the shape (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 SplinePointCount = 0;

	/// False for everything these tools create: the terrain is shaped by the sculpt tools, not by water brushes.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bAffectsLandscape = false;
};

/// Result of the water tools.
USTRUCT(BlueprintType)
struct FWorldBuilderWaterResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// Water bodies created (or listed).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderWaterBodyInfo> WaterBodies;
};

/// Add water to the world: oceans, lakes and ponds, rivers and streams, waterfalls with plunge pools, and custom water surfaces for pools, fountains, hot springs or underground/cave water. Uses Epic's Water plugin with the project's water materials, but never lets water bodies deform the landscape (that path crashes the editor): shape the ground first with the sculpt tools (CarvePath for river beds, Flatten/ApplyShape Crater for lake basins), then add the water surface here. Use these tools instead of spawning water actors directly. Units: meters, world space. Save all afterwards.
UCLASS(BlueprintType, Hidden)
class UWaterTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Adds an ocean at sea level filling the water zone around the landscape (terrain above sea level stays dry: islands and coasts).
	 * Only one ocean per level. One undo step.
	 * Example: CreateOcean(0) puts the sea at Z = 0 m; sculpt beaches just above it.
	 * @param SeaLevelM Water surface height (world Z), meters.
	 * @param Label Outliner label, or "auto".
	 * @return The ocean.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult CreateOcean(double SeaLevelM = 0.0, const FString& Label = TEXT("auto"));

	/**
	 * Adds a lake or pond: a flat water surface at WaterLevelM inside a closed outline. Make the basin first
	 * (ApplyShape Crater, Flatten below the surroundings, or CarvePath); the outline should sit where the shore meets WaterLevelM.
	 * Give either OutlineM (3+ points) or leave it empty for a circle of RadiusM around the centre. One undo step.
	 * Example: CreateLake([], 300, -200, 120, 45) makes a 120 m radius lake at 45 m.
	 * @param OutlineM Shore outline points in meters ([] = circle).
	 * @param CenterXM Circle centre X, meters (used when OutlineM is empty).
	 * @param CenterYM Circle centre Y, meters (used when OutlineM is empty).
	 * @param RadiusM Circle radius, meters (used when OutlineM is empty).
	 * @param WaterLevelM Water surface height (world Z), meters.
	 * @param Label Outliner label, or "auto".
	 * @return The lake.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult CreateLake(const TArray<FVector2D>& OutlineM, double CenterXM, double CenterYM, double RadiusM, double WaterLevelM,
		const FString& Label = TEXT("auto"));

	/**
	 * Adds a river or stream along a path, flowing from the first point to the last. Use the same points you gave CarvePath:
	 * the water surface follows the ground under each point plus WaterDepthM. Rivers ending in a lake or the ocean blend into it.
	 * One undo step.
	 * Example: CreateRiver([{"x":-900,"y":300},{"x":-200,"y":100},{"x":600,"y":-400}], 18, 1.5) adds an 18 m wide river 1.5 m deep.
	 * @param PointsM Path points in meters, upstream first, at least 2.
	 * @param WidthM River width, meters (slightly less than the carved bed width looks best).
	 * @param WaterDepthM Water depth above the bed, meters.
	 * @param FlowSpeed Flow speed scale (1 = default; 2-4 for rapids).
	 * @param Label Outliner label, or "auto".
	 * @return The river.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult CreateRiver(const TArray<FVector2D>& PointsM, double WidthM = 15.0, double WaterDepthM = 1.5, double FlowSpeed = 1.0,
		const FString& Label = TEXT("auto"));

	/**
	 * Adds a waterfall: a steep, fast river section from the top of a drop to its foot, with a round plunge pool at the bottom.
	 * The drop itself must exist in the terrain (a cliff from ApplyShape Plateau/Mesa, a canyon wall, or RaiseLower).
	 * One undo step for both parts.
	 * Example: CreateWaterfall(100, 0, 140, 0, 10) - water pours from (100,0) down to (140,0) with a pool at the bottom.
	 * @param TopXM Lip of the fall, X meters.
	 * @param TopYM Lip of the fall, Y meters.
	 * @param BottomXM Foot of the fall, X meters.
	 * @param BottomYM Foot of the fall, Y meters.
	 * @param WidthM Width of the falling water, meters.
	 * @param PoolRadiusM Plunge pool radius, meters (0 = no pool).
	 * @param WaterDepthM Water depth, meters.
	 * @param Label Base label, or "auto".
	 * @return The waterfall river and its pool.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult CreateWaterfall(double TopXM, double TopYM, double BottomXM, double BottomYM, double WidthM = 8.0,
		double PoolRadiusM = 12.0, double WaterDepthM = 1.0, const FString& Label = TEXT("auto"));

	/**
	 * Adds a custom water surface: a flat water plane of any size at any height that does not depend on the landscape or
	 * the water zone. Use for swimming pools, fountains, hot springs, wells, moats around buildings, and underground or cave
	 * pools (caves themselves need geometry: the landscape cannot make overhangs).
	 * One undo step.
	 * Example: CreateCustomWater(50, 80, -30, 20, 12) adds a 20 x 12 m pool at Z = -30 m (e.g. in a cave).
	 * @param CenterXM Centre X, meters.
	 * @param CenterYM Centre Y, meters.
	 * @param WaterLevelM Water surface height (world Z), meters.
	 * @param SizeXM Size along X, meters.
	 * @param SizeYM Size along Y, meters.
	 * @param YawDeg Rotation around Z, degrees.
	 * @param Label Outliner label, or "auto".
	 * @return The water body.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult CreateCustomWater(double CenterXM, double CenterYM, double WaterLevelM, double SizeXM, double SizeYM,
		double YawDeg = 0.0, const FString& Label = TEXT("auto"));

	/**
	 * Lists water bodies in the level.
	 * Example: ListWaterBodies()
	 * @return The water bodies.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult ListWaterBodies();

	/**
	 * Deletes a water body by label. One undo step.
	 * Example: RemoveWaterBody("River_Main")
	 * @param Label Outliner label of the water body.
	 * @return What was removed.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Water")
	static FWorldBuilderWaterResult RemoveWaterBody(const FString& Label);
};
