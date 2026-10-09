// Copyright Epic Games, Inc. All Rights Reserved.
// Player vehicle: Chaos box chassis, low-poly car visuals built from the shared
// car library, tyre smoke, chase/hood cameras and enter/exit support.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Vehicle/CitixCarLibrary.h"
#include "CitixVehiclePawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class USceneComponent;
class UInputAction;
class UInputMappingContext;
class UCitixVehicleMovementComponent;
class UCitixDriftSmokeComponent;
class UCitixBoostTrailComponent;
class UCitixAirFlowComponent;
class UCitixEngineAudioComponent;
struct FInputActionValue;

/** Compact server-driven state for remote HUD/audio (10-30 Hz, no prediction). */
USTRUCT(BlueprintType)
struct FCitixVehicleNetState
{
	GENERATED_BODY()

	UPROPERTY() float SpeedKmh = 0.f;
	UPROPERTY() float HullFraction = 1.f;
	UPROPERTY() float BoostCharge = 1.f;
	UPROPERTY() bool bBoosting = false;
	UPROPERTY() bool bDestroyed = false;
	UPROPERTY() float ThrottleAbs = 0.f;
	UPROPERTY() bool bGrounded = true;
	/** -1..1 steering (rank 18: remote wheels turn). */
	UPROPERTY() float SteerAbs = 0.f;
	/** Handbrake held (clients have no local handbrake state). */
	UPROPERTY() bool bHandbrake = false;
	/** Max lateral tyre slip speed, cm/s (remote tyre smoke amount). */
	UPROPERTY() float LateralSlip = 0.f;
};

