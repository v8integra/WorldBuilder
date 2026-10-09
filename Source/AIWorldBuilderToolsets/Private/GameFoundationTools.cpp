#include "GameFoundationTools.h"

#include "GameToolUtils.h"

#include "AGBCharacter.h"
#include "AGBDefaultInput.h"
#include "AGBGameFramework.h"
#include "AGBInteractableLight.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "FileHelpers.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Landscape.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderFoundation"

using namespace AIWorldBuilder;

namespace
{
	const FString CharacterBlueprintName = TEXT("BP_AGB_Character");
	const FString ControllerBlueprintName = TEXT("BP_AGB_PlayerController");
	const FString GameModeBlueprintName = TEXT("BP_AGB_GameMode");
	const FString MappingContextName = TEXT("IMC_AGB_Default");
	constexpr double WalkableAngleDeg = 44.765;

	bool IsAuto(const FString& Value)
	{
		return Value.IsEmpty() || Value.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
	}

	FString ObjectPathFor(const FString& Folder, const FString& Name)
	{
		return Folder / Name + TEXT(".") + Name;
	}

	/** An asset that is loaded or saved on disk, or nullptr (without load warnings). */
	template <typename T>
	T* FindAsset(const FString& Folder, const FString& Name)
	{
		const FString ObjectPath = ObjectPathFor(Folder, Name);
		if (T* Loaded = FindObject<T>(nullptr, *ObjectPath))
		{
			return Loaded;
		}
		return FPackageName::DoesPackageExist(Folder / Name) ? LoadObject<T>(nullptr, *ObjectPath) : nullptr;
	}

