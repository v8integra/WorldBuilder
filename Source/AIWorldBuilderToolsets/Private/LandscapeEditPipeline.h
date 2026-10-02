#pragma once

#include "CoreMinimal.h"
#include "LandscapeSculptTools.h"

class ALandscape;

/**
 * Shared landscape write path used by every tool that changes terrain heights (sculpt, generate, import).
 * See RunSculpt for the full sequence. Internal to the AIWorldBuilderToolsets module.
 */
namespace AIWorldBuilder::EditPipeline
{
	/** All AI terrain edits go into this edit layer so they stay separate from hand sculpting. */
	extern const FName AISculptLayerName;

	/** Per-call cap on samples per side (plan rule 5). 4097 covers a 4 km square at 1 m spacing. */
	inline constexpr int32 MaxSamplesPerSide = 4097;

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

	/**
	 * The shared write path:
	 *  1. resolve the landscape and the sample rectangle covering AreaCm, refuse unloaded or oversized areas;
	 *  2. open an undo transaction and find or create the "AI Sculpt" edit layer;
	 *  3. render the merged heights with (F) and without (B) that layer;
	 *  4. let the tool compute new heights N from F;
	 *  5. write the layer contribution (N - B) / alpha, so the merged result is exactly N;
	 *  6. force a synchronous layer update and verify collision at the point that changed most.
	 */
	FWorldBuilderSculptResult RunSculpt(const FText& TransactionName, const FString& LandscapeName, const FVector2D& ResolvePointCm,
		const FBox2D& AreaCm, bool bAllowClipping, FComputeFn Compute);

	/** A failed result with a message. */
	FWorldBuilderSculptResult Fail(const FString& Message);

	/** Splits a world-space area (cm) into tiles small enough for one RunSculpt call on this landscape. */
	TArray<FBox2D> SplitIntoTiles(const ALandscape* Landscape, const FBox2D& AreaCm);

	/** Folds one tile's result into a running total (first tile initializes it). */
	void AccumulateTileResult(FWorldBuilderSculptResult& Total, const FWorldBuilderSculptResult& Tile, bool bFirstTile);

	/** Checks the landscape can be edited right now (the public pieces of ALandscape::CanUpdateLayersContent). */
	bool CanEditLandscape(ALandscape* Landscape, FString& OutError);

	/**
	 * Renders the merged heights (all visible edit layers) for an inclusive sample rectangle into raw landscape values,
	 * tiling as needed. The region must be loaded.
	 */
	bool RenderMergedHeights(ALandscape* Landscape, const FIntRect& InclusiveRect, TArray<uint16>& OutValues, FString& OutError);
}
