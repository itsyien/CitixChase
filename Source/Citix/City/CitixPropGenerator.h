// Copyright Epic Games, Inc. All Rights Reserved.
// Street furniture builders. All emit FCitixBoxInstance data; none touch the scene.
// Swapping these for real meshes later is a contained change.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"

class CITIX_API FCitixPropGenerator
{
public:
	/** A street lamp: pole + head + emissive lamp sphere. BaseZ is the sidewalk top. */
	static void AddStreetLamp(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base,
		float Height, float YawDegrees);

	/** A simple two-part tree: trunk + canopy. */
	static void AddTree(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base,
		float Scale, FRandomStream& Rng);

	/** A traffic signal gantry: pole, horizontal arm, signal head and lights. */
	static void AddTrafficLight(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base,
		float ArmLength, float YawDegrees, bool bWarm);

	/** A road-side bollard / planter, used to add fine detail. */
	static void AddPlanter(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, FRandomStream& Rng);
	static void AddBush(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, float Scale, float YawDegrees, FRandomStream& Rng);

	/** A sidewalk bench: seat slab plus two leg blocks. Base is the sidewalk top. */
	static void AddBench(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, float YawDegrees);

	/** A rooftop / roadside AC unit cluster (cheap detail). */
	static void AddACUnit(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, FRandomStream& Rng);
};
