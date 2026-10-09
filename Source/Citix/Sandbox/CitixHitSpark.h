// Copyright Epic Games, Inc. All Rights Reserved.
// Hit spark: one pooled code-driven impact puff. A small emissive box pops out
// with a random velocity, falls a little, shrinks away. Actors tick only while
// a burst is live; the pool is small and shared by gunfire, rams and kills.

#pragma once

#include "CoreMinimal.h"
#include "Core/CitixTypes.h"
#include "GameFramework/Actor.h"
#include "CitixHitSpark.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixHitSpark : public AActor
{
	GENERATED_BODY()

public:
	ACitixHitSpark();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Launch one spark. Reuses this actor if it is idle. */
	void Fire(const FVector& From, const FVector& Velocity, ECitixSurface Surface);
	static void SpawnRapidBrake(UWorld* World,const FVector& Location,const FVector& Velocity);
	static void SpawnRelaySprinkles(UWorld* World,const FVector& Location,const FVector& Velocity);
	static void SpawnPursuitPulse(UWorld* World,const FVector& Location,const FVector& Velocity,bool Energy);
	static void SpawnPixelBurst(UWorld* World, const FVector& Location, bool bExplosion, bool bRedImpact = false);

	bool IsIdle() const { return !bActive; }

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SparkMesh;

	FVector SparkVelocity = FVector::ZeroVector;
	float Life = 0.45f;
	float PixelScale = .09f;
	float Gravity = 1200.f;
	FVector Spin = FVector::ZeroVector;
	float Age = 0.f;
 FVector ShapeScale=FVector::OneVector;
	bool bActive = false;
};
