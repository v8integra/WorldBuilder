#include "AudioFXTools.h"

#include "GameToolUtils.h"

#include "AGBAudioFX.h"
#include "AGBCreatureTypes.h"
#include "AGBUITheme.h"
#include "AIWorldBuilderCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Landscape.h"
#include "LandscapeLayerInfoObject.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ScopedTransaction.h"
#include "Sound/SoundBase.h"
#include "WaterBodyActor.h"
#include "WaterSplineComponent.h"

#define LOCTEXT_NAMESPACE "AudioFXTools"

using namespace AIWorldBuilder;

namespace
{
	const TCHAR* AudioFolder = TEXT("/Game/AIGameBuilder/Audio");
	const TCHAR* ConfigName = TEXT("DA_AGB_AudioFX");
	const FName AutoWaterTag(TEXT("AGB_AutoWater"));
	constexpr double CmPerMeter = 100.0;

	template <typename T>
	T* LoadAt(const FString& Folder, const FString& Name)
	{
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	template <typename T>
	T* LoadOrCreate(const FString& Folder, const FString& Name, TArray<UPackage*>& Dirty, TArray<FString>& Lines)
	{
		if (T* Existing = LoadAt<T>(Folder, Name))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*(Folder / Name));
		T* Asset = NewObject<T>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Dirty.AddUnique(Package);
		Lines.Add(Asset->GetPathName() + TEXT(" (created)"));
		return Asset;
	}

	UObject* LoadByPath(const FString& Path, UClass* Class)
	{
		FString ObjectPath = Path.TrimStartAndEnd();
		if (!ObjectPath.Contains(TEXT(".")))
		{
			ObjectPath += TEXT(".") + FPackageName::GetShortName(ObjectPath);
		}
		UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		return Object && Object->IsA(Class) ? Object : nullptr;
	}

	/** The project's audio config (created on first use). */
	UAGBAudioFXConfig* EnsureConfig(TArray<UPackage*>& Dirty, TArray<FString>& Lines)
	{
		UAGBUISettings* Settings = GetMutableDefault<UAGBUISettings>();
		UAGBAudioFXConfig* Config = Settings->AudioFX.LoadSynchronous();
		if (!Config)
		{
			Config = LoadOrCreate<UAGBAudioFXConfig>(AudioFolder, ConfigName, Dirty, Lines);
			Settings->AudioFX = Config;
			Settings->TryUpdateDefaultConfigFile();
		}
		return Config;
	}

	FString DescribeCue(const UAGBAudioFXConfig* Config, const FAGBCueSlot& Slot)
	{
		const FAGBCue* Cue = Config ? Config->Cues.Find(Slot.Name) : nullptr;
		FString Sounds;
		if (Cue && Cue->bMuted)
		{
			Sounds = TEXT("muted");
		}
		else if (Cue && Cue->Sounds.Num() > 0)
		{
			TArray<FString> Names;
			for (const TSoftObjectPtr<USoundBase>& Sound : Cue->Sounds)
			{
				Names.Add(Sound.GetAssetName());
			}
			Sounds = FString::Printf(TEXT("%d sound(s): %s"), Names.Num(), *FString::Join(Names, TEXT(", ")));
		}
		else
		{
			Sounds = Slot.Name.ToString().StartsWith(TEXT("Effect.")) ? FString(TEXT("-")) : FString(TEXT("placeholder"));
		}
		FString Line = FString::Printf(TEXT("%s: %s"), *Slot.Name.ToString(), *Sounds);
		if (Cue && !Cue->Effect.IsNull())
		{
			Line += TEXT(" + effect ") + Cue->Effect.GetAssetName();
		}
		else if (*Slot.PlaceholderEffect)
		{
			Line += TEXT(" + placeholder effect");
		}
		if (Cue && Cue->Volume != 1.f)
		{
			Line += FString::Printf(TEXT(" (volume %.2f)"), Cue->Volume);
		}
		return Line + TEXT(" - ") + Slot.Description;
	}

