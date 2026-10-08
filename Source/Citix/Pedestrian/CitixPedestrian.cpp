// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pedestrian/CitixPedestrian.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Net/UnrealNetwork.h"
#include "Citix.h"

namespace
{
	/** Local hip height in character space; the pose rotates the upper body about this. */
	constexpr float HipPivotZ = 88.f;

	/** Half extent of a character part, which is always a unit cube scaled per part. */
	constexpr float PartHalfExtent = 50.f;

	/** Signed shortest way round from A to B, degrees. */
	float ShortestAngleDelta(float From, float To)
	{
		return FMath::FindDeltaAngleDegrees(From, To);
	}

	/** FMath::Lerp has no overload for FTransform, so blend the pieces explicitly. */
	FTransform BlendTransform(const FTransform& From, const FTransform& To, float Alpha)
	{
		FTransform Result;
		Result.SetLocation(FMath::Lerp(From.GetLocation(), To.GetLocation(), Alpha));
		Result.SetRotation(FQuat::Slerp(From.GetRotation(), To.GetRotation(), Alpha));
		Result.SetScale3D(FMath::Lerp(From.GetScale3D(), To.GetScale3D(), Alpha));
		return Result;
	}

	/**
	 * The get-up animation, authored as keyframes because the character is built from
	 * primitives with no skeletal mesh to play a clip on. It is a real articulated
	 * sequence though: the hips flex to pull the knees under, the torso unfolds from
	 * doubled-over to upright, the arms swing from propping on the ground to the sides,
	 * and the whole body rises out of the crouch.
	 */
	struct FGetUpKey
	{
		float Time;
		FRotator Torso;
		FRotator Head;
		FRotator ShoulderL;
		FRotator ShoulderR;
		FRotator HipL;
		FRotator HipR;
		FVector BodyOffset;
	};

	const FGetUpKey GetUpKeys[] =
	{
		// t, torso, head, shoulderL, shoulderR, hipL, hipR, body offset
		{ 0.00f, FRotator(58.f, 0.f, -6.f),  FRotator(-22.f, 0.f, 0.f), FRotator(-38.f, 0.f, -26.f), FRotator(-38.f, 0.f, 26.f), FRotator(-54.f, 0.f, 4.f),  FRotator(-54.f, 0.f, -4.f), FVector(0.f, 0.f, -14.f) },
		{ 0.26f, FRotator(72.f, 0.f, -3.f),  FRotator(-26.f, 0.f, 0.f), FRotator(-30.f, 0.f, -20.f), FRotator(-30.f, 0.f, 20.f), FRotator(-74.f, 0.f, 3.f),  FRotator(-74.f, 0.f, -3.f), FVector(0.f, 0.f, -34.f) },
		{ 0.55f, FRotator(52.f, 0.f, 0.f),   FRotator(-16.f, 0.f, 0.f), FRotator(-16.f, 0.f, -12.f), FRotator(-16.f, 0.f, 12.f), FRotator(-56.f, 0.f, 2.f),  FRotator(-56.f, 0.f, -2.f), FVector(0.f, 0.f, -40.f) },
		{ 0.80f, FRotator(20.f, 0.f, 0.f),   FRotator(-6.f, 0.f, 0.f),  FRotator(-6.f, 0.f, -5.f),  FRotator(-6.f, 0.f, 5.f),  FRotator(-20.f, 0.f, 1.f),  FRotator(-20.f, 0.f, -1.f), FVector(0.f, 0.f, -16.f) },
		{ 1.00f, FRotator::ZeroRotator,      FRotator::ZeroRotator,     FRotator::ZeroRotator,       FRotator::ZeroRotator,      FRotator::ZeroRotator,      FRotator::ZeroRotator,      FVector::ZeroVector }
	};

	constexpr int32 GetUpKeyCount = UE_ARRAY_COUNT(GetUpKeys);
}

