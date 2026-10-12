#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"

#include "AGBSynth.generated.h"

/**
 * Recipe for a placeholder sound made from noise and tones (no audio files needed). One-shots fade out after
 * Duration; loops (Duration 0) play forever.
 */
struct AIGAMEBUILDERRUNTIME_API FAGBSynthPatch
{
	float Duration = 0.f;
	float Attack = 0.003f;
	/** Exponential decay time (s); 0 = sustain. */
	float Decay = 0.f;
	/** Fade at the end of a one-shot (s). */
	float Release = 0.f;
	float Gain = 0.4f;

	float NoiseLevel = 1.f;
	/** One-pole low-pass amount 0..1 (higher = brighter). */
	float LowPass = 0.3f;
	/** High-pass amount 0..1 (removes rumble). */
	float HighPass = 0.f;

	float ToneHz = 0.f;
	float ToneLevel = 0.f;
	/** Pitch change per second (Hz/s). */
	float ToneSweep = 0.f;
	/** Up to three extra tones (pads, chords). */
	float ChordHz[3] = { 0.f, 0.f, 0.f };
	float ChordLevel = 0.f;

	/** Random clicks per second (fire crackle, snow crunch, rain). */
	float CrackleRate = 0.f;
	float CrackleLevel = 0.f;

	/** Slow volume wobble (wind gusts, waves). */
	float ModHz = 0.f;
	float ModDepth = 0.f;

	/** Random short tones per second (birds, crickets, frogs, drips). */
	float ChirpRate = 0.f;
	float ChirpHz = 3000.f;
	float ChirpLength = 0.08f;
	float ChirpLevel = 0.f;

	bool IsLoop() const { return Duration <= 0.f; }
};

/** A playable placeholder sound generated on the audio thread from an FAGBSynthPatch. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBSynthSound : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UAGBSynthSound(const FObjectInitializer& ObjectInitializer);

	/** A new sound playing Patch (one instance per playback). */
	static UAGBSynthSound* Create(UObject* Outer, const FAGBSynthPatch& Patch);

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

	static constexpr int32 Rate = 24000;

private:
	FAGBSynthPatch Patch;
	uint32 Seed = 1;
	int64 SampleIndex = 0;
	float Low = 0.f;
	float Slow = 0.f;
	float Phase = 0.f;
	float ChordPhase[3] = { 0.f, 0.f, 0.f };
	float Crackle = 0.f;
	float ChirpPhase = 0.f;
	int32 ChirpLeft = 0;
	float ChirpFreq = 0.f;

	float Random();
};

/** Placeholder patches by cue name ("Footstep.Grass", "Ambience.Night"...) and ambience zone preset. */
namespace AGBSynth
{
	AIGAMEBUILDERRUNTIME_API bool GetPatch(FName Cue, FAGBSynthPatch& OutPatch);
	AIGAMEBUILDERRUNTIME_API bool GetZonePatch(FName Preset, bool bNight, FAGBSynthPatch& OutPatch);
}
