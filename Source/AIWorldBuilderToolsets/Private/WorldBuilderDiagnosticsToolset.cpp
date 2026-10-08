#include "WorldBuilderDiagnosticsToolset.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageType.h"
#include "GameFramework/PlayerStart.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "LandscapeEditLayer.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeUtils.h"
#include "Materials/MaterialInterface.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"

using namespace AIWorldBuilder;

namespace
{
	/** Default UCharacterMovementComponent::WalkableFloorAngle. */
	constexpr double DefaultWalkableAngleDeg = 44.765;
	constexpr int32 HeightScanPoints = 48;
	constexpr int32 TopActorClasses = 20;

	FWorldBuilderLandscapeOverview DescribeLandscape(ALandscape* Landscape)
	{
		FWorldBuilderLandscapeOverview O;
		O.Name = LandscapeUtils::GetDisplayName(Landscape);
		const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
		if (Bounds.IsValid)
		{
			O.BoundsMinM = Bounds.Min / CmPerMeter;
			O.BoundsMaxM = Bounds.Max / CmPerMeter;
		}
		O.SampleSpacingM = CmToMeters(LandscapeUtils::GetSampleSpacingCm(Landscape));
		const FTransform T = Landscape->LandscapeActorToWorld();
		double MinCm = 0.0, MaxCm = 0.0;
		LandscapeMath::GetHeightRangeCm(T.GetScale3D().Z, T.GetLocation().Z, MinCm, MaxCm);
		O.MinPossibleHeightM = CmToMeters(MinCm);
		O.MaxPossibleHeightM = CmToMeters(MaxCm);

		// Coarse height scan of loaded data.
		double Lo = TNumericLimits<double>::Max(), Hi = TNumericLimits<double>::Lowest();
		if (Bounds.IsValid)
		{
			for (int32 IY = 0; IY < HeightScanPoints; ++IY)
			{
				for (int32 IX = 0; IX < HeightScanPoints; ++IX)
				{
					const FVector2D P(FMath::Lerp(Bounds.Min.X, Bounds.Max.X, (IX + 0.5) / HeightScanPoints),
						FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, (IY + 0.5) / HeightScanPoints));
					if (const TOptional<double> H = LandscapeUtils::SampleHeightCm(Landscape, P))
					{
						Lo = FMath::Min(Lo, H.GetValue());
						Hi = FMath::Max(Hi, H.GetValue());
					}
				}
			}
		}
		if (Lo <= Hi)
		{
			O.MinHeightM = CmToMeters(Lo);
			O.MaxHeightM = CmToMeters(Hi);
		}

		if (Landscape->LandscapeMaterial)
		{
			O.MaterialPath = Landscape->LandscapeMaterial->GetPathName();
		}
		for (const ULandscapeEditLayerBase* Layer : Landscape->GetEditLayersConst())
		{
			if (Layer)
			{
				O.EditLayers.Add(Layer->GetName().ToString());
			}
		}

		TSet<FName> Seen;
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape->GetTargetLayers())
		{
			const ULandscapeLayerInfoObject* Info = Pair.Value.LayerInfoObj;
			if (Info && UE::Landscape::IsVisibilityLayer(Info))
			{
				continue;
			}
			Seen.Add(Pair.Key);
			O.PaintLayers.Add(Info ? Pair.Key.ToString() : Pair.Key.ToString() + TEXT(" (no layer info)"));
		}
		for (const FName& Name : Landscape->RetrieveTargetLayerNamesFromMaterials(/*bIncludeVisibilityLayer=*/false))
		{
			if (!Seen.Contains(Name))
			{
				O.PaintLayers.Add(Name.ToString() + TEXT(" (no layer info)"));
			}
		}
		return O;
	}
}

FString UWorldBuilderDiagnosticsToolset::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderPluginStatus UWorldBuilderDiagnosticsToolset::GetPluginStatus()
{
	FWorldBuilderPluginStatus Status;
	Status.Version = GetPluginVersion();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Status.Message = TEXT("No level is open in the editor.");
		return Status;
	}

	Status.LevelName = World->GetMapName();
	Status.bWorldPartitionEnabled = World->IsPartitionedWorld();

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		++Status.LandscapeCount;
	}

	Status.bSuccess = true;
	Status.Message = FString::Printf(TEXT("AI World Builder %s loaded. Level '%s' has %d landscape(s); World Partition %s."),
		*Status.Version, *Status.LevelName, Status.LandscapeCount,
		Status.bWorldPartitionEnabled ? TEXT("enabled") : TEXT("disabled"));
	return Status;
}

