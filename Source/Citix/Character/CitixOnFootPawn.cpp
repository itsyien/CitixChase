// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CitixOnFootPawn.h"
#include "Citix.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChaseRules.h"
#include "Sandbox/CitixHitSpark.h"
#include "Chase/CitixChasePlayerState.h"
#include "City/CitixCityGenerator.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "Pedestrian/CitixPedestrian.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Player/CitixPlayerState.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "Sandbox/CitixWeaponProp.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Traffic/CitixTrafficVehicle.h"

ACitixOnFootPawn::ACitixOnFootPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Multiplayer stage 1: replicates with client prediction (CharacterMovement).
	// Always relevant for the same reason as vehicles.
	bReplicates = true;
	bAlwaysRelevant = true;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(FCitixCharacterLibrary::CapsuleRadius, FCitixCharacterLibrary::CapsuleHalfHeight);

	// Third-person camera boom; rotates with the controller.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Capsule);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	CameraBoom->TargetArmLength = 320.f;
	CameraBoom->SocketOffset = FVector(0.f, 60.f, 70.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->FieldOfView = 85.f;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->BrakingDecelerationWalking = 2200.f;
	Movement->GroundFriction = 8.f;
	Movement->JumpZVelocity = 420.f;
	Movement->AirControl = 0.35f;
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 560.f, 0.f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// --- Input ----------------------------------------------------------
	CharacterContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("CharacterContext"));

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* Action = CreateDefaultSubobject<UInputAction>(Name);
		Action->ValueType = Type;
		return Action;
	};

	MoveForwardAction = MakeAction(TEXT("IA_MoveForward"), EInputActionValueType::Axis1D);
	MoveRightAction = MakeAction(TEXT("IA_MoveRight"), EInputActionValueType::Axis1D);
	LookAction = MakeAction(TEXT("IA_LookCharacter"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(TEXT("IA_Interact"), EInputActionValueType::Boolean);

	if (CharacterContext)
	{
		// Movement uses two explicit 1D actions. A 2D action fed by digital keys has
		// ambiguous axis assignment, which made the character strafe sideways.
		int32 NegateIndex = 0;
		auto MapNegated = [this, &NegateIndex](UInputAction* Action, const FKey& Key)
		{
			FEnhancedActionKeyMapping& Mapping = CharacterContext->MapKey(Action, Key);
			const FName ModifierName = *FString::Printf(TEXT("CharNegate_%d"), NegateIndex++);
			if (UInputModifierNegate* Negate = CreateDefaultSubobject<UInputModifierNegate>(ModifierName))
			{
				Negate->bX = true;
				Negate->bY = false;
				Negate->bZ = false;
				Mapping.Modifiers.Add(Negate);
			}
		};

		CharacterContext->MapKey(MoveForwardAction, EKeys::W);
		CharacterContext->MapKey(MoveForwardAction, EKeys::Up);
		CharacterContext->MapKey(MoveForwardAction, EKeys::Gamepad_LeftY);
		MapNegated(MoveForwardAction, EKeys::S);
		MapNegated(MoveForwardAction, EKeys::Down);

		CharacterContext->MapKey(MoveRightAction, EKeys::D);
		CharacterContext->MapKey(MoveRightAction, EKeys::Right);
		CharacterContext->MapKey(MoveRightAction, EKeys::Gamepad_LeftX);
		MapNegated(MoveRightAction, EKeys::A);
		MapNegated(MoveRightAction, EKeys::Left);

		CharacterContext->MapKey(LookAction, EKeys::Mouse2D);
		CharacterContext->MapKey(LookAction, EKeys::Gamepad_Right2D);
		CharacterContext->MapKey(JumpAction, EKeys::SpaceBar);
		CharacterContext->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
		CharacterContext->MapKey(SprintAction, EKeys::LeftShift);
		CharacterContext->MapKey(SprintAction, EKeys::Gamepad_LeftShoulder);
		CharacterContext->MapKey(InteractAction, EKeys::F);
	}
}

void ACitixOnFootPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixOnFootPawn, Style);
	DOREPLIFETIME(ACitixOnFootPawn, AppearanceSeed);
	DOREPLIFETIME(ACitixOnFootPawn, bSprinting);
	DOREPLIFETIME(ACitixOnFootPawn, SprintSpeed);
	DOREPLIFETIME(ACitixOnFootPawn, ShockwaveImpulse);
	DOREPLIFETIME(ACitixOnFootPawn, RecoveryEndsAt);
}

