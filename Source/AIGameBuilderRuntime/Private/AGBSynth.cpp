#include "AGBSynth.h"

UAGBSynthSound::UAGBSynthSound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetSampleRate(Rate);
	NumChannels = 1;
	SampleByteSize = 2;
	bLooping = false;
}

UAGBSynthSound* UAGBSynthSound::Create(UObject* Outer, const FAGBSynthPatch& Patch)
{
	UAGBSynthSound* Sound = NewObject<UAGBSynthSound>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	Sound->Patch = Patch;
	Sound->Seed = FMath::Max(1u, static_cast<uint32>(FMath::Rand()) * 2654435761u + 1u);
	Sound->Duration = Patch.IsLoop() ? INDEFINITELY_LOOPING_DURATION : Patch.Duration;
	return Sound;
}

float UAGBSynthSound::Random()
{
	// xorshift: cheap white noise on the audio thread.
	Seed ^= Seed << 13;
	Seed ^= Seed >> 17;
	Seed ^= Seed << 5;
	return static_cast<float>(Seed) / 2147483648.f - 1.f;
}

int32 UAGBSynthSound::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	const int64 PatchSamples = Patch.IsLoop() ? MAX_int64 : static_cast<int64>(Patch.Duration * Rate);
	const int32 Count = static_cast<int32>(FMath::Min<int64>(NumSamples, PatchSamples - SampleIndex));
	if (Count <= 0)
	{
		return 0;
	}
	OutAudio.Reset();
	OutAudio.AddZeroed(Count * sizeof(int16));
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());

	constexpr float Dt = 1.f / Rate;
	constexpr float TwoPi = 2.f * UE_PI;
	for (int32 Index = 0; Index < Count; ++Index, ++SampleIndex)
	{
		const float T = SampleIndex * Dt;

		// Filtered noise: low-pass, then remove the slow part for high-pass.
		const float Noise = Random();
		Low += Patch.LowPass * (Noise - Low);
		Slow += 0.002f * (Low - Slow);
		float Sample = Patch.NoiseLevel * (Low - Patch.HighPass * Slow * 4.f);

		if (Patch.ToneLevel > 0.f)
		{
			const float Freq = FMath::Max(20.f, Patch.ToneHz + Patch.ToneSweep * T);
			Phase = FMath::Fmod(Phase + Freq * Dt, 1.f);
			Sample += Patch.ToneLevel * FMath::Sin(TwoPi * Phase);
		}
		if (Patch.ChordLevel > 0.f)
		{
			for (int32 Voice = 0; Voice < 3; ++Voice)
			{
				if (Patch.ChordHz[Voice] > 0.f)
				{
					ChordPhase[Voice] = FMath::Fmod(ChordPhase[Voice] + Patch.ChordHz[Voice] * Dt, 1.f);
					// Slight detune wobble keeps pads alive.
					Sample += Patch.ChordLevel * FMath::Sin(TwoPi * ChordPhase[Voice] + 0.3f * FMath::Sin(TwoPi * 0.2f * T + Voice));
				}
			}
		}
		if (Patch.CrackleRate > 0.f)
		{
			if ((Random() * 0.5f + 0.5f) < Patch.CrackleRate * Dt)
			{
				Crackle = Patch.CrackleLevel * (0.5f + 0.5f * FMath::Abs(Random()));
			}
			Sample += Crackle * Random();
			Crackle *= 0.985f;
		}
		if (Patch.ChirpRate > 0.f)
		{
			if (ChirpLeft <= 0 && (Random() * 0.5f + 0.5f) < Patch.ChirpRate * Dt)
			{
				ChirpLeft = FMath::Max(1, static_cast<int32>(Patch.ChirpLength * Rate));
				ChirpFreq = Patch.ChirpHz * (0.85f + 0.3f * (Random() * 0.5f + 0.5f));
			}
			if (ChirpLeft > 0)
			{
				const float Progress = 1.f - static_cast<float>(ChirpLeft) / FMath::Max(1.f, Patch.ChirpLength * Rate);
				ChirpPhase = FMath::Fmod(ChirpPhase + ChirpFreq * (1.f + 0.25f * Progress) * Dt, 1.f);
				Sample += Patch.ChirpLevel * FMath::Sin(TwoPi * ChirpPhase) * FMath::Sin(UE_PI * Progress);
				--ChirpLeft;
			}
		}

		// Envelope.
		float Envelope = Patch.Attack > 0.f ? FMath::Min(1.f, T / Patch.Attack) : 1.f;
		if (Patch.Decay > 0.f)
		{
			Envelope *= FMath::Exp(-T / Patch.Decay);
		}
		if (!Patch.IsLoop() && Patch.Release > 0.f)
		{
			Envelope *= FMath::Clamp((Patch.Duration - T) / Patch.Release, 0.f, 1.f);
		}
		if (Patch.ModDepth > 0.f)
		{
			Envelope *= 1.f - Patch.ModDepth * (0.5f + 0.5f * FMath::Sin(TwoPi * Patch.ModHz * T + 0.7f * FMath::Sin(TwoPi * Patch.ModHz * 0.37f * T)));
		}
		Out[Index] = static_cast<int16>(FMath::Clamp(Sample * Envelope * Patch.Gain, -1.f, 1.f) * 32000.f);
	}
	return Count;
}

