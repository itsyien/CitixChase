// Copyright Epic Games, Inc. All Rights Reserved.
// A single ambient pedestrian. Presentation only: the pedestrian system owns the
// simulation and pushes transforms and walk poses into these actors.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/CitixCharacterLibrary.h"
#include "CitixPedestrian.generated.h"

class USceneComponent;

/** What a ragdoll is for: a survivable knock-down, or a death. */
UENUM()
enum class ECitixRagdollKind : uint8
{
	None,
	/** Survived the hit: physics tumble, then get back up. */
	Knockdown,
	/** Killed: physics tumble, then fade away. */
	Death
};

class UBoxComponent;
class ACitixPedestrianSystem;

UCLASS(NotBlueprintable)
class CITIX_API ACitixPedestrian : public AActor
{
	GENERATED_BODY()

public:
	ACitixPedestrian();

	virtual void BeginPlay() override;

	/** Build the body. Must be called once after spawning. */
	void InitializeAppearance(ECitixCharacterStyle InStyle, int32 Seed);

	void UpdatePose(float PhaseRadians, float SwingDegrees, float BobCm);
	void SetActiveVisual(bool bVisible);

	/** The system that owns this pedestrian, so a hitbox overlap can report a contact. */
	void SetOwningSystem(ACitixPedestrianSystem* InSystem) { OwningSystem = InSystem; }

	/** World centre and radius of the hitbox, for the vehicle's swept contact test. */
	FVector GetHitboxCentre() const;
	float GetHitboxRadius() const;

	/** Enable/disable the walking hitbox (off while ragdolling or hidden). */
	void SetHitboxEnabled(bool bEnabled);

	/** True while the walking hitbox can register contacts. */
	bool IsHitboxEnabled() const;

	/**
	 * Turn this pedestrian into a physics ragdoll: limbs become rigid bodies joined by
	 * angular-limited constraints, given the impact impulse.
	 *   Knockdown: they tumble, then play a get-up animation and walk on.
	 *   Death: they tumble, lie there, then shrink away and are recycled.
	 */
	void Ragdoll(const FVector& Impulse, const FVector& ImpactLocation, ECitixRagdollKind InKind);

	/** Which way to face (and what height to stand at) after a knock-down. */
	void SetRecoveryFacing(float YawDegrees, float StandZ);

	/**
	 * Advance the ragdoll sequence. Returns true when the whole sequence has finished
	 * (died and faded, or stood back up). Check IsFatalRagdoll() to tell them apart.
	 */
	bool UpdateRagdoll(float DeltaSeconds);

	/** Skip the rest of the get-up and stand up at once (used under physics budget pressure). */
	void FastForwardRecovery();
	/** Reuse the articulated rig as a local player effect, without ambient AI. */
	void ConfigurePlayerKnockdown(float TumbleSeconds) { MinSettleTime = MaxSettleTime = FMath::Max(0.f, TumbleSeconds); GetUpDuration = .65f; KnockdownImpulseScale = 1.f; }

	bool IsRagdolling() const { return RagdollKind != ECitixRagdollKind::None; }
	bool IsFatalRagdoll() const { return RagdollKind == ECitixRagdollKind::Death; }
	bool IsKnockedDown() const { return RagdollKind == ECitixRagdollKind::Knockdown; }

	ECitixCharacterStyle GetStyle() const { return Style; }
	int32 GetComponentCount() const { return Rig.GetComponentCount(); }

	/** Physics cost of one ragdoll, for budget decisions. */
	float GetRagdollAge() const { return RagdollAge; }

	/** Where the body actually is: the torso while ragdolling, the actor otherwise. */
	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrian")
	FVector GetBodyLocation() const;

	/** Restore the walking rig so the pooled actor can be reused. */
	void ResetFromRagdoll();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	/**
	 * Walking collision volume. Query-only and overlap-only, so the player's car (a physics
	 * body) and the on-foot player (a pawn) pass through it and get an overlap event: the
	 * person then reacts by ragdolling, while the car is never stopped by them and is never
	 * damaged. It is not a physics body itself, so it does not push anything.
	 */
	UPROPERTY()
	TObjectPtr<UBoxComponent> Hitbox;

