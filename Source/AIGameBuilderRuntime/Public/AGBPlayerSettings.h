#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "AGBPlayerSettings.generated.h"

class APlayerController;
class UInputAction;
class UInputMappingContext;
struct FAGBInputSet;

/** A player's own settings (graphics live in the engine's GameUserSettings). Saved on this machine. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBPlayerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float MasterVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float MusicVolume = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float EffectsVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float AmbientVolume = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float UIVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	float MouseSensitivity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	bool bInvertY = false;

	/** Keyboard/mouse key per action (asset name such as "IA_AGB_Jump"); missing = default key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	TMap<FName, FKey> KeyBindings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	float FieldOfView = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	float HUDScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay")
	bool bShowCrosshair = true;

	/** Name shown to other players. Empty = the computer's user name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer")
	FString PlayerName;

	/** This player's lasting id (made once): servers keep their character under it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer")
	FString PlayerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer")
	FString LastJoinAddress;
};

UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBPlayerSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FAGBPlayerSettings Settings;
};

/** An action players can rebind in Settings > Controls. */
struct FAGBRebindableAction
{
	FName AssetName;
	FText Label;
};

/** Loads, applies and saves the player's settings (volumes, mouse, FOV, HUD size, key bindings). */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBPlayerSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UAGBPlayerSettingsSubsystem* Get(const UObject* WorldContext);

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Settings")
	const FAGBPlayerSettings& GetSettings() const { return Settings; }

	/** Changes the settings and applies them right away (saved with SaveSettings). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Settings")
	void SetSettings(const FAGBPlayerSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Settings")
	void SaveSettings();

	/** Applies volumes and the local players' camera, mouse and key bindings. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Settings")
	void ApplySettings();

	void ApplyAudio(UWorld* World) const;

	/** Rebinds an action's keyboard/mouse key (an invalid key restores the default). */
	void RebindKey(FName ActionAssetName, const FKey& Key);
	void ResetKeyBindings();

	/** The actions shown in Settings > Controls. */
	static const TArray<FAGBRebindableAction>& GetRebindableActions();

	/** A runtime copy of a mapping context with the player's keys. */
	UInputMappingContext* MakeBoundContext(UInputMappingContext* Source, FAGBInputSet& Set, UObject* Outer) const;

	/** Current keyboard/mouse key of an action in contexts (empty if unbound). */
	static FKey FindKey(const TArray<TObjectPtr<UInputMappingContext>>& Contexts, const UInputAction* Action);

private:
	UPROPERTY()
	FAGBPlayerSettings Settings;
};
