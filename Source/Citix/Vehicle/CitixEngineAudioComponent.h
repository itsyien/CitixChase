// Copyright Epic Games, Inc. All Rights Reserved.
// Procedural engine audio.
//
// The project has no audio assets at all, so rather than ship silence this synthesises
// the engine note in real time with a USynthComponent: a stack of harmonics whose
// fundamental follows a faked gearbox, plus intake noise under throttle. A boost adds a
// brighter layer. It is deliberately simple — a placeholder that gives the car a voice
// until real engine samples exist, with the same call sites a real system would use.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "CitixEngineAudioComponent.generated.h"

UCLASS(ClassGroup = (Citix), meta = (BlueprintSpawnableComponent))
class CITIX_API UCitixEngineAudioComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UCitixEngineAudioComponent(const FObjectInitializer& ObjectInitializer);

	/** Push the current driving state in; the component smooths it internally. */
	void SetEngineState(float SpeedKmh, float Throttle, bool bBoosting, bool bGrounded);

	/** Idle and redline, used for the faked gearbox. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Audio")
	float IdleRpm = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Audio")
	float RedlineRpm = 6800.f;

	/** km/h covered by one gear, used to fake the shift points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Audio")
	float KmhPerGear = 38.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Audio")
	float MaxGears = 6.f;

	/** Overall output level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Audio")
	float OutputGain = 0.32f;

	/** Drop the note while airborne so landings read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Audio")
	float AirborneGain = 0.45f;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	float TargetRpm = 800.f;
	float CurrentRpm = 800.f;
	float TargetThrottle = 0.f;
	float CurrentThrottle = 0.f;
	float TargetGain = 0.f;
	float CurrentGain = 0.f;
	/** 0..1 blend of the bright boost layer. */
	float TargetBoost = 0.f;
	float CurrentBoost = 0.f;

	/** Crank phase in revolutions (0..1), advanced per sample. */
	float Phase = 0.f;
	/** Sample rate handed to us by Init; there is no public getter on USynthComponent. */
	int32 CachedSampleRate = 48000;
	FRandomStream Noise;
};
