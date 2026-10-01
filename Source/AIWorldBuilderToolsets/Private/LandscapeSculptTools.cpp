#include "LandscapeSculptTools.h"

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

namespace
{
	/** All AI sculpting goes into this edit layer so it stays separate from hand sculpting. */
	const FName AISculptLayerName(TEXT("AI Sculpt"));

	/** Per-call cap on samples per side (plan rule 5). 4097 covers a 4 km square at 1 m spacing. */
	constexpr int32 MaxSamplesPerSide = 4097;

	/** Changes smaller than this (cm) are not counted as changed samples. */
	constexpr double ChangeThresholdCm = 1.0;

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

	/** Heights for the area being edited, in world Z (cm), plus mapping between grid cells and world XY. */
	struct FSculptGrid
	{
		int32 X1 = 0;	// first landscape sample (inclusive)
		int32 Y1 = 0;
		int32 Width = 0;
		int32 Height = 0;
		double SpacingCm = 100.0;
		FTransform LandscapeToWorld;
		TArray<double> Current;	// heights before the edit
		TArray<double> New;		// heights after the edit (tools fill this)

		int32 Index(int32 IX, int32 IY) const { return IY * Width + IX; }

		FVector2D WorldXY(int32 IX, int32 IY) const
		{
			const FVector P = LandscapeToWorld.TransformPosition(FVector(X1 + IX, Y1 + IY, 0.0));
			return FVector2D(P.X, P.Y);
		}

		/** Bilinear height of the current terrain at a world point, clamped to the grid. */
		double SampleCurrent(const FVector2D& WorldCm) const
		{
			const FVector Local = LandscapeToWorld.InverseTransformPosition(FVector(WorldCm.X, WorldCm.Y, 0.0));
			const double FX = FMath::Clamp(Local.X - X1, 0.0, double(Width - 1));
			const double FY = FMath::Clamp(Local.Y - Y1, 0.0, double(Height - 1));
			const int32 IX = FMath::Min(FMath::FloorToInt32(FX), Width - 2 < 0 ? 0 : Width - 2);
			const int32 IY = FMath::Min(FMath::FloorToInt32(FY), Height - 2 < 0 ? 0 : Height - 2);
			const int32 IX1 = FMath::Min(IX + 1, Width - 1);
			const int32 IY1 = FMath::Min(IY + 1, Height - 1);
			const double TX = FX - IX, TY = FY - IY;
			const double A = FMath::Lerp(Current[Index(IX, IY)], Current[Index(IX1, IY)], TX);
			const double B = FMath::Lerp(Current[Index(IX, IY1)], Current[Index(IX1, IY1)], TX);
			return FMath::Lerp(A, B, TY);
		}

		/** Calls Fn(IX, IY, WorldXY, Index) for every sample. */
		template <typename FnType>
		void ForEach(FnType&& Fn) const
		{
			for (int32 IY = 0; IY < Height; ++IY)
			{
				for (int32 IX = 0; IX < Width; ++IX)
				{
					Fn(IX, IY, WorldXY(IX, IY), Index(IX, IY));
				}
			}
		}
	};

	using FComputeFn = TFunctionRef<bool(FSculptGrid& Grid, FString& OutError)>;

	FString FormatM(double Cm) { return FString::Printf(TEXT("%.1f m"), CmToMeters(Cm)); }

