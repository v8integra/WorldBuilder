#include "UITools.h"

#include "AGBMenus.h"
#include "AGBUITheme.h"
#include "AIWorldBuilderCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Blueprint.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Factories/WorldFactory.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

using namespace AIWorldBuilder;

namespace
{
	const TCHAR* UIFolder = TEXT("/Game/AIGameBuilder/UI");
	const TCHAR* AudioFolder = TEXT("/Game/AIGameBuilder/Audio");
	const TCHAR* MapsFolder = TEXT("/Game/AIGameBuilder/Maps");
	const TCHAR* ThemeName = TEXT("DA_AGB_UITheme");

	template <typename T>
	T* LoadAt(const FString& Folder, const FString& Name)
	{
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** Loads an asset from "/Game/Path/Asset" or "/Game/Path/Asset.Asset". */
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

	template <typename T>
	T* CreateAt(const FString& Folder, const FString& Name, TArray<UPackage*>& Dirty, TArray<FString>& Assets)
	{
		if (T* Existing = LoadAt<T>(Folder, Name))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*(Folder / Name));
		T* Asset = NewObject<T>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Dirty.AddUnique(Package);
		Assets.Add(Asset->GetPathName() + TEXT(" (created)"));
		return Asset;
	}

	UAGBUITheme* EnsureTheme(TArray<UPackage*>& Dirty, TArray<FString>& Assets)
	{
		UAGBUISettings* Settings = GetMutableDefault<UAGBUISettings>();
		UAGBUITheme* Theme = Settings->Theme.LoadSynchronous();
		if (!Theme)
		{
			const bool bExisted = LoadAt<UAGBUITheme>(UIFolder, ThemeName) != nullptr;
			Theme = CreateAt<UAGBUITheme>(UIFolder, ThemeName, Dirty, Assets);
			if (!bExisted)
			{
				Theme->GameTitle = FText::FromString(FApp::GetProjectName());
			}
			Settings->Theme = Theme;
			Settings->TryUpdateDefaultConfigFile();
		}
		return Theme;
	}

	bool ParseColor(const FString& Text, FLinearColor& Out)
	{
		FString Hex = Text.TrimStartAndEnd();
		Hex.RemoveFromStart(TEXT("#"));
		if (Hex.Len() != 6 && Hex.Len() != 8)
		{
			return false;
		}
		for (const TCHAR Char : Hex)
		{
			if (!FChar::IsHexDigit(Char))
			{
				return false;
			}
		}
		Out = FLinearColor(FColor::FromHex(Hex));
		return true;
	}

	FString Hex(const FLinearColor& Color)
	{
		return TEXT("#") + Color.ToFColorSRGB().ToHex();
	}

	template <typename T>
	FString PathOrNone(const T& Path)
	{
		return Path.IsNull() ? FString(TEXT("none")) : Path.ToSoftObjectPath().GetAssetPathString();
	}