ACitixPedestrian::ACitixPedestrian()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	// Multiplayer stage 1: server walks, everyone interpolates. Limbs simulate
	// on the server; remotes play a canned fall/recover from ReactionNet.
	// The tick only animates on remotes (see Tick).
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(10.f);
	SetNetCullDistanceSquared(35000.f * 35000.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);

	// Roughly the standing body: 170 cm tall, 48 cm across.
	Hitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("Hitbox"));
	Hitbox->SetupAttachment(SceneRoot);
	Hitbox->SetBoxExtent(FVector(24.f, 24.f, 85.f));
	Hitbox->SetRelativeLocation(FVector(0.f, 0.f, 85.f));
	Hitbox->SetMobility(EComponentMobility::Movable);
	Hitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitbox->SetCollisionObjectType(ECC_WorldDynamic);
	Hitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	// The player's car (a physics body), the on-foot player (a pawn) and traffic (the vehicle
	// channel). Traffic reports contacts through this overlap rather than by polling, so 150
	// cars cost nothing per frame.
	Hitbox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	Hitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Hitbox->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	Hitbox->SetGenerateOverlapEvents(true);
	Hitbox->SetCanEverAffectNavigation(false);
	Hitbox->SetHiddenInGame(true);
}

void ACitixPedestrian::BeginPlay()
{
	Super::BeginPlay();

	if (Hitbox)
	{
		Hitbox->OnComponentBeginOverlap.AddDynamic(this, &ACitixPedestrian::OnHitboxBeginOverlap);
	}

	if (!bAppearanceReady)
	{
		InitializeAppearance(Style, static_cast<int32>(GetUniqueID()));
	}
}

FVector ACitixPedestrian::GetHitboxCentre() const
{
	return Hitbox ? Hitbox->GetComponentLocation() : GetActorLocation();
}

float ACitixPedestrian::GetHitboxRadius() const
{
	return Hitbox ? Hitbox->GetScaledBoxExtent().Size2D() : 40.f;
}