FWorldBuilderWorldDescription UWorldBuilderDiagnosticsToolset::DescribeWorld()
{
	FWorldBuilderWorldDescription D;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		D.Message = TEXT("No level is open in the editor.");
		return D;
	}
	D.LevelName = World->GetMapName();
	D.bWorldPartition = World->IsPartitionedWorld();

	for (ALandscape* Landscape : LandscapeUtils::GetAllLandscapes(World))
	{
		D.Landscapes.Add(DescribeLandscape(Landscape));
	}

	// Foliage totals per type.
	TMap<UFoliageType*, int32> FoliageTotals;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		It->ForEachFoliageInfo([&FoliageTotals](UFoliageType* Type, FFoliageInfo& Info)
		{
			FoliageTotals.FindOrAdd(Type) += Info.Instances.Num();
			return true;
		});
	}
	int32 FoliageInstances = 0;
	for (const TPair<UFoliageType*, int32>& Pair : FoliageTotals)
	{
		FWorldBuilderFoliageCount& C = D.Foliage.AddDefaulted_GetRef();
		C.FoliageType = Pair.Key->GetPathName();
		const UObject* Source = Pair.Key->GetSource();
		C.Mesh = Source ? Source->GetPathName() : FString();
		C.Count = Pair.Value;
		FoliageInstances += Pair.Value;
	}

	// PCG volumes.
	for (TActorIterator<APCGVolume> It(World); It; ++It)
	{
		FWorldBuilderPCGVolumeInfo& V = D.PCGVolumes.AddDefaulted_GetRef();
		V.Label = It->GetActorLabel();
		const FBox Bounds = It->GetComponentsBoundingBox(/*bNonColliding=*/true);
		V.BoundsMinM = Bounds.Min / CmPerMeter;
		V.BoundsMaxM = Bounds.Max / CmPerMeter;
		if (UPCGComponent* Component = It->PCGComponent)
		{
			if (UPCGGraph* Graph = Component->GetGraph())
			{
				V.GraphPath = Graph->GetPathName();
			}
			V.Seed = Component->Seed;
			V.bGenerated = Component->bGenerated;
		}
	}

	// Lighting, player starts and actor classes.
	TMap<FString, int32> ClassCounts;
	for (FActorIterator It(World); It; ++It)
	{
		AActor* Actor = *It;
		++D.TotalActorCount;
		const FString ClassName = Actor->GetClass()->GetName();
		ClassCounts.FindOrAdd(ClassName) += 1;

		if (ADirectionalLight* Sun = Cast<ADirectionalLight>(Actor))
		{
			if (!D.Lighting.bHasDirectionalLight)
			{
				D.Lighting.bHasDirectionalLight = true;
				D.Lighting.SunPitchDeg = Sun->GetActorRotation().Pitch;
				D.Lighting.SunYawDeg = Sun->GetActorRotation().Yaw;
				if (const ULightComponent* Light = Sun->GetLightComponent())
				{
					D.Lighting.SunIntensity = Light->Intensity;
				}
			}
		}
		else if (APlayerStart* Start = Cast<APlayerStart>(Actor))
		{
			D.PlayerStartsM.Add(Start->GetActorLocation() / CmPerMeter);
		}
		D.Lighting.bHasSkyLight |= ClassName == TEXT("SkyLight");
		D.Lighting.bHasSkyAtmosphere |= ClassName == TEXT("SkyAtmosphere");
		D.Lighting.bHasHeightFog |= ClassName == TEXT("ExponentialHeightFog");
		D.Lighting.bHasVolumetricClouds |= ClassName == TEXT("VolumetricCloud");
		D.Lighting.PostProcessVolumeCount += ClassName == TEXT("PostProcessVolume") ? 1 : 0;
	}
	ClassCounts.ValueSort([](int32 A, int32 B) { return A > B; });
	for (const TPair<FString, int32>& Pair : ClassCounts)
	{
		if (D.ActorClasses.Num() >= TopActorClasses)
		{
			break;
		}
		FWorldBuilderActorClassCount& Entry = D.ActorClasses.AddDefaulted_GetRef();
		Entry.ClassName = Pair.Key;
		Entry.Count = Pair.Value;
	}

	// Plain-text summary.
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Level '%s' (World Partition %s), %d loaded actors."), *D.LevelName, D.bWorldPartition ? TEXT("on") : TEXT("off"), D.TotalActorCount));
	if (D.Landscapes.IsEmpty())
	{
		Lines.Add(TEXT("No landscape. Use LandscapeCreateTools.CreateLandscape to make one."));
	}
	for (const FWorldBuilderLandscapeOverview& L : D.Landscapes)
	{
		Lines.Add(FString::Printf(TEXT("Landscape '%s': %.0f x %.0f m at %.2f m spacing, from (%.0f, %.0f) to (%.0f, %.0f) m. Terrain %.1f to %.1f m (limits %.0f to %.0f m). Material: %s. Edit layers: %s. Paint layers: %s."),
			*L.Name, L.BoundsMaxM.X - L.BoundsMinM.X, L.BoundsMaxM.Y - L.BoundsMinM.Y, L.SampleSpacingM,
			L.BoundsMinM.X, L.BoundsMinM.Y, L.BoundsMaxM.X, L.BoundsMaxM.Y,
			L.MinHeightM, L.MaxHeightM, L.MinPossibleHeightM, L.MaxPossibleHeightM,
			L.MaterialPath.IsEmpty() ? TEXT("none") : *L.MaterialPath,
			L.EditLayers.IsEmpty() ? TEXT("none") : *FString::Join(L.EditLayers, TEXT(", ")),
			L.PaintLayers.IsEmpty() ? TEXT("none") : *FString::Join(L.PaintLayers, TEXT(", "))));
	}
	Lines.Add(FString::Printf(TEXT("Foliage: %d instance(s) of %d type(s). PCG volumes: %d."), FoliageInstances, D.Foliage.Num(), D.PCGVolumes.Num()));
	Lines.Add(FString::Printf(TEXT("Lighting: sun %s, sky light %s, sky atmosphere %s, height fog %s, clouds %s, %d post process volume(s)."),
		D.Lighting.bHasDirectionalLight ? *FString::Printf(TEXT("pitch %.0f yaw %.0f intensity %.1f"), D.Lighting.SunPitchDeg, D.Lighting.SunYawDeg, D.Lighting.SunIntensity) : TEXT("missing"),
		D.Lighting.bHasSkyLight ? TEXT("yes") : TEXT("no"), D.Lighting.bHasSkyAtmosphere ? TEXT("yes") : TEXT("no"),
		D.Lighting.bHasHeightFog ? TEXT("yes") : TEXT("no"), D.Lighting.bHasVolumetricClouds ? TEXT("yes") : TEXT("no"),
		D.Lighting.PostProcessVolumeCount));
	Lines.Add(FString::Printf(TEXT("Player starts: %d."), D.PlayerStartsM.Num()));
	D.Message = FString::Join(Lines, TEXT("\n"));
	D.bSuccess = true;
	return D;
}

