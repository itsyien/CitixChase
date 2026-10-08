// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CitixVehiclePawn.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Chase/CitixChaseRules.h"
#include "City/CitixCityGenerator.h"
#include "Traffic/CitixTrafficVehicle.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "World/CitixTimeOfDay.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Pedestrian/CitixPedestrian.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Player/CitixPlayerState.h"
#include "Character/CitixOnFootPawn.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "Vehicle/CitixBoostTrailComponent.h"
#include "Vehicle/CitixAirFlowComponent.h"
#include "Vehicle/CitixEngineAudioComponent.h"
#include "Vehicle/CitixDriftSmokeComponent.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Sandbox/CitixHitSpark.h"
#include "Net/UnrealNetwork.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "Citix.h"

ACitixVehiclePawn::ACitixVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
 PrimaryActorTick.TickGroup = TG_PostPhysics;

	// Multiplayer stage 1: server simulates, everyone interpolates.
	// Player pawns are always relevant: a shared-world driving game must never
	// cull another driver (bandwidth for 2-4 cars is trivial).
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	// Replicated rotation defaults to one byte per axis (~1.4 deg per step), which
	// makes every client's car turn in visible steps; location/velocity default to
	// whole units. Tighten all three so interpolated copies read smoothly.
	{
		FRepMovement RepMove = GetReplicatedMovement();
		RepMove.RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
		RepMove.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
		RepMove.VelocityQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
		SetReplicatedMovement(RepMove);
	}
	// Movement snapshots share the link with everything else; 60 Hz here is
	// what keeps remote cars smooth (arrivals were ~14 Hz at 30 Hz update).
	SetNetUpdateFrequency(60.f);
	// --- Physics chassis -----------------------------------------------
	BodyCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("BodyCollision"));
	SetRootComponent(BodyCollision);
	BodyCollision->SetBoxExtent(FVector(230.f, 95.f, 55.f));
	BodyCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyCollision->SetCollisionObjectType(ECC_PhysicsBody);
	BodyCollision->SetCollisionResponseToAllChannels(ECR_Block);
	BodyCollision->SetCanEverAffectNavigation(false);
	BodyCollision->SetUseCCD(true);

	CarVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CarVisualRoot"));
	CarVisualRoot->SetupAttachment(BodyCollision);
	for (int32 Index=0; Index<2; ++Index)
	{
		auto* Light=CreateDefaultSubobject<USpotLightComponent>(*FString::Printf(TEXT("Headlight%d"),Index));
		Light->SetupAttachment(CarVisualRoot);
		Light->SetRelativeLocation(FVector(232.f,Index==0 ? -65.f : 65.f,5.f));
		Light->SetRelativeRotation(FRotator(-5.f,0.f,0.f));
		Light->SetLightColor(FLinearColor(.88f,.94f,1.f));
		Light->IntensityUnits=ELightUnits::Lumens;
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(5500.f);
		Light->SetInnerConeAngle(15.f); Light->SetOuterConeAngle(32.f);
		Light->SetCastShadows(false);
		Headlights.Add(Light);
	}

	// --- Camera ---------------------------------------------------------
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(BodyCollision);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = ChaseArmLength;
	CameraBoom->SocketOffset = ChaseSocketOffset;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 9.f;
	CameraBoom->bEnableCameraRotationLag = false;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ChaseCamera->bUsePawnControlRotation = false;
	ChaseCamera->FieldOfView = 82.f;

	ChaseTrackerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ChaseTrackerRoot"));
	ChaseTrackerRoot->SetupAttachment(BodyCollision);
	ChaseTrackerRoot->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UStaticMeshComponent* Segment = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("ChaseTrackerRing%d"), Index));
		Segment->SetupAttachment(ChaseTrackerRoot);
		Segment->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete));
		Segment->SetMaterial(0, FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool));
		const float Angle = Index * 45.f;
		const float Radians = FMath::DegreesToRadians(Angle);
		Segment->SetRelativeLocation(FVector(FMath::Cos(Radians) * 250.f, FMath::Sin(Radians) * 250.f, 0.f));
		Segment->SetRelativeRotation(FRotator(0.f, Angle + 90.f, 0.f));
		Segment->SetRelativeScale3D(FVector(0.75f, 0.10f, 0.08f));
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCastShadow(false);
		Segment->SetCanEverAffectNavigation(false);
		Segment->SetVisibility(false, true);
		ChaseTrackerSegments.Add(Segment);
	}
	ChaseTrackerArrow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChaseTrackerArrow"));
	ChaseTrackerArrow->SetupAttachment(ChaseTrackerRoot);
	ChaseTrackerArrow->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::Spire));
	ChaseTrackerArrow->SetMaterial(0, FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool));
	ChaseTrackerArrow->SetRelativeLocation(FVector(250.f, 0.f, 0.f));
	ChaseTrackerArrow->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	ChaseTrackerArrow->SetRelativeScale3D(FVector(0.32f, 0.32f, 0.65f));
	ChaseTrackerArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChaseTrackerArrow->SetCastShadow(false);
	ChaseTrackerArrow->SetCanEverAffectNavigation(false);
	ChaseTrackerArrow->SetVisibility(false, true);

	VehicleMovement = CreateDefaultSubobject<UCitixVehicleMovementComponent>(TEXT("VehicleMovement"));
	DriftSmoke = CreateDefaultSubobject<UCitixDriftSmokeComponent>(TEXT("DriftSmoke"));
	BoostTrail = CreateDefaultSubobject<UCitixBoostTrailComponent>(TEXT("BoostTrail"));
	AirFlow = CreateDefaultSubobject<UCitixAirFlowComponent>(TEXT("AirFlow"));
	EngineAudio = CreateDefaultSubobject<UCitixEngineAudioComponent>(TEXT("EngineAudio"));

	// --- Input ----------------------------------------------------------
	DrivingContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("DrivingContext"));

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* Action = CreateDefaultSubobject<UInputAction>(Name);
		Action->ValueType = Type;
		return Action;
	};

	ThrottleAction = MakeAction(TEXT("IA_Throttle"), EInputActionValueType::Axis1D);
	SteeringAction = MakeAction(TEXT("IA_Steering"), EInputActionValueType::Axis1D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	HandbrakeAction = MakeAction(TEXT("IA_Handbrake"), EInputActionValueType::Boolean);
	ResetAction = MakeAction(TEXT("IA_Reset"), EInputActionValueType::Boolean);
	CameraToggleAction = MakeAction(TEXT("IA_CameraToggle"), EInputActionValueType::Boolean);
	ExitVehicleAction = MakeAction(TEXT("IA_ExitVehicle"), EInputActionValueType::Boolean);
	BoostAction = MakeAction(TEXT("IA_Boost"), EInputActionValueType::Boolean);

	if (DrivingContext)
	{
		DrivingContext->MapKey(ThrottleAction, EKeys::W);
		DrivingContext->MapKey(ThrottleAction, EKeys::Up);
		DrivingContext->MapKey(ThrottleAction, EKeys::Gamepad_LeftY);

		// Modifiers must be default subobjects (NewObject is illegal in a constructor).
		int32 NegateIndex = 0;
		auto MapNegated = [this, &NegateIndex](UInputAction* Action, const FKey& Key)
		{
			FEnhancedActionKeyMapping& Mapping = DrivingContext->MapKey(Action, Key);
			const FName ModifierName = *FString::Printf(TEXT("NegateModifier_%d"), NegateIndex++);
			if (UInputModifierNegate* Negate = CreateDefaultSubobject<UInputModifierNegate>(ModifierName))
			{
				Negate->bX = true;
				Negate->bY = false;
				Negate->bZ = false;
				Mapping.Modifiers.Add(Negate);
			}
		};

		MapNegated(ThrottleAction, EKeys::S);
		MapNegated(ThrottleAction, EKeys::Down);

		DrivingContext->MapKey(SteeringAction, EKeys::D);
		DrivingContext->MapKey(SteeringAction, EKeys::Right);
		MapNegated(SteeringAction, EKeys::A);
		MapNegated(SteeringAction, EKeys::Left);
		DrivingContext->MapKey(SteeringAction, EKeys::Gamepad_LeftX);

		DrivingContext->MapKey(LookAction, EKeys::Mouse2D);
		DrivingContext->MapKey(HandbrakeAction, EKeys::SpaceBar);
		DrivingContext->MapKey(HandbrakeAction, EKeys::Gamepad_FaceButton_Bottom);
		DrivingContext->MapKey(ResetAction, EKeys::R);
		DrivingContext->MapKey(CameraToggleAction, EKeys::C);
		DrivingContext->MapKey(ExitVehicleAction, EKeys::F);
		// Boost while driving. (Shift sprints on foot, but that is a different pawn.)
		DrivingContext->MapKey(BoostAction, EKeys::LeftShift);
		DrivingContext->MapKey(BoostAction, EKeys::Gamepad_LeftTrigger);
	}
}

void ACitixVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	if (BodyCollision)
	{
		// Chaos simulates on the authority only; remote copies interpolate.
		BodyCollision->SetSimulatePhysics(HasAuthority());
		// Mass is set from the car type in ApplyCarPerformance (via RebuildCarVisual).
		BodyCollision->SetCenterOfMass(FVector(0.f, 0.f, -30.f));
	}

	if (!HasAuthority())
	{
		// Seed the interpolation buffer from the spawn bunch (OnRep does not fire for it).
		PushRemoteSnapshot(GetActorLocation(), GetActorRotation(), FVector::ZeroVector);
	}

	// --- Build the visual car -------------------------------------------
	RebuildCarVisual();

	ApplyCameraMode();

	// --- Damage, crowd interaction and the destruction fireball -------------
	if (BodyCollision)
	{
		BodyCollision->SetNotifyRigidBodyCollision(true);
		BodyCollision->OnComponentHit.AddDynamic(this, &ACitixVehiclePawn::OnChassisHit);
	}

	if (!ExplosionSphere)
	{
		ExplosionSphere = NewObject<UStaticMeshComponent>(this, TEXT("ExplosionSphere"));
		if (ExplosionSphere)
		{
			ExplosionSphere->SetupAttachment(BodyCollision);
			ExplosionSphere->SetMobility(EComponentMobility::Movable);
			ExplosionSphere->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::Lamp));
			if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveWarm))
			{
				ExplosionSphere->SetMaterial(0, Material);
			}
			ExplosionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ExplosionSphere->SetCastShadow(false);
			ExplosionSphere->SetCanEverAffectNavigation(false);
			ExplosionSphere->RegisterComponent();
			ExplosionSphere->SetVisibility(false, true);
		}
	}

	auto MakeBlastPart = [this](const TCHAR* Name, ECitixSurface Surface)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, FName(Name));
		if (!Part)
		{
			return (UStaticMeshComponent*)nullptr;
		}
		Part->SetupAttachment(BodyCollision);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::Lamp));
		if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(Surface))
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetCanEverAffectNavigation(false);
		Part->RegisterComponent();
		Part->SetVisibility(false, true);
		return Part;
	};

	if (!BlastFlash)
	{
		BlastFlash = MakeBlastPart(TEXT("BlastFlash"), ECitixSurface::WindowWhite);
	}
	if (!BlastSmoke)
	{
		BlastSmoke = MakeBlastPart(TEXT("BlastSmoke"), ECitixSurface::PropDark);
	}
	if (!BlastRing)
	{
		BlastRing = MakeBlastPart(TEXT("BlastRing"), ECitixSurface::EmissiveCool);
	}

	for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
	{
		CachedPedestrians = *It;
		break;
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Vehicle ready: pedestrian system %s."),
		CachedPedestrians.IsValid() ? TEXT("found") : TEXT("NOT FOUND"));
}

void ACitixVehiclePawn::SetCarAppearance(ECitixCarType InType, FLinearColor InColor)
{
	CarType = InType;
	PaintColor = InColor;
	RebuildCarVisual();
}

void ACitixVehiclePawn::RebuildCarVisual()
{
	// Drop the previous visual car (used when the player swaps into another vehicle).
	for (UStaticMeshComponent* Part : CarVisual.Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	CarVisual = FCitixCarVisual();

	if (!CarVisualRoot)
	{
		return;
	}

	CarVisual = FCitixCarLibrary::BuildCar(this, CarVisualRoot, CarType, PaintColor,
		/*bCollision*/ false, /*bIncludeWheels*/ true, /*bHighDetail*/ true);
	TArray<FCitixCarPartDesc> LampParts;
	FCitixCarLibrary::BuildPartDescs(CarType,LampParts);
	int32 LampIndex=0;
	for (const auto& Part : LampParts)
		if (Part.Surface==ECitixSurface::HeadLight && LampIndex<Headlights.Num())
			Headlights[LampIndex++]->SetRelativeLocation(Part.Center+FVector(7.f,0.f,0.f));

	// Adopt the type's mass, power, hull and drift character. Done first because the
	// resting-suspension height below depends on the mass.
	ApplyCarPerformance();

	// Match the physics wheels to the visual car: wheelbase, track and radius differ
	// per car type, so this must be redone whenever the car changes.
	if (VehicleMovement && CarVisual.Wheels.Num() == 4)
	{
		const float Radius = CarVisual.WheelRadius;
		TArray<FCitixWheelSetup> Wheels;
		Wheels.SetNum(4);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FCitixWheelSetup& Wheel = Wheels[Index];
			Wheel.AttachOffset = FVector(CarVisual.WheelCenters[Index].X, CarVisual.WheelCenters[Index].Y, 0.f);
			Wheel.Radius = Radius;
			Wheel.bSteering = (Index < 2);
			Wheel.bDriven = true;
		}
		VehicleMovement->ConfigureWheels(Wheels);
	}

	// Place car-space Z=0 on the ground, given the resting suspension height.
	if (VehicleMovement)
	{
		const float RestCompression = FMath::Clamp(
			(VehicleMassKg * 980.f * 0.25f) / FMath::Max(1.f, VehicleMovement->SuspensionStiffness),
			0.f, VehicleMovement->SuspensionRestLength * 0.6f);
		GroundOffset = (VehicleMovement->SuspensionRestLength - RestCompression) + CarVisual.WheelRadius;
		CarVisualRoot->SetRelativeLocation(FVector(0.f, 0.f, -GroundOffset));
	}
}

void ACitixVehiclePawn::ApplyCarPerformance()
{
	bChasePerformanceApplied = false;
	const FCitixCarPerformance Performance = FCitixCarLibrary::GetPerformance(CarType);
	VehicleMassKg = Performance.MassKg;

	if (BodyCollision)
	{
		BodyCollision->SetMassOverrideInKg(NAME_None, VehicleMassKg, true);
	}
	if (VehicleMovement)
	{
		VehicleMovement->ApplyCarPerformance(Performance);
	}

	UE_LOG(LogCitix, Log,
		TEXT("[Citix] Car '%s': %.0f kg, %.0f km/h top, hull %.0f, handbrake grip %.2f."),
		FCitixCarLibrary::GetTypeName(CarType), VehicleMassKg, Performance.MaxSpeed * 0.036f,
		Performance.MaxHealth, Performance.HandbrakeGripScale);
}

void ACitixVehiclePawn::SetBodyColor(FLinearColor Color)
{
	// Repaint only the parts that carry the paint colour.
	if (UMaterialInterface* Paint = FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface::CarPaint, Color))
	{
		for (int32 PartIndex : CarVisual.PaintedParts)
		{
			if (CarVisual.Parts.IsValidIndex(PartIndex) && CarVisual.Parts[PartIndex])
			{
				CarVisual.Parts[PartIndex]->SetMaterial(0, Paint);
			}
		}
	}
}

void ACitixVehiclePawn::ApplyChasePerformance(bool bRunner)
{
	bChaseRunner = bRunner;
	bChasePerformanceApplied = true;
	const FCitixCarPerformance Base = FCitixCarLibrary::GetPerformance(ECitixCarType::Sedan);
 VehicleMassKg = Base.MassKg;
 if (BodyCollision) { BodyCollision->SetMassOverrideInKg(NAME_None, VehicleMassKg, true); BodyCollision->SetLinearDamping(.01f); }
 TArray<FCitixWheelSetup> ChaseWheels; ChaseWheels.SetNum(4);
 for (int32 I=0; I<4; ++I) { ChaseWheels[I].AttachOffset=FVector(I<2 ? 130.f : -130.f,I%2 ? 75.f : -75.f,0); ChaseWheels[I].Radius=34.f; ChaseWheels[I].bSteering=I<2; ChaseWheels[I].bDriven=true; }
 VehicleMovement->ConfigureWheels(ChaseWheels);
 const float Compression=FMath::Clamp(Base.MassKg*980.f*.25f/FMath::Max(1.f,Base.MassKg*VehicleMovement->SuspensionStiffnessPerKg),0.f,VehicleMovement->SuspensionRestLength*.6f);
 GroundOffset=VehicleMovement->SuspensionRestLength-Compression+34.f;
 if (CarVisualRoot) CarVisualRoot->SetRelativeLocation(FVector(0,0,-GroundOffset));
 // Chase bodies are cosmetic: normalize grip, drag, braking and durability once.

 VehicleMovement->SetMaxHealthPreservingFraction(100.f);
 VehicleMovement->MaxBrakeForce = Base.BrakeForce;
 VehicleMovement->DragCoefficient = 48.f;
 VehicleMovement->TireFrictionCoefficient = 1.3f;
 VehicleMovement->LateralStiffness = Base.LateralStiffness;
 VehicleMovement->HandbrakeGripScale = Base.HandbrakeGripScale;
 VehicleMovement->SuspensionStiffness = Base.MassKg * VehicleMovement->SuspensionStiffnessPerKg;
 VehicleMovement->SuspensionDamping = Base.MassKg * VehicleMovement->SuspensionDampingPerKg;
 VehicleMovement->MaxSuspensionForce = Base.MassKg * VehicleMovement->MaxSuspensionForcePerKg;
 VehicleMovement->MaxEngineForce = Base.EngineForce * FCitixChaseRules::EngineScale(bRunner, 0.f);
 VehicleMovement->MaxSpeed = FCitixChaseRules::SpeedLimit(bRunner);
 VehicleMovement->AbsoluteSpeedLimit = VehicleMovement->MaxSpeed;
 VehicleMovement->BoostSpeedMultiplier = 1.f;
 if (!bRunner) { PaintColor = FLinearColor(.9f,.025f,.04f); SetBodyColor(PaintColor); }
	VehicleMovement->SteerInterpSpeed = bRunner ? 32.f : 16.f;
	VehicleMovement->NormalSteerAuthority = bRunner ? .98f : .62f;
 VehicleMovement->HighSpeedSteerFraction=bRunner ? .82f : .55f;
 VehicleMovement->SteerReturnSpeed=bRunner ? 36.f : 30.f;
 VehicleMovement->MaxSteerAngle=bRunner ? 36.f : 34.f;
 VehicleMovement->TireFrictionCoefficient=bRunner ? 1.65f : 1.3f;
 VehicleMovement->LateralStiffness=Base.LateralStiffness*(bRunner ? 1.35f : 1.f);
	VehicleMovement->DriftSteerAuthority = 1.25f;
	VehicleMovement->DriftSlideAssistScale = .75f;
	VehicleMovement->DriftAngularDamping = .65f;
	VehicleMovement->DriftYawRateDegrees = 100.f;
	VehicleMovement->DriftAlignmentRate = 4.5f;
	VehicleMovement->DriftSpeedScrubRate = .12f;
}