void ACitixOnFootPawn::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld()->GetGameState<ACitixChaseGameState>()) GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	ApplySprintSpeed();

	// Each rendered copy owns its local prop; the role selects visibility.
	if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		FActorSpawnParameters PropParams;
		PropParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LocalWeaponProp = GetWorld()->SpawnActor<ACitixWeaponProp>(
			ACitixWeaponProp::StaticClass(), GetActorTransform(), PropParams);
		if (LocalWeaponProp)
		{
			LocalWeaponProp->FollowPawn(this);
			if (GetWorld()->GetGameState<ACitixChaseGameState>())
			{
				LocalWeaponProp->SetActorScale3D(FVector(.65f));
				LocalWeaponProp->SetActorTickEnabled(false);
			}
		}
	}
	FRandomStream Rng(AppearanceSeed != 0 ? AppearanceSeed : FMath::Rand());

	// The capsule centre sits half a height above the feet, and the humanoid rig's
	// origin is between the feet. Offset the body down so the feet meet the ground
	// instead of floating a capsule half-height above it.
	BodyRoot = NewObject<USceneComponent>(this, TEXT("BodyRoot"));
	if (BodyRoot)
	{
		BodyRoot->SetupAttachment(GetCapsuleComponent());
		BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -FCitixCharacterLibrary::CapsuleHalfHeight));
		BodyRoot->SetMobility(EComponentMobility::Movable);
		BodyRoot->RegisterComponent();
	}

	Rig = FCitixCharacterLibrary::BuildCharacter(this,
		BodyRoot ? static_cast<USceneComponent*>(BodyRoot) : GetCapsuleComponent(), Style, Rng);
}

