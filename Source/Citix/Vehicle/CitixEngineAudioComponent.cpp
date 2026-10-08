// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CitixEngineAudioComponent.h"

UCitixEngineAudioComponent::UCitixEngineAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	NumChannels = 1;
	Noise.Initialize(4711);
}

bool UCitixEngineAudioComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	CachedSampleRate = FMath::Max(1, SampleRate);
	// The audio thread can call OnGenerateAudio before the first game tick: start silent.
	CurrentGain = 0.f;
	TargetGain = 0.f;
	return true;
}

void UCitixEngineAudioComponent::SetEngineState(float SpeedKmh, float Throttle, bool bBoosting, bool bGrounded)
{
	const float Speed = FMath::Max(0.f, SpeedKmh);
	const float ThrottleClamped = FMath::Clamp(Throttle, 0.f, 1.f);

	// Faked gearbox: rpm climbs within a gear and drops at each shift, which is what makes
	// the note rise and fall instead of just tracking road speed.
	const float GearSpan = FMath::Max(5.f, KmhPerGear);
	const int32 Gear = FMath::Clamp(FMath::FloorToInt(Speed / GearSpan), 0, FMath::Max(0, FMath::RoundToInt(MaxGears) - 1));
	const float InGear = FMath::Clamp((Speed - Gear * GearSpan) / GearSpan, 0.f, 1.f);
	const float Rpm = IdleRpm
		+ InGear * (RedlineRpm - IdleRpm) * 0.82f
		+ ThrottleClamped * (RedlineRpm - IdleRpm) * 0.10f;

	TargetRpm = FMath::Clamp(Rpm, IdleRpm, RedlineRpm);
	TargetThrottle = ThrottleClamped;
	TargetBoost = bBoosting ? 1.f : 0.f;

	// Quiet at a standstill, loud under load, muffled in the air.
	const float LoadGain = 0.30f + 0.70f * ThrottleClamped + 0.35f * FMath::Clamp(Speed / 140.f, 0.f, 1.f);
	TargetGain = LoadGain * (bGrounded ? 1.f : AirborneGain);
}

int32 UCitixEngineAudioComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const int32 SampleRate = CachedSampleRate;
	if (SampleRate <= 0 || NumSamples <= 0)
	{
		return 0;
	}

	// Smooth the control values once per block: a block is a few milliseconds, so this is
	// fast enough to avoid zipper noise without per-sample filtering.
	const float BlockSeconds = static_cast<float>(NumSamples) / static_cast<float>(SampleRate);
	CurrentRpm = FMath::FInterpTo(CurrentRpm, TargetRpm, BlockSeconds, 7.f);
	CurrentThrottle = FMath::FInterpTo(CurrentThrottle, TargetThrottle, BlockSeconds, 12.f);
	CurrentGain = FMath::FInterpTo(CurrentGain, TargetGain, BlockSeconds, 9.f);
	CurrentBoost = FMath::FInterpTo(CurrentBoost, TargetBoost, BlockSeconds, 4.f);

	// A four-stroke four-cylinder fires twice per crank revolution.
	const float FiringHz = FMath::Max(24.f, CurrentRpm / 60.f * 2.f);
	const float PhaseIncrement = FiringHz / static_cast<float>(SampleRate);
	const float Gain = OutputGain * CurrentGain;
	const float NoiseAmount = 0.06f + 0.16f * CurrentThrottle;

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		Phase += PhaseIncrement;
		if (Phase >= 1.f)
		{
			Phase -= 1.f;
		}

		// Harmonic stack: a falling-amplitude series gives a raspy combustion tone rather
		// than a pure sine. The boost layer adds a brighter, higher partial.
		float Sample = 0.f;
		float Amplitude = 1.f;
		for (int32 Harmonic = 1; Harmonic <= 7; ++Harmonic)
		{
			Sample += Amplitude * FMath::Sin(2.f * PI * Phase * Harmonic);
			Amplitude *= 0.54f;
		}
		Sample *= 0.42f;

		// Intake / mechanical noise.
		Sample += (Noise.FRand() * 2.f - 1.f) * NoiseAmount;

		if (CurrentBoost > 0.001f)
		{
			// Boost: a fifth above, brighter and slightly detuned.
			Sample += CurrentBoost * 0.35f * FMath::Sin(2.f * PI * Phase * 2.98f);
		}

		OutAudio[Index] = FMath::Clamp(Sample * Gain, -1.f, 1.f);
	}

	return NumSamples;
}
