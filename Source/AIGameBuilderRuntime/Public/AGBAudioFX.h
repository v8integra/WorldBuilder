#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AGBAudioFX.generated.h"

class UAudioComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPhysicalMaterial;
class USceneComponent;
class USoundBase;
class USoundClass;
class USphereComponent;
struct FHitResult;

/** What something is made of, for footsteps and hit sounds/effects. */
UENUM(BlueprintType)
enum class EAGBSurface : uint8
{
	Default,
	Grass,
	Dirt,
	Rock,
	Sand,
	Snow,
	Wood,
	Stone,
	Metal,
	Water,
	Mud,
	Gravel,
	Flesh,
	Plant,
};

/** Volume group a cue plays in (Settings > Audio). */
enum class EAGBCueGroup : uint8
{
	Effects,
	Ambient,
	Music,
	UI,
};

/** A named sound slot ("Footstep.Grass"): random sound from the list, optional Niagara effect. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBCue
{
	GENERATED_BODY()

	/** One is picked at random each time. Empty = a placeholder sound (or a related cue's sounds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	/** Spawned where the cue plays (hits, fire, waterfall spray...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue")
	TSoftObjectPtr<UNiagaraSystem> Effect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (ClampMin = "0", ClampMax = "4"))
	float Volume = 1.f;

	/** Silence this cue entirely (no placeholder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue")
	bool bMuted = false;
};

USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBSurfaceMaterial
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	TSoftObjectPtr<UPhysicalMaterial> PhysicalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	EAGBSurface Surface = EAGBSurface::Default;
};

/** Description of a cue slot the game uses. */
struct AIGAMEBUILDERRUNTIME_API FAGBCueSlot
{
	FName Name;
	EAGBCueGroup Group = EAGBCueGroup::Effects;
	/** Played in the world (else 2D). */
	bool b3D = true;
	/** Loops (ambience, fire, water) instead of one-shots. */
	bool bLoop = false;
	float FalloffMeters = 30.f;
	/** Words in asset names that suggest this slot (for auto-assign). */
	const TCHAR* Keywords = TEXT("");
	const TCHAR* Description = TEXT("");
	/** Engine Niagara system used when no effect is set ("" = none). */
	const TCHAR* PlaceholderEffect = TEXT("");
};

/** Sounds, music and effects of the game: every cue the game plays, by name. */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBAudioFXConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Cue name ("Footstep.Grass", "Impact.Wood", "Music.Night"...) -> sounds and effect. See AGBAudioFX::GetSlots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cues")
	TMap<FName, FAGBCue> Cues;

	/** Physical material -> surface (others are guessed from their names: "PM_Grass" = Grass). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfaces")
	TArray<FAGBSurfaceMaterial> SurfaceMaterials;

	/** Generated placeholder sounds for cues without sounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeholders")
	bool bPlaceholderSounds = true;

	/** Placeholder music (soft chords) when no music is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeholders")
	bool bPlaceholderMusic = true;

	/** Engine Niagara templates for hits and waterfall spray when no effect is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placeholders")
	bool bPlaceholderEffects = true;

	/** Silence between music tracks (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0"))
	float MusicGapMin = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0"))
	float MusicGapMax = 120.f;

	/** Seconds danger music keeps playing after the last threat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "1"))
	float DangerHoldSeconds = 12.f;
};

namespace AGBAudioFX
{
	/** Every cue slot the game plays. */
	AIGAMEBUILDERRUNTIME_API const TArray<FAGBCueSlot>& GetSlots();
	AIGAMEBUILDERRUNTIME_API const FAGBCueSlot* FindSlot(FName Cue);
	AIGAMEBUILDERRUNTIME_API FString SurfaceName(EAGBSurface Surface);
	/** Surface from a name ("PM_Grass_01" = Grass); Default if none matches. */
	AIGAMEBUILDERRUNTIME_API EAGBSurface SurfaceFromName(const FString& Name);
	/** "Impact.Wood" style cue name. */
	AIGAMEBUILDERRUNTIME_API FName CueName(const TCHAR* Family, EAGBSurface Surface);
}

/** A background loop faded toward a target volume. */
USTRUCT()
struct FAGBAmbientLoop
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Component;

	float CueVolume = 1.f;
	float Current = 0.f;
};

/**
 * Plays the game's sounds and effects on each player's machine: cues (asset or placeholder), the ambience around
 * the local player (day/night, biome zones, rain, wind), and music by state (day, night, danger, menu).
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBAudioFXSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAGBAudioFXSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Plays a one-shot cue in the world (Override replaces the cue's sounds, e.g. a creature's own voice). */
	void PlayCue(FName Cue, const FVector& Location, float VolumeScale = 1.f, USoundBase* Override = nullptr);

	/** Plays a cue without position (interface, the local player's own sounds). */
	void PlayCue2D(FName Cue, float VolumeScale = 1.f);

	/** Starts a looping cue attached to a component (fire, water). Stop it with Stop/FadeOut. */
	UAudioComponent* StartLoop(FName Cue, USceneComponent* AttachTo, float FalloffMeters = 0.f, USoundBase* Override = nullptr);

	/** Spawns the cue's effect attached (looping effects) or at a point (bursts). */
	UNiagaraComponent* SpawnEffect(FName Cue, USceneComponent* AttachTo, const FVector& Location, UNiagaraSystem* Override = nullptr);

	EAGBSurface GetSurface(const FHitResult& Hit);

	/** The local player is in danger (hurt, attacked): danger music for a while. */
	void NotifyDanger();

	bool IsActive() const;