ACitixSandboxDirector* ACitixOnFootPawn::FindSandbox() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ACitixOnFootPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsInShockwaveRecovery() || LocalRagdoll)
	{
		UpdateShockwaveRecovery(DeltaSeconds);
		return;
	}
	if (GetWorld()->GetGameState<ACitixChaseGameState>()) ApplySprintSpeed();
 if (const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>())
  if (const auto* PS=GetPlayerState<ACitixChasePlayerState>()) {
   const float Now=S->GetServerWorldTimeSeconds(); const bool Energy=PS->ChaseRole==ECitixChaseRole::Runner && Now<S->BreakawayUntil;
   if (Energy || Now<PS->GateSlowUntil) { GatePulseTimer+=DeltaSeconds; if (GatePulseTimer>=.15f) { GatePulseTimer=0.f; ACitixHitSpark::SpawnPursuitPulse(GetWorld(),GetActorLocation(),GetActorForwardVector()*100.f,Energy); } }
   else GatePulseTimer=0.f;
  }


	// Local prop follows the resolved kind (rank 18). Own pawn: local loadout
	// mirror; remote pawn: owner's public VisibleWeapon via player array.
	if (LocalWeaponProp && GetWorld())
	{
		int32 Kind = -1;
		if (const ACitixChasePlayerState* ChasePlayer = GetPlayerState<ACitixChasePlayerState>())
		{
			Kind = ChasePlayer->ChaseRole == ECitixChaseRole::Chaser ? 0 : -1;
		}
		else if (IsLocallyControlled())
		{
			if (ACitixSandboxDirector* Sandbox = FindSandbox())
			{
				Kind = Sandbox->GetCurrentWeapon();
			}
		}
		else if (AGameStateBase* GS = GetWorld()->GetGameState())
		{
			for (TObjectPtr<APlayerState> Member : GS->PlayerArray)
			{
				ACitixPlayerState* PS = Member.Get() ? Cast<ACitixPlayerState>(Member.Get()) : nullptr;
				if (PS && PS->GetPawn() == this)
				{
					Kind = (PS->VisibleWeapon < 255) ? static_cast<int32>(PS->VisibleWeapon) : -1;
					break;
				}
			}
		}
		LocalWeaponProp->SetWeaponKind(FMath::Max(0, Kind));
		LocalWeaponProp->SetActorHiddenInGame(Kind < 0);
	}

	// --- Procedural walk cycle -----------------------------------------
	const FVector Velocity = GetVelocity();
	const float Speed = static_cast<float>(Velocity.Size2D());

	if (Speed > 5.f)
	{
		WalkPhase += (Speed * DeltaSeconds) / FMath::Max(1.f, StrideLength) * PI;
	}
	else
	{
		// Ease back to a neutral stance when standing still.
		WalkPhase = FMath::Fmod(WalkPhase + DeltaSeconds * 1.2f, 2.f * PI);
	}

	const float SpeedAlpha = FMath::Clamp(Speed / FMath::Max(1.f, WalkSpeed), 0.f, 1.4f);
	const float Swing = (Speed > 20.f) ? 26.f * FMath::Min(SpeedAlpha, 1.f) : 1.5f;
	const float Bob = (Speed > 20.f) ? 2.2f * FMath::Min(SpeedAlpha, 1.f) : 0.4f;

	Rig.UpdateWalk(WalkPhase, Swing, Bob);
	if (const ACitixChasePlayerState* PS = GetPlayerState<ACitixChasePlayerState>(); PS && PS->ChaseRole == ECitixChaseRole::Chaser && Rig.ArmR && LocalWeaponProp)
	{
		FRotator Aim = GetBaseAimRotation(); Aim.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch),-60.f,60.f);
  bUseControllerRotationYaw=true; GetCharacterMovement()->bOrientRotationToMovement=false;
  SetActorRotation(FRotator(0,Aim.Yaw,0));
		const FVector Direction = BodyRoot->GetComponentTransform().InverseTransformVectorNoScale(Aim.Vector()).GetSafeNormal();
  const FQuat ArmRotation = FQuat::FindBetweenNormals(FVector(0,0,-1), Direction);
  Rig.ArmR->SetRelativeLocationAndRotation(Rig.ShoulderR + Direction * Rig.ArmHalfLength, ArmRotation);
		const FVector Hand = Rig.ArmR->GetComponentLocation()+Rig.ArmR->GetComponentQuat().RotateVector(FVector(0,0,-Rig.ArmHalfLength));
		const FRotator GunRotation(Aim.Pitch,Aim.Yaw,0.f);
		LocalWeaponProp->SetActorLocationAndRotation(Hand-GunRotation.RotateVector(FVector(-12,0,-10)*.65f),GunRotation);
	}
}

void ACitixOnFootPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	EnhancedInput->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &ACitixOnFootPawn::HandleMoveForward);
	EnhancedInput->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &ACitixOnFootPawn::HandleMoveRight);
	EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACitixOnFootPawn::HandleLook);
	EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ACitixOnFootPawn::HandleJumpStart);
	EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACitixOnFootPawn::HandleJumpEnd);
	EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &ACitixOnFootPawn::HandleSprintStart);
	EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &ACitixOnFootPawn::HandleSprintEnd);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &ACitixOnFootPawn::HandleInteract); EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this, &ACitixOnFootPawn::HandleInteractEnd);

	AddMappingContext();
}

void ACitixOnFootPawn::BeginShockwaveRecovery(const FVector& Impulse)
{
	if (!HasAuthority()) return;
	ShockwaveImpulse = Impulse;
	RecoveryEndsAt = GetWorld()->GetGameState<ACitixChaseGameState>()->GetServerWorldTimeSeconds() + 3.65f;
	ForceNetUpdate();
	UpdateShockwaveRecovery(0.f);
}

void ACitixOnFootPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveMappingContext();
	if (LocalRagdoll) LocalRagdoll->Destroy();
	if (LocalWeaponProp) LocalWeaponProp->Destroy();
	Super::EndPlay(EndPlayReason);
}

bool ACitixOnFootPawn::FindRecoveryPosition(FVector& OutPosition) const
{
	const FVector Rest = LocalRagdoll ? LocalRagdoll->GetBodyLocation() : GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaseStandup), false, this);
	if (LocalRagdoll) Params.AddIgnoredActor(LocalRagdoll);
	for (int32 Index = 0; Index < 18; ++Index)
	{
		const FVector Centre = Index == 17 ? GetActorLocation() : Rest;
		const float Angle = Index * PI / 4.f;
		const float Radius = Index == 0 || Index == 17 ? 0.f : (Index <= 8 ? 160.f : 320.f);
		const FVector Probe = Centre + FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0.f);
		FTransform Dry;
  if (GetWorld()->GetGameState<ACitixChaseGameState>() && !ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Probe,FVector(48,48,85),0,this,Dry)) continue;
  FHitResult Ground;
		if (!GetWorld()->LineTraceSingleByChannel(Ground, Probe+FVector(0,0,350), Probe-FVector(0,0,700), ECC_WorldStatic, Params) || Ground.ImpactNormal.Z < .7f)
		{
			if (Index == 17 && FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTest"))) UE_LOG(LogCitix, Warning, TEXT("[RecoveryProbe] no floor actor=%s normal=%s"), *GetNameSafe(Ground.GetActor()), *Ground.ImpactNormal.ToCompactString());
			continue;
		}
		const FVector Candidate = Ground.ImpactPoint + FVector(0,0,FCitixCharacterLibrary::CapsuleHalfHeight+3.f);
		FHitResult Blocker;
		if (!GetWorld()->SweepSingleByChannel(Blocker,Candidate,Candidate,FQuat::Identity,ECC_WorldStatic,
			FCollisionShape::MakeCapsule(FCitixCharacterLibrary::CapsuleRadius,FCitixCharacterLibrary::CapsuleHalfHeight),Params))
		{
			OutPosition = Candidate;
			return true;
		}
		if (Index == 17 && FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTest"))) UE_LOG(LogCitix, Warning, TEXT("[RecoveryProbe] blocked actor=%s component=%s candidate=%s"), *GetNameSafe(Blocker.GetActor()), *GetNameSafe(Blocker.GetComponent()), *Candidate.ToCompactString());
	}
	return false;
}

