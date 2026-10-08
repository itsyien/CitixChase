// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixChasePlayerState.h"
#include "City/CitixCityGenerator.h"
#include "Chase/CitixChaseGameState.h"
#include "Vehicle/CitixVehiclePawn.h"

#include "Vehicle/CitixCarLibrary.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
#include "Citix.h"

UCitixVehicleMovementComponent::UCitixVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCitixVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureDefaultWheels();

	if (UPrimitiveComponent* Body = GetBodyPrimitive())
	{
		SetUpdatedComponent(Body);
		Body->SetLinearDamping(0.08f);
		Body->SetAngularDamping(1.2f);
	}
}

UPrimitiveComponent* UCitixVehicleMovementComponent::GetBodyPrimitive() const
{
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		return Cast<UPrimitiveComponent>(OwnerPawn->GetRootComponent());
	}
	return nullptr;
}

void UCitixVehicleMovementComponent::EnsureDefaultWheels()
{
	if (Wheels.Num() > 0)
	{
		WheelStates.SetNum(Wheels.Num());
		return;
	}

	// Default layout for a ~4.6 m x 1.9 m car.
	const float HalfWheelbase = 150.f;
	const float HalfTrack = 82.f;
	const float AttachZ = 0.f;

	auto MakeWheel = [](float X, float Y, float Z, bool bSteer, bool bDrive)
	{
		FCitixWheelSetup W;
		W.AttachOffset = FVector(X, Y, Z);
		W.bSteering = bSteer;
		W.bDriven = bDrive;
		W.Radius = 34.f;
		return W;
	};

	Wheels.Add(MakeWheel(HalfWheelbase, -HalfTrack, AttachZ, true, true));   // Front left
	Wheels.Add(MakeWheel(HalfWheelbase, HalfTrack, AttachZ, true, true));    // Front right
	Wheels.Add(MakeWheel(-HalfWheelbase, -HalfTrack, AttachZ, false, true)); // Rear left
	Wheels.Add(MakeWheel(-HalfWheelbase, HalfTrack, AttachZ, false, true));  // Rear right
	WheelStates.SetNum(Wheels.Num());
}

void UCitixVehicleMovementComponent::SetThrottleInput(float Value)
{
	ThrottleInput = FMath::Clamp(Value, -1.f, 1.f);
}

void UCitixVehicleMovementComponent::SetSteeringInput(float Value)
{
	SteeringInput = FMath::Clamp(Value, -1.f, 1.f);
}

void UCitixVehicleMovementComponent::SetBrakeInput(float Value)
{
	BrakeInput = FMath::Clamp(Value, 0.f, 1.f);
}

void UCitixVehicleMovementComponent::SetHandbrake(bool bEngaged)
{
	bHandbrake = bEngaged;
}

void UCitixVehicleMovementComponent::SetBoostInput(bool bIn)
{
	bBoostInput = bIn;
}

void UCitixVehicleMovementComponent::ApplyDamage(float Amount)
{
	if (bDestroyed || Amount <= 0.f)
	{
		return;
	}
	Health = FMath::Max(0.f, Health - Amount);
	if (Health <= 0.f)
	{
		bDestroyed = true;
		bBoostActive = false;
		bBoostInput = false;
		bHandbrake = true;
	}
}

void UCitixVehicleMovementComponent::RepairFull()
{
	Health = MaxHealth;
	bDestroyed = false;
	bHandbrake = false;
	BoostCharge = 1.f;
}