	/** Lower-case words of an asset's name and its two parent folders ("FootstepGrass_01" -> footstep, grass). */
	TArray<FString> Words(const FAssetData& Asset)
	{
		FString Text = Asset.AssetName.ToString();
		FString Path = Asset.PackagePath.ToString();
		TArray<FString> Folders;
		Path.ParseIntoArray(Folders, TEXT("/"));
		for (int32 Index = FMath::Max(0, Folders.Num() - 2); Index < Folders.Num(); ++Index)
		{
			Text += TEXT(" ") + Folders[Index];
		}
		TArray<FString> Result;
		FString Current;
		auto Flush = [&Result, &Current]()
		{
			if (Current.Len() > 1)
			{
				Result.Add(Current.ToLower());
			}
			Current.Reset();
		};
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Char = Text[Index];
			if (!FChar::IsAlpha(Char))
			{
				Flush();
				continue;
			}
			// camelCase boundary.
			if (FChar::IsUpper(Char) && Current.Len() > 0 && FChar::IsLower(Current[Current.Len() - 1]))
			{
				Flush();
			}
			Current.AppendChar(Char);
		}
		Flush();
		return Result;
	}

	/** Score of an asset for a slot's keywords ("a,b|c,d" = all groups must match); 0 = no match. */
	int32 Score(const TArray<FString>& AssetWords, const FAGBCueSlot& Slot)
	{
		TArray<FString> Groups;
		FString(Slot.Keywords).ParseIntoArray(Groups, TEXT("|"));
		if (Groups.IsEmpty())
		{
			return 0;
		}
		int32 Total = 0;
		for (const FString& Group : Groups)
		{
			TArray<FString> Options;
			Group.ParseIntoArray(Options, TEXT(","));
			int32 Best = 0;
			for (const FString& Option : Options)
			{
				for (const FString& Word : AssetWords)
				{
					if (Word.StartsWith(Option))
					{
						Best = FMath::Max(Best, Option.Len());
					}
				}
			}
			if (Best == 0)
			{
				return 0;
			}
			Total += Best;
		}
		return Total + Groups.Num() * 10;
	}

	bool IsEffectSlot(const FAGBCueSlot& Slot)
	{
		const FString Name = Slot.Name.ToString();
		return Name.StartsWith(TEXT("Effect.")) || Name.StartsWith(TEXT("Impact.")) || Name == TEXT("Event.Splash");
	}

	TArray<FAssetData> FindAssets(UClass* Class, const FString& Folder)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.RemoveAll([](const FAssetData& Asset) { return Asset.PackagePath.ToString().StartsWith(TEXT("/Game/AIGameBuilder")); });
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		return Assets;
	}

	FAudioFXToolsResult Fail(const FString& Message)
	{
		FAudioFXToolsResult R;
		R.Message = Message;
		return R;
	}

	FString Meters(const FVector& Location)
	{
		return FString::Printf(TEXT("(%.0f, %.0f, %.0f) m"), Location.X / CmPerMeter, Location.Y / CmPerMeter, Location.Z / CmPerMeter);
	}
}

