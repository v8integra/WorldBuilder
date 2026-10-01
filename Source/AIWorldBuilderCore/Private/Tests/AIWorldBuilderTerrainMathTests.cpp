#include "AIWorldBuilderTerrainMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AIWorldBuilder::TerrainMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIWorldBuilderFalloffBlendTest, "AIWorldBuilder.Core.TerrainMath.FalloffAndBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWorldBuilderFalloffBlendTest::RunTest(const FString& Parameters)
{
	for (EFalloff F : { EFalloff::Linear, EFalloff::Smooth, EFalloff::Sphere, EFalloff::Tip })
	{
		TestTrue(TEXT("falloff 1 at centre"), FMath::IsNearlyEqual(Falloff(F, 0.0), 1.0));
		TestEqual(TEXT("falloff 0 at edge"), Falloff(F, 1.0), 0.0);
		TestEqual(TEXT("falloff 0 beyond"), Falloff(F, 1.5), 0.0);
		double Prev = 2.0;
		for (double T = 0.0; T <= 1.0; T += 0.05)
		{
			const double W = Falloff(F, T);
			TestTrue(TEXT("falloff in range and non-increasing"), W >= 0.0 && W <= 1.0 && W <= Prev + 1e-9);
			Prev = W;
		}
	}
	TestTrue(TEXT("smooth midpoint"), FMath::IsNearlyEqual(Falloff(EFalloff::Smooth, 0.5), 0.5));
	TestEqual(TEXT("edge weight inner"), EdgeWeight(EFalloff::Linear, 0.5, 0.3), 1.0);
	TestTrue(TEXT("edge weight mid band"), FMath::IsNearlyEqual(EdgeWeight(EFalloff::Linear, 0.85, 0.3), 0.5));

	// Blend modes: current 10, reference 0, shape 25, full weight.
	TestEqual(TEXT("add"), BlendHeight(EHeightBlendMode::Add, 10, 0, 25, 1, 1), 35.0);
	TestEqual(TEXT("add half alpha"), BlendHeight(EHeightBlendMode::Add, 10, 0, 25, 1, 0.5), 22.5);
	TestEqual(TEXT("max raises"), BlendHeight(EHeightBlendMode::Max, 10, 0, 25, 1, 1), 25.0);
	TestEqual(TEXT("max keeps higher ground"), BlendHeight(EHeightBlendMode::Max, 40, 0, 25, 1, 1), 40.0);
	TestEqual(TEXT("min lowers"), BlendHeight(EHeightBlendMode::Min, 40, 0, 25, 1, 1), 25.0);
	TestEqual(TEXT("min keeps lower ground"), BlendHeight(EHeightBlendMode::Min, 10, 0, 25, 1, 1), 10.0);
	TestEqual(TEXT("replace"), BlendHeight(EHeightBlendMode::Replace, 10, 0, 25, 1, 0.1), 25.0);
	TestEqual(TEXT("replace zero weight"), BlendHeight(EHeightBlendMode::Replace, 10, 0, 25, 0, 1), 10.0);
	TestEqual(TEXT("blend half"), BlendHeight(EHeightBlendMode::Blend, 10, 0, 30, 1, 0.5), 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIWorldBuilderShapesTest, "AIWorldBuilder.Core.TerrainMath.Shapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWorldBuilderShapesTest::RunTest(const FString& Parameters)
{
	FRadialShapeParams P;
	P.RadiusCm = 150000.0;	// 1.5 km
	P.HeightCm = 40000.0;	// 400 m
	P.NoiseAmount = 0.0;	// deterministic profiles

	// Every radial shape vanishes outside its footprint.
	for (ERadialShape S : { ERadialShape::Mountain, ERadialShape::Volcano, ERadialShape::Crater, ERadialShape::Hill, ERadialShape::Plateau, ERadialShape::Mesa })
	{
		const double Footprint = GetFootprintRadiusCm(S, P);
		const FShapeSample Out = EvaluateRadialShape(S, P, FVector2D(Footprint * 1.01, 0.0));
		TestTrue(FString::Printf(TEXT("shape %d weight 0 outside"), int32(S)), Out.Weight <= 1e-6);
		TestTrue(FString::Printf(TEXT("shape %d ~0 height outside"), int32(S)), FMath::Abs(Out.Height) < P.HeightCm * 0.01);
	}

	// Hill and mountain peak at the centre.
	TestTrue(TEXT("hill peak"), FMath::IsNearlyEqual(EvaluateRadialShape(ERadialShape::Hill, P, FVector2D::ZeroVector).Height, P.HeightCm, 1.0));
	TestTrue(TEXT("mountain peak"), FMath::IsNearlyEqual(EvaluateRadialShape(ERadialShape::Mountain, P, FVector2D::ZeroVector).Height, P.HeightCm, 1.0));

	// Volcano: rim at full height, crater floor 20% lower, flanks descend.
	const double Rim = 0.12 * P.RadiusCm;
	const double RimH = EvaluateRadialShape(ERadialShape::Volcano, P, FVector2D(Rim, 0.0)).Height;
	const double FloorH = EvaluateRadialShape(ERadialShape::Volcano, P, FVector2D::ZeroVector).Height;
	const double FlankH = EvaluateRadialShape(ERadialShape::Volcano, P, FVector2D(0.5 * P.RadiusCm, 0.0)).Height;
	TestTrue(TEXT("volcano rim ~ height"), FMath::IsNearlyEqual(RimH, P.HeightCm, P.HeightCm * 0.03));
	TestTrue(TEXT("volcano floor = height - 20%"), FMath::IsNearlyEqual(FloorH, P.HeightCm * 0.8, 1.0));
	TestTrue(TEXT("volcano flank lower than rim"), FlankH < RimH && FlankH > 0.0);

	// Rim continuity: no step just inside vs. just outside the rim even with a rough outline.
	FRadialShapeParams Rough = P;
	Rough.NoiseAmount = 1.0;
	for (int32 A = 0; A < 16; ++A)
	{
		const FVector2D Dir(FMath::Cos(A * PI / 8.0), FMath::Sin(A * PI / 8.0));
		const double In = EvaluateRadialShape(ERadialShape::Volcano, Rough, Dir * (Rim - 100.0)).Height;
		const double Out = EvaluateRadialShape(ERadialShape::Volcano, Rough, Dir * (Rim + 100.0)).Height;
		TestTrue(TEXT("volcano rim continuous"), FMath::Abs(In - Out) < P.HeightCm * 0.01);
	}

	// Lava channels notch the rim.
	FRadialShapeParams Lava = P;
	Lava.LavaChannels = 4;
	double MinRim = TNumericLimits<double>::Max();
	for (int32 A = 0; A < 360; ++A)
	{
		const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)));
		MinRim = FMath::Min(MinRim, EvaluateRadialShape(ERadialShape::Volcano, Lava, Dir * Rim).Height);
	}
	TestTrue(TEXT("lava channel notches rim"), MinRim < P.HeightCm * 0.95);

	// Crater: centre is H below, rim is above ground.
	TestTrue(TEXT("crater centre depth"), FMath::IsNearlyEqual(EvaluateRadialShape(ERadialShape::Crater, P, FVector2D::ZeroVector).Height, -P.HeightCm, 1.0));
	TestTrue(TEXT("crater rim raised"), EvaluateRadialShape(ERadialShape::Crater, P, FVector2D(P.RadiusCm, 0.0)).Height > 0.0);

	// Plateau and mesa: flat top.
	for (ERadialShape S : { ERadialShape::Plateau, ERadialShape::Mesa })
	{
		const double C = EvaluateRadialShape(S, P, FVector2D::ZeroVector).Height;
		const double T = EvaluateRadialShape(S, P, FVector2D(0.5 * P.RadiusCm, 0.0)).Height;
		TestTrue(TEXT("flat top"), FMath::IsNearlyEqual(C, T, 1.0) && FMath::IsNearlyEqual(C, P.HeightCm, 1.0));
	}

	// Line shapes: valley is negative on the line, ridge positive; both 0 beyond the half width.
	FLineShapeParams L;
	L.StartCm = FVector2D(0, 0);
	L.EndCm = FVector2D(100000, 0);
	L.HalfWidthCm = 5000;
	L.HeightCm = 2000;
	L.NoiseAmount = 0.0;
	double Along = 0.0;
	TestTrue(TEXT("valley floor"), FMath::IsNearlyEqual(EvaluateLineShape(ELineShape::Valley, L, FVector2D(50000, 0), Along).Height, -2000.0, 1.0));
	TestTrue(TEXT("valley along mid"), FMath::IsNearlyEqual(Along, 0.5, 1e-6));
	TestTrue(TEXT("ridge crest"), FMath::IsNearlyEqual(EvaluateLineShape(ELineShape::Ridge, L, FVector2D(50000, 0), Along).Height, 2000.0, 1.0));
	TestTrue(TEXT("line outside"), FMath::IsNearlyZero(EvaluateLineShape(ELineShape::Ridge, L, FVector2D(50000, 6000), Along).Height));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIWorldBuilderGeometryTest, "AIWorldBuilder.Core.TerrainMath.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWorldBuilderGeometryTest::RunTest(const FString& Parameters)
{
	// Polyline: an L shape 100 + 100 long.
	const TArray<FVector2D> Line = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100) };
	double Along = 0.0;
	TestTrue(TEXT("dist to first segment"), FMath::IsNearlyEqual(DistanceToPolyline(FVector2D(50, 10), Line, Along), 10.0));
	TestTrue(TEXT("along first segment"), FMath::IsNearlyEqual(Along, 50.0));
	TestTrue(TEXT("dist to second segment"), FMath::IsNearlyEqual(DistanceToPolyline(FVector2D(110, 50), Line, Along), 10.0));
	TestTrue(TEXT("along second segment"), FMath::IsNearlyEqual(Along, 150.0));
	const TArray<double> Lengths = PolylineCumulativeLengths(Line);
	TestTrue(TEXT("cumulative lengths"), Lengths.Num() == 3 && FMath::IsNearlyEqual(Lengths[2], 200.0));

	// Blur keeps a constant field constant and conserves a spike's total on a large grid.
	TArray<double> Flat;
	Flat.Init(5.0, 20 * 10);
	BoxBlur(Flat, 20, 10, 2, 3);
	bool bFlat = true;
	for (double V : Flat) { bFlat &= FMath::IsNearlyEqual(V, 5.0); }
	TestTrue(TEXT("blur keeps constant"), bFlat);

	TArray<double> Spike;
	Spike.Init(0.0, 41 * 41);
	Spike[20 * 41 + 20] = 100.0;
	BoxBlur(Spike, 41, 41, 2, 3);
	double Total = 0.0;
	for (double V : Spike) { Total += V; }
	TestTrue(TEXT("blur conserves mass away from edges"), FMath::IsNearlyEqual(Total, 100.0, 1e-6));
	TestTrue(TEXT("blur spreads spike"), Spike[20 * 41 + 20] < 100.0 && Spike[20 * 41 + 21] > 0.0);

	// Noise: bounded and seed-dependent.
	bool bBounded = true;
	for (int32 I = 0; I < 200; ++I)
	{
		const FVector2D Pt(I * 0.173, I * 0.311);
		bBounded &= FMath::Abs(FBM(Pt, 5, 3)) <= 1.0;
		const double R = Ridged(Pt, 5, 3);
		bBounded &= R >= 0.0 && R <= 1.0;
	}
	TestTrue(TEXT("noise bounded"), bBounded);
	TestTrue(TEXT("noise seeds differ"), !FMath::IsNearlyEqual(FBM(FVector2D(1.3, 2.7), 4, 1), FBM(FVector2D(1.3, 2.7), 4, 2)));
	TestTrue(TEXT("noise deterministic"), FBM(FVector2D(1.3, 2.7), 4, 1) == FBM(FVector2D(1.3, 2.7), 4, 1));

	// Preview colours.
	TestTrue(TEXT("flat is green"), SlopeToColor(0.0) == FColor(34, 139, 34));
	TestTrue(TEXT("very steep is purple"), SlopeToColor(80.0) == FColor(128, 0, 128));
	TestTrue(TEXT("stop exact"), SlopeToColor(30.0) == FColor(255, 140, 0));
	TestEqual(TEXT("grey min"), HeightToGrey(10.0, 10.0, 20.0).R, uint8(0));
	TestEqual(TEXT("grey max"), HeightToGrey(20.0, 10.0, 20.0).R, uint8(255));
	TestEqual(TEXT("grey flat range"), HeightToGrey(5.0, 5.0, 5.0).R, uint8(128));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