void UCitixVehicleMovementComponent::ApplyCarPerformance(const FCitixCarPerformance& Performance)
{
	MaxSpeed = Performance.MaxSpeed;
	MaxEngineForce = Performance.EngineForce;
	MaxBrakeForce = Performance.BrakeForce;

	// A different car is a different car, but swapping types must not be a free repair:
	// the hull fraction carries over, so cycling cars cannot be used to heal.
	const float HullFraction = bPerformanceApplied && MaxHealth > 0.f
		? FMath::Clamp(Health / MaxHealth, 0.f, 1.f)
		: 1.f;
	MaxHealth = FMath::Max(1.f, Performance.MaxHealth);
	Health = FMath::Max(1.f, HullFraction * MaxHealth);
	bPerformanceApplied = true;
	bDestroyed = false;

	// Tyre and drift character.
	LateralStiffness = Performance.LateralStiffness;
	HandbrakeGripScale = Performance.HandbrakeGripScale;
	DriftSteerAuthority = Performance.DriftSteerAuthority;
	DriftSlideAssistScale = Performance.DriftSlideAssistScale;
	DriftAngularDamping = Performance.DriftAngularDamping;

	// Springing follows the mass, so every car sits at the same ride height and simply
	// carries its own weight instead of a bus bottoming out on sedan springs.
	SuspensionStiffness = Performance.MassKg * SuspensionStiffnessPerKg;
	SuspensionDamping = Performance.MassKg * SuspensionDampingPerKg;
	MaxSuspensionForce = Performance.MassKg * MaxSuspensionForcePerKg;
}

void UCitixVehicleMovementComponent::ConfigureWheels(const TArray<FCitixWheelSetup>& InWheels)
{
	if (InWheels.Num() == 0)
	{
		return;
	}
	Wheels = InWheels;
	WheelStates.SetNum(Wheels.Num());
}

bool UCitixVehicleMovementComponent::GetWheelWorldLocation(int32 WheelIndex, FVector& OutLocation) const
{
	if (WheelStates.IsValidIndex(WheelIndex))
	{
		OutLocation = WheelStates[WheelIndex].WorldLocation;
		return true;
	}
	return false;
}

int32 UCitixVehicleMovementComponent::GetWheelCount() const
{
	return Wheels.Num() > 0 ? Wheels.Num() : 4;
}

float UCitixVehicleMovementComponent::GetWheelRadius(int32 WheelIndex) const
{
	if (Wheels.IsValidIndex(WheelIndex))
	{
		return FMath::Max(1.f, Wheels[WheelIndex].Radius);
	}
	return 34.f;
}

bool UCitixVehicleMovementComponent::IsWheelSteering(int32 WheelIndex) const
{
	if (Wheels.IsValidIndex(WheelIndex))
	{
		return Wheels[WheelIndex].bSteering;
	}
	return WheelIndex < 2;
}

float UCitixVehicleMovementComponent::GetMaxSteerAngleDegrees() const
{
	return MaxSteerAngle;
}

void UCitixVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UPrimitiveComponent* Body = GetBodyPrimitive();
	if (!Body || !Body->IsSimulatingPhysics())
	{
		return;
	}
	if (Wheels.Num() == 0)
	{
		EnsureDefaultWheels();
	}
	if (WheelStates.Num() != Wheels.Num())
	{
		WheelStates.SetNum(Wheels.Num());
	}

	if (AbsoluteSpeedLimit > 0.f) Body->SetPhysicsLinearVelocity(FCitixChaseRules::LimitVelocity(Body->GetPhysicsLinearVelocity(), AbsoluteSpeedLimit));


	// Resolve the lock in PrePhysics, before any input can produce force on either peer.
 bIceFrozen=false;
 if (auto* Car=Cast<ACitixVehiclePawn>(GetOwner())) {
  const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>();
  const AController* Driver=Car->GetOwningController();
  const auto* PS=Driver ? Driver->GetPlayerState<ACitixChasePlayerState>() : Car->GetPlayerState<ACitixChasePlayerState>();
  bIceFrozen=S && PS && S->Phase==ECitixChasePhase::Pursuit && PS->FrozenUntil>S->GetServerWorldTimeSeconds();
  if (bIceFrozen) {
   FVector V=Body->GetPhysicsLinearVelocity(); const float Speed=V.Size2D();
   if (Speed>PS->FrozenSpeedLimit && Speed>0) { const float Scale=PS->FrozenSpeedLimit/Speed; V.X*=Scale; V.Y*=Scale; Body->SetPhysicsLinearVelocity(V); }
   bBoostActive=false;
  }
 }

 ApplySuspensionAndTires(DeltaTime, *Body);
 // Server-simulated foliage drag: overlap-only foliage never becomes a wall or
 // a suspension surface. One spatial query per simulated car, no foliage ticks.
 TArray<FOverlapResult> BushHits;
 FCollisionQueryParams BushQuery(SCENE_QUERY_STAT(CitixBushDrag),false,GetOwner());
 if (Body->GetPhysicsLinearVelocity().SizeSquared2D()>100.f*100.f) GetWorld()->OverlapMultiByObjectType(BushHits,Body->GetComponentLocation(),Body->GetComponentQuat(),FCollisionObjectQueryParams(ECC_GameTraceChannel2),Body->GetCollisionShape(),BushQuery);
 bInBush=BushHits.ContainsByPredicate([](const FOverlapResult& Hit) {return Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("CitixBush"));});
 if (bInBush && !bDestroyed) {
  FVector BushVelocity=Body->GetPhysicsLinearVelocity(); const float Drag=FMath::Exp(-1.2f*FMath::Max(0.f,DeltaTime));
  BushVelocity.X*=Drag; BushVelocity.Y*=Drag; Body->SetPhysicsLinearVelocity(BushVelocity);
 }

 // --- Boost reserve ---------------------------------------------------
	const bool bWantsBoost = bBoostInput && !bDestroyed && !bIceFrozen;
	if (bChaseBreakawayBoost && !bDestroyed && !bIceFrozen)
	{
		bBoostActive = true; // Station power is free and does not consume the driver's reserve.
		BoostRegenTimer = 0.f;
	}
	else if (bWantsBoost && BoostCharge > (bBoostActive ? 0.f : MinBoostToStart))
	{
		bBoostActive = true;
		BoostCharge = FMath::Max(0.f, BoostCharge - BoostDrainRate * DeltaTime);
		BoostRegenTimer = 0.f;
		if (BoostCharge <= 0.f)
		{
			bBoostActive = false;
		}
	}
	else
	{
		bBoostActive = false;
		BoostRegenTimer += DeltaTime;
		if (BoostRegenTimer >= BoostRegenDelay)
		{
			BoostCharge = FMath::Min(1.f, BoostCharge + BoostRegenRate * DeltaTime);
		}
	}

	// Upside-down recovery so the player is never permanently stuck.
	const FVector BodyUp = Body->GetComponentTransform().GetUnitAxis(EAxis::Z);
	const float UpDot = FVector::DotProduct(BodyUp, FVector::UpVector);
	const float Speed = Body->GetPhysicsLinearVelocity().Size();
	bUpsideDown = (UpDot < -0.1f) && (Speed < 150.f);

	if (!bDestroyed && bUpsideDown && AutoResetUpsideDownDelay > 0.f)
	{
		UpsideDownTimer += DeltaTime;
		if (UpsideDownTimer >= AutoResetUpsideDownDelay)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Vehicle auto-reset (upside down)."));
			ResetVehicle();
		}
	}
	else
	{
		UpsideDownTimer = 0.f;
	}

	// Clamp runaway spin (can happen on violent collisions / bad landings).
	const FVector AngVel = Body->GetPhysicsAngularVelocityInDegrees();
	const float AngSpeed = AngVel.Size();
	if (AngSpeed > MaxAngularSpeedDegrees && AngSpeed > KINDA_SMALL_NUMBER)
	{
		Body->SetPhysicsAngularVelocityInDegrees(AngVel * (MaxAngularSpeedDegrees / AngSpeed));
	}
}