	TWeakObjectPtr<ACitixPedestrianSystem> OwningSystem;

	UFUNCTION()
	void OnHitboxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	ECitixCharacterStyle Style = ECitixCharacterStyle::Casual;

	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	int32 AppearanceSeed = 0;

	UPROPERTY(ReplicatedUsing=OnRep_Visible)
	bool bVisibleNet = false;

	/**
	 * Compact reaction state for remotes (rank 14): 0 walking, 1 down,
	 * 2 dead, 3 recovering. Limbs stay server-side; remotes play a canned
	 * fall/get-up so both screens agree on the NPC's condition.
	 */
	UPROPERTY(ReplicatedUsing=OnRep_Reaction)
	uint8 ReactionNet = 0;

	UFUNCTION()
	void OnRep_Appearance();

	UFUNCTION()
	void OnRep_Visible();

	UFUNCTION()
	void OnRep_Reaction();

	FCitixHumanoidRig Rig;
	bool bAppearanceReady = false;

	virtual void Tick(float DeltaSeconds) override;

	/** Remote canned fall/recover animation age (remotes only). */
	float RemoteAnimAge = 0.f;

	// ---- Ragdoll state ------------------------------------------------

	ECitixRagdollKind RagdollKind = ECitixRagdollKind::None;

	/** Seconds since the ragdoll started. */
	float RagdollAge = 0.f;

	/** Time the body lies there before it starts fading (death only), seconds. */
	UPROPERTY(EditAnywhere, Category = "Citix|Pedestrian")
	float RagdollHoldTime = 1.6f;

	/** How long the death fade-out takes, seconds. */
	UPROPERTY(EditAnywhere, Category = "Citix|Pedestrian")
	float RagdollFadeTime = 1.4f;

	/** Minimum time spent as a ragdoll before a survivor tries to get up. */
	UPROPERTY(EditAnywhere, Category = "Citix|Pedestrian")
	float MinSettleTime = 1.05f;

	/** Hard cap on ragdolling: get up even if the body has not fully settled. */
	UPROPERTY(EditAnywhere, Category = "Citix|Pedestrian")
	float MaxSettleTime = 2.4f;

	/** How long the get-up animation takes, seconds. */
	UPROPERTY(EditAnywhere, Category = "Citix|Pedestrian")
	float GetUpDuration = 1.45f;

	/** Survivors are tossed less than a fatal hit, so they land near where they stood. */
	UPROPERTY(EditAnywhere, Category = "Citix|Pedestrian")
	float KnockdownImpulseScale = 0.5f;

	float SettleQuietTime = 0.f;

	bool bGettingUp = false;
	float GetUpElapsed = 0.f;

	/** Pose captured from the physics the moment the get-up begins. */
	TArray<FTransform> RecoveryStartTransforms;
	FVector RecoveryStartLocation = FVector::ZeroVector;
	float RecoveryStartYaw = 0.f;

	FVector RecoveryTargetLocation = FVector::ZeroVector;
	float RecoveryTargetYaw = 0.f;
	float RecoveryStandZ = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<class UPhysicsConstraintComponent>> RagdollConstraints;

	// ---- Helpers ------------------------------------------------------

	/** World Z of the lowest point of the body (where it rests on the ground). */
	float ComputeGroundZ() const;

	/** True once the torso has all but stopped moving. */
	bool IsPhysicsSettled() const;

	/** Capture the physical pose, re-parent the limbs and start the get-up animation. */
	void BeginGetUp();

	/** Sample the authored get-up animation at t in 0..1. */
	static FCitixHumanoidPose SampleGetUpPose(float T);

	/** Pose and place the body for the get-up at t in 0..1. */
	void ApplyGetUp(float T);

	/** Tear down the ragdoll constraints. */
	void RemoveConstraints();
};
