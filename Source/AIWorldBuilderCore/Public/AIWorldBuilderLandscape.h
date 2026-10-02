#pragma once

#include "CoreMinimal.h"

class ALandscape;
class UWorld;

namespace AIWorldBuilder
{
	/** Tools take meters; Unreal works in centimeters. */
	inline constexpr double CmPerMeter = 100.0;
	inline double MetersToCm(double Meters) { return Meters * CmPerMeter; }
	inline double CmToMeters(double Cm) { return Cm / CmPerMeter; }

	/**
	 * Landscape height encoding. Heights are stored as uint16 where 32768 is zero and one stored unit
	 * is 1/128 of a local unit; local units are scaled by the landscape actor's Z scale (cm).
	 *   WorldZ(cm) = (Value - 32768) * ScaleZ / 128 + ActorZ
	 * Assumes the landscape has no pitch or roll (Unreal landscapes only support yaw in practice).
	 */
	namespace LandscapeMath
	{
		AIWORLDBUILDERCORE_API double HeightValueToLocalZ(uint16 Value);
		AIWORLDBUILDERCORE_API uint16 LocalZToHeightValue(double LocalZ);
		AIWORLDBUILDERCORE_API double HeightValueToWorldZCm(uint16 Value, double ScaleZ, double ActorZCm);
		AIWORLDBUILDERCORE_API uint16 WorldZCmToHeightValue(double WorldZCm, double ScaleZ, double ActorZCm);

		/** The lowest and highest world Z (cm) this landscape can represent. */
		AIWORLDBUILDERCORE_API void GetHeightRangeCm(double ScaleZ, double ActorZCm, double& OutMinCm, double& OutMaxCm);

		/** Slope in degrees from height differences (cm) across a horizontal distance (cm) on each axis. */
		AIWORLDBUILDERCORE_API double SlopeDegrees(double DzDxCm, double DzDyCm);
	}

	namespace LandscapeUtils
	{
		/** All Landscape actors (not streaming proxies) in the world. */
		AIWORLDBUILDERCORE_API TArray<ALandscape*> GetAllLandscapes(UWorld* World);

		/** The editor label shown in the Outliner. */
		AIWORLDBUILDERCORE_API FString GetDisplayName(const ALandscape* Landscape);

		/**
		 * Default for tools' LandscapeName parameter. The Toolset Registry treats an empty-string default as
		 * "no default", which makes the argument mandatory, so optional names default to this instead.
		 */
		inline const TCHAR* AutoLandscapeName = TEXT("auto");

		/**
		 * Resolves which landscape a tool should act on.
		 * - Name given (not empty and not "auto"): match by Outliner label or object name (case-insensitive).
		 * - Else, if a point is given: the landscape whose full bounds contain it.
		 * - Else: the only landscape in the level.
		 * Returns nullptr and fills OutError when nothing matches or the choice is ambiguous.
		 */
		AIWORLDBUILDERCORE_API ALandscape* Resolve(UWorld* World, const FString& Name, const FVector2D* PointCm, FString& OutError);

		/**
		 * Full world bounds (cm): saved World Partition proxies (loaded or not) plus everything currently loaded,
		 * so landscapes created or extended since the last save are included. Always use this, not ULandscapeInfo::GetCompleteBounds.
		 */
		AIWORLDBUILDERCORE_API FBox GetCompleteBounds(const ALandscape* Landscape);

		/**
		 * Full extent in landscape sample coordinates (inclusive Min..Max vertices), with the same saved + loaded rule.
		 * Invalid (Min > Max) if the landscape has no components. Always use this, not ULandscapeInfo::GetCompleteLandscapeExtent.
		 */
		AIWORLDBUILDERCORE_API FIntRect GetCompleteExtent(const ALandscape* Landscape);

		/** Horizontal distance between height samples (cm). */
		AIWORLDBUILDERCORE_API double GetSampleSpacingCm(const ALandscape* Landscape);

		/**
		 * Ground height (world Z, cm) at a world XY, read from the landscape's collision heightfield
		 * (full-resolution editor heightfield when available, else complex collision). Read-only.
		 * Unset if that part of the landscape is not loaded or has no collision.
		 */
		AIWORLDBUILDERCORE_API TOptional<double> SampleHeightCm(const ALandscape* Landscape, const FVector2D& PointCm);
	}
}