void UCitixVehicleMovementComponent::ApplySuspensionAndTires(float DeltaTime, UPrimitiveComponent& Body)
{
	const FTransform BodyXf = Body.GetComponentTransform();
	const FVector BodyForward = BodyXf.GetUnitAxis(EAxis::X);
	const FVector BodyRight = BodyXf.GetUnitAxis(EAxis::Y);
	const FVector BodyVel = Body.GetPhysicsLinearVelocity();

	const float ForwardSpeed = FVector::DotProduct(BodyVel, BodyForward);
	const float Speed = BodyVel.Size();
	ForwardSpeedCached = ForwardSpeed;

	// --- Handbrake: drive is cut ----------------------------------------
	// Holding the handbrake kills engine drive (HandbrakeEngineForceScale, 0 by default),
	// so a drift can never accelerate. It is a brake, not a boost.
	const float EngineForceScale = bIceFrozen ? 0.f : bHandbrake ? HandbrakeEngineForceScale : 1.f;

	// Boost raises both the top speed and the engine force.
	const float EffectiveMaxSpeed = MaxSpeed * (bBoostActive ? BoostSpeedMultiplier : 1.f);
	const float EffectiveEngineForce = MaxEngineForce * (bBoostActive ? BoostForceMultiplier : 1.f);

	// --- Steering: reduced lock at speed, and less authority when not drifting ---
	const float SpeedSteerScale = FMath::GetMappedRangeValueClamped(
		FVector2D(0.f, EffectiveMaxSpeed), FVector2D(1.f, HighSpeedSteerFraction), FMath::Abs(ForwardSpeed));
	const float SteerAuthority = bHandbrake ? DriftSteerAuthority : NormalSteerAuthority;
	const float TargetSteer = SteeringInput * MaxSteerAngle * SpeedSteerScale * SteerAuthority;
	// Snappy on the way in, even faster back to centre so the car stops arcing
	// once the player releases the steering keys.
	const float SteerRate = (FMath::Abs(TargetSteer) >= FMath::Abs(CurrentSteerAngle))
		? SteerInterpSpeed : SteerReturnSpeed;
	const bool bProgressiveDrift=bHandbrake && !bIceFrozen && DriftYawRateDegrees>0.f;
	if (bProgressiveDrift) {
		const float Rate=FMath::Abs(SteeringInput)>.05f ? 8.f : SteerReturnSpeed;
		CurrentSteerAngle=FMath::Lerp(CurrentSteerAngle,TargetSteer,1.f-FMath::Exp(-Rate*DeltaTime));
	} else CurrentSteerAngle=FMath::FInterpTo(CurrentSteerAngle,TargetSteer,DeltaTime,SteerRate);
	if (FMath::Abs(CurrentSteerAngle) < 0.05f)
	{
		CurrentSteerAngle = 0.f;
	}

	// Yaw is heavily damped while driving normally and released while drifting, so
	// the car holds a line instead of spinning, but can still rotate on the handbrake.
	Body.SetAngularDamping(bHandbrake ? DriftAngularDamping : NormalAngularDamping);

	// --- Drive / brake force -------------------------------------------
	float DriveForce = 0.f;
	float BrakeAmount = BrakeInput;

	if (!bDestroyed && ThrottleInput > KINDA_SMALL_NUMBER)
	{
		const float SpeedFactor = FMath::Clamp(
			1.f - FMath::Abs(ForwardSpeed) / FMath::Max(1.f, EffectiveMaxSpeed * (AbsoluteSpeedLimit > 0.f ? 1.65f : 1.f)), 0.04f, 1.f);
		DriveForce = ThrottleInput * EffectiveEngineForce * SpeedFactor * EngineForceScale;
	}
	else if (!bDestroyed && ThrottleInput < -KINDA_SMALL_NUMBER)
	{
		if (ForwardSpeed > ReverseThresholdSpeed)
		{
			BrakeAmount = FMath::Max(BrakeAmount, -ThrottleInput);
		}
		else
		{
			DriveForce = ThrottleInput * EffectiveEngineForce * ReverseForceScale * EngineForceScale;
		}
	}

	int32 DrivenCount = 0;
	for (const FCitixWheelSetup& Wheel : Wheels)
	{
		if (Wheel.bDriven)
		{
			++DrivenCount;
		}
	}
	DrivenCount = FMath::Max(1, DrivenCount);

	// --- Global resistance + downforce ---------------------------------
	Body.AddForce(-BodyVel * DragCoefficient);
	if (Speed > 1.f)
	{
		Body.AddForce(-FVector::UpVector * (DownforceCoefficient * Speed * Speed));
	}

	// --- Per-wheel suspension & tire forces ----------------------------
	NumGroundedWheels = 0;
	MaxLateralSlipSpeed = 0.f;
	for (int32 Index = 0; Index < Wheels.Num(); ++Index)
	{
		const FCitixWheelSetup& Wheel = Wheels[Index];
		FCitixWheelRuntimeState& State = WheelStates[Index];

		const FVector WorldAttach = BodyXf.TransformPosition(Wheel.AttachOffset);
		const float RayLength = SuspensionRestLength + Wheel.Radius;
		const FVector RayStart = WorldAttach;
		const FVector RayEnd = RayStart - FVector::UpVector * RayLength;

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CitixWheelTrace), false, GetOwner());
		FHitResult Hit;
		const bool bHit = Body.GetWorld() && Body.GetWorld()->LineTraceSingleByChannel(
			Hit, RayStart, RayEnd, GroundTraceChannel, QueryParams);

		State.LocalOffset = Wheel.AttachOffset;
		State.SteerAngleDegrees = Wheel.bSteering ? CurrentSteerAngle : 0.f;
		State.bGrounded = bHit;

		if (!bHit)
		{
			State.CompressionRatio = 0.f;
			State.WorldLocation = WorldAttach - FVector::UpVector * SuspensionRestLength;
			continue;
		}

		// Suspension
		const float CurrentLength = FMath::Max(0.f, Hit.Distance - Wheel.Radius);
		const float Compression = FMath::Clamp(SuspensionRestLength - CurrentLength, 0.f, SuspensionRestLength);
		State.CompressionRatio = Compression / SuspensionRestLength;

		const FVector WheelCenter = RayStart - FVector::UpVector * CurrentLength;
		State.WorldLocation = WheelCenter;
		const float ContactVelUp = FVector::DotProduct(
			Body.GetPhysicsLinearVelocityAtPoint(WheelCenter), FVector::UpVector);
		const float SpringForce = SuspensionStiffness * Compression;
		const float DamperForce = -SuspensionDamping * ContactVelUp;
		const float SuspensionForce = FMath::Clamp(SpringForce + DamperForce, 0.f, MaxSuspensionForce);

		Body.AddForceAtLocation(FVector::UpVector * SuspensionForce, WheelCenter);
		++NumGroundedWheels;

		// Tire friction
		FVector WheelForward = FRotator(0.f, State.SteerAngleDegrees, 0.f).RotateVector(BodyForward);
		WheelForward.Z = 0.f;
		if (!WheelForward.Normalize())
		{
			WheelForward = BodyForward;
		}
		const FVector WheelRight = FVector::CrossProduct(FVector::UpVector, WheelForward).GetSafeNormal();

		const FVector ContactVel = Body.GetPhysicsLinearVelocityAtPoint(WheelCenter);
		const float FwdVel = FVector::DotProduct(ContactVel, WheelForward);
		const float LatVel = FVector::DotProduct(ContactVel, WheelRight);
		MaxLateralSlipSpeed = FMath::Max(MaxLateralSlipSpeed, FMath::Abs(LatVel));

		float LateralForce = -LatVel * LateralStiffness;
		if (bHandbrake && !Wheel.bSteering)
		{
			LateralForce *= HandbrakeGripScale;
		}

		float LongitudinalForce = 0.f;
		if (Wheel.bDriven)
		{
			LongitudinalForce += DriveForce / static_cast<float>(DrivenCount);
		}
		if (BrakeAmount > KINDA_SMALL_NUMBER)
		{
			const float BrakeMag = BrakeAmount * MaxBrakeForce / static_cast<float>(Wheels.Num());
			LongitudinalForce += -FMath::Sign(FwdVel) * BrakeMag;
		}
		else if (bHandbrake)
		{
			// The handbrake is a brake: it scrubs speed instead of driving the car.
			const float HandbrakeMag = HandbrakeBrakeScale * MaxBrakeForce / static_cast<float>(Wheels.Num());
			LongitudinalForce += -FMath::Sign(FwdVel) * HandbrakeMag;
		}

		// Friction circle: never exceed what the tire can transmit.
		const float MaxFriction = SuspensionForce * TireFrictionCoefficient;
		const float Magnitude = FMath::Sqrt(LateralForce * LateralForce + LongitudinalForce * LongitudinalForce);
		if (Magnitude > MaxFriction && Magnitude > KINDA_SMALL_NUMBER)
		{
			const float Scale = MaxFriction / Magnitude;
			LateralForce *= Scale;
			LongitudinalForce *= Scale;
		}

		Body.AddForceAtLocation(WheelForward * LongitudinalForce + WheelRight * LateralForce, WheelCenter);

		State.SpinAngleDegrees = FMath::Fmod(
			State.SpinAngleDegrees + FMath::RadiansToDegrees(FwdVel * DeltaTime / FMath::Max(1.f, Wheel.Radius)),
			360.f);
	}

	// --- Stability assist: keep the car tracking its nose -----------------
	// A pure linear tyre model is soft at small slip, which reads as the car sliding
	// wide (following its inertia). This adds a bounded lateral deceleration opposing
	// the slide, scaled by the slip angle. While drifting it is only reduced rather than
	// removed, so the car still rotates without being flung sideways.
	if (NumGroundedWheels > 0 && Speed > SlideAssistMinSpeed)
	{
		const FVector PlanarVelocity(BodyVel.X, BodyVel.Y, 0.f);
		const float PlanarSpeed = PlanarVelocity.Size();
		if (PlanarSpeed > 50.f)
		{
			const FVector VelocityDirection = PlanarVelocity / PlanarSpeed;
			const float SinSlip = FVector::DotProduct(VelocityDirection, BodyRight);
			const float ForwardAlign = FVector::DotProduct(VelocityDirection, BodyForward);
			if (ForwardAlign > 0.25f)
			{
				const float AssistScale = bHandbrake ? DriftSlideAssistScale : 1.f;
				const float AssistForce = Body.GetMass() * SlideAssistAccel * SinSlip * AssistScale;
				Body.AddForce(-BodyRight * AssistForce);
			}
		}
	}

	ApplyGroundedDriftResponse(DeltaTime, Body);
}