void ACitixVehiclePawn::OnRep_Occupied()
{
	SetOccupied(bOccupied);
}

void ACitixVehiclePawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	AddDrivingMappingContext();
	if (VehicleMovement) { VehicleMovement->SetBrakeInput(0.f); VehicleMovement->SetHandbrake(false); }
}

void ACitixVehiclePawn::SetOccupied(bool bNewOccupied)
{
	bOccupied = bNewOccupied;
	NetThrottle=NetSteer=0.f;
	bNetHandbrake=bNetBoost=false;
	if (!VehicleMovement)
	{
		return;
	}

	if (bOccupied)
	{
		VehicleMovement->SetBoostInput(false);
		// Release the park brake applied while the car was left standing, otherwise
		// the vehicle can never pull away after being re-entered.
		VehicleMovement->SetThrottleInput(0.f);
		VehicleMovement->SetSteeringInput(0.f);
		VehicleMovement->SetBrakeInput(0.f);
		VehicleMovement->SetHandbrake(false);
		bThrottleFromInput = false;
		bSteeringFromInput = false;
		ThrottleInputAge = 0.f;
		SteeringInputAge = 0.f;
	}
	else
	{
		VehicleMovement->SetBoostInput(false);
		// Park it: no inputs, brakes on, handbrake up.
		VehicleMovement->SetThrottleInput(0.f);
		VehicleMovement->SetSteeringInput(0.f);
		VehicleMovement->SetBrakeInput(1.f);
		VehicleMovement->SetHandbrake(true);
	}
}

FTransform ACitixVehiclePawn::GetExitTransform() const
{
	const FVector CarLocation = GetActorLocation();
	const FVector Right = GetActorRightVector();
	const FVector ExitLocation = CarLocation + Right * 190.f + FVector(0.f, 0.f, GroundOffset + 20.f);
	return FTransform(FRotator(0.f, GetActorRotation().Yaw, 0.f), ExitLocation, FVector::OneVector);
}

bool ACitixVehiclePawn::CanAcceptDriveInput() const
{
 if (const auto* PC=Cast<ACitixDrivingPlayerController>(GetController()); PC && PC->IsLocalController() && PC->IsSettingsMenuOpen()) return false;
 const auto* State=GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr;
 return !State || State->Phase==ECitixChasePhase::Pursuit;
}

void ACitixVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
 const auto* DriveState=GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr;
 if (DriveState && !CanAcceptDriveInput()) {
  NetThrottle=NetSteer=0.f; bNetHandbrake=bNetBoost=false;
  if (VehicleMovement) { VehicleMovement->SetThrottleInput(0); VehicleMovement->SetSteeringInput(0); VehicleMovement->SetHandbrake(false); VehicleMovement->SetBoostInput(false); }
 }
 if (HasAuthority() && BodyCollision) {
  const bool Freeze=DriveState && DriveState->Phase==ECitixChasePhase::Countdown && bOccupied;
  if (Freeze) {
   if (!bCountdownFrozen) { CountdownHoldPose=GetActorTransform(); BodyCollision->SetPhysicsLinearVelocity(FVector::ZeroVector); BodyCollision->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector); BodyCollision->SetSimulatePhysics(false); bCountdownFrozen=true; }
   SetActorTransform(CountdownHoldPose,false,nullptr,ETeleportType::TeleportPhysics);
  } else if (bCountdownFrozen) { BodyCollision->SetSimulatePhysics(true); BodyCollision->SetPhysicsLinearVelocity(FVector::ZeroVector); BodyCollision->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector); bCountdownFrozen=false; }
 }

	if (const auto* Clock=ACitixTimeOfDay::Find(GetWorld()))
		for (USpotLightComponent* Light : Headlights) Light->SetIntensity(IsDisplayDestroyed() ? 0.f : 900.f*Clock->GetNightAlpha());
 if (BodyCollision && BodyCollision->IsSimulatingPhysics() && VehicleMovement->AbsoluteSpeedLimit>0.f)
  BodyCollision->SetPhysicsLinearVelocity(FCitixChaseRules::LimitVelocity(BodyCollision->GetPhysicsLinearVelocity(),VehicleMovement->AbsoluteSpeedLimit));
 if (GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>() && BodyCollision && BodyCollision->IsSimulatingPhysics()) {
  FTransform Surface;
  const FVector Velocity=BodyCollision->GetPhysicsLinearVelocity();
  const FVector Extent=BodyCollision->GetUnscaledBoxExtent();
  const bool Dry=ACitixCityGenerator::ValidateChaseSurface(GetWorld(),GetActorLocation(),FVector(Extent.X,Extent.Y,85),GetActorRotation().Yaw,this,Surface,false);
  FTransform Ahead;
  const bool SafeAhead=Dry && ACitixCityGenerator::ValidateChaseSurface(GetWorld(),GetActorLocation()+Velocity*.08f,FVector(Extent.X,Extent.Y,85),GetActorRotation().Yaw,this,Ahead,false);
  if (SafeAhead) { LastDryPose=GetActorTransform(); bHasDryPose=true; }
  else if (bHasDryPose) {
   ++ShoreRestores;
   SetActorTransform(LastDryPose,false,nullptr,ETeleportType::TeleportPhysics);
   BodyCollision->SetPhysicsLinearVelocity(FVector::ZeroVector);
   BodyCollision->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
  }
 }
	if (GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>())
	{
		AController* Driver = OwningController ? OwningController.Get() : GetController();
		if (const ACitixChasePlayerState* ChasePlayer = Driver ? Driver->GetPlayerState<ACitixChasePlayerState>() : GetPlayerState<ACitixChasePlayerState>())
		{
			const bool bRunner = ChasePlayer->ChaseRole == ECitixChaseRole::Runner;
			if (!bChasePerformanceApplied || bChaseRunner != bRunner)
			{
				ApplyChasePerformance(bRunner);
				bChasePerformanceApplied = true;
			}
			const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>();
			const bool bStationActive = Chase->Phase == ECitixChasePhase::Pursuit && Chase->BreakawayUntil > Chase->GetServerWorldTimeSeconds();
			const bool bBreakaway = bRunner && bStationActive;
			const float Clock=Chase->GetServerWorldTimeSeconds();
   const float Slow=bRunner ? 1.f : FCitixChaseRules::GateSlowScale(Clock,ChasePlayer->GateSlowStartedAt,ChasePlayer->GateSlowUntil);
   if (bOccupied && (bBreakaway || (!bRunner && (Clock<ChasePlayer->GateSlowUntil)))) {
    PursuitPulseTimer+=DeltaSeconds;
    if (PursuitPulseTimer>=.15f) { PursuitPulseTimer=0.f; ACitixHitSpark::SpawnPursuitPulse(GetWorld(),GetActorLocation(),GetActorForwardVector()*FMath::Max(100.f,GetDisplaySpeedKmh()/.036f),bBreakaway); }
   } else PursuitPulseTimer=0.f;
			VehicleMovement->bChaseBreakawayBoost = bBreakaway;
			VehicleMovement->MaxSpeed = VehicleMovement->AbsoluteSpeedLimit = FCitixChaseRules::SpeedLimit(bRunner)*Slow;
   if (bRunner && ChasePlayer->FrozenUntil>Clock) VehicleMovement->AbsoluteSpeedLimit=FMath::Max(.01f,ChasePlayer->FrozenSpeedLimit);
			const FCitixCarPerformance Base = FCitixCarLibrary::GetPerformance(ECitixCarType::Sedan);
   const float Speed = BodyCollision ? BodyCollision->GetPhysicsLinearVelocity().Size2D()*.036f : 0.f;
   VehicleMovement->MaxEngineForce = Base.EngineForce * FCitixChaseRules::EngineScale(bRunner, Speed) * Slow;
			VehicleMovement->BoostRegenRate = bBreakaway ? .5f : .14f;
		}
	}
	if (!HasAuthority() && BodyCollision && VehicleMovement)
	{
		const ACitixChaseGameState* Chase = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr;
		const bool bPredict = IsLocallyControlled() && bOccupied && !NetState.bDestroyed
			&& Chase && Chase->Phase == ECitixChasePhase::Pursuit;
		if (BodyCollision->IsSimulatingPhysics() != bPredict)
		{
			VehicleMovement->ReconcileBoostCharge(NetState.BoostCharge);
			BodyCollision->SetSimulatePhysics(bPredict);
		}
		else if (FMath::Abs(VehicleMovement->GetBoostCharge() - NetState.BoostCharge) > .15f)
			VehicleMovement->ReconcileBoostCharge(NetState.BoostCharge);
		if (bPredict)
		{
			VehicleMovement->SetThrottleInput(NetThrottle);
			VehicleMovement->SetSteeringInput(NetSteer);
			VehicleMovement->SetHandbrake(bNetHandbrake);
			VehicleMovement->SetBoostInput(bNetBoost);
		}
	}

	if (!VehicleMovement || !CameraBoom)
	{
		return;
	}
	if (HasAuthority())
	{
		if (const ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr; Chase && !Chase->CanAcceptDriveInput())
		{
			VehicleMovement->SetThrottleInput(0.f);
			VehicleMovement->SetSteeringInput(0.f);
			VehicleMovement->SetHandbrake(false);
			VehicleMovement->SetBoostInput(false);
		}
	}
	// Headless/render verification uses the same drive RPC, without window focus.
	if (IsLocallyControlled() && FParse::Param(FCommandLine::Get(), TEXT("CitixNetDrift")))
	{
		if (const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>(); Chase && Chase->Phase == ECitixChasePhase::Pursuit)
		{
			NetThrottle = 1.f; NetSteer = .2f;
			bNetHandbrake = FMath::Fmod(Chase->PhaseSecondsRemaining, 8.f) < 2.f;
			bNetBoost = !bNetHandbrake;
			ThrottleInputAge = SteeringInputAge = 0.f;
			VehicleMovement->SetThrottleInput(NetThrottle);
			VehicleMovement->SetSteeringInput(NetSteer);
			VehicleMovement->SetHandbrake(bNetHandbrake);
			VehicleMovement->SetBoostInput(bNetBoost);
		}
	}
	UpdateChaseTracker();

	if (CrashDamageCooldown > 0.f)
	{
		CrashDamageCooldown = FMath::Max(0.f, CrashDamageCooldown - DeltaSeconds);
	}
	ResetCooldownTimer = FMath::Max(0.f, ResetCooldownTimer - DeltaSeconds);
	const bool bChaseWreck = GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>()
		&& (HasAuthority() ? VehicleMovement->IsDestroyed() : NetState.bDestroyed);
	if (bChaseWreck)
	{
		// Chase wrecks are disabled evidence, not the sandbox's explosion/repair loop.
		if (HasAuthority()) PublishNetState();
		return;
	}

	// --- Destruction: fireball, area damage, then repair so the player is not stuck ---
	// Server decides and repairs; remote copies mirror the visuals only.
	if (HasAuthority())
	{
		if (VehicleMovement->IsDestroyed())
		{
			if (!bExploding && DestroyedTimer <= 0.f)
			{
				TriggerExplosion();
			}
			DestroyedTimer += DeltaSeconds;
			UpdateExplosion(DeltaSeconds);

			if (DestroyedTimer >= RepairDelay)
			{
				VehicleMovement->RepairFull();
				ResetVehicle();
				DestroyedTimer = 0.f;
				bExploding = false;
				HideExplosionVisuals();
			}
			PublishNetState();
			return;
		}
	}
	else if (NetState.bDestroyed)
	{
		if (!bExploding && DestroyedTimer <= 0.f)
		{
			bExploding = true;
			ExplosionTimer = 0.f;
			DestroyedTimer = 0.f;
		}
		DestroyedTimer += DeltaSeconds;
		UpdateExplosion(DeltaSeconds);
		return;
	}
	else if (bExploding)
	{
		bExploding = false;
		DestroyedTimer = 0.f;
		HideExplosionVisuals();
	}

	// --- Run people over when moving fast enough ---
	if (CachedPedestrians.IsValid())
	{
		const float Speed = GetVelocity().Size();
		if (GetVelocity().SizeSquared2D() > FMath::Square(RunOverMinSpeed))
		{
			FVector HitImpulse = GetVelocity();
			HitImpulse.Z = 0.f;
			HitImpulse = HitImpulse.GetSafeNormal()
				* FMath::Clamp(Speed * 0.9f, 400.f, 2600.f);
			HitImpulse.Z = FMath::FRandRange(150.f, 420.f);

			// Swept against each pedestrian's own hitbox, from last frame's position to this
			// one. The car can cross a whole body between two frames at speed, so a radius
			// bubble around the car either misses people or has to be so large that it
			// catches people the car never touched.
			if (bHasPreviousLocation)
			{
				const float SweepLength = FVector::Dist2D(PreviousLocation, GetActorLocation());
				// Guard against a teleport (reset, respawn): a huge segment would sweep
				// through everyone between the two points.
				if (SweepLength < 2000.f)
				{
					const int32 Hit = CachedPedestrians->HitPedestriansAlongSweep(PreviousLocation,
						GetActorLocation(), RunOverRadius, HitImpulse, Speed);
					if (Hit > 0)
					{
						UE_LOG(LogCitix, Log, TEXT("[Citix] Car contact: %d pedestrian(s) at %.0f km/h."),
							Hit, Speed * 0.036f);
					}
				}
			}
		}
	}

	// --- Run PLAYERS over (server only): same sweep, on-foot drivers only ----
	// Drivers are safe inside their cars; walkers are not. Per-victim ram
	// invulnerability stops a parked car farming its victim.
	// (Runs before PreviousLocation is refreshed below.)
	if (HasAuthority() && bHasPreviousLocation
		&& (GetVelocity().SizeSquared2D() > FMath::Square(RunOverMinSpeed)
			|| (GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() && LastSpeed * .036f > 40.f)))
	{
		const float SweepLength = FVector::Dist2D(PreviousLocation, GetActorLocation());
		if (SweepLength > KINDA_SMALL_NUMBER && SweepLength < 2000.f)
		{
			RamPlayersAlongSweep(PreviousLocation, GetActorLocation());
		}
	}

	PreviousLocation = GetActorLocation();
	bHasPreviousLocation = true;

	// --- Camera: yaw follows the car, mouse look is a temporary offset ---
	LookReturnTimer += DeltaSeconds;
	if (LookReturnTimer > 0.8f && !FMath::IsNearlyZero(LookYawOffset))
	{
		LookYawOffset = FMath::FInterpTo(LookYawOffset, 0.f, DeltaSeconds, LookReturnSpeed);
		if (FMath::Abs(LookYawOffset) < 0.05f)
		{
			LookYawOffset = 0.f;
		}
	}

	const float VehicleYaw = BodyCollision ? BodyCollision->GetComponentRotation().Yaw : 0.f;
	CameraBoom->SetWorldRotation(FRotator(BaseCameraPitch, VehicleYaw + LookYawOffset, 0.f));

	// --- Speed effect: the chase camera pulls back and widens its FOV as the car goes
	// faster, so speed reads on screen. Hood camera is untouched (moving it would clip
	// through the car), and only the player-driven pawn gets the effect.
	if (!bHoodCamera && ChaseCamera)
	{
		const bool bDriven = IsOccupied() && Cast<APlayerController>(GetController()) != nullptr;
		const float TargetFraction = bDriven && SpeedEffectMaxKmh > 1.f
			? FMath::Clamp(GetDisplaySpeedKmh() / SpeedEffectMaxKmh, 0.f, 1.f)
			: 0.f;
		SmoothedSpeedFraction = FMath::FInterpTo(SmoothedSpeedFraction, TargetFraction, DeltaSeconds, SpeedEffectRate);
		CameraBoom->TargetArmLength = ChaseArmLength + SpeedArmExtension * SmoothedSpeedFraction;
		ChaseCamera->SetFieldOfView(ChaseBaseFov + SpeedFovWiden * SmoothedSpeedFraction);
	}

	// --- Safety: never leave an input-driven control latched if a release is missed ---
	SteeringInputAge += DeltaSeconds;
	ThrottleInputAge += DeltaSeconds;
	if (bSteeringFromInput && SteeringInputAge > 0.25f
		&& FMath::Abs(VehicleMovement->GetSteeringInput()) > 0.001f)
	{
		VehicleMovement->SetSteeringInput(0.f);
	}
	if (bThrottleFromInput && ThrottleInputAge > 0.40f
		&& FMath::Abs(VehicleMovement->GetThrottleInput()) > 0.001f)
	{
		VehicleMovement->SetThrottleInput(0.f);
	}
	// Headless top-speed probe: hold full throttle on the authority so steady
	// state can be measured at any frame rate (-CitixHoldThrottle).
	if (HasAuthority() && VehicleMovement
		&& FParse::Param(FCommandLine::Get(), TEXT("CitixHoldThrottle")))
	{
		VehicleMovement->SetThrottleInput(1.f);
	}

	// --- Crash damage uses the speed we *had*, not the speed an impact gave us ----
	// Otherwise being rear-ended while parked would wreck the player's car.
	LastSpeed = GetVelocity().Size();
	ChasePreviousVelocity = GetVelocity();

	// --- Engine audio ---------------------------------------------------
	if (EngineAudio)
	{
		// Audio while this car is driven: a parked car with no occupant stays
		// silent. Occupancy replicates (rank 11), so remotes read as driven too
		// (the old PlayerController cast never survived replication).
		const bool bDriven = IsOccupied();
		EngineAudio->SetEngineState(
			bDriven ? GetDisplaySpeedKmh() : 0.f,
			bDriven ? GetDisplayThrottleAbs() : 0.f,
			bDriven && IsDisplayBoosting(),
			IsDisplayGrounded());
	}

	// --- Networking: publish state, forward inputs -------------------------
	if (HasAuthority())
	{
		// Display state at 15 Hz: every-tick publishes saturated the link and
		// starved movement updates down to ~14 Hz (visible stepping).
		NetStateTimer += DeltaSeconds;
		if (NetStateTimer >= 1.f / 15.f)
		{
			NetStateTimer = 0.f;
			PublishNetState();
		}
	}
	else
	{
		SendDriveInputToServer();
		InterpolateRemoteMovement(DeltaSeconds);
	}

	// --- Wheels: place them at their true physical positions -------------
	// Location comes from the physics (it includes suspension travel), but the
	// rotation must be RELATIVE to the car: a world-space rotation would roll the
	// wheel about the world X axis and skew it whenever the car is yawed.
	// Remotes have no physics state: spin + steer from the replicated snapshot
	// (rank 18) so other drivers' cars read as driven, not sliding.
	const TArray<FCitixWheelRuntimeState>& States = VehicleMovement->GetWheelStates();
	if (!HasAuthority() && States.Num() == 0 && VehicleMovement)
	{
		// NetState is published at 15 Hz, so ease the visual values: otherwise the
		// front wheels (and the car's read) step noticeably while turning.
		RemoteSpeedKmh = FMath::FInterpTo(RemoteSpeedKmh, NetState.SpeedKmh, DeltaSeconds, 12.f);
		const float RemoteSpeed = RemoteSpeedKmh * 100.f / 3600.f; // cm/s
		RemoteWheelSpin += RemoteSpeed * DeltaSeconds
			/ FMath::Max(1.f, VehicleMovement->GetWheelRadius(0)) * 57.2958f;
		RemoteWheelSpin = FMath::Fmod(RemoteWheelSpin, 360.f);
		const float TargetSteerDeg = NetState.SteerAbs * VehicleMovement->GetMaxSteerAngleDegrees();
		RemoteSteerDeg = FMath::FInterpTo(RemoteSteerDeg, TargetSteerDeg, DeltaSeconds, 10.f);
		for (int32 Index = 0; Index < CarVisual.Wheels.Num(); ++Index)
		{
			UStaticMeshComponent* Wheel = CarVisual.Wheels[Index];
			if (!Wheel)
			{
				continue;
			}
			const float Steer = VehicleMovement->IsWheelSteering(Index) ? RemoteSteerDeg : 0.f;
			Wheel->SetRelativeRotation(FRotator(RemoteWheelSpin, Steer, 90.f));
		}
	}
	else
	{
		for (int32 Index = 0; Index < CarVisual.Wheels.Num(); ++Index)
		{
			UStaticMeshComponent* Wheel = CarVisual.Wheels[Index];
			if (!Wheel || !States.IsValidIndex(Index))
			{
				continue;
			}
			const FCitixWheelRuntimeState& State = States[Index];
			FVector WheelPosition=State.WorldLocation;
   if (bChasePerformanceApplied && Index<4) {
    const FVector VisualPosition=CarVisualRoot->GetComponentTransform().TransformPosition(CarVisual.WheelCenters[Index]);
    WheelPosition.X=VisualPosition.X; WheelPosition.Y=VisualPosition.Y;
    WheelPosition+=GetActorUpVector()*(CarVisual.WheelRadius-VehicleMovement->GetWheelRadius(Index));
   }
   Wheel->SetWorldLocation(WheelPosition, false, nullptr, ETeleportType::None);
			Wheel->SetRelativeRotation(FRotator(0.f, State.SteerAngleDegrees, 90.f));
		}
	}
}

