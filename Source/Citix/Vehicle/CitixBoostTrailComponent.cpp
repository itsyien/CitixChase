// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CitixBoostTrailComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixVehiclePawn.h"

UCitixBoostTrailComponent::UCitixBoostTrailComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCitixBoostTrailComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Movement = Owner->FindComponentByClass<UCitixVehicleMovementComponent>();
	}
	EnsurePool();
}

void UCitixBoostTrailComponent::EnsurePool()
{
	if (bPoolReady)
	{
		return;
	}
	bPoolReady = true;

	AActor* Owner = GetOwner();
	if (!Owner || MaxFlames <= 0)
	{
		return;
	}

	UStaticMesh* CubeMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);
	if (!CubeMesh)
	{
		return;
	}

	Flames.Reserve(MaxFlames);
	for (int32 Index = 0; Index < MaxFlames; ++Index)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			Owner, *FString::Printf(TEXT("BoostFlame_%d"), Index));
		if (!Component)
		{
			continue;
		}
		Component->SetupAttachment(Owner->GetRootComponent());
		Component->SetAbsolute(true, true, true);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(CubeMesh);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetRelativeScale3D(FVector(0.001f));
		Component->SetVisibility(false, true);
		Component->RegisterComponent();

		FCitixFlameChunk Chunk;
		Chunk.Component = Component;
		Flames.Add(Chunk);
	}
}

int32 UCitixBoostTrailComponent::GetActiveFlameCount() const
{
	int32 Count = 0;
	for (const FCitixFlameChunk& Chunk : Flames)
	{
		Count += Chunk.bActive ? 1 : 0;
	}
	return Count;
}

void UCitixBoostTrailComponent::EmitFlame()
{
	if (Flames.Num() == 0)
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FCitixFlameChunk* Chunk = &Flames[NextFlameIndex];
	NextFlameIndex = (NextFlameIndex + 1) % Flames.Num();
	if (!Chunk->Component)
	{
		return;
	}

	// Spawn at the rear of the car, alternating left/right exhaust.
	const FVector Back = -Owner->GetActorForwardVector();
	const FVector Right = Owner->GetActorRightVector();
	const FVector Exhaust = Owner->GetActorLocation() + Back * 210.f
		+ Right * ((NextFlameIndex % 2 == 0) ? -70.f : 70.f)
		- FVector(0.f, 0.f, 20.f);

	// Red-hot with occasional hotter core, matching the low-poly palette.
	const bool Energy=Movement.IsValid() && Movement->bChaseBreakawayBoost;
 const FLinearColor Colour = Energy ? FLinearColor(1.f,.78f,.06f) : (FMath::Rand() % 3 == 0)
		? FCitixSurfaceLibrary::GetColor(ECitixSurface::EmissiveWarm)
		: FCitixSurfaceLibrary::GetColor(ECitixSurface::TailLight);

	Chunk->bActive = true;
	Chunk->Age = 0.f;
	Chunk->Life = FMath::FRandRange(FlameLifeMin, FlameLifeMax);
	Chunk->StartSize = FlameStartSize * FMath::FRandRange(0.7f, 1.25f)*(Energy ? .55f : 1.f);
	Chunk->Velocity = Back * FlameTrailSpeed
		+ Right * FMath::FRandRange(-120.f, 120.f)
		+ FVector(0.f, 0.f, FMath::FRandRange(-40.f, 60.f));

	if (UMaterialInterface* Material = Energy ? FCitixSurfaceLibrary::GetTintedEmissiveMaterial(Colour) : FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface::CarPaint, Colour))
	{
		Chunk->Component->SetMaterial(0, Material);
	}

	Chunk->Component->SetWorldLocationAndRotation(Exhaust,
		FRotator(FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f)));
	Chunk->Component->SetVisibility(true, true);
}

void UCitixBoostTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bPoolReady)
	{
		EnsurePool();
	}
	if (Flames.Num() == 0 || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Clients do not simulate the car: use the replicated display state so the
	// guest still sees the boost trail.
	const ACitixVehiclePawn* Car = Cast<ACitixVehiclePawn>(GetOwner());
	const bool bBoosting = Car
		? Car->IsDisplayBoosting()
		: (Movement.IsValid() && Movement->IsBoosting());

	if (bBoosting)
	{
		EmitAccumulator += EmissionRate * DeltaTime;
		while (EmitAccumulator >= 1.f)
		{
			EmitAccumulator -= 1.f;
			EmitFlame();
		}
	}
	else
	{
		EmitAccumulator = 0.f;
	}

	for (FCitixFlameChunk& Chunk : Flames)
	{
		if (!Chunk.bActive || !Chunk.Component)
		{
			continue;
		}

		Chunk.Age += DeltaTime;
		const float Alpha = Chunk.Age / Chunk.Life;
		if (Alpha >= 1.f)
		{
			Chunk.bActive = false;
			Chunk.Component->SetVisibility(false, true);
			continue;
		}

		// Shrink away as it burns out; no billboards, just chunky cubes.
		const float Size = Chunk.StartSize * (1.f - Alpha) * (1.f - Alpha);
		Chunk.Component->SetWorldScale3D(FVector(Size / FCitixSurfaceLibrary::PrimitiveSize));

		Chunk.Velocity *= FMath::Max(0.f, 1.f - 3.5f * DeltaTime);
		Chunk.Component->AddWorldOffset(Chunk.Velocity * DeltaTime, false);
	}
}