FString UAudioFXTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FAudioFXToolsResult UAudioFXTools::SetupAudioFX(bool bAssignLandscapeSurfaces, bool bAddWaterSounds)
{
	FAudioFXToolsResult R;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	TArray<UPackage*> Dirty;
	UAGBAudioFXConfig* Config = EnsureConfig(Dirty, R.Lines);

	// Surface physical materials for footsteps.
	TMap<EAGBSurface, UPhysicalMaterial*> Materials;
	for (const EAGBSurface Surface : { EAGBSurface::Grass, EAGBSurface::Dirt, EAGBSurface::Rock, EAGBSurface::Sand, EAGBSurface::Snow, EAGBSurface::Mud, EAGBSurface::Gravel })
	{
		UPhysicalMaterial* Material = LoadOrCreate<UPhysicalMaterial>(FString(AudioFolder) / TEXT("Surfaces"), TEXT("PM_AGB_") + AGBAudioFX::SurfaceName(Surface), Dirty, R.Lines);
		Materials.Add(Surface, Material);
		if (!Config->SurfaceMaterials.ContainsByPredicate([Material](const FAGBSurfaceMaterial& Entry) { return Entry.PhysicalMaterial.Get() == Material; }))
		{
			Config->Modify();
			Config->SurfaceMaterials.Add({ Material, Surface });
			Dirty.AddUnique(Config->GetPackage());
		}
	}

	if (bAssignLandscapeSurfaces)
	{
		int32 Assigned = 0;
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			bool bChanged = false;
			for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : It->GetTargetLayers())
			{
				ULandscapeLayerInfoObject* LayerInfo = Pair.Value.LayerInfoObj;
				const EAGBSurface Surface = AGBAudioFX::SurfaceFromName(Pair.Key.ToString());
				UPhysicalMaterial** Material = Materials.Find(Surface);
				if (!LayerInfo || !Material)
				{
					if (LayerInfo)
					{
						R.Lines.Add(FString::Printf(TEXT("Layer '%s': no surface matches its name (default footsteps)"), *Pair.Key.ToString()));
					}
					continue;
				}
				const UPhysicalMaterial* Current = LayerInfo->GetPhysicalMaterial();
				if (Current && Current != *Material)
				{
					R.Lines.Add(FString::Printf(TEXT("Layer '%s': kept its physical material %s"), *Pair.Key.ToString(), *Current->GetName()));
					continue;
				}
				if (Current != *Material)
				{
					LayerInfo->SetPhysicalMaterial(*Material, /*bInModify=*/true);
					Dirty.AddUnique(LayerInfo->GetPackage());
					bChanged = true;
					++Assigned;
				}
				R.Lines.Add(FString::Printf(TEXT("Layer '%s' -> %s footsteps"), *Pair.Key.ToString(), *AGBAudioFX::SurfaceName(Surface)));
			}
			if (bChanged)
			{
				// Rebuild the collision's per-layer materials.
				It->ChangedPhysMaterial();
				for (TActorIterator<ALandscapeProxy> Proxy(World); Proxy; ++Proxy)
				{
					if (*Proxy != *It && Proxy->GetLandscapeActor() == *It)
					{
						Proxy->ChangedPhysMaterial();
					}
				}
			}
		}
		if (Assigned > 0)
		{
			R.Lines.Add(FString::Printf(TEXT("%d landscape layer(s) got surface materials; save the level (Save All) to keep the landscape change."), Assigned));
		}
	}

	if (bAddWaterSounds)
	{
		const FScopedTransaction Transaction(LOCTEXT("WaterSounds", "AI Game Builder: Water Sounds"));
		for (TActorIterator<AAGBSoundEmitter> It(World); It; ++It)
		{
			if (It->Tags.Contains(AutoWaterTag))
			{
				It->Destroy();
			}
		}
		int32 Placed = 0;
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			const UWaterSplineComponent* Spline = It->GetWaterSpline();
			const EWaterBodyType Type = It->GetWaterBodyType();
			if (!Spline || Type == EWaterBodyType::Transition)
			{
				continue;
			}
			const bool bRiver = Type == EWaterBodyType::River;
			const float Length = Spline->GetSplineLength();
			const float Spacing = bRiver ? 3000.f : 4000.f;
			const int32 Count = FMath::Clamp(FMath::CeilToInt(Length / Spacing), 1, bRiver ? 20 : 10);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Distance = Count == 1 ? 0.f : Length * Index / (bRiver ? FMath::Max(1, Count - 1) : Count);
				const FVector Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World) + FVector(0.0, 0.0, 50.0);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AAGBSoundEmitter* Emitter = World->SpawnActor<AAGBSoundEmitter>(AAGBSoundEmitter::StaticClass(), FTransform(Location), Params);
				if (!Emitter)
				{
					continue;
				}
				Emitter->Modify();
				Emitter->Preset = bRiver ? TEXT("River") : TEXT("Water");
				Emitter->RadiusMeters = bRiver ? 30.f : 25.f;
				Emitter->Tags.Add(AutoWaterTag);
				Emitter->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), FString::Printf(TEXT("Sound_%s"), *Emitter->Preset.ToString())));
				Emitter->RerunConstructionScripts();
				++Placed;
			}
			R.Lines.Add(FString::Printf(TEXT("%s: %d %s sound(s) along it"), *It->GetActorLabel(), Count, bRiver ? TEXT("river") : TEXT("shore")));
		}
		if (Placed == 0)
		{
			R.Lines.Add(TEXT("No lakes or rivers (Water plugin bodies) found for water sounds."));
		}
	}

	if (Dirty.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(Dirty, /*bOnlyDirty=*/false);
	}
	int32 Filled = 0;
	for (const FAGBCueSlot& Slot : AGBAudioFX::GetSlots())
	{
		const FAGBCue* Cue = Config->Cues.Find(Slot.Name);
		Filled += Cue && (Cue->Sounds.Num() > 0 || !Cue->Effect.IsNull()) ? 1 : 0;
	}
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Audio set up: %d of %d cue slots have assets, the rest play generated placeholders. Add a sound pack, then AutoAssignSounds."),
		Filled, AGBAudioFX::GetSlots().Num());
	return R;
}

