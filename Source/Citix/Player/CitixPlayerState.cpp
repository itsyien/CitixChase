// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/CitixPlayerState.h"

#include "Net/UnrealNetwork.h"

void ACitixPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Private progression replicates owner-only (rank 16): wallet, arsenal,
	// objectives and feeds are nobody else's business. Public facts (health,
	// wanted, shots, appearance) replicate to everyone.
	DOREPLIFETIME_CONDITION(ACitixPlayerState, CitixScore, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, Money, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, JobsCompleted, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivitiesCompleted, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, PhotosTaken, COND_OwnerOnly);
	DOREPLIFETIME(ACitixPlayerState, Heat);
	DOREPLIFETIME(ACitixPlayerState, WantedStars);
	DOREPLIFETIME(ACitixPlayerState, WantedState);
	DOREPLIFETIME(ACitixPlayerState, EscapeProgress);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, bHasWaypoint, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, WaypointLocation, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, WaypointName, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, DiscoveredPOIs, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, VisitedDistrictsPersonal, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, JobState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, JobDestination, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, JobTimeRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, JobDistanceRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, bQuestOffered, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityType, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityName, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityStopsDone, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityStops, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityStopsTotal, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityTimeRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ActivityDistanceRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, bOffRoute, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, CardTitle, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, CardSub, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, CardSerial, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ToastText, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ToastSerial, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, LastPhotoScore, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, PhotoCooldown, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, LastPhotoBreakdown, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, OwnedMask, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, MagAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ReserveAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, CurrentWeapon, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, bReloading, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, ReloadFraction, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, AdsAmount, COND_OwnerOnly);
	DOREPLIFETIME(ACitixPlayerState, Health);
	DOREPLIFETIME(ACitixPlayerState, MaxHealth);
	DOREPLIFETIME(ACitixPlayerState, ShotMuzzle);
	DOREPLIFETIME(ACitixPlayerState, ShotImpact);
	DOREPLIFETIME(ACitixPlayerState, ShotSurface);
	DOREPLIFETIME(ACitixPlayerState, ShotSerial);
	DOREPLIFETIME(ACitixPlayerState, VisibleWeapon);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RaceState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RaceCountdown, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RaceCheckpoint, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RaceCheckpointsDone, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RaceCheckpointsTotal, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RacePosition, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACitixPlayerState, RaceTime, COND_OwnerOnly);
}
