// Copyright Epic Games, Inc. All Rights Reserved.
// Weather: rain that is cheap to render and actually changes the look of the city.
//
// Design notes
//  * The streaks are ONE instanced static mesh component (a single draw call), so
//    rain costs almost nothing on the render thread compared with particle systems.
//  * The volume follows the player and wraps around them, so a fixed pool of ~200
//    streaks covers an unlimited area while driving.
//  * Rain does not just spawn particles: it drives road wetness, fog density, ambient
//    light and emissive haze through the time-of-day actor, which is what makes the
//    city genuinely read as rainy rather than "sparkles in front of the camera".

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixWeatherSystem.generated.h"

class UInstancedStaticMeshComponent;
class ACitixTimeOfDay;

UENUM()
enum class ECitixWeatherState : uint8
{
	Clear,
	Starting,
	Raining,
	Stopping
};

UCLASS(NotBlueprintable)
class CITIX_API ACitixWeatherSystem : public AActor
{
	GENERATED_BODY()

public:
	ACitixWeatherSystem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 0 = dry, 1 = heavy downpour. */
	UFUNCTION(BlueprintPure, Category = "Citix|Weather")
	float GetRainIntensity() const { return CurrentIntensity; }

	UFUNCTION(BlueprintPure, Category = "Citix|Weather")
	bool IsRaining() const { return State == ECitixWeatherState::Starting || State == ECitixWeatherState::Raining; }

	/** Turn the automatic weather cycle on or off. Off ramps the rain down. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Weather")
	void SetRainEnabled(bool bEnabled);

	/** Immediately force a given rain strength (used by tests / the console). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Weather")
	void ForceRain(float Intensity);

	UFUNCTION(BlueprintPure, Category = "Citix|Weather")
	bool IsRainEnabled() const { return bRainEnabled; }

	/** Find the weather system in a world, if any. */
	static ACitixWeatherSystem* Find(UWorld* World);

	// ---- Tuning --------------------------------------------------------

	/** Automatic weather changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather")
	bool bRainEnabled = true;

	/** Streak instances in the pool (all in one draw call). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "16"))
	int32 MaxStreaks = 220;

	/** Half-extent of the rain volume that follows the player, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "1000"))
	float RainAreaCm = 4500.f;

	/** Vertical extent of the rain volume, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "500"))
	float RainHeightCm = 6000.f;

	/** Fall speed of a streak, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather")
	float FallSpeed = 4200.f;

	/** Lateral drift of the rain, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather")
	FVector Wind = FVector(180.f, 60.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "1.0"))
	float MinClearSeconds = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "1.0"))
	float MaxClearSeconds = 280.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "1.0"))
	float MinRainSeconds = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "1.0"))
	float MaxRainSeconds = 200.f;

	/** Seconds to fade the rain in or out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather", meta = (ClampMin = "0.5"))
	float RampSeconds = 9.f;

	/** Rain strength range for a natural shower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather")
	float MinIntensity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather")
	float MaxIntensity = 1.0f;

	/** Streak length, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weather")
	float StreakLengthCm = 55.f;

protected:
	void UpdateState(float DeltaSeconds);
	void UpdateStreaks(float DeltaSeconds);
	void ApplyIntensityToWorld();
	void PlaceStreak(int32 Index, const FVector& Centre, bool bAtTop);

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RainStreaks;

	UPROPERTY()
	TObjectPtr<ACitixTimeOfDay> TimeOfDay;

	TArray<FVector> StreakPositions;
	TArray<FTransform> StreakTransforms;

	ECitixWeatherState State = ECitixWeatherState::Clear;
	float CurrentIntensity = 0.f;
	float TargetIntensity = 0.f;
	float StateTimer = 120.f;

	FRandomStream Rng;
};
