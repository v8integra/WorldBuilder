#include "AGBPlayerSettings.h"

#include "AGBCharacter.h"
#include "AGBDefaultInput.h"
#include "AGBUITheme.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace
{
	const TCHAR* SettingsSlot = TEXT("AGBPlayerSettings");
}

void UAGBPlayerSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (const UAGBPlayerSettingsSave* Saved = Cast<UAGBPlayerSettingsSave>(UGameplayStatics::LoadGameFromSlot(SettingsSlot, 0)))
	{
		Settings = Saved->Settings;
	}
}

UAGBPlayerSettingsSubsystem* UAGBPlayerSettingsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAGBPlayerSettingsSubsystem>() : nullptr;
}

void UAGBPlayerSettingsSubsystem::SetSettings(const FAGBPlayerSettings& NewSettings)
{
	Settings = NewSettings;
	Settings.MouseSensitivity = FMath::Clamp(Settings.MouseSensitivity, 0.1f, 5.f);
	Settings.FieldOfView = FMath::Clamp(Settings.FieldOfView, 60.f, 120.f);
	Settings.HUDScale = FMath::Clamp(Settings.HUDScale, 0.5f, 2.f);
	ApplySettings();
}

void UAGBPlayerSettingsSubsystem::SaveSettings()
{
	UAGBPlayerSettingsSave* Save = NewObject<UAGBPlayerSettingsSave>();
	Save->Settings = Settings;
	UGameplayStatics::SaveGameToSlot(Save, SettingsSlot, 0);
}

void UAGBPlayerSettingsSubsystem::ApplySettings()
{
	UGameInstance* GameInstance = GetGameInstance();
	ApplyAudio(GameInstance->GetWorld());
	for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		APlayerController* Player = LocalPlayer ? LocalPlayer->GetPlayerController(GameInstance->GetWorld()) : nullptr;
		if (AAGBCharacter* Character = Player ? Cast<AAGBCharacter>(Player->GetPawn()) : nullptr)
		{
			Character->ApplyPlayerSettings();
		}
	}
}

void UAGBPlayerSettingsSubsystem::ApplyAudio(UWorld* World) const
{
	const UAGBUISettings* UISettings = GetDefault<UAGBUISettings>();
	USoundMix* Mix = UISettings->VolumeMix.LoadSynchronous();
	if (!World || !Mix)
	{
		return;
	}
	// Each group gets master x its own volume (no parent/child multiplying to reason about).
	auto Set = [World, Mix](const TSoftObjectPtr<USoundClass>& Class, float Volume)
	{
		if (USoundClass* Loaded = Class.LoadSynchronous())
		{
			UGameplayStatics::SetSoundMixClassOverride(World, Mix, Loaded, FMath::Clamp(Volume, 0.f, 1.f), 1.f, 0.1f, /*bApplyToChildren=*/false);
		}
	};
	Set(UISettings->MasterSoundClass, Settings.MasterVolume);
	Set(UISettings->MusicSoundClass, Settings.MasterVolume * Settings.MusicVolume);
	Set(UISettings->EffectsSoundClass, Settings.MasterVolume * Settings.EffectsVolume);
	Set(UISettings->AmbientSoundClass, Settings.MasterVolume * Settings.AmbientVolume);
	Set(UISettings->UISoundClass, Settings.MasterVolume * Settings.UIVolume);
	UGameplayStatics::PushSoundMixModifier(World, Mix);
}

void UAGBPlayerSettingsSubsystem::RebindKey(FName ActionAssetName, const FKey& Key)
{
	if (Key.IsValid())
	{
		// One action per key: a key taken from another action sends that one back to its default.
		for (auto It = Settings.KeyBindings.CreateIterator(); It; ++It)
		{
			if (It->Value == Key && It->Key != ActionAssetName)
			{
				It.RemoveCurrent();
			}
		}
		Settings.KeyBindings.Add(ActionAssetName, Key);
	}
	else
	{
		Settings.KeyBindings.Remove(ActionAssetName);
	}
	ApplySettings();
}

