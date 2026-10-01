#include "WorldCaptureTools.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AIWorldBuilderTerrainMath.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Landscape.h"
#include "LevelEditorViewport.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UnrealClient.h"

using namespace AIWorldBuilder;
namespace TM = AIWorldBuilder::TerrainMath;

namespace
{
	/** Frames rendered before reading the image, so temporal effects (AA, exposure, lighting) settle after the camera jump. */
	constexpr int32 WarmupFrames = 4;
	constexpr int32 MinImagePx = 64;
	constexpr int32 MaxImagePx = 2048;
	/** Cameras placed by the tools stay at least this far above the terrain (cm). */
	constexpr double MinCameraClearanceCm = 500.0;

	FString CaptureDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AIWorldBuilder") / TEXT("Captures"));
	}

	/** Unique, sortable file path: <dir>/<Prefix>_<yyyymmdd_hhmmss_ms>[_NN].png */
	FString MakeCapturePath(const FString& Prefix, int32 Index = INDEX_NONE)
	{
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s"));
		const FString Suffix = Index == INDEX_NONE ? FString() : FString::Printf(TEXT("_%02d"), Index);
		return CaptureDirectory() / FString::Printf(TEXT("%s_%s%s.png"), *Prefix, *Stamp, *Suffix);
	}

	bool SavePng(const FString& Path, const TArray<FColor>& Pixels, int32 Width, int32 Height, FString& OutError)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		if (!FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), Width, Height)))
		{
			OutError = FString::Printf(TEXT("Failed to write %s."), *Path);
			return false;
		}
		return true;
	}

	/** The perspective level viewport the user is working in. */
	FLevelEditorViewportClient* FindViewportClient(FString& OutError)
	{
		FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient;
		if ((!Client || !Client->IsPerspective()) && GEditor)
		{
			for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
			{
				if (Candidate && Candidate->Viewport && Candidate->IsPerspective())
				{
					Client = Candidate;
					break;
				}
			}
		}
		if (!Client || !Client->Viewport)
		{
			OutError = TEXT("No level viewport is available to render from. Open a level viewport and try again.");
			return nullptr;
		}
		if (!Client->IsPerspective())
		{
			OutError = TEXT("The level viewports are all orthographic. Switch one to Perspective and try again.");
			return nullptr;
		}
		return Client;
	}

	/**
	 * Renders the level viewport from a given camera and returns Width x Height pixels.
	 * The viewport is cropped to the requested aspect ratio and its FOV widened so the crop has FovDeg horizontally.
	 * Camera, FOV and editor overlays (gizmos, selection) are restored afterwards.
	 */
	bool RenderViewport(const FVector& LocationCm, const FRotator& Rotation, double FovDeg, int32 Width, int32 Height,
		TArray<FColor>& OutPixels, FString& OutError)
	{
		if (GEditor && GEditor->PlayWorld)
		{
			OutError = TEXT("Stop Play-In-Editor before capturing.");
			return false;
		}
		FLevelEditorViewportClient* Client = FindViewportClient(OutError);
		if (!Client)
		{
			return false;
		}

		FViewport* Viewport = Client->Viewport;
		const FIntPoint Size = Viewport->GetSizeXY();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			OutError = TEXT("The level viewport has zero size (is it hidden or minimized?).");
			return false;
		}

		// Crop the viewport to the requested aspect, centred.
		const double TargetAspect = double(Width) / Height;
		const double ViewportAspect = double(Size.X) / Size.Y;
		int32 CropW = Size.X, CropH = Size.Y;
		if (ViewportAspect > TargetAspect)
		{
			CropW = FMath::Clamp(FMath::RoundToInt32(Size.Y * TargetAspect), 1, Size.X);
		}
		else
		{
			CropH = FMath::Clamp(FMath::RoundToInt32(Size.X / TargetAspect), 1, Size.Y);
		}
		const FIntRect CropRect(FIntPoint((Size.X - CropW) / 2, (Size.Y - CropH) / 2), FIntPoint((Size.X - CropW) / 2 + CropW, (Size.Y - CropH) / 2 + CropH));

		// The viewport FOV is horizontal across its full width; widen it so the cropped width spans FovDeg.
		const double HalfFov = FMath::DegreesToRadians(FovDeg * 0.5);
		const double ViewportFov = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfFov) * double(Size.X) / CropW));

		const FVector SavedLocation = Client->GetViewLocation();
		const FRotator SavedRotation = Client->GetViewRotation();
		const float SavedFov = Client->ViewFOV;
		const bool bSavedModeWidgets = Client->EngineShowFlags.ModeWidgets != 0;
		const bool bSavedSelectionOutline = Client->EngineShowFlags.SelectionOutline != 0;
		const bool bSavedSelection = Client->EngineShowFlags.Selection != 0;

		ON_SCOPE_EXIT
		{
			Client->SetViewLocation(SavedLocation);
			Client->SetViewRotation(SavedRotation);
			Client->ViewFOV = SavedFov;
			Client->EngineShowFlags.SetModeWidgets(bSavedModeWidgets);
			Client->EngineShowFlags.SetSelectionOutline(bSavedSelectionOutline);
			Client->EngineShowFlags.SetSelection(bSavedSelection);
			Client->Invalidate();
		};

		Client->SetViewLocation(LocationCm);
		Client->SetViewRotation(Rotation);
		Client->ViewFOV = float(FMath::Clamp(ViewportFov, 5.0, 170.0));
		Client->EngineShowFlags.SetModeWidgets(false);
		Client->EngineShowFlags.SetSelectionOutline(false);
		Client->EngineShowFlags.SetSelection(false);

		for (int32 Frame = 0; Frame < WarmupFrames; ++Frame)
		{
			Client->Invalidate();
			Viewport->Draw();
			FlushRenderingCommands();
		}

		TArray<FColor> Cropped;
		if (!GetViewportScreenShot(Viewport, Cropped, CropRect) || Cropped.Num() != CropW * CropH)
		{
			OutError = TEXT("Failed to read the viewport image.");
			return false;
		}
		for (FColor& Pixel : Cropped)
		{
			Pixel.A = 255;
		}

		if (CropW == Width && CropH == Height)
		{
			OutPixels = MoveTemp(Cropped);
		}
		else
		{
			OutPixels.SetNumUninitialized(Width * Height);
			FImageUtils::ImageResize(CropW, CropH, Cropped, Width, Height, OutPixels, /*bResizeSRGBinLinearSpace=*/false);
		}
		return true;
	}

	/** Renders one view and appends it to Result. */
	bool CaptureOne(const FString& Prefix, int32 Index, const FVector& LocationCm, const FRotator& Rotation, double FovDeg,
		int32 Width, int32 Height, FWorldBuilderCaptureResult& Result)
	{
		TArray<FColor> Pixels;
		FString Error;
		if (!RenderViewport(LocationCm, Rotation, FovDeg, Width, Height, Pixels, Error))
		{
			Result.Message = Error;
			return false;
		}
		const FString Path = MakeCapturePath(Prefix, Index);
		if (!SavePng(Path, Pixels, Width, Height, Error))
		{
			Result.Message = Error;
			return false;
		}

		FWorldBuilderCaptureImage& Image = Result.Images.AddDefaulted_GetRef();
		Image.FilePath = Path;
		Image.WidthPx = Width;
		Image.HeightPx = Height;
		Image.CameraLocationM = LocationCm / CmPerMeter;
		Image.PitchDeg = Rotation.Pitch;
		Image.YawDeg = Rotation.Yaw;
		Image.FovDeg = FovDeg;
		return true;
	}

	FString DescribeImages(const FWorldBuilderCaptureResult& Result)
	{
		TArray<FString> Paths;
		for (const FWorldBuilderCaptureImage& Image : Result.Images)
		{
			Paths.Add(Image.FilePath);
		}
		return FString::Printf(TEXT("Saved %d image(s); open them to view: %s"), Paths.Num(), *FString::Join(Paths, TEXT(", ")));
	}

	/**
	 * Ground height (cm) at a point, or 0 if there is no landscape there. Fails only if a landscape was named explicitly and not found.
	 */
	bool GroundHeightCm(const FString& LandscapeName, const FVector2D& PointCm, double& OutHeightCm, FString& OutError)
	{
		OutHeightCm = 0.0;
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			OutError = TEXT("No level is open in the editor.");
			return false;
		}
		FString ResolveError;
		const ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, &PointCm, ResolveError);
		const bool bExplicitName = !LandscapeName.IsEmpty() && !LandscapeName.Equals(LandscapeUtils::AutoLandscapeName, ESearchCase::IgnoreCase);
		if (!Landscape)
		{
			if (bExplicitName)
			{
				OutError = ResolveError;
				return false;
			}
			return true;
		}
		if (const TOptional<double> H = LandscapeUtils::SampleHeightCm(Landscape, PointCm))
		{
			OutHeightCm = H.GetValue();
		}
		return true;
	}

	bool ValidateImageSize(int32 Width, int32 Height, FString& OutError)
	{
		if (Width < MinImagePx || Width > MaxImagePx || Height < MinImagePx || Height > MaxImagePx)
		{
			OutError = FString::Printf(TEXT("Image size must be between %d and %d pixels per side (got %d x %d)."), MinImagePx, MaxImagePx, Width, Height);
			return false;
		}
		return true;
	}

	bool ValidateFov(double FovDeg, double MaxFov, FString& OutError)
	{
		if (FovDeg < 10.0 || FovDeg > MaxFov)
		{
			OutError = FString::Printf(TEXT("FovDeg must be between 10 and %.0f (got %.1f)."), MaxFov, FovDeg);
			return false;
		}
		return true;
	}
}

