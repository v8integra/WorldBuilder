#include "PublishTools.h"

#include "GameToolUtils.h"
#include "PlaytestTools.h"

#include "AGBAudioFX.h"
#include "AGBCameraTour.h"
#include "AGBCraftingStation.h"
#include "AGBCreatureSpawnZone.h"
#include "AGBPlaytest.h"
#include "AGBUITheme.h"
#include "AIWorldBuilderCore.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMapsSettings.h"
#include "GeneralProjectSettings.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LandscapeProxy.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Settings/ProjectPackagingSettings.h"
#include "WaterBodyActor.h"
#include "WaterSplineComponent.h"

#define LOCTEXT_NAMESPACE "PublishTools"

using namespace AIWorldBuilder;

namespace
{
	constexpr double CmPerMeter = 100.0;

	// The packaging job (one at a time).
	struct FPackagingJob
	{
		TSharedPtr<FMonitoredProcess> Process;
		FCriticalSection Lock;
		TArray<FString> Lines;
		TArray<FString> Errors;
		FString Configuration;
		FString OutputFolder;
		FString LogPath;
		FDateTime Started;
		FDateTime Finished;
		int32 ReturnCode = -1;
		bool bRunning = false;
		bool bRan = false;
	};
	FPackagingJob Job;

	FString StoreKitDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("StoreKit");
	}

	FString DefaultOutput(const FString& Configuration)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Packaged") / Configuration);
	}

	int64 FolderSize(const FString& Folder, FString* OutExe = nullptr)
	{
		int64 Total = 0;
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*"), true, false);
		for (const FString& File : Files)
		{
			Total += IFileManager::Get().FileSize(*File);
			if (OutExe && File.EndsWith(TEXT(".exe")) && FPaths::GetBaseFilename(File) == FApp::GetProjectName())
			{
				*OutExe = File;
			}
		}
		return Total;
	}

	bool ContainsPath(const TArray<FDirectoryPath>& Paths, const FString& Path)
	{
		return Paths.ContainsByPredicate([&Path](const FDirectoryPath& Entry) { return Entry.Path.Equals(Path, ESearchCase::IgnoreCase); });
	}

	bool ContainsMap(const TArray<FFilePath>& Maps, const FString& PackageName)
	{
		return Maps.ContainsByPredicate([&PackageName](const FFilePath& Entry) { return Entry.FilePath.Equals(PackageName, ESearchCase::IgnoreCase); });
	}

	FString ConfigurationName(EProjectPackagingBuildConfigurations Configuration)
	{
		return Configuration == EProjectPackagingBuildConfigurations::PPBC_Shipping ? TEXT("Shipping")
			: (Configuration == EProjectPackagingBuildConfigurations::PPBC_DebugGame ? TEXT("DebugGame") : TEXT("Development"));
	}

	bool GroundAt(UWorld* World, double X, double Y, double& OutZ)
	{
		FHitResult Hit;
		if (GameToolUtils::TraceGround(World, X / CmPerMeter, Y / CmPerMeter, Hit))
		{
			OutZ = Hit.ImpactPoint.Z;
			return true;
		}
		return false;
	}

	FAGBPhotoSpot MakeSpot(const FVector& Camera, const FVector& Target, float Hour, const FString& Label)
	{
		FAGBPhotoSpot Spot;
		Spot.Camera = FTransform((Target - Camera).Rotation(), Camera);
		Spot.Hour = Hour;
		Spot.Label = Label;
		return Spot;
	}

	/** Good-looking views found in the level: overlook, sunset, lake shore, base, wildlife, eye-level views. */
	TArray<FAGBPhotoSpot> AutoSpots(UWorld* World)
	{
		TArray<FAGBPhotoSpot> Spots;
		FBox Bounds(ForceInit);
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			Bounds += It->GetComponentsBoundingBox();
		}
		if (!Bounds.IsValid)
		{
			return Spots;
		}
		const FVector Center = Bounds.GetCenter();
		double CenterZ = Center.Z;
		GroundAt(World, Center.X, Center.Y, CenterZ);
		const FVector CenterGround(Center.X, Center.Y, CenterZ);

		// Highest point: an overlook toward the middle, and a sunset view.
		FVector High = CenterGround;
		const FVector Size = Bounds.GetSize() * 0.8;
		for (int32 X = 0; X < 15; ++X)
		{
			for (int32 Y = 0; Y < 15; ++Y)
			{
				const double PX = Center.X - Size.X * 0.5 + Size.X * X / 14.0;
				const double PY = Center.Y - Size.Y * 0.5 + Size.Y * Y / 14.0;
				double Z = 0.0;
				if (GroundAt(World, PX, PY, Z) && Z > High.Z)
				{
					High = FVector(PX, PY, Z);
				}
			}
		}
		const FVector Overlook = High + FVector(0.0, 0.0, 600.0);
		Spots.Add(MakeSpot(Overlook, CenterGround + FVector(0.0, 0.0, (High.Z - CenterZ) * 0.3), 10.f, TEXT("overlook")));
		const FVector Side = FRotator(0.0, 70.0, 0.0).RotateVector(CenterGround - High);
		Spots.Add(MakeSpot(Overlook, High + Side + FVector(0.0, 0.0, -400.0), 18.6f, TEXT("sunset")));

		// Lake shore, looking across the water.
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			const UWaterSplineComponent* Spline = It->GetWaterSpline();
			if (!Spline || It->GetWaterBodyType() != EWaterBodyType::Lake)
			{
				continue;
			}
			const FVector Shore = Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
			const FVector Outward = (Shore - It->GetActorLocation()).GetSafeNormal2D();
			FVector Camera = Shore + Outward * 1800.0;
			double Z = Camera.Z;
			GroundAt(World, Camera.X, Camera.Y, Z);
			Camera.Z = FMath::Max(Z, Shore.Z) + 250.0;
			Spots.Add(MakeSpot(Camera, It->GetActorLocation(), 9.f, TEXT("lake")));
			break;
		}
		// The base (stations placed in the level): at night by the fire.
		for (TActorIterator<AAGBCraftingStation> It(World); It; ++It)
		{
			const FVector Target = It->GetActorLocation() + FVector(0.0, 0.0, 80.0);
			FVector Camera = Target + FVector(900.0, 700.0, 0.0);
			double Z = Camera.Z;
			GroundAt(World, Camera.X, Camera.Y, Z);
			Camera.Z = Z + 220.0;
			Spots.Add(MakeSpot(Camera, Target, 21.5f, TEXT("campfire_night")));
			break;
		}
		// Wildlife area.
		for (TActorIterator<AAGBCreatureSpawnZone> It(World); It; ++It)
		{
			const FVector Target = It->GetActorLocation();
			FVector Camera = Target + FVector(-2200.0, 900.0, 0.0);
			double Z = Camera.Z;
			GroundAt(World, Camera.X, Camera.Y, Z);
			Camera.Z = Z + 200.0;
			Spots.Add(MakeSpot(Camera, Target + FVector(0.0, 0.0, 100.0), 8.f, TEXT("wildlife")));
			break;
		}
		// Eye-level views around the middle.
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const FVector Offset = FRotator(0.0, Index * 60.0 + 20.0, 0.0).RotateVector(FVector(Size.X * 0.22, 0.0, 0.0));
			FVector Camera = CenterGround + Offset;
			double Z = 0.0;
			if (!GroundAt(World, Camera.X, Camera.Y, Z))
			{
				continue;
			}
			Camera.Z = Z + 170.0;
			const FVector Toward = CenterGround + FRotator(0.0, 40.0, 0.0).RotateVector(-Offset) * 0.3;
			Spots.Add(MakeSpot(Camera, FVector(Toward.X, Toward.Y, Z + 120.0), Index % 2 == 0 ? 11.f : 16.5f, FString::Printf(TEXT("view%d"), Index + 1)));
		}
		return Spots;
	}

	TArray<FAGBPhotoSpot> CollectSpots(UWorld* World, int32 Count)
	{
		TArray<FAGBPhotoSpot> Spots = GetDefault<UAGBStoreKitSettings>()->PhotoSpots;
		for (const FAGBPhotoSpot& Auto : AutoSpots(World))
		{
			if (Spots.Num() >= Count)
			{
				break;
			}
			Spots.Add(Auto);
		}
		if (Spots.Num() > Count)
		{
			Spots.SetNum(Count);
		}
		return Spots;
	}

	FString DescribeSpot(const FAGBPhotoSpot& Spot)
	{
		const FVector L = Spot.Camera.GetLocation();
		return FString::Printf(TEXT("%s at (%.0f, %.0f, %.0f) m%s"), *Spot.Label, L.X / CmPerMeter, L.Y / CmPerMeter, L.Z / CmPerMeter,
			Spot.Hour >= 0.f ? *FString::Printf(TEXT(", %.1f h"), Spot.Hour) : TEXT(""));
	}
}

