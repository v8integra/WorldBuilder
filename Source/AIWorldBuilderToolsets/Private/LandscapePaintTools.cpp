#include "LandscapePaintTools.h"

#include "LandscapeEditPipeline.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AIWorldBuilderTerrainMath.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeUtils.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIWorldBuilderPaint"

using namespace AIWorldBuilder;
using namespace AIWorldBuilder::EditPipeline;
namespace TM = AIWorldBuilder::TerrainMath;

namespace
{
	/** All AI painting goes into this edit layer, separate from hand painting and from "AI Sculpt". */
	const FName AIPaintLayerName(TEXT("AI Paint"));

	/** Rule limits at or beyond these values mean "no limit". */
	constexpr double NoHeightLimitM = 99999.0;

	struct FPaintLayer
	{
		FName Name;
		ULandscapeLayerInfoObject* Info = nullptr;
	};

	FString BlendMethodName(ELandscapeTargetLayerBlendMethod Method)
	{
		switch (Method)
		{
		case ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending: return TEXT("Advanced");
		case ELandscapeTargetLayerBlendMethod::FinalWeightBlending:        return TEXT("Legacy");
		default:                                                           return TEXT("None");
		}
	}

	ELandscapeTargetLayerBlendMethod ToEngine(EWorldBuilderWeightBlend Blend)
	{
		switch (Blend)
		{
		case EWorldBuilderWeightBlend::Legacy: return ELandscapeTargetLayerBlendMethod::FinalWeightBlending;
		case EWorldBuilderWeightBlend::None:   return ELandscapeTargetLayerBlendMethod::None;
		case EWorldBuilderWeightBlend::Advanced:
		default:                               return ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending;
		}
	}

	TM::EFalloff ToMath(EWorldBuilderFalloff Falloff)
	{
		switch (Falloff)
		{
		case EWorldBuilderFalloff::Linear: return TM::EFalloff::Linear;
		case EWorldBuilderFalloff::Sphere: return TM::EFalloff::Sphere;
		case EWorldBuilderFalloff::Tip:    return TM::EFalloff::Tip;
		default:                           return TM::EFalloff::Smooth;
		}
	}

	bool IsVisibility(const ULandscapeLayerInfoObject* Info)
	{
		return Info && UE::Landscape::IsVisibilityLayer(Info);
	}

