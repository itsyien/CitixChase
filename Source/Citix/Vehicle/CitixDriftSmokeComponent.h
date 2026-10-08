// Copyright Epic Games, Inc. All Rights Reserved.
// Tyre smoke particle emitter, driven entirely from code so the project needs no
// authored FX asset. A pool of primitive "puffs" is recycled: each grows, drifts,
// rises and shrinks out.
//
// This is deliberately a self-contained emitter. When an artist authors a proper
// sprite-based Niagara system, this component is the single place to swap it in.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CitixDriftSmokeComponent.generated.h"

class UStaticMeshComponent;
class UCitixVehicleMovementComponent;

/** One recycled smoke puff. */
USTRUCT()
struct FCitixSmokePuff
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Component = nullptr;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	FRotator Spin = FRotator::ZeroRotator;

	UPROPERTY()
	float Age = 0.f;

	UPROPERTY()
	float Life = 1.f;

	UPROPERTY()
	float StartSize = 26.f;

	UPROPERTY()
	float PeakSize = 135.f;

	UPROPERTY()
	bool bActive = false;
};

UCLASS(ClassGroup = (Citix), meta = (BlueprintSpawnableComponent), HideCategories = (Variable))
class CITIX_API UCitixDriftSmokeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCitixDriftSmokeComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Citix|FX")
	int32 GetActivePuffCount() const;

	/** Latest computed slip magnitude, cm/s. */
	UFUNCTION(BlueprintPure, Category = "Citix|FX")
	float GetSlipSpeed() const { return CurrentSlipSpeed; }

	// ---- Tuning -------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	int32 MaxPuffs = 56;

	/** Lateral slip (cm/s) at or above which full-rate smoke is emitted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "1"))
	float FullSlipSpeed = 900.f;

	/** Below this slip nothing is emitted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	float MinSlipSpeed = 200.f;

	/** Vehicle must be moving at least this fast (cm/s) for smoke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	float MinSpeedForSmoke = 200.f;

	/** Puffs per second while the handbrake is held at speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	float HandbrakeEmissionRate = 52.f;

	/** Extra puffs/s scaled by lateral slip while the handbrake is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	float SlipEmissionRate = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0.05"))
	float PuffLifeMin = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0.05"))
	float PuffLifeMax = 1.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float PuffStartSize = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float PuffPeakSize = 92.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float PuffRiseSpeed = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float PuffDrag = 1.6f;

	/** Random lateral spread applied to a puff's velocity, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float PuffSpreadSpeed = 120.f;

protected:
	void EnsurePool();
	void EmitPuff(const FVector& Location, const FVector& BaseVelocity, float Intensity);

	UPROPERTY()
	TArray<FCitixSmokePuff> Puffs;

	TWeakObjectPtr<UCitixVehicleMovementComponent> Movement;
	float EmitAccumulator = 0.f;
	float CurrentSlipSpeed = 0.f;
	int32 NextPuffIndex = 0;
	bool bPoolReady = false;
};