FString UPublishTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FPublishToolsResult UPublishTools::PreparePackaging(const FString& GameName, const FString& Version, const FString& CompanyName, const FString& Description,
	const FString& Configuration)
{
	FPublishToolsResult R;
	UGeneralProjectSettings* General = GetMutableDefault<UGeneralProjectSettings>();
	if (!GameName.IsEmpty())
	{
		General->ProjectName = GameName;
		General->ProjectDisplayedTitle = FText::FromString(GameName);
	}
	if (!Version.IsEmpty())
	{
		General->ProjectVersion = Version;
	}
	if (!CompanyName.IsEmpty())
	{
		General->CompanyName = CompanyName;
		General->CopyrightNotice = FString::Printf(TEXT("Copyright %d %s"), FDateTime::Now().GetYear(), *CompanyName);
	}
	if (!Description.IsEmpty())
	{
		General->Description = Description;
	}
	General->TryUpdateDefaultConfigFile();

	UProjectPackagingSettings* Packaging = GetMutableDefault<UProjectPackagingSettings>();
	if (!Configuration.IsEmpty())
	{
		Packaging->BuildConfiguration = Configuration.Equals(TEXT("Shipping"), ESearchCase::IgnoreCase)
			? EProjectPackagingBuildConfigurations::PPBC_Shipping : EProjectPackagingBuildConfigurations::PPBC_Development;
	}
	Packaging->IncludeCrashReporter = true;
	Packaging->bUseIoStore = true;
	// Maps: the main menu and the game level are opened by name, so they must be listed.
	const UAGBUISettings* UI = GetDefault<UAGBUISettings>();
	for (const TSoftObjectPtr<UWorld>& Map : { UI->MainMenuMap, UI->GameMap })
	{
		const FString PackageName = Map.ToSoftObjectPath().GetLongPackageName();
		if (!PackageName.IsEmpty() && !ContainsMap(Packaging->MapsToCook, PackageName))
		{
			FFilePath Entry;
			Entry.FilePath = PackageName;
			Packaging->MapsToCook.Add(Entry);
		}
	}
	// Data the game finds by searching (items, recipes, resources, creatures) and placeholder effects.
	for (const TCHAR* Folder : { TEXT("/Game/AIGameBuilder"), TEXT("/Niagara/DefaultAssets/Templates/Systems") })
	{
		if (!ContainsPath(Packaging->DirectoriesToAlwaysCook, Folder))
		{
			FDirectoryPath Entry;
			Entry.Path = Folder;
			Packaging->DirectoriesToAlwaysCook.Add(Entry);
		}
	}
	Packaging->TryUpdateDefaultConfigFile();

	R.Lines.Add(FString::Printf(TEXT("Game: '%s' version %s by %s - %s"), *General->ProjectName, *General->ProjectVersion,
		General->CompanyName.IsEmpty() ? TEXT("(no company)") : *General->CompanyName, General->Description.IsEmpty() ? TEXT("(no description)") : *General->Description));
	R.Lines.Add(FString::Printf(TEXT("Build: %s, crash reporter on, starts in %s"), *ConfigurationName(Packaging->BuildConfiguration), *UGameMapsSettings::GetGameDefaultMap()));
	R.Lines.Add(TEXT("Maps cooked: ") + FString::JoinBy(Packaging->MapsToCook, TEXT(", "), [](const FFilePath& Map) { return Map.FilePath; }));
	R.Lines.Add(TEXT("Always cooked: ") + FString::JoinBy(Packaging->DirectoriesToAlwaysCook, TEXT(", "), [](const FDirectoryPath& Path) { return Path.Path; }));
	if (UI->GameMap.IsNull())
	{
		R.Lines.Add(TEXT("WARNING: no game level set (UITools.SetupUI with the game level open): the packaged game would have nothing to play."));
	}
	R.bSuccess = true;
	R.Message = TEXT("Ready to package (save all first). Next: PackageGame.");
	return R;
}