	/** Target layers that have a Layer Info (and so can be painted), excluding the visibility (holes) layer. */
	TArray<FPaintLayer> GetPaintableLayers(const ALandscape* Landscape)
	{
		TArray<FPaintLayer> Layers;
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape->GetTargetLayers())
		{
			ULandscapeLayerInfoObject* Info = Pair.Value.LayerInfoObj;
			if (Info && !IsVisibility(Info))
			{
				Layers.Add({ Pair.Key, Info });
			}
		}
		return Layers;
	}

	FWorldBuilderPaintLayerListResult BuildLayerList(ALandscape* Landscape)
	{
		FWorldBuilderPaintLayerListResult Result;
		Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);
		if (Landscape->LandscapeMaterial)
		{
			Result.MaterialPath = Landscape->LandscapeMaterial->GetPathName();
		}

		TMap<FName, FWorldBuilderPaintLayerStatus> ByName;
		for (const FName& Name : Landscape->RetrieveTargetLayerNamesFromMaterials(/*bIncludeVisibilityLayer=*/false))
		{
			FWorldBuilderPaintLayerStatus& S = ByName.FindOrAdd(Name);
			S.Name = Name.ToString();
			S.bInMaterial = true;
			S.BlendMethod = TEXT("None");
		}
		for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape->GetTargetLayers())
		{
			const ULandscapeLayerInfoObject* Info = Pair.Value.LayerInfoObj;
			if (IsVisibility(Info))
			{
				continue;
			}
			FWorldBuilderPaintLayerStatus& S = ByName.FindOrAdd(Pair.Key);
			S.Name = Pair.Key.ToString();
			if (Info)
			{
				S.bHasLayerInfo = true;
				S.LayerInfoPath = Info->GetPathName();
				S.BlendMethod = BlendMethodName(Info->GetBlendMethod());
			}
			else if (S.BlendMethod.IsEmpty())
			{
				S.BlendMethod = TEXT("None");
			}
		}
		ByName.GenerateValueArray(Result.Layers);
		return Result;
	}

	/** Weights and terrain for the area being painted. Weights are the "AI Paint" edit layer's own, 0..1. */
	struct FPaintGrid
	{
		FIntRect Rect;	// inclusive
		int32 Width = 0;
		int32 Height = 0;
		FTransform LandscapeToWorld;
		TArray<double> HeightsCm;
		TArray<double> SlopesDeg;
		TArray<FPaintLayer> Layers;
		TArray<TArray<double>> Weights;	// [layer][sample]

		FVector2D WorldXY(int32 IX, int32 IY) const
		{
			const FVector P = LandscapeToWorld.TransformPosition(FVector(Rect.Min.X + IX, Rect.Min.Y + IY, 0.0));
			return FVector2D(P.X, P.Y);
		}

		int32 FindLayer(const FString& Name) const
		{
			return Layers.IndexOfByPredicate([&Name](const FPaintLayer& L) { return L.Name.ToString().Equals(Name, ESearchCase::IgnoreCase); });
		}

		FString LayerNames() const
		{
			TArray<FString> Names;
			for (const FPaintLayer& L : Layers) { Names.Add(L.Name.ToString()); }
			return FString::Join(Names, TEXT(", "));
		}
	};

	using FPaintFn = TFunctionRef<bool(FPaintGrid& Grid, FString& OutError)>;

	FWorldBuilderPaintResult FailPaint(const FString& Message)
	{
		FWorldBuilderPaintResult Result;
		Result.Message = Message;
		return Result;
	}

	/**
	 * Shared paint path: resolve the area, open an undo transaction, find or create the "AI Paint" edit layer,
	 * read merged heights (with a one-sample margin for slopes) and the layer's current weights, let the tool
	 * compute new weights, write the changed layers, then merge synchronously.
	 */
	FWorldBuilderPaintResult RunPaint(const FText& TransactionName, ALandscape* Landscape, const FBox2D& AreaCm, FPaintFn Compute)
	{
		FWorldBuilderPaintResult Result;
		Result.EditLayerName = AIPaintLayerName.ToString();
		Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);

		if (GEditor && GEditor->PlayWorld)
		{
			Result.Message = TEXT("Stop Play-In-Editor before painting.");
			return Result;
		}
		if (!CanEditLandscape(Landscape, Result.Message))
		{
			return Result;
		}

		FPaintGrid Grid;
		Grid.Layers = GetPaintableLayers(Landscape);
		if (Grid.Layers.IsEmpty())
		{
			Result.Message = FString::Printf(TEXT("Landscape '%s' has no paint layers with a Layer Info. Run CreateLayerInfos first (and make sure the landscape material has Landscape Layer Blend layers)."), *Result.LandscapeName);
			return Result;
		}
		if (!ResolveSampleRect(Landscape, AreaCm, Grid.Rect, Result.Message))
		{
			return Result;
		}
		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		Grid.Width = Grid.Rect.Max.X - Grid.Rect.Min.X + 1;
		Grid.Height = Grid.Rect.Max.Y - Grid.Rect.Min.Y + 1;
		Grid.LandscapeToWorld = Landscape->LandscapeActorToWorld();
		const int32 Count = Grid.Width * Grid.Height;
		Result.SampleCount = Count;
		const FVector WorldMin = Grid.LandscapeToWorld.TransformPosition(FVector(Grid.Rect.Min.X, Grid.Rect.Min.Y, 0.0));
		const FVector WorldMax = Grid.LandscapeToWorld.TransformPosition(FVector(Grid.Rect.Max.X, Grid.Rect.Max.Y, 0.0));
		Result.RegionMinM = FVector2D(FMath::Min(WorldMin.X, WorldMax.X), FMath::Min(WorldMin.Y, WorldMax.Y)) / CmPerMeter;
		Result.RegionMaxM = FVector2D(FMath::Max(WorldMin.X, WorldMax.X), FMath::Max(WorldMin.Y, WorldMax.Y)) / CmPerMeter;

		FScopedTransaction Transaction(TransactionName);

		const ULandscapeEditLayerBase* Layer = GetOrCreateEditLayer(Landscape, AIPaintLayerName, ELandscapeToolTargetType::Weightmap, Result.bCreatedEditLayer, Result.Message);
		if (!Layer)
		{
			Transaction.Cancel();
			return Result;
		}
		const FGuid LayerGuid = Layer->GetGuid();

		// Merged heights with a one-sample margin so slopes at the edge use real neighbours.
		const FIntRect Extent = LandscapeUtils::GetCompleteExtent(Landscape);
		const FIntRect HeightRect(
			FMath::Max(Grid.Rect.Min.X - 1, Extent.Min.X), FMath::Max(Grid.Rect.Min.Y - 1, Extent.Min.Y),
			FMath::Min(Grid.Rect.Max.X + 1, Extent.Max.X), FMath::Min(Grid.Rect.Max.Y + 1, Extent.Max.Y));
		TArray<uint16> HeightValues;
		if (!RenderMergedHeights(Landscape, HeightRect, HeightValues, Result.Message))
		{
			Transaction.Cancel();
			return Result;
		}
		const int32 HW = HeightRect.Max.X - HeightRect.Min.X + 1;
		const int32 HH = HeightRect.Max.Y - HeightRect.Min.Y + 1;
		const double ScaleZ = Grid.LandscapeToWorld.GetScale3D().Z;
		const double ActorZ = Grid.LandscapeToWorld.GetLocation().Z;
		const double SpacingCm = LandscapeUtils::GetSampleSpacingCm(Landscape);
		auto HeightAt = [&](int32 HX, int32 HY)
		{
			HX = FMath::Clamp(HX, 0, HW - 1);
			HY = FMath::Clamp(HY, 0, HH - 1);
			return LandscapeMath::HeightValueToWorldZCm(HeightValues[HY * HW + HX], ScaleZ, ActorZ);
		};
		Grid.HeightsCm.SetNumUninitialized(Count);
		Grid.SlopesDeg.SetNumUninitialized(Count);
		const int32 OffX = Grid.Rect.Min.X - HeightRect.Min.X, OffY = Grid.Rect.Min.Y - HeightRect.Min.Y;
		for (int32 IY = 0; IY < Grid.Height; ++IY)
		{
			for (int32 IX = 0; IX < Grid.Width; ++IX)
			{
				const int32 HX = IX + OffX, HY = IY + OffY;
				const int32 I = IY * Grid.Width + IX;
				Grid.HeightsCm[I] = HeightAt(HX, HY);
				const int32 XL = FMath::Max(HX - 1, 0), XR = FMath::Min(HX + 1, HW - 1);
				const int32 YD = FMath::Max(HY - 1, 0), YU = FMath::Min(HY + 1, HH - 1);
				const double DzDx = XR > XL ? (HeightAt(XR, HY) - HeightAt(XL, HY)) / ((XR - XL) * SpacingCm) : 0.0;
				const double DzDy = YU > YD ? (HeightAt(HX, YU) - HeightAt(HX, YD)) / ((YU - YD) * SpacingCm) : 0.0;
				Grid.SlopesDeg[I] = LandscapeMath::SlopeDegrees(DzDx, DzDy);
			}
		}

		// Current weights in the AI Paint layer.
		TArray<TArray<uint8>> OldValues;
		OldValues.SetNum(Grid.Layers.Num());
		Grid.Weights.SetNum(Grid.Layers.Num());
		for (int32 L = 0; L < Grid.Layers.Num(); ++L)
		{
			OldValues[L].Init(0, Count);
			TAlphamapAccessor<false> Accessor(Info, Grid.Layers[L].Info);
			Accessor.SetEditLayer(LayerGuid);
			Accessor.GetDataFast(Grid.Rect.Min.X, Grid.Rect.Min.Y, Grid.Rect.Max.X, Grid.Rect.Max.Y, OldValues[L].GetData());
			Grid.Weights[L].SetNumUninitialized(Count);
			for (int32 I = 0; I < Count; ++I)
			{
				Grid.Weights[L][I] = OldValues[L][I] / 255.0;
			}
		}

		if (!Compute(Grid, Result.Message))
		{
			Transaction.Cancel();
			return Result;
		}

		// Write only the layers that changed.
		int32 ChangedLayers = 0;
		for (int32 L = 0; L < Grid.Layers.Num(); ++L)
		{
			TArray<uint8> NewValues;
			NewValues.SetNumUninitialized(Count);
			bool bChanged = false;
			double Sum = 0.0;
			for (int32 I = 0; I < Count; ++I)
			{
				const double W = FMath::Clamp(Grid.Weights[L][I], 0.0, 1.0);
				NewValues[I] = static_cast<uint8>(FMath::RoundToInt32(W * 255.0));
				bChanged |= NewValues[I] != OldValues[L][I];
				Sum += W;
			}
			Result.LayerCoverage.Add(Grid.Layers[L].Name.ToString(), Sum / Count);
			if (bChanged)
			{
				TAlphamapAccessor<false> Accessor(Info, Grid.Layers[L].Info);
				Accessor.SetEditLayer(LayerGuid);
				Accessor.SetData(Grid.Rect.Min.X, Grid.Rect.Min.Y, Grid.Rect.Max.X, Grid.Rect.Max.Y, NewValues.GetData(), ELandscapeLayerPaintingRestriction::None);
				++ChangedLayers;
			}
		}
		if (ChangedLayers == 0)
		{
			Transaction.Cancel();
			Result.bSuccess = true;
			Result.Message = TEXT("Nothing changed (the paint already matches).");
			return Result;
		}

		Landscape->ForceUpdateLayersContent();

		TArray<FString> Coverage;
		for (const TPair<FString, double>& Pair : Result.LayerCoverage)
		{
			Coverage.Add(FString::Printf(TEXT("%s %.0f%%"), *Pair.Key, Pair.Value * 100.0));
		}
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("%s on '%s' in edit layer '%s'%s over %.0f x %.0f m. AI paint coverage: %s."),
			*TransactionName.ToString(), *Result.LandscapeName, *Result.EditLayerName, Result.bCreatedEditLayer ? TEXT(" (created)") : TEXT(""),
			Result.RegionMaxM.X - Result.RegionMinM.X, Result.RegionMaxM.Y - Result.RegionMinM.Y, *FString::Join(Coverage, TEXT(", ")));
		return Result;
	}

	/** Soft membership of Value in [Min, Max] with transition width Blend (limits at/beyond Unlimited are open). */
	double RangeMask(double Value, double Min, double Max, double Blend, double UnlimitedLow, double UnlimitedHigh)
	{
		const double B = FMath::Max(Blend, 0.0);
		const double Low = Min <= UnlimitedLow ? 1.0 : TM::SmoothStep(Min - B, Min + B, Value);
		const double High = Max >= UnlimitedHigh ? 1.0 : 1.0 - TM::SmoothStep(Max - B, Max + B, Value);
		return Low * High;
	}

	double EdgeTaper(const FBox2D& AreaCm, const FVector2D& P, double EdgeCm)
	{
		if (EdgeCm <= 0.0)
		{
			return 1.0;
		}
		const double D = FMath::Min(FMath::Min(P.X - AreaCm.Min.X, AreaCm.Max.X - P.X), FMath::Min(P.Y - AreaCm.Min.Y, AreaCm.Max.Y - P.Y));
		return TM::SmoothStep(0.0, EdgeCm, D);
	}
}

