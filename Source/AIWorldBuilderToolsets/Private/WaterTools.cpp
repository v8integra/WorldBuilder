#include "WaterTools.h"

#include "ActorFactories/ActorFactory.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "ScopedTransaction.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyCustomActor.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyRiverActor.h"
#include "WaterSplineComponent.h"
#include "WaterSplineMetadata.h"
#include "WaterZoneActor.h"

#define LOCTEXT_NAMESPACE "AIWorldBuilderWater"

using namespace AIWorldBuilder;

namespace
{
	constexpr int32 CirclePoints = 16;

	FString TypeOf(const AWaterBody* Body)
	{
		if (Body->IsA<AWaterBodyOcean>()) { return TEXT("Ocean"); }
		if (Body->IsA<AWaterBodyLake>())  { return TEXT("Lake"); }
		if (Body->IsA<AWaterBodyRiver>()) { return TEXT("River"); }
		if (Body->IsA<AWaterBodyCustom>()) { return TEXT("Custom"); }
		return Body->GetClass()->GetName();
	}

	FWorldBuilderWaterBodyInfo Describe(AWaterBody* Body)
	{
		FWorldBuilderWaterBodyInfo Info;
		Info.Label = Body->GetActorLabel();
		Info.Type = TypeOf(Body);
		Info.LocationM = Body->GetActorLocation() / CmPerMeter;
		const FBox Bounds = Body->GetComponentsBoundingBox(/*bNonColliding=*/true);
		if (Bounds.IsValid)
		{
			Info.BoundsMinM = Bounds.Min / CmPerMeter;
			Info.BoundsMaxM = Bounds.Max / CmPerMeter;
		}
		if (const UWaterSplineComponent* Spline = Body->GetWaterSpline())
		{
			Info.SplinePointCount = Spline->GetNumberOfSplinePoints();
		}
		if (const UWaterBodyComponent* Component = Body->GetWaterBodyComponent())
		{
			Info.bAffectsLandscape = Component->bAffectsLandscape;
		}
		return Info;
	}

	/** Ground height (cm) at a point from the landscape under it. */
	TOptional<double> GroundCm(UWorld* World, const FVector2D& PointCm)
	{
		FString Ignored;
		if (const ALandscape* Landscape = LandscapeUtils::Resolve(World, TEXT("auto"), &PointCm, Ignored))
		{
			return LandscapeUtils::SampleHeightCm(Landscape, PointCm);
		}
		return {};
	}

