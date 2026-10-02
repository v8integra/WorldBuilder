#include "FoliageScatterTools.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Math/RandomStream.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "AIWorldBuilderFoliage"

using namespace AIWorldBuilder;

namespace
{
	/** Safety cap on candidate positions per call. */
	constexpr int64 MaxCandidates = 2000000;
	constexpr double NoHeightLimitM = 99999.0;

	bool IsNone(const FString& Value)
	{
		return Value.IsEmpty() || Value.Equals(TEXT("none"), ESearchCase::IgnoreCase);
	}

	FString MeshPathOf(const UFoliageType* Type)
	{
		const UObject* Source = Type ? Type->GetSource() : nullptr;
		return Source ? Source->GetPathName() : FString();
	}

	/** Matches a foliage type against user-supplied mesh or foliage type paths (full path or bare asset name, case-insensitive). */
	bool MatchesAny(const UFoliageType* Type, const TArray<FString>& Paths)
	{
		if (Paths.IsEmpty())
		{
			return true;
		}
		const FString TypePath = Type->GetPathName();
		const FString MeshPath = MeshPathOf(Type);
		for (const FString& P : Paths)
		{
			const FString Name = FPackageName::ObjectPathToObjectName(P);
			if (TypePath.Equals(P, ESearchCase::IgnoreCase) || MeshPath.Equals(P, ESearchCase::IgnoreCase)
				|| TypePath.EndsWith(TEXT(".") + Name) || MeshPath.EndsWith(TEXT(".") + Name))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Resolves a path to a foliage type: an existing Foliage Type asset, or a static mesh for which a
	 * FT_<Mesh> asset is found or created in Folder.
	 */
	UFoliageType* ResolveFoliageType(const FString& Path, const FString& Folder, bool& bOutCreated, FString& OutError)
	{
		bOutCreated = false;
		if (UFoliageType* Existing = LoadObject<UFoliageType>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Mesh)
		{
			OutError = FString::Printf(TEXT("'%s' is not a static mesh or foliage type asset. Use a full path like /Game/Trees/SM_Pine.SM_Pine (AssetTools can search)."), *Path);
			return nullptr;
		}

		const FString AssetName = TEXT("FT_") + Mesh->GetName();
		const FString PackageName = Folder / AssetName;
		if (UFoliageType* Found = LoadObject<UFoliageType>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Found;
		}

		UPackage* Package = CreatePackage(*PackageName);
		UFoliageType_InstancedStaticMesh* Type = NewObject<UFoliageType_InstancedStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		Type->SetStaticMesh(Mesh);
		FAssetRegistryModule::AssetCreated(Type);
		Package->MarkPackageDirty();
		bOutCreated = true;
		return Type;
	}

	/** Paint layer info by name on a landscape (target layers), or nullptr. */
	ULandscapeLayerInfoObject* FindLayerInfo(const ALandscape* Landscape, const FString& LayerName)
	{
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape->GetTargetLayers())
		{
			if (Pair.Key.ToString().Equals(LayerName, ESearchCase::IgnoreCase))
			{
				return Pair.Value.LayerInfoObj;
			}
		}
		return nullptr;
	}

	/** Final (merged) paint weight 0..1 of a layer at a world point. */
	float LayerWeightAt(const ALandscape* Landscape, ULandscapeInfo* Info, ULandscapeLayerInfoObject* LayerInfo, const FVector& WorldCm)
	{
		const FVector Local = Landscape->LandscapeActorToWorld().InverseTransformPosition(WorldCm);
		const int32 CSQ = FMath::Max(Info->ComponentSizeQuads, 1);
		const FIntPoint Key(FMath::FloorToInt32(Local.X / CSQ), FMath::FloorToInt32(Local.Y / CSQ));
		ULandscapeComponent* Component = Info->XYtoComponentMap.FindRef(Key);
		return Component ? Component->GetLayerWeightAtLocation(WorldCm, LayerInfo) : 0.0f;
	}
}

FString UFoliageScatterTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderFoliageResult UFoliageScatterTools::ScatterFoliage(const TArray<FString>& MeshPaths, const TArray<FWorldBuilderCircle>& ExcludeAreas,
	double CenterXM, double CenterYM, double SizeXM, double SizeYM, double DensityPerHectare,
	double MinSlopeDeg, double MaxSlopeDeg, double MinHeightM, double MaxHeightM,
	const FString& LayerName, double MinLayerWeight, double MinScale, double MaxScale,
	bool bAlignToNormal, double SinkM, int32 Seed, const FString& FoliageFolder, const FString& LandscapeName)
{
	FWorldBuilderFoliageResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		Result.Message = TEXT("Stop Play-In-Editor before placing foliage.");
		return Result;
	}
	if (MeshPaths.IsEmpty())
	{
		Result.Message = TEXT("Give at least one mesh path in MeshPaths.");
		return Result;
	}
	if (DensityPerHectare <= 0.0)
	{
		Result.Message = TEXT("DensityPerHectare must be greater than 0.");
		return Result;
	}
	if ((SizeXM > 0.0) != (SizeYM > 0.0) || SizeXM < 0.0 || SizeYM < 0.0)
	{
		Result.Message = TEXT("Give both SizeXM and SizeYM (greater than 0), or leave both at 0 for the whole landscape.");
		return Result;
	}
	if (MinScale <= 0.0 || MaxScale < MinScale)
	{
		Result.Message = TEXT("Scales must be positive and MaxScale >= MinScale.");
		return Result;
	}
	FString Folder = FoliageFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	if (!Folder.StartsWith(TEXT("/Game")))
	{
		Result.Message = TEXT("FoliageFolder must be a content folder starting with /Game/.");
		return Result;
	}