FString ULandscapePaintTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderPaintLayerListResult ULandscapePaintTools::ListPaintLayers(const FString& LandscapeName)
{
	FWorldBuilderPaintLayerListResult Result;
	UWorld* World = GetEditorWorld();
	ALandscape* Landscape = World ? LandscapeUtils::Resolve(World, LandscapeName, nullptr, Result.Message) : nullptr;
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
	}
	if (!Landscape)
	{
		return Result;
	}

	Result = BuildLayerList(Landscape);
	int32 Missing = 0, NoBlend = 0;
	for (const FWorldBuilderPaintLayerStatus& L : Result.Layers)
	{
		Missing += L.bHasLayerInfo ? 0 : 1;
		NoBlend += (L.bHasLayerInfo && L.BlendMethod == TEXT("None")) ? 1 : 0;
	}
	Result.bSuccess = true;
	Result.Message = Result.Layers.IsEmpty()
		? FString::Printf(TEXT("Landscape '%s' has no paint layers. Its material needs a Landscape Layer Blend node with named layers."), *Result.LandscapeName)
		: FString::Printf(TEXT("%d paint layer(s) on '%s'.%s%s"), Result.Layers.Num(), *Result.LandscapeName,
			Missing > 0 ? *FString::Printf(TEXT(" %d have no Layer Info; run CreateLayerInfos before painting."), Missing) : TEXT(""),
			NoBlend > 0 ? *FString::Printf(TEXT(" %d use no weight blending (painting them won't reduce other layers); CreateLayerInfos with bUpdateExisting fixes that."), NoBlend) : TEXT(""));
	return Result;
}