	FString UniqueLabel(UWorld* World, const FString& Requested, const FString& Fallback)
	{
		const FString Base = (Requested.IsEmpty() || Requested.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) ? Fallback : Requested;
		TSet<FString> Used;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Used.Add(It->GetActorLabel());
		}
		FString Label = Base;
		for (int32 N = 2; Used.Contains(Label); ++N)
		{
			Label = FString::Printf(TEXT("%s_%d"), *Base, N);
		}
		return Label;
	}

	/** Existing water bodies that deform the landscape (created outside these tools); those can crash the editor. */
	FString LandscapeBrushWarning(UWorld* World)
	{
		TArray<FString> Risky;
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			if (const UWaterBodyComponent* Component = It->GetWaterBodyComponent(); Component && Component->bAffectsLandscape)
			{
				Risky.Add(It->GetActorLabel());
			}
		}
		return Risky.IsEmpty() ? FString()
			: FString::Printf(TEXT(" Warning: %s still deform(s) the landscape (Affects Landscape on), which has crashed the editor before; remove with RemoveWaterBody or turn Affects Landscape off."),
				*FString::Join(Risky, TEXT(", ")));
	}

	/**
	 * Spawns a water body through the editor's Water actor factory (project water materials, waves, spline defaults, and an
	 * automatically created Water Zone), with Affects Landscape switched off *before* the editor's "actor added" handler runs.
	 * That handler otherwise creates a landscape Water brush + "Water" edit layer, which triggers a landscape assertion
	 * (LandscapeEditLayers.cpp: GetLayerUpdateFlagPerMode() == 0) and crashes the editor. Call inside a transaction.
	 */
	AWaterBody* SpawnWaterBody(UWorld* World, UClass* Class, const FVector& LocationCm, const FRotator& Rotation, const FString& Label, FString& OutError)
	{
		const FDelegateHandle PreSpawnHandle = World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateLambda([Class](AActor* Actor)
		{
			if (Actor && Actor->IsA(Class))
			{
				if (UWaterBodyComponent* Component = CastChecked<AWaterBody>(Actor)->GetWaterBodyComponent())
				{
					Component->bAffectsLandscape = false;
				}
			}
		}));
		ON_SCOPE_EXIT { World->RemoveOnActorPreSpawnInitialization(PreSpawnHandle); };

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.InitialActorLabel = Label;
		const FTransform Transform(Rotation, LocationCm);

		AActor* Actor = nullptr;
		if (UActorFactory* Factory = GEditor ? GEditor->FindActorFactoryForActorClass(Class) : nullptr)
		{
			Actor = Factory->CreateActor(Class, World->PersistentLevel, Transform, Params);
		}
		else
		{
			Actor = World->SpawnActor(Class, &Transform, Params);
		}

		AWaterBody* Body = Cast<AWaterBody>(Actor);
		if (!Body || !Body->GetWaterBodyComponent())
		{
			OutError = TEXT("Failed to spawn the water body. Is the Water plugin enabled?");
			return nullptr;
		}
		Body->SetActorLabel(Label);
		// Belt and braces: never deform the landscape from these tools.
		Body->GetWaterBodyComponent()->bAffectsLandscape = false;
		return Body;
	}

	/** Replaces a water body's spline (world-space points) and per-point river metadata, then rebuilds the body. */
	void ApplySpline(AWaterBody* Body, const TArray<FVector>& WorldPointsCm, double WidthCm, double DepthCm, double Velocity)
	{
		UWaterSplineComponent* Spline = Body->GetWaterSpline();
		if (!Spline)
		{
			return;
		}
		Body->Modify();
		Spline->Modify();

		const FTransform ToWorld = Spline->GetComponentTransform();
		TArray<FVector> LocalPoints;
		for (const FVector& P : WorldPointsCm)
		{
			LocalPoints.Add(ToWorld.InverseTransformPosition(P));
		}
		Spline->ResetSpline(LocalPoints);

		if (UWaterSplineMetadata* Meta = Body->GetWaterSplineMetadata())
		{
			Meta->Modify();
			auto Fill = [&](FInterpCurveFloat& Curve, float Value)
			{
				Curve.Points.Reset();
				for (int32 I = 0; I < LocalPoints.Num(); ++I)
				{
					Curve.Points.Add(FInterpCurvePoint<float>(float(I), Value));
				}
			};
			Fill(Meta->Depth, float(DepthCm));
			Fill(Meta->RiverWidth, float(WidthCm));
			Fill(Meta->WaterVelocityScalar, float(Velocity));
		}

		Spline->K2_SynchronizeAndBroadcastDataChange();
		FOnWaterBodyChangedParams Changed;
		Changed.bShapeOrPositionChanged = true;
		Body->GetWaterBodyComponent()->UpdateAll(Changed);
	}

	TArray<FVector> CircleCm(const FVector2D& CenterCm, double RadiusCm, double ZCm)
	{
		TArray<FVector> Points;
		for (int32 I = 0; I < CirclePoints; ++I)
		{
			const double A = 2.0 * PI * I / CirclePoints;
			Points.Add(FVector(CenterCm.X + RadiusCm * FMath::Cos(A), CenterCm.Y + RadiusCm * FMath::Sin(A), ZCm));
		}
		return Points;
	}

	bool CheckEditable(UWorld*& OutWorld, FWorldBuilderWaterResult& Result)
	{
		OutWorld = GetEditorWorld();
		if (!OutWorld)
		{
			Result.Message = TEXT("No level is open in the editor.");
			return false;
		}
		if (GEditor && GEditor->PlayWorld)
		{
			Result.Message = TEXT("Stop Play-In-Editor before adding water.");
			return false;
		}
		return true;
	}
}