FPublishToolsResult UPublishTools::PackageGame(const FString& Configuration, const FString& OutputFolder)
{
	FPublishToolsResult R;
	if (Job.bRunning)
	{
		R.Message = TEXT("Packaging is already running: GetPackagingStatus.");
		return R;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		R.Message = TEXT("Stop Play first.");
		return R;
	}
	const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
	const FString Config = Configuration.IsEmpty() ? ConfigurationName(Packaging->BuildConfiguration)
		: (Configuration.Equals(TEXT("Shipping"), ESearchCase::IgnoreCase) ? FString(TEXT("Shipping")) : FString(TEXT("Development")));
	const FString Output = OutputFolder.IsEmpty() ? DefaultOutput(Config) : FPaths::ConvertRelativePathToFull(OutputFolder);
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString UAT = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.bat"));

	FString Args = FString::Printf(TEXT("BuildCookRun -project=\"%s\" -noP4 -platform=Win64 -clientconfig=%s -build -cook -stage -pak -iostore -archive -archivedirectory=\"%s\" -prereqs -unattended -utf8output -nocompileeditor"),
		*Project, *Config, *Output);
	if (Packaging->IncludeCrashReporter)
	{
		Args += TEXT(" -crashreporter");
	}
	if (Config == TEXT("Shipping"))
	{
		Args += TEXT(" -nodebuginfo");
	}
	Job.Process = MakeShared<FMonitoredProcess>(TEXT("cmd.exe"), FString::Printf(TEXT("/c \"\"%s\" %s\""), *UAT, *Args), true);
	{
		FScopeLock ScopeLock(&Job.Lock);
		Job.Lines.Reset();
		Job.Errors.Reset();
	}
	Job.Configuration = Config;
	Job.OutputFolder = Output;
	Job.Started = FDateTime::Now();
	Job.ReturnCode = -1;
	Job.LogPath = FPaths::ProjectSavedDir() / TEXT("AGBPackaging") / Job.Started.ToString(TEXT("%Y%m%d_%H%M%S_")) + Config + TEXT(".log");
	Job.Process->OnOutput().BindLambda([](FString Line)
	{
		FScopeLock ScopeLock(&Job.Lock);
		Job.Lines.Add(Line);
		if ((Line.Contains(TEXT("error")) && (Line.Contains(TEXT(": error")) || Line.Contains(TEXT("Error:")) || Line.Contains(TEXT("ERROR:"))))
			&& Job.Errors.Num() < 50)
		{
			Job.Errors.Add(Line.TrimStartAndEnd());
		}
	});
	Job.Process->OnCompleted().BindLambda([](int32 ReturnCode)
	{
		FScopeLock ScopeLock(&Job.Lock);
		Job.ReturnCode = ReturnCode;
		Job.bRunning = false;
		Job.Finished = FDateTime::Now();
		FFileHelper::SaveStringToFile(FString::Join(Job.Lines, TEXT("\n")), *Job.LogPath);
	});
	if (!Job.Process->Launch())
	{
		R.Message = TEXT("Could not start the packaging process (RunUAT).");
		return R;
	}
	Job.bRunning = true;
	Job.bRan = true;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Packaging %s build to %s started. This takes 5-60 min (shaders the first time). Poll GetPackagingStatus every few minutes."), *Config, *Output);
	return R;
}

