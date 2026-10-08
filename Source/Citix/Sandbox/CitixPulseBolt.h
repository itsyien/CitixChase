// Copyright Epic Games, Inc. All Rights Reserved.
// Stylised pulse bolt: a pooled visual for the non-graphic ranged tool.
// One small emissive box flies to the hit point, then pops into a brief impact
// flash. Actors tick only while a shot is in flight; the pool is tiny (8).

#pragma once

#include "CoreMinimal.h"
#include "Core/CitixTypes.h"
#include "GameFramework/Actor.h"
#include "CitixPulseBolt.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixPulseBolt : public AActor
{
	GENERATED_BODY()

public:
	ACitixPulseBolt();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Launch a bolt from From to To. Reuses this actor if it is idle. */
	void Fire(const FVector& From, const FVector& To,
		ECitixSurface TracerSurface = ECitixSurface::EmissiveCool);

	bool IsIdle() const { return !bActive; }

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BoltMesh;

	FVector FlightFrom = FVector::ZeroVector;
	FVector FlightTo = FVector::ZeroVector;
	float FlightDuration = 0.12f;
	float FlightAge = 0.f;
	float ImpactAge = 0.f;
	bool bActive = false;
	bool bImpactPhase = false;
};