	UBlueprint* LoadOrCreateBlueprint(const FString& Folder, const FString& Name, UClass* ParentClass, TArray<UPackage*>& Dirty, TArray<FString>& Assets, FString& OutError)
	{
		if (UBlueprint* Existing = FindAsset<UBlueprint>(Folder, Name))
		{
			if (!Existing->GeneratedClass || !Existing->GeneratedClass->IsChildOf(ParentClass))
			{
				OutError = FString::Printf(TEXT("%s exists but is not a %s Blueprint. Rename or move it, or pass another Folder."), *Existing->GetPathName(), *ParentClass->GetName());
				return nullptr;
			}
			Assets.Add(Existing->GetPathName() + TEXT(" (updated)"));
			return Existing;
		}

		UPackage* Package = CreatePackage(*(Folder / Name));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("Could not create Blueprint %s."), *(Folder / Name));
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Dirty.AddUnique(Package);
		Assets.Add(Blueprint->GetPathName() + TEXT(" (created)"));
		return Blueprint;
	}

	/** Reuses existing input assets (so the user's rebinds survive); creates the defaults otherwise. */
	FAGBInputSet LoadOrCreateInput(const FString& InputFolder, TArray<UPackage*>& Dirty, TArray<FString>& Assets)
	{
		auto Factory = [&](UClass* Class, const FString& AssetName) -> UObject*
		{
			// Reuse an existing asset of the right class; if the name is taken by something else, use a free name.
			FString Name = AssetName;
			UObject* Asset = FindAsset<UObject>(InputFolder, Name);
			for (int32 Suffix = 2; Asset && !Asset->IsA(Class); ++Suffix)
			{
				Name = FString::Printf(TEXT("%s_%d"), *AssetName, Suffix);
				Asset = FindAsset<UObject>(InputFolder, Name);
			}
			if (!Asset)
			{
				UPackage* Package = CreatePackage(*(InputFolder / Name));
				Asset = NewObject<UObject>(Package, Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
				FAssetRegistryModule::AssetCreated(Asset);
				Assets.Add(Asset->GetPathName() + TEXT(" (created)"));
			}
			Asset->Modify();
			Dirty.AddUnique(Asset->GetPackage());
			return Asset;
		};

		UInputMappingContext* Existing = FindAsset<UInputMappingContext>(InputFolder, MappingContextName);
		if (!Existing)
		{
			return AGBInput::CreateDefaultInput(Factory);
		}

		// Keep the existing context and actions (the user's rebinds survive); add actions from newer versions.
		FAGBInputSet Set;
		Set.MappingContext = Existing;
		Set.Move = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Move"));
		Set.Look = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Look"));
		Set.Jump = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Jump"));
		Set.Sprint = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Sprint"));
		Set.Crouch = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Crouch"));
		Set.Interact = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Interact"));
		Set.ToggleCamera = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_ToggleCamera"));
		Set.Inventory = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Inventory"));
		Set.Drop = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_Drop"));
		Set.HotbarSelect = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_HotbarSelect"));
		Set.HotbarCycle = FindAsset<UInputAction>(InputFolder, TEXT("IA_AGB_HotbarCycle"));
		if (AGBInput::IsComplete(Set))
		{
			Assets.Add(Existing->GetPathName() + TEXT(" (kept, with its actions)"));
			return Set;
		}
		Existing->Modify();
		Dirty.AddUnique(Existing->GetPackage());
		Assets.Add(Existing->GetPathName() + TEXT(" (kept; added bindings for new actions)"));
		return AGBInput::CreateDefaultInput(Factory, &Set);
	}

	/** First asset of a class under /Game whose name is in the preference list (in list order). */
	FAssetData FindPreferredAsset(UClass* Class, std::initializer_list<const TCHAR*> PreferredNames)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		for (const TCHAR* Name : PreferredNames)
		{
			for (const FAssetData& Asset : Found)
			{
				if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
				{
					return Asset;
				}
			}
		}
		return FAssetData();
	}

	UAnimBlueprint* FindAnimBlueprintFor(const USkeleton* Skeleton)
	{
		if (!Skeleton)
		{
			return nullptr;
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		for (const TCHAR* Name : { TEXT("ABP_Unarmed"), TEXT("ABP_Manny"), TEXT("ABP_Quinn"), TEXT("ABP_ThirdPerson") })
		{
			for (const FAssetData& Asset : Found)
			{
				if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
				{
					UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset.GetAsset());
					if (AnimBlueprint && AnimBlueprint->TargetSkeleton == Skeleton && AnimBlueprint->GeneratedClass)
					{
						return AnimBlueprint;
					}
				}
			}
		}
		return nullptr;
	}

	bool UsesEnhancedInput()
	{
		const UClass* InputComponentClass = UInputSettings::GetDefaultInputComponentClass();
		return InputComponentClass && InputComponentClass->IsChildOf(UEnhancedInputComponent::StaticClass());
	}

	/** Fills the setup fields of a result from the project settings and the open level. */
	void FillStatus(UWorld* World, FGameFoundationResult& R)
	{
		R.ProjectGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
		R.GameDefaultMap = UGameMapsSettings::GetGameDefaultMap();
		R.EditorStartupMap = GetDefault<UGameMapsSettings>()->EditorStartupMap.GetLongPackageName();
		R.bEnhancedInput = UsesEnhancedInput();
		R.PlayerStartCount = 0;
		R.PlayerStartsM.Reset();

		UClass* GameModeClass = nullptr;
		if (World)
		{
			R.CurrentLevel = World->GetOutermost()->GetName();
			if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				GameModeClass = WorldSettings->DefaultGameMode;
				R.LevelGameMode = GameModeClass ? GameModeClass->GetPathName() : FString();
			}
			for (TActorIterator<APlayerStart> It(World); It; ++It)
			{
				++R.PlayerStartCount;
				R.PlayerStartsM.Add(It->GetActorLocation() / CmPerMeter);
			}
		}
		if (!GameModeClass && !R.ProjectGameMode.IsEmpty())
		{
			GameModeClass = LoadClass<AGameModeBase>(nullptr, *R.ProjectGameMode);
		}

		const AGameModeBase* GameMode = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
		const AAGBCharacter* Character = (GameMode && GameMode->DefaultPawnClass) ? Cast<AAGBCharacter>(GameMode->DefaultPawnClass->GetDefaultObject()) : nullptr;
		if (Character)
		{
			R.CharacterBlueprint = Character->GetClass()->GetPathName();
			R.CharacterMesh = Character->BodyMesh ? Character->BodyMesh->GetPathName() : TEXT("placeholder body");
			R.DefaultCameraMode = Character->DefaultCameraMode == EAGBCameraMode::FirstPerson ? TEXT("FirstPerson") : TEXT("ThirdPerson");
		}
	}

	FString DescribeStatus(const FGameFoundationResult& R)
	{
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("Level %s uses game mode %s."), *R.CurrentLevel, R.LevelGameMode.IsEmpty() ? *FString::Printf(TEXT("%s (project default)"), *R.ProjectGameMode) : *R.LevelGameMode));
		Lines.Add(R.CharacterBlueprint.IsEmpty()
			? FString(TEXT("The game mode does not use an AI Game Builder character: run SetupGameFoundation."))
			: FString::Printf(TEXT("Player: %s, model %s, starts in %s."), *R.CharacterBlueprint, *R.CharacterMesh, *R.DefaultCameraMode));
		Lines.Add(FString::Printf(TEXT("Player starts in this level: %d%s."), R.PlayerStartCount, R.PlayerStartCount == 0 ? TEXT(" (place one with PlacePlayerStart)") : TEXT("")));
		Lines.Add(FString::Printf(TEXT("Game starts in %s; editor opens %s."), *R.GameDefaultMap, *R.EditorStartupMap));
		if (!R.bEnhancedInput)
		{
			Lines.Add(TEXT("WARNING: the project does not use Enhanced Input; SetupGameFoundation switches it on."));
		}
		return FString::Join(Lines, TEXT(" "));
	}

	FGameFoundationResult Fail(const FString& Message)
	{
		FGameFoundationResult R;
		R.Message = Message;
		return R;
	}
}