	/** Theme presets: text, dim text, accent, panel, slot, button, hover, backdrop. */
	bool ApplyPreset(UAGBUITheme* Theme, const FString& Preset)
	{
		struct FPreset
		{
			const TCHAR* Name;
			FLinearColor Text, Accent, Panel, Slot, Button, Hover, Backdrop;
		};
		const FPreset Presets[] = {
			{ TEXT("Survival"), FLinearColor(0.93f, 0.91f, 0.87f), FLinearColor(0.95f, 0.65f, 0.2f), FLinearColor(0.05f, 0.05f, 0.06f, 0.88f),
				FLinearColor(0.f, 0.f, 0.f, 0.55f), FLinearColor(0.16f, 0.15f, 0.14f, 0.95f), FLinearColor(0.42f, 0.28f, 0.1f), FLinearColor(0.f, 0.f, 0.f, 0.6f) },
			{ TEXT("Frost"), FLinearColor(0.92f, 0.96f, 1.f), FLinearColor(0.45f, 0.8f, 1.f), FLinearColor(0.04f, 0.07f, 0.1f, 0.88f),
				FLinearColor(0.02f, 0.05f, 0.08f, 0.55f), FLinearColor(0.1f, 0.15f, 0.2f, 0.95f), FLinearColor(0.18f, 0.35f, 0.5f), FLinearColor(0.f, 0.02f, 0.05f, 0.6f) },
			{ TEXT("Forest"), FLinearColor(0.92f, 0.95f, 0.86f), FLinearColor(0.55f, 0.85f, 0.35f), FLinearColor(0.05f, 0.08f, 0.05f, 0.88f),
				FLinearColor(0.02f, 0.04f, 0.02f, 0.55f), FLinearColor(0.12f, 0.16f, 0.1f, 0.95f), FLinearColor(0.25f, 0.38f, 0.15f), FLinearColor(0.f, 0.03f, 0.f, 0.6f) },
			{ TEXT("Desert"), FLinearColor(1.f, 0.95f, 0.85f), FLinearColor(0.95f, 0.7f, 0.35f), FLinearColor(0.16f, 0.11f, 0.07f, 0.9f),
				FLinearColor(0.08f, 0.05f, 0.03f, 0.55f), FLinearColor(0.3f, 0.2f, 0.12f, 0.95f), FLinearColor(0.55f, 0.35f, 0.15f), FLinearColor(0.05f, 0.03f, 0.f, 0.55f) },
			{ TEXT("SciFi"), FLinearColor(0.85f, 0.95f, 1.f), FLinearColor(0.2f, 0.9f, 1.f), FLinearColor(0.02f, 0.04f, 0.08f, 0.88f),
				FLinearColor(0.f, 0.03f, 0.06f, 0.6f), FLinearColor(0.05f, 0.1f, 0.18f, 0.95f), FLinearColor(0.05f, 0.35f, 0.5f), FLinearColor(0.f, 0.01f, 0.03f, 0.65f) },
			{ TEXT("Horror"), FLinearColor(0.9f, 0.86f, 0.84f), FLinearColor(0.85f, 0.1f, 0.1f), FLinearColor(0.03f, 0.02f, 0.02f, 0.92f),
				FLinearColor(0.f, 0.f, 0.f, 0.65f), FLinearColor(0.1f, 0.05f, 0.05f, 0.95f), FLinearColor(0.4f, 0.05f, 0.05f), FLinearColor(0.f, 0.f, 0.f, 0.75f) },
		};
		for (const FPreset& Entry : Presets)
		{
			if (Preset.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				Theme->TextColor = Entry.Text;
				Theme->DimTextColor = FLinearColor(Entry.Text.R, Entry.Text.G, Entry.Text.B, 0.5f);
				Theme->AccentColor = Entry.Accent;
				Theme->PanelColor = Entry.Panel;
				Theme->SlotColor = Entry.Slot;
				Theme->ButtonColor = Entry.Button;
				Theme->ButtonHoverColor = Entry.Hover;
				Theme->BackdropColor = Entry.Backdrop;
				return true;
			}
		}
		return false;
	}

	void Describe(FUIToolsResult& R)
	{
		const UAGBUISettings* Settings = GetDefault<UAGBUISettings>();
		const UAGBUITheme* Theme = UAGBUISettings::GetTheme();
		R.Status.Add(FString::Printf(TEXT("Main menu level: %s"), *PathOrNone(Settings->MainMenuMap)));
		R.Status.Add(FString::Printf(TEXT("Game level (New Game / Continue): %s"), *PathOrNone(Settings->GameMap)));
		R.Status.Add(FString::Printf(TEXT("Game starts in: %s"), *UGameMapsSettings::GetGameDefaultMap()));
		R.Status.Add(FString::Printf(TEXT("Theme: %s - title '%s', text %s, accent %s, panel %s, button %s / hover %s, HUD scale %.2f"),
			*PathOrNone(Settings->Theme), *Theme->GameTitle.ToString(), *Hex(Theme->TextColor), *Hex(Theme->AccentColor), *Hex(Theme->PanelColor),
			*Hex(Theme->ButtonColor), *Hex(Theme->ButtonHoverColor), Theme->HUDScale));
		R.Status.Add(FString::Printf(TEXT("Theme assets: font %s, title font %s, panel %s, slot %s, button %s, menu background %s, logo %s"),
			*PathOrNone(Theme->Font), *PathOrNone(Theme->TitleFont), *PathOrNone(Theme->PanelTexture), *PathOrNone(Theme->SlotTexture),
			*PathOrNone(Theme->ButtonTexture), *PathOrNone(Theme->MenuBackground), *PathOrNone(Theme->Logo)));
		auto MenuName = [](const TSoftClassPtr<UUserWidget>& Class, const TCHAR* BuiltIn)
		{
			return Class.IsNull() ? FString::Printf(TEXT("built-in %s"), BuiltIn) : Class.ToSoftObjectPath().GetAssetPathString();
		};
		R.Status.Add(FString::Printf(TEXT("Menus: main %s, pause %s, settings %s"), *MenuName(Settings->MainMenuClass, TEXT("main menu")),
			*MenuName(Settings->PauseMenuClass, TEXT("pause menu")), *MenuName(Settings->SettingsMenuClass, TEXT("settings"))));
		R.Status.Add(Settings->VolumeMix.IsNull()
			? FString(TEXT("Volume groups: not set up (SetupUI)"))
			: FString::Printf(TEXT("Volume groups: master %s, music %s, effects %s, ambient %s, UI %s; mix %s"), *PathOrNone(Settings->MasterSoundClass),
				*PathOrNone(Settings->MusicSoundClass), *PathOrNone(Settings->EffectsSoundClass), *PathOrNone(Settings->AmbientSoundClass),
				*PathOrNone(Settings->UISoundClass), *PathOrNone(Settings->VolumeMix)));
	}

