#include "LandscapeInspectTools.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/MaterialInterface.h"

using namespace AIWorldBuilder;

namespace
{
	constexpr int32 MaxGridCount = 64;
	constexpr int32 MaxStatsPointsPerSide = 256;
	constexpr double FlatSlopeDeg = 5.0;
	constexpr double SteepSlopeDeg = 35.0;

	FVector CmToMetersVec(const FVector& V)
	{
		return V / CmPerMeter;
	}

	FWorldBuilderLandscapeSummary BuildSummary(ALandscape* Landscape)
	{
		FWorldBuilderLandscapeSummary S;
		S.Name = LandscapeUtils::GetDisplayName(Landscape);

		const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
		if (Bounds.IsValid)
		{
			S.BoundsMinM = CmToMetersVec(Bounds.Min);
			S.BoundsMaxM = CmToMetersVec(Bounds.Max);
			S.SizeXM = S.BoundsMaxM.X - S.BoundsMinM.X;
			S.SizeYM = S.BoundsMaxM.Y - S.BoundsMinM.Y;
		}

		S.LocationM = CmToMetersVec(Landscape->GetActorLocation());
		S.Scale = Landscape->GetActorScale3D();
		S.SampleSpacingM = CmToMeters(LandscapeUtils::GetSampleSpacingCm(Landscape));
		S.ComponentSizeQuads = Landscape->ComponentSizeQuads;
		S.SectionsPerComponent = Landscape->NumSubsections;

		double MinCm = 0.0, MaxCm = 0.0;
		LandscapeMath::GetHeightRangeCm(S.Scale.Z, Landscape->GetActorLocation().Z, MinCm, MaxCm);
		S.MinPossibleHeightM = CmToMeters(MinCm);
		S.MaxPossibleHeightM = CmToMeters(MaxCm);

		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			const FIntRect Extent = LandscapeUtils::GetCompleteExtent(Landscape);
			if (Extent.Min.X <= Extent.Max.X)
			{
				S.ResolutionX = Extent.Width() + 1;
				S.ResolutionY = Extent.Height() + 1;
				if (Info->ComponentSizeQuads > 0)
				{
					S.ComponentCount = (Extent.Width() / Info->ComponentSizeQuads) * (Extent.Height() / Info->ComponentSizeQuads);
				}
			}
			S.LoadedComponentCount = Info->XYtoComponentMap.Num();
			S.StreamingProxyCount = Info->GetSortedStreamingProxies().Num();
		}

		if (UWorld* World = Landscape->GetWorld())
		{
			S.bUsesWorldPartition = World->IsPartitionedWorld();
		}

		const FGuid EditingLayer = Landscape->GetEditingLayer();
		for (const ULandscapeEditLayerBase* Layer : Landscape->GetEditLayersConst())
		{
			if (!Layer)
			{
				continue;
			}
			FWorldBuilderEditLayerInfo L;
			L.Name = Layer->GetName().ToString();
			L.bIsEditingLayer = Layer->GetGuid() == EditingLayer;
			L.bVisible = Layer->IsVisible();
			L.bLocked = Layer->IsLocked();
			S.EditLayers.Add(L);
		}
		return S;
	}

	UWorld* RequireWorld(FString& OutError)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			OutError = TEXT("No level is open in the editor.");
		}
		return World;
	}
}

FString ULandscapeInspectTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderLandscapeListResult ULandscapeInspectTools::ListLandscapes()
{
	FWorldBuilderLandscapeListResult Result;
	UWorld* World = RequireWorld(Result.Message);
	if (!World)
	{
		return Result;
	}

	for (ALandscape* Landscape : LandscapeUtils::GetAllLandscapes(World))
	{
		Result.Landscapes.Add(BuildSummary(Landscape));
	}

	Result.bSuccess = true;
	Result.Message = Result.Landscapes.IsEmpty()
		? TEXT("The open level has no landscape.")
		: FString::Printf(TEXT("Found %d landscape(s)."), Result.Landscapes.Num());
	return Result;
}