void ACitixVehiclePawn::UpdateChaseTracker()
{
	bool bShowTracker = false;
	APawn* RunnerPawn = nullptr;
	if (APlayerController* LocalController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (LocalController->PlayerState == GetPlayerState<ACitixChasePlayerState>())
		{
			if (const ACitixChasePlayerState* LocalState = LocalController->GetPlayerState<ACitixChasePlayerState>(); LocalState && LocalState->ChaseRole == ECitixChaseRole::Chaser)
			{
				if (const ACitixChaseGameState* State = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr; State && State->bRunnerRevealed)
				{
					for (TActorIterator<APawn> It(GetWorld()); It; ++It)
					{
						if (const ACitixChasePlayerState* PawnState = It->GetPlayerState<ACitixChasePlayerState>(); PawnState && PawnState->ChaseRole == ECitixChaseRole::Runner)
						{
							RunnerPawn = *It;
							break;
						}
					}
					bShowTracker = RunnerPawn != nullptr;
				}
			}
		}
	}
	for (UStaticMeshComponent* Segment : ChaseTrackerSegments)
	{
		if (Segment) Segment->SetVisibility(bShowTracker, true);
	}
	if (ChaseTrackerArrow) ChaseTrackerArrow->SetVisibility(bShowTracker, true);
	if (bShowTracker && ChaseTrackerRoot)
	{
		FVector Direction = RunnerPawn->GetActorLocation() - GetActorLocation();
		Direction.Z = 0.f;
		if (!Direction.IsNearlyZero()) ChaseTrackerRoot->SetWorldRotation(Direction.Rotation());
	}
}

void ACitixVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixVehiclePawn, CarType);
	DOREPLIFETIME(ACitixVehiclePawn, PaintColor);
	DOREPLIFETIME(ACitixVehiclePawn, NetState);
	DOREPLIFETIME(ACitixVehiclePawn, RoundStartPose);
	DOREPLIFETIME(ACitixVehiclePawn, OwningController);
	DOREPLIFETIME(ACitixVehiclePawn, bOccupied);
}

void ACitixVehiclePawn::InterpolateRemoteMovement(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || DeltaSeconds <= 0.f || RemoteMovementBuffer.Num() == 0)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	// Drop stale history (keeps at least two entries so a blend is always possible).
	while (RemoteMovementBuffer.Num() > 2 && Now - RemoteMovementBuffer[0].Time > 1.0)
	{
		RemoteMovementBuffer.RemoveAt(0);
	}

	const FRemoteMovementSnapshot& Newest = RemoteMovementBuffer.Last();
	if (IsLocallyControlled() && BodyCollision && BodyCollision->IsSimulatingPhysics())
	{
		const float Age = FMath::Clamp(static_cast<float>(Now - Newest.Time), 0.f, 0.25f);
		const FVector Target = Newest.Location + Newest.Velocity * Age;
		const FVector Error = Target - GetActorLocation();
		if (Error.SizeSquared() > FMath::Square(RemoteSnapDistance))
		{
			SetActorLocationAndRotation(Target, Newest.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
			BodyCollision->SetPhysicsLinearVelocity(Newest.Velocity);
		}
		else
		{
			// ponytail: velocity reconciliation without full input replay; upgrade to
			// timestamped replay if measured high-latency correction remains visible.
			const FVector Velocity = BodyCollision->GetPhysicsLinearVelocity();
			BodyCollision->SetPhysicsLinearVelocity(FCitixChaseRules::LimitVelocity(FMath::VInterpTo(Velocity, Newest.Velocity + Error * 2.f, DeltaSeconds, 2.f),VehicleMovement->AbsoluteSpeedLimit));
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), Newest.Rotation, DeltaSeconds, 2.f), ETeleportType::TeleportPhysics);
		}
		return;
	}

	// Render a little in the past so there are always two snapshots to blend
	// between. A short delay for the owner (responsiveness), a longer one for
	// remote cars (smoothness). This is what removes per-snapshot rubber-banding.
	const float DelayScale = IsLocallyControlled() ? 1.8f : 2.6f;
	const double Delay = FMath::Clamp(DelayScale * RemoteSnapshotInterval, 0.045, 0.18);
	const double RenderTime = Now - Delay;

	FVector TargetLocation;
	FRotator TargetRotation;

	if (FParse::Param(FCommandLine::Get(), TEXT("CitixLegacyInterp")))
	{
		// Legacy per-snapshot dead reckoning, kept only for A/B measurement.
		const float Age = static_cast<float>(FMath::Clamp(Now - LastSnapTime, 0.0, 0.5));
		const float VelKeep = FMath::Clamp(1.f - Age * 2.f, 0.f, 1.f);
		TargetLocation = LastSnapLocation + LastSnapVelocity * Age * VelKeep;
		TargetRotation = LastSnapRotation;
	}
	else if (RenderTime >= Newest.Time || RemoteMovementBuffer.Num() == 1)
	{
		// Starved (hitch or first snapshot): extrapolate, decaying to rest so a
		// parked car does not creep.
		const double Age = FMath::Clamp(RenderTime - Newest.Time, 0.0, 0.30);
		const float Keep = FMath::Clamp(1.f - static_cast<float>(Age * 2.5), 0.f, 1.f);
		TargetLocation = Newest.Location + Newest.Velocity * static_cast<float>(Age) * Keep;
		TargetRotation = Newest.Rotation;
	}
	else
	{
		// Find the pair bracketing the render time and blend between them.
		int32 Next = 1;
		while (Next < RemoteMovementBuffer.Num() && RemoteMovementBuffer[Next].Time <= RenderTime)
		{
			++Next;
		}
		const FRemoteMovementSnapshot& A = RemoteMovementBuffer[Next - 1];
		const FRemoteMovementSnapshot& B =
			RemoteMovementBuffer[FMath::Min(Next, RemoteMovementBuffer.Num() - 1)];
		const double Span = B.Time - A.Time;
		const float Alpha = Span > 1e-4
			? static_cast<float>(FMath::Clamp((RenderTime - A.Time) / Span, 0.0, 1.0)) : 1.f;
		TargetLocation = FMath::Lerp(A.Location, B.Location, Alpha);
		TargetRotation = FQuat::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), Alpha).Rotator();
	}

	// Exits, round spawns, resets: jump, never glide across the map.
	const FVector CurrentLocation = GetActorLocation();

	// Smoothness probe (-CitixSmoothLog): statistics of the commanded step.
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixSmoothLog")))
	{
		if (bSmoothLogInit)
		{
			const FVector Delta = TargetLocation - SmoothLogPrevTarget;
			const float Step = Delta.Size();
			SmoothLogSum += Step;
			SmoothLogMax = FMath::Max(SmoothLogMax, Step);
			const float YawStep = FMath::Abs(FMath::FindDeltaAngleDegrees(SmoothLogPrevYaw, TargetRotation.Yaw));
			SmoothLogYawSum += YawStep;
			SmoothLogYawMax = FMath::Max(SmoothLogYawMax, YawStep);
			SmoothLogPrevYaw = TargetRotation.Yaw;
			++SmoothLogFrames;
			if (!Delta.IsNearlyZero() && !SmoothLogPrevDelta.IsNearlyZero()
				&& FVector::DotProduct(Delta.GetSafeNormal(), SmoothLogPrevDelta.GetSafeNormal()) < -0.3f)
			{
				++SmoothLogReversals;
			}
			SmoothLogPrevDelta = Delta;
		}
		bSmoothLogInit = true;
		SmoothLogPrevTarget = TargetLocation;
		SmoothLogTimer += DeltaSeconds;
		if (SmoothLogTimer >= 2.0)
		{
			if (SmoothLogFrames > 0)
			{
				UE_LOG(LogCitix, Log, TEXT("[CitixSmooth:%s] fps=%.0f step mean=%.1f max=%.1f cm | yaw mean=%.2f max=%.2f deg | reversals=%d/%d"),
					IsLocallyControlled() ? TEXT("own") : TEXT("remote"),
					static_cast<float>(SmoothLogFrames / SmoothLogTimer),
					SmoothLogSum / SmoothLogFrames, SmoothLogMax,
					SmoothLogYawSum / SmoothLogFrames, SmoothLogYawMax,
					SmoothLogReversals, SmoothLogFrames);
			}
			SmoothLogTimer = 0.0;
			SmoothLogSum = 0.f;
			SmoothLogMax = 0.f;
			SmoothLogYawSum = 0.f;
			SmoothLogYawMax = 0.f;
			SmoothLogFrames = 0;
			SmoothLogReversals = 0;
		}
	}

	if (FVector::DistSquared(CurrentLocation, TargetLocation) >= FMath::Square(RemoteSnapDistance))
	{
		SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(TargetRotation);
}