FString UWaterTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderWaterResult UWaterTools::CreateOcean(double SeaLevelM, const FString& Label)
{
	FWorldBuilderWaterResult Result;
	UWorld* World = nullptr;
	if (!CheckEditable(World, Result))
	{
		return Result;
	}
	for (TActorIterator<AWaterBodyOcean> It(World); It; ++It)
	{
		Result.Message = FString::Printf(TEXT("The level already has an ocean ('%s'). Only one ocean is supported; RemoveWaterBody it first to replace it."), *It->GetActorLabel());
		return Result;
	}

	// Centre on (and later cover) all landscapes.
	FBox LandBounds(ForceInit);
	for (ALandscape* Landscape : LandscapeUtils::GetAllLandscapes(World))
	{
		LandBounds += LandscapeUtils::GetCompleteBounds(Landscape);
	}
	const FVector2D CenterCm = LandBounds.IsValid ? FVector2D(LandBounds.GetCenter()) : FVector2D::ZeroVector;

	FScopedTransaction Transaction(LOCTEXT("CreateOcean", "AI: Create Ocean"));
	FString Error;
	AWaterBody* Ocean = SpawnWaterBody(World, AWaterBodyOcean::StaticClass(), FVector(CenterCm.X, CenterCm.Y, MetersToCm(SeaLevelM)), FRotator::ZeroRotator,
		UniqueLabel(World, Label, TEXT("Ocean")), Error);
	if (!Ocean)
	{
		Transaction.Cancel();
		Result.Message = Error;
		return Result;
	}

	// The auto-created water zone is sized from saved landscape bounds; make sure it covers everything with a margin.
	for (TActorIterator<AWaterZone> It(World); It; ++It)
	{
		AWaterZone* Zone = *It;
		if (LandBounds.IsValid)
		{
			Zone->Modify();
			const FVector2D Needed = FVector2D(LandBounds.GetSize()) * 1.5;
			const FVector2D Current = Zone->GetZoneExtent();
			Zone->SetActorLocation(FVector(CenterCm.X, CenterCm.Y, Zone->GetActorLocation().Z));
			Zone->SetZoneExtent(FVector2D::Max(Current, Needed));
		}
		if (UWaterBodyOceanComponent* OceanComponent = Cast<UWaterBodyOceanComponent>(Ocean->GetWaterBodyComponent()))
		{
			const double CollisionHeight = OceanComponent->GetCollisionExtents().Z;
			OceanComponent->SetCollisionExtents(FVector(Zone->GetZoneExtent() / 2.0, CollisionHeight));
			OceanComponent->FillWaterZoneWithOcean();
		}
		break;
	}
	FOnWaterBodyChangedParams Changed;
	Changed.bShapeOrPositionChanged = true;
	Ocean->GetWaterBodyComponent()->UpdateAll(Changed);

	Result.WaterBodies.Add(Describe(Ocean));
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created ocean '%s' at sea level %.1f m covering the water zone. Terrain above %.1f m stays dry; sculpt beaches a few meters above it. Save all to keep it.%s"),
		*Ocean->GetActorLabel(), SeaLevelM, SeaLevelM, *LandscapeBrushWarning(World));
	return Result;
}