namespace AGBSynth
{
	namespace
	{
		FAGBSynthPatch Shot(float Duration, float Decay, float LowPass, float Gain)
		{
			FAGBSynthPatch P;
			P.Duration = Duration;
			P.Decay = Decay;
			P.LowPass = LowPass;
			P.Gain = Gain;
			return P;
		}

		FAGBSynthPatch Tone(float Duration, float Decay, float Hz, float Sweep, float Gain, float Noise = 0.f)
		{
			FAGBSynthPatch P;
			P.Duration = Duration;
			P.Decay = Decay;
			P.ToneHz = Hz;
			P.ToneSweep = Sweep;
			P.ToneLevel = 1.f;
			P.NoiseLevel = Noise;
			P.LowPass = 0.3f;
			P.Gain = Gain;
			return P;
		}

		FAGBSynthPatch Loop(float LowPass, float Gain, float ModHz = 0.f, float ModDepth = 0.f)
		{
			FAGBSynthPatch P;
			P.LowPass = LowPass;
			P.Gain = Gain;
			P.ModHz = ModHz;
			P.ModDepth = ModDepth;
			P.Attack = 1.f;
			return P;
		}

		FAGBSynthPatch Pad(float A, float B, float C, float Gain)
		{
			FAGBSynthPatch P;
			P.Duration = 50.f;
			P.Attack = 5.f;
			P.Release = 6.f;
			P.NoiseLevel = 0.f;
			P.ChordHz[0] = A;
			P.ChordHz[1] = B;
			P.ChordHz[2] = C;
			P.ChordLevel = 0.33f;
			P.Gain = Gain;
			P.ModHz = 0.08f;
			P.ModDepth = 0.25f;
			return P;
		}

		FAGBSynthPatch WithChirps(FAGBSynthPatch P, float Rate, float Hz, float Length, float Level)
		{
			P.ChirpRate = Rate;
			P.ChirpHz = Hz;
			P.ChirpLength = Length;
			P.ChirpLevel = Level;
			return P;
		}

		FAGBSynthPatch WithCrackle(FAGBSynthPatch P, float Rate, float Level)
		{
			P.CrackleRate = Rate;
			P.CrackleLevel = Level;
			return P;
		}

		FAGBSynthPatch WithTone(FAGBSynthPatch P, float Hz, float Level, float Sweep = 0.f)
		{
			P.ToneHz = Hz;
			P.ToneLevel = Level;
			P.ToneSweep = Sweep;
			return P;
		}

		FAGBSynthPatch WithHighPass(FAGBSynthPatch P, float HighPass)
		{
			P.HighPass = HighPass;
			return P;
		}

		FAGBSynthPatch WithAttack(FAGBSynthPatch P, float Attack)
		{
			P.Attack = Attack;
			return P;
		}