FString UGameFoundationTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FGameFoundationResult UGameFoundationTools::SetupGameFoundation(const FString& Perspective, const FString& CharacterMeshPath, const FString& AnimBlueprintPath,
	const FString& Folder, bool bSetProjectDefault, bool bAllowCameraToggle)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}

	EAGBCameraMode CameraMode;
	if (Perspective.Equals(TEXT("ThirdPerson"), ESearchCase::IgnoreCase) || Perspective.Equals(TEXT("Third"), ESearchCase::IgnoreCase))
	{
		CameraMode = EAGBCameraMode::ThirdPerson;
	}
	else if (Perspective.Equals(TEXT("FirstPerson"), ESearchCase::IgnoreCase) || Perspective.Equals(TEXT("First"), ESearchCase::IgnoreCase))
	{
		CameraMode = EAGBCameraMode::FirstPerson;
	}
	else
	{
		return Fail(FString::Printf(TEXT("Unknown perspective '%s': use ThirdPerson or FirstPerson."), *Perspective));
	}

	FString Root = Folder;
	Root.RemoveFromEnd(TEXT("/"));
	if (!Root.StartsWith(TEXT("/Game/")) && Root != TEXT("/Game"))
	{
		return Fail(FString::Printf(TEXT("Folder must be under /Game (got '%s')."), *Folder));
	}

	// Resolve the character model before creating anything, so a bad path changes nothing.
	USkeletalMesh* Mesh = nullptr;
	bool bChangeMesh = false;
	TArray<FString> Notes;
	if (CharacterMeshPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		bChangeMesh = true;
	}
	else if (!IsAuto(CharacterMeshPath))
	{
		Mesh = LoadObject<USkeletalMesh>(nullptr, *CharacterMeshPath);
		if (!Mesh)
		{
			return Fail(FString::Printf(TEXT("Skeletal mesh '%s' not found. Use a skeletal mesh asset path, \"auto\" or \"none\"."), *CharacterMeshPath));
		}
		bChangeMesh = true;
	}

	UClass* AnimClass = nullptr;
	bool bChangeAnim = false;
	if (!IsAuto(AnimBlueprintPath) && !AnimBlueprintPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AnimBlueprintPath);
		if (!AnimBlueprint || !AnimBlueprint->GeneratedClass)
		{
			return Fail(FString::Printf(TEXT("Animation Blueprint '%s' not found."), *AnimBlueprintPath));
		}
		AnimClass = AnimBlueprint->GeneratedClass.Get();
		bChangeAnim = true;
	}

	FGameFoundationResult R;
	TArray<UPackage*> Dirty;
	FString Error;

	const FAGBInputSet InputSet = LoadOrCreateInput(Root / TEXT("Input"), Dirty, R.Assets);

	UBlueprint* CharacterBlueprint = LoadOrCreateBlueprint(Root, CharacterBlueprintName, AAGBCharacter::StaticClass(), Dirty, R.Assets, Error);
	UBlueprint* ControllerBlueprint = CharacterBlueprint ? LoadOrCreateBlueprint(Root, ControllerBlueprintName, AAGBPlayerController::StaticClass(), Dirty, R.Assets, Error) : nullptr;
	UBlueprint* GameModeBlueprint = ControllerBlueprint ? LoadOrCreateBlueprint(Root, GameModeBlueprintName, AAGBGameMode::StaticClass(), Dirty, R.Assets, Error) : nullptr;
	if (!GameModeBlueprint)
	{
		R.Message = Error;
		return R;
	}

	// Character defaults.
	AAGBCharacter* Character = CastChecked<AAGBCharacter>(CharacterBlueprint->GeneratedClass->GetDefaultObject());
	Character->Modify();
	Character->Input = InputSet;
	Character->DefaultCameraMode = CameraMode;
	Character->bAllowCameraToggle = bAllowCameraToggle;

	if (IsAuto(CharacterMeshPath) && !Character->BodyMesh)
	{
		const FAssetData Mannequin = FindPreferredAsset(USkeletalMesh::StaticClass(), { TEXT("SKM_Manny_Simple"), TEXT("SKM_Manny"), TEXT("SKM_Quinn_Simple"), TEXT("SKM_Quinn") });
		Mesh = Mannequin.IsValid() ? Cast<USkeletalMesh>(Mannequin.GetAsset()) : nullptr;
		bChangeMesh = Mesh != nullptr;
		if (!Mesh)
		{
			Notes.Add(TEXT("No mannequin in the project: the player uses the placeholder body until a character model is provided (CharacterMeshPath)."));
		}
	}
	if (bChangeMesh)
	{
		Character->BodyMesh = Mesh;
		if (!Mesh)
		{
			Character->BodyAnimClass = nullptr;
		}
	}
	if (AnimBlueprintPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Character->BodyAnimClass = nullptr;
	}
	else if (bChangeAnim)
	{
		Character->BodyAnimClass = AnimClass;
	}
	else if (IsAuto(AnimBlueprintPath) && Character->BodyMesh && (bChangeMesh || !Character->BodyAnimClass))
	{
		if (UAnimBlueprint* AnimBlueprint = FindAnimBlueprintFor(Character->BodyMesh->GetSkeleton()))
		{
			Character->BodyAnimClass = AnimBlueprint->GeneratedClass.Get();
		}
		else
		{
			Character->BodyAnimClass = nullptr;
			Notes.Add(FString::Printf(TEXT("No Animation Blueprint found for %s's skeleton: the model will not animate (pass AnimBlueprintPath)."), *Character->BodyMesh->GetName()));
		}
	}
	Dirty.AddUnique(CharacterBlueprint->GetPackage());

	// Game mode defaults.
	AAGBGameMode* GameMode = CastChecked<AAGBGameMode>(GameModeBlueprint->GeneratedClass->GetDefaultObject());
	GameMode->Modify();
	GameMode->DefaultPawnClass = CharacterBlueprint->GeneratedClass.Get();
	GameMode->PlayerControllerClass = ControllerBlueprint->GeneratedClass.Get();
	GameMode->HUDClass = AAGBHUD::StaticClass();
	Dirty.AddUnique(GameModeBlueprint->GetPackage());

	for (UPackage* Package : Dirty)
	{
		Package->MarkPackageDirty();
	}
	UEditorLoadingAndSavingUtils::SavePackages(Dirty, /*bOnlyDirty=*/false);

	// Project settings: Enhanced Input and (optionally) the default game mode.
	if (!UsesEnhancedInput())
	{
		UInputSettings::SetDefaultPlayerInputClass(UEnhancedPlayerInput::StaticClass());
		UInputSettings::SetDefaultInputComponentClass(UEnhancedInputComponent::StaticClass());
		GetMutableDefault<UInputSettings>()->TryUpdateDefaultConfigFile();
		Notes.Add(TEXT("Switched the project to Enhanced Input."));
	}
	const FString PreviousProjectGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
	if (bSetProjectDefault)
	{
		UGameMapsSettings::SetGlobalDefaultGameMode(GameModeBlueprint->GeneratedClass->GetPathName());
		GetMutableDefault<UGameMapsSettings>()->TryUpdateDefaultConfigFile();
	}

	// The open level: game mode override (so it plays with this game mode whatever it used before).
	AWorldSettings* WorldSettings = World->GetWorldSettings();
	const FString PreviousLevelGameMode = (WorldSettings && WorldSettings->DefaultGameMode) ? WorldSettings->DefaultGameMode->GetPathName() : FString();
	if (WorldSettings)
	{
		const FScopedTransaction Transaction(LOCTEXT("SetLevelGameMode", "AI Game Builder: Set Level Game Mode"));
		WorldSettings->Modify();
		WorldSettings->DefaultGameMode = GameModeBlueprint->GeneratedClass.Get();
	}

	FillStatus(World, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Game foundation ready in %s (%s camera%s). "), *Root,
		CameraMode == EAGBCameraMode::FirstPerson ? TEXT("first-person") : TEXT("third-person"), bAllowCameraToggle ? TEXT(", V switches") : TEXT(""));
	if (bSetProjectDefault && PreviousProjectGameMode != R.ProjectGameMode)
	{
		R.Message += FString::Printf(TEXT("Project default game mode was %s. "), *PreviousProjectGameMode);
	}
	if (!PreviousLevelGameMode.IsEmpty() && PreviousLevelGameMode != R.LevelGameMode)
	{
		R.Message += FString::Printf(TEXT("This level's game mode override was %s. "), *PreviousLevelGameMode);
	}
	for (const FString& Note : Notes)
	{
		R.Message += Note + TEXT(" ");
	}
	R.Message += DescribeStatus(R) + TEXT(" Save the level to keep its game mode override.");
	return R;
}