	FUIToolsResult Fail(const FString& Message)
	{
		FUIToolsResult R;
		R.Message = Message;
		Describe(R);
		return R;
	}
}

FString UUITools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FUIToolsResult UUITools::SetupUI(const FString& GameTitle, bool bSetStartupMap)
{
	FUIToolsResult R;
	UWorld* EditorWorld = GetEditorWorld();
	if (!EditorWorld)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	TArray<UPackage*> Dirty;
	UAGBUISettings* Settings = GetMutableDefault<UAGBUISettings>();

	// Theme.
	UAGBUITheme* Theme = EnsureTheme(Dirty, R.Assets);
	if (!GameTitle.TrimStartAndEnd().IsEmpty())
	{
		Theme->Modify();
		Theme->GameTitle = FText::FromString(GameTitle.TrimStartAndEnd());
		Dirty.AddUnique(Theme->GetPackage());
	}

	// Volume groups: effects, music, ambience and interface under a master class, all driven by one sound mix.
	USoundClass* Master = CreateAt<USoundClass>(AudioFolder, TEXT("SC_AGB_Master"), Dirty, R.Assets);
	auto Child = [&](const TCHAR* Name) -> USoundClass*
	{
		USoundClass* Class = CreateAt<USoundClass>(AudioFolder, Name, Dirty, R.Assets);
		if (Class->ParentClass != Master)
		{
			Class->Modify();
			Class->ParentClass = Master;
			Dirty.AddUnique(Class->GetPackage());
		}
		if (!Master->ChildClasses.Contains(Class))
		{
			Master->Modify();
			Master->ChildClasses.Add(Class);
			Dirty.AddUnique(Master->GetPackage());
		}
		return Class;
	};
	USoundClass* Music = Child(TEXT("SC_AGB_Music"));
	USoundClass* Effects = Child(TEXT("SC_AGB_Effects"));
	USoundClass* Ambient = Child(TEXT("SC_AGB_Ambient"));
	USoundClass* Interface = Child(TEXT("SC_AGB_UI"));
	USoundMix* Mix = CreateAt<USoundMix>(AudioFolder, TEXT("SCM_AGB_Volumes"), Dirty, R.Assets);
	Settings->MasterSoundClass = Master;
	Settings->MusicSoundClass = Music;
	Settings->EffectsSoundClass = Effects;
	Settings->AmbientSoundClass = Ambient;
	Settings->UISoundClass = Interface;
	Settings->VolumeMix = Mix;

	// Sounds without a class follow the Effects volume (only if the project still uses the engine default).
	FString AudioNote;
	UAudioSettings* Audio = GetMutableDefault<UAudioSettings>();
	const FString DefaultClass = Audio->DefaultSoundClassName.ToString();
	if (DefaultClass.IsEmpty() || DefaultClass.StartsWith(TEXT("/Engine/")))
	{
		Audio->DefaultSoundClassName = FSoftObjectPath(Effects);
		Audio->TryUpdateDefaultConfigFile();
		AudioNote = TEXT(" Sounds without a class now use the Effects volume (after an editor restart).");
	}

	// Main menu level.
	const FString MenuPackage = FString(MapsFolder) / TEXT("MainMenu");
	UWorld* MenuWorld = LoadAt<UWorld>(MapsFolder, TEXT("MainMenu"));
	if (!MenuWorld)
	{
		UWorldFactory* Factory = NewObject<UWorldFactory>();
		Factory->WorldType = EWorldType::Inactive;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		MenuWorld = Cast<UWorld>(AssetTools.CreateAsset(TEXT("MainMenu"), MapsFolder, UWorld::StaticClass(), Factory));
		if (!MenuWorld)
		{
			return Fail(TEXT("Could not create the MainMenu level."));
		}
		R.Assets.Add(MenuPackage + TEXT(" (created)"));
	}
	if (AWorldSettings* WorldSettings = MenuWorld->GetWorldSettings())
	{
		if (WorldSettings->DefaultGameMode != AAGBMenuGameMode::StaticClass())
		{
			WorldSettings->Modify();
			WorldSettings->DefaultGameMode = AAGBMenuGameMode::StaticClass();
			Dirty.AddUnique(MenuWorld->GetPackage());
		}
	}
	Settings->MainMenuMap = MenuWorld;

	// The level open now is the game (unless it is the menu itself).
	FString GameNote;
	const FString CurrentPackage = EditorWorld->GetOutermost()->GetName();
	if (CurrentPackage == MenuPackage)
	{
		GameNote = Settings->GameMap.IsNull() ? TEXT(" Open the game level and run SetupUI again to set New Game / Continue.") : TEXT("");
	}
	else if (CurrentPackage.StartsWith(TEXT("/Temp/")) || !FPackageName::DoesPackageExist(CurrentPackage))
	{
		GameNote = TEXT(" The open level is not saved: save it and run SetupUI again so New Game / Continue can open it.");
	}
	else
	{
		Settings->GameMap = EditorWorld;
	}
	Settings->TryUpdateDefaultConfigFile();

	if (bSetStartupMap)
	{
		UGameMapsSettings::SetGameDefaultMap(MenuWorld->GetPathName());
		GetMutableDefault<UGameMapsSettings>()->TryUpdateDefaultConfigFile();
	}

	if (Dirty.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(Dirty, /*bOnlyDirty=*/false);
	}
	Describe(R);
	R.bSuccess = true;
	R.Message = TEXT("UI set up. Play the MainMenu level to see the main menu; in the game press P (Escape in the packaged game) for the pause menu.") + GameNote + AudioNote;
	return R;
}