		const TMap<FName, FAGBSynthPatch>& Patches()
		{
			static const TMap<FName, FAGBSynthPatch> Map = {
				// Footsteps: soft for earth and plants, bright for stone, hollow for wood.
				{ TEXT("Footstep.Default"), Shot(0.12f, 0.035f, 0.25f, 0.5f) },
				{ TEXT("Footstep.Dirt"), Shot(0.12f, 0.035f, 0.22f, 0.5f) },
				{ TEXT("Footstep.Grass"), WithHighPass(Shot(0.15f, 0.05f, 0.45f, 0.3f), 0.05f) },
				{ TEXT("Footstep.Rock"), WithTone(Shot(0.08f, 0.015f, 0.7f, 0.45f), 900.f, 0.12f) },
				{ TEXT("Footstep.Stone"), WithTone(Shot(0.08f, 0.015f, 0.65f, 0.45f), 700.f, 0.15f) },
				{ TEXT("Footstep.Wood"), WithTone(Shot(0.12f, 0.03f, 0.2f, 0.55f), 180.f, 0.5f) },
				{ TEXT("Footstep.Sand"), WithHighPass(Shot(0.16f, 0.06f, 0.6f, 0.22f), 0.1f) },
				{ TEXT("Footstep.Snow"), WithCrackle(Shot(0.18f, 0.07f, 0.5f, 0.3f), 300.f, 0.4f) },
				{ TEXT("Footstep.Gravel"), WithCrackle(Shot(0.14f, 0.05f, 0.6f, 0.35f), 400.f, 0.5f) },
				{ TEXT("Footstep.Mud"), Shot(0.18f, 0.06f, 0.12f, 0.55f) },
				{ TEXT("Footstep.Metal"), WithTone(Shot(0.3f, 0.08f, 0.8f, 0.3f), 1400.f, 0.5f) },
				{ TEXT("Footstep.Water"), WithCrackle(WithHighPass(Shot(0.3f, 0.1f, 0.5f, 0.35f), 0.05f), 120.f, 0.3f) },
				// Impacts.
				{ TEXT("Impact.Default"), Shot(0.2f, 0.04f, 0.4f, 0.6f) },
				{ TEXT("Impact.Wood"), WithTone(Shot(0.25f, 0.06f, 0.3f, 0.7f), 140.f, 0.6f) },
				{ TEXT("Impact.Stone"), WithTone(Shot(0.15f, 0.03f, 0.8f, 0.6f), 1200.f, 0.3f) },
				{ TEXT("Impact.Flesh"), WithTone(Shot(0.18f, 0.05f, 0.15f, 0.7f), 90.f, 0.5f) },
				{ TEXT("Impact.Plant"), WithHighPass(Shot(0.25f, 0.08f, 0.5f, 0.45f), 0.05f) },
				{ TEXT("Impact.Metal"), WithTone(Shot(0.6f, 0.25f, 0.8f, 0.4f), 1800.f, 0.6f) },
				// Player and world events.
				{ TEXT("Event.Swing"), WithAttack(WithHighPass(Shot(0.25f, 0.1f, 0.35f, 0.25f), 0.05f), 0.08f) },
				{ TEXT("Event.Hurt"), Tone(0.2f, 0.08f, 220.f, -300.f, 0.5f, 0.3f) },
				{ TEXT("Event.Death"), Tone(0.8f, 0.35f, 160.f, -120.f, 0.5f) },
				{ TEXT("Event.Eat"), WithCrackle(Shot(0.25f, 0.1f, 0.5f, 0.35f), 200.f, 0.6f) },
				{ TEXT("Event.Drink"), Tone(0.35f, 0.15f, 400.f, 600.f, 0.35f, 0.2f) },
				{ TEXT("Event.Pickup"), Tone(0.12f, 0.05f, 880.f, 900.f, 0.3f) },
				{ TEXT("Event.Craft"), Tone(0.35f, 0.15f, 660.f, 0.f, 0.3f) },
				{ TEXT("Event.Place"), WithTone(Shot(0.25f, 0.07f, 0.2f, 0.6f), 110.f, 0.6f) },
				{ TEXT("Event.Shoot"), Tone(0.2f, 0.08f, 300.f, -200.f, 0.45f, 0.3f) },
				{ TEXT("Event.Land"), WithTone(Shot(0.2f, 0.05f, 0.15f, 0.6f), 80.f, 0.5f) },
				{ TEXT("Event.LevelUp"), [] { FAGBSynthPatch P = Pad(523.3f, 659.3f, 784.f, 0.25f); P.Duration = 1.4f; P.Attack = 0.02f; P.Release = 0.8f; P.ModDepth = 0.f; return P; }() },
				{ TEXT("Event.Splash"), WithCrackle(WithHighPass(Shot(0.5f, 0.15f, 0.5f, 0.45f), 0.05f), 150.f, 0.4f) },
				// Interface.
				{ TEXT("UI.Click"), Tone(0.04f, 0.012f, 1500.f, 0.f, 0.25f) },
				{ TEXT("UI.Hover"), Tone(0.02f, 0.006f, 2200.f, 0.f, 0.08f) },
				{ TEXT("UI.Open"), Tone(0.12f, 0.06f, 600.f, 800.f, 0.2f) },
				{ TEXT("UI.Close"), Tone(0.12f, 0.06f, 900.f, -800.f, 0.2f) },
				{ TEXT("UI.Error"), Tone(0.2f, 0.12f, 180.f, 0.f, 0.3f) },
				// Creatures.
				{ TEXT("Creature.Hurt"), Tone(0.3f, 0.1f, 300.f, -200.f, 0.5f, 0.4f) },
				{ TEXT("Creature.Attack"), [] { FAGBSynthPatch P = Tone(0.5f, 0.25f, 90.f, 0.f, 0.55f, 0.5f); P.Attack = 0.05f; P.ModHz = 30.f; P.ModDepth = 0.6f; P.LowPass = 0.2f; return P; }() },
				{ TEXT("Creature.Death"), Tone(0.9f, 0.4f, 250.f, -150.f, 0.5f, 0.2f) },
				{ TEXT("Creature.Idle"), Tone(0.35f, 0.15f, 400.f, -100.f, 0.3f, 0.1f) },
				// Ambience (loops).
				{ TEXT("Ambience.Day"), WithChirps(Loop(0.02f, 0.3f, 0.1f, 0.5f), 0.6f, 3200.f, 0.12f, 0.06f) },
				{ TEXT("Ambience.Night"), WithChirps(Loop(0.015f, 0.22f, 0.08f, 0.5f), 3.f, 4500.f, 0.05f, 0.03f) },
				{ TEXT("Ambience.Rain"), WithCrackle(WithHighPass(Loop(0.5f, 0.3f), 0.1f), 400.f, 0.2f) },
				{ TEXT("Ambience.Wind"), Loop(0.03f, 0.5f, 0.07f, 0.7f) },
				{ TEXT("Loop.Fire"), WithCrackle(Loop(0.08f, 0.15f), 25.f, 0.7f) },
				{ TEXT("Loop.Water"), WithHighPass(Loop(0.12f, 0.35f, 0.3f, 0.4f), 0.01f) },
				{ TEXT("Loop.River"), Loop(0.2f, 0.4f, 0.5f, 0.2f) },
				{ TEXT("Loop.Waterfall"), Loop(0.35f, 0.6f) },
				{ TEXT("Loop.Insects"), WithChirps(Loop(0.01f, 0.05f), 6.f, 5000.f, 0.04f, 0.05f) },
				{ TEXT("Loop.Wind"), Loop(0.03f, 0.5f, 0.07f, 0.7f) },
				{ TEXT("Loop.Birds"), WithChirps(Loop(0.01f, 0.03f), 1.5f, 3000.f, 0.15f, 0.08f) },
				// Music (placeholder chords).
				{ TEXT("Music.Day"), Pad(261.6f, 329.6f, 392.f, 0.12f) },
				{ TEXT("Music.Night"), Pad(220.f, 261.6f, 329.6f, 0.1f) },
				{ TEXT("Music.Danger"), [] { FAGBSynthPatch P = Pad(146.8f, 174.6f, 220.f, 0.16f); P.Attack = 0.5f; P.ModHz = 2.f; P.ModDepth = 0.6f; P.Duration = 30.f; return P; }() },
				{ TEXT("Music.Menu"), Pad(174.6f, 220.f, 261.6f, 0.12f) },
			};
			return Map;
		}
	}