FString UWorldCaptureTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderCaptureResult UWorldCaptureTools::CaptureView(double CameraXM, double CameraYM, double CameraZM, double PitchDeg, double YawDeg,
	double FovDeg, int32 WidthPx, int32 HeightPx)
{
	FWorldBuilderCaptureResult Result;
	if (!ValidateImageSize(WidthPx, HeightPx, Result.Message) || !ValidateFov(FovDeg, 120.0, Result.Message))
	{
		return Result;
	}
	const FVector LocationCm = FVector(CameraXM, CameraYM, CameraZM) * CmPerMeter;
	const FRotator Rotation(FMath::Clamp(PitchDeg, -89.9, 89.9), YawDeg, 0.0);
	if (CaptureOne(TEXT("View"), INDEX_NONE, LocationCm, Rotation, FovDeg, WidthPx, HeightPx, Result))
	{
		Result.bSuccess = true;
		Result.Message = DescribeImages(Result);
	}
	return Result;
}

FWorldBuilderCaptureResult UWorldCaptureTools::CaptureLookAt(double CameraXM, double CameraYM, double CameraZM, double TargetXM, double TargetYM, double TargetZM,
	double FovDeg, int32 WidthPx, int32 HeightPx)
{
	FWorldBuilderCaptureResult Result;
	if (!ValidateImageSize(WidthPx, HeightPx, Result.Message) || !ValidateFov(FovDeg, 120.0, Result.Message))
	{
		return Result;
	}
	const FVector CameraCm = FVector(CameraXM, CameraYM, CameraZM) * CmPerMeter;
	const FVector TargetCm = FVector(TargetXM, TargetYM, TargetZM) * CmPerMeter;
	if (CameraCm.Equals(TargetCm, 1.0))
	{
		Result.Message = TEXT("Camera and target are at the same point.");
		return Result;
	}
	FRotator Rotation = (TargetCm - CameraCm).Rotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -89.9, 89.9);
	if (CaptureOne(TEXT("LookAt"), INDEX_NONE, CameraCm, Rotation, FovDeg, WidthPx, HeightPx, Result))
	{
		Result.bSuccess = true;
		Result.Message = DescribeImages(Result);
	}
	return Result;
}

