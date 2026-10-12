#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "AudioFXTools.generated.h"

/// Result of the audio and effects tools.
USTRUCT(BlueprintType)
struct FAudioFXToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// One line per item: slots with their sounds, assignments made, emitters placed...
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Lines;
};

/// Sounds, music and effects of AI Game Builder games. Every sound the game plays is a named cue slot ("Footstep.Grass", "Impact.Wood", "UI.Click", "Ambience.Night", "Loop.Fire", "Music.Danger"...); empty slots play generated placeholder sounds, so the game is never silent. Footsteps follow the ground's surface (landscape layers get physical materials), ambience follows day/night, weather, altitude and biome zones, music follows day/night/danger/menu, campfires crackle and flicker. After the user adds a sound pack: AutoAssignSounds.
UCLASS(BlueprintType, Hidden)
class UAudioFXTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Sets up sound for the game: the audio config asset, surface physical materials assigned to the landscape's
	 * paint layers by name (grass, dirt, rock, sand, snow, mud, gravel) for footsteps, and water sound emitters along
	 * lakes and rivers. Safe to run again (replaces the water emitters it placed).
	 * @param bAssignLandscapeSurfaces Give paint layers surface materials (only layers without one).
	 * @param bAddWaterSounds Place lake-shore and river sounds along water bodies.
	 * @return What was set up.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult SetupAudioFX(bool bAssignLandscapeSurfaces = true, bool bAddWaterSounds = true);

	/**
	 * Sets the sounds and/or effect of a cue slot (see ListSoundSlots).
	 * @param SoundPaths Sound assets (variations, one picked at random). [] = keep, ["none"] = placeholder again, ["mute"] = silent.
	 * @param Slot Cue slot, e.g. "Footstep.Grass", "Music.Night", "Effect.Fire".
	 * @param EffectPath Niagara system for the slot ("none" clears, "" keeps).
	 * @param Volume Volume 0-4 (0 = keep).
	 * @param bAdd Add the sounds to the existing ones instead of replacing them.
	 * @return The slot now.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult SetSoundCue(const TArray<FString>& SoundPaths, const FString& Slot, const FString& EffectPath = TEXT(""), double Volume = 0.0, bool bAdd = false);

	/**
	 * Fills cue slots from sound and Niagara assets by their names and folders (e.g. "SFX_Footstep_Grass_03" ->
	 * Footstep.Grass, "Music_Combat_Loop" -> Music.Danger). Run after adding a sound or VFX pack.
	 * @param Folder Content folder to search ("/Game" = everything).
	 * @param bOverwrite Replace slots that already have sounds.
	 * @return Assignments and assets that matched nothing.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult AutoAssignSounds(const FString& Folder = TEXT("/Game"), bool bOverwrite = false);

	/**
	 * Lists every cue slot with its sounds/effect or "placeholder".
	 * @return One line per slot.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult ListSoundSlots();

	/**
	 * Adds a biome sound area: its own day/night ambience replaces the default inside.
	 * @param CenterXM Center X (m).
	 * @param CenterYM Center Y (m).
	 * @param RadiusM Radius (m).
	 * @param Preset Placeholder style: Forest, Desert, Beach, Snow, Cave, Swamp, Plains.
	 * @param DaySoundPath Sound asset by day ("" = placeholder).
	 * @param NightSoundPath Sound asset at night ("" = day sound or placeholder).
	 * @param EdgeM Fade width at the edge (m).
	 * @param Label Actor label ("auto").
	 * @return The zone.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult CreateAmbienceZone(double CenterXM, double CenterYM, double RadiusM = 150.0, const FString& Preset = TEXT("Forest"),
		const FString& DaySoundPath = TEXT(""), const FString& NightSoundPath = TEXT(""), double EdgeM = 40.0, const FString& Label = TEXT("auto"));

	/**
	 * Places a sound source (with an optional effect) on the ground: waterfalls, streams, insects, wind spots, fires.
	 * @param XM X (m).
	 * @param YM Y (m).
	 * @param Preset Water, River, Waterfall (adds spray), Fire, Insects, Wind, Birds.
	 * @param HeightM Height above the ground (m).
	 * @param RadiusM Heard up to (m).
	 * @param SoundPath Sound asset ("" = the preset's cue).
	 * @param EffectPath Niagara system ("" = the preset's effect).
	 * @param Label Actor label ("auto").
	 * @return The emitter.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult PlaceSoundEmitter(double XM, double YM, const FString& Preset = TEXT("Water"), double HeightM = 1.0, double RadiusM = 30.0,
		const FString& SoundPath = TEXT(""), const FString& EffectPath = TEXT(""), const FString& Label = TEXT("auto"));

	/**
	 * Gives a creature its own voice ("" = keep, "none" = use the game's Creature.* cue).
	 * @param CreatureId Creature id.
	 * @param HurtSoundPath When hurt.
	 * @param AttackSoundPath When attacking.
	 * @param DeathSoundPath When dying.
	 * @param IdleSoundPath Now and then.
	 * @return The creature's sounds.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult SetCreatureSounds(const FString& CreatureId, const FString& HurtSoundPath = TEXT(""), const FString& AttackSoundPath = TEXT(""),
		const FString& DeathSoundPath = TEXT(""), const FString& IdleSoundPath = TEXT(""));

	/**
	 * Shows the audio setup: config, filled vs placeholder slots, surfaces, zones, emitters.
	 * @return Status lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Audio")
	static FAudioFXToolsResult GetAudioFXStatus();
};