UCLASS(Blueprintable)
class CITIX_API ACitixVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ACitixVehiclePawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;
	virtual void PawnClientRestart() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Movement-arrival counter (diagnoses one-sided freezes under -CitixNetLog). */
	virtual void OnRep_ReplicatedMovement() override;

	/** Server RPC: continuous drive inputs (unreliable, resent at 15 Hz). */
	UFUNCTION(Server, Unreliable)
	void ServerSendDriveInput(float Throttle, float Steer, bool bHandbrake, bool bBoost);

	/** Server RPC: instant reset (reliable). */
	UFUNCTION(Server, Reliable)
	void ServerResetVehicle();

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	UCitixVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void ResetVehicle();

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void ToggleCamera();

	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetBodyColor(FLinearColor Color);

	/** Which controller owns this car (never hand over another driver's ride). */
	UPROPERTY(Replicated)
	TObjectPtr<AController> OwningController = nullptr;

	void SetOwningController(AController* InController) { OwningController = InController; }
	AController* GetOwningController() const { return OwningController; }

	/** Park the car: zero inputs, handbrake on, no player inside. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetOccupied(bool bNewOccupied);

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	bool IsOccupied() const { return bOccupied; }
 FTransform LastDryPose;
 bool bHasDryPose = false;
 bool CanAcceptDriveInput() const;
 void SetRoundStartPose(const FTransform& Pose);
 UPROPERTY(ReplicatedUsing=OnRep_RoundStartPose)
 FTransform RoundStartPose;
 UFUNCTION() void OnRep_RoundStartPose();
 bool bCountdownFrozen=false;
 FTransform CountdownHoldPose;
 int32 ShoreRestores = 0;

	/** World-space point where a driver would stand after leaving the car. */
	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	FTransform GetExitTransform() const;

	/** Swap the visual car (and matching physics wheels). Used for vehicle variety. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void SetCarAppearance(ECitixCarType InType, FLinearColor InColor);

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	ECitixCarType GetCarType() const { return CarType; }

	UFUNCTION(BlueprintPure, Category = "Citix|Vehicle")
	FLinearColor GetPaintColor() const { return PaintColor; }

	/** Rebuild the visual car for the current CarType/PaintColor. */
	void RebuildCarVisual();

	/** Push the current car type's attributes into the physics body and movement. */
	void ApplyCarPerformance();
	void ApplyChasePerformance(bool bRunner);
	bool bChaseRunner = false;
	bool bChasePerformanceApplied = false;
	FVector ChasePreviousVelocity = FVector::ZeroVector;
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastChaseImpact(FVector_NetQuantize Location);

	/** Called by collision to damage the car (also used by explosions / crowd hits). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Vehicle")
	void ApplyVehicleDamage(float Amount);

	/** Car type used for the visuals. Replicated so takeovers match everywhere. */
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_CarAppearance, BlueprintReadWrite, Category = "Citix|Vehicle")
	ECitixCarType CarType = ECitixCarType::Sedan;

	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_CarAppearance, BlueprintReadWrite, Category = "Citix|Vehicle")
	FLinearColor PaintColor = FLinearColor(0.02f, 0.22f, 0.55f);

	/** Server-driven display state (HUD/audio on remote copies). */
	UPROPERTY(Replicated)
	FCitixVehicleNetState NetState;

	/** Display getters: live values with authority, replicated state remotely. */
	float GetDisplaySpeedKmh() const;
	float GetDisplayHullFraction() const;
	float GetDisplayBoostCharge() const;
	bool IsDisplayBoosting() const;
	bool IsDisplayDestroyed() const;
	float GetDisplayThrottleAbs() const;
	bool IsDisplayGrounded() const;

	/** Active pooled particle counts (diagnostics: verifies remotes still emit). */
	int32 GetActiveSmokePuffs() const;
	int32 GetActiveBoostFlames() const;

	/** Forwards locally-driven inputs at 15 Hz (autonomous proxies only). */
	void SendDriveInputToServer();

	/** Server-only: damage on-foot drivers along the drive segment. */
	void RamPlayersAlongSweep(const FVector& From, const FVector& To);

	/** Ram damage per hit (two hits kill at full health). */
	UPROPERTY(EditAnywhere, Category = "Citix|Combat")
	float RamDamage = 50.f;

	/**
	 * Remote interpolation (runs on every non-authority copy, owner included).
	 * The engine skips movement apply for autonomous proxies, and physics-packed
	 * snapshots only feed simulating bodies (ours simulate server-side only), so
	 * without this every client copy stays frozen at its spawn.
	 */
	void InterpolateRemoteMovement(float DeltaSeconds);

	/** One replicated movement snapshot, buffered so copies interpolate smoothly. */
	struct FRemoteMovementSnapshot
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Velocity = FVector::ZeroVector;
		double Time = 0.0;
	};

	/** Buffer of recent snapshots; the render pose is sampled from it (see .cpp). */
	TArray<FRemoteMovementSnapshot> RemoteMovementBuffer;

	/** Measured mean gap between snapshots, seconds (drives the interp delay). */
	float RemoteSnapshotInterval = 1.f / 20.f;

	/** Push an arriving server snapshot into the interpolation buffer. */
	void PushRemoteSnapshot(const FVector& InLocation, const FRotator& InRotation, const FVector& InVelocity);

	/** Snap distance for remote interpolation, cm (exits, resets, respawns). */
	UPROPERTY(EditAnywhere, Category = "Citix|Network")
	float RemoteSnapDistance = 800.f;

	/** Convergence speed for remote interpolation. */
	UPROPERTY(EditAnywhere, Category = "Citix|Network")
	float RemoteInterpSpeed = 12.f;

	UFUNCTION()
	void OnRep_CarAppearance();

	/** Movement-arrival diagnostics (see .cpp). */
	int32 NetMoveArrivals = 0;
	double LastNetMoveLog = 0.0;

	/** Smoothness probe (-CitixSmoothLog): per-frame step statistics of the
	 *  commanded render pose. Stutter shows up as large max steps / reversals. */
	double SmoothLogTimer = 0.0;
	float SmoothLogSum = 0.f;
	float SmoothLogMax = 0.f;
	float SmoothLogYawSum = 0.f;
	float SmoothLogYawMax = 0.f;
	float SmoothLogPrevYaw = 0.f;
	int32 SmoothLogFrames = 0;
	int32 SmoothLogReversals = 0;
	FVector SmoothLogPrevTarget = FVector::ZeroVector;
	FVector SmoothLogPrevDelta = FVector::ZeroVector;
	bool bSmoothLogInit = false;

	/** Dead-reckoning snapshot (latest replicated movement + world time). */
	FVector LastSnapLocation = FVector::ZeroVector;
	FRotator LastSnapRotation = FRotator::ZeroRotator;
	FVector LastSnapVelocity = FVector::ZeroVector;
	double LastSnapTime = 0.0;

	/** HUD-state publish throttle accumulator (server, seconds). */
	float NetStateTimer = 0.f;