void ACitixPedestrian::SetHitboxEnabled(bool bEnabled)
{
	if (Hitbox)
	{
		Hitbox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

bool ACitixPedestrian::IsHitboxEnabled() const
{
	return Hitbox && Hitbox->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
}

void ACitixPedestrian::OnHitboxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// The player (car or on foot), or a traffic car. Anything else that overlaps - other
	// pedestrians' ragdoll limbs, props - is ignored, which is why the response is set per
	// channel rather than to everything.
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	const bool bPlayer = (PlayerPawn && OtherActor == PlayerPawn);
	const bool bTraffic = OtherActor && OtherActor->IsA<ACitixTrafficVehicle>();
	if (!bPlayer && !bTraffic)
	{
		return;
	}
	if (!OwningSystem.IsValid())
	{
		return;
	}

	// The contact is described by the other body's motion. Traffic actors are moved by
	// teleport, so their reported velocity is zero: fall back to a nominal city speed so a
	// car that drives into someone still knocks them down convincingly.
	FVector ContactVelocity = OtherActor ? OtherActor->GetVelocity() : FVector::ZeroVector;
	if (bTraffic && ContactVelocity.SizeSquared() < FMath::Square(200.f))
	{
		FVector Heading = OtherActor->GetActorForwardVector();
		Heading.Z = 0.f;
		ContactVelocity = Heading.GetSafeNormal() * 900.f;
	}

	ACitixPedestrianSystem* System = OwningSystem.Get();
	if (!System)
	{
		return;
	}

	// A person shoving through on foot staggers pedestrians, never ragdolls them.
	// Ragdolls belong to vehicles: the player's car and traffic.
	if (bPlayer && !OtherActor->IsA<ACitixVehiclePawn>())
	{
		System->StaggerPedestrian(this, OtherActor->GetActorLocation());
		return;
	}
	System->NotifyPedestrianContact(this, OtherActor->GetActorLocation(), ContactVelocity);
}

void ACitixPedestrian::InitializeAppearance(ECitixCharacterStyle InStyle, int32 Seed)
{
	if (bAppearanceReady)
	{
		return;
	}
	bAppearanceReady = true;
	Style = InStyle;
	AppearanceSeed = Seed;

	FRandomStream Rng(Seed);
	Rig = FCitixCharacterLibrary::BuildCharacter(this, SceneRoot, Style, Rng);
}

void ACitixPedestrian::UpdatePose(float PhaseRadians, float SwingDegrees, float BobCm)
{
	if (bAppearanceReady)
	{
		Rig.UpdateWalk(PhaseRadians, SwingDegrees, BobCm);
	}
}

void ACitixPedestrian::SetActiveVisual(bool bVisible)
{
	bVisibleNet = bVisible;
	SetActorHiddenInGame(!bVisible);
	if (bAppearanceReady)
	{
		Rig.SetVisibility(bVisible);
	}
	// A hidden (pooled, out-of-sight) pedestrian has no hitbox, so nothing can bump into a
	// person that is not there. While ragdolling the limbs are the collision, not the box.
	SetHitboxEnabled(bVisible && !IsRagdolling());
}

void ACitixPedestrian::OnRep_Appearance()
{
	if (!bAppearanceReady && AppearanceSeed != 0)
	{
		InitializeAppearance(Style, AppearanceSeed);
	}
}

void ACitixPedestrian::OnRep_Visible()
{
	SetActorHiddenInGame(!bVisibleNet);
	if (bAppearanceReady)
	{
		Rig.SetVisibility(bVisibleNet);
	}
	// Hidden pooled actors have no hitbox on any screen (rank 14): without
	// this, remotes kept collision on invisible people.
	SetHitboxEnabled(bVisibleNet && ReactionNet == 0);
}

void ACitixPedestrian::OnRep_Reaction()
{
	RemoteAnimAge = 0.f;
	// Hitbox follows condition on every screen: only walkers collide.
	SetHitboxEnabled(bVisibleNet && ReactionNet == 0);
	if (!bAppearanceReady)
	{
		return;
	}
	if (ReactionNet == 0)
	{
		// Back on their feet: restore the walking rig exactly.
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
		SetActorScale3D(FVector(1.f));
		Rig.SetVisibility(bVisibleNet);
	}
	else
	{
		Rig.SetVisibility(true);
	}
}

void ACitixPedestrian::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Remotes only: the server drives the full ragdoll through the system.
	// Canned fall (tip over), dead hold + shrink, recover (stand back up).
	if (HasAuthority() || !bAppearanceReady || DeltaSeconds <= 0.f)
	{
		return;
	}
	RemoteAnimAge += DeltaSeconds;
	const FRotator BaseYaw(0.f, GetActorRotation().Yaw, 0.f);
	if (ReactionNet == 1 || ReactionNet == 2)
	{
		const float Fall = FMath::Clamp(RemoteAnimAge / 0.35f, 0.f, 1.f);
		SetActorRotation(FRotator(-80.f * Fall, BaseYaw.Yaw, 0.f));
		if (ReactionNet == 2)
		{
			const float Fade = FMath::Clamp((RemoteAnimAge - 1.6f) / 1.4f, 0.f, 1.f);
			const float Scale = FMath::Max(0.01f, 1.f - Fade);
			SetActorScale3D(FVector(Scale));
			if (Fade >= 1.f)
			{
				SetActorHiddenInGame(true);
			}
		}
	}
	else if (ReactionNet == 3)
	{
		const float Rise = FMath::Clamp(RemoteAnimAge / 1.45f, 0.f, 1.f);
		SetActorRotation(FRotator(-80.f * (1.f - Rise), BaseYaw.Yaw, 0.f));
	}
	else
	{
		SetActorRotation(BaseYaw);
		SetActorScale3D(FVector(1.f));
	}
}