private:
	const UAGBAudioFXConfig* GetConfig() const;
	const FAGBCue* FindCue(FName Cue) const;
	/** A sound for the cue: its assets, a related cue's assets, or a new placeholder. */
	USoundBase* ResolveSound(FName Cue, float& OutVolume, USoundBase* Override, bool bMusic = false);
	USoundClass* GetSoundClass(EAGBCueGroup Group) const;
	UAudioComponent* CreateAudio(USoundBase* Sound, const FAGBCueSlot* Slot, bool b3D, float FalloffMeters, USceneComponent* AttachTo, const FVector& Location);
	void StopAfter(UAudioComponent* Component, float Seconds);

	void TickAmbience(float DeltaTime, const FVector& ViewLocation);
	void TickMusic(float DeltaTime);
	void SetLoopVolume(FAGBAmbientLoop& Loop, FName Cue, float Target, float DeltaTime, USoundBase* Override = nullptr, FName ZonePreset = NAME_None, bool bNight = false);

	UPROPERTY(Transient)
	FAGBAmbientLoop DayLoop;

	UPROPERTY(Transient)
	FAGBAmbientLoop NightLoop;

	UPROPERTY(Transient)
	FAGBAmbientLoop ZoneDayLoop;

	UPROPERTY(Transient)
	FAGBAmbientLoop ZoneNightLoop;

	UPROPERTY(Transient)
	FAGBAmbientLoop RainLoop;

	UPROPERTY(Transient)
	FAGBAmbientLoop WindLoop;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Music;

	TWeakObjectPtr<class AAGBAmbienceZone> CurrentZone;
	TMap<TWeakObjectPtr<const UPhysicalMaterial>, EAGBSurface> SurfaceCache;

	FName MusicState;
	FName PlayingState;
	double TrackEndTime = 0.0;
	double NextTrackTime = 0.0;
	double DangerUntil = -1000.0;
	double NextShelterCheck = 0.0;
	bool bSheltered = false;
};

/** Plays footsteps by surface (and splashes in water, landings) for any character, on every player's machine. */
UCLASS(ClassGroup = (AIGameBuilder), meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBFootstepComponent();

	/** Distance between steps when walking / sprinting (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Audio")
	float WalkStride = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Audio")
	float SprintStride = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Audio")
	float Volume = 0.8f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void PlayStep(bool bLanding, float Loudness);

	float Distance = 0.f;
	bool bWasFalling = false;
	float FallSpeed = 0.f;
};

/** A biome sound area: its own day and night ambience replaces the default inside (soft edges). */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBAmbienceZone : public AActor
{
	GENERATED_BODY()

public:
	AAGBAmbienceZone();

	/** Placeholder sound style: Forest, Desert, Beach, Snow, Cave, Swamp, Plains. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Ambience")
	FName Preset = TEXT("Forest");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Ambience")
	TSoftObjectPtr<USoundBase> DaySound;

	/** Empty = DaySound at night too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Ambience")
	TSoftObjectPtr<USoundBase> NightSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Ambience", meta = (ClampMin = "1"))
	float RadiusMeters = 150.f;

	/** Fade width at the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Ambience", meta = (ClampMin = "1"))
	float EdgeMeters = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Ambience", meta = (ClampMin = "0", ClampMax = "4"))
	float Volume = 1.f;

	/** 0 outside .. 1 inside (2D distance). */
	float GetWeight(const FVector& Location) const;

	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "AI Game Builder|Ambience")
	TObjectPtr<USphereComponent> Area;
};

/** A sound source in the world: lake shore, river, waterfall, fire, insects... with an optional effect. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBSoundEmitter : public AActor
{
	GENERATED_BODY()

public:
	AAGBSoundEmitter();

	/** Plays cue "Loop.<Preset>" and effect "Effect.<Preset>": Water, River, Waterfall, Fire, Insects, Wind, Birds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Audio")
	FName Preset = TEXT("Water");

	/** Replaces the cue's sound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Audio")
	TSoftObjectPtr<USoundBase> Sound;

	/** Replaces the cue's effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Audio")
	TSoftObjectPtr<UNiagaraSystem> Effect;

	/** Heard up to this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Audio", meta = (ClampMin = "1"))
	float RadiusMeters = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Audio", meta = (ClampMin = "0", ClampMax = "4"))
	float Volume = 1.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "AI Game Builder|Audio")
	TObjectPtr<USphereComponent> Area;
};
