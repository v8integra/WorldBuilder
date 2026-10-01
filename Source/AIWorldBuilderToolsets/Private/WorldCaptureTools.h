#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "WorldCaptureTools.generated.h"

/// One saved image.
USTRUCT(BlueprintType)
struct FWorldBuilderCaptureImage
{
	GENERATED_BODY()

	/// Absolute path of the PNG on disk. Open this file to look at the image.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString FilePath;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 WidthPx = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 HeightPx = 0;

	/// Camera position in meters.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector CameraLocationM = FVector::ZeroVector;

	/// Camera pitch in degrees (negative looks down).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double PitchDeg = 0.0;

	/// Camera yaw in degrees (0 looks along +X, 90 along +Y).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double YawDeg = 0.0;

	/// Horizontal field of view in degrees.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double FovDeg = 0.0;
};

/// Result of the camera capture tools.
USTRUCT(BlueprintType)
struct FWorldBuilderCaptureResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// Saved images, in capture order.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderCaptureImage> Images;
};

/// Result of ExportHeightPreview.
USTRUCT(BlueprintType)
struct FWorldBuilderHeightPreviewResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LandscapeName;

	/// Grayscale height map PNG (black = MinHeightM, white = MaxHeightM).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString HeightMapPath;

	/// Coloured slope map PNG (see SlopeLegend).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString SlopeMapPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 WidthPx = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 HeightPx = 0;

	/// Area covered, minimum corner (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMinM = FVector2D::ZeroVector;

	/// Area covered, maximum corner (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FVector2D RegionMaxM = FVector2D::ZeroVector;

	/// Ground distance per pixel (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MetersPerPixel = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MinHeightM = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	double MaxHeightM = 0.0;

	/// Pixels with no landscape data (shown magenta).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 MissingPixelCount = 0;

	/// Slope colour legend.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString SlopeLegend;
};