	/**
	 * The shared write path for every sculpt tool:
	 *  1. resolve the landscape and the sample rectangle covering AreaCm, refuse unloaded or oversized areas;
	 *  2. open an undo transaction and find or create the "AI Sculpt" edit layer;
	 *  3. render the merged heights with (F) and without (B) that layer;
	 *  4. let the tool compute new heights N from F;
	 *  5. write the layer contribution (N - B) / alpha, so the merged result is exactly N;
	 *  6. force a synchronous layer update and verify collision at the point that changed most.
	 */
	FWorldBuilderSculptResult RunSculpt(const FText& TransactionName, const FString& LandscapeName, const FVector2D& ResolvePointCm,
		const FBox2D& AreaCm, bool bAllowClipping, FComputeFn Compute)
	{
		FWorldBuilderSculptResult Result;
		Result.EditLayerName = AISculptLayerName.ToString();

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result.Message = TEXT("No level is open in the editor.");
			return Result;
		}
		if (GEditor && GEditor->PlayWorld)
		{
			Result.Message = TEXT("Stop Play-In-Editor before sculpting.");
			return Result;
		}

		ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, &ResolvePointCm, Result.Message);
		if (!Landscape)
		{
			return Result;
		}
		Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);

		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		if (!FApp::CanEverRender() || !Info || !Info->AreAllComponentsRegistered() || !Info->SupportsLandscapeEditing())
		{
			Result.Message = FString::Printf(TEXT("Landscape '%s' can't be edited right now (still loading, or this world doesn't support landscape editing). Try again in a moment."), *Result.LandscapeName);
			return Result;
		}

		// 1. Sample rectangle (inclusive) covering the area, clipped to the landscape.
		const FTransform LandscapeToWorld = Landscape->LandscapeActorToWorld();
		double MinLX = TNumericLimits<double>::Max(), MinLY = TNumericLimits<double>::Max();
		double MaxLX = TNumericLimits<double>::Lowest(), MaxLY = TNumericLimits<double>::Lowest();
		for (const FVector2D& Corner : { AreaCm.Min, AreaCm.Max, FVector2D(AreaCm.Min.X, AreaCm.Max.Y), FVector2D(AreaCm.Max.X, AreaCm.Min.Y) })
		{
			const FVector L = LandscapeToWorld.InverseTransformPosition(FVector(Corner.X, Corner.Y, 0.0));
			MinLX = FMath::Min(MinLX, L.X); MaxLX = FMath::Max(MaxLX, L.X);
			MinLY = FMath::Min(MinLY, L.Y); MaxLY = FMath::Max(MaxLY, L.Y);
		}
		const FIntRect Extent = Info->GetCompleteLandscapeExtent();
		const int32 X1 = FMath::Max(FMath::FloorToInt32(MinLX), Extent.Min.X);
		const int32 Y1 = FMath::Max(FMath::FloorToInt32(MinLY), Extent.Min.Y);
		const int32 X2 = FMath::Min(FMath::CeilToInt32(MaxLX), Extent.Max.X);
		const int32 Y2 = FMath::Min(FMath::CeilToInt32(MaxLY), Extent.Max.Y);
		if (X1 > X2 || Y1 > Y2)
		{
			Result.Message = FString::Printf(TEXT("The area does not overlap landscape '%s'. Use ListLandscapes to see its bounds."), *Result.LandscapeName);
			return Result;
		}

		const double SpacingCm = LandscapeUtils::GetSampleSpacingCm(Landscape);
		FSculptGrid Grid;
		Grid.X1 = X1;
		Grid.Y1 = Y1;
		Grid.Width = X2 - X1 + 1;
		Grid.Height = Y2 - Y1 + 1;
		Grid.SpacingCm = SpacingCm;
		Grid.LandscapeToWorld = LandscapeToWorld;
		const int32 Count = Grid.Width * Grid.Height;

		const FVector WorldMin = LandscapeToWorld.TransformPosition(FVector(X1, Y1, 0.0));
		const FVector WorldMax = LandscapeToWorld.TransformPosition(FVector(X2, Y2, 0.0));
		Result.RegionMinM = FVector2D(FMath::Min(WorldMin.X, WorldMax.X), FMath::Min(WorldMin.Y, WorldMax.Y)) / CmPerMeter;
		Result.RegionMaxM = FVector2D(FMath::Max(WorldMin.X, WorldMax.X), FMath::Max(WorldMin.Y, WorldMax.Y)) / CmPerMeter;
		Result.SampleCount = Count;

		if (Grid.Width > MaxSamplesPerSide || Grid.Height > MaxSamplesPerSide)
		{
			Result.Message = FString::Printf(TEXT("The area is %d x %d samples (%.0f x %.0f m); the limit per call is %d x %d (%.0f m at this landscape's %.2f m spacing). Use a smaller radius or split the work into smaller areas."),
				Grid.Width, Grid.Height, CmToMeters(Grid.Width * SpacingCm), CmToMeters(Grid.Height * SpacingCm),
				MaxSamplesPerSide, MaxSamplesPerSide, CmToMeters(MaxSamplesPerSide * SpacingCm), CmToMeters(SpacingCm));
			return Result;
		}

		// Only touch loaded components: in World Partition, unloaded regions cannot be edited.
		const int32 CSQ = FMath::Max(Info->ComponentSizeQuads, 1);
		int32 MissingComponents = 0, TotalComponents = 0;
		for (int32 KY = FMath::FloorToInt32(double(Y1) / CSQ); KY <= FMath::FloorToInt32(double(FMath::Max(Y2 - 1, Y1)) / CSQ); ++KY)
		{
			for (int32 KX = FMath::FloorToInt32(double(X1) / CSQ); KX <= FMath::FloorToInt32(double(FMath::Max(X2 - 1, X1)) / CSQ); ++KX)
			{
				++TotalComponents;
				MissingComponents += Info->XYtoComponentMap.Contains(FIntPoint(KX, KY)) ? 0 : 1;
			}
		}
		if (MissingComponents > 0)
		{
			Result.Message = FString::Printf(TEXT("%d of %d landscape components in this area are not loaded. In World Partition, load that region (World Partition window: select the cells, right-click, Load) and try again."),
				MissingComponents, TotalComponents);
			return Result;
		}

		// 2. Undo transaction and edit layer.
		FScopedTransaction Transaction(TransactionName);

		int32 LayerIndex = Landscape->GetLayerIndex(AISculptLayerName);
		if (LayerIndex == INDEX_NONE)
		{
			LayerIndex = Landscape->CreateLayer(AISculptLayerName);
			if (LayerIndex == INDEX_NONE)
			{
				Transaction.Cancel();
				Result.Message = TEXT("Couldn't create the 'AI Sculpt' edit layer. The landscape may already have the maximum number of edit layers; remove one in Landscape mode and try again.");
				return Result;
			}
			Result.bCreatedEditLayer = true;
		}

		const ULandscapeEditLayerBase* Layer = Landscape->GetEditLayerConst(LayerIndex);
		if (!Layer)
		{
			Transaction.Cancel();
			Result.Message = TEXT("Couldn't access the 'AI Sculpt' edit layer.");
			return Result;
		}
		if (Layer->IsLocked())
		{
			Transaction.Cancel();
			Result.Message = TEXT("The 'AI Sculpt' edit layer is locked. Unlock it in Landscape mode (Edit Layers panel) and try again.");
			return Result;
		}
		if (!Layer->IsVisible())
		{
			Transaction.Cancel();
			Result.Message = TEXT("The 'AI Sculpt' edit layer is hidden, so changes would not show. Make it visible in Landscape mode (Edit Layers panel) and try again.");
			return Result;
		}
		const double Alpha = Layer->GetAlphaForTargetType(ELandscapeToolTargetType::Heightmap);
		if (FMath::IsNearlyZero(Alpha))
		{
			Transaction.Cancel();
			Result.Message = TEXT("The 'AI Sculpt' edit layer's heightmap alpha is 0, so changes would not show. Set its alpha to 1 in Landscape mode.");
			return Result;
		}

		// Initializes edit layers if needed and brings the merged result up to date before reading it.
		Landscape->ForceUpdateLayersContent();

		// 3. Merged heights with (F) and without (B) the AI layer.
		TBitArray<> ActiveLayers;
		for (const ULandscapeEditLayerBase* EditLayer : Landscape->GetEditLayersConst())
		{
			ActiveLayers.Add(EditLayer && EditLayer->IsVisible());
		}
		TArray<uint16> FinalValues, BaseValues;
		FinalValues.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), Count);
		BaseValues.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), Count);

		FLandscapeEditLayerRenderHeightParams Render;
		Render.Bounds = FIntRect(X1, Y1, X2 + 1, Y2 + 1);	// half-open
		Render.ActiveEditLayers = ActiveLayers;
		Render.CpuResult = MakeArrayView(FinalValues);
		bool bRendered = Landscape->SelectiveRenderEditLayersHeightmaps(Render);

		ActiveLayers[LayerIndex] = false;
		Render.ActiveEditLayers = ActiveLayers;
		Render.CpuResult = MakeArrayView(BaseValues);
		bRendered = bRendered && Landscape->SelectiveRenderEditLayersHeightmaps(Render);
		if (!bRendered)
		{
			Transaction.Cancel();
			Result.Message = TEXT("Couldn't read the landscape's current heights (edit layers not ready). Try again in a moment.");
			return Result;
		}

		const double ScaleZ = LandscapeToWorld.GetScale3D().Z;
		const double ActorZ = LandscapeToWorld.GetLocation().Z;
		Grid.Current.SetNumUninitialized(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			Grid.Current[I] = LandscapeMath::HeightValueToWorldZCm(FinalValues[I], ScaleZ, ActorZ);
		}
		Grid.New = Grid.Current;

		// 4. Tool-specific height computation.
		FString ComputeError;
		if (!Compute(Grid, ComputeError))
		{
			Transaction.Cancel();
			Result.Message = ComputeError;
			return Result;
		}

		// 5. Convert to layer contributions, checking the landscape's height limits.
		double RangeMinCm = 0.0, RangeMaxCm = 0.0;
		LandscapeMath::GetHeightRangeCm(ScaleZ, ActorZ, RangeMinCm, RangeMaxCm);
		constexpr double MaxLocal = 255.99;	// representable local units either side of zero

		TArray<uint16> LayerValues;
		LayerValues.SetNumUninitialized(Count);
		double RequestedMinCm = TNumericLimits<double>::Max(), RequestedMaxCm = TNumericLimits<double>::Lowest();
		for (int32 I = 0; I < Count; ++I)
		{
			double NewCm = Grid.New[I];
			RequestedMinCm = FMath::Min(RequestedMinCm, NewCm);
			RequestedMaxCm = FMath::Max(RequestedMaxCm, NewCm);

			bool bClipped = false;
			if (NewCm > RangeMaxCm || NewCm < RangeMinCm)
			{
				NewCm = FMath::Clamp(NewCm, RangeMinCm, RangeMaxCm);
				bClipped = true;
			}
			const double NewLocal = (NewCm - ActorZ) / ScaleZ;
			double LayerLocal = (NewLocal - LandscapeMath::HeightValueToLocalZ(BaseValues[I])) / Alpha;
			if (FMath::Abs(LayerLocal) > MaxLocal)
			{
				LayerLocal = FMath::Clamp(LayerLocal, -MaxLocal, MaxLocal);
				bClipped = true;
			}
			LayerValues[I] = LandscapeMath::LocalZToHeightValue(LayerLocal);
			Result.ClippedSampleCount += bClipped ? 1 : 0;

			// What the merged result will actually be after quantization and clamping.
			Grid.New[I] = (LandscapeMath::HeightValueToLocalZ(BaseValues[I]) + Alpha * LandscapeMath::HeightValueToLocalZ(LayerValues[I])) * ScaleZ + ActorZ;
		}

		if (Result.ClippedSampleCount > 0 && !bAllowClipping)
		{
			Transaction.Cancel();
			const double NeededScaleZ = FMath::Max(FMath::Abs(RequestedMaxCm - ActorZ), FMath::Abs(RequestedMinCm - ActorZ)) / MaxLocal;
			Result.Message = FString::Printf(TEXT("Not applied: %d sample(s) would exceed landscape '%s''s height range (%s to %s); the result would reach %s to %s. ")
				TEXT("Options: use a smaller height, raise the landscape's Scale Z to at least %.0f (this also scales all existing terrain heights), or pass bAllowClipping=true to clamp."),
				Result.ClippedSampleCount, *Result.LandscapeName, *FormatM(RangeMinCm), *FormatM(RangeMaxCm),
				*FormatM(RequestedMinCm), *FormatM(RequestedMaxCm), FMath::CeilToDouble(NeededScaleZ));
			return Result;
		}

		// Change statistics.
		int32 MaxChangeIndex = INDEX_NONE;
		double MaxAbsChange = 0.0;
		double NewMin = TNumericLimits<double>::Max(), NewMax = TNumericLimits<double>::Lowest();
		for (int32 I = 0; I < Count; ++I)
		{
			const double Change = Grid.New[I] - Grid.Current[I];
			NewMin = FMath::Min(NewMin, Grid.New[I]);
			NewMax = FMath::Max(NewMax, Grid.New[I]);
			if (FMath::Abs(Change) > ChangeThresholdCm)
			{
				++Result.ChangedSampleCount;
				Result.MinChangeM = FMath::Min(Result.MinChangeM, CmToMeters(Change));
				Result.MaxChangeM = FMath::Max(Result.MaxChangeM, CmToMeters(Change));
			}
			if (FMath::Abs(Change) > MaxAbsChange)
			{
				MaxAbsChange = FMath::Abs(Change);
				MaxChangeIndex = I;
			}
		}
		Result.NewMinHeightM = CmToMeters(NewMin);
		Result.NewMaxHeightM = CmToMeters(NewMax);

		if (Result.ChangedSampleCount == 0)
		{
			Transaction.Cancel();
			Result.bSuccess = true;
			Result.Message = TEXT("Nothing changed (the terrain already matches, e.g. Max mode where the ground is already higher).");
			return Result;
		}

		// 5b. Write the layer contribution (records the textures in the transaction for undo).
		{
			FHeightmapAccessor<false> Accessor(Info);
			Accessor.SetEditLayer(Layer->GetGuid());
			Accessor.SetData(X1, Y1, X2, Y2, LayerValues.GetData());
		}

		// 6. Merge now so the result and its collision are ready for the next call, then verify.
		Landscape->ForceUpdateLayersContent();

		if (MaxChangeIndex != INDEX_NONE)
		{
			const FVector2D PointCm = Grid.WorldXY(MaxChangeIndex % Grid.Width, MaxChangeIndex / Grid.Width);
			const double ExpectedCm = Grid.New[MaxChangeIndex];
			Result.VerifyPointM = FVector(PointCm.X, PointCm.Y, ExpectedCm) / CmPerMeter;
			if (const TOptional<double> CollisionCm = LandscapeUtils::SampleHeightCm(Landscape, PointCm))
			{
				Result.CollisionHeightM = CmToMeters(CollisionCm.GetValue());
				const double ToleranceCm = FMath::Max(5.0, 2.0 * ScaleZ / 128.0);
				Result.bCollisionVerified = FMath::Abs(CollisionCm.GetValue() - ExpectedCm) <= ToleranceCm;
			}
		}

		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("%s on '%s' in edit layer '%s'%s: %d samples changed (raised up to %.1f m, lowered up to %.1f m) over %.0f x %.0f m. Heights now %.1f to %.1f m.%s%s"),
			*TransactionName.ToString(), *Result.LandscapeName, *Result.EditLayerName, Result.bCreatedEditLayer ? TEXT(" (created)") : TEXT(""),
			Result.ChangedSampleCount, Result.MaxChangeM, -Result.MinChangeM,
			Result.RegionMaxM.X - Result.RegionMinM.X, Result.RegionMaxM.Y - Result.RegionMinM.Y,
			Result.NewMinHeightM, Result.NewMaxHeightM,
			Result.ClippedSampleCount > 0 ? *FString::Printf(TEXT(" %d sample(s) were clamped to the height limits."), Result.ClippedSampleCount) : TEXT(""),
			Result.bCollisionVerified ? TEXT(" Collision verified.") : TEXT(" Collision not confirmed yet; check with SampleHeight."));
		return Result;
	}

	FWorldBuilderSculptResult Fail(const FString& Message)
	{
		FWorldBuilderSculptResult Result;
		Result.Message = Message;
		return Result;
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