	const bool bWhole = SizeXM <= 0.0;
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, bWhole ? nullptr : &CenterCm, Result.Message);
	if (!Landscape)
	{
		return Result;
	}
	Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);
	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();

	ULandscapeLayerInfoObject* LayerInfo = nullptr;
	if (!IsNone(LayerName))
	{
		LayerInfo = FindLayerInfo(Landscape, LayerName);
		if (!LayerInfo)
		{
			Result.Message = FString::Printf(TEXT("Paint layer '%s' not found or has no Layer Info. Use ListPaintLayers to see the layers."), *LayerName);
			return Result;
		}
	}

	FBox2D AreaCm;
	if (bWhole)
	{
		const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
		AreaCm = FBox2D(FVector2D(Bounds.Min.X, Bounds.Min.Y), FVector2D(Bounds.Max.X, Bounds.Max.Y));
	}
	else
	{
		const FVector2D Half = FVector2D(SizeXM, SizeYM) * (CmPerMeter * 0.5);
		AreaCm = FBox2D(CenterCm - Half, CenterCm + Half);
	}

	// Jittered grid: one candidate per cell of the size that gives the requested density.
	const double CellCm = MetersToCm(FMath::Sqrt(10000.0 / DensityPerHectare));
	const int64 CellsX = FMath::Max<int64>(1, FMath::FloorToInt64(AreaCm.GetSize().X / CellCm));
	const int64 CellsY = FMath::Max<int64>(1, FMath::FloorToInt64(AreaCm.GetSize().Y / CellCm));
	if (CellsX * CellsY > MaxCandidates)
	{
		Result.Message = FString::Printf(TEXT("That would test %lld positions (limit %lld). Use a smaller region or a lower density, and run several calls."), CellsX * CellsY, MaxCandidates);
		return Result;
	}

	const double SpacingCm = LandscapeUtils::GetSampleSpacingCm(Landscape);
	const double SinkCm = MetersToCm(SinkM);
	TArray<TArray<FTransform>> PerMesh;
	PerMesh.SetNum(MeshPaths.Num());
	int32 RejSlope = 0, RejHeight = 0, RejLayer = 0, RejExcluded = 0, RejNoGround = 0;

	for (int64 CY = 0; CY < CellsY; ++CY)
	{
		for (int64 CX = 0; CX < CellsX; ++CX)
		{
			FRandomStream Rand(HashCombine(GetTypeHash(Seed), GetTypeHash(CY * CellsX + CX)));
			const FVector2D P = AreaCm.Min + FVector2D((CX + Rand.FRand()) * CellCm, (CY + Rand.FRand()) * CellCm);
			++Result.CandidateCount;

			bool bExcluded = false;
			for (const FWorldBuilderCircle& Circle : ExcludeAreas)
			{
				if (FVector2D::DistSquared(P, FVector2D(Circle.XM, Circle.YM) * CmPerMeter) <= FMath::Square(MetersToCm(Circle.RadiusM)))
				{
					bExcluded = true;
					break;
				}
			}
			if (bExcluded)
			{
				++RejExcluded;
				continue;
			}

			const TOptional<double> Z = LandscapeUtils::SampleHeightCm(Landscape, P);
			if (!Z.IsSet())
			{
				++RejNoGround;
				continue;
			}
			const double HeightM = CmToMeters(Z.GetValue());
			if ((MinHeightM > -NoHeightLimitM && HeightM < MinHeightM) || (MaxHeightM < NoHeightLimitM && HeightM > MaxHeightM))
			{
				++RejHeight;
				continue;
			}

			const double XP = LandscapeUtils::SampleHeightCm(Landscape, P + FVector2D(SpacingCm, 0.0)).Get(Z.GetValue());
			const double XN = LandscapeUtils::SampleHeightCm(Landscape, P - FVector2D(SpacingCm, 0.0)).Get(Z.GetValue());
			const double YP = LandscapeUtils::SampleHeightCm(Landscape, P + FVector2D(0.0, SpacingCm)).Get(Z.GetValue());
			const double YN = LandscapeUtils::SampleHeightCm(Landscape, P - FVector2D(0.0, SpacingCm)).Get(Z.GetValue());
			const double DzDx = (XP - XN) / (2.0 * SpacingCm), DzDy = (YP - YN) / (2.0 * SpacingCm);
			const double Slope = LandscapeMath::SlopeDegrees(DzDx, DzDy);
			if (Slope < MinSlopeDeg || Slope > MaxSlopeDeg)
			{
				++RejSlope;
				continue;
			}

			const FVector Location(P.X, P.Y, Z.GetValue() - SinkCm);
			if (LayerInfo && LayerWeightAt(Landscape, Info, LayerInfo, FVector(P.X, P.Y, Z.GetValue())) < MinLayerWeight)
			{
				++RejLayer;
				continue;
			}

			const FRotator Yaw(0.0, Rand.FRandRange(0.0, 360.0), 0.0);
			FQuat Rotation = Yaw.Quaternion();
			if (bAlignToNormal)
			{
				const FVector Normal = FVector(-DzDx, -DzDy, 1.0).GetSafeNormal();
				Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal) * Rotation;
			}
			const double Scale = Rand.FRandRange(MinScale, MaxScale);
			const int32 MeshIndex = Rand.RandRange(0, MeshPaths.Num() - 1);
			PerMesh[MeshIndex].Add(FTransform(Rotation, Location, FVector(Scale)));
		}
	}

	Result.Rejected.Add(TEXT("slope"), RejSlope);
	Result.Rejected.Add(TEXT("height"), RejHeight);
	Result.Rejected.Add(TEXT("layer"), RejLayer);
	Result.Rejected.Add(TEXT("excluded"), RejExcluded);
	Result.Rejected.Add(TEXT("noGround"), RejNoGround);

	FScopedTransaction Transaction(LOCTEXT("ScatterFoliage", "AI: Scatter Foliage"));
	TArray<FString> CreatedTypes;
	for (int32 M = 0; M < MeshPaths.Num(); ++M)
	{
		bool bCreated = false;
		FString Error;
		UFoliageType* Type = ResolveFoliageType(MeshPaths[M], Folder, bCreated, Error);
		if (!Type)
		{
			Transaction.Cancel();
			Result.Message = Error;
			return Result;
		}
		if (bCreated)
		{
			CreatedTypes.Add(Type->GetPathName());
		}

		// Same grouping as AInstancedFoliageActor::AddInstances: each instance goes to the foliage actor
		// owning its location (one per World Partition cell).
		TArray<FFoliageInstance> Instances;
		Instances.Reserve(PerMesh[M].Num());
		TMap<AInstancedFoliageActor*, TArray<const FFoliageInstance*>> ByActor;
		for (const FTransform& T : PerMesh[M])
		{
			FFoliageInstance& Instance = Instances.AddDefaulted_GetRef();
			Instance.Location = T.GetLocation();
			Instance.Rotation = T.GetRotation().Rotator();
			Instance.DrawScale3D = FVector3f(T.GetScale3D());
		}
		for (const FFoliageInstance& Instance : Instances)
		{
			if (AInstancedFoliageActor* IFA = AInstancedFoliageActor::Get(World, /*bCreateIfNone=*/true, World->PersistentLevel, Instance.Location))
			{
				ByActor.FindOrAdd(IFA).Add(&Instance);
			}
		}
		for (const TPair<AInstancedFoliageActor*, TArray<const FFoliageInstance*>>& Pair : ByActor)
		{
			Pair.Key->Modify();
			FFoliageInfo* TypeInfo = nullptr;
			if (UFoliageType* AddedType = Pair.Key->AddFoliageType(Type, &TypeInfo))
			{
				TypeInfo->AddInstances(AddedType, Pair.Value);
			}
		}

		FWorldBuilderFoliageCount& Count = Result.Types.AddDefaulted_GetRef();
		Count.FoliageType = Type->GetPathName();
		Count.Mesh = MeshPathOf(Type);
		Count.Count = Instances.Num();
		Result.InstanceCount += Instances.Num();
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Placed %d instance(s) from %d candidates over %.0f x %.0f m (rejected: %d slope, %d height, %d layer, %d excluded, %d no ground).%s Save all to keep them."),
		Result.InstanceCount, Result.CandidateCount, CmToMeters(AreaCm.GetSize().X), CmToMeters(AreaCm.GetSize().Y),
		RejSlope, RejHeight, RejLayer, RejExcluded, RejNoGround,
		CreatedTypes.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Created foliage type(s): %s."), *FString::Join(CreatedTypes, TEXT(", "))));
	return Result;
}

