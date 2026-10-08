// Copyright Epic Games, Inc. All Rights Reserved.
// Line-of-sight test used to gate all pooling of traffic and pedestrians.
//
// Traffic, pedestrians and parked cars must never be spawned, hidden or recycled while
// the player can see them, or they visibly pop in and out. Distance alone is not enough:
// an object 600 m away on a straight avenue is still on screen.
//
// The test is deliberately cheap and two-stage:
//   1. a cone test against the camera (free) - anything behind the player is rejected
//      without a trace; most recycling candidates are behind, so few traces happen;
//   2. a single line trace to confirm the point is not occluded by a building.
//
// Everything fails safe: if there is no valid view, it reports "visible" so nothing is
// pooled rather than risking a pop.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/** Snapshot of the local player's camera. */
struct FCitixPlayerView
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	/** Cosine of the (slightly widened) half field of view. */
	float CosHalfFOV = 0.5f;
	bool bValid = false;
};

class CITIX_API FCitixVisibility
{
public:
	/** Capture the local player's camera. bValid is false if there is no local player. */
	static FCitixPlayerView GetPlayerView(const UWorld* World);

	/** Cheap test: is the point inside the camera's view cone? No trace. */
	static bool IsInViewCone(const FCitixPlayerView& View, const FVector& Point);

	/**
	 * Full test: in the view cone AND not occluded by world geometry.
	 * @param IgnoreActor  usually the player pawn, so its own body does not occlude.
	 */
	static bool IsVisible(const UWorld* World, const FCitixPlayerView& View, const FVector& Point,
		const AActor* IgnoreActor = nullptr);

	/** Convenience wrapper that captures the view itself. */
	static bool IsPointVisible(const UWorld* World, const FVector& Point,
		const AActor* IgnoreActor = nullptr);
};
