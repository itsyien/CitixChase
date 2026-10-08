// Copyright Epic Games, Inc. All Rights Reserved.
// Player-centred street lighting.
//
// A city has thousands of lamps, so we never give every lamp a light. Instead a small
// pool of shadowless point lights is re-targeted at the nearest lamp heads each time
// the player moves far enough, and their intensity fades in with darkness. This is the
// standard way to make a night city actually read as lit.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixStreetLightSystem.generated.h"

class UPointLightComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixStreetLightSystem : public AActor
{
	GENERATED_BODY()

public:
	ACitixStreetLightSystem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Lamp head world positions, supplied by the city generator. */
	void SetLampLocations(const TArray<FVector>& InLampLocations);

	/**
	 * Emitter points inside lit buildings. These lights are shadowless, so a light
	 * inside a building spills outward and bathes the street and neighbours: the
	 * "city lit by its buildings" look, without thousands of lights.
	 */
	void SetBuildingEmitters(const TArray<FVector>& InLocations, const TArray<FLinearColor>& InColors);

	/** Number of pooled lights, for diagnostics. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	int32 GetLightCount() const { return Lights.Num(); }

	/** Total lamp positions known. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	int32 GetLampCount() const { return LampLocations.Num(); }

	/** Building emitter positions known. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	int32 GetBuildingEmitterCount() const { return BuildingEmitterLocations.Num(); }

	/** How many pool lights are currently lit. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	int32 GetActiveLightCount() const;

	// ---- Tuning -------------------------------------------------------

	/** Size of the dynamic light pool. Higher = more lit lamps, more cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "0"))
	int32 MaxLights = 26;

	/** Pool of lights placed inside lit buildings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "0"))
	int32 MaxBuildingLights = 16;

	/** Reach of a building light, cm. Larger than a lamp: it stands for a facade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "100"))
	float BuildingLightRadius = 7000.f;

	/** Peak intensity of a building light in candelas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "0"))
	float BuildingLightIntensity = 10000.f;

	/** Lamps further than this from the player are ignored, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "1000"))
	float MaxDistance = 26000.f;

	/** How far a lamp's light reaches, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "100"))
	float LightRadius = 1700.f;

	/** Peak intensity in candelas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "0"))
	float LightIntensity = 19000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights")
	FLinearColor LightColor = FLinearColor(1.0f, 0.82f, 0.58f);

	/** Seconds between re-assignments (kept low-frequency to stay cheap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "0.05"))
	float RefreshInterval = 0.25f;

	/** Re-assign early if the player has moved at least this far, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Street Lights", meta = (ClampMin = "0"))
	float RefreshMoveDistance = 2500.f;

protected:
	void EnsurePool();
	void RefreshAssignments();

	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> Lights;

	/** Second pool: lights inside lit buildings. */
	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> BuildingLights;

	TArray<FVector> LampLocations;
	TArray<FVector> BuildingEmitterLocations;
	TArray<FLinearColor> BuildingEmitterColors;
	FVector LastRefreshLocation = FVector(FLT_MAX);
	float RefreshTimer = 0.f;
	float CurrentNightAlpha = 0.f;
	bool bPoolReady = false;
};
