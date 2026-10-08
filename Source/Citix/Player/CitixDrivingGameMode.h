// Copyright Epic Games, Inc. All Rights Reserved.
// Driving game mode. It also bootstraps a playable world: if the current map has
// no city, lighting or spawn point, it creates them. That keeps the project
// playable from any map, including a brand new empty level, with no manual setup.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CitixDrivingGameMode.generated.h"

class ACitixCityGenerator;
class ACitixTimeOfDay;
class ACitixSandboxDirector;
class ACitixWeatherSystem;
class ACitixPedestrian;
class ACameraActor;
class APlayerStart;

UCLASS(Blueprintable)
class CITIX_API ACitixDrivingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACitixDrivingGameMode();

	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
	virtual void Logout(AController* Exiting) override;

	/** Headless PvP loop test tick (`-CitixPvpTest` on the server). */
	void PvpTestTick();

	/** Isolation test tick (`-CitixIsoTest` on the server). */
	void IsoTestTick();

	/** Race loop test tick (`-CitixRaceTest` on the server). */
	void RaceTestTick();

	/** Weapon test phase tick (readiness-gated). */
	void WeaponTestTick();

	/** Isolation test phase counter. */
	int32 IsoPhase = 0;

	/** Logout cleanup helper (pursuit + personal record). */
	void ReleasePoliceUnitsForLeaver(AController* Exiting);

	/** Whether the staged ram has been placed this run. */
	bool bPvpRamStaged = false;

	UFUNCTION(BlueprintPure, Category = "Citix|World")
	ACitixCityGenerator* GetCityGenerator() const { return CityGenerator; }

	UFUNCTION(BlueprintPure, Category = "Citix|World")
	ACitixTimeOfDay* GetTimeOfDay() const { return TimeOfDay; }

	/** Discovery, delivery jobs, stunts and the navigation waypoint. */
	UFUNCTION(BlueprintPure, Category = "Citix|Sandbox")
	ACitixSandboxDirector* GetSandbox() const { return Sandbox; }

	/** Rain and the weather cycle. */
	UFUNCTION(BlueprintPure, Category = "Citix|Weather")
	ACitixWeatherSystem* GetWeather() const { return Weather; }

protected:
	/** Idempotent: city, lighting and player start all ensured. */
	void EnsureWorldReady();

	/** Spawn an overview camera (used by -CitixScreenshot). */
	void SetupOverviewCamera();

	/** Spawn a close three-quarter camera aimed at the player car (wheel inspection). */
	void SetupCarInspectCamera();

	/** Point the camera at the nearest knocked-down pedestrian (test framing). */
	void SetupRagdollCamera();

	/** Look along the river, to inspect the water (test framing). */
	void SetupWaterCamera();

	/** View the Lujiazui hero cluster from the opposite bank (test framing). */
	void SetupSkylineCamera();

	/** Test framing: lock onto one knocked-down body and follow it through the get-up. */
	void BeginRagdollCamera();
	void UpdateRagdollCamera();

	void CaptureScreenshotAndExit();

	ACitixCityGenerator* EnsureCityGenerator();
	void EnsureLighting();
	void EnsureTimeOfDay();
	void EnsureSandbox();
	void EnsureWeather();
	void EnsurePlayerStart();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bAutoCreateCity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bAutoCreateLighting = true;

	/** Create and drive a day/night cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bAutoCreateTimeOfDay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	TSubclassOf<ACitixTimeOfDay> TimeOfDayClass;

	/** Height the vehicle is dropped from, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	float SpawnHeight = 140.f;

	UPROPERTY()
	TObjectPtr<ACitixCityGenerator> CityGenerator;

	UPROPERTY()
	TObjectPtr<ACitixTimeOfDay> TimeOfDay;

	UPROPERTY()
	TObjectPtr<ACitixSandboxDirector> Sandbox;

	UPROPERTY()
	TObjectPtr<ACitixWeatherSystem> Weather;

	/** Test-only framing: the body the ragdoll camera follows. */
	TWeakObjectPtr<ACitixPedestrian> RagdollTestTarget;

	UPROPERTY()
	TObjectPtr<ACameraActor> RagdollTestCamera;

	FVector RagdollCameraOffset = FVector(620.f, 0.f, 380.f);

	/** Wanted test: pursuit distances proving units close in (not just spawn). */
	float WantedPursuitDistA = -1.f;
	float WantedPursuitDistB = -1.f;

	/** Weapon test phase machine (readiness-gated, not wall-clock). */
	int32 WeaponTestPhase = 0;
	float WeaponTestPhaseTime = 0.f;
	bool WeaponTestArmed = false;

	UPROPERTY()
	TObjectPtr<APlayerStart> GeneratedPlayerStart;

	/** Overflow starts around the main one so joiners never interpenetrate. */
	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> GeneratedPlayerStarts;

	/** Which start each controller was given (freed on logout). Linear scan:
	 *  N is tiny and pointer identity never lies, unlike hashed weak keys. */
	TArray<TPair<TWeakObjectPtr<AController>, int32>> PlayerStartClaims;

	/** Periodic multiplayer state line (`-CitixNetLog` test hook). */
	void LogNetState();

private:
	bool bWorldReady = false;
};