FWorldBuilderFoliageResult UFoliageScatterTools::RemoveFoliage(double CenterXM, double CenterYM, double RadiusM, const TArray<FString>& MeshPaths)
{
	FWorldBuilderFoliageResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		Result.Message = TEXT("Stop Play-In-Editor before removing foliage.");
		return Result;
	}
	if (RadiusM <= 0.0)
	{
		Result.Message = TEXT("RadiusM must be greater than 0.");
		return Result;
	}

	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	const double RadiusCm = MetersToCm(RadiusM);
	const FBox Box(FVector(CenterCm.X - RadiusCm, CenterCm.Y - RadiusCm, -HALF_WORLD_MAX), FVector(CenterCm.X + RadiusCm, CenterCm.Y + RadiusCm, HALF_WORLD_MAX));

	FScopedTransaction Transaction(LOCTEXT("RemoveFoliage", "AI: Remove Foliage"));
	TMap<UFoliageType*, int32> Removed;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		IFA->ForEachFoliageInfo([&](UFoliageType* Type, FFoliageInfo& FoliageInfo)
		{
			if (!MatchesAny(Type, MeshPaths))
			{
				return true;
			}
			TArray<int32> Indices = FoliageInfo.GetInstancesOverlappingBox(Box);
			Indices.RemoveAll([&](int32 Index)
			{
				return FVector2D::DistSquared(FVector2D(FoliageInfo.Instances[Index].Location), CenterCm) > FMath::Square(RadiusCm);
			});
			if (!Indices.IsEmpty())
			{
				IFA->Modify();
				FoliageInfo.RemoveInstances(Indices, /*RebuildFoliageTree=*/true);
				Removed.FindOrAdd(Type) += Indices.Num();
			}
			return true;
		});
	}

	if (Removed.IsEmpty())
	{
		Transaction.Cancel();
	}
	for (const TPair<UFoliageType*, int32>& Pair : Removed)
	{
		FWorldBuilderFoliageCount& Count = Result.Types.AddDefaulted_GetRef();
		Count.FoliageType = Pair.Key->GetPathName();
		Count.Mesh = MeshPathOf(Pair.Key);
		Count.Count = Pair.Value;
		Result.InstanceCount += Pair.Value;
	}
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Removed %d foliage instance(s) within %.0f m of (%.0f, %.0f) m."), Result.InstanceCount, RadiusM, CenterXM, CenterYM);
	return Result;
}

FWorldBuilderFoliageResult UFoliageScatterTools::ListFoliage()
{
	FWorldBuilderFoliageResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}

	TMap<UFoliageType*, int32> Totals;
	int32 ActorCount = 0;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		++ActorCount;
		It->ForEachFoliageInfo([&](UFoliageType* Type, FFoliageInfo& FoliageInfo)
		{
			Totals.FindOrAdd(Type) += FoliageInfo.Instances.Num();
			return true;
		});
	}
	for (const TPair<UFoliageType*, int32>& Pair : Totals)
	{
		FWorldBuilderFoliageCount& Count = Result.Types.AddDefaulted_GetRef();
		Count.FoliageType = Pair.Key->GetPathName();
		Count.Mesh = MeshPathOf(Pair.Key);
		Count.Count = Pair.Value;
		Result.InstanceCount += Pair.Value;
	}
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%d foliage type(s), %d instance(s) in %d loaded foliage actor(s)."), Result.Types.Num(), Result.InstanceCount, ActorCount);
	return Result;
}

#undef LOCTEXT_NAMESPACE