void ACitixVehiclePawn::PushRemoteSnapshot(const FVector& InLocation, const FRotator& InRotation, const FVector& InVelocity)
{
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (RemoteMovementBuffer.Num() > 0)
	{
		const FRemoteMovementSnapshot& Prev = RemoteMovementBuffer.Last();
		if (FVector::DistSquared(Prev.Location, InLocation) > FMath::Square(RemoteSnapDistance))
		{
			// A jump this large is a teleport: drop history so the copy snaps.
			RemoteMovementBuffer.Reset();
		}
		else
		{
			const double Gap = Now - Prev.Time;
			if (Gap > 0.0005 && Gap < 0.5)
			{
				RemoteSnapshotInterval = FMath::Lerp(RemoteSnapshotInterval, static_cast<float>(Gap), 0.2f);
			}
		}
	}

	FRemoteMovementSnapshot Snap;
	Snap.Location = InLocation;
	Snap.Rotation = InRotation;
	Snap.Velocity = InVelocity;
	Snap.Time = Now;
	RemoteMovementBuffer.Add(Snap);

	while (RemoteMovementBuffer.Num() > 12)
	{
		RemoteMovementBuffer.RemoveAt(0);
	}

	LastSnapLocation = InLocation;
	LastSnapRotation = InRotation;
	LastSnapVelocity = InVelocity;
	LastSnapTime = Now;
}

void ACitixVehiclePawn::RamPlayersAlongSweep(const FVector& From, const FVector& To)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (ACitixChaseGameMode* Chase = World->GetAuthGameMode<ACitixChaseGameMode>())
	{
		Chase->TryRunOver(this, From, To, FMath::Max(LastSpeed, static_cast<float>(GetVelocity().Size()))*.036f);
		return;
	}
	ACitixSandboxDirector* Sandbox = nullptr;
	for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It)
	{
		Sandbox = *It;
		break;
	}
	if (!Sandbox)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	FString DriverName = TEXT("Someone");
	if (Controller && Controller->PlayerState)
	{
		DriverName = Controller->PlayerState->GetPlayerName();
	}

	auto SegmentDistance2D = [](const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector2D P2(P.X, P.Y);
		const FVector2D A2(A.X, A.Y);
		const FVector2D B2(B.X, B.Y);
		const FVector2D AB = B2 - A2;
		const float LengthSq = static_cast<float>(AB.SizeSquared());
		if (LengthSq < 1.f)
		{
			return FVector2D::Distance(P2, A2);
		}
		const float T = FMath::Clamp(FVector2D::DotProduct(P2 - A2, AB) / LengthSq, 0.f, 1.f);
		return FVector2D::Distance(P2, A2 + AB * T);
	};

	for (TActorIterator<ACitixOnFootPawn> It(World); It; ++It)
	{
		ACitixOnFootPawn* Victim = *It;
		if (!Victim)
		{
			continue;
		}
		AController* VictimPC = Victim->GetController();
		if (!VictimPC || VictimPC == Controller)
		{
			continue;
		}
		if (SegmentDistance2D(Victim->GetActorLocation(), From, To) > RunOverRadius)
		{
			continue;
		}
		ACitixPlayerState* PS = VictimPC->PlayerState
			? Cast<ACitixPlayerState>(VictimPC->PlayerState) : nullptr;
		if (!PS || Now < PS->RamInvulnUntil)
		{
			continue;
		}
		PS->RamInvulnUntil = Now + 1.f;
		Sandbox->NoteViolence(Controller);
		Sandbox->DamagePlayer(VictimPC, RamDamage, Controller, DriverName, TEXT("run over"));
		// Knock the walker flying (server launch replicates to everyone).
		FVector Toss = GetVelocity() * 0.5f + FVector(0.f, 0.f, 400.f);
		Victim->LaunchCharacter(Toss.GetClampedToMaxSize(2500.f), true, true);
		UE_LOG(LogCitix, Log, TEXT("[Citix] %s ran over %s."),
			*DriverName, *PS->GetPlayerName());
	}
}

void ACitixVehiclePawn::OnRep_CarAppearance(){
	if (CarType == BuiltCarType && PaintColor.Equals(BuiltPaintColor))
	{
		return;
	}
	BuiltCarType = CarType;
	BuiltPaintColor = PaintColor;
	RebuildCarVisual();
	ApplyCarPerformance();
}

void ACitixVehiclePawn::SetRoundStartPose(const FTransform& Pose)
{
 if (!HasAuthority()) return;
 RoundStartPose = Pose;
 LastDryPose = CountdownHoldPose = Pose;
 bHasDryPose = true;
 if (BodyCollision) {
  BodyCollision->SetPhysicsLinearVelocity(FVector::ZeroVector);
  BodyCollision->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
 }
 SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
 ForceNetUpdate();
}

void ACitixVehiclePawn::OnRep_RoundStartPose()
{
 RemoteMovementBuffer.Reset();
 SetActorTransform(RoundStartPose, false, nullptr, ETeleportType::TeleportPhysics);
 PushRemoteSnapshot(RoundStartPose.GetLocation(), RoundStartPose.Rotator(), FVector::ZeroVector);
}

void ACitixVehiclePawn::OnRep_ReplicatedMovement()
{
	// NOTE: read the applied result after Super (ReplicatedMovement is private).
	const FVector Before = GetActorLocation();
	// The interpolation path applies snapshots. Applying Super first snaps the
	// car and its camera on every packet before smoothing can run.
	if (HasAuthority()) Super::OnRep_ReplicatedMovement();
	const FVector After = GetActorLocation();

	// One-sided-freeze diagnosis: count movement arrivals (throttled log).
	// If this never fires on a client, the server is not sending; if it fires
	// but Before == After while the server moves, the apply path is broken.
	if (!HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("CitixNetLog")))
	{
		++NetMoveArrivals;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastNetMoveLog >= 5.0)
		{
			LastNetMoveLog = Now;
			const FRepMovement& Arrived = GetReplicatedMovement();
			UE_LOG(LogCitix, Log, TEXT("[CitixNet] car %s: %d movement arrivals, arrived=(%.0f,%.0f,%.0f) vel=%.0f applied (%.0f,%.0f,%.0f)->(%.0f,%.0f,%.0f)."),
				*GetName(), NetMoveArrivals,
				Arrived.Location.X, Arrived.Location.Y, Arrived.Location.Z,
				Arrived.LinearVelocity.Size(),
				Before.X, Before.Y, Before.Z, After.X, After.Y, After.Z);
		}
	}

	// Latch the snapshot into the interpolation buffer (all non-authority copies).
	if (!HasAuthority())
	{
		const FRepMovement& Arrived = GetReplicatedMovement();
		PushRemoteSnapshot(Arrived.Location, Arrived.Rotation, Arrived.LinearVelocity);
	}
}

float ACitixVehiclePawn::GetDisplaySpeedKmh() const
{
	return (HasAuthority() || IsLocallyControlled()) && VehicleMovement ? VehicleMovement->GetSpeedKmh() : NetState.SpeedKmh;
}

float ACitixVehiclePawn::GetDisplayHullFraction() const
{
	return HasAuthority() && VehicleMovement ? VehicleMovement->GetHealthFraction() : NetState.HullFraction;
}

float ACitixVehiclePawn::GetDisplayBoostCharge() const
{
	return (HasAuthority() || IsLocallyControlled()) && VehicleMovement ? VehicleMovement->GetBoostCharge() : NetState.BoostCharge;
}

bool ACitixVehiclePawn::IsDisplayBoosting() const
{
	return (HasAuthority() || IsLocallyControlled()) && VehicleMovement ? VehicleMovement->IsBoosting() : NetState.bBoosting;
}

bool ACitixVehiclePawn::IsDisplayDestroyed() const
{
	return HasAuthority() && VehicleMovement ? VehicleMovement->IsDestroyed() : NetState.bDestroyed;
}

float ACitixVehiclePawn::GetDisplayThrottleAbs() const
{
	return HasAuthority() && VehicleMovement ? FMath::Abs(VehicleMovement->GetThrottleInput()) : NetState.ThrottleAbs;
}

bool ACitixVehiclePawn::IsDisplayGrounded() const
{
	return HasAuthority() && VehicleMovement ? VehicleMovement->IsGrounded() : NetState.bGrounded;
}

int32 ACitixVehiclePawn::GetActiveSmokePuffs() const
{
	return DriftSmoke ? DriftSmoke->GetActivePuffCount() : 0;
}

int32 ACitixVehiclePawn::GetActiveBoostFlames() const
{
	return BoostTrail ? BoostTrail->GetActiveFlameCount() : 0;
}