FUIToolsResult UUITools::SetUITheme(const FString& Preset, const FString& GameTitle, const FString& Subtitle, const FString& TextColor,
	const FString& AccentColor, const FString& PanelColor, const FString& ButtonColor, const FString& ButtonHoverColor, const FString& FontPath,
	const FString& TitleFontPath, const FString& PanelTexturePath, const FString& SlotTexturePath, const FString& ButtonTexturePath,
	const FString& MenuBackgroundPath, const FString& LogoPath, double HUDScale)
{
	FUIToolsResult R;
	TArray<UPackage*> Dirty;
	UAGBUITheme* Theme = EnsureTheme(Dirty, R.Assets);
	Theme->Modify();
	Dirty.AddUnique(Theme->GetPackage());
	TArray<FString> Problems;

	if (!Preset.IsEmpty() && !ApplyPreset(Theme, Preset))
	{
		Problems.Add(FString::Printf(TEXT("unknown preset '%s' (Survival, Frost, Forest, Desert, SciFi, Horror)"), *Preset));
	}
	if (!GameTitle.IsEmpty())
	{
		Theme->GameTitle = FText::FromString(GameTitle);
	}
	if (!Subtitle.IsEmpty())
	{
		Theme->Subtitle = Subtitle.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FText() : FText::FromString(Subtitle);
	}

	auto SetColor = [&Problems](const FString& Text, const TCHAR* Name, FLinearColor& Target)
	{
		if (!Text.IsEmpty() && !ParseColor(Text, Target))
		{
			Problems.Add(FString::Printf(TEXT("%s '%s' is not a hex color like #E8A33A"), Name, *Text));
		}
	};
	SetColor(TextColor, TEXT("textColor"), Theme->TextColor);
	if (!TextColor.IsEmpty())
	{
		Theme->DimTextColor = FLinearColor(Theme->TextColor.R, Theme->TextColor.G, Theme->TextColor.B, 0.5f);
	}
	SetColor(AccentColor, TEXT("accentColor"), Theme->AccentColor);
	SetColor(PanelColor, TEXT("panelColor"), Theme->PanelColor);
	SetColor(ButtonColor, TEXT("buttonColor"), Theme->ButtonColor);
	SetColor(ButtonHoverColor, TEXT("buttonHoverColor"), Theme->ButtonHoverColor);

	auto SetAsset = [&Problems](const FString& Path, const TCHAR* Name, UClass* Class, auto& Target)
	{
		if (Path.IsEmpty())
		{
			return;
		}
		if (Path.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			Target.Reset();
			return;
		}
		if (UObject* Asset = LoadByPath(Path, Class))
		{
			Target = FSoftObjectPath(Asset);
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("%s: no %s at '%s'"), Name, *Class->GetName(), *Path));
		}
	};
	SetAsset(FontPath, TEXT("fontPath"), UFont::StaticClass(), Theme->Font);
	SetAsset(TitleFontPath, TEXT("titleFontPath"), UFont::StaticClass(), Theme->TitleFont);
	SetAsset(PanelTexturePath, TEXT("panelTexturePath"), UTexture2D::StaticClass(), Theme->PanelTexture);
	SetAsset(SlotTexturePath, TEXT("slotTexturePath"), UTexture2D::StaticClass(), Theme->SlotTexture);
	SetAsset(ButtonTexturePath, TEXT("buttonTexturePath"), UTexture2D::StaticClass(), Theme->ButtonTexture);
	SetAsset(MenuBackgroundPath, TEXT("menuBackgroundPath"), UTexture2D::StaticClass(), Theme->MenuBackground);
	SetAsset(LogoPath, TEXT("logoPath"), UTexture2D::StaticClass(), Theme->Logo);
	if (HUDScale > 0.0)
	{
		Theme->HUDScale = static_cast<float>(FMath::Clamp(HUDScale, 0.5, 2.0));
	}

	UEditorLoadingAndSavingUtils::SavePackages(Dirty, /*bOnlyDirty=*/false);
	Describe(R);
	R.bSuccess = Problems.IsEmpty();
	R.Message = Problems.IsEmpty() ? FString(TEXT("Theme updated: menus and HUD use it next time they open."))
		: TEXT("Theme updated, except: ") + FString::Join(Problems, TEXT("; "));
	return R;
}