FWorldBuilderGroundTraceResult UWorldBuilderDiagnosticsToolset::TraceGround(double XM, double YM)
{
	FWorldBuilderGroundTraceResult R;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		R.Message = TEXT("No level is open in the editor.");
		return R;
	}

	const FVector2D PointCm = FVector2D(XM, YM) * CmPerMeter;

	// Trace from above the highest landscape to below the lowest (or a generous default).
	double TopCm = 1000000.0, BottomCm = -1000000.0;
	FBox AllBounds(ForceInit);
	for (ALandscape* Landscape : LandscapeUtils::GetAllLandscapes(World))
	{
		AllBounds += LandscapeUtils::GetCompleteBounds(Landscape);
	}
	if (AllBounds.IsValid)
	{
		TopCm = AllBounds.Max.Z + 50000.0;
		BottomCm = AllBounds.Min.Z - 50000.0;
	}

	FString Ignored;
	if (const ALandscape* Landscape = LandscapeUtils::Resolve(World, TEXT("auto"), &PointCm, Ignored))
	{
		if (const TOptional<double> H = LandscapeUtils::SampleHeightCm(Landscape, PointCm))
		{
			R.LandscapeHeightM = CmToMeters(H.GetValue());
		}
	}

	// ECC_Pawn: the channel a player's capsule collides on, so this hits exactly what would block the player.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AIWorldBuilderTraceGround), /*bTraceComplex=*/false);
	const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(PointCm.X, PointCm.Y, TopCm), FVector(PointCm.X, PointCm.Y, BottomCm), ECC_Pawn, Params);

	R.bSuccess = true;
	if (!bHit)
	{
		R.Message = FString::Printf(TEXT("Nothing blocks the player at (%.1f, %.1f) m: no collision. If there should be landscape here, it may be unloaded (World Partition) or have no collision."), XM, YM);
		return R;
	}

	R.bHit = true;
	R.HitLocationM = Hit.ImpactPoint / CmPerMeter;
	R.HitActor = Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : FString();
	R.HitComponentClass = Hit.GetComponent() ? Hit.GetComponent()->GetClass()->GetName() : FString();
	R.bHitLandscape = Hit.GetComponent() && Hit.GetComponent()->IsA<ULandscapeHeightfieldCollisionComponent>();
	R.SurfaceSlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0, 1.0)));
	R.bWalkable = R.SurfaceSlopeDeg <= DefaultWalkableAngleDeg;
	R.Message = FString::Printf(TEXT("At (%.1f, %.1f) m the player lands on %s ('%s', %s) at %.2f m; slope %.1f deg, %s.%s"),
		XM, YM, R.bHitLandscape ? TEXT("landscape") : TEXT("an object"), *R.HitActor, *R.HitComponentClass, R.HitLocationM.Z,
		R.SurfaceSlopeDeg, R.bWalkable ? TEXT("walkable") : TEXT("too steep to walk on"),
		(!R.bHitLandscape && R.LandscapeHeightM != 0.0) ? *FString::Printf(TEXT(" Landscape is at %.2f m below it."), R.LandscapeHeightM) : TEXT(""));
	return R;
}