void ACitixVehiclePawn::ServerSendDriveInput_Implementation(float Throttle, float Steer, bool bHandbrake, bool bBoost)
{
 // The opt-in deterministic physics probe supplies inputs directly; ignore background guest input.
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixSpeedProbe")) && FParse::Param(FCommandLine::Get(),TEXT("CitixChaseTest"))) return;
	if (const ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr; Chase && !Chase->CanAcceptDriveInput())
	{
		if (VehicleMovement)
		{
			VehicleMovement->SetThrottleInput(0.f);
			VehicleMovement->SetSteeringInput(0.f);
			VehicleMovement->SetHandbrake(false);
			VehicleMovement->SetBoostInput(false);
		}
		return;
	}
	ThrottleInputAge = 0.f;
	SteeringInputAge = 0.f;
	bThrottleFromInput = true;
	bSteeringFromInput = true;
	if (VehicleMovement)
	{
		VehicleMovement->SetThrottleInput(Throttle);
		VehicleMovement->SetSteeringInput(Steer);
		VehicleMovement->SetHandbrake(bHandbrake);
		VehicleMovement->SetBoostInput(bBoost);
	}
}

void ACitixVehiclePawn::ServerResetVehicle_Implementation()
{
	if (ResetCooldownTimer > 0.f || IsDisplayDestroyed())
	{
		return;
	}
	if (const ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr; Chase && !Chase->CanAcceptDriveInput())
	{
		return;
	}
	ResetCooldownTimer = 5.f;
	ResetVehicle();
}

void ACitixVehiclePawn::SendDriveInputToServer()
{
	if (HasAuthority() || !IsLocallyControlled())
	{
		return;
	}
	NetSendTimer += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const bool bChanged = !FMath::IsNearlyEqual(NetThrottle, LastSentThrottle)
		|| !FMath::IsNearlyEqual(NetSteer, LastSentSteer)
		|| bNetHandbrake != bLastSentHandbrake || bNetBoost != bLastSentBoost;
	if (bChanged || NetSendTimer >= 1.f / 30.f)
	{
		NetSendTimer = 0.f;
		LastSentThrottle = NetThrottle;
		LastSentSteer = NetSteer;
		bLastSentHandbrake = bNetHandbrake;
		bLastSentBoost = bNetBoost;
		ServerSendDriveInput(NetThrottle, NetSteer, bNetHandbrake, bNetBoost);
	}
}

void ACitixVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogCitix, Error, TEXT("ACitixVehiclePawn requires an Enhanced Input component."));
		return;
	}

	EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ACitixVehiclePawn::HandleThrottle);
	EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ACitixVehiclePawn::HandleThrottleReleased);
	EnhancedInput->BindAction(SteeringAction, ETriggerEvent::Triggered, this, &ACitixVehiclePawn::HandleSteering);
	EnhancedInput->BindAction(SteeringAction, ETriggerEvent::Completed, this, &ACitixVehiclePawn::HandleSteeringReleased);
	EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACitixVehiclePawn::HandleLook);
	EnhancedInput->BindAction(HandbrakeAction, ETriggerEvent::Started, this, &ACitixVehiclePawn::HandleHandbrakeStart);
	EnhancedInput->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &ACitixVehiclePawn::HandleHandbrakeEnd);
	EnhancedInput->BindAction(ResetAction, ETriggerEvent::Started, this, &ACitixVehiclePawn::HandleReset);
	EnhancedInput->BindAction(CameraToggleAction, ETriggerEvent::Started, this, &ACitixVehiclePawn::HandleCameraToggle);
	EnhancedInput->BindAction(ExitVehicleAction, ETriggerEvent::Started, this, &ACitixVehiclePawn::HandleExitVehicle);
	EnhancedInput->BindAction(BoostAction, ETriggerEvent::Started, this, &ACitixVehiclePawn::HandleBoostStart);
	EnhancedInput->BindAction(BoostAction, ETriggerEvent::Completed, this, &ACitixVehiclePawn::HandleBoostEnd);

	AddDrivingMappingContext();
}

void ACitixVehiclePawn::AddDrivingMappingContext()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !DrivingContext)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			Subsystem->AddMappingContext(DrivingContext, 0);
		}
	}
}

void ACitixVehiclePawn::RemoveDrivingMappingContext()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !DrivingContext)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			Subsystem->RemoveMappingContext(DrivingContext);
		}
	}
}

void ACitixVehiclePawn::UnPossessed()
{
	RemoveDrivingMappingContext();

	// Release every driving control immediately, so the car coasts instead of
	// holding the last input (e.g. when the photo camera takes over).
	if (VehicleMovement)
	{
		VehicleMovement->SetThrottleInput(0.f);
		VehicleMovement->SetSteeringInput(0.f);
	}
	ThrottleInputAge = 0.f;
	SteeringInputAge = 0.f;
	bThrottleFromInput = false;
	bSteeringFromInput = false;

	Super::UnPossessed();
}

void ACitixVehiclePawn::HandleThrottle(const FInputActionValue& Value)
{
 if (!CanAcceptDriveInput()) return;
	ThrottleInputAge = 0.f;
	bThrottleFromInput = true;
	if (!HasAuthority())
	{
		NetThrottle = Value.Get<float>();
		return;
	}
	if (VehicleMovement) { VehicleMovement->SetThrottleInput(Value.Get<float>()); }
}

void ACitixVehiclePawn::HandleThrottleReleased(const FInputActionValue& Value)
{
	ThrottleInputAge = 0.f;
	bThrottleFromInput = true;
	if (!HasAuthority())
	{
		NetThrottle = 0.f;
		return;
	}
	if (VehicleMovement) { VehicleMovement->SetThrottleInput(0.f); }
}

void ACitixVehiclePawn::HandleSteering(const FInputActionValue& Value)
{
 if (!CanAcceptDriveInput()) return;
	SteeringInputAge = 0.f;
	bSteeringFromInput = true;
	if (!HasAuthority())
	{
		NetSteer = Value.Get<float>();
		return;
	}
	if (VehicleMovement) { VehicleMovement->SetSteeringInput(Value.Get<float>()); }
}

void ACitixVehiclePawn::HandleSteeringReleased(const FInputActionValue& Value)
{
	SteeringInputAge = 0.f;
	bSteeringFromInput = true;
	if (!HasAuthority())
	{
		NetSteer = 0.f;
		return;
	}
	if (VehicleMovement) { VehicleMovement->SetSteeringInput(0.f); }
}

void ACitixVehiclePawn::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	LookYawOffset = FMath::Clamp(LookYawOffset + Axis.X * MouseSensitivity, -170.f, 170.f);
	LookReturnTimer = 0.f;
}

void ACitixVehiclePawn::HandleHandbrakeStart(const FInputActionValue& Value)
{
 if (!CanAcceptDriveInput()) return;
	if (!HasAuthority())
	{
		bNetHandbrake = true;
		return;
	}
	if (VehicleMovement) { VehicleMovement->SetHandbrake(true); }
}

void ACitixVehiclePawn::HandleHandbrakeEnd(const FInputActionValue& Value)
{
	if (!HasAuthority())
	{
		bNetHandbrake = false;
		return;
	}
	if (VehicleMovement) { VehicleMovement->SetHandbrake(false); }
}

void ACitixVehiclePawn::HandleReset(const FInputActionValue& Value)
{
	if (!HasAuthority())
	{
		ServerResetVehicle();
		return;
	}
	if (ResetCooldownTimer > 0.f || IsDisplayDestroyed())
	{
		return;
	}
	if (const ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr; Chase && !Chase->CanAcceptDriveInput())
	{
		return;
	}
	ResetCooldownTimer = 5.f;
	ResetVehicle();
}

void ACitixVehiclePawn::HandleCameraToggle(const FInputActionValue& Value)
{
	ToggleCamera();
}

void ACitixVehiclePawn::HandleExitVehicle(const FInputActionValue& Value)
{
	if (ACitixDrivingPlayerController* DrivingController = Cast<ACitixDrivingPlayerController>(GetController()))
	{
		if (const ACitixChaseGameState* Chase = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr; Chase && (Chase->Phase == ECitixChasePhase::Waiting || Chase->Phase == ECitixChasePhase::MatchResults))
		{
			DrivingController->ServerChaseInteract();
			return;
		}
		DrivingController->RequestExitVehicle();
	}
}

void ACitixVehiclePawn::HandleBoostStart(const FInputActionValue& Value)
{
 if (!CanAcceptDriveInput()) return;
	if (!HasAuthority())
	{
		bNetBoost = true;
		return;
	}
	if (VehicleMovement)
	{
		VehicleMovement->SetBoostInput(true);
	}
}

void ACitixVehiclePawn::HandleBoostEnd(const FInputActionValue& Value)
{
	if (!HasAuthority())
	{
		bNetBoost = false;
		return;
	}
	if (VehicleMovement)
	{
		VehicleMovement->SetBoostInput(false);
	}
}

void ACitixVehiclePawn::ApplyVehicleDamage(float Amount)
{
	if (ACitixChaseGameState* Chase = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr)
	{
		if (!HasAuthority() || !bChaseRunner) return;
		if (Amount > 0.f) { ++Chase->ImpactSerial; MulticastChaseImpact(GetActorLocation()); }
	}
	if (VehicleMovement)
	{
		VehicleMovement->ApplyDamage(Amount);
	}
}

void ACitixVehiclePawn::MulticastChaseImpact_Implementation(FVector_NetQuantize Location)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	for (int32 Index = 0; Index < 18; ++Index)
	{
		if (ACitixHitSpark* Spark = GetWorld()->SpawnActor<ACitixHitSpark>())
		{
			Spark->Fire(Location + FVector(0.f, 0.f, 60.f), FMath::VRand() * 650.f + FVector(0.f, 0.f, 300.f), ECitixSurface::EmissiveWarm);
			Spark->SetLifeSpan(1.f);
		}
	}
}