void UCitixVehicleMovementComponent::ApplyGroundedDriftResponse(float DeltaTime, UPrimitiveComponent& Body)
{
	const FVector DriftVelocity = Body.GetPhysicsLinearVelocity();
	const FVector PlanarVelocity(DriftVelocity.X, DriftVelocity.Y, 0.f);
	const float PlanarSpeed = PlanarVelocity.Size();
	const FVector Nose = Body.GetForwardVector().GetSafeNormal2D();
	const float ForwardSpeed = FVector::DotProduct(PlanarVelocity, Nose);
	// Fresh contacts are essential: last frame's grounded flag would assist the
	// first airborne frame after a ramp. Frozen/destroyed vehicles keep their rules.
	const bool bCanAssist = NumGroundedWheels >= 2 && !bIceFrozen && !bDestroyed
		&& DriftYawRateDegrees > 0.f && DeltaTime > 0.f
		&& Body.GetUpVector().Z > .5f && ForwardSpeed > 500.f;
	if (!bCanAssist)
	{
		DriftResponseBlend = 0.f;
		return;
	}

	const bool bTurning = bHandbrake && FMath::Abs(SteeringInput) > .05f;
	const float PreviousBlend = DriftResponseBlend;
	const float BlendAlpha = 1.f - FMath::Exp(-(bTurning ? 7.f : 10.f) * DeltaTime);
	DriftResponseBlend = FMath::Lerp(DriftResponseBlend, bTurning ? 1.f : 0.f, BlendAlpha);
	if (!bTurning && PreviousBlend < .01f)
	{
		DriftResponseBlend = 0.f;
		return;
	}

	// Fade in at low speed and reduce turn authority at racing speed. Tracking a
	// bounded target also makes countersteer cancel yaw instead of stacking torque.
	const float LowSpeedBlend = FMath::GetMappedRangeValueClamped(
		FVector2D(500.f, 1800.f), FVector2D(0.f, 1.f), PlanarSpeed);
	const float SpeedAuthority = FMath::GetMappedRangeValueClamped(
		FVector2D(3000.f, FMath::Max(3001.f, MaxSpeed)), FVector2D(1.f, .65f), PlanarSpeed);
	const float LockScale=FMath::GetMappedRangeValueClamped(FVector2D(0.f,MaxSpeed),FVector2D(1.f,HighSpeedSteerFraction),FMath::Abs(ForwardSpeed));
	const float ProgressiveInput=FMath::Clamp(CurrentSteerAngle/FMath::Max(1.f,MaxSteerAngle*DriftSteerAuthority*LockScale),-1.f,1.f);
	const float TargetYaw = bTurning ? ProgressiveInput * FMath::Clamp(DriftYawRateDegrees, 0.f, 120.f) * LowSpeedBlend * SpeedAuthority : 0.f;
	FVector AngularVelocity = Body.GetPhysicsAngularVelocityInDegrees();
	// Steering release needs more damping than entry: merely fading the yaw
	// controller out would leave residual spin while the handbrake is held.
	const float YawResponseRate = bTurning ? 8.f : 16.f;
	const float YawAlpha = 1.f - FMath::Exp(-YawResponseRate * DeltaTime);
	AngularVelocity.Z = FMath::Lerp(AngularVelocity.Z, TargetYaw, YawAlpha * (bTurning ? DriftResponseBlend : PreviousBlend));
	Body.SetPhysicsAngularVelocityInDegrees(AngularVelocity);

	if (bTurning && PlanarSpeed > KINDA_SMALL_NUMBER)
	{
		const FVector Direction = PlanarVelocity / PlanarSpeed;
		const float SlipAngle = FMath::Atan2(FVector::CrossProduct(Direction, Nose).Z, FVector::DotProduct(Direction, Nose));
		const float Weight = DriftResponseBlend * LowSpeedBlend;
		const float AlignAlpha = 1.f - FMath::Exp(-FMath::Max(0.f, DriftAlignmentRate) * Weight * DeltaTime);
		const float Rotation = FMath::Clamp(SlipAngle * AlignAlpha,
			-FMath::DegreesToRadians(90.f) * DeltaTime, FMath::DegreesToRadians(90.f) * DeltaTime);
		// Exact rotation + exponential scrub is independent of body mass, bounded
		// at every timestep, and cannot inject planar kinetic energy. Suspension and
		// collision Z velocity are copied exactly; pitch/roll were untouched above.
		FVector Aligned = FQuat(FVector::UpVector, Rotation).RotateVector(PlanarVelocity);
		Aligned *= FMath::Exp(-FMath::Max(0.f, DriftSpeedScrubRate) * Weight * DeltaTime);
		Aligned.Z = DriftVelocity.Z;
		Body.SetPhysicsLinearVelocity(Aligned);
	}
}

