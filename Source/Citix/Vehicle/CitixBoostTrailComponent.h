// Copyright Epic Games, Inc. All Rights Reserved.
// Low-poly red fire trail spat out behind the car while boosting.
//
// Same idea as the tyre smoke: a small pool of emissive primitives, recycled, so the
// effect costs one component and no physics. Kept deliberately chunky ("low poly") to
// match the art style.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CitixBoostTrailComponent.generated.h"

class UStaticMeshComponent;
class UCitixVehicleMovementComponent;

/** One recycled flame chunk. */
USTRUCT()
struct FCitixFlameChunk
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Component = nullptr;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	float Age = 0.f;

	UPROPERTY()
	float Life = 0.5f;

	UPROPERTY()
	float StartSize = 40.f;

	UPROPERTY()
	bool bActive = false;
};

UCLASS(ClassGroup = (Citix), meta = (BlueprintSpawnableComponent), HideCategories = (Variable))
class CITIX_API UCitixBoostTrailComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCitixBoostTrailComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Citix|FX")
	int32 GetActiveFlameCount() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	int32 MaxFlames = 54;

	/** Flames emitted per second while boosting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0"))
	float EmissionRate = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0.05"))
	float FlameLifeMin = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX", meta = (ClampMin = "0.05"))
	float FlameLifeMax = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float FlameStartSize = 46.f;

	/** How fast flames shoot backwards relative to the car, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|FX")
	float FlameTrailSpeed = 900.f;

protected:
	void EnsurePool();
	void EmitFlame();

	UPROPERTY()
	TArray<FCitixFlameChunk> Flames;

	TWeakObjectPtr<UCitixVehicleMovementComponent> Movement;
	float EmitAccumulator = 0.f;
	int32 NextFlameIndex = 0;
	bool bPoolReady = false;
};