FAudioFXToolsResult UAudioFXTools::SetSoundCue(const TArray<FString>& SoundPaths, const FString& Slot, const FString& EffectPath, double Volume, bool bAdd)
{
	const FAGBCueSlot* CueSlot = AGBAudioFX::FindSlot(FName(*Slot.TrimStartAndEnd()));
	if (!CueSlot)
	{
		return Fail(FString::Printf(TEXT("Unknown slot '%s': see ListSoundSlots."), *Slot));
	}
	FAudioFXToolsResult R;
	TArray<UPackage*> Dirty;
	UAGBAudioFXConfig* Config = EnsureConfig(Dirty, R.Lines);
	Config->Modify();
	Dirty.AddUnique(Config->GetPackage());
	FAGBCue& Cue = Config->Cues.FindOrAdd(CueSlot->Name);
	TArray<FString> Problems;

	if (SoundPaths.Num() == 1 && SoundPaths[0].Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Cue.Sounds.Reset();
		Cue.bMuted = false;
	}
	else if (SoundPaths.Num() == 1 && SoundPaths[0].Equals(TEXT("mute"), ESearchCase::IgnoreCase))
	{
		Cue.Sounds.Reset();
		Cue.bMuted = true;
	}
	else if (SoundPaths.Num() > 0)
	{
		if (!bAdd)
		{
			Cue.Sounds.Reset();
		}
		Cue.bMuted = false;
		for (const FString& Path : SoundPaths)
		{
			if (USoundBase* Sound = Cast<USoundBase>(LoadByPath(Path, USoundBase::StaticClass())))
			{
				Cue.Sounds.AddUnique(Sound);
			}
			else
			{
				Problems.Add(FString::Printf(TEXT("no sound at '%s'"), *Path));
			}
		}
	}
	if (EffectPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Cue.Effect.Reset();
	}
	else if (!EffectPath.IsEmpty())
	{
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadByPath(EffectPath, UNiagaraSystem::StaticClass())))
		{
			Cue.Effect = System;
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("no Niagara system at '%s'"), *EffectPath));
		}
	}
	if (Volume > 0.0)
	{
		Cue.Volume = static_cast<float>(FMath::Clamp(Volume, 0.0, 4.0));
	}
	UEditorLoadingAndSavingUtils::SavePackages(Dirty, /*bOnlyDirty=*/false);
	R.Lines.Add(DescribeCue(Config, *CueSlot));
	R.bSuccess = Problems.IsEmpty();
	R.Message = Problems.IsEmpty() ? FString(TEXT("Cue updated.")) : TEXT("Cue updated, except: ") + FString::Join(Problems, TEXT("; "));
	return R;
}

