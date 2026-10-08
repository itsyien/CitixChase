// Copyright Epic Games, Inc. All Rights Reserved.
// Turns a cheap FCitixBuildingSpec into a small set of instanced boxes.
// Geometry-only and stateless so it can be unit-tested and reused by any producer.
//
// Archetypes give the skyline genuine variety, inspired by the Pudong cluster:
// twisting supertalls, slabs with a crown opening, stepped ziggurats, twin towers,
// cylindrical towers, spired masts, courtyard blocks, apartment slabs, shophouse
// rows and industrial sheds.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"

class UCitixCitySettings;

class CITIX_API FCitixBuildingGenerator
{
public:
	/** Appends instanced geometry for a spec. Handles buildings, plazas, parks, parking. */
	static void GenerateBuildingBoxes(
		const FCitixBuildingSpec& Spec,
		const UCitixCitySettings& Settings,
		FRandomStream& Rng,
		TArray<FCitixBoxInstance>& OutBoxes);

	/** Primary facade palette entry for a district/style combination. */
	static ECitixSurface GetFacadeSurface(ECitixDistrict District, int32 Style);

	/** Contrasting accent used for bands, podiums and roof detail. */
	static ECitixSurface GetAccentSurface(ECitixDistrict District, int32 Style);

	/** Pick a plausible archetype for a district. */
	static ECitixBuildingArchetype PickArchetype(ECitixDistrict District, FRandomStream& Rng);

	/** Tall signature archetypes that make good landmarks. */
	static bool IsLandmarkArchetype(ECitixBuildingArchetype Archetype);

private:
	static void GenerateBox(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GeneratePodiumTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateSetbackTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateTwistedTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateCrownedTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateHoledSlab(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateTwinTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateCylinderTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateSpireTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateCourtyard(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateApartmentSlab(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateShophouse(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateWarehouse(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GeneratePearlBroadcastTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateTwistingSupertall(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateCrownOpeningTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
	static void GenerateTieredCrownTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng, TArray<FCitixBoxInstance>& Out);
};