	bool GetPatch(FName Cue, FAGBSynthPatch& OutPatch)
	{
		if (const FAGBSynthPatch* Found = Patches().Find(Cue))
		{
			OutPatch = *Found;
			return true;
		}
		return false;
	}

	bool GetZonePatch(FName Preset, bool bNight, FAGBSynthPatch& OutPatch)
	{
		const FString Name = Preset.ToString();
		if (Name == TEXT("Forest"))
		{
			OutPatch = bNight ? WithChirps(Loop(0.015f, 0.2f, 0.08f, 0.5f), 4.f, 4200.f, 0.05f, 0.04f)
				: WithChirps(Loop(0.02f, 0.25f, 0.1f, 0.5f), 1.5f, 3000.f, 0.14f, 0.08f);
		}
		else if (Name == TEXT("Desert"))
		{
			OutPatch = bNight ? WithChirps(Loop(0.02f, 0.25f, 0.05f, 0.6f), 0.8f, 4800.f, 0.04f, 0.02f) : Loop(0.04f, 0.45f, 0.06f, 0.8f);
		}
		else if (Name == TEXT("Beach"))
		{
			OutPatch = Loop(0.15f, 0.45f, 0.08f, 0.8f);
		}
		else if (Name == TEXT("Snow"))
		{
			OutPatch = WithTone(Loop(0.03f, 0.5f, 0.05f, 0.9f), 320.f, 0.02f);
		}
		else if (Name == TEXT("Cave"))
		{
			OutPatch = WithChirps(Loop(0.008f, 0.35f), 0.3f, 1200.f, 0.03f, 0.1f);
		}
		else if (Name == TEXT("Swamp"))
		{
			OutPatch = bNight ? WithChirps(Loop(0.02f, 0.15f), 2.5f, 280.f, 0.15f, 0.12f) : WithChirps(Loop(0.02f, 0.2f, 0.1f, 0.4f), 1.f, 350.f, 0.12f, 0.08f);
		}
		else if (Name == TEXT("Plains"))
		{
			return GetPatch(bNight ? FName(TEXT("Ambience.Night")) : FName(TEXT("Ambience.Day")), OutPatch);
		}
		else
		{
			return false;
		}
		return true;
	}
}
