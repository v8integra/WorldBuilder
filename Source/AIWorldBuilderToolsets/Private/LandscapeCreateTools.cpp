#include "LandscapeCreateTools.h"

#include "LandscapeEditPipeline.h"

#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AIWorldBuilderTerrainMath.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeImportHelper.h"
#include "LandscapeInfo.h"
#include "LandscapeSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIWorldBuilderCreate"

using namespace AIWorldBuilder;
using namespace AIWorldBuilder::EditPipeline;
namespace TM = AIWorldBuilder::TerrainMath;

namespace
{
	/** Unreal's New Landscape UI limit: at most 8191 quads (8192 samples) per side. */
	constexpr int32 MaxQuadsPerSide = 8191;

	TM::EHeightBlendMode ToMath(EWorldBuilderBlendMode Mode)
	{
		switch (Mode)
		{
		case EWorldBuilderBlendMode::Add:     return TM::EHeightBlendMode::Add;
		case EWorldBuilderBlendMode::Max:     return TM::EHeightBlendMode::Max;
		case EWorldBuilderBlendMode::Min:     return TM::EHeightBlendMode::Min;
		case EWorldBuilderBlendMode::Blend:   return TM::EHeightBlendMode::Blend;
		case EWorldBuilderBlendMode::Replace:
		default:                              return TM::EHeightBlendMode::Replace;
		}
	}

	TM::ETerrainPreset ToMath(EWorldBuilderTerrainPreset Preset)
	{
		switch (Preset)
		{
		case EWorldBuilderTerrainPreset::Mountains: return TM::ETerrainPreset::Mountains;
		case EWorldBuilderTerrainPreset::Islands:   return TM::ETerrainPreset::Islands;
		case EWorldBuilderTerrainPreset::Canyons:   return TM::ETerrainPreset::Canyons;
		case EWorldBuilderTerrainPreset::Plains:    return TM::ETerrainPreset::Plains;
		case EWorldBuilderTerrainPreset::RollingHills:
		default:                                    return TM::ETerrainPreset::RollingHills;
		}
	}

	bool IsAuto(const FString& Value)
	{
		return Value.IsEmpty() || Value.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
	}