void ACitixOnFootPawn::UpdateShockwaveRecovery(float DeltaSeconds)
{
	if (RecoveryEndsAt > 0.f)
	{
		GetCharacterMovement()->DisableMovement();
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (LocalWeaponProp) LocalWeaponProp->SetActorHiddenInGame(true);
		if (!LocalRagdoll)
		{
			const FTransform Feet(GetActorRotation(),GetActorLocation()-FVector(0,0,FCitixCharacterLibrary::CapsuleHalfHeight));
			LocalRagdoll = GetWorld()->SpawnActorDeferred<ACitixPedestrian>(ACitixPedestrian::StaticClass(),Feet,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!LocalRagdoll) return;
			LocalRagdoll->SetReplicates(false);
			LocalRagdoll->FinishSpawning(Feet);
			LocalRagdoll->InitializeAppearance(Style,AppearanceSeed);
			LocalRagdoll->SetActiveVisual(true);
			const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>();
			LocalRagdoll->ConfigurePlayerKnockdown(RecoveryEndsAt-Chase->GetServerWorldTimeSeconds()-.65f);
			LocalRagdoll->SetRecoveryFacing(GetActorRotation().Yaw,Feet.GetLocation().Z);
			LocalRagdoll->Ragdoll(ShockwaveImpulse,Feet.GetLocation(),ECitixRagdollKind::Knockdown);
			TInlineComponentArray<UStaticMeshComponent*> Parts(LocalRagdoll);
			for (UStaticMeshComponent* Part : Parts) { Part->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore); Part->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore); }
			Rig.SetVisibility(false);
			bRecoveryPoseFinished = false;
			UE_LOG(LogCitix, Log, TEXT("[CitixChase] Shockwave local ragdoll started net=%d remaining=%.2f."), (int32)GetNetMode(), RecoveryEndsAt-Chase->GetServerWorldTimeSeconds());
			if (IsLocallyControlled() && FParse::Param(FCommandLine::Get(), TEXT("CitixImpactScreenshot")))
			{
				FTimerHandle Timer;
				const FString Tag = GetNetMode() == NM_Client ? TEXT("Guest") : TEXT("Host");
				GetWorldTimerManager().SetTimer(Timer, [Tag]() {
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("Shockwave-")+Tag+TEXT(".png")),true,false);
				}, 1.f, false);
			}
		}
		const FVector CameraOffset = GetActorTransform().InverseTransformVectorNoScale(LocalRagdoll->GetBodyLocation()-GetActorLocation());
		CameraBoom->SetRelativeLocation(CameraOffset+FVector(0,0,55));
		bRecoveryPoseFinished |= LocalRagdoll->UpdateRagdoll(DeltaSeconds);
		if (!HasAuthority() || !bRecoveryPoseFinished) return;
		FVector Stand;
		if (!FindRecoveryPosition(Stand)) return;
		SetActorLocation(Stand,false,nullptr,ETeleportType::TeleportPhysics);
		RecoveryEndsAt = 0.f;
		UE_LOG(LogCitix, Log, TEXT("[CitixChase] Shockwave recovery complete; safe stand at %s."), *Stand.ToCompactString());
		ForceNetUpdate();
	}
	if (LocalRagdoll) { LocalRagdoll->Destroy(); LocalRagdoll = nullptr; }
	Rig.SetVisibility(true);
	CameraBoom->SetRelativeLocation(FVector(0,0,55));
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

void ACitixOnFootPawn::AddMappingContext()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !CharacterContext)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			MappingSubsystem = Subsystem;
			Subsystem->AddMappingContext(CharacterContext, 1);
		}
	}
}

void ACitixOnFootPawn::RemoveMappingContext()
{
	if (MappingSubsystem.IsValid())
	{
		MappingSubsystem->RemoveMappingContext(CharacterContext);
		MappingSubsystem.Reset();
		return;
	}
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !CharacterContext)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			Subsystem->RemoveMappingContext(CharacterContext);
		}
	}
}

void ACitixOnFootPawn::UnPossessed()
{
	RemoveMappingContext();
	Super::UnPossessed();
}

void ACitixOnFootPawn::OnRep_Controller()
{
	RemoveMappingContext();
	Super::OnRep_Controller();
	if (IsLocallyControlled()) AddMappingContext();
}

void ACitixOnFootPawn::HandleMoveForward(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	if (!Controller || FMath::IsNearlyZero(Axis))
	{
		return;
	}

	// Move relative to where the camera is looking.
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	AddMovementInput(Forward, Axis);
}

void ACitixOnFootPawn::HandleMoveRight(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	if (!Controller || FMath::IsNearlyZero(Axis))
	{
		return;
	}

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(Right, Axis);
}

void ACitixOnFootPawn::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void ACitixOnFootPawn::HandleJumpStart(const FInputActionValue& Value)
{
	if (IsInShockwaveRecovery()) return;
	Jump();
}

void ACitixOnFootPawn::HandleJumpEnd(const FInputActionValue& Value)
{
	StopJumping();
}

void ACitixOnFootPawn::HandleSprintStart(const FInputActionValue& Value)
{
	bSprinting = true;
	ApplySprintSpeed();
	if (!HasAuthority())
	{
		ServerSetSprinting(true);
	}
}

void ACitixOnFootPawn::HandleSprintEnd(const FInputActionValue& Value)
{
	bSprinting = false;
	ApplySprintSpeed();
	if (!HasAuthority())
	{
		ServerSetSprinting(false);
	}
}

