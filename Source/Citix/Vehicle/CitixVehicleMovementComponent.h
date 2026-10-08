// Copyright Epic Games, Inc. All Rights Reserved.
// A self-contained wheeled vehicle model built on Chaos rigid-body physics.
//
// Why not UChaosWheeledVehicleMovementComponent?
//   * It requires a skeletal mesh, physics asset and bone-named wheels, i.e. authored
//     content we do not have yet.
//   * This implementation needs only a box collider, so the game is drivable with
//     zero art dependencies, and it is trivially replaceable later.
//
// Model: per-wheel spring/damper suspension via line traces + a clamped tire friction
// circle. Forces are applied through AddForceAtLocation so weight transfer, roll and
// pitch emerge naturally.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "CitixVehicleMovementComponent.generated.h"

class UPrimitiveComponent;
struct FCitixCarPerformance;

/** One wheel of the vehicle. */
USTRUCT(BlueprintType)
struct FCitixWheelSetup
{
	GENERATED_BODY()

	/** Attach point in vehicle local space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
	FVector AttachOffset = FVector::ZeroVector;

	/** Steers with driver input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
	bool bSteering = false;

	/** Receives engine force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel")
	bool bDriven = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheel", meta = (ClampMin = "5"))
	float Radius = 34.f;
};

/** Per-frame wheel state, exposed so the pawn can pose cosmetic wheels. */
USTRUCT(BlueprintType)
struct FCitixWheelRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Wheel")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Wheel")
	bool bGrounded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Wheel")
	float SteerAngleDegrees = 0.f;

	/** 0 = fully extended, 1 = fully compressed. Drive cosmetic suspension travel. */
	UPROPERTY(BlueprintReadOnly, Category = "Wheel")
	float CompressionRatio = 0.f;

	/** Rolling distance, for wheel spin animation. */
	UPROPERTY(BlueprintReadOnly, Category = "Wheel")
	float SpinAngleDegrees = 0.f;

	/** World-space wheel centre this frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Wheel")
	FVector WorldLocation = FVector::ZeroVector;
};

UCLASS(ClassGroup = (Citix), meta = (BlueprintSpawnableComponent), HideCategories = (Variable))
class CITIX_API UCitixVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UCitixVehicleMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Driver input -------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetThrottleInput(float Value);
 bool bIceFrozen=false;
 bool bInBush=false;

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetSteeringInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetBrakeInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetHandbrake(bool bEngaged);

	/** Teleport the vehicle upright to the nearest safe ground position and stop it. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void ResetVehicle();

	// ---- Readouts -----------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetForwardSpeed() const;

	/** Speed in km/h, for the HUD. */
	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetSpeedKmh() const;

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetCurrentSteerAngleDegrees() const { return CurrentSteerAngle; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool IsGrounded() const { return NumGroundedWheels > 0; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool IsUpsideDown() const { return bUpsideDown; }

	/** Largest lateral tyre slip across all wheels, cm/s. Drives drift smoke. */
	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetMaxLateralSlipSpeed() const { return MaxLateralSlipSpeed; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool IsHandbrakeEngaged() const { return bHandbrake; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetThrottleInput() const { return ThrottleInput; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetSteeringInput() const { return SteeringInput; }

	/** World-space centre of a wheel, for FX placement. */
	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool GetWheelWorldLocation(int32 WheelIndex, FVector& OutLocation) const;

	/** Wheel layout queries for remote visual spin (rank 18). */
	int32 GetWheelCount() const;
	float GetWheelRadius(int32 WheelIndex) const;
	bool IsWheelSteering(int32 WheelIndex) const;
	float GetMaxSteerAngleDegrees() const;

	/** Replace the wheel layout (used to match the visual car). */
	void ConfigureWheels(const TArray<FCitixWheelSetup>& InWheels);

	/**
	 * Adopt a car type's driving and durability attributes: power, top speed, braking,
	 * hull strength, tyre grip and how it behaves on the handbrake. Suspension springing is
	 * derived from the mass so every car rides at the same height. Resets the hull.
	 */
	void ApplyCarPerformance(const FCitixCarPerformance& Performance);

	// ---- Boost ---------------------------------------------------------

	/** Hold-to-boost input (Shift). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetBoostInput(bool bIn);

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool IsBoosting() const { return bBoostActive; }
	bool bChaseBreakawayBoost = false;

	/** Boost reserve, 0..1. Drains while boosting and refills over time. */
	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetBoostCharge() const { return BoostCharge; }
	void ReconcileBoostCharge(float ServerCharge) { BoostCharge = FMath::Clamp(ServerCharge, 0.f, 1.f); }

	// ---- Damage --------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void ApplyDamage(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void RepairFull();
 void SetMaxHealthPreservingFraction(float Maximum) { Health=GetHealthFraction()*FMath::Max(1.f,Maximum); MaxHealth=FMath::Max(1.f,Maximum); }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	float GetHealthFraction() const { return Health / FMath::Max(1.f, MaxHealth); }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool IsDestroyed() const { return bDestroyed; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Boost")
	float BoostSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Boost")
	float BoostForceMultiplier = 1.75f;

	/** Boost reserve drained per second while boosting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Boost")
	float BoostDrainRate = 0.34f;

	/** Boost reserve refilled per second once regeneration starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Boost")
	float BoostRegenRate = 0.14f;

	/** Seconds after releasing boost before it starts refilling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Boost")
	float BoostRegenDelay = 1.2f;

	/** Reserve needed to start a fresh boost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Boost")
	float MinBoostToStart = 0.12f;

	/** Hull points. Overridden per car type by ApplyCarPerformance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Damage")
	float MaxHealth = 400.f;

	/** Damage per cm/s of impact speed above the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Damage")
	float CrashDamagePerSpeed = 0.06f;

	/** Impact speed below which a collision does no damage: 30 km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Damage")
	float CrashSpeedThreshold = 833.f;

	/** Cap on damage from a single collision, so one wall tap cannot instantly wreck the car. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Damage")
	float CrashDamageMax = 45.f;

	const TArray<FCitixWheelRuntimeState>& GetWheelStates() const { return WheelStates; }

	// ---- Tuning (defaults are tuned in cm/kg/s) -----------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float SuspensionStiffness = 42000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float SuspensionDamping = 7200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float SuspensionRestLength = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float MaxSuspensionForce = 1400000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Engine")
	float MaxEngineForce = 1350000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Engine")
	float MaxBrakeForce = 2600000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Engine")
	float ReverseForceScale = 0.5f;

	/** Speed above which the throttle is treated as a brake, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Engine")
	float ReverseThresholdSpeed = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Engine")
	float MaxSpeed = 6600.f;
 float AbsoluteSpeedLimit = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Engine")
	float DragCoefficient = 48.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float MaxSteerAngle = 34.f;

	/**
	 * Steering authority when the handbrake is not held. Lower = calmer, less twitchy
	 * normal driving; full authority is restored while drifting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float NormalSteerAuthority = 0.62f;
 float HighSpeedSteerFraction=.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float DriftSteerAuthority = 1.0f;

	/** How fast steering moves toward the driver's input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float SteerInterpSpeed = 16.f;

	/** Faster rate used to straighten the wheels when input returns to centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float SteerReturnSpeed = 30.f;

	/** Lateral force per cm/s of slip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float LateralStiffness = 850.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float TireFrictionCoefficient = 1.30f;

	/**
	 * Extra lateral deceleration (cm/s^2, scaled by slip) that keeps the car tracking
	 * its nose instead of sliding wide. Applied only when the handbrake is released.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float SlideAssistAccel = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float SlideAssistMinSpeed = 300.f;

	/** Extra yaw damping while driving normally; released while drifting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float NormalAngularDamping = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float DriftAngularDamping = 0.80f;

	/** Grounded handbrake yaw target in degrees/s. Zero preserves unassisted handling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float DriftYawRateDegrees = 0.f;

	/** Time-based planar alignment; changes direction without adding speed or altering Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float DriftAlignmentRate = 4.f;

	/** Modest speed cost per second of active drift assistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DriftSpeedScrubRate = .12f;


	// ---- Mass-derived springing ---------------------------------------
	// Rather than hand-tuning suspension per car, it is derived from the mass so that
	// every car sits at the same ride height and simply carries its own weight. The
	// defaults reproduce the original sedan values (1500 kg -> 42000 / 7140 / 1.47e6).

	/** Spring rate per kg of vehicle mass, kg cm/s^2 per kg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float SuspensionStiffnessPerKg = 28.f;

	/** Damper rate per kg (tuned for ~0.9 damping ratio). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float SuspensionDampingPerKg = 4.76f;

	/** Force ceiling per kg, so a heavy car cannot be launched by a bump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Suspension")
	float MaxSuspensionForcePerKg = 980.f;

	/**
	 * Rear lateral grip retained while the handbrake is held. Low values are what actually
	 * produce a drift: the rear lets go, so the car rotates while still travelling forwards.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandbrakeGripScale = 0.40f;

	/**
	 * Engine force multiplier while the handbrake is held. 0 = the handbrake cuts drive
	 * completely: a drift cannot accelerate the car. That is the whole cost of holding it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandbrakeEngineForceScale = 0.f;

	/**
	 * Longitudinal braking applied by the handbrake itself. 0 = the handbrake does NOT
	 * slow the car down; it only takes away your ability to accelerate, so it is a
	 * trade-off rather than a brake pedal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandbrakeBrakeScale = 0.f;

	/**
	 * Stability assist retained while drifting, as a fraction of the normal assist.
	 * A little of it keeps the drift controllable instead of a spin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DriftSlideAssistScale = 0.30f;

	/** Downforce per (cm/s)^2 of speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float DownforceCoefficient = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float MaxAngularSpeedDegrees = 420.f;

	/** Auto-right the car after this long spent upside down. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle|Handling")
	float AutoResetUpsideDownDelay = 3.f;

	/** Line trace channel used for wheel suspension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_WorldStatic;

protected:
	/** Fills default wheel layout if the pawn has not supplied one. */
	void EnsureDefaultWheels();

	UPrimitiveComponent* GetBodyPrimitive() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle")
	TArray<FCitixWheelSetup> Wheels;

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Vehicle")
	TArray<FCitixWheelRuntimeState> WheelStates;

private:
	float ThrottleInput = 0.f;
	float SteeringInput = 0.f;
	float BrakeInput = 0.f;
	bool bHandbrake = false;

	float CurrentSteerAngle = 0.f;
	float DriftResponseBlend = 0.f;
	float ForwardSpeedCached = 0.f;
	float MaxLateralSlipSpeed = 0.f;
	int32 NumGroundedWheels = 0;

	// Boost / damage state.
	bool bBoostInput = false;
	bool bBoostActive = false;
	float BoostCharge = 1.f;
	float BoostRegenTimer = 0.f;
	float Health = 100.f;
	bool bDestroyed = false;
	bool bUpsideDown = false;
	float UpsideDownTimer = 0.f;
	float WheelSpinAccumulator = 0.f;
	/** True once a car type's attributes have been applied, so health carries over. */
	bool bPerformanceApplied = false;

	void ApplySuspensionAndTires(float DeltaTime, UPrimitiveComponent& Body);
	void ApplyGroundedDriftResponse(float DeltaTime, UPrimitiveComponent& Body);
};