FAudioFXToolsResult UAudioFXTools::AutoAssignSounds(const FString& Folder, bool bOverwrite)
{
	FAudioFXToolsResult R;
	TArray<UPackage*> Dirty;
	UAGBAudioFXConfig* Config = EnsureConfig(Dirty, R.Lines);
	const FString Root = Folder.IsEmpty() ? FString(TEXT("/Game")) : Folder;

	// Best slot per asset (specific slots like Footstep.Grass beat Footstep.Default).
	TMap<FName, TArray<FAssetData>> SoundsBySlot;
	TMap<FName, TArray<FAssetData>> EffectsBySlot;
	TArray<FString> Unmatched;
	auto Match = [&Unmatched](const TArray<FAssetData>& Assets, bool bEffects, TMap<FName, TArray<FAssetData>>& Out)
	{
		for (const FAssetData& Asset : Assets)
		{
			const TArray<FString> AssetWords = Words(Asset);
			const FAGBCueSlot* Best = nullptr;
			int32 BestScore = 0;
			for (const FAGBCueSlot& Slot : AGBAudioFX::GetSlots())
			{
				const bool bEffectOnly = Slot.Name.ToString().StartsWith(TEXT("Effect."));
				if (bEffects ? !IsEffectSlot(Slot) : bEffectOnly)
				{
					continue;
				}
				const int32 SlotScore = Score(AssetWords, Slot);
				if (SlotScore > BestScore)
				{
					BestScore = SlotScore;
					Best = &Slot;
				}
			}
			if (Best)
			{
				Out.FindOrAdd(Best->Name).Add(Asset);
			}
			else
			{
				Unmatched.Add(Asset.AssetName.ToString());
			}
		}
	};
	const TArray<FAssetData> Sounds = FindAssets(USoundBase::StaticClass(), Root);
	const TArray<FAssetData> Effects = FindAssets(UNiagaraSystem::StaticClass(), Root);
	Match(Sounds, false, SoundsBySlot);
	Match(Effects, true, EffectsBySlot);

	int32 Changed = 0;
	for (const FAGBCueSlot& Slot : AGBAudioFX::GetSlots())
	{
		const TArray<FAssetData>* SlotSounds = SoundsBySlot.Find(Slot.Name);
		const TArray<FAssetData>* SlotEffects = EffectsBySlot.Find(Slot.Name);
		if (!SlotSounds && !SlotEffects)
		{
			continue;
		}
		FAGBCue& Cue = Config->Cues.FindOrAdd(Slot.Name);
		TArray<FString> Parts;
		if (SlotSounds)
		{
			if (Cue.Sounds.Num() > 0 && !bOverwrite)
			{
				Parts.Add(FString::Printf(TEXT("kept its %d sound(s) (%d candidates)"), Cue.Sounds.Num(), SlotSounds->Num()));
			}
			else
			{
				Config->Modify();
				Cue.Sounds.Reset();
				Cue.bMuted = false;
				TArray<FString> Names;
				for (int32 Index = 0; Index < SlotSounds->Num() && Index < 12; ++Index)
				{
					Cue.Sounds.Add(TSoftObjectPtr<USoundBase>((*SlotSounds)[Index].ToSoftObjectPath()));
					Names.Add((*SlotSounds)[Index].AssetName.ToString());
				}
				Parts.Add(FString::Printf(TEXT("%d sound(s): %s"), Names.Num(), *FString::Join(Names, TEXT(", "))));
				++Changed;
			}
		}
		if (SlotEffects)
		{
			if (!Cue.Effect.IsNull() && !bOverwrite)
			{
				Parts.Add(TEXT("kept its effect"));
			}
			else
			{
				Config->Modify();
				Cue.Effect = TSoftObjectPtr<UNiagaraSystem>((*SlotEffects)[0].ToSoftObjectPath());
				Parts.Add(TEXT("effect ") + (*SlotEffects)[0].AssetName.ToString());
				++Changed;
			}
		}
		R.Lines.Add(FString::Printf(TEXT("%s <- %s"), *Slot.Name.ToString(), *FString::Join(Parts, TEXT("; "))));
	}
	if (Changed > 0)
	{
		Dirty.AddUnique(Config->GetPackage());
	}
	if (Dirty.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(Dirty, /*bOnlyDirty=*/false);
	}
	if (Unmatched.Num() > 0)
	{
		const int32 Shown = FMath::Min(Unmatched.Num(), 25);
		R.Lines.Add(FString::Printf(TEXT("%d asset(s) matched no slot (assign with SetSoundCue if useful): %s%s"), Unmatched.Num(),
			*FString::Join(TArrayView<const FString>(Unmatched.GetData(), Shown), TEXT(", ")), Unmatched.Num() > Shown ? TEXT(", ...") : TEXT("")));
	}
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Searched %d sound(s) and %d effect(s) in %s; %d slot(s) updated."), Sounds.Num(), Effects.Num(), *Root, Changed);
	return R;
}

