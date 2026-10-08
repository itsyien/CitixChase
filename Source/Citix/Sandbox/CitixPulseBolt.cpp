// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixPulseBolt.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"

ACitixPulseBolt::ACitixPulseBolt()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BoltMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoltMesh"));
	BoltMesh->SetupAttachment(Root);
	BoltMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoltMesh->SetCanEverAffectNavigation(false);
	BoltMesh->SetCastShadow(false);
	BoltMesh->SetReceivesDecals(false);
	BoltMesh->SetVisibility(false, true);
}

void ACitixPulseBolt::BeginPlay()
{
	Super::BeginPlay();

	if (BoltMesh)
	{
		if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
		{
			BoltMesh->SetStaticMesh(Mesh);
		}
	}
	SetActorTickEnabled(false);
}

void ACitixPulseBolt::Fire(const FVector& From, const FVector& To, ECitixSurface TracerSurface)
{
	FlightFrom = From;
	FlightTo = To;
	FlightAge = 0.f;
	ImpactAge = 0.f;
	bActive = true;
	bImpactPhase = false;

	SetActorLocation(From);
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(TracerSurface))
	{
		BoltMesh->SetMaterial(0, Material);
	}
	// Small bright pellet, stretched along its travel direction.
	const FVector Direction = (To - From).GetSafeNormal();
	SetActorRotation(Direction.Rotation());
	BoltMesh->SetRelativeScale3D(FVector(0.5f, 0.09f, 0.09f));
	BoltMesh->SetVisibility(true, true);
	SetActorTickEnabled(true);
}

void ACitixPulseBolt::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bActive)
	{
		return;
	}

	if (!bImpactPhase)
	{
		FlightAge += DeltaSeconds;
		const float Alpha = FMath::Clamp(FlightAge / FMath::Max(0.01f, FlightDuration), 0.f, 1.f);
		SetActorLocation(FMath::Lerp(FlightFrom, FlightTo, Alpha));
		if (Alpha >= 1.f)
		{
			bImpactPhase = true;
			if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveWarm))
			{
				BoltMesh->SetMaterial(0, Material);
			}
		}
		return;
	}

	// Impact pop: grow and fade over a blink, then go idle.
	ImpactAge += DeltaSeconds;
	const float Pop = FMath::Clamp(ImpactAge / 0.16f, 0.f, 1.f);
	BoltMesh->SetRelativeScale3D(FVector(0.5f + Pop * 1.6f, 0.09f + Pop * 1.2f, 0.09f + Pop * 1.2f));
	if (Pop >= 1.f)
	{
		bActive = false;
		bImpactPhase = false;
		BoltMesh->SetVisibility(false, true);
		SetActorTickEnabled(false);
	}
}