protected:
	void HandleThrottle(const FInputActionValue& Value);
	void HandleThrottleReleased(const FInputActionValue& Value);
	void HandleSteering(const FInputActionValue& Value);
	void HandleSteeringReleased(const FInputActionValue& Value);
	void HandleHandbrakeStart(const FInputActionValue& Value);
	void HandleHandbrakeEnd(const FInputActionValue& Value);
	void HandleReset(const FInputActionValue& Value);
	void HandleCameraToggle(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleExitVehicle(const FInputActionValue& Value);
	void HandleBoostStart(const FInputActionValue& Value);
	void HandleBoostEnd(const FInputActionValue& Value);

	UFUNCTION()
	void OnChassisHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** Blow the car up: area damage to the crowd, fireball, then repair after a delay. */
	void TriggerExplosion();
	void UpdateExplosion(float DeltaSeconds);
	void HideExplosionVisuals();
	void PublishNetState();

	void AddDrivingMappingContext();
	void RemoveDrivingMappingContext();
	void ApplyCameraMode();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UBoxComponent> BodyCollision;

	/** Parent of the visual car; offset so car-space Z=0 sits on the ground. */
	UPROPERTY()
	TObjectPtr<USceneComponent> CarVisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UCitixVehicleMovementComponent> VehicleMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UCitixDriftSmokeComponent> DriftSmoke;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UCitixBoostTrailComponent> BoostTrail;
	UPROPERTY() TObjectPtr<UCitixAirFlowComponent> AirFlow;

	/** Synthesised engine note (no audio assets required). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UCitixEngineAudioComponent> EngineAudio;

	/** Expanding fireball shown when the car is destroyed. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ExplosionSphere;

	/** White-hot ignition flash (first blink of the blast). */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BlastFlash;

	/** Dark smoke column that rises and lingers after the fireball. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BlastSmoke;

	/** Flat shockwave disc that races outward and vanishes. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BlastRing;

	/** Runtime-built visual car. Components are actor-owned, so this is GC-safe. */
	FCitixCarVisual CarVisual;

	// ---- Camera tuning ------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float ChaseArmLength = 850.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	FVector ChaseSocketOffset = FVector(0.f, 0.f, 150.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float HoodArmLength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	FVector HoodSocketOffset = FVector(150.f, 0.f, 95.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float BaseCameraPitch = -13.f;

	/** Chase camera pulls back this much further at top speed, cm. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float SpeedArmExtension = 300.f;

	/** Chase camera FOV widens this much at top speed, degrees. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float SpeedFovWiden = 10.f;

	/** Speed at which the pull-back / FOV effects reach full strength, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float SpeedEffectMaxKmh = 200.f;

	/** How fast the camera effects chase the speed, 1/s. Low = smooth, no popping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float SpeedEffectRate = 2.5f;

	/** Base chase FOV, degrees. The speed effect widens from here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float ChaseBaseFov = 82.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float LookReturnSpeed = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Camera")
	float MouseSensitivity = 1.6f;

	/** Vehicle mass in kg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Vehicle")
	float VehicleMassKg = 1500.f;

	// ---- Input (built in C++) -----------------------------------------
	UPROPERTY() TObjectPtr<UInputMappingContext> DrivingContext;
	UPROPERTY() TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY() TObjectPtr<UInputAction> SteeringAction;
	UPROPERTY() TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY() TObjectPtr<UInputAction> ResetAction;
	UPROPERTY() TObjectPtr<UInputAction> CameraToggleAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> ExitVehicleAction;
	UPROPERTY() TObjectPtr<UInputAction> BoostAction;

private:
	float LookYawOffset = 0.f;
	float LookReturnTimer = 1.f;

	/** Latest locally-driven inputs, forwarded to the server at 15 Hz. */
	float NetThrottle = 0.f;
	float NetSteer = 0.f;
	bool bNetHandbrake = false;
	bool bNetBoost = false;
	float NetSendTimer = 0.f;
	float LastSentThrottle = 0.f;
	float LastSentSteer = 0.f;
	bool bLastSentHandbrake = false;
	bool bLastSentBoost = false;

	/** Last appearance actually built (guards double rebuilds on replicate). */
	ECitixCarType BuiltCarType = ECitixCarType::Count;
	FLinearColor BuiltPaintColor = FLinearColor::Black;
	bool bHoodCamera = false;
	/** Smoothed 0..1 speed fraction driving the chase-camera pull-back and FOV. */
	float SmoothedSpeedFraction = 0.f;
	/** Replicated: prompts and remote effects read occupancy, not guesses. */
	UPROPERTY(ReplicatedUsing=OnRep_Occupied)
	bool bOccupied = true;
	UFUNCTION() void OnRep_Occupied();
	UPROPERTY() TArray<TObjectPtr<class USpotLightComponent>> Headlights;
	float GroundOffset = 70.f;

	/** Seconds since the last steering / throttle input event (stuck-input guard). */
	float SteeringInputAge = 1.f;
	float ThrottleInputAge = 1.f;

	/** True once the player has actually driven this control, so the guard only
	 *  releases values that came from input (never programmatic/test values). */
	bool bSteeringFromInput = false;
	bool bThrottleFromInput = false;
	/** Seconds since the last crash damage, so one impact is not counted many times. */
	float CrashDamageCooldown = 0.f;
 float PursuitPulseTimer=0.f;


	/** Player-initiated reset cooldown (system recovery bypasses it). */
	float ResetCooldownTimer = 0.f;

	/** Remote wheel spin accumulator (rank 18: remotes read as driven). */
	float RemoteWheelSpin = 0.f;
	/** Smoothed remote steering angle, degrees (NetState is only 15 Hz). */
	float RemoteSteerDeg = 0.f;
	/** Smoothed remote road speed, km/h (drives the remote wheel spin rate). */
	float RemoteSpeedKmh = 0.f;
	/** Speed at the previous frame: distinguishes ramming something from being rammed. */
	float LastSpeed = 0.f;

	/** Explosion / post-destruction state. */
	bool bExploding = false;
	float ExplosionTimer = 0.f;
	float DestroyedTimer = 0.f;

	/** Cached so the car can mow down pedestrians without a hard dependency. */
	TWeakObjectPtr<class ACitixPedestrianSystem> CachedPedestrians;

	/** Half width of the swept body used for run-over tests, cm. The test is a segment
	 *  (last frame's position to this frame's) widened by this, against each pedestrian's
	 *  own hitbox, so a car crossing a body in one frame still registers the contact. */
	UPROPERTY(EditAnywhere, Category = "Citix|Vehicle|Damage")
	float RunOverRadius = 300.f;

	UPROPERTY(EditAnywhere, Category = "Citix|Vehicle|Damage")
	float RunOverMinSpeed = 260.f;

	/** Previous tick's location, for the swept run-over test. */
	FVector PreviousLocation = FVector::ZeroVector;
	bool bHasPreviousLocation = false;

	UPROPERTY(EditAnywhere, Category = "Citix|Vehicle|Damage")
	float ExplosionRadius = 1400.f;

	UPROPERTY(EditAnywhere, Category = "Citix|Vehicle|Damage")
	float ExplosionDuration = 1.1f;

	/** Seconds after destruction before the car is repaired and drivable again. */
	UPROPERTY(EditAnywhere, Category = "Citix|Vehicle|Damage")
	float RepairDelay = 8.f;
};