FWorldBuilderLandscapeDetailResult ULandscapeInspectTools::GetLandscapeInfo(const FString& LandscapeName)
{
	FWorldBuilderLandscapeDetailResult Result;
	UWorld* World = RequireWorld(Result.Message);
	ALandscape* Landscape = World ? LandscapeUtils::Resolve(World, LandscapeName, nullptr, Result.Message) : nullptr;
	if (!Landscape)
	{
		return Result;
	}

	Result.Summary = BuildSummary(Landscape);
	if (Landscape->LandscapeMaterial)
	{
		Result.MaterialPath = Landscape->LandscapeMaterial->GetPathName();
	}

	// Paint layers come from two places: the landscape's target layers and the Landscape editor's
	// layer list (filled from the material). Merge them by name.
	TMap<FName, FWorldBuilderPaintLayerInfo> Layers;
	auto AddLayer = [&Layers](FName Name, const ULandscapeLayerInfoObject* LayerInfo)
	{
		if (Name.IsNone())
		{
			return;
		}
		FWorldBuilderPaintLayerInfo& L = Layers.FindOrAdd(Name);
		L.Name = Name.ToString();
		if (LayerInfo && !L.bHasLayerInfo)
		{
			L.bHasLayerInfo = true;
			L.LayerInfoPath = LayerInfo->GetPathName();
		}
	};

	for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape->GetTargetLayers())
	{
		AddLayer(Pair.Key, Pair.Value.LayerInfoObj);
	}
	if (const ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
		{
			AddLayer(Layer.GetLayerName(), Layer.LayerInfoObj);
		}
	}
	Layers.GenerateValueArray(Result.PaintLayers);

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Landscape '%s': %.0f x %.0f m, %d x %d samples, %d paint layer(s)."),
		*Result.Summary.Name, Result.Summary.SizeXM, Result.Summary.SizeYM,
		Result.Summary.ResolutionX, Result.Summary.ResolutionY, Result.PaintLayers.Num());
	return Result;
}

FWorldBuilderHeightSampleResult ULandscapeInspectTools::SampleHeight(double XM, double YM, const FString& LandscapeName)
{
	FWorldBuilderHeightSampleResult Result;
	Result.XM = XM;
	Result.YM = YM;

	UWorld* World = RequireWorld(Result.Message);
	const FVector2D PointCm(MetersToCm(XM), MetersToCm(YM));
	ALandscape* Landscape = World ? LandscapeUtils::Resolve(World, LandscapeName, &PointCm, Result.Message) : nullptr;
	if (!Landscape)
	{
		return Result;
	}
	Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);

	const TOptional<double> HeightCm = LandscapeUtils::SampleHeightCm(Landscape, PointCm);
	if (!HeightCm.IsSet())
	{
		Result.Message = FString::Printf(TEXT("No landscape data at (%.1f, %.1f) m. The point may be outside '%s' or in an unloaded World Partition region."),
			XM, YM, *Result.LandscapeName);
		return Result;
	}

	Result.HeightM = CmToMeters(HeightCm.GetValue());
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Ground at (%.1f, %.1f) m is %.2f m."), XM, YM, Result.HeightM);
	return Result;
}