FWorldBuilderCaptureResult UWorldCaptureTools::CaptureOrbit(double TargetXM, double TargetYM, double DistanceM, int32 Count,
	double ElevationDeg, double TargetHeightAboveGroundM, double StartYawDeg, double FovDeg, int32 WidthPx, int32 HeightPx, const FString& LandscapeName)
{
	FWorldBuilderCaptureResult Result;
	if (!ValidateImageSize(WidthPx, HeightPx, Result.Message) || !ValidateFov(FovDeg, 120.0, Result.Message))
	{
		return Result;
	}
	if (DistanceM <= 0.0)
	{
		Result.Message = TEXT("DistanceM must be greater than 0.");
		return Result;
	}
	if (Count < 1 || Count > 12)
	{
		Result.Message = FString::Printf(TEXT("Count must be between 1 and 12 (got %d)."), Count);
		return Result;
	}

	const FVector2D TargetXY = FVector2D(TargetXM, TargetYM) * CmPerMeter;
	double GroundCm = 0.0;
	if (!GroundHeightCm(LandscapeName, TargetXY, GroundCm, Result.Message))
	{
		return Result;
	}
	const FVector TargetCm(TargetXY.X, TargetXY.Y, GroundCm + MetersToCm(TargetHeightAboveGroundM));
	const double Elevation = FMath::DegreesToRadians(FMath::Clamp(ElevationDeg, 5.0, 89.0));
	const double DistanceCm = MetersToCm(DistanceM);

	TArray<FString> Notes;
	for (int32 I = 0; I < Count; ++I)
	{
		const double Yaw = FMath::DegreesToRadians(StartYawDeg + 360.0 * I / Count);
		FVector CameraCm = TargetCm + DistanceCm * FVector(FMath::Cos(Elevation) * FMath::Cos(Yaw), FMath::Cos(Elevation) * FMath::Sin(Yaw), FMath::Sin(Elevation));

		// Keep the camera out of the terrain.
		double CameraGroundCm = 0.0;
		FString Ignored;
		GroundHeightCm(LandscapeName, FVector2D(CameraCm.X, CameraCm.Y), CameraGroundCm, Ignored);
		if (CameraCm.Z < CameraGroundCm + MinCameraClearanceCm)
		{
			CameraCm.Z = CameraGroundCm + MinCameraClearanceCm;
			Notes.Add(FString::Printf(TEXT("view %d raised above terrain"), I));
		}

		FRotator Rotation = (TargetCm - CameraCm).Rotation();
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -89.9, 89.9);
		if (!CaptureOne(TEXT("Orbit"), I, CameraCm, Rotation, FovDeg, WidthPx, HeightPx, Result))
		{
			return Result;
		}
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Orbit around (%.0f, %.0f, %.0f) m at %.0f m distance. %s%s"),
		TargetCm.X / CmPerMeter, TargetCm.Y / CmPerMeter, TargetCm.Z / CmPerMeter, DistanceM, *DescribeImages(Result),
		Notes.IsEmpty() ? TEXT("") : *(TEXT(" Note: ") + FString::Join(Notes, TEXT("; ")) + TEXT(".")));
	return Result;
}