FWorldBuilderPaintLayerListResult ULandscapePaintTools::CreateLayerInfos(const TArray<FString>& LayerNames, const FString& FolderPath,
	EWorldBuilderWeightBlend BlendMethod, bool bUpdateExisting, const FString& LandscapeName)
{
	FWorldBuilderPaintLayerListResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, nullptr, Result.Message);
	if (!Landscape)
	{
		return Result;
	}
	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		Result.Message = TEXT("Landscape has no landscape info.");
		return Result;
	}
	FString Folder = FolderPath;
	Folder.RemoveFromEnd(TEXT("/"));
	if (!Folder.StartsWith(TEXT("/Game/")) && Folder != TEXT("/Game"))
	{
		Result.Message = TEXT("FolderPath must be a content folder starting with /Game/, e.g. /Game/Landscape/LayerInfos.");
		return Result;
	}

	TArray<FName> Names;
	if (LayerNames.IsEmpty())
	{
		Names = Landscape->RetrieveTargetLayerNamesFromMaterials(/*bIncludeVisibilityLayer=*/false);
		if (Names.IsEmpty())
		{
			Result.Message = TEXT("The landscape material has no paint layers (no Landscape Layer Blend node). Assign a layered landscape material first, or pass LayerNames.");
			return Result;
		}
	}
	else
	{
		for (const FString& Name : LayerNames)
		{
			if (!Name.IsEmpty())
			{
				Names.AddUnique(FName(*Name));
			}
		}
	}

	const ELandscapeTargetLayerBlendMethod Method = ToEngine(BlendMethod);
	FScopedTransaction Transaction(LOCTEXT("CreateLayerInfos", "AI: Create Landscape Layer Infos"));
	Landscape->Modify();

	TArray<FString> Created, Updated;
	for (const FName& Name : Names)
	{
		const FLandscapeTargetLayerSettings* Existing = Landscape->GetTargetLayers().Find(Name);
		ULandscapeLayerInfoObject* ExistingInfo = Existing ? Existing->LayerInfoObj.Get() : nullptr;
		if (ExistingInfo)
		{
			if (bUpdateExisting && ExistingInfo->GetBlendMethod() != Method)
			{
				ExistingInfo->SetBlendMethod(Method, /*bInModify=*/true);
				Updated.Add(Name.ToString());
			}
			continue;
		}

		FName ObjectName;
		UE::Landscape::GetLayerInfoObjectPackageName(Name, Folder, ObjectName);
		ULandscapeLayerInfoObject* LayerInfo = UE::Landscape::CreateTargetLayerInfo(Name, Folder, ObjectName.ToString());
		if (!LayerInfo)
		{
			Result.Message = FString::Printf(TEXT("Failed to create a Layer Info for '%s' in %s."), *Name.ToString(), *Folder);
			return Result;
		}
		LayerInfo->SetBlendMethod(Method, /*bInModify=*/false);

		if (Landscape->HasTargetLayer(Name))
		{
			Landscape->UpdateTargetLayer(Name, FLandscapeTargetLayerSettings(LayerInfo));
		}
		else
		{
			Landscape->AddTargetLayer(Name, FLandscapeTargetLayerSettings(LayerInfo));
		}
		Info->CreateTargetLayerSettingsFor(LayerInfo);
		Created.Add(FString::Printf(TEXT("%s (%s)"), *Name.ToString(), *LayerInfo->GetPathName()));
	}
	Info->UpdateLayerInfoMap(Landscape);

	Result = BuildLayerList(Landscape);
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created %d Layer Info asset(s)%s%s. Updated blend method on %d existing layer(s)%s. Blend method: %s. Save all (Ctrl+Shift+S) to keep the new assets."),
		Created.Num(), Created.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Created, TEXT(", ")),
		Updated.Num(), Updated.IsEmpty() ? TEXT("") : *(TEXT(" (") + FString::Join(Updated, TEXT(", ")) + TEXT(")")),
		*BlendMethodName(Method));
	return Result;
}