	/**
	 * Region for tools that work on "a region or the whole landscape": SizeXM/SizeYM of 0 means the landscape's full bounds.
	 * Resolves the landscape (by name, by the region centre, or the only one).
	 */
	ALandscape* ResolveRegion(const FString& LandscapeName, double CenterXM, double CenterYM, double SizeXM, double SizeYM,
		FBox2D& OutAreaCm, bool& bOutWhole, FString& OutError)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			OutError = TEXT("No level is open in the editor.");
			return nullptr;
		}
		if ((SizeXM > 0.0) != (SizeYM > 0.0) || SizeXM < 0.0 || SizeYM < 0.0)
		{
			OutError = TEXT("Give both SizeXM and SizeYM (greater than 0), or leave both at 0 for the whole landscape.");
			return nullptr;
		}
		bOutWhole = SizeXM <= 0.0;
		const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
		ALandscape* Landscape = LandscapeUtils::Resolve(World, LandscapeName, bOutWhole ? nullptr : &CenterCm, OutError);
		if (!Landscape)
		{
			return nullptr;
		}
		if (bOutWhole)
		{
			const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
			OutAreaCm = FBox2D(FVector2D(Bounds.Min.X, Bounds.Min.Y), FVector2D(Bounds.Max.X, Bounds.Max.Y));
		}
		else
		{
			const FVector2D Half = FVector2D(SizeXM, SizeYM) * (CmPerMeter * 0.5);
			OutAreaCm = FBox2D(CenterCm - Half, CenterCm + Half);
		}
		return Landscape;
	}

	/** Weight that ramps 0 -> 1 over EdgeCm inward from the area border (1 everywhere if EdgeCm <= 0). */
	double EdgeTaper(const FBox2D& AreaCm, const FVector2D& P, double EdgeCm)
	{
		if (EdgeCm <= 0.0)
		{
			return 1.0;
		}
		const double D = FMath::Min(FMath::Min(P.X - AreaCm.Min.X, AreaCm.Max.X - P.X), FMath::Min(P.Y - AreaCm.Min.Y, AreaCm.Max.Y - P.Y));
		return TM::SmoothStep(0.0, EdgeCm, D);
	}

	/** Runs Compute over Area in tiles (one undo step each) and sums the results. */
	FWorldBuilderSculptResult RunTiled(const FText& TransactionName, ALandscape* Landscape, const FBox2D& AreaCm, bool bAllowClipping,
		TFunctionRef<bool(FSculptGrid&, FString&)> Compute, int32& OutTileCount)
	{
		const TArray<FBox2D> Tiles = SplitIntoTiles(Landscape, AreaCm);
		OutTileCount = Tiles.Num();
		const FString Name = LandscapeUtils::GetDisplayName(Landscape);
		FWorldBuilderSculptResult Total;
		for (int32 I = 0; I < Tiles.Num(); ++I)
		{
			const FWorldBuilderSculptResult Tile = RunSculpt(TransactionName, Name, Tiles[I].GetCenter(), Tiles[I], bAllowClipping, Compute);
			if (!Tile.bSuccess)
			{
				FWorldBuilderSculptResult Failed = Tile;
				Failed.Message = Tiles.Num() > 1
					? FString::Printf(TEXT("Tile %d of %d failed (%d earlier tile(s) were applied; undo them with Ctrl+Z if needed): %s"), I + 1, Tiles.Num(), I, *Tile.Message)
					: Tile.Message;
				return Failed;
			}
			AccumulateTileResult(Total, Tile, I == 0);
		}
		if (Tiles.Num() > 1)
		{
			Total.Message = FString::Printf(TEXT("%s in %d tiles (one undo step each): %d samples changed (raised up to %.1f m, lowered up to %.1f m). Heights now %.1f to %.1f m.%s"),
				*TransactionName.ToString(), Tiles.Num(), Total.ChangedSampleCount, Total.MaxChangeM, -Total.MinChangeM,
				Total.NewMinHeightM, Total.NewMaxHeightM, Total.bCollisionVerified ? TEXT(" Collision verified.") : TEXT(" Collision not confirmed on every tile; check with SampleHeight."));
		}
		return Total;
	}

	/** Bilinear sample of a W x H uint16 grid at fractional pixel coordinates (clamped). */
	double SampleBilinear(const TArray<uint16>& Data, int32 W, int32 H, double PX, double PY)
	{
		PX = FMath::Clamp(PX, 0.0, double(W - 1));
		PY = FMath::Clamp(PY, 0.0, double(H - 1));
		const int32 X0 = FMath::Min(FMath::FloorToInt32(PX), FMath::Max(W - 2, 0));
		const int32 Y0 = FMath::Min(FMath::FloorToInt32(PY), FMath::Max(H - 2, 0));
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const double TX = PX - X0, TY = PY - Y0;
		const double A = FMath::Lerp(double(Data[Y0 * W + X0]), double(Data[Y0 * W + X1]), TX);
		const double B = FMath::Lerp(double(Data[Y1 * W + X0]), double(Data[Y1 * W + X1]), TX);
		return FMath::Lerp(A, B, TY);
	}

	FString HeightmapDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AIWorldBuilder") / TEXT("Heightmaps"));
	}
}

