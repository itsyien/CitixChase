// Copyright Epic Games, Inc. All Rights Reserved.
// Time of day: drives a sun and moon directional light, the sky light, height fog
// and the emissive strength of windows/lamps/lights, so the same city reads as
// midday, golden hour or a lit night skyline.
//
// This is the first piece of the WorldSystem called for in the project goal.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixTimeOfDay.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;

UCLASS(Blueprintable)
class CITIX_API ACitixTimeOfDay : public AActor
{
	GENERATED_BODY()

public:
	ACitixTimeOfDay();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Set the hour of day (wrapped into 0..24). */
	UFUNCTION(BlueprintCallable, Category = "Citix|World")
	void SetHours(float InHours);

	UFUNCTION(BlueprintPure, Category = "Citix|World")
	float GetHours() const { return Hours; }

	UFUNCTION(BlueprintCallable, Category = "Citix|World")
	void AddHours(float DeltaHours) { SetHours(Hours + DeltaHours); }

	/** Pause or resume the automatic day/night cycle. */
	UFUNCTION(BlueprintCallable, Category = "Citix|World")
	void SetTimePaused(bool bInPaused) { bPaused = bInPaused; }

	UFUNCTION(BlueprintPure, Category = "Citix|World")
	bool IsTimePaused() const { return bPaused; }

	/** "HH:MM" for HUD display. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	FString GetTimeString() const;

	UFUNCTION(BlueprintPure, Category = "Citix|World")
	float GetSunElevationDegrees() const { return SunElevation; }

	/** True when the sun is below the horizon. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	bool IsNight() const { return SunElevation < -3.f; }

	/** 0 in full daylight, 1 in full night. Used to fade artificial lighting in. */
	UFUNCTION(BlueprintPure, Category = "Citix|World")
	float GetNightAlpha() const { return FMath::Clamp((-SunElevation + 3.f) / 12.f, 0.f, 1.f); }

	/**
	 * Rain strength, 0 = dry, 1 = heavy. Driven by the weather system; folded into
	 * road wetness, fog thickness, ambient light and emissive haze so a downpour
	 * reads as a different night rather than a particle effect pasted on top.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citix|World")
	void SetRainIntensity(float InIntensity);

	UFUNCTION(BlueprintPure, Category = "Citix|World")
	float GetRainIntensity() const { return RainIntensity; }

	/** Find the time of day actor in a world, if any. */
	static ACitixTimeOfDay* Find(UWorld* World);

	/** Hour at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	/** Start at dusk, not morning: the city's night lighting is the whole point of the look,
	 *  so pressing Play should show it immediately rather than twelve minutes later. */
	float StartHours = 19.f;

	/** Real-world minutes for a full 24 hour cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.25"))
	float DayLengthMinutes = 3.f;

	/**
	 * How much slower the clock runs between dusk and dawn, so night lasts longer
	 * than day. 2 = night takes twice as long as the equivalent daytime hours.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "1.0"))
	float NightDurationScale = 2.2f;

	/** Blend a photographic sky dome over SkyAtmosphere after dusk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bUseNightSkyDome = true;

	/** Radius of the night sky dome, cm. Must exceed the city extent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "100000"))
	float NightSkyRadius = 260000.f;

	/** Wet streets at night: lower roughness so roads catch light and reflect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bWetStreetsAtNight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bAutoAdvance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bPaused = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float SunPeakIntensity = 6.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float MoonIntensity = 0.45f;

	/** Emissive multiplier for windows/lamps at night and in daylight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float NightEmissiveBoost = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float DayEmissiveBoost = 0.55f;

	/** Exposure compensation (EV) applied at night and during the day. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float NightExposureBias = -1.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float DayExposureBias = 0.f;

	/** Bloom strength: gentle at night. Large brightness with large bloom is what
	 *  washes out the massing; the windows now carry the detail instead of the glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0"))
	float NightBloomIntensity = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0"))
	float DayBloomIntensity = 0.7f;

	/**
	 * Night ambient standing in for the collective glow of a lit city. Sky light is
	 * cheap and does not spike the exposure histogram the way extra point lights do,
	 * which is what makes the whole city read as "bathed in light" after dark.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0"))
	float NightSkyLightIntensity = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0"))
	float DaySkyLightIntensity = 1.0f;

	/** Exposure multiplier after dusk; keeps the skyline lights as the focal point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0"))
	float NightSkyExposure = 0.40f;

	/** Blue-violet grade applied to the photographed night sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	FLinearColor NightSkyTint = FLinearColor(0.42f, 0.52f, 0.78f);

	/** Horizontal alignment of the HDRI in full panorama turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NightSkyRotation = 0.75f;

	/** Warm city-light glow where the cloud sky meets the skyline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	FLinearColor NightHorizonGlow = FLinearColor(0.110f, 0.050f, 0.032f);

	/** Warm tint of the night ambient (city light pollution bouncing off the haze). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	FLinearColor NightSkyLightColor = FLinearColor(1.0f, 0.84f, 0.68f);

protected:
	/** Find (or create) the sun, moon, sky light and fog we drive. */
	void ResolveSceneActors();

	/** Build the night dome (engine sky sphere + authored HDR material). */
	void CreateNightSkyDome();

	/** Position the lights and push the resulting look into materials. */
	void UpdateSky();

	UPROPERTY()
	TObjectPtr<ADirectionalLight> SunLight;

	UPROPERTY()
	TObjectPtr<ADirectionalLight> MoonLight;

	UPROPERTY()
	TObjectPtr<ASkyLight> SkyLight;

	UPROPERTY()
	TObjectPtr<AExponentialHeightFog> HeightFog;

	UPROPERTY()
	TObjectPtr<class APostProcessVolume> PostProcessVolume;

	/** Star dome that replaces the sky after dark. */
	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> NightSkyDome;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> NightSkyMaterial;

private:
	UPROPERTY(ReplicatedUsing=OnRep_TimeState)
	float Hours = 19.f;
	float SunElevation = 20.f;
	UPROPERTY(ReplicatedUsing=OnRep_TimeState)
	float RainIntensity = 0.f;

	UFUNCTION()
	void OnRep_TimeState() { UpdateSky(); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;	bool bResolved = false;
};