FWorldBuilderWaterResult UWaterTools::CreateLake(const TArray<FVector2D>& OutlineM, double CenterXM, double CenterYM, double RadiusM, double WaterLevelM, const FString& Label)
{
	FWorldBuilderWaterResult Result;
	UWorld* World = nullptr;
	if (!CheckEditable(World, Result))
	{
		return Result;
	}
	const double ZCm = MetersToCm(WaterLevelM);
	TArray<FVector> Points;
	if (OutlineM.IsEmpty())
	{
		if (RadiusM <= 0.0)
		{
			Result.Message = TEXT("Give OutlineM (3+ points) or a RadiusM greater than 0.");
			return Result;
		}
		Points = CircleCm(FVector2D(CenterXM, CenterYM) * CmPerMeter, MetersToCm(RadiusM), ZCm);
	}
	else
	{
		if (OutlineM.Num() < 3)
		{
			Result.Message = TEXT("OutlineM needs at least 3 points.");
			return Result;
		}
		for (const FVector2D& P : OutlineM)
		{
			Points.Add(FVector(P.X * CmPerMeter, P.Y * CmPerMeter, ZCm));
		}
	}
	FVector Centroid = FVector::ZeroVector;
	for (const FVector& P : Points) { Centroid += P; }
	Centroid /= Points.Num();

	// Warn if the basin floor isn't below the water level (the lake would sit on top of the ground).
	const TOptional<double> Floor = GroundCm(World, FVector2D(Centroid));
	FString Note;
	if (Floor.IsSet() && Floor.GetValue() >= ZCm)
	{
		Note = FString::Printf(TEXT(" Note: the ground at the centre (%.1f m) is not below the water level; carve or flatten a basin first so the water has depth."), CmToMeters(Floor.GetValue()));
	}

	FScopedTransaction Transaction(LOCTEXT("CreateLake", "AI: Create Lake"));
	FString Error;
	AWaterBody* Lake = SpawnWaterBody(World, AWaterBodyLake::StaticClass(), Centroid, FRotator::ZeroRotator, UniqueLabel(World, Label, TEXT("Lake")), Error);
	if (!Lake)
	{
		Transaction.Cancel();
		Result.Message = Error;
		return Result;
	}
	ApplySpline(Lake, Points, /*WidthCm=*/0.0, /*DepthCm=*/Floor.IsSet() ? FMath::Max(ZCm - Floor.GetValue(), 100.0) : 500.0, /*Velocity=*/0.0);

	Result.WaterBodies.Add(Describe(Lake));
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created lake '%s' with %d shore points at water level %.1f m.%s Save all to keep it.%s"),
		*Lake->GetActorLabel(), Points.Num(), WaterLevelM, *Note, *LandscapeBrushWarning(World));
	return Result;
}

FWorldBuilderWaterResult UWaterTools::CreateRiver(const TArray<FVector2D>& PointsM, double WidthM, double WaterDepthM, double FlowSpeed, const FString& Label)
{
	FWorldBuilderWaterResult Result;
	UWorld* World = nullptr;
	if (!CheckEditable(World, Result))
	{
		return Result;
	}
	if (PointsM.Num() < 2)
	{
		Result.Message = TEXT("PointsM needs at least 2 points, upstream first.");
		return Result;
	}
	if (WidthM <= 0.0 || WaterDepthM <= 0.0)
	{
		Result.Message = TEXT("WidthM and WaterDepthM must be greater than 0.");
		return Result;
	}

	// Water surface = ground under each point (the carved bed) + depth.
	TArray<FVector> Points;
	int32 Uphill = 0;
	for (const FVector2D& P : PointsM)
	{
		const FVector2D Cm = P * CmPerMeter;
		const TOptional<double> Ground = GroundCm(World, Cm);
		if (!Ground.IsSet())
		{
			Result.Message = FString::Printf(TEXT("Point (%.0f, %.0f) m is not on a loaded landscape."), P.X, P.Y);
			return Result;
		}
		const FVector Point(Cm.X, Cm.Y, Ground.GetValue() + MetersToCm(WaterDepthM));
		if (!Points.IsEmpty() && Point.Z > Points.Last().Z + 50.0)
		{
			++Uphill;
		}
		Points.Add(Point);
	}

	FScopedTransaction Transaction(LOCTEXT("CreateRiver", "AI: Create River"));
	FString Error;
	AWaterBody* River = SpawnWaterBody(World, AWaterBodyRiver::StaticClass(), Points[0], FRotator::ZeroRotator, UniqueLabel(World, Label, TEXT("River")), Error);
	if (!River)
	{
		Transaction.Cancel();
		Result.Message = Error;
		return Result;
	}
	ApplySpline(River, Points, MetersToCm(WidthM), MetersToCm(WaterDepthM), FMath::Max(FlowSpeed, 0.0));

	Result.WaterBodies.Add(Describe(River));
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created river '%s': %d points, %.0f m wide, %.1f m deep, from %.1f m down to %.1f m.%s Save all to keep it.%s"),
		*River->GetActorLabel(), Points.Num(), WidthM, WaterDepthM, CmToMeters(Points[0].Z), CmToMeters(Points.Last().Z),
		Uphill > 0 ? *FString::Printf(TEXT(" Warning: %d segment(s) flow uphill; carve the bed deeper there (CarvePath) or reorder points."), Uphill) : TEXT(""),
		*LandscapeBrushWarning(World));
	return Result;
}