void ACitixPedestrian::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixPedestrian, Style);
	DOREPLIFETIME(ACitixPedestrian, AppearanceSeed);
	DOREPLIFETIME(ACitixPedestrian, bVisibleNet);
	DOREPLIFETIME(ACitixPedestrian, ReactionNet);
}

FVector ACitixPedestrian::GetBodyLocation() const
{
	// While the ragdoll is still physics-simulating the actor root is parked where the person
	// was standing and only the torso knows where the body actually is. Once the get-up has
	// begun the actor IS the body, so report that instead: following the torso during the
	// animation made the camera (and any consumer) jump if a pose ever displaced it.
	if (IsRagdolling() && !bGettingUp && Rig.Torso)
	{
		return Rig.Torso->GetComponentLocation();
	}
	return GetActorLocation();
}

void ACitixPedestrian::SetRecoveryFacing(float YawDegrees, float StandZ)
{
	RecoveryTargetYaw = YawDegrees;
	RecoveryStandZ = StandZ;
}

// ---------------------------------------------------------------------------
// Ragdoll
// ---------------------------------------------------------------------------

void ACitixPedestrian::Ragdoll(const FVector& Impulse, const FVector& ImpactLocation, ECitixRagdollKind InKind)
{
	if (IsRagdolling() || !bAppearanceReady || Rig.Parts.Num() == 0)
	{
		return;
	}
	RagdollKind = InKind;
	RagdollAge = 0.f;
	SettleQuietTime = 0.f;
	bGettingUp = false;
	GetUpElapsed = 0.f;
	// Remotes mirror the condition (rank 14): dead holds + shrinks, knockdown
	// falls and later recovers.
	ReactionNet = (InKind == ECitixRagdollKind::Death) ? 2 : 1;
	// The limbs take over the collision from here.
	SetHitboxEnabled(false);

	UStaticMeshComponent* Torso = Rig.Torso;
	if (!Torso)
	{
		return;
	}

	const FVector BodyVelocity = GetVelocity();
	// A survivable hit knocks someone off their feet; it does not launch them down the
	// street. Scaling the impulse here is what keeps them landing near where they stood.
	const float ImpulseScale = (InKind == ECitixRagdollKind::Knockdown) ? KnockdownImpulseScale : 1.f;

	for (UStaticMeshComponent* Part : Rig.Parts)
	{
		if (!Part)
		{
			continue;
		}
		Part->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// WorldDynamic, not PhysicsBody: pavements and kerbs deliberately ignore the
		// physics-body channel so the player's car (also a physics body) is never stopped
		// by a kerb, which meant a ragdoll fell straight through the slab it landed on and
		// was swallowed by the pavement. Pavements block WorldDynamic, so the body now
		// rests on the surface it was knocked onto instead of disappearing into it.
		Part->SetCollisionObjectType(ECC_WorldDynamic);
		Part->SetCollisionResponseToAllChannels(ECR_Block);
		Part->SetSimulatePhysics(true);
		Part->SetEnableGravity(true);
		Part->WakeAllRigidBodies();

		// Carry the hit through: the struck region takes the impulse, and the whole
		// body inherits the car's velocity so it does not just drop straight down.
		const FVector ToPart = Part->GetComponentLocation() - ImpactLocation;
		const float Falloff = 1.f / (1.f + ToPart.Size() * 0.004f);
		Part->AddImpulse((Impulse * Falloff + BodyVelocity * 0.6f
			+ ToPart.GetSafeNormal() * Impulse.Size() * 0.30f) * ImpulseScale, NAME_None, /*bVelChange*/ true);
		Part->AddAngularImpulseInDegrees(
			FVector(FMath::FRandRange(-420.f, 420.f), FMath::FRandRange(-420.f, 420.f), FMath::FRandRange(-420.f, 420.f))
				* ImpulseScale,
			NAME_None, true);
	}

	// Joint positions in character space, mirroring the rig geometry (torso centre at
	// Z=122, shoulders at Z=150, hips at Z=88). The constraint's own transform is the
	// joint frame, so it MUST sit at the joint: initialising it at the actor root would
	// make the locked linear limits drag the limbs toward the root and tear the body apart.
	struct FJoint
	{
		UStaticMeshComponent* A;
		UStaticMeshComponent* B;
		FVector LocalJoint;
		float Swing;
	};
	const FJoint Joints[] =
	{
		{ Torso, Rig.Head, FVector(0.f, 0.f, 152.f), 62.f },
		// Hair is a separate box sitting on the head; without a joint it just falls off.
		{ Rig.Head, Rig.Hair, FVector(-1.f, 0.f, 181.f), 12.f },
		{ Torso, Rig.ArmL, FVector(0.f, -21.f, 150.f), 78.f },
		{ Torso, Rig.ArmR, FVector(0.f, 21.f, 150.f), 78.f },
		{ Torso, Rig.LegL, FVector(0.f, -11.f, 88.f), 72.f },
		{ Torso, Rig.LegR, FVector(0.f, 11.f, 88.f), 72.f }
	};

	// The actor is still standing in its walk pose here, so its transform is the frame
	// the joints are expressed in.
	const FTransform BodyFrame = GetActorTransform();

	for (const FJoint& Joint : Joints)
	{
		if (!Joint.A || !Joint.B)
		{
			continue;
		}
		UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(
			this, *FString::Printf(TEXT("RagdollJoint_%d"), RagdollConstraints.Num()));
		if (!Constraint)
		{
			continue;
		}
		Constraint->SetupAttachment(SceneRoot);
		Constraint->RegisterComponent();
		Constraint->SetMobility(EComponentMobility::Movable);
		Constraint->SetWorldLocationAndRotation(BodyFrame.TransformPosition(Joint.LocalJoint), FRotator::ZeroRotator);
		Constraint->SetConstrainedComponents(Joint.A, NAME_None, Joint.B, NAME_None);
		Constraint->SetDisableCollision(true);
		Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, Joint.Swing);
		Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, Joint.Swing);
		Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Limited, 45.f);
		RagdollConstraints.Add(Constraint);
	}
}

