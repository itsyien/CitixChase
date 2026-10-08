// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixPoliceOfficer.h"

#include "Net/UnrealNetwork.h"

ACitixPoliceOfficer::ACitixPoliceOfficer()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(15.f);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ACitixPoliceOfficer::BeginPlay()
{
	Super::BeginPlay();

	// Remotes build from the replicated seed (spawn bunch lands first).
	if (!bRigReady && !HasAuthority() && OfficerSeed != 0)
	{
		InitializeOfficer(OfficerSeed);
	}
	LastTickLocation = GetActorLocation();
	bHasLastTickLocation = true;
}

void ACitixPoliceOfficer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Presentation only, remotes only: the server drives officers through the
	// director (stride + dying). Fall plays from the replicated death flag;
	// walk phase derives from replicated motion.
	if (HasAuthority() || DeltaSeconds <= 0.f)
	{
		LastTickLocation = GetActorLocation();
		return;
	}
	if (bDead)
	{
		if (UpdateDying(DeltaSeconds))
		{
			SetActorHiddenInGame(true);
		}
		LastTickLocation = GetActorLocation();
		return;
	}
	const FVector Now = GetActorLocation();
	float Speed = 0.f;
	if (bHasLastTickLocation)
	{
		Speed = FVector::Dist2D(Now, LastTickLocation) / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	}
	LastTickLocation = Now;
	UpdateStride(Speed, DeltaSeconds);
}

void ACitixPoliceOfficer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixPoliceOfficer, bDead);
	DOREPLIFETIME(ACitixPoliceOfficer, OfficerSeed);
}

void ACitixPoliceOfficer::OnRep_Dead()
{
	// The fall plays in Tick from here.
}

void ACitixPoliceOfficer::OnRep_Seed()
{
	if (!bRigReady && OfficerSeed != 0)
	{
		InitializeOfficer(OfficerSeed);
	}
}

void ACitixPoliceOfficer::InitializeOfficer(int32 Seed)
{
	if (bRigReady)
	{
		return;
	}
	bRigReady = true;
	OfficerSeed = Seed;
	FRandomStream Rng(Seed);
	Rig = FCitixCharacterLibrary::BuildCharacter(
		this, GetRootComponent(), FCitixCharacterLibrary::GetRandomStyle(Rng), Rng);
	Rig.ApplyPose(FCitixHumanoidPose::Rest());
}

void ACitixPoliceOfficer::UpdateStride(float SpeedCmS, float DeltaSeconds)
{
	if (SpeedCmS < 10.f)
	{
		Rig.ApplyPose(FCitixHumanoidPose::Rest());
		return;
	}
	// Walk phase from distance travelled, like the pedestrian presentations.
	WalkPhase = FMath::Fmod(WalkPhase + (SpeedCmS * DeltaSeconds) / 150.f * PI, 2.f * PI);
	Rig.UpdateWalk(WalkPhase, 28.f, 4.f);
}

bool ACitixPoliceOfficer::UpdateDying(float DeltaSeconds)
{
	DyingTimer += DeltaSeconds;
	// Tip over fast, then shrink away: readable, non-graphic, cheap.
	const float Fall = FMath::Clamp(DyingTimer / 0.35f, 0.f, 1.f);
	SetActorRotation(FRotator(-90.f * Fall, GetActorRotation().Yaw, 0.f));
	if (DyingTimer > 0.35f)
	{
		const float Fade = FMath::Clamp((DyingTimer - 0.35f) / 0.8f, 0.f, 1.f);
		const float Scale = 1.f - Fade;
		SetActorScale3D(FVector(Scale, Scale, FMath::Max(0.01f, Scale)));
	}
	return DyingTimer >= 1.15f;
}