FWorldBuilderWaterResult UWaterTools::CreateWaterfall(double TopXM, double TopYM, double BottomXM, double BottomYM, double WidthM, double PoolRadiusM,
	double WaterDepthM, const FString& Label)
{
	FWorldBuilderWaterResult Result;
	UWorld* World = nullptr;
	if (!CheckEditable(World, Result))
	{
		return Result;
	}
	const FVector2D TopCm = FVector2D(TopXM, TopYM) * CmPerMeter;
	const FVector2D BottomCm = FVector2D(BottomXM, BottomYM) * CmPerMeter;
	const TOptional<double> TopZ = GroundCm(World, TopCm);
	const TOptional<double> BottomZ = GroundCm(World, BottomCm);
	if (!TopZ.IsSet() || !BottomZ.IsSet())
	{
		Result.Message = TEXT("Both ends of the waterfall must be on a loaded landscape.");
		return Result;
	}
	if (TopZ.GetValue() - BottomZ.GetValue() < 200.0)
	{
		Result.Message = FString::Printf(TEXT("There is no drop: the top is at %.1f m and the bottom at %.1f m. Make a cliff first (ApplyShape Plateau/Mesa, RaiseLower) or move the points."),
			CmToMeters(TopZ.GetValue()), CmToMeters(BottomZ.GetValue()));
		return Result;
	}

	const double DepthCm = MetersToCm(FMath::Max(WaterDepthM, 0.1));
	const FVector2D Dir = (BottomCm - TopCm).GetSafeNormal();
	const double LeadCm = FMath::Max(MetersToCm(WidthM), 1000.0);
	const FVector2D UpstreamCm = TopCm - Dir * LeadCm;
	const double UpstreamZ = GroundCm(World, UpstreamCm).Get(TopZ.GetValue());
	const TArray<FVector> Points = {
		FVector(UpstreamCm.X, UpstreamCm.Y, UpstreamZ + DepthCm),
		FVector(TopCm.X, TopCm.Y, TopZ.GetValue() + DepthCm),
		FVector(BottomCm.X, BottomCm.Y, BottomZ.GetValue() + DepthCm),
	};

	FScopedTransaction Transaction(LOCTEXT("CreateWaterfall", "AI: Create Waterfall"));
	FString Error;
	const FString BaseLabel = UniqueLabel(World, Label, TEXT("Waterfall"));
	AWaterBody* Fall = SpawnWaterBody(World, AWaterBodyRiver::StaticClass(), Points[0], FRotator::ZeroRotator, BaseLabel, Error);
	if (!Fall)
	{
		Transaction.Cancel();
		Result.Message = Error;
		return Result;
	}
	ApplySpline(Fall, Points, MetersToCm(WidthM), DepthCm, /*Velocity=*/4.0);
	Result.WaterBodies.Add(Describe(Fall));

	if (PoolRadiusM > 0.0)
	{
		const double PoolLevel = BottomZ.GetValue() + DepthCm;
		AWaterBody* Pool = SpawnWaterBody(World, AWaterBodyLake::StaticClass(), FVector(BottomCm.X, BottomCm.Y, PoolLevel), FRotator::ZeroRotator,
			UniqueLabel(World, BaseLabel + TEXT("_Pool"), BaseLabel + TEXT("_Pool")), Error);
		if (!Pool)
		{
			Transaction.Cancel();
			Result.Message = Error;
			return Result;
		}
		ApplySpline(Pool, CircleCm(BottomCm, MetersToCm(PoolRadiusM), PoolLevel), 0.0, DepthCm * 2.0, 0.0);
		Result.WaterBodies.Add(Describe(Pool));
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created waterfall '%s' dropping %.1f m (from %.1f to %.1f m), %.0f m wide%s. For a natural plunge pool, lower the ground there first (ApplyShape Crater or RaiseLower). Save all to keep it.%s"),
		*BaseLabel, CmToMeters(TopZ.GetValue() - BottomZ.GetValue()), CmToMeters(TopZ.GetValue()), CmToMeters(BottomZ.GetValue()), WidthM,
		PoolRadiusM > 0.0 ? *FString::Printf(TEXT(" with a %.0f m plunge pool"), PoolRadiusM) : TEXT(""), *LandscapeBrushWarning(World));
	return Result;
}