float ACitixPedestrian::ComputeGroundZ() const
{
	float GroundZ = TNumericLimits<float>::Max();
	for (const UStaticMeshComponent* Part : Rig.Parts)
	{
		if (!Part)
		{
			continue;
		}
		// Lowest corner of the part's oriented box: the parts are scaled cubes, so the
		// world-Z half extent is the rotation-projected scale.
		const FTransform& Xf = Part->GetComponentTransform();
		const FVector HalfScale = Xf.GetScale3D().GetAbs() * PartHalfExtent;
		const float ExtentZ =
			FMath::Abs(Xf.GetUnitAxis(EAxis::X).Z) * HalfScale.X +
			FMath::Abs(Xf.GetUnitAxis(EAxis::Y).Z) * HalfScale.Y +
			FMath::Abs(Xf.GetUnitAxis(EAxis::Z).Z) * HalfScale.Z;
		GroundZ = FMath::Min(GroundZ, static_cast<float>(Xf.GetLocation().Z - ExtentZ));
	}
	return (GroundZ == TNumericLimits<float>::Max()) ? 0.f : GroundZ;
}

bool ACitixPedestrian::IsPhysicsSettled() const
{
	const UStaticMeshComponent* Torso = Rig.Torso;
	if (!Torso || !Torso->IsSimulatingPhysics())
	{
		return true;
	}
	return Torso->GetPhysicsLinearVelocity().SizeSquared() < 900.f
		&& Torso->GetPhysicsAngularVelocityInDegrees().SizeSquared() < 900.f;
}

void ACitixPedestrian::RemoveConstraints()
{
	for (UPhysicsConstraintComponent* Constraint : RagdollConstraints)
	{
		if (Constraint)
		{
			Constraint->TermComponentConstraint();
			Constraint->DestroyComponent();
		}
	}
	RagdollConstraints.Reset();
}