FGameFoundationResult UGameFoundationTools::PlacePlayerStart(double XM, double YM, double YawDeg, bool bReplaceExisting)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}

	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m: nothing with collision there (unloaded World Partition region, or outside the landscape)."), XM, YM));
	}
	const double SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0, 1.0)));

	const FScopedTransaction Transaction(LOCTEXT("PlacePlayerStart", "AI Game Builder: Place Player Start"));
	int32 Removed = 0;
	if (bReplaceExisting)
	{
		TArray<APlayerStart*> Existing;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Existing.Add(*It);
		}
		for (APlayerStart* Start : Existing)
		{
			World->EditorDestroyActor(Start, /*bShouldModifyLevel=*/true);
			++Removed;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerStart* Start = World->SpawnActor<APlayerStart>(Hit.ImpactPoint, FRotator(0.0, YawDeg, 0.0), Params);
	if (!Start)
	{
		return Fail(TEXT("Could not spawn a player start."));
	}
	// Stand the capsule on the ground, with a little clearance.
	const double HalfHeight = Start->GetCapsuleComponent() ? Start->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 92.0;
	Start->SetActorLocation(Hit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 5.0));
	Start->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), TEXT("PlayerStart_AGB")));

	FGameFoundationResult R;
	FillStatus(World, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Player start at (%.1f, %.1f, %.1f) m facing %.0f deg on %s (slope %.0f deg%s).%s"),
		XM, YM, Hit.ImpactPoint.Z / CmPerMeter, YawDeg, Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("ground"), SlopeDeg,
		SlopeDeg > WalkableAngleDeg ? TEXT(", TOO STEEP: players will slide; pick flatter ground") : TEXT(""),
		Removed > 0 ? *FString::Printf(TEXT(" Removed %d old player start(s)."), Removed) : TEXT(""));
	return R;
}

