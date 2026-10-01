#include "AIWorldBuilderLandscape.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAIWorldBuilderLandscapeMathTest, "AIWorldBuilder.Core.LandscapeMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWorldBuilderLandscapeMathTest::RunTest(const FString& Parameters)
{
	using namespace AIWorldBuilder;
	using namespace AIWorldBuilder::LandscapeMath;

	// Encoding: 32768 is zero; 128 stored units per local unit.
	TestEqual(TEXT("32768 -> local 0"), HeightValueToLocalZ(32768), 0.0);
	TestEqual(TEXT("32768+128 -> local 1"), HeightValueToLocalZ(32768 + 128), 1.0);
	TestEqual(TEXT("local 0 -> 32768"), (int32)LocalZToHeightValue(0.0), 32768);
	TestEqual(TEXT("local clamps low"), (int32)LocalZToHeightValue(-10000.0), 0);
	TestEqual(TEXT("local clamps high"), (int32)LocalZToHeightValue(10000.0), 65535);

	// World conversion with ScaleZ = 100 (default) and the actor at Z = 1000 cm.
	TestEqual(TEXT("world zero at actor Z"), HeightValueToWorldZCm(32768, 100.0, 1000.0), 1000.0);
	TestEqual(TEXT("+128 units = +100 cm"), HeightValueToWorldZCm(32768 + 128, 100.0, 1000.0), 1100.0);
	TestEqual(TEXT("world -> value round trip"), (int32)WorldZCmToHeightValue(1100.0, 100.0, 1000.0), 32768 + 128);

	// Round trip at ScaleZ = 200 within one stored unit (200/128 cm).
	for (double Z : { -40000.0, -123.4, 0.0, 777.7, 40000.0 })
	{
		const uint16 V = WorldZCmToHeightValue(Z, 200.0, 0.0);
		TestTrue(FString::Printf(TEXT("round trip %.1f"), Z), FMath::Abs(HeightValueToWorldZCm(V, 200.0, 0.0) - Z) <= 200.0 / 128.0);
	}

	// Height range: default scale is about +/-256 m; ScaleZ 200 is about +/-512 m.
	double MinCm = 0, MaxCm = 0;
	GetHeightRangeCm(100.0, 0.0, MinCm, MaxCm);
	TestEqual(TEXT("range min @100"), MinCm, -25600.0);
	TestTrue(TEXT("range max @100"), FMath::IsNearlyEqual(MaxCm, 25600.0, 1.0));
	GetHeightRangeCm(200.0, 500.0, MinCm, MaxCm);
	TestEqual(TEXT("range min @200 offset"), MinCm, -51200.0 + 500.0);

	// Units and slope.
	TestEqual(TEXT("m -> cm"), MetersToCm(1.5), 150.0);
	TestEqual(TEXT("cm -> m"), CmToMeters(250.0), 2.5);
	TestTrue(TEXT("flat slope"), FMath::IsNearlyZero(SlopeDegrees(0.0, 0.0)));
	TestTrue(TEXT("45 deg slope"), FMath::IsNearlyEqual(SlopeDegrees(1.0, 0.0), 45.0, 1e-6));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
