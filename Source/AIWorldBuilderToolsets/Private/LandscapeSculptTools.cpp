#include "LandscapeSculptTools.h"

#include "LandscapeEditPipeline.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AIWorldBuilderTerrainMath.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "Misc/App.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIWorldBuilderSculpt"

using namespace AIWorldBuilder;
namespace TM = AIWorldBuilder::TerrainMath;
using namespace AIWorldBuilder::EditPipeline;

namespace
{

	TM::EHeightBlendMode ToMath(EWorldBuilderBlendMode Mode)
	{
		switch (Mode)
		{
		case EWorldBuilderBlendMode::Max:     return TM::EHeightBlendMode::Max;
		case EWorldBuilderBlendMode::Min:     return TM::EHeightBlendMode::Min;
		case EWorldBuilderBlendMode::Replace: return TM::EHeightBlendMode::Replace;
		case EWorldBuilderBlendMode::Blend:   return TM::EHeightBlendMode::Blend;
		case EWorldBuilderBlendMode::Add:
		default:                              return TM::EHeightBlendMode::Add;
		}
	}

	TM::EFalloff ToMath(EWorldBuilderFalloff Falloff)
	{
		switch (Falloff)
		{
		case EWorldBuilderFalloff::Linear: return TM::EFalloff::Linear;
		case EWorldBuilderFalloff::Sphere: return TM::EFalloff::Sphere;
		case EWorldBuilderFalloff::Tip:    return TM::EFalloff::Tip;
		case EWorldBuilderFalloff::Smooth:
		default:                           return TM::EFalloff::Smooth;
		}
	}

	TM::ERadialShape ToMath(EWorldBuilderShape Shape)
	{
		switch (Shape)
		{
		case EWorldBuilderShape::Mountain: return TM::ERadialShape::Mountain;
		case EWorldBuilderShape::Volcano:  return TM::ERadialShape::Volcano;
		case EWorldBuilderShape::Crater:   return TM::ERadialShape::Crater;
		case EWorldBuilderShape::Plateau:  return TM::ERadialShape::Plateau;
		case EWorldBuilderShape::Mesa:     return TM::ERadialShape::Mesa;
		case EWorldBuilderShape::Hill:
		default:                           return TM::ERadialShape::Hill;
		}
	}

	FString EnumName(EWorldBuilderShape Shape) { return StaticEnum<EWorldBuilderShape>()->GetNameStringByValue(int64(Shape)); }
	FString EnumName(EWorldBuilderLineShape Shape) { return StaticEnum<EWorldBuilderLineShape>()->GetNameStringByValue(int64(Shape)); }

	FBox2D BoxAround(const FVector2D& CenterCm, double RadiusCm)
	{
		return FBox2D(CenterCm - FVector2D(RadiusCm), CenterCm + FVector2D(RadiusCm));
	}

}

FString ULandscapeSculptTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderSculptResult ULandscapeSculptTools::ApplyShape(
	EWorldBuilderShape ShapeType, double CenterXM, double CenterYM, double RadiusM, double HeightM,
	EWorldBuilderBlendMode BlendMode, double BlendAlpha, double NoiseAmount, int32 Seed,
	double CraterRadiusM, double CraterDepthM, double RimSharpness, int32 LavaChannels,
	double TopFraction, bool bAllowClipping, const FString& LandscapeName)
{
	if (RadiusM <= 0.0)
	{
		return Fail(TEXT("RadiusM must be greater than 0."));
	}
	if (HeightM <= 0.0)
	{
		return Fail(TEXT("HeightM must be greater than 0 (for Crater it is the depth)."));
	}
	if (ShapeType == EWorldBuilderShape::Volcano && CraterRadiusM >= RadiusM)
	{
		return Fail(TEXT("CraterRadiusM must be smaller than RadiusM."));
	}

	TM::FRadialShapeParams Params;
	Params.RadiusCm = MetersToCm(RadiusM);
	Params.HeightCm = MetersToCm(HeightM);
	Params.NoiseAmount = FMath::Clamp(NoiseAmount, 0.0, 1.0);
	Params.Seed = Seed;
	Params.CraterRadiusCm = MetersToCm(FMath::Max(CraterRadiusM, 0.0));
	Params.CraterDepthCm = MetersToCm(FMath::Max(CraterDepthM, 0.0));
	Params.RimSharpness = FMath::Clamp(RimSharpness, 0.0, 1.0);
	Params.LavaChannels = FMath::Clamp(LavaChannels, 0, 12);
	Params.TopFraction = FMath::Clamp(TopFraction, 0.05, 0.95);

	const TM::ERadialShape Shape = ToMath(ShapeType);
	const TM::EHeightBlendMode Mode = ToMath(BlendMode);
	const double Alpha = FMath::Clamp(BlendAlpha, 0.0, 1.0);
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	const double FootprintCm = TM::GetFootprintRadiusCm(Shape, Params);

	return RunSculpt(FText::Format(LOCTEXT("ApplyShape", "AI: Apply {0}"), FText::FromString(EnumName(ShapeType))),
		LandscapeName, CenterCm, BoxAround(CenterCm, FootprintCm), bAllowClipping,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			const double Reference = Grid.SampleCurrent(CenterCm);
			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				const FVector2D Offset = World - CenterCm;
				if (Offset.SizeSquared() > FMath::Square(FootprintCm))
				{
					return;
				}
				const TM::FShapeSample S = TM::EvaluateRadialShape(Shape, Params, Offset);
				Grid.New[I] = TM::BlendHeight(Mode, Grid.Current[I], Reference, S.Height, S.Weight, Alpha);
			});
			return true;
		});
}