void UAGBPlayerSettingsSubsystem::ResetKeyBindings()
{
	Settings.KeyBindings.Reset();
	ApplySettings();
}

const TArray<FAGBRebindableAction>& UAGBPlayerSettingsSubsystem::GetRebindableActions()
{
	static const TArray<FAGBRebindableAction> Actions = {
		{ TEXT("IA_AGB_Interact"), NSLOCTEXT("AIGameBuilder", "KeyInteract", "Interact") },
		{ TEXT("IA_AGB_UseItem"), NSLOCTEXT("AIGameBuilder", "KeyUse", "Use / attack") },
		{ TEXT("IA_AGB_Jump"), NSLOCTEXT("AIGameBuilder", "KeyJump", "Jump") },
		{ TEXT("IA_AGB_Sprint"), NSLOCTEXT("AIGameBuilder", "KeySprint", "Sprint") },
		{ TEXT("IA_AGB_Crouch"), NSLOCTEXT("AIGameBuilder", "KeyCrouch", "Crouch") },
		{ TEXT("IA_AGB_Inventory"), NSLOCTEXT("AIGameBuilder", "KeyInventory", "Inventory") },
		{ TEXT("IA_AGB_Drop"), NSLOCTEXT("AIGameBuilder", "KeyDrop", "Drop") },
		{ TEXT("IA_AGB_Rotate"), NSLOCTEXT("AIGameBuilder", "KeyRotate", "Rotate building") },
		{ TEXT("IA_AGB_ToggleCamera"), NSLOCTEXT("AIGameBuilder", "KeyCamera", "Switch camera") },
		{ TEXT("IA_AGB_Pause"), NSLOCTEXT("AIGameBuilder", "KeyPause", "Pause menu") },
	};
	return Actions;
}

UInputMappingContext* UAGBPlayerSettingsSubsystem::MakeBoundContext(UInputMappingContext* Source, FAGBInputSet& Set, UObject* Outer) const
{
	if (!Source)
	{
		return nullptr;
	}
	UInputMappingContext* Copy = DuplicateObject<UInputMappingContext>(Source, Outer);
	Copy->SetFlags(RF_Transient);
	if (Settings.KeyBindings.IsEmpty())
	{
		return Copy;
	}
	AGBInput::ForEachAction(Set, [this, Copy](TObjectPtr<UInputAction>& Action, const TCHAR* AssetName)
	{
		const FKey* NewKey = Settings.KeyBindings.Find(AssetName);
		if (!Action || !NewKey)
		{
			return;
		}
		// Replace the keyboard/mouse keys of the action (gamepad keys stay).
		TArray<FEnhancedActionKeyMapping> Old;
		for (const FEnhancedActionKeyMapping& Mapping : Copy->GetMappings())
		{
			if (Mapping.Action == Action && !Mapping.Key.IsGamepadKey())
			{
				Old.Add(Mapping);
			}
		}
		if (Old.IsEmpty())
		{
			return;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Old)
		{
			Copy->UnmapKey(Action, Mapping.Key);
		}
		FEnhancedActionKeyMapping& Mapped = Copy->MapKey(Action, *NewKey);
		Mapped.Modifiers = Old[0].Modifiers;
		Mapped.Triggers = Old[0].Triggers;
	});
	return Copy;
}

FKey UAGBPlayerSettingsSubsystem::FindKey(const TArray<TObjectPtr<UInputMappingContext>>& Contexts, const UInputAction* Action)
{
	for (const UInputMappingContext* Context : Contexts)
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context ? Context->GetMappings() : TArray<FEnhancedActionKeyMapping>())
		{
			if (Action && Mapping.Action == Action && !Mapping.Key.IsGamepadKey())
			{
				return Mapping.Key;
			}
		}
	}
	return FKey();
}