FPublishToolsResult UPublishTools::GetPackagingStatus()
{
	FPublishToolsResult R;
	if (!Job.bRan)
	{
		// A previous editor session: report what is on disk.
		for (const TCHAR* Config : { TEXT("Shipping"), TEXT("Development") })
		{
			FString Exe;
			const int64 Size = FolderSize(DefaultOutput(Config), &Exe);
			if (!Exe.IsEmpty())
			{
				R.Lines.Add(FString::Printf(TEXT("%s build on disk: %s (%.2f GB, built %s)"), Config, *Exe, Size / (1024.0 * 1024.0 * 1024.0),
					*IFileManager::Get().GetTimeStamp(*Exe).ToString(TEXT("%Y-%m-%d %H:%M"))));
			}
		}
		R.bSuccess = true;
		R.Message = TEXT("No packaging run in this editor session.");
		return R;
	}
	FScopeLock ScopeLock(&Job.Lock);
	const FTimespan Elapsed = (Job.bRunning ? FDateTime::Now() : Job.Finished) - Job.Started;
	const FString Time = FString::Printf(TEXT("%d:%02d"), static_cast<int32>(Elapsed.GetTotalMinutes()), Elapsed.GetSeconds());
	// The stage UAT is in, from its last progress lines.
	FString Stage = TEXT("starting");
	for (int32 Index = Job.Lines.Num() - 1; Index >= 0 && Index > Job.Lines.Num() - 400; --Index)
	{
		const FString& Line = Job.Lines[Index];
		if (Line.Contains(TEXT("********** ")))
		{
			Stage = Line.TrimStartAndEnd().Replace(TEXT("*"), TEXT("")).TrimStartAndEnd();
			break;
		}
	}
	if (Job.bRunning)
	{
		R.Message = FString::Printf(TEXT("Packaging %s: running for %s (%s)."), *Job.Configuration, *Time, *Stage);
	}
	else if (Job.ReturnCode == 0)
	{
		FString Exe;
		const int64 Size = FolderSize(Job.OutputFolder, &Exe);
		R.Message = FString::Printf(TEXT("Packaging %s SUCCEEDED in %s."), *Job.Configuration, *Time);
		R.Lines.Add(FString::Printf(TEXT("Game folder: %s (%.2f GB)"), *Job.OutputFolder, Size / (1024.0 * 1024.0 * 1024.0)));
		R.Lines.Add(TEXT("Start it with: ") + (Exe.IsEmpty() ? FString(TEXT("(executable not found)")) : Exe));
		R.Lines.Add(TEXT("To share: zip the Windows folder inside it (itch.io), or upload it with SteamPipe (Steam)."));
	}
	else
	{
		R.Message = FString::Printf(TEXT("Packaging %s FAILED (code %d) after %s. Log: %s"), *Job.Configuration, Job.ReturnCode, *Time, *Job.LogPath);
	}
	if (Job.Errors.Num() > 0)
	{
		R.Lines.Add(FString::Printf(TEXT("Errors (%d):"), Job.Errors.Num()));
		for (int32 Index = 0; Index < Job.Errors.Num() && Index < 15; ++Index)
		{
			R.Lines.Add(TEXT("  ") + Job.Errors[Index]);
		}
	}
	R.Lines.Add(TEXT("Recent output:"));
	for (int32 Index = FMath::Max(0, Job.Lines.Num() - 15); Index < Job.Lines.Num(); ++Index)
	{
		R.Lines.Add(TEXT("  ") + Job.Lines[Index].Left(300));
	}
	R.bSuccess = Job.bRunning || Job.ReturnCode == 0;
	return R;
}