FString ULandscapeCreateTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FWorldBuilderCreateLandscapeResult ULandscapeCreateTools::CreateLandscape(double SizeXKm, double SizeYKm, double CenterXM, double CenterYM,
	double BaseHeightM, double MaxHeightM, double SampleSpacingM, int32 QuadsPerSection, int32 SectionsPerComponent,
	int32 WorldPartitionGridSize, const FString& MaterialPath, const FString& Label)
{
	FWorldBuilderCreateLandscapeResult Result;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		Result.Message = TEXT("No level is open in the editor.");
		return Result;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		Result.Message = TEXT("Stop Play-In-Editor before creating a landscape.");
		return Result;
	}
	if (SizeXKm <= 0.0 || SizeYKm <= 0.0)
	{
		Result.Message = TEXT("SizeXKm and SizeYKm must be greater than 0.");
		return Result;
	}
	static const TArray<int32> ValidQuads = { 7, 15, 31, 63, 127, 255 };
	if (!ValidQuads.Contains(QuadsPerSection))
	{
		Result.Message = TEXT("QuadsPerSection must be 7, 15, 31, 63, 127 or 255.");
		return Result;
	}
	if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
	{
		Result.Message = TEXT("SectionsPerComponent must be 1 or 2.");
		return Result;
	}
	if (SampleSpacingM < 0.1 || SampleSpacingM > 64.0)
	{
		Result.Message = TEXT("SampleSpacingM must be between 0.1 and 64.");
		return Result;
	}
	if (MaxHeightM < 1.0 || MaxHeightM > 50000.0)
	{
		Result.Message = TEXT("MaxHeightM must be between 1 and 50000.");
		return Result;
	}
	if (WorldPartitionGridSize < 1 || WorldPartitionGridSize > 64)
	{
		Result.Message = TEXT("WorldPartitionGridSize must be between 1 and 64.");
		return Result;
	}

	// Nearest valid size: whole components, at most 8191 quads (and 256 components) per side.
	const int32 QuadsPerComponent = QuadsPerSection * SectionsPerComponent;
	const int32 MaxComponents = FMath::Min(256, MaxQuadsPerSide / QuadsPerComponent);
	auto ComponentsFor = [&](double SizeKm)
	{
		return FMath::Clamp(FMath::RoundToInt32(SizeKm * 1000.0 / SampleSpacingM / QuadsPerComponent), 1, MaxComponents);
	};
	const int32 ComponentsX = ComponentsFor(SizeXKm);
	const int32 ComponentsY = ComponentsFor(SizeYKm);
	const int32 SizeX = ComponentsX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentsY * QuadsPerComponent + 1;
	const bool bClampedSize = ComponentsX == MaxComponents || ComponentsY == MaxComponents;

	// Material.
	UMaterialInterface* Material = nullptr;
	if (IsAuto(MaterialPath))
	{
		for (const ALandscape* Existing : LandscapeUtils::GetAllLandscapes(World))
		{
			if (Existing->LandscapeMaterial)
			{
				Material = Existing->LandscapeMaterial;
				break;
			}
		}
	}
	else if (!MaterialPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (!Material)
		{
			Result.Message = FString::Printf(TEXT("Material '%s' not found. Use an asset path like /Game/Materials/M_Landscape.M_Landscape, \"auto\" or \"none\"."), *MaterialPath);
			return Result;
		}
	}

	// Unique label.
	TSet<FString> UsedLabels;
	for (const ALandscape* Existing : LandscapeUtils::GetAllLandscapes(World))
	{
		UsedLabels.Add(LandscapeUtils::GetDisplayName(Existing));
	}
	const FString BaseLabel = IsAuto(Label) ? FString(TEXT("Landscape")) : Label;
	FString UniqueLabel = BaseLabel;
	for (int32 Suffix = 2; UsedLabels.Contains(UniqueLabel); ++Suffix)
	{
		UniqueLabel = FString::Printf(TEXT("%s%d"), *BaseLabel, Suffix);
	}

	const double ScaleXY = MetersToCm(SampleSpacingM);
	const double ScaleZ = MetersToCm(MaxHeightM) / 256.0;	// local units span +/-256 around the base
	const FVector Location(MetersToCm(CenterXM) - (SizeX - 1) * ScaleXY * 0.5, MetersToCm(CenterYM) - (SizeY - 1) * ScaleXY * 0.5, MetersToCm(BaseHeightM));

	FScopedTransaction Transaction(LOCTEXT("CreateLandscape", "AI: Create Landscape"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Landscape)
	{
		Transaction.Cancel();
		Result.Message = TEXT("Failed to spawn the landscape actor.");
		return Result;
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));
	// Same lighting LOD rule as the editor's New Landscape tool (keeps Lightmass happy on big landscapes).
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

	TMap<FGuid, TArray<uint16>> HeightData;
	TArray<uint16>& Flat = HeightData.Add(FGuid());
	Flat.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), SizeX * SizeY);
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
	MaterialLayers.Add(FGuid());

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, SectionsPerComponent, QuadsPerSection, HeightData,
		TEXT(""), MaterialLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	Landscape->SetActorLabel(UniqueLabel);

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		Result.Message = TEXT("The landscape was spawned but has no landscape info; check the Output Log.");
		return Result;
	}
	Info->UpdateLayerInfoMap(Landscape);

	ULandscapeSubsystem* Subsystem = World->GetSubsystem<ULandscapeSubsystem>();
	if (Subsystem && Subsystem->IsGridBased())
	{
		Subsystem->ChangeGridSize(Info, static_cast<uint32>(WorldPartitionGridSize));
		Result.bWorldPartition = true;
	}

	Result.LandscapeName = UniqueLabel;
	Result.ResolutionX = SizeX;
	Result.ResolutionY = SizeY;
	Result.ComponentCountX = ComponentsX;
	Result.ComponentCountY = ComponentsY;
	Result.SampleSpacingM = SampleSpacingM;
	Result.SizeXM = (SizeX - 1) * SampleSpacingM;
	Result.SizeYM = (SizeY - 1) * SampleSpacingM;
	Result.ScaleZ = ScaleZ;
	double MinCm = 0.0, MaxCm = 0.0;
	LandscapeMath::GetHeightRangeCm(ScaleZ, Location.Z, MinCm, MaxCm);
	Result.MinPossibleHeightM = CmToMeters(MinCm);
	Result.MaxPossibleHeightM = CmToMeters(MaxCm);
	const FBox Bounds = LandscapeUtils::GetCompleteBounds(Landscape);
	Result.BoundsMinM = Bounds.Min / CmPerMeter;
	Result.BoundsMaxM = Bounds.Max / CmPerMeter;
	Result.StreamingProxyCount = Info->GetSortedStreamingProxies().Num();
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Created landscape '%s': %.0f x %.0f m (%d x %d samples, %d x %d components of %d quads, %.2f m spacing), height range %.0f to %.0f m%s.%s%s Save the level to keep it."),
		*UniqueLabel, Result.SizeXM, Result.SizeYM, SizeX, SizeY, ComponentsX, ComponentsY, QuadsPerComponent, SampleSpacingM,
		Result.MinPossibleHeightM, Result.MaxPossibleHeightM,
		Result.bWorldPartition ? *FString::Printf(TEXT(", %d World Partition streaming proxies"), Result.StreamingProxyCount) : TEXT(""),
		Material ? *FString::Printf(TEXT(" Material: %s."), *Material->GetName()) : TEXT(" No material set (default grid)."),
		bClampedSize ? TEXT(" Size was clamped to Unreal's 8191-quad limit; use a larger SampleSpacingM for bigger maps.") : TEXT(""));
	return Result;
}

