// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixWeaponProp.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "GameFramework/Pawn.h"

ACitixWeaponProp::ACitixWeaponProp()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto MakePart = [this, Root](const TCHAR* Name)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Root);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->SetReceivesDecals(false);
		return Part;
	};

	BodyMesh = MakePart(TEXT("Body"));
	BarrelMesh = MakePart(TEXT("Barrel"));
	GripMesh = MakePart(TEXT("Grip"));
	SightMesh = MakePart(TEXT("Sight"));
	SetActorHiddenInGame(true);
}

void ACitixWeaponProp::BeginPlay()
{
	Super::BeginPlay();

	UStaticMesh* BoxMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);
	UMaterialInterface* DarkMat = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::PropDark);
	UMaterialInterface* GlowMat = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool);
	for (UStaticMeshComponent* Part : { BodyMesh, BarrelMesh, GripMesh })
	{
		if (Part)
		{
			if (BoxMesh)
			{
				Part->SetStaticMesh(BoxMesh);
			}
			if (DarkMat)
			{
				Part->SetMaterial(0, DarkMat);
			}
		}
	}
	if (SightMesh)
	{
		if (BoxMesh)
		{
			SightMesh->SetStaticMesh(BoxMesh);
		}
		if (GlowMat)
		{
			SightMesh->SetMaterial(0, GlowMat);
		}
	}
	SetWeaponKind(0);
	SetActorTickEnabled(false);
}

void ACitixWeaponProp::SetWeaponKind(int32 Kind)
{
	WeaponKind = FMath::Clamp(Kind, 0, 3);
	// Engine cube is 100 cm. Compact pistol up to a long sniper barrel.
	const float BarrelLength = (WeaponKind == 3) ? 0.85f : (WeaponKind == 0 ? 0.28f : 0.5f);
	BarrelTipX = 21.f + BarrelLength * 100.f;
	if (BodyMesh)
	{
		BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.09f, 0.13f));
		BodyMesh->SetRelativeLocation(FVector::ZeroVector);
	}
	if (BarrelMesh)
	{
		BarrelMesh->SetRelativeScale3D(FVector(BarrelLength, 0.045f, 0.045f));
		BarrelMesh->SetRelativeLocation(FVector(21.f + BarrelLength * 50.f, 0.f, 3.f));
	}
	if (GripMesh)
	{
		GripMesh->SetRelativeScale3D(FVector(0.07f, 0.06f, 0.16f));
		GripMesh->SetRelativeLocation(FVector(-12.f, 0.f, -10.f));
	}
	if (SightMesh)
	{
		// Glowing sight dot: reads at dusk and marks the aim line.
		SightMesh->SetRelativeScale3D(FVector(0.14f, 0.045f, 0.05f));
		SightMesh->SetRelativeLocation(FVector(10.f, 0.f, 9.f));
	}
}

FVector ACitixWeaponProp::GetMuzzleLocation() const
{
	const FVector Forward = GetActorRotation().Vector();
	return GetActorLocation() + Forward * BarrelTipX + FVector(0.f, 0.f, 3.f);
}

void ACitixWeaponProp::FollowPawn(APawn* Pawn)
{
	FollowTarget = Pawn;
	const bool bShow = Pawn != nullptr;
	SetActorHiddenInGame(!bShow);
	SetActorTickEnabled(bShow);
}

void ACitixWeaponProp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Pawn = FollowTarget.Get();
	if (!Pawn)
	{
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		return;
	}

	// Over-shoulder carry: beside the chest (origin is the capsule centre, ~88 cm
	// above the feet - not the feet). Well clear of torso and arms, in open air
	// from the follow camera. ADS slides it to the centre line. The barrel
	// pitches with the camera, so the gun aims where you look.
	const FVector PawnLocation = Pawn->GetActorLocation();
	const FRotator PawnRotation(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	const FVector Forward = PawnRotation.Vector();
	const FVector Right = FRotator(0.f, PawnRotation.Yaw + 90.f, 0.f).Vector();
	const FVector HipOffset = Forward * 60.f + Right * 52.f + FVector(0.f, 0.f, 25.f);
	const FVector AdsOffset = Forward * 65.f + Right * 12.f + FVector(0.f, 0.f, 35.f);
	SetActorLocation(PawnLocation + FMath::Lerp(HipOffset, AdsOffset, AdsAmount));
	SetActorRotation(FRotator(AimPitch, PawnRotation.Yaw, 0.f));
	(void)DeltaSeconds;
}
