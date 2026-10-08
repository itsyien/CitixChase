// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CitixDriftSmokeComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixVehiclePawn.h"

UCitixDriftSmokeComponent::UCitixDriftSmokeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCitixDriftSmokeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Movement = Owner->FindComponentByClass<UCitixVehicleMovementComponent>();
	}
	EnsurePool();
}

void UCitixDriftSmokeComponent::EnsurePool()
{
	if (bPoolReady)
	{
		return;
	}
	bPoolReady = true;

	AActor* Owner = GetOwner();
	if (!Owner || MaxPuffs <= 0)
	{
		return;
	}

	UStaticMesh* SphereMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::Foliage);
	if (!SphereMesh)
	{
		return;
	}

	Puffs.Reserve(MaxPuffs);
	for (int32 Index = 0; Index < MaxPuffs; ++Index)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			Owner, *FString::Printf(TEXT("SmokePuff_%d"), Index));
		if (!Component)
		{
			continue;
		}
		Component->SetupAttachment(Owner->GetRootComponent());
		Component->SetAbsolute(true, true, true);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(SphereMesh);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetRelativeScale3D(FVector(0.001f));
		Component->SetVisibility(false, true);
		Component->RegisterComponent();

		FCitixSmokePuff Puff;
		Puff.Component = Component;
		Puffs.Add(Puff);
	}
}

int32 UCitixDriftSmokeComponent::GetActivePuffCount() const
{
	int32 Count = 0;
	for (const FCitixSmokePuff& Puff : Puffs)
	{
		Count += Puff.bActive ? 1 : 0;
	}
	return Count;
}

void UCitixDriftSmokeComponent::EmitPuff(const FVector& Location, const FVector& BaseVelocity, float Intensity)
{
	if (Puffs.Num() == 0)
	{
		return;
	}

	// Round-robin over the pool: the oldest puff is the one we are about to reuse.
	FCitixSmokePuff* Puff = &Puffs[NextPuffIndex];
	NextPuffIndex = (NextPuffIndex + 1) % Puffs.Num();

	if (!Puff->Component)
	{
		return;
	}

	const float Life = FMath::FRandRange(PuffLifeMin, PuffLifeMax);
	const float SizeScale = FMath::Lerp(0.75f, 1.25f, Intensity);

	Puff->bActive = true;
	Puff->Age = 0.f;
	Puff->Life = Life;
	Puff->StartSize = PuffStartSize * SizeScale;
	Puff->PeakSize = PuffPeakSize * SizeScale;
	Puff->Velocity = BaseVelocity + FVector(
		FMath::FRandRange(-PuffSpreadSpeed, PuffSpreadSpeed),
		FMath::FRandRange(-PuffSpreadSpeed, PuffSpreadSpeed),
		FMath::FRandRange(PuffRiseSpeed * 0.4f, PuffRiseSpeed * 1.4f));
	Puff->Spin = FRotator(
		FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f));

	// Slight shade variation so overlapping puffs read as volume, using cached materials.
	static const float Shades[] = { 0.42f, 0.52f, 0.62f, 0.72f };
	const float Shade = Shades[FMath::RandRange(0, 3)];
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetTintedMaterial(
		ECitixSurface::Foliage, FLinearColor(Shade, Shade, Shade)))
	{
		Puff->Component->SetMaterial(0, Material);
	}

	Puff->Component->SetWorldLocationAndRotation(Location, FRotator::ZeroRotator);
	Puff->Component->SetVisibility(true, true);
}

void UCitixDriftSmokeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bPoolReady)
	{
		EnsurePool();
	}
	if (Puffs.Num() == 0 || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// --- Emit -----------------------------------------------------------
	// Tyre smoke is tied to the handbrake (Space): holding it at speed breaks
	// traction and smokes. Slip only scales the amount, it never triggers it.
	CurrentSlipSpeed = 0.f;
	float Emission = 0.f;

	const ACitixVehiclePawn* Car = Cast<ACitixVehiclePawn>(GetOwner());
	const UCitixVehicleMovementComponent* VehicleMovement = Movement.Get();
	const bool bRemoteCopy = Car && !Car->HasAuthority() && !Car->IsLocallyControlled();

	if (bRemoteCopy)
	{
		// Clients do not simulate the car, so the movement component has no state.
		// Read the replicated snapshot instead so the guest still sees tyre smoke.
		const FCitixVehicleNetState& State = Car->NetState;
		const float Speed = FMath::Abs(State.SpeedKmh) * 100.f / 36.f; // km/h -> cm/s
		const float Slip = State.LateralSlip;
		CurrentSlipSpeed = Slip;

		if (State.bHandbrake && Speed > MinSpeedForSmoke)
		{
			const float SlipAlpha = FMath::Clamp(
				(Slip - MinSlipSpeed) / FMath::Max(1.f, FullSlipSpeed - MinSlipSpeed), 0.f, 1.f);
			Emission = HandbrakeEmissionRate + SlipEmissionRate * SlipAlpha;
		}
	}
	else if (VehicleMovement)
	{
		const float Speed = FMath::Abs(VehicleMovement->GetForwardSpeed());
		const float Slip = VehicleMovement->GetMaxLateralSlipSpeed();
		CurrentSlipSpeed = Slip;

		if (VehicleMovement->IsHandbrakeEngaged() && Speed > MinSpeedForSmoke)
		{
			const float SlipAlpha = FMath::Clamp(
				(Slip - MinSlipSpeed) / FMath::Max(1.f, FullSlipSpeed - MinSlipSpeed), 0.f, 1.f);
			Emission = HandbrakeEmissionRate + SlipEmissionRate * SlipAlpha;
		}
	}

	if (Emission > 0.f)
	{
		EmitAccumulator += Emission * DeltaTime;

		// Rear wheels only: that is where tyre smoke comes from.
		FVector EmitPoints[2];
		int32 EmitPointCount = 0;
		if (!bRemoteCopy)
		{
			if (const UCitixVehicleMovementComponent* LocalMovement = Movement.Get())
			{
				for (int32 WheelIndex : { 2, 3 })
				{
					FVector WheelLocation;
					if (LocalMovement->GetWheelWorldLocation(WheelIndex, WheelLocation))
					{
						EmitPoints[EmitPointCount++] = WheelLocation;
					}
				}
			}
		}
		if (EmitPointCount == 0 && Car)
		{
			// Remotes have no wheel state: use the rear axle from the car transform.
			const FVector Back = Car->GetActorLocation() - Car->GetActorForwardVector() * 150.f - FVector(0.f, 0.f, 20.f);
			const FVector Right = Car->GetActorRightVector() * 80.f;
			EmitPoints[0] = Back + Right;
			EmitPoints[1] = Back - Right;
			EmitPointCount = 2;
		}

		while (EmitAccumulator >= 1.f)
		{
			EmitAccumulator -= 1.f;
			const FVector Origin = (EmitPointCount > 0)
				? EmitPoints[FMath::RandRange(0, EmitPointCount - 1)]
				: GetOwner()->GetActorLocation();
			const FVector Jitter(FMath::FRandRange(-18.f, 18.f), FMath::FRandRange(-18.f, 18.f), FMath::FRandRange(-6.f, 10.f));
			const float Intensity = FMath::Clamp(Emission / FMath::Max(1.f, HandbrakeEmissionRate), 0.f, 1.f);
			EmitPuff(Origin + Jitter, FVector::ZeroVector, Intensity);
		}
	}
	else
	{
		EmitAccumulator = 0.f;
	}

	// --- Simulate -------------------------------------------------------
	for (FCitixSmokePuff& Puff : Puffs)
	{
		if (!Puff.bActive || !Puff.Component)
		{
			continue;
		}

		Puff.Age += DeltaTime;
		const float Alpha = Puff.Age / Puff.Life;

		if (Alpha >= 1.f)
		{
			Puff.bActive = false;
			Puff.Component->SetVisibility(false, true);
			continue;
		}

		// Grow to the peak around the middle of the life, then shrink away.
		const float SizeAlpha = FMath::Sin(Alpha * PI);
		const float Size = FMath::Lerp(Puff.StartSize, Puff.PeakSize, SizeAlpha);
		Puff.Component->SetWorldScale3D(FVector(Size / FCitixSurfaceLibrary::PrimitiveSize));

		Puff.Velocity *= FMath::Max(0.f, 1.f - PuffDrag * DeltaTime);
		Puff.Component->AddWorldOffset(Puff.Velocity * DeltaTime, false);
		Puff.Component->AddLocalRotation(Puff.Spin * DeltaTime);
	}
}