FWorldBuilderSculptResult ULandscapeCreateTools::GenerateTerrain(EWorldBuilderTerrainPreset Preset, double CenterXM, double CenterYM,
	double SizeXM, double SizeYM, double BaseHeightM, double AmplitudeM, double WavelengthM, int32 Seed,
	int32 ErosionIterations, double ErosionTalusDeg, double EdgeBlendM, EWorldBuilderBlendMode BlendMode, bool bAllowClipping, const FString& LandscapeName)
{
	if (WavelengthM <= 0.0)
	{
		return Fail(TEXT("WavelengthM must be greater than 0."));
	}
	if (AmplitudeM < 0.0)
	{
		return Fail(TEXT("AmplitudeM must be 0 or more."));
	}

	FBox2D AreaCm;
	bool bWhole = false;
	FString Error;
	ALandscape* Landscape = ResolveRegion(LandscapeName, CenterXM, CenterYM, SizeXM, SizeYM, AreaCm, bWhole, Error);
	if (!Landscape)
	{
		return Fail(Error);
	}

	const TM::ETerrainPreset MathPreset = ToMath(Preset);
	const TM::EHeightBlendMode Mode = ToMath(BlendMode);
	const double BaseCm = MetersToCm(BaseHeightM);
	const double AmplitudeCm = MetersToCm(AmplitudeM);
	const double WavelengthCm = MetersToCm(WavelengthM);
	const double EdgeCm = bWhole ? 0.0 : MetersToCm(FMath::Max(EdgeBlendM, 0.0));
	const int32 Iterations = FMath::Clamp(ErosionIterations, 0, 500);
	const int32 TileCount = SplitIntoTiles(Landscape, AreaCm).Num();
	const bool bErode = Iterations > 0 && TileCount == 1;

	auto Compute = [&](FSculptGrid& Grid, FString& OutError)
	{
		// Target terrain over the whole grid, so erosion can see neighbours.
		TArray<double> Target;
		Target.SetNumUninitialized(Grid.Width * Grid.Height);
		Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
		{
			const FVector2D UV = (World - AreaCm.Min) / AreaCm.GetSize();
			Target[I] = BaseCm + TM::PresetHeight(MathPreset, World / WavelengthCm, UV, AmplitudeCm, Seed);
		});
		if (bErode)
		{
			TM::ThermalErosion(Target, Grid.Width, Grid.Height, Grid.SpacingCm, ErosionTalusDeg, Iterations);
		}
		Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
		{
			if (!AreaCm.IsInside(World) && !bWhole)
			{
				return;
			}
			const double W = EdgeTaper(AreaCm, World, EdgeCm);
			Grid.New[I] = TM::BlendHeight(Mode, Grid.Current[I], BaseCm, Target[I] - BaseCm, W, 1.0);
		});
		return true;
	};

	const FString PresetName = StaticEnum<EWorldBuilderTerrainPreset>()->GetNameStringByValue(int64(Preset));
	int32 Tiles = 0;
	FWorldBuilderSculptResult Result = RunTiled(FText::Format(LOCTEXT("Generate", "AI: Generate {0}"), FText::FromString(PresetName)),
		Landscape, AreaCm, bAllowClipping, Compute, Tiles);
	if (Result.bSuccess && Iterations > 0 && !bErode)
	{
		Result.Message += TEXT(" Erosion was skipped because the region spans several tiles; run GenerateTerrain on smaller regions (up to 4 km at 1 m spacing) to erode.");
	}
	return Result;
}