FAudioFXToolsResult UAudioFXTools::ListSoundSlots()
{
	FAudioFXToolsResult R;
	const UAGBAudioFXConfig* Config = GetDefault<UAGBUISettings>()->AudioFX.LoadSynchronous();
	for (const FAGBCueSlot& Slot : AGBAudioFX::GetSlots())
	{
		R.Lines.Add(DescribeCue(Config, Slot));
	}
	R.bSuccess = true;
	R.Message = Config ? FString::Printf(TEXT("%d cue slots."), R.Lines.Num()) : FString(TEXT("No audio config yet (SetupAudioFX): every slot plays its placeholder."));
	return R;
}

FAudioFXToolsResult UAudioFXTools::CreateAmbienceZone(double CenterXM, double CenterYM, double RadiusM, const FString& Preset, const FString& DaySoundPath,
	const FString& NightSoundPath, double EdgeM, const FString& Label)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	static const TArray<FString> Presets = { TEXT("Forest"), TEXT("Desert"), TEXT("Beach"), TEXT("Snow"), TEXT("Cave"), TEXT("Swamp"), TEXT("Plains") };
	const FString* Known = Presets.FindByPredicate([&Preset](const FString& Name) { return Name.Equals(Preset, ESearchCase::IgnoreCase); });
	if (!Known)
	{
		return Fail(FString::Printf(TEXT("Unknown preset '%s': %s."), *Preset, *FString::Join(Presets, TEXT(", "))));
	}
	USoundBase* Day = DaySoundPath.IsEmpty() ? nullptr : Cast<USoundBase>(LoadByPath(DaySoundPath, USoundBase::StaticClass()));
	USoundBase* Night = NightSoundPath.IsEmpty() ? nullptr : Cast<USoundBase>(LoadByPath(NightSoundPath, USoundBase::StaticClass()));
	if ((!DaySoundPath.IsEmpty() && !Day) || (!NightSoundPath.IsEmpty() && !Night))
	{
		return Fail(TEXT("A sound path did not load (use /Game/... asset paths)."));
	}
	FHitResult Ground;
	const double Z = GameToolUtils::TraceGround(World, CenterXM, CenterYM, Ground) ? Ground.ImpactPoint.Z : 0.0;

	const FScopedTransaction Transaction(LOCTEXT("CreateAmbienceZone", "AI Game Builder: Create Ambience Zone"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAGBAmbienceZone* Zone = World->SpawnActor<AAGBAmbienceZone>(AAGBAmbienceZone::StaticClass(), FTransform(FVector(CenterXM * CmPerMeter, CenterYM * CmPerMeter, Z)), Params);
	if (!Zone)
	{
		return Fail(TEXT("Could not spawn the zone."));
	}
	Zone->Modify();
	Zone->Preset = FName(**Known);
	Zone->DaySound = Day;
	Zone->NightSound = Night;
	Zone->RadiusMeters = static_cast<float>(FMath::Max(1.0, RadiusM));
	Zone->EdgeMeters = static_cast<float>(FMath::Clamp(EdgeM, 1.0, RadiusM));
	Zone->SetActorLabel(GameToolUtils::UniqueLabel(World, Label, TEXT("Ambience_") + *Known));
	Zone->RerunConstructionScripts();

	FAudioFXToolsResult R;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s: %s ambience, %.0f m radius at %s%s."), *Zone->GetActorLabel(), **Known, RadiusM, *Meters(Zone->GetActorLocation()),
		Day ? TEXT("") : TEXT(" (placeholder sound until a day sound is set)"));
	return R;
}