/// Eyes for the AI: render views of the level and terrain maps to PNG files, then open the files to look at the result. Use after sculpting or painting to check the work. Images are saved under <Project>/Saved/AIWorldBuilder/Captures/. Units: meters and degrees, world space. In top-down images, image top = +X and image right = +Y.
UCLASS(BlueprintType, Hidden)
class UWorldCaptureTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Renders the level from a camera position and angle, through the editor viewport (same lighting as the editor), and saves a PNG.
	 * The user's viewport camera is restored afterwards.
	 * Example: CaptureView(-2000, 0, 400, -10, 0) looks along +X from 2 km west, slightly downward.
	 * @param CameraXM Camera X in meters.
	 * @param CameraYM Camera Y in meters.
	 * @param CameraZM Camera Z (height) in meters.
	 * @param PitchDeg Pitch in degrees: 0 = horizontal, negative looks down, -90 straight down.
	 * @param YawDeg Yaw in degrees: 0 looks along +X, 90 along +Y.
	 * @param FovDeg Horizontal field of view, 10-120 degrees.
	 * @param WidthPx Image width, 64-2048.
	 * @param HeightPx Image height, 64-2048.
	 * @return The saved image path and camera.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Capture")
	static FWorldBuilderCaptureResult CaptureView(double CameraXM, double CameraYM, double CameraZM, double PitchDeg, double YawDeg,
		double FovDeg = 60.0, int32 WidthPx = 1280, int32 HeightPx = 720);

	/**
	 * Renders the level from a camera position looking at a target point, and saves a PNG. Easier than CaptureView when you know what to look at.
	 * Example: CaptureLookAt(-1500, -1500, 600, 0, 0, 100) views the map centre from the south-west.
	 * @param CameraXM Camera X in meters.
	 * @param CameraYM Camera Y in meters.
	 * @param CameraZM Camera Z in meters.
	 * @param TargetXM Target X in meters.
	 * @param TargetYM Target Y in meters.
	 * @param TargetZM Target Z in meters.
	 * @param FovDeg Horizontal field of view, 10-120 degrees.
	 * @param WidthPx Image width, 64-2048.
	 * @param HeightPx Image height, 64-2048.
	 * @return The saved image path and camera.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Capture")
	static FWorldBuilderCaptureResult CaptureLookAt(double CameraXM, double CameraYM, double CameraZM, double TargetXM, double TargetYM, double TargetZM,
		double FovDeg = 60.0, int32 WidthPx = 1280, int32 HeightPx = 720);

	/**
	 * Renders Count views circling a target point on the ground, all looking at it. Ideal for checking a sculpted feature from every side.
	 * Cameras are kept at least 5 m above the terrain.
	 * Example: CaptureOrbit(0, 0, 2500, 4) takes 4 views of the map centre from 2.5 km away at 30 degrees elevation.
	 * @param TargetXM Target X in meters.
	 * @param TargetYM Target Y in meters.
	 * @param DistanceM Camera distance from the target in meters.
	 * @param Count Number of views, 1-12, evenly spaced around the target.
	 * @param ElevationDeg Camera elevation above the horizon in degrees (5-89).
	 * @param TargetHeightAboveGroundM Aim this many meters above the ground at the target (e.g. half a mountain's height).
	 * @param StartYawDeg Yaw of the first camera position around the target, degrees (0 = camera on the +X side).
	 * @param FovDeg Horizontal field of view, 10-120 degrees.
	 * @param WidthPx Image width, 64-2048.
	 * @param HeightPx Image height, 64-2048.
	 * @param LandscapeName Landscape used to find ground height, or "auto" (default).
	 * @return One saved image per view.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Capture")
	static FWorldBuilderCaptureResult CaptureOrbit(double TargetXM, double TargetYM, double DistanceM, int32 Count = 4,
		double ElevationDeg = 30.0, double TargetHeightAboveGroundM = 0.0, double StartYawDeg = 45.0,
		double FovDeg = 60.0, int32 WidthPx = 1280, int32 HeightPx = 720, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Renders a square area straight down from above (perspective camera placed high enough to frame the area at its highest point).
	 * Image top = +X, image right = +Y. For exact height/slope data use ExportHeightPreview.
	 * Example: CaptureTopDown(0, 0, 2000) frames a 2 km square centred on the origin.
	 * @param CenterXM Area centre X in meters.
	 * @param CenterYM Area centre Y in meters.
	 * @param SizeM Width of the square area in meters.
	 * @param SizePx Image width and height, 64-2048.
	 * @param FovDeg Field of view, 10-90 degrees; smaller looks flatter (closer to orthographic) but places the camera higher.
	 * @param LandscapeName Landscape used to find terrain height, or "auto" (default).
	 * @return The saved image.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Capture")
	static FWorldBuilderCaptureResult CaptureTopDown(double CenterXM, double CenterYM, double SizeM, int32 SizePx = 1024,
		double FovDeg = 30.0, const FString& LandscapeName = TEXT("auto"));

	/**
	 * Draws the terrain itself (not a render) as two PNGs: a grayscale height map and a coloured slope map
	 * (green flat, yellow moderate, orange/red steep, purple cliffs). Exact, lighting-independent, top-down.
	 * Image top = +X, image right = +Y. Leave SizeM at 0 for the whole landscape.
	 * Example: ExportHeightPreview(0, 0, 0, 1024) maps the whole landscape.
	 * @param CenterXM Area centre X in meters (ignored when SizeM is 0).
	 * @param CenterYM Area centre Y in meters (ignored when SizeM is 0).
	 * @param SizeM Width of the square area in meters; 0 = whole landscape.
	 * @param WidthPx Image width in pixels (along Y), 64-2048; height follows the area's proportions.
	 * @param LandscapeName Landscape label, or "auto" (default).
	 * @return Paths of both images, height range and slope legend.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Capture")
	static FWorldBuilderHeightPreviewResult ExportHeightPreview(double CenterXM = 0.0, double CenterYM = 0.0, double SizeM = 0.0,
		int32 WidthPx = 1024, const FString& LandscapeName = TEXT("auto"));
};