FWorldBuilderPaintResult ULandscapePaintTools::PaintLayer(const FString& LayerName, double CenterXM, double CenterYM, double RadiusM, double Strength,
	EWorldBuilderFalloff Falloff, const FString& LandscapeName)
{
	if (RadiusM <= 0.0)
	{
		return FailPaint(TEXT("RadiusM must be greater than 0."));
	}
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return FailPaint(TEXT("No level is open in the editor."));
	}
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	FString Error;
	ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, &CenterCm, Error);
	if (!Landscape)
	{
		return FailPaint(Error);
	}
	const double RadiusCm = MetersToCm(RadiusM);
	const double Amount = FMath::Clamp(Strength, 0.0, 1.0);
	const TM::EFalloff FalloffType = ToMath(Falloff);

	return RunPaint(FText::Format(LOCTEXT("PaintLayer", "AI: Paint {0}"), FText::FromString(LayerName)), Landscape,
		FBox2D(CenterCm - FVector2D(RadiusCm), CenterCm + FVector2D(RadiusCm)),
		[&](FPaintGrid& Grid, FString& OutError)
		{
			const int32 Target = Grid.FindLayer(LayerName);
			if (Target == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("No paintable layer named '%s'. Paintable layers: %s. If it's in the material but missing here, run CreateLayerInfos."), *LayerName, *Grid.LayerNames());
				return false;
			}
			for (int32 IY = 0; IY < Grid.Height; ++IY)
			{
				for (int32 IX = 0; IX < Grid.Width; ++IX)
				{
					const double S = Amount * TM::Falloff(FalloffType, FVector2D::Distance(Grid.WorldXY(IX, IY), CenterCm) / RadiusCm);
					if (S <= 0.0)
					{
						continue;
					}
					const int32 I = IY * Grid.Width + IX;
					for (int32 L = 0; L < Grid.Layers.Num(); ++L)
					{
						Grid.Weights[L][I] *= (1.0 - S);
					}
					Grid.Weights[Target][I] += S;
				}
			}
			return true;
		});
}