void ACitixVehiclePawn::OnChassisHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!VehicleMovement || OtherActor == this || VehicleMovement->IsDestroyed())
	{
		return;
	}

	// Crashing into people never damages the car. This also covers ragdolled bodies,
	// which DO have collision (so they would otherwise register as a crash).
	if (Cast<ACitixPedestrian>(OtherActor))
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] Chassis impact ignored: pedestrian."));
		return;
	}
	// The server alone validates player-to-player rams.  Every chase client must
	// also skip the sandbox crash-damage fallback, otherwise local traffic or
	// building impacts can temporarily damage (or destroy) its prediction copy.
	if (GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>())
	{
		if (HasAuthority())
		{
			if (ACitixChaseGameMode* Chase = GetWorld()->GetAuthGameMode<ACitixChaseGameMode>())
			{
				if (ACitixVehiclePawn* OtherVehicle = Cast<ACitixVehiclePawn>(OtherActor))
				{
					const FVector Direction = (OtherVehicle->GetActorLocation() - GetActorLocation()).GetSafeNormal();
					// Contact callbacks can arrive after Chaos has removed closing velocity.
					const float ClosingSpeedKmh = FMath::Max(
						FVector::DotProduct(GetVelocity() - OtherVehicle->GetVelocity(), Direction),
						FVector::DotProduct(ChasePreviousVelocity - OtherVehicle->ChasePreviousVelocity, Direction)) * 0.036f;
					Chase->TryRam(this, OtherVehicle, ClosingSpeedKmh);
				}
			}
		}
		// Chase collision damage is only dealt by the server-validated ram above.
		// Traffic and geometry still block and push cars but never wear either hull.
		return;
	}

	// One impact can produce many contact events; only count it once per cooldown.
	if (CrashDamageCooldown > 0.f)
	{
		return;
	}

	const float Speed = LastSpeed;
	if (Speed < VehicleMovement->CrashSpeedThreshold)
	{
		return;
	}

	const float Damage = FMath::Min(
		(Speed - VehicleMovement->CrashSpeedThreshold) * VehicleMovement->CrashDamagePerSpeed,
		VehicleMovement->CrashDamageMax);
	VehicleMovement->ApplyDamage(Damage);
	CrashDamageCooldown = 0.25f;

	// Logged at Log level (not Verbose) because it only fires on a damaging impact, and
	// it is the evidence that the "no damage below 30 km/h" gate is working.
	UE_LOG(LogCitix, Log, TEXT("[Citix] Crash damage %.1f at %.0f km/h (hull %.0f)."),
		Damage, Speed * 0.036f, VehicleMovement->GetHealth());
}

void ACitixVehiclePawn::TriggerExplosion()
{
	bExploding = true;
	ExplosionTimer = 0.f;
	DestroyedTimer = 0.f;

	// Anyone on foot near the wreck is caught in it.
	if (ACitixPedestrianSystem* Pedestrians = CachedPedestrians.Get())
	{
		// Blast: mostly radial (the ragdoll pushes parts away from the centre) plus lift.
		Pedestrians->HitPedestriansInRadius(GetActorLocation(), ExplosionRadius, FVector(300.f, 300.f, 900.f), 4000.f);
	}

	// On-foot drivers in the blast take damage (drivers inside cars are
	// ejected below, unharmed). Server only: clients never decide damage.
	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			ACitixSandboxDirector* Sandbox = nullptr;
			for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It)
			{
				Sandbox = *It;
				break;
			}
			if (Sandbox)
			{
				FString DriverName = TEXT("Someone");
				if (Controller && Controller->PlayerState)
				{
					DriverName = Controller->PlayerState->GetPlayerName();
				}
				Sandbox->NoteViolence(Controller);
				for (TActorIterator<ACitixOnFootPawn> It(World); It; ++It)
				{
					ACitixOnFootPawn* Victim = *It;
					if (!Victim)
					{
						continue;
					}
					AController* VictimPC = Victim->GetController();
					if (!VictimPC || VictimPC == Controller)
					{
						continue;
					}
					if (FVector::Dist(Victim->GetActorLocation(), GetActorLocation()) < ExplosionRadius)
					{
						Sandbox->DamagePlayer(VictimPC, 60.f, Controller, DriverName, TEXT("blast"));
					}
				}
			}
		}
	}

	if (BodyCollision)
	{
		// Throw the wreck so it visibly reacts.
		BodyCollision->AddImpulse(FVector(0.f, 0.f, 300000.f), NAME_None, false);
	}

	// The driver does not ride out the blast: eject on foot beside the wreck.
	if (bOccupied)
	{
		if (ACitixDrivingPlayerController* DrivingPC = Cast<ACitixDrivingPlayerController>(Controller))
		{
			DrivingPC->RequestExitVehicle();
		}
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] Vehicle destroyed - explosion at %s"),
		*GetActorLocation().ToCompactString());
}

void ACitixVehiclePawn::HideExplosionVisuals()
{
	if (ExplosionSphere) { ExplosionSphere->SetVisibility(false, true); }
	if (BlastFlash) { BlastFlash->SetVisibility(false, true); }
	if (BlastSmoke) { BlastSmoke->SetVisibility(false, true); }
	if (BlastRing) { BlastRing->SetVisibility(false, true); }
}

void ACitixVehiclePawn::PublishNetState()
{
	if (!VehicleMovement)
	{
		return;
	}
	NetState.SpeedKmh = VehicleMovement->GetSpeedKmh();
	NetState.HullFraction = VehicleMovement->GetHealthFraction();
	NetState.BoostCharge = VehicleMovement->GetBoostCharge();
	NetState.bBoosting = VehicleMovement->IsBoosting();
	NetState.bDestroyed = VehicleMovement->IsDestroyed();
	NetState.ThrottleAbs = FMath::Abs(VehicleMovement->GetThrottleInput());
	NetState.SteerAbs = FMath::Clamp(VehicleMovement->GetSteeringInput(), -1.f, 1.f);
	NetState.bGrounded = VehicleMovement->IsGrounded();
	NetState.bHandbrake = VehicleMovement->IsHandbrakeEngaged();
	NetState.LateralSlip = VehicleMovement->GetMaxLateralSlipSpeed();
}

void ACitixVehiclePawn::UpdateExplosion(float DeltaSeconds)
{
	ExplosionTimer += DeltaSeconds;
	const float Alpha = FMath::Clamp(ExplosionTimer / FMath::Max(0.05f, ExplosionDuration), 0.f, 1.f);
	const float Unit = FCitixSurfaceLibrary::PrimitiveSize;

	// Stage 1: white-hot ignition blink.
	if (BlastFlash)
	{
		const bool bFlashOn = ExplosionTimer < 0.15f;
		BlastFlash->SetVisibility(bFlashOn, true);
		if (bFlashOn)
		{
			BlastFlash->SetWorldScale3D(FVector(320.f / Unit, 320.f / Unit, 320.f / Unit));
		}
	}

	// Stage 2: the fireball swells far past the old size, then is gone.
	if (ExplosionSphere)
	{
		const float Size = FMath::Lerp(260.f, ExplosionRadius * 1.2f, Alpha);
		ExplosionSphere->SetVisibility(Alpha < 1.f, true);
		ExplosionSphere->SetWorldScale3D(FVector(Size / Unit, Size / Unit, Size / Unit));
	}

	// Stage 3: dark smoke column rises and lingers past the flames.
	if (BlastSmoke)
	{
		const float SmokeStart = 0.3f;
		const float SmokeEnd = ExplosionDuration + 0.7f;
		const bool bSmokeOn = ExplosionTimer >= SmokeStart && ExplosionTimer <= SmokeEnd;
		BlastSmoke->SetVisibility(bSmokeOn, true);
		if (bSmokeOn)
		{
			const float SmokeAlpha = (ExplosionTimer - SmokeStart) / FMath::Max(0.05f, SmokeEnd - SmokeStart);
			const float SmokeSize = FMath::Lerp(200.f, 700.f, SmokeAlpha);
			BlastSmoke->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, 150.f + SmokeAlpha * 600.f));
			BlastSmoke->SetWorldScale3D(FVector(SmokeSize / Unit, SmokeSize / Unit, SmokeSize / Unit));
		}
	}

	// Stage 4: flat shockwave disc races outward and vanishes.
	if (BlastRing)
	{
		const float RingEnd = 0.7f;
		const bool bRingOn = ExplosionTimer <= RingEnd;
		BlastRing->SetVisibility(bRingOn, true);
		if (bRingOn)
		{
			const float RingAlpha = ExplosionTimer / RingEnd;
			const float RingSize = FMath::Lerp(300.f, ExplosionRadius * 2.f, RingAlpha);
			BlastRing->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, 60.f));
			BlastRing->SetWorldScale3D(FVector(RingSize / Unit, RingSize / Unit, 12.f));
		}
	}

	if (ExplosionTimer >= ExplosionDuration + 0.7f)
	{
		bExploding = false;
		if (ExplosionSphere)
		{
			ExplosionSphere->SetVisibility(false, true);
		}
		if (BlastSmoke)
		{
			BlastSmoke->SetVisibility(false, true);
		}
	}
}


void ACitixVehiclePawn::ResetVehicle()
{
	if (VehicleMovement)
	{
		VehicleMovement->ResetVehicle();
	}
	// A reset is a teleport: start the run-over sweep fresh so it cannot cut across the map.
	bHasPreviousLocation = false;
}

void ACitixVehiclePawn::ToggleCamera()
{
	bHoodCamera = !bHoodCamera;
	ApplyCameraMode();
}

void ACitixVehiclePawn::ApplyCameraMode()
{
	if (!CameraBoom)
	{
		return;
	}
	if (bHoodCamera)
	{
		CameraBoom->TargetArmLength = HoodArmLength;
		CameraBoom->SocketOffset = HoodSocketOffset;
	}
	else
	{
		CameraBoom->TargetArmLength = ChaseArmLength;
		CameraBoom->SocketOffset = ChaseSocketOffset;
	}
}