FWorldBuilderSculptResult ULandscapeSculptTools::ApplyLineShape(
	EWorldBuilderLineShape ShapeType, double StartXM, double StartYM, double EndXM, double EndYM, double WidthM, double HeightM,
	EWorldBuilderBlendMode BlendMode, double BlendAlpha, double NoiseAmount, int32 Seed, bool bAllowClipping, const FString& LandscapeName)
{
	if (WidthM <= 0.0 || HeightM <= 0.0)
	{
		return Fail(TEXT("WidthM and HeightM must be greater than 0 (HeightM is the depth for Valley)."));
	}

	TM::FLineShapeParams Params;
	Params.StartCm = FVector2D(MetersToCm(StartXM), MetersToCm(StartYM));
	Params.EndCm = FVector2D(MetersToCm(EndXM), MetersToCm(EndYM));
	Params.HalfWidthCm = MetersToCm(WidthM) * 0.5;
	Params.HeightCm = MetersToCm(HeightM);
	Params.NoiseAmount = FMath::Clamp(NoiseAmount, 0.0, 1.0);
	Params.Seed = Seed;

	const TM::ELineShape Shape = ShapeType == EWorldBuilderLineShape::Ridge ? TM::ELineShape::Ridge : TM::ELineShape::Valley;
	const TM::EHeightBlendMode Mode = ToMath(BlendMode);
	const double Alpha = FMath::Clamp(BlendAlpha, 0.0, 1.0);
	const double ReachCm = Params.HalfWidthCm * (1.0 + 0.5 * Params.NoiseAmount);	// meander can shift the line sideways

	FBox2D Area(ForceInit);
	Area += Params.StartCm;
	Area += Params.EndCm;
	Area = Area.ExpandBy(ReachCm);

	return RunSculpt(FText::Format(LOCTEXT("ApplyLineShape", "AI: Apply {0}"), FText::FromString(EnumName(ShapeType))),
		LandscapeName, (Params.StartCm + Params.EndCm) * 0.5, Area, bAllowClipping,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			const double GroundStart = Grid.SampleCurrent(Params.StartCm);
			const double GroundEnd = Grid.SampleCurrent(Params.EndCm);
			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				double Along = 0.0;
				const TM::FShapeSample S = TM::EvaluateLineShape(Shape, Params, World, Along);
				if (S.Weight <= 0.0 && S.Height == 0.0)
				{
					return;
				}
				const double Reference = FMath::Lerp(GroundStart, GroundEnd, Along);
				Grid.New[I] = TM::BlendHeight(Mode, Grid.Current[I], Reference, S.Height, S.Weight, Alpha);
			});
			return true;
		});
}