FUIToolsResult UUITools::SetMenuWidgets(const FString& MainMenuWidget, const FString& PauseMenuWidget, const FString& SettingsWidget)
{
	UAGBUISettings* Settings = GetMutableDefault<UAGBUISettings>();
	TArray<FString> Problems;
	auto Set = [&Problems](const FString& Path, const TCHAR* Name, TSoftClassPtr<UUserWidget>& Target)
	{
		if (Path.IsEmpty())
		{
			return;
		}
		if (Path.Equals(TEXT("default"), ESearchCase::IgnoreCase) || Path.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			Target.Reset();
			return;
		}
		const UBlueprint* Blueprint = Cast<UBlueprint>(LoadByPath(Path, UBlueprint::StaticClass()));
		UClass* Class = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
		if (Class && Class->IsChildOf(UUserWidget::StaticClass()))
		{
			Target = Class;
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("%s: '%s' is not a Widget Blueprint"), Name, *Path));
		}
	};
	Set(MainMenuWidget, TEXT("mainMenuWidget"), Settings->MainMenuClass);
	Set(PauseMenuWidget, TEXT("pauseMenuWidget"), Settings->PauseMenuClass);
	Set(SettingsWidget, TEXT("settingsWidget"), Settings->SettingsMenuClass);
	Settings->TryUpdateDefaultConfigFile();

	FUIToolsResult R;
	Describe(R);
	R.bSuccess = Problems.IsEmpty();
	R.Message = Problems.IsEmpty() ? FString(TEXT("Menus updated.")) : FString::Join(Problems, TEXT("; "));
	return R;
}

FUIToolsResult UUITools::GetUIStatus()
{
	FUIToolsResult R;
	Describe(R);
	R.bSuccess = true;
	R.Message = GetDefault<UAGBUISettings>()->MainMenuMap.IsNull() ? TEXT("No main menu yet: run SetupUI.") : TEXT("UI is set up.");
	return R;
}