FWorldBuilderPaintResult ULandscapePaintTools::PaintByRules(const TArray<FWorldBuilderPaintRule>& Rules, double CenterXM, double CenterYM,
	double SizeXM, double SizeYM, double EdgeNoise, double EdgeBlendM, int32 Seed, const FString& LandscapeName)
{
	if (Rules.IsEmpty())
	{
		return FailPaint(TEXT("Give at least one rule, e.g. [{\"layerName\":\"Grass\"}, {\"layerName\":\"Rock\",\"minSlopeDeg\":35}]."));
	}
	if ((SizeXM > 0.0) != (SizeYM > 0.0) || SizeXM < 0.0 || SizeYM < 0.0)
	{
		return FailPaint(TEXT("Give both SizeXM and SizeYM (greater than 0), or leave both at 0 for the whole landscape."));
	}
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return FailPaint(TEXT("No level is open in the editor."));
	}
	const bool bWhole = SizeXM <= 0.0;
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	FString Error;
	ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, bWhole ? nullptr : &CenterCm, Error);
	if (!Landscape)
	{
		return FailPaint(Error);
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
	const double Noise = FMath::Clamp(EdgeNoise, 0.0, 1.0);
	const double EdgeCm = bWhole ? 0.0 : MetersToCm(FMath::Max(EdgeBlendM, 0.0));

	auto Compute = [&](FPaintGrid& Grid, FString& OutError)
	{
		TArray<int32> RuleLayer;
		for (const FWorldBuilderPaintRule& Rule : Rules)
		{
			const int32 L = Grid.FindLayer(Rule.LayerName);
			if (L == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Rule layer '%s' is not paintable. Paintable layers: %s. Run CreateLayerInfos if it's in the material."), *Rule.LayerName, *Grid.LayerNames());
				return false;
			}
			RuleLayer.Add(L);
		}

		TArray<double> W;
		W.SetNumUninitialized(Grid.Layers.Num());
		for (int32 IY = 0; IY < Grid.Height; ++IY)
		{
			for (int32 IX = 0; IX < Grid.Width; ++IX)
			{
				const int32 I = IY * Grid.Width + IX;
				const FVector2D World = Grid.WorldXY(IX, IY);
				const double Taper = EdgeTaper(AreaCm, World, EdgeCm);
				if (Taper <= 0.0)
				{
					continue;
				}
				const double HeightM = CmToMeters(Grid.HeightsCm[I]);
				const double Slope = Grid.SlopesDeg[I];

				for (double& V : W) { V = 0.0; }
				for (int32 R = 0; R < Rules.Num(); ++R)
				{
					const FWorldBuilderPaintRule& Rule = Rules[R];
					// Noise shifts the measured height/slope a little so transitions wander naturally.
					const double HJitter = Noise * Rule.HeightBlendM * 2.0 * TM::FBM(World / 2000.0, 3, Seed + R * 17);
					const double SJitter = Noise * Rule.SlopeBlendDeg * 2.0 * TM::FBM(World / 1500.0 + FVector2D(31.7), 3, Seed + R * 31);
					double M = RangeMask(HeightM + HJitter, Rule.MinHeightM, Rule.MaxHeightM, Rule.HeightBlendM, -NoHeightLimitM, NoHeightLimitM)
						* RangeMask(Slope + SJitter, Rule.MinSlopeDeg, Rule.MaxSlopeDeg, Rule.SlopeBlendDeg, 0.0, 90.0);
					M *= FMath::Clamp(Rule.Strength, 0.0, 1.0);
					if (M <= 0.0)
					{
						continue;
					}
					for (double& V : W) { V *= (1.0 - M); }
					W[RuleLayer[R]] += M;
				}

				double Sum = 0.0;
				for (double V : W) { Sum += V; }
				if (Sum < 1e-6)
				{
					W[RuleLayer[0]] = 1.0;
					Sum = 1.0;
				}
				for (int32 L = 0; L < Grid.Layers.Num(); ++L)
				{
					Grid.Weights[L][I] = FMath::Lerp(Grid.Weights[L][I], W[L] / Sum, Taper);
				}
			}
		}
		return true;
	};

	// Large areas in tiles; coverage is averaged by sample count.
	const TArray<FBox2D> Tiles = SplitIntoTiles(Landscape, AreaCm);
	FWorldBuilderPaintResult Total;
	TMap<FString, double> WeightedCoverage;
	for (int32 T = 0; T < Tiles.Num(); ++T)
	{
		FWorldBuilderPaintResult Tile = RunPaint(LOCTEXT("PaintByRules", "AI: Paint By Rules"), Landscape, Tiles[T], Compute);
		if (!Tile.bSuccess)
		{
			if (Tiles.Num() > 1)
			{
				Tile.Message = FString::Printf(TEXT("Tile %d of %d failed (%d earlier tile(s) were applied): %s"), T + 1, Tiles.Num(), T, *Tile.Message);
			}
			return Tile;
		}
		for (const TPair<FString, double>& Pair : Tile.LayerCoverage)
		{
			WeightedCoverage.FindOrAdd(Pair.Key) += Pair.Value * Tile.SampleCount;
		}
		if (T == 0)
		{
			Total = Tile;
		}
		else
		{
			Total.SampleCount += Tile.SampleCount;
			Total.bCreatedEditLayer |= Tile.bCreatedEditLayer;
			Total.RegionMinM = FVector2D::Min(Total.RegionMinM, Tile.RegionMinM);
			Total.RegionMaxM = FVector2D::Max(Total.RegionMaxM, Tile.RegionMaxM);
		}
	}
	if (Tiles.Num() > 1)
	{
		Total.LayerCoverage.Reset();
		TArray<FString> Coverage;
		for (const TPair<FString, double>& Pair : WeightedCoverage)
		{
			Total.LayerCoverage.Add(Pair.Key, Pair.Value / FMath::Max(Total.SampleCount, 1));
			Coverage.Add(FString::Printf(TEXT("%s %.0f%%"), *Pair.Key, 100.0 * Pair.Value / FMath::Max(Total.SampleCount, 1)));
		}
		Total.Message = FString::Printf(TEXT("Painted by rules in %d tiles (one undo step each). AI paint coverage: %s."), Tiles.Num(), *FString::Join(Coverage, TEXT(", ")));
	}
	return Total;
}

#undef LOCTEXT_NAMESPACE
