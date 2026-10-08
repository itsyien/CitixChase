// Copyright Epic Games, Inc. All Rights Reserved.
// Per-player replicated state: progression, objectives, event feed, loadout.
// The server simulates; clients render. Shared city knowledge lives in the
// game state; everything personal lives here.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "CitixPlayerState.generated.h"

UCLASS(Blueprintable)
class CITIX_API ACitixPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Progression ---------------------------------------------------
	// NOTE: APlayerState already defines Score (float). Ours is CitixScore.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Progress")
	int32 CitixScore = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Progress")
	int32 Money = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Progress")
	int32 JobsCompleted = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Progress")
	int32 ActivitiesCompleted = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Progress")
	int32 PhotosTaken = 0;

	// ---- Wanted --------------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Wanted")
	float Heat = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Wanted")
	int32 WantedStars = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Wanted")
	uint8 WantedState = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Wanted")
	float EscapeProgress = 0.f;

	// ---- Waypoint ------------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	bool bHasWaypoint = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FVector WaypointLocation = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FString WaypointName;

	/** Personally discovered POI indices (private progression). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<int32> DiscoveredPOIs;

	/** Personally visited district indices (private progression). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<int32> VisitedDistrictsPersonal;

	// ---- Delivery job --------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	ECitixJobState JobState = ECitixJobState::Idle;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FVector JobDestination = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	float JobTimeRemaining = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	float JobDistanceRemaining = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	bool bQuestOffered = false;

	// ---- Activity ------------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	ECitixActivityType ActivityType = ECitixActivityType::None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	ECitixActivityState ActivityState = ECitixActivityState::Idle;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FString ActivityName;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	int32 ActivityStopsDone = 0;

	/** World-space tour/drive stops (empty unless an activity is active). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<FVector> ActivityStops;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	int32 ActivityStopsTotal = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	float ActivityTimeRemaining = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	float ActivityDistanceRemaining = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	bool bOffRoute = false;

	// ---- Event feed (serial bumps; clients show each once, locally timed) --
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FString CardTitle;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FString CardSub;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	int32 CardSerial = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FString ToastText;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	int32 ToastSerial = 0;

	// ---- Photo ---------------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	int32 LastPhotoScore = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	float PhotoCooldown = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	FString LastPhotoBreakdown;

	// ---- Loadout -------------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	int32 OwnedMask = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	TArray<int32> MagAmmo;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	TArray<int32> ReserveAmmo;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	int32 CurrentWeapon = -1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	bool bReloading = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	float ReloadFraction = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	float AdsAmount = 0.f;

	// ---- Health (PvP) ----------------------------------------------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Health")
	float Health = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Health")
	float MaxHealth = 100.f;

	// ---- Shot event feed (serial; remotes play their local pools) --------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	FVector ShotMuzzle = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	FVector ShotImpact = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	uint8 ShotSurface = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	int32 ShotSerial = 0;

	/** Equipped weapon for remote visuals (public; loadout stays private). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Weapons")
	uint8 VisibleWeapon = 255;

	// ---- Race snapshot (owner-only; beacon rebuilt locally) ---------------
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	uint8 RaceState = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	float RaceCountdown = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	FVector RaceCheckpoint = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	int32 RaceCheckpointsDone = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	int32 RaceCheckpointsTotal = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	int32 RacePosition = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Race")
	float RaceTime = 0.f;

	/** Server-only damage bookkeeping (never replicated). */
	float LastDamageTime = -1000.f;
	float RamInvulnUntil = -1000.f;
};