FWorldBuilderSculptResult ULandscapeSculptTools::RaiseLower(double CenterXM, double CenterYM, double RadiusM, double DeltaM,
	EWorldBuilderFalloff Falloff, bool bAllowClipping, const FString& LandscapeName)
{
	if (RadiusM <= 0.0)
	{
		return Fail(TEXT("RadiusM must be greater than 0."));
	}
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	const double RadiusCm = MetersToCm(RadiusM);
	const double DeltaCm = MetersToCm(DeltaM);
	const TM::EFalloff FalloffType = ToMath(Falloff);

	return RunSculpt(DeltaM >= 0.0 ? LOCTEXT("Raise", "AI: Raise Terrain") : LOCTEXT("Lower", "AI: Lower Terrain"),
		LandscapeName, CenterCm, BoxAround(CenterCm, RadiusCm), bAllowClipping,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				Grid.New[I] = Grid.Current[I] + DeltaCm * TM::Falloff(FalloffType, FVector2D::Distance(World, CenterCm) / RadiusCm);
			});
			return true;
		});
}

FWorldBuilderSculptResult ULandscapeSculptTools::Flatten(double CenterXM, double CenterYM, double RadiusM, double TargetHeightM,
	double Strength, double EdgeFraction, EWorldBuilderFalloff Falloff, bool bAllowClipping, const FString& LandscapeName)
{
	if (RadiusM <= 0.0)
	{
		return Fail(TEXT("RadiusM must be greater than 0."));
	}
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	const double RadiusCm = MetersToCm(RadiusM);
	const double TargetCm = MetersToCm(TargetHeightM);
	const double Amount = FMath::Clamp(Strength, 0.0, 1.0);
	const double Edge = FMath::Clamp(EdgeFraction, 0.0, 1.0);
	const TM::EFalloff FalloffType = ToMath(Falloff);

	return RunSculpt(LOCTEXT("Flatten", "AI: Flatten"), LandscapeName, CenterCm, BoxAround(CenterCm, RadiusCm), bAllowClipping,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				const double W = Amount * TM::EdgeWeight(FalloffType, FVector2D::Distance(World, CenterCm) / RadiusCm, Edge);
				Grid.New[I] = FMath::Lerp(Grid.Current[I], TargetCm, W);
			});
			return true;
		});
}

FWorldBuilderSculptResult ULandscapeSculptTools::Smooth(double CenterXM, double CenterYM, double RadiusM, double Strength,
	double KernelRadiusM, int32 Iterations, EWorldBuilderFalloff Falloff, const FString& LandscapeName)
{
	if (RadiusM <= 0.0)
	{
		return Fail(TEXT("RadiusM must be greater than 0."));
	}
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	const double RadiusCm = MetersToCm(RadiusM);
	const double KernelCm = MetersToCm(KernelRadiusM > 0.0 ? KernelRadiusM : RadiusM * 0.05);
	const int32 Passes = FMath::Clamp(Iterations, 1, 10);
	const double Amount = FMath::Clamp(Strength, 0.0, 1.0);
	const TM::EFalloff FalloffType = ToMath(Falloff);

	// Read extra terrain around the circle so the blur near the edge sees real neighbours.
	const double MarginCm = KernelCm * Passes;

	return RunSculpt(LOCTEXT("Smooth", "AI: Smooth"), LandscapeName, CenterCm, BoxAround(CenterCm, RadiusCm + MarginCm), /*bAllowClipping=*/true,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			const int32 KernelSamples = FMath::Max(1, FMath::RoundToInt32(KernelCm / Grid.SpacingCm));
			TArray<double> Blurred = Grid.Current;
			TM::BoxBlur(Blurred, Grid.Width, Grid.Height, KernelSamples, Passes);
			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				const double W = Amount * TM::Falloff(FalloffType, FVector2D::Distance(World, CenterCm) / RadiusCm);
				Grid.New[I] = FMath::Lerp(Grid.Current[I], Blurred[I], W);
			});
			return true;
		});
}