void ACitixOnFootPawn::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	bSprinting = bNewSprinting;
	ApplySprintSpeed();
}

void ACitixOnFootPawn::OnRep_Sprinting()
{
	ApplySprintSpeed();
}

void ACitixOnFootPawn::OnRep_SprintSpeed()
{
	ApplySprintSpeed();
}

void ACitixOnFootPawn::ApplySprintSpeed()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
  Movement->MaxAcceleration=2048.f;
		if (const ACitixChasePlayerState* ChasePlayer = GetPlayerState<ACitixChasePlayerState>())
		{
			if (ChasePlayer->ChaseRole == ECitixChaseRole::Runner || ChasePlayer->ChaseRole == ECitixChaseRole::Chaser) Movement->MaxWalkSpeed *= FCitixChaseRules::OnFootSpeedScale(ChasePlayer->ChaseRole == ECitixChaseRole::Runner, bSprinting);
   if (const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>(); S && S->Phase==ECitixChasePhase::Pursuit && ChasePlayer->ChaseRole==ECitixChaseRole::Chaser) Movement->MaxWalkSpeed *= FCitixChaseRules::GateSlowScale(S->GetServerWorldTimeSeconds(),ChasePlayer->GateSlowStartedAt,ChasePlayer->GateSlowUntil);
			if (const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>(); Chase && ChasePlayer->StaggerUntil > Chase->GetServerWorldTimeSeconds()) Movement->MaxWalkSpeed *= .5f;
   if (const auto* Chase=GetWorld()->GetGameState<ACitixChaseGameState>(); Chase && ChasePlayer->FrozenUntil>Chase->GetServerWorldTimeSeconds()) { Movement->MaxWalkSpeed=ChasePlayer->FrozenSpeedLimit; Movement->MaxAcceleration=0; }
		}
	}
}

void ACitixOnFootPawn::HandleInteract(const FInputActionValue& Value)
{
	if (IsInShockwaveRecovery()) return;
	if (ACitixDrivingPlayerController* Controller_ = Cast<ACitixDrivingPlayerController>(GetController()))
	{
		if (Controller_->IsChaseMode()) { Controller_->ServerChaseInteract(); return; }
		Controller_->RequestEnterVehicle();
	}
}

void ACitixOnFootPawn::HandleInteractEnd(const FInputActionValue& Value)
{
	if (ACitixDrivingPlayerController* Controller_ = Cast<ACitixDrivingPlayerController>(GetController())) Controller_->ServerCancelChaseInteract();
}

ACitixVehiclePawn* ACitixOnFootPawn::FindNearestVehicle(float MaxDistance) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ACitixVehiclePawn* Best = nullptr;
	float BestDistanceSq = MaxDistance * MaxDistance;
	const FVector Location = GetActorLocation();

	for (TActorIterator<ACitixVehiclePawn> It(World); It; ++It)
	{
		ACitixVehiclePawn* Vehicle = *It;
		if (!Vehicle || Vehicle->IsOccupied() || Vehicle->IsDisplayDestroyed())
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(Location, Vehicle->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Vehicle;
		}
	}
	return Best;
}

ACitixTrafficVehicle* ACitixOnFootPawn::FindNearestTrafficVehicle(float MaxDistance) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ACitixTrafficVehicle* Best = nullptr;
	float BestDistanceSq = MaxDistance * MaxDistance;
	const FVector Location = GetActorLocation();

	for (TActorIterator<ACitixTrafficVehicle> It(World); It; ++It)
	{
		ACitixTrafficVehicle* Vehicle = *It;
		if (!Vehicle || Vehicle->IsTakenByPlayer() || Vehicle->IsHidden())
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(Location, Vehicle->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Vehicle;
		}
	}
	return Best;
}

FVector ACitixOnFootPawn::GetChaseMuzzle() const { return LocalWeaponProp ? LocalWeaponProp->GetMuzzleLocation() : GetActorLocation()+FVector(0,0,50); }