FWorldBuilderHeightGridResult ULandscapeInspectTools::SampleHeightGrid(double CenterXM, double CenterYM, double SizeM, int32 GridCount, const FString& LandscapeName)
{
	FWorldBuilderHeightGridResult Result;
	if (SizeM <= 0.0)
	{
		Result.Message = TEXT("SizeM must be greater than 0.");
		return Result;
	}
	if (GridCount < 2 || GridCount > MaxGridCount)
	{
		Result.Message = FString::Printf(TEXT("GridCount must be between 2 and %d (got %d). Use GetHeightStats for larger areas."), MaxGridCount, GridCount);
		return Result;
	}

	UWorld* World = RequireWorld(Result.Message);
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	ALandscape* Landscape = World ? LandscapeUtils::Resolve(World, LandscapeName, &CenterCm, Result.Message) : nullptr;
	if (!Landscape)
	{
		return Result;
	}

	Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);
	Result.GridCount = GridCount;
	Result.SpacingM = SizeM / (GridCount - 1);
	Result.OriginXM = CenterXM - SizeM * 0.5;
	Result.OriginYM = CenterYM - SizeM * 0.5;
	Result.Heights.Reserve(GridCount * GridCount);

	double MinM = TNumericLimits<double>::Max();
	double MaxM = TNumericLimits<double>::Lowest();
	for (int32 Row = 0; Row < GridCount; ++Row)
	{
		for (int32 Col = 0; Col < GridCount; ++Col)
		{
			const FVector2D PointCm(MetersToCm(Result.OriginXM + Col * Result.SpacingM), MetersToCm(Result.OriginYM + Row * Result.SpacingM));
			const TOptional<double> HeightCm = LandscapeUtils::SampleHeightCm(Landscape, PointCm);
			if (HeightCm.IsSet())
			{
				const double H = CmToMeters(HeightCm.GetValue());
				Result.Heights.Add(H);
				MinM = FMath::Min(MinM, H);
				MaxM = FMath::Max(MaxM, H);
			}
			else
			{
				Result.Heights.Add(Result.MissingValue);
				++Result.MissingCount;
			}
		}
	}

	const int32 Total = GridCount * GridCount;
	if (Result.MissingCount == Total)
	{
		Result.Message = TEXT("No landscape data in that area. It may be outside the landscape or in an unloaded World Partition region.");
		return Result;
	}

	Result.MinHeightM = MinM;
	Result.MaxHeightM = MaxM;
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%d x %d grid, %.1f m spacing. Heights %.2f to %.2f m."), GridCount, GridCount, Result.SpacingM, MinM, MaxM);
	if (Result.MissingCount > 0)
	{
		Result.Message += FString::Printf(TEXT(" %d point(s) had no data (marked %.0f)."), Result.MissingCount, Result.MissingValue);
	}
	return Result;
}