FAudioFXToolsResult UAudioFXTools::PlaceSoundEmitter(double XM, double YM, const FString& Preset, double HeightM, double RadiusM, const FString& SoundPath,
	const FString& EffectPath, const FString& Label)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	static const TArray<FString> Presets = { TEXT("Water"), TEXT("River"), TEXT("Waterfall"), TEXT("Fire"), TEXT("Insects"), TEXT("Wind"), TEXT("Birds") };
	const FString* Known = Presets.FindByPredicate([&Preset](const FString& Name) { return Name.Equals(Preset, ESearchCase::IgnoreCase); });
	if (!Known)
	{
		return Fail(FString::Printf(TEXT("Unknown preset '%s': %s."), *Preset, *FString::Join(Presets, TEXT(", "))));
	}
	USoundBase* Sound = SoundPath.IsEmpty() ? nullptr : Cast<USoundBase>(LoadByPath(SoundPath, USoundBase::StaticClass()));
	UNiagaraSystem* Effect = EffectPath.IsEmpty() ? nullptr : Cast<UNiagaraSystem>(LoadByPath(EffectPath, UNiagaraSystem::StaticClass()));
	if ((!SoundPath.IsEmpty() && !Sound) || (!EffectPath.IsEmpty() && !Effect))
	{
		return Fail(TEXT("A sound or effect path did not load (use /Game/... asset paths)."));
	}
	FHitResult Ground;
	const double Z = GameToolUtils::TraceGround(World, XM, YM, Ground) ? Ground.ImpactPoint.Z : 0.0;

	const FScopedTransaction Transaction(LOCTEXT("PlaceSoundEmitter", "AI Game Builder: Place Sound Emitter"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAGBSoundEmitter* Emitter = World->SpawnActor<AAGBSoundEmitter>(AAGBSoundEmitter::StaticClass(),
		FTransform(FVector(XM * CmPerMeter, YM * CmPerMeter, Z + HeightM * CmPerMeter)), Params);
	if (!Emitter)
	{
		return Fail(TEXT("Could not spawn the emitter."));
	}
	Emitter->Modify();
	Emitter->Preset = FName(**Known);
	Emitter->Sound = Sound;
	Emitter->Effect = Effect;
	Emitter->RadiusMeters = static_cast<float>(FMath::Max(1.0, RadiusM));
	Emitter->SetActorLabel(GameToolUtils::UniqueLabel(World, Label, TEXT("Sound_") + *Known));
	Emitter->RerunConstructionScripts();

	FAudioFXToolsResult R;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s: %s sound, heard up to %.0f m, at %s."), *Emitter->GetActorLabel(), **Known, RadiusM, *Meters(Emitter->GetActorLocation()));
	return R;
}

FAudioFXToolsResult UAudioFXTools::SetCreatureSounds(const FString& CreatureId, const FString& HurtSoundPath, const FString& AttackSoundPath,
	const FString& DeathSoundPath, const FString& IdleSoundPath)
{
	UAGBCreatureDefinition* Creature = Cast<UAGBCreatureDefinition>(GameToolUtils::FindAssetByTag(UAGBCreatureDefinition::StaticClass(), TEXT("CreatureId"), CreatureId));
	if (!Creature)
	{
		return Fail(FString::Printf(TEXT("No creature '%s'. Creatures: %s"), *CreatureId, *GameToolUtils::ListIds(UAGBCreatureDefinition::StaticClass(), TEXT("CreatureId"))));
	}
	FAudioFXToolsResult R;
	TArray<FString> Problems;
	Creature->Modify();
	auto Set = [&](const TCHAR* Kind, const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return;
		}
		if (Path.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			Creature->Sounds.Remove(Kind);
		}
		else if (USoundBase* Sound = Cast<USoundBase>(LoadByPath(Path, USoundBase::StaticClass())))
		{
			Creature->Sounds.Add(Kind, Sound);
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("%s: no sound at '%s'"), Kind, *Path));
		}
	};
	Set(TEXT("Hurt"), HurtSoundPath);
	Set(TEXT("Attack"), AttackSoundPath);
	Set(TEXT("Death"), DeathSoundPath);
	Set(TEXT("Idle"), IdleSoundPath);
	UEditorLoadingAndSavingUtils::SavePackages({ Creature->GetPackage() }, /*bOnlyDirty=*/false);
	for (const TCHAR* Kind : { TEXT("Hurt"), TEXT("Attack"), TEXT("Death"), TEXT("Idle") })
	{
		const TSoftObjectPtr<USoundBase>* Sound = Creature->Sounds.Find(Kind);
		R.Lines.Add(FString::Printf(TEXT("%s: %s"), Kind, Sound ? *Sound->GetAssetName() : *FString::Printf(TEXT("game's Creature.%s cue"), Kind)));
	}
	R.bSuccess = Problems.IsEmpty();
	R.Message = Problems.IsEmpty() ? FString::Printf(TEXT("%s sounds updated."), *CreatureId) : FString::Join(Problems, TEXT("; "));
	return R;
}

