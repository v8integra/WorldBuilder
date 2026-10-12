#include "AGBAudioFX.h"

#include "AGBBuildPiece.h"
#include "AGBBuildingTypes.h"
#include "AGBMenus.h"
#include "AGBSynth.h"
#include "AGBUITheme.h"
#include "AGBVitalsComponent.h"
#include "AGBWorldEnvironment.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "TimerManager.h"

namespace
{
	const FName WaterProfile(TEXT("WaterBodyCollision"));
	const TCHAR* BurstEffect = TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurstLightweight.DirectionalBurstLightweight");
	const TCHAR* FountainEffect = TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight");

	FAGBCueSlot Slot(const TCHAR* Name, EAGBCueGroup Group, bool b3D, bool bLoop, float Falloff, const TCHAR* Keywords, const TCHAR* Description, const TCHAR* Effect = TEXT(""))
	{
		FAGBCueSlot S;
		S.Name = Name;
		S.Group = Group;
		S.b3D = b3D;
		S.bLoop = bLoop;
		S.FalloffMeters = Falloff;
		S.Keywords = Keywords;
		S.Description = Description;
		S.PlaceholderEffect = Effect;
		return S;
	}
}

// ---------------------------------------------------------------- slots

namespace AGBAudioFX
{
	const TArray<FAGBCueSlot>& GetSlots()
	{
		using G = EAGBCueGroup;
		// Keywords: "a,b|c,d" = (a or b) and (c or d) in the asset's name or folder.
		static const TArray<FAGBCueSlot> Slots = {
			Slot(TEXT("Footstep.Default"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot"), TEXT("Footsteps on anything without its own sound")),
			Slot(TEXT("Footstep.Grass"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|grass"), TEXT("Footsteps on grass")),
			Slot(TEXT("Footstep.Dirt"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|dirt,soil,earth,ground"), TEXT("Footsteps on dirt")),
			Slot(TEXT("Footstep.Rock"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|rock,cliff"), TEXT("Footsteps on rock")),
			Slot(TEXT("Footstep.Sand"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|sand,beach"), TEXT("Footsteps on sand")),
			Slot(TEXT("Footstep.Snow"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|snow,ice"), TEXT("Footsteps on snow")),
			Slot(TEXT("Footstep.Wood"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|wood,plank,timber"), TEXT("Footsteps on wooden floors")),
			Slot(TEXT("Footstep.Stone"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|stone,concrete,tile,brick"), TEXT("Footsteps on stone floors")),
			Slot(TEXT("Footstep.Metal"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|metal,steel,iron"), TEXT("Footsteps on metal")),
			Slot(TEXT("Footstep.Water"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot,wade|water,puddle,shallow,wade"), TEXT("Walking through shallow water")),
			Slot(TEXT("Footstep.Mud"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|mud"), TEXT("Footsteps in mud")),
			Slot(TEXT("Footstep.Gravel"), G::Effects, true, false, 25.f, TEXT("footstep,footsteps,step,walk,foot|gravel,pebble"), TEXT("Footsteps on gravel")),
			Slot(TEXT("Impact.Default"), G::Effects, true, false, 35.f, TEXT("impact,hit,punch"), TEXT("Hits on anything else"), BurstEffect),
			Slot(TEXT("Impact.Wood"), G::Effects, true, false, 40.f, TEXT("impact,hit,chop,axe|wood,tree,log,timber"), TEXT("Chopping trees, hitting wood"), BurstEffect),
			Slot(TEXT("Impact.Stone"), G::Effects, true, false, 40.f, TEXT("impact,hit,pickaxe,mine,mining|stone,rock,ore"), TEXT("Mining rock"), BurstEffect),
			Slot(TEXT("Impact.Flesh"), G::Effects, true, false, 35.f, TEXT("impact,hit,stab,punch,slash|flesh,body,meat,gore"), TEXT("Hitting creatures"), BurstEffect),
			Slot(TEXT("Impact.Plant"), G::Effects, true, false, 30.f, TEXT("rustle,bush,leaves,foliage,plant"), TEXT("Picking bushes and plants"), BurstEffect),
			Slot(TEXT("Impact.Metal"), G::Effects, true, false, 40.f, TEXT("impact,hit,clang|metal,steel,iron"), TEXT("Hitting metal"), BurstEffect),
			Slot(TEXT("Event.Swing"), G::Effects, true, false, 20.f, TEXT("swing,whoosh,swoosh,woosh"), TEXT("Swinging a tool or weapon")),
			Slot(TEXT("Event.Hurt"), G::Effects, true, false, 30.f, TEXT("hurt,pain,grunt,damage"), TEXT("Player gets hurt")),
			Slot(TEXT("Event.Death"), G::Effects, true, false, 40.f, TEXT("death,die,dying|player,male,female,human,voice"), TEXT("Player dies")),
			Slot(TEXT("Event.Eat"), G::Effects, true, false, 15.f, TEXT("eat,eating,chew,bite,crunch"), TEXT("Eating")),
			Slot(TEXT("Event.Drink"), G::Effects, true, false, 15.f, TEXT("drink,drinking,gulp,sip"), TEXT("Drinking")),
			Slot(TEXT("Event.Pickup"), G::Effects, false, false, 0.f, TEXT("pickup,pick_up,collect,loot"), TEXT("Picking an item up (heard by the player)")),
			Slot(TEXT("Event.Craft"), G::Effects, false, false, 0.f, TEXT("craft,crafting,anvil,complete"), TEXT("Crafting finished (heard by the player)")),
			Slot(TEXT("Event.Place"), G::Effects, true, false, 35.f, TEXT("place,placement,build,construct"), TEXT("Placing a building piece or station")),
			Slot(TEXT("Event.Shoot"), G::Effects, true, false, 40.f, TEXT("bow,arrow,shoot,twang,throw"), TEXT("Shooting or throwing")),
			Slot(TEXT("Event.Land"), G::Effects, true, false, 25.f, TEXT("land,landing"), TEXT("Landing after a jump or fall")),
			Slot(TEXT("Event.Splash"), G::Effects, true, false, 30.f, TEXT("splash"), TEXT("Jumping into water")),
			Slot(TEXT("UI.Click"), G::UI, false, false, 0.f, TEXT("click,select,confirm,button"), TEXT("Clicking buttons and slots")),
			Slot(TEXT("UI.Hover"), G::UI, false, false, 0.f, TEXT("hover,highlight,tick"), TEXT("Moving over a button")),
			Slot(TEXT("UI.Open"), G::UI, false, false, 0.f, TEXT("open,menu_open,inventory_open,window_open,whoosh_in"), TEXT("Opening a menu or the inventory")),
			Slot(TEXT("UI.Close"), G::UI, false, false, 0.f, TEXT("close,back,cancel,whoosh_out"), TEXT("Closing a menu")),
			Slot(TEXT("UI.Error"), G::UI, false, false, 0.f, TEXT("error,deny,denied,fail,invalid"), TEXT("Something is not possible")),
			Slot(TEXT("Creature.Hurt"), G::Effects, true, false, 40.f, TEXT("creature,animal,wolf,deer,bear,boar,beast,monster|hurt,pain,hit,yelp"), TEXT("Creature hurt (creatures can have their own)")),
			Slot(TEXT("Creature.Attack"), G::Effects, true, false, 45.f, TEXT("creature,animal,wolf,bear,boar,beast,monster|attack,growl,bite,snarl"), TEXT("Creature attacks")),
			Slot(TEXT("Creature.Death"), G::Effects, true, false, 45.f, TEXT("creature,animal,wolf,deer,bear,boar,beast,monster|death,die"), TEXT("Creature dies")),
			Slot(TEXT("Creature.Idle"), G::Effects, true, false, 35.f, TEXT("creature,animal,wolf,deer,bear,boar,beast,monster|idle,call,breath,sniff,howl,bark"), TEXT("Creature sounds now and then")),
			Slot(TEXT("Ambience.Day"), G::Ambient, false, true, 0.f, TEXT("ambience,ambient,amb,atmos,atmosphere|day,forest,birds,meadow,nature"), TEXT("Background sound by day")),
			Slot(TEXT("Ambience.Night"), G::Ambient, false, true, 0.f, TEXT("ambience,ambient,amb,atmos,atmosphere|night,crickets,insects,owl"), TEXT("Background sound at night")),
			Slot(TEXT("Ambience.Rain"), G::Ambient, false, true, 0.f, TEXT("rain,rainfall,storm"), TEXT("Rain (follows the weather; quieter under a roof)")),
			Slot(TEXT("Ambience.Wind"), G::Ambient, false, true, 0.f, TEXT("wind,gust,breeze,howling"), TEXT("Wind (stronger high up and in storms)")),
			Slot(TEXT("Loop.Fire"), G::Ambient, true, true, 15.f, TEXT("fire,campfire,flame,burning,crackle,fireplace"), TEXT("Burning campfires and furnaces")),
			Slot(TEXT("Loop.Water"), G::Ambient, true, true, 30.f, TEXT("lake,pond,shore,lapping,waves"), TEXT("Lake shores (sound emitters)")),
			Slot(TEXT("Loop.River"), G::Ambient, true, true, 35.f, TEXT("river,stream,creek,brook,flowing"), TEXT("Rivers (sound emitters)")),
			Slot(TEXT("Loop.Waterfall"), G::Ambient, true, true, 60.f, TEXT("waterfall,cascade"), TEXT("Waterfalls (sound emitters)")),
			Slot(TEXT("Loop.Insects"), G::Ambient, true, true, 20.f, TEXT("insect,insects,cricket,bugs,cicada"), TEXT("Insects (sound emitters)")),
			Slot(TEXT("Loop.Wind"), G::Ambient, true, true, 40.f, TEXT("wind,gust"), TEXT("Wind spots (sound emitters)")),
			Slot(TEXT("Loop.Birds"), G::Ambient, true, true, 40.f, TEXT("bird,birds,songbird,chirp"), TEXT("Birds (sound emitters)")),
			Slot(TEXT("Effect.Fire"), G::Effects, true, false, 0.f, TEXT("fire,flame,campfire"), TEXT("Flames on burning stations (effect)")),
			Slot(TEXT("Effect.Smoke"), G::Effects, true, false, 0.f, TEXT("smoke"), TEXT("Smoke on burning stations (effect)")),
			Slot(TEXT("Effect.Waterfall"), G::Effects, true, false, 0.f, TEXT("waterfall,mist,spray"), TEXT("Waterfall spray (effect)"), FountainEffect),
			Slot(TEXT("Music.Day"), G::Music, false, false, 0.f, TEXT("music,song,track,theme,ost|day,explore,exploration,calm,peaceful,ambient"), TEXT("Music by day (tracks play with silence between)")),
			Slot(TEXT("Music.Night"), G::Music, false, false, 0.f, TEXT("music,song,track,theme,ost|night,dark,mysterious,moon"), TEXT("Music at night")),
			Slot(TEXT("Music.Danger"), G::Music, false, false, 0.f, TEXT("music,song,track,theme,ost|combat,battle,danger,action,fight,tension"), TEXT("Music when hurt or attacked")),
			Slot(TEXT("Music.Menu"), G::Music, false, false, 0.f, TEXT("music,song,track,theme,ost|menu,title,main"), TEXT("Main menu music")),
		};
		return Slots;
	}

	const FAGBCueSlot* FindSlot(FName Cue)
	{
		return GetSlots().FindByPredicate([Cue](const FAGBCueSlot& S) { return S.Name == Cue; });
	}

	FString SurfaceName(EAGBSurface Surface)
	{
		return StaticEnum<EAGBSurface>()->GetNameStringByValue(static_cast<int64>(Surface));
	}

	EAGBSurface SurfaceFromName(const FString& Name)
	{
		const FString Lower = Name.ToLower();
		struct FWords { EAGBSurface Surface; const TCHAR* Words[5]; };
		static const FWords Table[] = {
			{ EAGBSurface::Grass, { TEXT("grass"), TEXT("meadow"), nullptr } },
			{ EAGBSurface::Gravel, { TEXT("gravel"), TEXT("pebble"), nullptr } },
			{ EAGBSurface::Mud, { TEXT("mud"), TEXT("swamp"), nullptr } },
			{ EAGBSurface::Sand, { TEXT("sand"), TEXT("beach"), TEXT("dune"), nullptr } },
			{ EAGBSurface::Snow, { TEXT("snow"), TEXT("ice"), TEXT("frost"), nullptr } },
			{ EAGBSurface::Water, { TEXT("water"), nullptr } },
			{ EAGBSurface::Metal, { TEXT("metal"), TEXT("steel"), TEXT("iron"), nullptr } },
			{ EAGBSurface::Stone, { TEXT("stone"), TEXT("brick"), TEXT("concrete"), TEXT("cobble"), nullptr } },
			{ EAGBSurface::Rock, { TEXT("rock"), TEXT("cliff"), TEXT("boulder"), nullptr } },
			{ EAGBSurface::Wood, { TEXT("wood"), TEXT("plank"), TEXT("timber"), TEXT("log"), nullptr } },
			{ EAGBSurface::Dirt, { TEXT("dirt"), TEXT("soil"), TEXT("earth"), TEXT("ground"), nullptr } },
			{ EAGBSurface::Plant, { TEXT("leaf"), TEXT("leaves"), TEXT("bush"), TEXT("foliage"), nullptr } },
			{ EAGBSurface::Flesh, { TEXT("flesh"), TEXT("meat"), nullptr } },
		};
		for (const FWords& Entry : Table)
		{
			for (const TCHAR* Word : Entry.Words)
			{
				if (!Word)
				{
					break;
				}
				if (Lower.Contains(Word))
				{
					return Entry.Surface;
				}
			}
		}
		return EAGBSurface::Default;
	}

	FName CueName(const TCHAR* Family, EAGBSurface Surface)
	{
		return FName(*FString::Printf(TEXT("%s.%s"), Family, *SurfaceName(Surface)));
	}
}

// ---------------------------------------------------------------- subsystem

UAGBAudioFXSubsystem* UAGBAudioFXSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UAGBAudioFXSubsystem>() : nullptr;
}

bool UAGBAudioFXSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UAGBAudioFXSubsystem::Deinitialize()
{
	for (FAGBAmbientLoop* Loop : { &DayLoop, &NightLoop, &ZoneDayLoop, &ZoneNightLoop, &RainLoop, &WindLoop })
	{
		Loop->Component = nullptr;
	}
	Music = nullptr;
	Super::Deinitialize();
}

TStatId UAGBAudioFXSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAGBAudioFXSubsystem, STATGROUP_Tickables);
}

bool UAGBAudioFXSubsystem::IsActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_DedicatedServer && World->bAllowAudioPlayback;
}

const UAGBAudioFXConfig* UAGBAudioFXSubsystem::GetConfig() const
{
	if (const UAGBAudioFXConfig* Config = GetDefault<UAGBUISettings>()->AudioFX.LoadSynchronous())
	{
		return Config;
	}
	return GetDefault<UAGBAudioFXConfig>();
}

const FAGBCue* UAGBAudioFXSubsystem::FindCue(FName Cue) const
{
	return GetConfig()->Cues.Find(Cue);
}

USoundClass* UAGBAudioFXSubsystem::GetSoundClass(EAGBCueGroup Group) const
{
	const UAGBUISettings* Settings = GetDefault<UAGBUISettings>();
	switch (Group)
	{
	case EAGBCueGroup::Ambient: return Settings->AmbientSoundClass.LoadSynchronous();
	case EAGBCueGroup::Music: return Settings->MusicSoundClass.LoadSynchronous();
	case EAGBCueGroup::UI: return Settings->UISoundClass.LoadSynchronous();
	default: return Settings->EffectsSoundClass.LoadSynchronous();
	}
}

USoundBase* UAGBAudioFXSubsystem::ResolveSound(FName Cue, float& OutVolume, USoundBase* Override, bool bMusic)
{
	const FAGBCue* Found = FindCue(Cue);
	OutVolume = Found ? Found->Volume : 1.f;
	if (Override)
	{
		return Override;
	}
	if (Found && Found->bMuted)
	{
		return nullptr;
	}
	auto Pick = [](const FAGBCue* Entry) -> USoundBase*
	{
		return Entry && Entry->Sounds.Num() > 0 ? Entry->Sounds[FMath::RandHelper(Entry->Sounds.Num())].LoadSynchronous() : nullptr;
	};
	if (USoundBase* Sound = Pick(Found))
	{
		return Sound;
	}
	// "Footstep.Grass" without sounds uses "Footstep.Default" sounds before a placeholder.
	FString Family, Leaf;
	const bool bHasFamily = Cue.ToString().Split(TEXT("."), &Family, &Leaf);
	if (bHasFamily && Leaf != TEXT("Default"))
	{
		if (USoundBase* Sound = Pick(FindCue(FName(*(Family + TEXT(".Default"))))))
		{
			return Sound;
		}
	}
	const UAGBAudioFXConfig* Config = GetConfig();
	if (!(bMusic ? Config->bPlaceholderMusic : Config->bPlaceholderSounds))
	{
		return nullptr;
	}
	FAGBSynthPatch Patch;
	if (AGBSynth::GetPatch(Cue, Patch) || (bHasFamily && AGBSynth::GetPatch(FName(*(Family + TEXT(".Default"))), Patch)))
	{
		return UAGBSynthSound::Create(GetWorld(), Patch);
	}
	return nullptr;
}

UAudioComponent* UAGBAudioFXSubsystem::CreateAudio(USoundBase* Sound, const FAGBCueSlot* CueSlot, bool b3D, float FalloffMeters, USceneComponent* AttachTo, const FVector& Location)
{
	UWorld* World = GetWorld();
	AActor* Owner = AttachTo ? AttachTo->GetOwner() : World->GetWorldSettings();
	if (!Owner || !Sound)
	{
		return nullptr;
	}
	UAudioComponent* Audio = NewObject<UAudioComponent>(Owner);
	Audio->bAutoActivate = false;
	Audio->bAutoDestroy = !(CueSlot && CueSlot->bLoop) && !AttachTo;
	Audio->SetSound(Sound);
	const EAGBCueGroup Group = CueSlot ? CueSlot->Group : EAGBCueGroup::Effects;
	Audio->SoundClassOverride = GetSoundClass(Group);
	Audio->bIsUISound = Group == EAGBCueGroup::UI || Group == EAGBCueGroup::Music;
	Audio->bAllowSpatialization = b3D;
	if (b3D)
	{
		// Many imported sounds have no attenuation: give every world sound a sensible range.
		Audio->bOverrideAttenuation = true;
		FSoundAttenuationSettings& Attenuation = Audio->AttenuationOverrides;
		Attenuation.bAttenuate = true;
		Attenuation.bSpatialize = true;
		const float Falloff = FMath::Max(1.f, FalloffMeters) * 100.f;
		Attenuation.AttenuationShapeExtents = FVector(Falloff * 0.1f, 0.f, 0.f);
		Attenuation.FalloffDistance = Falloff;
	}
	if (AttachTo)
	{
		Audio->SetupAttachment(AttachTo);
	}
	else
	{
		Audio->SetWorldLocation(Location);
	}
	Audio->RegisterComponentWithWorld(World);
	return Audio;
}

void UAGBAudioFXSubsystem::StopAfter(UAudioComponent* Component, float Seconds)
{
	// Generated one-shots never end by themselves.
	TWeakObjectPtr<UAudioComponent> Weak = Component;
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
	{
		if (UAudioComponent* Audio = Weak.Get())
		{
			Audio->Stop();
			if (!Audio->bAutoDestroy)
			{
				Audio->DestroyComponent();
			}
		}
	}), FMath::Max(0.05f, Seconds), false);
}

void UAGBAudioFXSubsystem::PlayCue(FName Cue, const FVector& Location, float VolumeScale, USoundBase* Override)
{
	if (!IsActive())
	{
		return;
	}
	const FAGBCueSlot* CueSlot = AGBAudioFX::FindSlot(Cue);
	float Volume = 1.f;
	if (USoundBase* Sound = ResolveSound(Cue, Volume, Override))
	{
		const bool b3D = !CueSlot || CueSlot->b3D;
		if (UAudioComponent* Audio = CreateAudio(Sound, CueSlot, b3D, CueSlot ? CueSlot->FalloffMeters : 30.f, nullptr, Location))
		{
			Audio->SetVolumeMultiplier(Volume * VolumeScale);
			// A little variation so repeated steps and hits do not sound identical.
			Audio->SetPitchMultiplier(FMath::FRandRange(0.93f, 1.07f));
			Audio->Play();
			if (const UAGBSynthSound* Synth = Cast<UAGBSynthSound>(Sound))
			{
				StopAfter(Audio, Synth->Duration + 0.05f);
			}
		}
	}
	SpawnEffect(Cue, nullptr, Location);
}

void UAGBAudioFXSubsystem::PlayCue2D(FName Cue, float VolumeScale)
{
	if (!IsActive())
	{
		return;
	}
	const FAGBCueSlot* CueSlot = AGBAudioFX::FindSlot(Cue);
	float Volume = 1.f;
	if (USoundBase* Sound = ResolveSound(Cue, Volume, nullptr))
	{
		if (UAudioComponent* Audio = CreateAudio(Sound, CueSlot, false, 0.f, nullptr, FVector::ZeroVector))
		{
			Audio->SetVolumeMultiplier(Volume * VolumeScale);
			Audio->Play();
			if (const UAGBSynthSound* Synth = Cast<UAGBSynthSound>(Sound))
			{
				StopAfter(Audio, Synth->Duration + 0.05f);
			}
		}
	}
}

UAudioComponent* UAGBAudioFXSubsystem::StartLoop(FName Cue, USceneComponent* AttachTo, float FalloffMeters, USoundBase* Override)
{
	if (!IsActive() || !AttachTo)
	{
		return nullptr;
	}
	const FAGBCueSlot* CueSlot = AGBAudioFX::FindSlot(Cue);
	float Volume = 1.f;
	USoundBase* Sound = ResolveSound(Cue, Volume, Override);
	UAudioComponent* Audio = Sound ? CreateAudio(Sound, CueSlot, true, FalloffMeters > 0.f ? FalloffMeters : (CueSlot ? CueSlot->FalloffMeters : 30.f), AttachTo, FVector::ZeroVector) : nullptr;
	if (Audio)
	{
		Audio->SetVolumeMultiplier(Volume);
		Audio->FadeIn(1.f, 1.f);
	}
	return Audio;
}

UNiagaraComponent* UAGBAudioFXSubsystem::SpawnEffect(FName Cue, USceneComponent* AttachTo, const FVector& Location, UNiagaraSystem* Override)
{
	if (!IsActive())
	{
		return nullptr;
	}
	UNiagaraSystem* System = Override;
	if (!System)
	{
		const FAGBCue* Found = FindCue(Cue);
		if (Found && Found->bMuted)
		{
			return nullptr;
		}
		System = Found ? Found->Effect.LoadSynchronous() : nullptr;
	}
	if (!System && GetConfig()->bPlaceholderEffects)
	{
		const FAGBCueSlot* CueSlot = AGBAudioFX::FindSlot(Cue);
		if (CueSlot && *CueSlot->PlaceholderEffect)
		{
			System = LoadObject<UNiagaraSystem>(nullptr, CueSlot->PlaceholderEffect, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}
	if (!System)
	{
		return nullptr;
	}
	if (AttachTo)
	{
		return UNiagaraFunctionLibrary::SpawnSystemAttached(System, AttachTo, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/false);
	}
	return UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Location, FRotator::ZeroRotator, FVector(0.4f), /*bAutoDestroy=*/true);
}

EAGBSurface UAGBAudioFXSubsystem::GetSurface(const FHitResult& Hit)
{
	const AActor* Actor = Hit.GetActor();
	if (const AAGBBuildPiece* Piece = Cast<AAGBBuildPiece>(Actor))
	{
		const EAGBSurface FromTier = Piece->Definition ? AGBAudioFX::SurfaceFromName(Piece->Definition->Tier.ToString()) : EAGBSurface::Default;
		return FromTier == EAGBSurface::Default ? EAGBSurface::Wood : FromTier;
	}
	if (Cast<APawn>(Actor))
	{
		return EAGBSurface::Flesh;
	}
	const UPhysicalMaterial* Material = Hit.PhysMaterial.Get();
	if (!Material)
	{
		return EAGBSurface::Default;
	}
	if (const EAGBSurface* Cached = SurfaceCache.Find(Material))
	{
		return *Cached;
	}
	EAGBSurface Surface = AGBAudioFX::SurfaceFromName(Material->GetName());
	for (const FAGBSurfaceMaterial& Entry : GetConfig()->SurfaceMaterials)
	{
		if (Entry.PhysicalMaterial.Get() == Material)
		{
			Surface = Entry.Surface;
			break;
		}
	}
	SurfaceCache.Add(Material, Surface);
	return Surface;
}

void UAGBAudioFXSubsystem::NotifyDanger()
{
	if (const UWorld* World = GetWorld())
	{
		DangerUntil = World->GetRealTimeSeconds() + GetConfig()->DangerHoldSeconds;
	}
}

void UAGBAudioFXSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsActive())
	{
		return;
	}
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Player || !Player->IsLocalController())
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (!Player->IsA<AAGBMenuPlayerController>())
	{
		TickAmbience(DeltaTime, ViewLocation);
	}
	TickMusic(DeltaTime);
}

void UAGBAudioFXSubsystem::SetLoopVolume(FAGBAmbientLoop& Loop, FName Cue, float Target, float DeltaTime, USoundBase* Override, FName ZonePreset, bool bNight)
{
	if (!Loop.Component)
	{
		if (Target < 0.01f)
		{
			return;
		}
		USoundBase* Sound = Override;
		Loop.CueVolume = 1.f;
		if (!Sound && ZonePreset != NAME_None)
		{
			FAGBSynthPatch Patch;
			if (GetConfig()->bPlaceholderSounds && AGBSynth::GetZonePatch(ZonePreset, bNight, Patch))
			{
				Sound = UAGBSynthSound::Create(GetWorld(), Patch);
			}
		}
		else if (!Sound)
		{
			Sound = ResolveSound(Cue, Loop.CueVolume, nullptr);
		}
		Loop.Component = CreateAudio(Sound, AGBAudioFX::FindSlot(TEXT("Ambience.Day")), false, 0.f, nullptr, FVector::ZeroVector);
		if (!Loop.Component)
		{
			return;
		}
		Loop.Component->bAutoDestroy = false;
		Loop.Current = 0.f;
		Loop.Component->SetVolumeMultiplier(0.f);
		Loop.Component->Play();
	}
	Loop.Current = FMath::FInterpConstantTo(Loop.Current, Target, DeltaTime, 0.5f);
	Loop.Component->SetVolumeMultiplier(FMath::Max(0.001f, Loop.Current * Loop.CueVolume));
	if (!Loop.Component->IsPlaying() && Target > 0.01f)
	{
		Loop.Component->Play(); // Imported ambience that does not loop by itself.
	}
	if (Target < 0.01f && Loop.Current < 0.01f)
	{
		Loop.Component->Stop();
		Loop.Component->DestroyComponent();
		Loop.Component = nullptr;
	}
}

void UAGBAudioFXSubsystem::TickAmbience(float DeltaTime, const FVector& ViewLocation)
{
	const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld());
	const float Night = Environment ? Environment->GetNightAmount() : 0.f;
	const float Precipitation = Environment ? Environment->GetPrecipitation() : 0.f;

	// Strongest biome zone around the player.
	AAGBAmbienceZone* Best = nullptr;
	float Weight = 0.f;
	for (TActorIterator<AAGBAmbienceZone> It(GetWorld()); It; ++It)
	{
		const float W = It->GetWeight(ViewLocation);
		if (W > Weight)
		{
			Weight = W;
			Best = *It;
		}
	}
	if (Best != CurrentZone.Get())
	{
		for (FAGBAmbientLoop* Loop : { &ZoneDayLoop, &ZoneNightLoop })
		{
			if (Loop->Component)
			{
				Loop->Component->FadeOut(1.5f, 0.f);
				Loop->Component->bAutoDestroy = true;
				Loop->Component = nullptr;
			}
		}
		CurrentZone = Best;
	}

	// Rain is muffled under a roof; snow is silent but windier.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Precipitation > 0.05f && Now >= NextShelterCheck)
	{
		NextShelterCheck = Now + 0.5;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBAudioShelter), false, GetWorld()->GetFirstPlayerController()->GetPawn());
		bSheltered = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + FVector(0.0, 0.0, 3000.0), ECC_Visibility, Params);
	}
	const APawn* Pawn = GetWorld()->GetFirstPlayerController()->GetPawn();
	const UAGBVitalsComponent* Vitals = Pawn ? Pawn->FindComponentByClass<UAGBVitalsComponent>() : nullptr;
	const bool bSnow = Environment && Vitals && Vitals->GetAirTemperature() < Environment->SnowBelowC;
	const float Rain = bSnow ? 0.f : Precipitation * (bSheltered ? 0.35f : 1.f);
	const float Wind = FMath::Clamp(0.12f + FMath::Clamp(static_cast<float>(ViewLocation.Z - 3000.0) / 15000.f, 0.f, 0.6f)
		+ Precipitation * (bSnow ? 0.6f : 0.3f), 0.f, 1.f);

	const float Base = 1.f - Weight;
	SetLoopVolume(DayLoop, TEXT("Ambience.Day"), Base * (1.f - Night), DeltaTime);
	SetLoopVolume(NightLoop, TEXT("Ambience.Night"), Base * Night, DeltaTime);
	if (Best)
	{
		USoundBase* DaySound = Best->DaySound.LoadSynchronous();
		USoundBase* NightSound = Best->NightSound.IsNull() ? DaySound : Best->NightSound.LoadSynchronous();
		SetLoopVolume(ZoneDayLoop, NAME_None, Weight * Best->Volume * (1.f - Night), DeltaTime, DaySound, Best->Preset, false);
		SetLoopVolume(ZoneNightLoop, NAME_None, Weight * Best->Volume * Night, DeltaTime, NightSound, Best->Preset, true);
	}
	SetLoopVolume(RainLoop, TEXT("Ambience.Rain"), Rain, DeltaTime);
	SetLoopVolume(WindLoop, TEXT("Ambience.Wind"), Wind, DeltaTime);
}

void UAGBAudioFXSubsystem::TickMusic(float DeltaTime)
{
	UWorld* World = GetWorld();
	const double Now = World->GetRealTimeSeconds();
	const APlayerController* Player = World->GetFirstPlayerController();
	const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(World);
	const bool bMenu = Player && Player->IsA<AAGBMenuPlayerController>();
	const FName Desired = bMenu ? FName(TEXT("Menu")) : (Now < DangerUntil ? FName(TEXT("Danger")) : (Environment && Environment->IsNight() ? FName(TEXT("Night")) : FName(TEXT("Day"))));
	if (MusicState.IsNone())
	{
		MusicState = Desired;
		NextTrackTime = Now + (bMenu ? 0.5 : 6.0); // A moment of quiet when the game starts.
	}

	const bool bPlaying = Music && Music->IsPlaying() && Now < TrackEndTime;
	if (Music && !bPlaying)
	{
		Music->FadeOut(3.f, 0.f);
		Music = nullptr;
		const UAGBAudioFXConfig* Config = GetConfig();
		const bool bContinuous = PlayingState == TEXT("Danger") || PlayingState == TEXT("Menu");
		NextTrackTime = Now + (bContinuous ? 1.0 : FMath::FRandRange(Config->MusicGapMin, FMath::Max(Config->MusicGapMin, Config->MusicGapMax)));
	}
	// Danger and the menu cut in; day and night wait for the current track to end.
	const bool bUrgent = Desired != PlayingState && (Desired == TEXT("Danger") || PlayingState == TEXT("Danger") || Desired == TEXT("Menu") || PlayingState == TEXT("Menu"));
	if (Music && bUrgent)
	{
		Music->FadeOut(Desired == TEXT("Danger") ? 1.f : 3.f, 0.f);
		Music = nullptr;
		NextTrackTime = Now + (Desired == TEXT("Danger") ? 0.5 : FMath::FRandRange(8.f, 20.f));
		PlayingState = NAME_None;
	}
	else if (!Music && Desired != MusicState && (Desired == TEXT("Danger") || MusicState == TEXT("Danger")))
	{
		NextTrackTime = FMath::Min(NextTrackTime, Now + (Desired == TEXT("Danger") ? 0.5 : 10.0));
	}
	MusicState = Desired;

	if (!Music && Now >= NextTrackTime)
	{
		const FName Cue(*(TEXT("Music.") + Desired.ToString()));
		float Volume = 1.f;
		USoundBase* Track = ResolveSound(Cue, Volume, nullptr, /*bMusic=*/true);
		Music = Track ? CreateAudio(Track, AGBAudioFX::FindSlot(Cue), false, 0.f, nullptr, FVector::ZeroVector) : nullptr;
		if (!Music)
		{
			NextTrackTime = Now + 10.0;
			return;
		}
		Music->bAutoDestroy = true;
		Music->FadeIn(Desired == TEXT("Danger") ? 1.5f : 4.f, Volume);
		const float Length = Track->GetDuration();
		TrackEndTime = Now + (Length <= 0.f || Length >= INDEFINITELY_LOOPING_DURATION * 0.5f ? 180.0 : Length);
		PlayingState = Desired;
	}
}

// ---------------------------------------------------------------- footsteps

UAGBFootstepComponent::UAGBFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAGBFootstepComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
	}
}

void UAGBFootstepComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || Movement->MovementMode == MOVE_None)
	{
		return;
	}
	const bool bFalling = Movement->IsFalling();
	if (bWasFalling && !bFalling && FallSpeed < -350.f)
	{
		PlayStep(true, FMath::Clamp(-FallSpeed / 900.f, 0.4f, 1.2f));
		Distance = 0.f;
	}
	bWasFalling = bFalling;
	if (bFalling)
	{
		FallSpeed = static_cast<float>(Movement->Velocity.Z);
		return;
	}
	FallSpeed = 0.f;
	if (!Movement->IsMovingOnGround())
	{
		return;
	}
	const float Speed = static_cast<float>(Movement->Velocity.Size2D());
	const bool bSprinting = Speed > Movement->MaxWalkSpeed * 1.1f;
	const float Stride = Movement->IsCrouching() ? WalkStride * 0.75f : (bSprinting ? SprintStride : WalkStride);
	if (Speed < 40.f)
	{
		Distance = Stride * 0.6f; // The first step comes quickly when starting to move.
		return;
	}
	Distance += Speed * DeltaTime;
	if (Distance >= Stride)
	{
		Distance = 0.f;
		PlayStep(false, Movement->IsCrouching() ? 0.4f : (bSprinting ? 1.f : 0.75f));
	}
}

void UAGBFootstepComponent::PlayStep(bool bLanding, float Loudness)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this);
	if (!Character || !Audio)
	{
		return;
	}
	const FVector Feet = Character->GetActorLocation() - FVector(0.0, 0.0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBFootstep), false, Character);
	Params.bReturnPhysicalMaterial = true;

	// Standing in water: the water surface is above the feet (water bodies answer Pawn-channel traces).
	TArray<FHitResult> WaterHits;
	GetWorld()->LineTraceMultiByChannel(WaterHits, Feet + FVector(0.0, 0.0, 120.0), Feet - FVector(0.0, 0.0, 10.0), ECC_Pawn, Params);
	const bool bInWater = WaterHits.ContainsByPredicate([&Feet](const FHitResult& Hit)
	{
		return Hit.GetComponent() && Hit.GetComponent()->GetCollisionProfileName() == WaterProfile && Hit.ImpactPoint.Z > Feet.Z + 5.0;
	});

	EAGBSurface Surface = EAGBSurface::Water;
	if (!bInWater)
	{
		FHitResult Ground;
		Surface = GetWorld()->LineTraceSingleByChannel(Ground, Feet + FVector(0.0, 0.0, 30.0), Feet - FVector(0.0, 0.0, 60.0), ECC_Visibility, Params)
			? Audio->GetSurface(Ground) : EAGBSurface::Default;
	}
	if (bLanding)
	{
		Audio->PlayCue(bInWater ? FName(TEXT("Event.Splash")) : FName(TEXT("Event.Land")), Feet, Volume * Loudness);
	}
	Audio->PlayCue(AGBAudioFX::CueName(TEXT("Footstep"), Surface), Feet, Volume * Loudness);
}

// ---------------------------------------------------------------- ambience zone and emitter

AAGBAmbienceZone::AAGBAmbienceZone()
{
	Area = CreateDefaultSubobject<USphereComponent>(TEXT("Area"));
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetHiddenInGame(true);
	Area->ShapeColor = FColor(80, 200, 120);
	RootComponent = Area;
}

void AAGBAmbienceZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Area->SetSphereRadius(RadiusMeters * 100.f);
}

float AAGBAmbienceZone::GetWeight(const FVector& Location) const
{
	const float Distance = static_cast<float>(FVector::Dist2D(Location, GetActorLocation()));
	return FMath::Clamp((RadiusMeters * 100.f - Distance) / FMath::Max(100.f, EdgeMeters * 100.f), 0.f, 1.f);
}

AAGBSoundEmitter::AAGBSoundEmitter()
{
	Area = CreateDefaultSubobject<USphereComponent>(TEXT("Area"));
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetHiddenInGame(true);
	Area->ShapeColor = FColor(80, 140, 255);
	RootComponent = Area;
}

void AAGBSoundEmitter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Area->SetSphereRadius(RadiusMeters * 100.f);
}

void AAGBSoundEmitter::BeginPlay()
{
	Super::BeginPlay();
	UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this);
	if (!Audio || !Audio->IsActive())
	{
		return;
	}
	if (UAudioComponent* Loop = Audio->StartLoop(FName(*(TEXT("Loop.") + Preset.ToString())), Area, RadiusMeters, Sound.LoadSynchronous()))
	{
		Loop->SetVolumeMultiplier(Loop->VolumeMultiplier * Volume);
	}
	Audio->SpawnEffect(FName(*(TEXT("Effect.") + Preset.ToString())), Area, GetActorLocation(), Effect.LoadSynchronous());
}