FPublishToolsResult UPublishTools::CancelPackaging()
{
	FPublishToolsResult R;
	if (Job.bRunning && Job.Process)
	{
		Job.Process->Cancel(/*bKillTree=*/true);
		R.Message = TEXT("Packaging cancelled.");
	}
	else
	{
		R.Message = TEXT("Nothing is packaging.");
	}
	R.bSuccess = true;
	return R;
}

FPublishToolsResult UPublishTools::AddPhotoSpot(const FString& Label, double Hour)
{
	FPublishToolsResult R;
	if (!GCurrentLevelEditingViewportClient)
	{
		R.Message = TEXT("No level viewport: click into the level viewport first.");
		return R;
	}
	UAGBStoreKitSettings* Settings = GetMutableDefault<UAGBStoreKitSettings>();
	FAGBPhotoSpot Spot;
	Spot.Camera = FTransform(GCurrentLevelEditingViewportClient->GetViewRotation(), GCurrentLevelEditingViewportClient->GetViewLocation());
	Spot.Hour = static_cast<float>(Hour);
	Spot.Label = Label.IsEmpty() ? FString::Printf(TEXT("spot%d"), Settings->PhotoSpots.Num() + 1) : Label.Replace(TEXT(" "), TEXT("_"));
	Settings->PhotoSpots.Add(Spot);
	Settings->TryUpdateDefaultConfigFile();
	for (const FAGBPhotoSpot& Saved : Settings->PhotoSpots)
	{
		R.Lines.Add(DescribeSpot(Saved));
	}
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Saved the current view as '%s' (%d photo spot(s))."), *Spot.Label, Settings->PhotoSpots.Num());
	return R;
}

FPublishToolsResult UPublishTools::ClearPhotoSpots()
{
	UAGBStoreKitSettings* Settings = GetMutableDefault<UAGBStoreKitSettings>();
	Settings->PhotoSpots.Reset();
	Settings->TryUpdateDefaultConfigFile();
	FPublishToolsResult R;
	R.bSuccess = true;
	R.Message = TEXT("Photo spots cleared (automatic spots are still used).");
	return R;
}

