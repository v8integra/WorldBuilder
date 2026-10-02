#include "LandscapeEditPipeline.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "Misc/App.h"
#include "ScopedTransaction.h"

namespace AIWorldBuilder::EditPipeline
{
	const FName AISculptLayerName(TEXT("AI Sculpt"));

	namespace
	{
		/** Changes smaller than this (cm) are not counted as changed samples. */
		constexpr double ChangeThresholdCm = 1.0;

		FString FormatM(double Cm) { return FString::Printf(TEXT("%.1f m"), CmToMeters(Cm)); }
	}

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
		const FIntRect Extent = LandscapeUtils::GetCompleteExtent(Landscape);
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

	bool CanEditLandscape(ALandscape* Landscape, FString& OutError)
	{
		ULandscapeInfo* Info = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
		if (!FApp::CanEverRender() || !Info || !Info->AreAllComponentsRegistered() || !Info->SupportsLandscapeEditing())
		{
			OutError = FString::Printf(TEXT("Landscape '%s' can't be edited right now (still loading, or this world doesn't support landscape editing). Try again in a moment."),
				*LandscapeUtils::GetDisplayName(Landscape));
			return false;
		}
		return true;
	}

	TArray<FBox2D> SplitIntoTiles(const ALandscape* Landscape, const FBox2D& AreaCm)
	{
		TArray<FBox2D> Tiles;
		const double SpacingCm = LandscapeUtils::GetSampleSpacingCm(Landscape);
		// Leave a couple of samples of slack for rounding of tile edges to sample positions.
		const double TileCm = (MaxSamplesPerSide - 3) * SpacingCm;
		const int32 CountX = FMath::Max(1, FMath::CeilToInt32((AreaCm.Max.X - AreaCm.Min.X) / TileCm));
		const int32 CountY = FMath::Max(1, FMath::CeilToInt32((AreaCm.Max.Y - AreaCm.Min.Y) / TileCm));
		const FVector2D Step((AreaCm.Max.X - AreaCm.Min.X) / CountX, (AreaCm.Max.Y - AreaCm.Min.Y) / CountY);
		for (int32 TY = 0; TY < CountY; ++TY)
		{
			for (int32 TX = 0; TX < CountX; ++TX)
			{
				const FVector2D Min = AreaCm.Min + FVector2D(Step.X * TX, Step.Y * TY);
				Tiles.Add(FBox2D(Min, Min + Step));
			}
		}
		return Tiles;
	}

	void AccumulateTileResult(FWorldBuilderSculptResult& Total, const FWorldBuilderSculptResult& Tile, bool bFirstTile)
	{
		if (bFirstTile)
		{
			Total = Tile;
			return;
		}
		Total.bSuccess = Total.bSuccess && Tile.bSuccess;
		Total.bCreatedEditLayer = Total.bCreatedEditLayer || Tile.bCreatedEditLayer;
		Total.RegionMinM = FVector2D::Min(Total.RegionMinM, Tile.RegionMinM);
		Total.RegionMaxM = FVector2D::Max(Total.RegionMaxM, Tile.RegionMaxM);
		Total.SampleCount += Tile.SampleCount;
		Total.ChangedSampleCount += Tile.ChangedSampleCount;
		Total.MinChangeM = FMath::Min(Total.MinChangeM, Tile.MinChangeM);
		Total.MaxChangeM = FMath::Max(Total.MaxChangeM, Tile.MaxChangeM);
		Total.NewMinHeightM = FMath::Min(Total.NewMinHeightM, Tile.NewMinHeightM);
		Total.NewMaxHeightM = FMath::Max(Total.NewMaxHeightM, Tile.NewMaxHeightM);
		Total.ClippedSampleCount += Tile.ClippedSampleCount;
		Total.bCollisionVerified = Total.bCollisionVerified && Tile.bCollisionVerified;
		if (FMath::Abs(Tile.VerifyPointM.Z - Tile.CollisionHeightM) <= FMath::Abs(Total.VerifyPointM.Z - Total.CollisionHeightM))
		{
			Total.VerifyPointM = Tile.VerifyPointM;
			Total.CollisionHeightM = Tile.CollisionHeightM;
		}
	}

	bool RenderMergedHeights(ALandscape* Landscape, const FIntRect& InclusiveRect, TArray<uint16>& OutValues, FString& OutError)
	{
		if (!CanEditLandscape(Landscape, OutError))
		{
			return false;
		}
		const int32 Width = InclusiveRect.Max.X - InclusiveRect.Min.X + 1;
		const int32 Height = InclusiveRect.Max.Y - InclusiveRect.Min.Y + 1;
		if (Width <= 0 || Height <= 0)
		{
			OutError = TEXT("Empty region.");
			return false;
		}

		// SelectiveRenderEditLayersHeightmaps check()s that edit layers are initialized; this guarantees it.
		Landscape->ForceUpdateLayersContent();

		TBitArray<> ActiveLayers;
		for (const ULandscapeEditLayerBase* EditLayer : Landscape->GetEditLayersConst())
		{
			ActiveLayers.Add(EditLayer && EditLayer->IsVisible());
		}

		OutValues.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), Width * Height);
		TArray<uint16> TileValues;
		for (int32 TY = InclusiveRect.Min.Y; TY <= InclusiveRect.Max.Y; TY += MaxSamplesPerSide)
		{
			for (int32 TX = InclusiveRect.Min.X; TX <= InclusiveRect.Max.X; TX += MaxSamplesPerSide)
			{
				const int32 TX2 = FMath::Min(TX + MaxSamplesPerSide - 1, InclusiveRect.Max.X);
				const int32 TY2 = FMath::Min(TY + MaxSamplesPerSide - 1, InclusiveRect.Max.Y);
				const int32 TW = TX2 - TX + 1, TH = TY2 - TY + 1;
				TileValues.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), TW * TH);

				FLandscapeEditLayerRenderHeightParams Render;
				Render.Bounds = FIntRect(TX, TY, TX2 + 1, TY2 + 1);	// half-open
				Render.ActiveEditLayers = ActiveLayers;
				Render.CpuResult = MakeArrayView(TileValues);
				if (!Landscape->SelectiveRenderEditLayersHeightmaps(Render))
				{
					OutError = TEXT("Couldn't read the landscape's heights (edit layers not ready). Try again in a moment.");
					return false;
				}
				for (int32 Row = 0; Row < TH; ++Row)
				{
					FMemory::Memcpy(&OutValues[(TY - InclusiveRect.Min.Y + Row) * Width + (TX - InclusiveRect.Min.X)], &TileValues[Row * TW], TW * sizeof(uint16));
				}
			}
		}
		return true;
	}
}
