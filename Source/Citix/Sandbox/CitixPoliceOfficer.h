// Copyright Epic Games, Inc. All Rights Reserved.
// Dismounted police officer: a low-poly character with a procedural walk cycle,
// driven externally by the sandbox director (no tick, no simulation of its own).
// Falls and fades when killed - the same non-graphic language as everything else.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/CitixCharacterLibrary.h"
#include "CitixPoliceOfficer.generated.h"

UCLASS(NotBlueprintable)
class CITIX_API ACitixPoliceOfficer : public AActor
{
	GENERATED_BODY()

public:
	ACitixPoliceOfficer();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Build the character. Call once after spawning. */
	void InitializeOfficer(int32 Seed);

	/** Advance the walk cycle for a frame of motion (0 speed = stand). */
	void UpdateStride(float SpeedCmS, float DeltaSeconds);

	/** Fall + shrink-fade. Returns true when finished and releasable. */
	bool UpdateDying(float DeltaSeconds);

	/** Replicated death flag (server decides, everyone plays the fall). */
	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	/** Appearance seed (one fixed uniform: same on every screen). */
	UPROPERTY(ReplicatedUsing = OnRep_Seed)
	int32 OfficerSeed = 0;

protected:
	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Seed();

private:
	FCitixHumanoidRig Rig;
	float WalkPhase = 0.f;
	float DyingTimer = 0.f;
	bool bRigReady = false;
	FVector LastTickLocation = FVector::ZeroVector;
	bool bHasLastTickLocation = false;
};