FWorldBuilderSculptResult ULandscapeCreateTools::ImportHeightmap(const FString& FilePath, double CenterXM, double CenterYM, double SizeXM, double SizeYM,
	EWorldBuilderHeightEncoding Encoding, double MinHeightM, double MaxHeightM, EWorldBuilderBlendMode BlendMode, double EdgeBlendM, bool bFlipY,
	bool bAllowClipping, const FString& LandscapeName)
{
	if (!FPaths::FileExists(FilePath))
	{
		return Fail(FString::Printf(TEXT("File not found: %s (use an absolute path)."), *FilePath));
	}
	const FString Ext = FPaths::GetExtension(FilePath).ToLower();
	if (Ext != TEXT("png") && Ext != TEXT("r16") && Ext != TEXT("raw"))
	{
		return Fail(TEXT("Heightmaps must be 16-bit .png, .r16 or .raw files."));
	}
	if (Encoding == EWorldBuilderHeightEncoding::Range && MaxHeightM <= MinHeightM)
	{
		return Fail(TEXT("MaxHeightM must be greater than MinHeightM for Range encoding."));
	}

	FBox2D AreaCm;
	bool bWhole = false;
	FString Error;
	ALandscape* Landscape = ResolveRegion(LandscapeName, CenterXM, CenterYM, SizeXM, SizeYM, AreaCm, bWhole, Error);
	if (!Landscape)
	{
		return Fail(Error);
	}

	// A file named like name_x0_y0.png is treated as one tile of a set.
	FIntPoint Coord;
	FString Pattern;
	const bool bTiled = FLandscapeImportHelper::ExtractCoordinates(FPaths::GetBaseFilename(FilePath), Coord, Pattern);

	FLandscapeImportDescriptor Descriptor;
	FText ImportMessage;
	if (FLandscapeImportHelper::GetHeightmapImportDescriptor(FilePath, /*bSingleFile=*/!bTiled, bFlipY, Descriptor, ImportMessage) == ELandscapeImportResult::Error
		|| Descriptor.ImportResolutions.IsEmpty())
	{
		return Fail(FString::Printf(TEXT("Couldn't read heightmap: %s"), *ImportMessage.ToString()));
	}
	TArray<uint16> Data;
	if (FLandscapeImportHelper::GetHeightmapImportData(Descriptor, 0, Data, ImportMessage) == ELandscapeImportResult::Error)
	{
		return Fail(FString::Printf(TEXT("Couldn't read heightmap data: %s"), *ImportMessage.ToString()));
	}
	const int32 W = int32(Descriptor.ImportResolutions[0].Width);
	const int32 H = int32(Descriptor.ImportResolutions[0].Height);
	if (W < 2 || H < 2 || Data.Num() != W * H)
	{
		return Fail(FString::Printf(TEXT("Unexpected heightmap size %d x %d (%d values)."), W, H, Data.Num()));
	}

	const FTransform LandscapeToWorld = Landscape->LandscapeActorToWorld();
	const double ScaleZ = LandscapeToWorld.GetScale3D().Z;
	const double ActorZ = LandscapeToWorld.GetLocation().Z;
	const TM::EHeightBlendMode Mode = ToMath(BlendMode);
	const double EdgeCm = MetersToCm(FMath::Max(EdgeBlendM, 0.0));
	const double MinCm = MetersToCm(MinHeightM), MaxCm = MetersToCm(MaxHeightM);

	auto Compute = [&](FSculptGrid& Grid, FString& OutError)
	{
		Grid.ForEach([&](int32, int32, const FVector2D& World, int32 I)
		{
			const FVector2D UV = (World - AreaCm.Min) / AreaCm.GetSize();
			if (UV.X < -KINDA_SMALL_NUMBER || UV.Y < -KINDA_SMALL_NUMBER || UV.X > 1.0 + KINDA_SMALL_NUMBER || UV.Y > 1.0 + KINDA_SMALL_NUMBER)
			{
				return;
			}
			// Columns along +X, rows along +Y.
			const double Value = SampleBilinear(Data, W, H, UV.X * (W - 1), UV.Y * (H - 1));
			const double HeightCm = Encoding == EWorldBuilderHeightEncoding::Native
				? (Value - LandscapeDataAccess::MidValue) * LANDSCAPE_ZSCALE * ScaleZ + ActorZ
				: FMath::Lerp(MinCm, MaxCm, Value / 65535.0);
			Grid.New[I] = TM::BlendHeight(Mode, Grid.Current[I], 0.0, HeightCm, EdgeTaper(AreaCm, World, EdgeCm), 1.0);
		});
		return true;
	};

	int32 Tiles = 0;
	FWorldBuilderSculptResult Result = RunTiled(LOCTEXT("ImportHeightmap", "AI: Import Heightmap"), Landscape, AreaCm, bAllowClipping, Compute, Tiles);
	if (Result.bSuccess)
	{
		Result.Message = FString::Printf(TEXT("Imported %s (%d x %d%s) onto %.0f x %.0f m. %s"),
			*FPaths::GetCleanFilename(FilePath), W, H, bTiled ? TEXT(", tiled set") : TEXT(""),
			CmToMeters(AreaCm.GetSize().X), CmToMeters(AreaCm.GetSize().Y), *Result.Message);
	}
	return Result;
}