FGameFoundationResult UGameFoundationTools::SpawnInteractableLight(double XM, double YM, const FString& Label)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m."), XM, YM));
	}

	const FScopedTransaction Transaction(LOCTEXT("SpawnInteractableLight", "AI Game Builder: Spawn Interactable Light"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAGBInteractableLight* Light = World->SpawnActor<AAGBInteractableLight>(Hit.ImpactPoint, FRotator::ZeroRotator, Params);
	if (!Light)
	{
		return Fail(TEXT("Could not spawn the light."));
	}
	Light->SetActorLabel(GameToolUtils::UniqueLabel(World, Label, TEXT("AGB_InteractableLight")));

	FGameFoundationResult R;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Placed '%s' at (%.1f, %.1f, %.1f) m. In Play mode, look at it from within 2.5 m and press E to switch it."),
		*Light->GetActorLabel(), XM, YM, Hit.ImpactPoint.Z / CmPerMeter);
	return R;
}

FGameFoundationResult UGameFoundationTools::SetStartupMap(const FString& MapPath, bool bGameDefault, bool bEditorStartup)
{
	UWorld* World = GetEditorWorld();
	FString PackageName = MapPath;
	if (MapPath.Equals(TEXT("current"), ESearchCase::IgnoreCase))
	{
		if (!World)
		{
			return Fail(TEXT("No level is open in the editor."));
		}
		PackageName = World->GetOutermost()->GetName();
	}
	PackageName = FPackageName::ObjectPathToPackageName(PackageName);
	if (PackageName.StartsWith(TEXT("/Temp/")) || !FPackageName::DoesPackageExist(PackageName))
	{
		return Fail(FString::Printf(TEXT("Map '%s' is not saved on disk: save the level first (or give a saved map path)."), *PackageName));
	}

	const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
	UGameMapsSettings* Settings = GetMutableDefault<UGameMapsSettings>();
	if (bGameDefault)
	{
		UGameMapsSettings::SetGameDefaultMap(ObjectPath);
	}
	if (bEditorStartup)
	{
		Settings->EditorStartupMap = FSoftObjectPath(ObjectPath);
	}
	Settings->TryUpdateDefaultConfigFile();

	FGameFoundationResult R;
	FillStatus(World, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Game starts in %s; editor opens %s."), *R.GameDefaultMap, *R.EditorStartupMap);
	return R;
}

FGameFoundationResult UGameFoundationTools::GetGameFoundationStatus()
{
	FGameFoundationResult R;
	FillStatus(GetEditorWorld(), R);
	R.bSuccess = true;
	R.Message = DescribeStatus(R);
	return R;
}

#undef LOCTEXT_NAMESPACE