FWorldBuilderCaptureResult UWorldCaptureTools::CaptureTopDown(double CenterXM, double CenterYM, double SizeM, int32 SizePx, double FovDeg, const FString& LandscapeName)
{
	FWorldBuilderCaptureResult Result;
	if (!ValidateImageSize(SizePx, SizePx, Result.Message) || !ValidateFov(FovDeg, 90.0, Result.Message))
	{
		return Result;
	}
	if (SizeM <= 0.0)
	{
		Result.Message = TEXT("SizeM must be greater than 0.");
		return Result;
	}

	// Highest ground in the area (coarse 16 x 16 scan) so the whole square is framed even at that height.
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	const double HalfCm = MetersToCm(SizeM) * 0.5;
	double TopCm = TNumericLimits<double>::Lowest();
	for (int32 IY = 0; IY < 16; ++IY)
	{
		for (int32 IX = 0; IX < 16; ++IX)
		{
			const FVector2D P = CenterCm + FVector2D(-HalfCm + 2.0 * HalfCm * IX / 15.0, -HalfCm + 2.0 * HalfCm * IY / 15.0);
			double H = 0.0;
			if (!GroundHeightCm(LandscapeName, P, H, Result.Message))
			{
				return Result;
			}
			TopCm = FMath::Max(TopCm, H);
		}
	}

	const double CameraHeightCm = TopCm + HalfCm / FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5));
	const FVector CameraCm(CenterCm.X, CenterCm.Y, CameraHeightCm);
	// Pitch -90 with yaw 0 puts +X at the top of the image and +Y at the right.
	const FRotator Rotation(-89.9, 0.0, 0.0);
	if (CaptureOne(TEXT("TopDown"), INDEX_NONE, CameraCm, Rotation, FovDeg, SizePx, SizePx, Result))
	{
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Top-down view of a %.0f m square centred on (%.0f, %.0f) m from %.0f m height. Image top = +X, right = +Y. %s"),
			SizeM, CenterXM, CenterYM, CmToMeters(CameraHeightCm), *DescribeImages(Result));
	}
	return Result;
}