void ACitixPedestrian::BeginGetUp()
{
	UStaticMeshComponent* Torso = Rig.Torso;
	if (!Torso)
	{
		FastForwardRecovery();
		return;
	}

	// Freeze the physical pose as the first frame of the animation, so the get-up starts
	// exactly where the ragdoll left off and cannot pop.
	const FTransform TorsoTransform = Torso->GetComponentTransform();
	const float BodyYaw = TorsoTransform.Rotator().Yaw;
	FVector FloorLocation = TorsoTransform.GetLocation();

	// The body's actual lowest point, and the surface it should be resting ON. Physics settles
	// a ragdoll slightly into the road, and a limb can end up under a slab, which leaves the
	// whole get-up playing inside the ground: the pavement then hides the body until the pose
	// rises, which is exactly "it disappears for half a second and then stands up".
	const float FeetZ = FMath::Max(0.f, ComputeGroundZ());
	// Never start under the walking surface: a body captured even a few cm low
	// plays the whole get-up inside the pavement and reads as vanished.
	const float SurfaceZ = FMath::Max(FeetZ, RecoveryStandZ);
	const float Lift = SurfaceZ - FeetZ;
	FloorLocation.Z = SurfaceZ;

	const FTransform BodyFrame(FRotator(0.f, BodyYaw, 0.f), FloorLocation);

	RecoveryStartTransforms.SetNum(Rig.Parts.Num());
	for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
	{
		const UStaticMeshComponent* Part = Rig.Parts[Index];
		if (!Part)
		{
			RecoveryStartTransforms[Index] = FTransform::Identity;
			continue;
		}
		// BodyFrame carries a yaw only, so its Z axis is world up: adding the lift here moves
		// every limb up by the same amount and keeps the pose itself untouched.
		FTransform Captured = BodyFrame.Inverse() * Part->GetComponentTransform();
		Captured.AddToTranslation(FVector(0.f, 0.f, Lift));
		RecoveryStartTransforms[Index] = Captured;
	}

	RecoveryStartLocation = FloorLocation;
	RecoveryStartYaw = BodyYaw;
	// Where the body actually came to rest. On a pavement this is the pavement top; if it
	// ever reads as the bare ground plane the body fell through the surface it landed on,
	// which is exactly what made a knock-down seem to sink out of sight.
	UE_LOG(LogCitix, Log, TEXT("[Citix] Ragdoll get-up: body rests at Z=%.1f cm (pavement is %.1f cm)."),
		FloorLocation.Z, RecoveryStandZ);
	// Stand up where the body came to rest. The only deliberate movement is the short
	// step up to pavement height.
	RecoveryTargetLocation = FVector(FloorLocation.X, FloorLocation.Y,
		FMath::Max(FloorLocation.Z, RecoveryStandZ));

	// Hand the limbs back to the actor, then start the animation from the captured pose.
	SetActorLocationAndRotation(FloorLocation, FRotator(0.f, BodyYaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);

	for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
	{
		UStaticMeshComponent* Part = Rig.Parts[Index];
		if (!Part)
		{
			continue;
		}
		Part->SetSimulatePhysics(false);
		Part->SetEnableGravity(false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
		Part->SetRelativeTransform(RecoveryStartTransforms[Index]);
	}

	RemoveConstraints();
	bGettingUp = true;
	GetUpElapsed = 0.f;
	ReactionNet = 3;
	// Belt and suspenders: nothing hides during the handoff, ever.
	Rig.SetVisibility(true);
	SetActiveVisual(true);
}

void ACitixPedestrian::FastForwardRecovery()
{
	// Used when the physics budget is full: skip the animation but still leave the body
	// standing rather than making them vanish.
	if (RagdollKind == ECitixRagdollKind::Death)
	{
		RagdollAge = RagdollHoldTime + RagdollFadeTime;
		return;
	}
	if (!bGettingUp)
	{
		BeginGetUp();
	}
	GetUpElapsed = GetUpDuration;
	ApplyGetUp(1.f);
}

FCitixHumanoidPose ACitixPedestrian::SampleGetUpPose(float T)
{
	FCitixHumanoidPose Pose;
	const float Clamped = FMath::Clamp(T, 0.f, 1.f);

	for (int32 Index = 0; Index < GetUpKeyCount - 1; ++Index)
	{
		const FGetUpKey& A = GetUpKeys[Index];
		const FGetUpKey& B = GetUpKeys[Index + 1];
		if (Clamped < A.Time || Clamped > B.Time)
		{
			continue;
		}
		const float Span = FMath::Max(KINDA_SMALL_NUMBER, B.Time - A.Time);
		// Ease each segment so the motion accelerates out of one key and settles into the next.
		const float U = FMath::SmoothStep(0.f, 1.f, (Clamped - A.Time) / Span);

		Pose.Torso = FMath::Lerp(A.Torso, B.Torso, U);
		Pose.Head = FMath::Lerp(A.Head, B.Head, U);
		Pose.ShoulderL = FMath::Lerp(A.ShoulderL, B.ShoulderL, U);
		Pose.ShoulderR = FMath::Lerp(A.ShoulderR, B.ShoulderR, U);
		Pose.HipL = FMath::Lerp(A.HipL, B.HipL, U);
		Pose.HipR = FMath::Lerp(A.HipR, B.HipR, U);
		Pose.BodyOffset = FMath::Lerp(A.BodyOffset, B.BodyOffset, U);
		return Pose;
	}

	if (Clamped >= GetUpKeys[GetUpKeyCount - 1].Time)
	{
		return Pose;
	}

	const FGetUpKey& First = GetUpKeys[0];
	Pose.Torso = First.Torso;
	Pose.Head = First.Head;
	Pose.ShoulderL = First.ShoulderL;
	Pose.ShoulderR = First.ShoulderR;
	Pose.HipL = First.HipL;
	Pose.HipR = First.HipR;
	Pose.BodyOffset = First.BodyOffset;
	return Pose;
}

void ACitixPedestrian::ApplyGetUp(float T)
{
	// The body rises and turns to face its walking direction only once it is mostly
	// upright, so the turn reads as standing up and looking around rather than spinning
	// on the floor.
	const float MoveAlpha = FMath::SmoothStep(0.35f, 1.f, T);
	const FVector BodyLocation = FMath::Lerp(RecoveryStartLocation, RecoveryTargetLocation, MoveAlpha);
	const float BodyYaw = RecoveryStartYaw + ShortestAngleDelta(RecoveryStartYaw, RecoveryTargetYaw) * MoveAlpha;

	SetActorLocationAndRotation(BodyLocation, FRotator(0.f, BodyYaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);

	// Write the authored pose, then blend out of the captured physical pose into it. The
	// blend does not START until T = 0.35: through the first third the body holds exactly
	// the pose the physics left it in, so there is no frame in which it can be displaced
	// between the two animations. It then morphs (knees under, torso unfolding) over the
	// second third while it rises, which reads as pushing up off the ground.
	Rig.ApplyPose(SampleGetUpPose(T));
	const float PoseAlpha = FMath::SmoothStep(0.35f, 0.78f, T);
	for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
	{
		UStaticMeshComponent* Part = Rig.Parts[Index];
		if (!Part || !RecoveryStartTransforms.IsValidIndex(Index))
		{
			continue;
		}
		Part->SetRelativeTransform(BlendTransform(
			RecoveryStartTransforms[Index], Part->GetRelativeTransform(), PoseAlpha));
	}
}

bool ACitixPedestrian::UpdateRagdoll(float DeltaSeconds)
{
	if (!IsRagdolling())
	{
		return true;
	}

	// The body must be visible for the whole sequence. If anything culled or hid the actor
	// just before the hit (the presentation hides pedestrians it considers distant and
	// unseen), the tumble would play invisibly and then the get-up would appear to pop in -
	// the "vanishes for half a second then stands up" that was reported. Nine components,
	// and it makes the ragdoll-to-get-up handoff gap-free by construction.
	Rig.SetVisibility(true);

	RagdollAge += DeltaSeconds;

	if (RagdollKind == ECitixRagdollKind::Death)
	{
		// Let the body settle, then shrink it away. (Opaque primitives cannot fade
		// opacity, so the gentle disappearance is a shrink rather than an alpha fade.)
		const float FadeAlpha = FMath::Clamp(
			(RagdollAge - RagdollHoldTime) / FMath::Max(0.05f, RagdollFadeTime), 0.f, 1.f);
		const float Scale = 1.f - FadeAlpha;

		for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
		{
			UStaticMeshComponent* Part = Rig.Parts[Index];
			if (!Part)
			{
				continue;
			}
			const float RestScale = Rig.RestTransforms.IsValidIndex(Index)
				? Rig.RestTransforms[Index].GetScale3D().X : 1.f;
			Part->SetWorldScale3D(FVector(RestScale * Scale));

			if (FadeAlpha >= 1.f)
			{
				Part->SetSimulatePhysics(false);
				Part->SetVisibility(false, true);
			}
		}

		return FadeAlpha >= 1.f;
	}

	// ---- Knock-down: tumble, lie still, then get up ----
	if (!bGettingUp)
	{
		SettleQuietTime = IsPhysicsSettled() ? SettleQuietTime + DeltaSeconds : 0.f;

		const bool bSettled = RagdollAge >= MinSettleTime && SettleQuietTime >= 0.1f;
		if (bSettled || RagdollAge >= MaxSettleTime)
		{
			BeginGetUp();
		}
		return false;
	}

	GetUpElapsed += DeltaSeconds;
	const float T = FMath::Clamp(GetUpElapsed / FMath::Max(0.1f, GetUpDuration), 0.f, 1.f);
	ApplyGetUp(T);

	if (T < 1.f)
	{
		return false;
	}

	// Back on their feet: hand a clean walking rig to the system.
	for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
	{
		if (Rig.Parts[Index] && Rig.RestTransforms.IsValidIndex(Index))
		{
			Rig.Parts[Index]->SetRelativeTransform(Rig.RestTransforms[Index]);
		}
	}
	RagdollKind = ECitixRagdollKind::None;
	bGettingUp = false;
	GetUpElapsed = 0.f;
	RagdollAge = 0.f;
	SettleQuietTime = 0.f;
	RecoveryStartTransforms.Reset();
	ReactionNet = 0;
	return true;
}

void ACitixPedestrian::ResetFromRagdoll()
{
	RemoveConstraints();

	for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
	{
		UStaticMeshComponent* Part = Rig.Parts[Index];
		if (!Part)
		{
			continue;
		}
		// Drop the physics state and re-parent so the walk cycle can pose it again.
		Part->SetSimulatePhysics(false);
		Part->SetEnableGravity(false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		if (Rig.RestTransforms.IsValidIndex(Index))
		{
			Part->SetRelativeTransform(Rig.RestTransforms[Index]);
		}
	}
	Rig.ApplyPose(FCitixHumanoidPose::Rest());

	RagdollKind = ECitixRagdollKind::None;
	RagdollAge = 0.f;
	SettleQuietTime = 0.f;
	bGettingUp = false;
	GetUpElapsed = 0.f;
	RecoveryStartTransforms.Reset();
	ReactionNet = 0;
	RemoteAnimAge = 0.f;
	// A recycled death left every part shrunk and invisible: restore all of it,
	// or the walker comes back as a ghost.
	for (int32 Index = 0; Index < Rig.Parts.Num(); ++Index)
	{
		if (Rig.Parts[Index])
		{
			Rig.Parts[Index]->SetVisibility(true, true);
		}
	}
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
	SetActorScale3D(FVector(1.f));
	SetActiveVisual(false);
}