float UCitixVehicleMovementComponent::GetForwardSpeed() const
{
	return ForwardSpeedCached;
}

float UCitixVehicleMovementComponent::GetSpeedKmh() const
{
	if (const UPrimitiveComponent* Body=GetBodyPrimitive(); Body && Body->IsSimulatingPhysics()) return Body->GetPhysicsLinearVelocity().Size2D()*.036f;
 return FMath::Abs(ForwardSpeedCached) * 0.036f;
}

void UCitixVehicleMovementComponent::ResetVehicle()
{
	UPrimitiveComponent* Body = GetBodyPrimitive();
	if (!Body)
	{
		return;
	}

	FVector Location = Body->GetComponentLocation();

	if (GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>()) {
  FTransform Safe;
  if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Location,FVector(240,110,85),Body->GetComponentRotation().Yaw,GetOwner(),Safe)) {
   const ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(GetOwner());
   if (!Car || !Car->bHasDryPose) return;
   if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Car->LastDryPose.GetLocation(),FVector(240,110,85),Car->LastDryPose.Rotator().Yaw,GetOwner(),Safe)) return;
  }
  Body->SetWorldLocationAndRotation(Safe.GetLocation(),FRotator(0,Safe.Rotator().Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
  Body->SetPhysicsLinearVelocity(FVector::ZeroVector); Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
  UpsideDownTimer=0; bUpsideDown=false; return;
 }
 // Find the ground below and place the car above it.
	if (UWorld* World = GetWorld())
	{
		const FVector Start = Location + FVector::UpVector * 400.f;
		const FVector End = Location - FVector::UpVector * 3000.f;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CitixResetTrace), false, GetOwner());
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, End, GroundTraceChannel, QueryParams))
		{
			Location.Z = Hit.ImpactPoint.Z + 90.f;
		}
		else
		{
			Location.Z += 150.f;
		}
	}

	const float Yaw = Body->GetComponentRotation().Yaw;
	Body->SetWorldLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Body->WakeAllRigidBodies();

	UpsideDownTimer = 0.f;
	bUpsideDown = false;
}