FWorldBuilderWaterResult UWaterTools::CreateCustomWater(double CenterXM, double CenterYM, double WaterLevelM, double SizeXM, double SizeYM, double YawDeg, const FString& Label)
{
	FWorldBuilderWaterResult Result;
	UWorld* World = nullptr;
	if (!CheckEditable(World, Result))
	{
		return Result;
	}
	if (SizeXM <= 0.0 || SizeYM <= 0.0)
	{
		Result.Message = TEXT("SizeXM and SizeYM must be greater than 0.");
		return Result;
	}

	FScopedTransaction Transaction(LOCTEXT("CreateCustomWater", "AI: Create Custom Water"));
	FString Error;
	const FVector Location(MetersToCm(CenterXM), MetersToCm(CenterYM), MetersToCm(WaterLevelM));
	AWaterBody* Body = SpawnWaterBody(World, AWaterBodyCustom::StaticClass(), Location, FRotator(0.0, YawDeg, 0.0), UniqueLabel(World, Label, TEXT("Water")), Error);
	if (!Body)
	{
		Transaction.Cancel();
		Result.Message = Error;
		return Result;
	}

	UWaterBodyComponent* Component = Body->GetWaterBodyComponent();
	UStaticMesh* Mesh = Component->GetWaterMeshOverride();
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		Component->SetWaterMeshOverride(Mesh);
	}
	// Scale the water mesh so its footprint matches the requested size.
	const FVector MeshSize = Mesh ? Mesh->GetBoundingBox().GetSize() : FVector(100.0);
	Body->Modify();
	Body->SetActorScale3D(FVector(MetersToCm(SizeXM) / FMath::Max(MeshSize.X, 1.0), MetersToCm(SizeYM) / FMath::Max(MeshSize.Y, 1.0), 1.0));
	FOnWaterBodyChangedParams Changed;
	Changed.bShapeOrPositionChanged = true;
	Component->UpdateAll(Changed);

	Result.WaterBodies.Add(Describe(Body));
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created custom water '%s' (%.0f x %.0f m) at %.1f m. It renders on its own (no water zone), so it works for pools, fountains and underground water; caves themselves need geometry. Save all to keep it.%s"),
		*Body->GetActorLabel(), SizeXM, SizeYM, WaterLevelM, *LandscapeBrushWarning(World));
	return Result;
}

FWorldBuilderWaterResult UWaterTools::ListWaterBodies()
{
	FWorldBuilderWaterResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	for (TActorIterator<AWaterBody> It(World); It; ++It)
	{
		Result.WaterBodies.Add(Describe(*It));
	}
	int32 Zones = 0;
	for (TActorIterator<AWaterZone> It(World); It; ++It) { ++Zones; }
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%d water body(ies), %d water zone(s).%s"), Result.WaterBodies.Num(), Zones, *LandscapeBrushWarning(World));
	return Result;
}

FWorldBuilderWaterResult UWaterTools::RemoveWaterBody(const FString& Label)
{
	FWorldBuilderWaterResult Result;
	UWorld* World = nullptr;
	if (!CheckEditable(World, Result))
	{
		return Result;
	}
	for (TActorIterator<AWaterBody> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
		{
			FScopedTransaction Transaction(LOCTEXT("RemoveWaterBody", "AI: Remove Water Body"));
			Result.WaterBodies.Add(Describe(*It));
			It->Modify();
			World->EditorDestroyActor(*It, /*bShouldModifyLevel=*/true);
			Result.bSuccess = true;
			Result.Message = FString::Printf(TEXT("Removed water body '%s'."), *Label);
			return Result;
		}
	}
	Result.Message = FString::Printf(TEXT("No water body labelled '%s'. Use ListWaterBodies."), *Label);
	return Result;
}

#undef LOCTEXT_NAMESPACE