FWorldBuilderHeightPreviewResult UWorldCaptureTools::ExportHeightPreview(double CenterXM, double CenterYM, double SizeM, int32 WidthPx, const FString& LandscapeName)
{
	FWorldBuilderHeightPreviewResult Result;
	Result.SlopeLegend = TM::SlopeLegend();
	if (WidthPx < MinImagePx || WidthPx > MaxImagePx)
	{
		Result.Message = FString::Printf(TEXT("WidthPx must be between %d and %d."), MinImagePx, MaxImagePx);
		return Result;
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	const bool bWhole = SizeM <= 0.0;
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, bWhole ? nullptr : &CenterCm, Result.Message);
	if (!Landscape)
	{
		return Result;
	}
	Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);

	FVector2D MinCm, MaxCm;
	if (bWhole)
	{
		const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
		MinCm = FVector2D(Bounds.Min.X, Bounds.Min.Y);
		MaxCm = FVector2D(Bounds.Max.X, Bounds.Max.Y);
	}
	else
	{
		const double HalfCm = MetersToCm(SizeM) * 0.5;
		MinCm = CenterCm - FVector2D(HalfCm);
		MaxCm = CenterCm + FVector2D(HalfCm);
	}
	Result.RegionMinM = MinCm / CmPerMeter;
	Result.RegionMaxM = MaxCm / CmPerMeter;

	// Image right = +Y (width), image top = +X (height), matching the top-down camera.
	const double SizeXCm = MaxCm.X - MinCm.X, SizeYCm = MaxCm.Y - MinCm.Y;
	int32 Width = WidthPx;
	int32 Height = FMath::Max(1, FMath::RoundToInt32(Width * SizeXCm / SizeYCm));
	if (Height > MaxImagePx)
	{
		Width = FMath::Max(1, FMath::RoundToInt32(Width * double(MaxImagePx) / Height));
		Height = MaxImagePx;
	}
	const double StepCm = SizeYCm / Width;
	Result.WidthPx = Width;
	Result.HeightPx = Height;
	Result.MetersPerPixel = CmToMeters(StepCm);

	// Heights and true local slopes (neighbours one landscape sample away).
	const double SpacingCm = LandscapeUtils::GetSampleSpacingCm(Landscape);
	TArray<double> Heights;
	TArray<double> Slopes;
	TArray<bool> Valid;
	Heights.SetNumZeroed(Width * Height);
	Slopes.SetNumZeroed(Width * Height);
	Valid.SetNumZeroed(Width * Height);
	double MinH = TNumericLimits<double>::Max(), MaxH = TNumericLimits<double>::Lowest();

	for (int32 Row = 0; Row < Height; ++Row)
	{
		const double X = MaxCm.X - (Row + 0.5) * (SizeXCm / Height);
		for (int32 Col = 0; Col < Width; ++Col)
		{
			const double Y = MinCm.Y + (Col + 0.5) * StepCm;
			const FVector2D P(X, Y);
			const int32 I = Row * Width + Col;
			const TOptional<double> H = LandscapeUtils::SampleHeightCm(Landscape, P);
			if (!H.IsSet())
			{
				++Result.MissingPixelCount;
				continue;
			}
			Valid[I] = true;
			Heights[I] = H.GetValue();
			MinH = FMath::Min(MinH, Heights[I]);
			MaxH = FMath::Max(MaxH, Heights[I]);

			const TOptional<double> XP = LandscapeUtils::SampleHeightCm(Landscape, P + FVector2D(SpacingCm, 0.0));
			const TOptional<double> XN = LandscapeUtils::SampleHeightCm(Landscape, P - FVector2D(SpacingCm, 0.0));
			const TOptional<double> YP = LandscapeUtils::SampleHeightCm(Landscape, P + FVector2D(0.0, SpacingCm));
			const TOptional<double> YN = LandscapeUtils::SampleHeightCm(Landscape, P - FVector2D(0.0, SpacingCm));
			const double DzDx = (XP.IsSet() && XN.IsSet()) ? (XP.GetValue() - XN.GetValue()) / (2.0 * SpacingCm) : 0.0;
			const double DzDy = (YP.IsSet() && YN.IsSet()) ? (YP.GetValue() - YN.GetValue()) / (2.0 * SpacingCm) : 0.0;
			Slopes[I] = LandscapeMath::SlopeDegrees(DzDx, DzDy);
		}
	}

	if (Result.MissingPixelCount == Width * Height)
	{
		Result.Message = TEXT("No landscape data in that area (outside the landscape or in an unloaded World Partition region).");
		return Result;
	}
	Result.MinHeightM = CmToMeters(MinH);
	Result.MaxHeightM = CmToMeters(MaxH);

	const FColor Missing(255, 0, 255, 255);
	TArray<FColor> HeightPixels, SlopePixels;
	HeightPixels.SetNumUninitialized(Width * Height);
	SlopePixels.SetNumUninitialized(Width * Height);
	for (int32 I = 0; I < Width * Height; ++I)
	{
		HeightPixels[I] = Valid[I] ? TM::HeightToGrey(Heights[I], MinH, MaxH) : Missing;
		SlopePixels[I] = Valid[I] ? TM::SlopeToColor(Slopes[I]) : Missing;
	}

	Result.HeightMapPath = MakeCapturePath(TEXT("HeightMap"));
	Result.SlopeMapPath = MakeCapturePath(TEXT("SlopeMap"));
	if (!SavePng(Result.HeightMapPath, HeightPixels, Width, Height, Result.Message) || !SavePng(Result.SlopeMapPath, SlopePixels, Width, Height, Result.Message))
	{
		return Result;
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Height and slope maps of '%s', %d x %d px at %.2f m/px covering (%.0f, %.0f) to (%.0f, %.0f) m. Image top = +X, right = +Y. Height: black = %.1f m, white = %.1f m. Slope: %s. Open: %s and %s"),
		*Result.LandscapeName, Width, Height, Result.MetersPerPixel,
		Result.RegionMinM.X, Result.RegionMinM.Y, Result.RegionMaxM.X, Result.RegionMaxM.Y,
		Result.MinHeightM, Result.MaxHeightM, *Result.SlopeLegend, *Result.HeightMapPath, *Result.SlopeMapPath);
	if (Result.MissingPixelCount > 0)
	{
		Result.Message += FString::Printf(TEXT(" %d pixel(s) had no data (magenta)."), Result.MissingPixelCount);
	}
	return Result;
}