FWorldBuilderSculptResult ULandscapeSculptTools::AddNoise(double CenterXM, double CenterYM, double SizeXM, double SizeYM,
	double AmplitudeM, double WavelengthM, int32 Octaves, int32 Seed, bool bRidged, bool bAllowClipping, const FString& LandscapeName)
{
	if (SizeXM <= 0.0 || SizeYM <= 0.0)
	{
		return Fail(TEXT("SizeXM and SizeYM must be greater than 0."));
	}
	if (WavelengthM <= 0.0)
	{
		return Fail(TEXT("WavelengthM must be greater than 0."));
	}
	const FVector2D CenterCm(MetersToCm(CenterXM), MetersToCm(CenterYM));
	const FVector2D HalfCm(MetersToCm(SizeXM) * 0.5, MetersToCm(SizeYM) * 0.5);
	const double AmplitudeCm = MetersToCm(AmplitudeM);
	const double WavelengthCm = MetersToCm(WavelengthM);
	const int32 OctaveCount = FMath::Clamp(Octaves, 1, 8);

	return RunSculpt(LOCTEXT("AddNoise", "AI: Add Noise"), LandscapeName, CenterCm, FBox2D(CenterCm - HalfCm, CenterCm + HalfCm), bAllowClipping,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				// Fade out over the outer 10% of each side so the region has no visible seam.
				const FVector2D UV = (World - (CenterCm - HalfCm)) / (HalfCm * 2.0);
				const double Taper = TM::SmoothStep(0.0, 0.1, UV.X) * TM::SmoothStep(0.0, 0.1, 1.0 - UV.X)
					* TM::SmoothStep(0.0, 0.1, UV.Y) * TM::SmoothStep(0.0, 0.1, 1.0 - UV.Y);
				if (Taper <= 0.0)
				{
					return;
				}
				const FVector2D P = World / WavelengthCm;
				const double N = bRidged ? TM::Ridged(P, OctaveCount, Seed) : TM::FBM(P, OctaveCount, Seed);
				Grid.New[I] = Grid.Current[I] + AmplitudeCm * N * Taper;
			});
			return true;
		});
}

FWorldBuilderSculptResult ULandscapeSculptTools::CarvePath(const TArray<FVector2D>& PointsM, double WidthM, double DepthM,
	double BankWidthM, EWorldBuilderFalloff Falloff, bool bLowerOnly, bool bAllowClipping, const FString& LandscapeName)
{
	if (PointsM.Num() < 2)
	{
		return Fail(TEXT("PointsM needs at least 2 points, e.g. [{\"x\":0,\"y\":0},{\"x\":500,\"y\":200}]."));
	}
	if (WidthM <= 0.0)
	{
		return Fail(TEXT("WidthM must be greater than 0."));
	}
	if (DepthM < 0.0)
	{
		return Fail(TEXT("DepthM must be 0 or more (it is measured downward)."));
	}

	TArray<FVector2D> PointsCm;
	FBox2D Area(ForceInit);
	for (const FVector2D& P : PointsM)
	{
		PointsCm.Add(P * CmPerMeter);
		Area += P * CmPerMeter;
	}
	const double InnerCm = MetersToCm(WidthM) * 0.5;
	const double BankCm = MetersToCm(BankWidthM > 0.0 ? BankWidthM : WidthM * 0.5);
	const double OuterCm = InnerCm + BankCm;
	const double DepthCm = MetersToCm(DepthM);
	const TM::EFalloff FalloffType = ToMath(Falloff);
	Area = Area.ExpandBy(OuterCm);

	return RunSculpt(LOCTEXT("CarvePath", "AI: Carve Path"), LandscapeName, PointsCm[0], Area, bAllowClipping,
		[&](FSculptGrid& Grid, FString& OutError)
		{
			// Bed height at each vertex: ground there minus the depth; interpolated along the path.
			const TArray<double> Lengths = TM::PolylineCumulativeLengths(PointsCm);
			TArray<double> BedCm;
			for (const FVector2D& P : PointsCm)
			{
				BedCm.Add(Grid.SampleCurrent(P) - DepthCm);
			}
			auto BedAt = [&](double Along)
			{
				for (int32 S = 0; S + 1 < Lengths.Num(); ++S)
				{
					if (Along <= Lengths[S + 1] || S + 2 == Lengths.Num())
					{
						const double SegLen = Lengths[S + 1] - Lengths[S];
						const double T = SegLen > 0.0 ? FMath::Clamp((Along - Lengths[S]) / SegLen, 0.0, 1.0) : 0.0;
						return FMath::Lerp(BedCm[S], BedCm[S + 1], T);
					}
				}
				return BedCm.Last();
			};

			Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
			{
				double Along = 0.0;
				const double D = TM::DistanceToPolyline(World, PointsCm, Along);
				if (D >= OuterCm)
				{
					return;
				}
				const double W = D <= InnerCm ? 1.0 : TM::Falloff(FalloffType, (D - InnerCm) / BankCm);
				const double Bed = BedAt(Along);
				const double Target = bLowerOnly ? FMath::Min(Grid.Current[I], Bed) : Bed;
				Grid.New[I] = FMath::Lerp(Grid.Current[I], Target, W);
			});
			return true;
		});
}

#undef LOCTEXT_NAMESPACE