FWorldBuilderHeightmapExportResult ULandscapeCreateTools::ExportHeightmap(const FString& FilePath, double CenterXM, double CenterYM,
	double SizeXM, double SizeYM, const FString& LandscapeName)
{
	FWorldBuilderHeightmapExportResult Result;

	FBox2D AreaCm;
	bool bWhole = false;
	ALandscape* Landscape = ResolveRegion(LandscapeName, CenterXM, CenterYM, SizeXM, SizeYM, AreaCm, bWhole, Result.Message);
	if (!Landscape)
	{
		return Result;
	}
	Result.LandscapeName = LandscapeUtils::GetDisplayName(Landscape);
	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		Result.Message = TEXT("Landscape has no landscape info.");
		return Result;
	}

	// Output path.
	FString Path = FilePath;
	if (IsAuto(Path))
	{
		Path = HeightmapDirectory() / FString::Printf(TEXT("Heightmap_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s")));
	}
	else if (FPaths::IsRelative(Path))
	{
		Path = HeightmapDirectory() / Path;
	}
	const FString Ext = FPaths::GetExtension(Path).ToLower();
	if (Ext != TEXT("png") && Ext != TEXT("r16") && Ext != TEXT("raw"))
	{
		Result.Message = TEXT("FilePath must end in .png, .r16 or .raw.");
		return Result;
	}

	// Inclusive sample rectangle for the area, clipped to the landscape.
	const FTransform LandscapeToWorld = Landscape->LandscapeActorToWorld();
	const FVector LocalMin = LandscapeToWorld.InverseTransformPosition(FVector(AreaCm.Min.X, AreaCm.Min.Y, 0.0));
	const FVector LocalMax = LandscapeToWorld.InverseTransformPosition(FVector(AreaCm.Max.X, AreaCm.Max.Y, 0.0));
	const FIntRect Extent = LandscapeUtils::GetCompleteExtent(Landscape);
	const FIntRect Rect(
		FMath::Max(FMath::RoundToInt32(FMath::Min(LocalMin.X, LocalMax.X)), Extent.Min.X),
		FMath::Max(FMath::RoundToInt32(FMath::Min(LocalMin.Y, LocalMax.Y)), Extent.Min.Y),
		FMath::Min(FMath::RoundToInt32(FMath::Max(LocalMin.X, LocalMax.X)), Extent.Max.X),
		FMath::Min(FMath::RoundToInt32(FMath::Max(LocalMin.Y, LocalMax.Y)), Extent.Max.Y));
	const int32 Width = Rect.Max.X - Rect.Min.X + 1;
	const int32 Height = Rect.Max.Y - Rect.Min.Y + 1;
	if (Width < 2 || Height < 2)
	{
		Result.Message = TEXT("The region does not overlap the landscape.");
		return Result;
	}
	if (Width > MaxQuadsPerSide + 2 || Height > MaxQuadsPerSide + 2)
	{
		Result.Message = FString::Printf(TEXT("Region is %d x %d samples; export at most 8193 per side. Use a smaller region."), Width, Height);
		return Result;
	}

	// Refuse unloaded areas (World Partition).
	const int32 CSQ = FMath::Max(Info->ComponentSizeQuads, 1);
	for (int32 KY = Rect.Min.Y / CSQ; KY <= FMath::Max(Rect.Max.Y - 1, Rect.Min.Y) / CSQ; ++KY)
	{
		for (int32 KX = Rect.Min.X / CSQ; KX <= FMath::Max(Rect.Max.X - 1, Rect.Min.X) / CSQ; ++KX)
		{
			if (!Info->XYtoComponentMap.Contains(FIntPoint(KX, KY)))
			{
				Result.Message = TEXT("Part of the region is not loaded. In World Partition, load it (World Partition window: select cells, right-click, Load) and try again.");
				return Result;
			}
		}
	}

	TArray<uint16> Values;
	if (!RenderMergedHeights(Landscape, Rect, Values, Result.Message))
	{
		return Result;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	bool bSaved = false;
	if (Ext == TEXT("png"))
	{
		bSaved = FImageUtils::SaveImageByExtension(*Path, FImageView(Values.GetData(), Width, Height, ERawImageFormat::G16));
	}
	else
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Values.Num() * 2);
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			Bytes[I * 2] = uint8(Values[I] & 0xFF);
			Bytes[I * 2 + 1] = uint8(Values[I] >> 8);
		}
		bSaved = FFileHelper::SaveArrayToFile(Bytes, *Path);
	}
	if (!bSaved)
	{
		Result.Message = FString::Printf(TEXT("Failed to write %s."), *Path);
		return Result;
	}

	const double ScaleZ = LandscapeToWorld.GetScale3D().Z;
	const double ActorZ = LandscapeToWorld.GetLocation().Z;
	uint16 MinV = MAX_uint16, MaxV = 0;
	for (uint16 V : Values)
	{
		MinV = FMath::Min(MinV, V);
		MaxV = FMath::Max(MaxV, V);
	}
	const FVector WorldMin = LandscapeToWorld.TransformPosition(FVector(Rect.Min.X, Rect.Min.Y, 0.0));
	const FVector WorldMax = LandscapeToWorld.TransformPosition(FVector(Rect.Max.X, Rect.Max.Y, 0.0));

	Result.FilePath = Path;
	Result.WidthPx = Width;
	Result.HeightPx = Height;
	Result.RegionMinM = FVector2D(WorldMin.X, WorldMin.Y) / CmPerMeter;
	Result.RegionMaxM = FVector2D(WorldMax.X, WorldMax.Y) / CmPerMeter;
	Result.MinHeightM = CmToMeters(LandscapeMath::HeightValueToWorldZCm(MinV, ScaleZ, ActorZ));
	Result.MaxHeightM = CmToMeters(LandscapeMath::HeightValueToWorldZCm(MaxV, ScaleZ, ActorZ));
	Result.Encoding = FString::Printf(TEXT("Native: height_m = (value - 32768) * %.6f + %.3f (ScaleZ %.2f / 12800, plus the landscape's base Z). Re-import with Encoding=Native."),
		ScaleZ / 12800.0, CmToMeters(ActorZ), ScaleZ);
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Exported '%s' %d x %d samples to %s. Heights %.1f to %.1f m. Columns run along +X, rows along +Y; pixel (0,0) is at (%.0f, %.0f) m."),
		*Result.LandscapeName, Width, Height, *Path, Result.MinHeightM, Result.MaxHeightM, Result.RegionMinM.X, Result.RegionMinM.Y);
	return Result;
}

#undef LOCTEXT_NAMESPACE