FAudioFXToolsResult UAudioFXTools::GetAudioFXStatus()
{
	FAudioFXToolsResult R;
	const UAGBUISettings* Settings = GetDefault<UAGBUISettings>();
	const UAGBAudioFXConfig* Config = Settings->AudioFX.LoadSynchronous();
	int32 Filled = 0;
	TArray<FString> Placeholder;
	for (const FAGBCueSlot& Slot : AGBAudioFX::GetSlots())
	{
		const FAGBCue* Cue = Config ? Config->Cues.Find(Slot.Name) : nullptr;
		if (Cue && (Cue->Sounds.Num() > 0 || Cue->bMuted))
		{
			++Filled;
		}
		else if (!Slot.Name.ToString().StartsWith(TEXT("Effect.")))
		{
			Placeholder.Add(Slot.Name.ToString());
		}
	}
	R.Lines.Add(FString::Printf(TEXT("Config: %s"), Config ? *Config->GetPathName() : TEXT("none (SetupAudioFX)")));
	R.Lines.Add(FString::Printf(TEXT("Slots with sounds: %d; placeholder: %s"), Filled, Placeholder.IsEmpty() ? TEXT("none") : *FString::Join(Placeholder, TEXT(", "))));
	if (Config)
	{
		R.Lines.Add(FString::Printf(TEXT("Placeholders: sounds %s, music %s, effects %s"), Config->bPlaceholderSounds ? TEXT("on") : TEXT("off"),
			Config->bPlaceholderMusic ? TEXT("on") : TEXT("off"), Config->bPlaceholderEffects ? TEXT("on") : TEXT("off")));
		TArray<FString> Surfaces;
		for (const FAGBSurfaceMaterial& Entry : Config->SurfaceMaterials)
		{
			Surfaces.Add(FString::Printf(TEXT("%s=%s"), *Entry.PhysicalMaterial.GetAssetName(), *AGBAudioFX::SurfaceName(Entry.Surface)));
		}
		R.Lines.Add(TEXT("Surface materials: ") + (Surfaces.IsEmpty() ? FString(TEXT("none")) : FString::Join(Surfaces, TEXT(", "))));
	}
	R.Lines.Add(FString::Printf(TEXT("Volume groups: %s"), Settings->VolumeMix.IsNull() ? TEXT("not set up (UITools.SetupUI)") : TEXT("set up")));
	if (UWorld* World = GetEditorWorld())
	{
		for (TActorIterator<AAGBAmbienceZone> It(World); It; ++It)
		{
			R.Lines.Add(FString::Printf(TEXT("Ambience zone %s: %s, %.0f m at %s"), *It->GetActorLabel(), *It->Preset.ToString(), It->RadiusMeters, *Meters(It->GetActorLocation())));
		}
		int32 Emitters = 0;
		TMap<FName, int32> ByPreset;
		for (TActorIterator<AAGBSoundEmitter> It(World); It; ++It)
		{
			++Emitters;
			ByPreset.FindOrAdd(It->Preset)++;
		}
		TArray<FString> Parts;
		for (const TPair<FName, int32>& Pair : ByPreset)
		{
			Parts.Add(FString::Printf(TEXT("%d %s"), Pair.Value, *Pair.Key.ToString()));
		}
		R.Lines.Add(FString::Printf(TEXT("Sound emitters: %d%s"), Emitters, Parts.IsEmpty() ? TEXT("") : *(TEXT(" (") + FString::Join(Parts, TEXT(", ")) + TEXT(")"))));
	}
	R.bSuccess = true;
	R.Message = Config ? TEXT("Audio is set up.") : TEXT("Not set up yet: placeholders play everywhere. Run SetupAudioFX.");
	return R;
}

#undef LOCTEXT_NAMESPACE
