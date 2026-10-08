// Copyright Epic Games, Inc. All Rights Reserved.
// Builds the city geometry ON the approved macro plan.
//
// Everything here reads the plan and never invents layout of its own:
//   * roads, sidewalks, markings and junctions follow the plan's road graph
//   * the river and its embankment follow the plan's river
//   * blocks, parcels, buildings and landmarks follow the plan's blocks and zones
//
// Geometry safety is structural, not hopeful: roads are trimmed at each junction pad so
// no two road meshes overlap, parcels are inset from the roads that bound them so no
// building can touch a road, and buildings sit strictly inside their parcel.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"

struct FCitixCityPlan;
struct FCitixRoadNetwork;
struct FCitixBoxInstance;
struct FCitixPlanBlock;
class UCitixCitySettings;

/** What a parcel inside a block is used for. */
enum class ECitixParcelUse : uint8
{
	Building,
	Park,
	Plaza,
	Parking,
	Empty
};

/** One parcel: a building lot, inside a block, aligned to the block's own frame. */
struct FCitixParcel
{
	FVector2D Centre = FVector2D::ZeroVector;
	FVector2D HalfExtents = FVector2D(1000.f, 1000.f);
	float Yaw = 0.f;
	int32 DistrictIndex = INDEX_NONE;
	ECitixPlanZone Zone = ECitixPlanZone::MediumDensity;
	ECitixParcelUse Use = ECitixParcelUse::Building;
	/** Chance this parcel becomes a signature landmark tower. */
	float LandmarkChance = 0.f;
	/** Deterministic Shanghai-inspired hero landmark, if this parcel hosts one. */
	ECitixLandmarkStyle LandmarkStyle = ECitixLandmarkStyle::None;
};

/** Everything the build stage produced, for logging and for the next stages. */
struct FCitixBuildResult
{
	int32 RoadSegments = 0;
	int32 Junctions = 0;
	int32 Blocks = 0;
	int32 Parcels = 0;
	int32 Buildings = 0;
	int32 Landmarks = 0;
	int32 HeroInstances = 0;
	int32 LocalStreets = 0;
	int32 LampLocations = 0;
	int32 PropInstances = 0;
	int32 TotalInstances = 0;
};

/**
 * Emits the city as box instances. The caller routes them into whatever presentation it
 * uses (chunked instanced mesh components), so this stays free of scene objects.
 */
class CITIX_API FCitixCityBuilder
{
public:
	/**
	 * @param Emit  called once per box instance, already in world space.
	 */
	static FCitixBuildResult Build(
		const FCitixCityPlan& Plan,
		const FCitixRoadNetwork& Graph,
		const UCitixCitySettings& Settings,
		FRandomStream& Rng,
		TFunctionRef<void(const FCitixBoxInstance&)> Emit,
		TArray<FVector>& OutLampLocations,
		TArray<FVector>& OutLandmarkLocations);

	/**
	 * Subdivide the plan's blocks into parcels. Exposed separately so the parcel layout
	 * can be inspected without building anything on it.
	 */
	static TArray<FCitixParcel> GenerateParcels(
		const FCitixCityPlan& Plan,
		const FCitixRoadNetwork& Graph,
		const UCitixCitySettings& Settings,
		FRandomStream& Rng,
		TArray<FCitixPlanBlock>* OutLocalStreetBlocks = nullptr);
};