FWorldBuilderHeightStatsResult ULandscapeInspectTools::GetHeightStats(double CenterXM, double CenterYM, double SizeXM, double SizeYM, const FString& LandscapeName)
{
	FWorldBuilderHeightStatsResult Result;
	const bool bWholeLandscape = SizeXM <= 0.0 || SizeYM <= 0.0;

	UWorld* World = RequireWorld(Result.Message);
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	ALandscape* Landscape = World ? LandscapeUtils::Resolve(World, LandscapeName, bWholeLandscape ? nullptr : &CenterCm, Result.Message) : nullptr;
	if (!Landscape)
	{
		return Result;
	}
	Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);

	// Region in cm, clipped to the landscape.
	const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
	FVector2D MinCm(Bounds.Min.X, Bounds.Min.Y);
	FVector2D MaxCm(Bounds.Max.X, Bounds.Max.Y);
	if (!bWholeLandscape)
	{
		const FVector2D Half(MetersToCm(SizeXM) * 0.5, MetersToCm(SizeYM) * 0.5);
		MinCm = FVector2D::Max(MinCm, CenterCm - Half);
		MaxCm = FVector2D::Min(MaxCm, CenterCm + Half);
		if (MinCm.X > MaxCm.X || MinCm.Y > MaxCm.Y)
		{
			Result.Message = FString::Printf(TEXT("The region does not overlap landscape '%s'. Use ListLandscapes to see its bounds."), *Result.LandscapeName);
			return Result;
		}
	}
	Result.RegionMinM = MinCm / CmPerMeter;
	Result.RegionMaxM = MaxCm / CmPerMeter;

	// Step through the region at full resolution, or coarser so at most 256 points per side.
	const double SpacingCm = LandscapeUtils::GetSampleSpacingCm(Landscape);
	const int32 SamplesX = FMath::FloorToInt32((MaxCm.X - MinCm.X) / SpacingCm) + 1;
	const int32 SamplesY = FMath::FloorToInt32((MaxCm.Y - MinCm.Y) / SpacingCm) + 1;
	const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(SamplesX, SamplesY), MaxStatsPointsPerSide));
	const double StepCm = Stride * SpacingCm;
	const int32 PointsX = FMath::FloorToInt32((MaxCm.X - MinCm.X) / StepCm) + 1;
	const int32 PointsY = FMath::FloorToInt32((MaxCm.Y - MinCm.Y) / StepCm) + 1;
	Result.SampleSpacingM = CmToMeters(StepCm);

	double MinH = TNumericLimits<double>::Max();
	double MaxH = TNumericLimits<double>::Lowest();
	double SumH = 0.0, SumSlope = 0.0, MaxSlope = 0.0;
	int32 Valid = 0, SlopeCount = 0, FlatCount = 0, SteepCount = 0;

	for (int32 IY = 0; IY < PointsY; ++IY)
	{
		for (int32 IX = 0; IX < PointsX; ++IX)
		{
			const FVector2D P(MinCm.X + IX * StepCm, MinCm.Y + IY * StepCm);
			const TOptional<double> H = LandscapeUtils::SampleHeightCm(Landscape, P);
			if (!H.IsSet())
			{
				++Result.MissingCount;
				continue;
			}

			const double Z = H.GetValue();
			++Valid;
			SumH += Z;
			if (Z < MinH) { MinH = Z; Result.LowestPointM = FVector(P.X, P.Y, Z) / CmPerMeter; }
			if (Z > MaxH) { MaxH = Z; Result.HighestPointM = FVector(P.X, P.Y, Z) / CmPerMeter; }

			// Slope from neighbours one landscape sample away (true local slope, independent of Stride).
			const TOptional<double> XP = LandscapeUtils::SampleHeightCm(Landscape, P + FVector2D(SpacingCm, 0.0));
			const TOptional<double> XN = LandscapeUtils::SampleHeightCm(Landscape, P - FVector2D(SpacingCm, 0.0));
			const TOptional<double> YP = LandscapeUtils::SampleHeightCm(Landscape, P + FVector2D(0.0, SpacingCm));
			const TOptional<double> YN = LandscapeUtils::SampleHeightCm(Landscape, P - FVector2D(0.0, SpacingCm));
			const double DzDx = (XP.IsSet() && XN.IsSet()) ? (XP.GetValue() - XN.GetValue()) / (2.0 * SpacingCm)
				: XP.IsSet() ? (XP.GetValue() - Z) / SpacingCm
				: XN.IsSet() ? (Z - XN.GetValue()) / SpacingCm : 0.0;
			const double DzDy = (YP.IsSet() && YN.IsSet()) ? (YP.GetValue() - YN.GetValue()) / (2.0 * SpacingCm)
				: YP.IsSet() ? (YP.GetValue() - Z) / SpacingCm
				: YN.IsSet() ? (Z - YN.GetValue()) / SpacingCm : 0.0;
			const double Slope = LandscapeMath::SlopeDegrees(DzDx, DzDy);
			SumSlope += Slope;
			MaxSlope = FMath::Max(MaxSlope, Slope);
			++SlopeCount;
			FlatCount += Slope < FlatSlopeDeg ? 1 : 0;
			SteepCount += Slope > SteepSlopeDeg ? 1 : 0;
		}
	}

	Result.SampleCount = PointsX * PointsY;
	if (Valid == 0)
	{
		Result.Message = TEXT("No landscape data in that region. It may be in an unloaded World Partition region.");
		return Result;
	}

	Result.MinHeightM = CmToMeters(MinH);
	Result.MaxHeightM = CmToMeters(MaxH);
	Result.AverageHeightM = CmToMeters(SumH / Valid);
	Result.AverageSlopeDeg = SumSlope / SlopeCount;
	Result.MaxSlopeDeg = MaxSlope;
	Result.FlatFraction = double(FlatCount) / SlopeCount;
	Result.SteepFraction = double(SteepCount) / SlopeCount;
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Heights %.2f to %.2f m (avg %.2f). Slope avg %.1f deg, max %.1f deg; %.0f%% flat, %.0f%% steep. %d points at %.1f m spacing."),
		Result.MinHeightM, Result.MaxHeightM, Result.AverageHeightM, Result.AverageSlopeDeg, Result.MaxSlopeDeg,
		Result.FlatFraction * 100.0, Result.SteepFraction * 100.0, Result.SampleCount, Result.SampleSpacingM);
	if (Result.MissingCount > 0)
	{
		Result.Message += FString::Printf(TEXT(" %d point(s) had no data (unloaded World Partition region?)."), Result.MissingCount);
	}
	return Result;
}