FPublishToolsResult UPublishTools::CaptureStoreScreenshots(int32 Count, const FString& Resolution)
{
	FPublishToolsResult R;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		R.Message = TEXT("No level is open in the editor.");
		return R;
	}
	const TArray<FAGBPhotoSpot> Spots = CollectSpots(World, FMath::Clamp(Count, 1, 30));
	if (Spots.IsEmpty())
	{
		R.Message = TEXT("No photo spots: fly the viewport to a nice view and AddPhotoSpot (no landscape for automatic spots).");
		return R;
	}
	FAGBPlaytestRequest Request;
	Request.Scenario = TEXT("Photos");
	Request.MaxMinutes = 2.f + Spots.Num() * 0.25f;
	Request.PhotoFolder = FPaths::ConvertRelativePathToFull(StoreKitDir() / TEXT("Screenshots"));
	Request.PhotoResolution = Resolution.Contains(TEXT("x")) ? Resolution : FString(TEXT("1920x1080"));
	for (const FAGBPhotoSpot& Spot : Spots)
	{
		Request.PhotoCameras.Add(Spot.Camera);
		Request.PhotoHours.Add(Spot.Hour);
		Request.PhotoLabels.Add(Spot.Label);
		R.Lines.Add(DescribeSpot(Spot));
	}
	IFileManager::Get().MakeDirectory(*Request.PhotoFolder, true);
	const FPlaytestToolsResult Started = UPlaytestTools::StartWithRequest(Request);
	R.bSuccess = Started.bSuccess;
	R.Message = Started.bSuccess
		? FString::Printf(TEXT("Taking %d screenshot(s) in the game (about %d s) into %s. Poll PlaytestTools.GetPlaytestStatus; Play ends by itself."), Spots.Num(), Spots.Num() * 7, *Request.PhotoFolder)
		: Started.Message;
	return R;
}

