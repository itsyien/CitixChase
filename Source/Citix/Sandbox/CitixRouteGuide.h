// Copyright Epic Games, Inc. All Rights Reserved.
// In-scene route guide: a pool of flat glowing chevrons laid along the active
// route, so the player can follow delivery/quest/waypoint navigation without
// opening the map. One instanced component, re-laid at 4 Hz, hidden with no route.

#pragma once

#include "CoreMinimal.h"
#include "Core/CitixTypes.h"
#include "GameFramework/Actor.h"
#include "CitixRouteGuide.generated.h"

class UInstancedStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixRouteGuide : public AActor
{
	GENERATED_BODY()

public:
	ACitixRouteGuide();

	virtual void BeginPlay() override;

	/** Re-lay chevrons along a world-space route (cm). Empty = hidden. */
	void UpdateRoute(const TArray<FVector>& Points, ECitixSurface Surface);

	void Clear();

	int32 GetMarkerCount() const { return MarkerCount; }

	/** Chevron spacing along the route, cm. */
	UPROPERTY(EditAnywhere, Category = "Citix|Guide")
	float MarkerSpacing = 1500.f;
	FVector MarkerScale = FVector(7.f, 1.2f, .22f);

	/** Pool cap: bounds the per-update matrix work. */
	UPROPERTY(EditAnywhere, Category = "Citix|Guide")
	int32 MaxMarkers = 128;

private:
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Markers;

	ECitixSurface CurrentSurface = ECitixSurface::EmissiveCool;
	bool bSurfaceSet = false;
	int32 MarkerCount = 0;
};