FPublishToolsResult UPublishTools::MakeStoreImages(const FString& ScreenshotFile)
{
	FPublishToolsResult R;
	const FString Folder = StoreKitDir() / TEXT("Screenshots");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.png")), true, false);
	Files.Sort();
	FString Source = ScreenshotFile.IsEmpty() ? (Files.Num() > 0 ? Folder / Files[0] : FString()) : (FPaths::FileExists(ScreenshotFile) ? ScreenshotFile : Folder / ScreenshotFile);
	FImage Loaded;
	if (Source.IsEmpty() || !FImageUtils::LoadImage(*Source, Loaded))
	{
		R.Message = TEXT("No screenshot to use: CaptureStoreScreenshots first.");
		return R;
	}
	FImage Image;
	Loaded.CopyTo(Image, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

	struct FCapsule { const TCHAR* Name; int32 Width; int32 Height; };
	const FCapsule Capsules[] = {
		{ TEXT("header_capsule"), 920, 430 }, { TEXT("small_capsule"), 462, 174 }, { TEXT("main_capsule"), 1232, 706 },
		{ TEXT("vertical_capsule"), 748, 896 }, { TEXT("library_capsule"), 600, 900 }, { TEXT("library_hero"), 3840, 1240 },
		{ TEXT("itch_cover"), 630, 500 },
	};
	const FString Out = StoreKitDir() / TEXT("Capsules");
	IFileManager::Get().MakeDirectory(*Out, true);
	for (const FCapsule& Capsule : Capsules)
	{
		// Centre crop to the capsule's shape, then scale.
		const double Aspect = static_cast<double>(Capsule.Width) / Capsule.Height;
		int32 CropW = Image.SizeX;
		int32 CropH = FMath::RoundToInt(CropW / Aspect);
		if (CropH > Image.SizeY)
		{
			CropH = Image.SizeY;
			CropW = FMath::RoundToInt(CropH * Aspect);
		}
		const int32 X0 = (Image.SizeX - CropW) / 2;
		const int32 Y0 = (Image.SizeY - CropH) / 2;
		FImage Crop(CropW, CropH, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		for (int32 Row = 0; Row < CropH; ++Row)
		{
			FMemory::Memcpy(&Crop.RawData[static_cast<int64>(Row) * CropW * 4], &Image.RawData[(static_cast<int64>(Y0 + Row) * Image.SizeX + X0) * 4], static_cast<SIZE_T>(CropW) * 4);
		}
		FImage Scaled;
		Crop.ResizeTo(Scaled, Capsule.Width, Capsule.Height, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const FString Path = Out / FString::Printf(TEXT("%s_%dx%d.png"), Capsule.Name, Capsule.Width, Capsule.Height);
		if (FImageUtils::SaveImageByExtension(*Path, Scaled))
		{
			R.Lines.Add(Path);
		}
	}
	R.bSuccess = R.Lines.Num() > 0;
	R.Message = FString::Printf(TEXT("Made %d capsule background(s) from %s. The user adds the game's title/logo in an image editor (Steam requires it readable on capsules)."),
		R.Lines.Num(), *FPaths::GetCleanFilename(Source));
	return R;
}

FPublishToolsResult UPublishTools::CreateTrailerTour(double SecondsPerShot, double Hour)
{
	FPublishToolsResult R;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		R.Message = TEXT("No level is open in the editor.");
		return R;
	}
	const TArray<FAGBPhotoSpot> Spots = CollectSpots(World, 12);
	if (Spots.Num() < 2)
	{
		R.Message = TEXT("Need at least 2 photo spots (AddPhotoSpot) or a landscape for automatic ones.");
		return R;
	}
	const FScopedTransaction Transaction(LOCTEXT("TrailerTour", "AI Game Builder: Trailer Tour"));
	AAGBCameraTour* Tour = nullptr;
	for (TActorIterator<AAGBCameraTour> It(World); It; ++It)
	{
		Tour = *It;
		break;
	}
	if (!Tour)
	{
		Tour = World->SpawnActor<AAGBCameraTour>(AAGBCameraTour::StaticClass(), Spots[0].Camera);
		Tour->SetActorLabel(TEXT("TrailerTour"));
	}
	Tour->Modify();
	Tour->Points.Reset();
	for (const FAGBPhotoSpot& Spot : Spots)
	{
		Tour->Points.Add(Spot.Camera);
		R.Lines.Add(DescribeSpot(Spot));
	}
	Tour->SecondsPerShot = static_cast<float>(FMath::Max(1.0, SecondsPerShot));
	Tour->Hour = static_cast<float>(Hour);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Trailer tour through %d spots (%.0f s). Save the level. To record: Play (Standalone looks best), open the console with ` and type AGBTour, record with Win+Alt+R; AGBTour again stops it."),
		Spots.Num(), (Spots.Num() - 1) * Tour->SecondsPerShot);
	return R;
}

FPublishToolsResult UPublishTools::GetReleaseChecklist()
{
	FPublishToolsResult R;
	int32 Open = 0;
	auto Item = [&R, &Open](bool bDone, const FString& Text, bool bProblem = false)
	{
		R.Lines.Add((bDone ? TEXT("[x] ") : (bProblem ? TEXT("[!] ") : TEXT("[ ] "))) + Text);
		Open += bDone ? 0 : 1;
	};
	auto User = [&R](const FString& Text) { R.Lines.Add(TEXT("[user] ") + Text); };

	// Game data and setup.
	const FPlaytestToolsResult Validation = UPlaytestTools::ValidateGame();
	Item(Validation.bSuccess, TEXT("Game data has no dead ends (ValidateGame): ") + Validation.Message, !Validation.bSuccess);
	const UGeneralProjectSettings* General = GetDefault<UGeneralProjectSettings>();
	Item(!General->ProjectName.IsEmpty() && !General->CompanyName.IsEmpty() && !General->ProjectVersion.IsEmpty(),
		FString::Printf(TEXT("Name, company and version set ('%s' %s by %s) (PreparePackaging)"), *General->ProjectName, *General->ProjectVersion, *General->CompanyName));
	const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
	const UAGBUISettings* UI = GetDefault<UAGBUISettings>();
	Item(!UI->MainMenuMap.IsNull() && UGameMapsSettings::GetGameDefaultMap().Contains(UI->MainMenuMap.GetAssetName()), TEXT("The game starts in the main menu (UITools.SetupUI)"));
	Item(ContainsPath(Packaging->DirectoriesToAlwaysCook, TEXT("/Game/AIGameBuilder")) && !UI->GameMap.IsNull() && ContainsMap(Packaging->MapsToCook, UI->GameMap.ToSoftObjectPath().GetLongPackageName()),
		TEXT("Levels and game data are in the cook list (PreparePackaging)"));
	Item(Packaging->IncludeCrashReporter, TEXT("Crash reporter included"));

	// Builds.
	FString ShippingExe;
	FString DevelopmentExe;
	FolderSize(DefaultOutput(TEXT("Shipping")), &ShippingExe);
	FolderSize(DefaultOutput(TEXT("Development")), &DevelopmentExe);
	Item(!DevelopmentExe.IsEmpty() || !ShippingExe.IsEmpty(), TEXT("A packaged build exists and starts (PackageGame, then play it from the Packaged folder)"));
	Item(!ShippingExe.IsEmpty(), TEXT("A Shipping build for release (PackageGame Shipping)"));
	if (Job.bRan && !Job.bRunning && Job.ReturnCode != 0)
	{
		Item(false, TEXT("The last packaging run failed: GetPackagingStatus"), true);
	}

	// Playtests and performance.
	TArray<FString> Reports;
	IFileManager::Get().FindFiles(Reports, *(FPaths::ProjectSavedDir() / TEXT("AGBPlaytests") / TEXT("*.txt")), true, false);
	Reports.Sort([](const FString& A, const FString& B) { return A > B; });
	bool bSmoke = false;
	bool bSurvive = false;
	for (const FString& Report : Reports)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("AGBPlaytests") / Report));
		const bool bPassed = Text.Contains(TEXT(": PASSED"));
		bSmoke |= Report.Contains(TEXT("Smoke")) && bPassed;
		bSurvive |= Report.Contains(TEXT("Survive")) && bPassed;
	}
	Item(bSmoke, TEXT("Smoke playtest passed (PlaytestTools.StartPlaytest Smoke)"));
	Item(bSurvive, TEXT("Survive playtest passed (the bot gets a tool, a fire and through a night)"));
	const FPlaytestToolsResult Balance = UPlaytestTools::GetBalanceReport(10, true);
	const int32 Flags = Balance.Lines.FilterByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("FLAG")); }).Num();
	Item(Balance.bSuccess && Flags == 0, Balance.bSuccess ? FString::Printf(TEXT("Balance and performance on target (%d FLAG line(s) in GetBalanceReport)"), Flags)
		: FString(TEXT("Balance report needs play sessions (play or playtest first)")), Flags > 0);

	// Store kit.
	TArray<FString> Shots;
	IFileManager::Get().FindFiles(Shots, *(StoreKitDir() / TEXT("Screenshots") / TEXT("*.png")), true, false);
	Item(Shots.Num() >= 5, FString::Printf(TEXT("At least 5 store screenshots (%d in Saved/StoreKit/Screenshots; CaptureStoreScreenshots)"), Shots.Num()));
	TArray<FString> CapsuleFiles;
	IFileManager::Get().FindFiles(CapsuleFiles, *(StoreKitDir() / TEXT("Capsules") / TEXT("*.png")), true, false);
	Item(CapsuleFiles.Num() > 0, TEXT("Capsule backgrounds made (MakeStoreImages); the user adds the logo/title"));
	Item(FPaths::FileExists(FPaths::ProjectDir() / TEXT("AIGameBuilder/StorePage.md")), TEXT("Store page text drafted (GameDesignTools.save_store_page)"));
	bool bTour = false;
	if (UWorld* World = GetEditorWorld())
	{
		for (TActorIterator<AAGBCameraTour> It(World); It; ++It)
		{
			bTour = true;
		}
	}
	Item(bTour, TEXT("Trailer camera tour in the level (CreateTrailerTour), trailer recorded"));

	// Content quality.
	const UAGBAudioFXConfig* Audio = UI->AudioFX.LoadSynchronous();
	int32 Placeholders = 0;
	for (const FAGBCueSlot& Slot : AGBAudioFX::GetSlots())
	{
		const FAGBCue* Cue = Audio ? Audio->Cues.Find(Slot.Name) : nullptr;
		Placeholders += (!Cue || (Cue->Sounds.IsEmpty() && !Cue->bMuted)) && !Slot.Name.ToString().StartsWith(TEXT("Effect.")) ? 1 : 0;
	}
	Item(Placeholders < 10, FString::Printf(TEXT("Real sounds instead of placeholders (%d slot(s) still use generated sounds; AudioFXTools.ListSoundSlots)"), Placeholders));
	Item(FPaths::FileExists(FPaths::ProjectDir() / TEXT("Build/Windows/Application.ico")), TEXT("Game icon (Project Settings > Platforms > Windows > Game Icon, a 256x256 .ico)"));

	// The user's part.
	User(TEXT("Play the packaged game on another PC without Unreal installed (and a Steam Deck if targeted); check saving, menus, settings, co-op."));
	User(TEXT("Store account: Steamworks (one-time app fee per game, tax and bank forms, identity check) or itch.io (free)."));
	User(TEXT("Age rating questionnaire (Steam content survey; IARC for consoles) and content descriptors (hunting, blood, online chat)."));
	User(TEXT("Legal: a EULA/privacy note if the game goes online (multiplayer chat), licences of every Fab/marketplace pack (Fab standard licence allows games)."));
	User(TEXT("Capsule art with the logo, trailer upload, price and release date in the store dashboard."));
	User(TEXT("Upload: Steam via SteamPipe (Steamworks SDK ContentBuilder) with the Shipping folder; itch.io via a zip of the Windows folder or butler."));
	User(TEXT("Steam store review (a few days) and build review before release; announce 'Coming Soon' at least 2 weeks before launch."));

	R.bSuccess = Open == 0;
	R.Message = FString::Printf(TEXT("%d automatic item(s) open; [user] steps are done by the user in the store."), Open);
	return R;
}

#undef LOCTEXT_NAMESPACE
