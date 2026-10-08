// Copyright Epic Games, Inc. All Rights Reserved.
// THE MACRO CITY PLAN.
//
// This is a *planning* stage, not a city generator: it produces the macro layout only
// (geography, districts, arterials, bridges, secondary roads, block detection) as plain
// data. Nothing here creates buildings, props or final geometry.
//
// Design principle: STRUCTURED IRREGULARITY.
// The city is laid out on a river-relative lattice, so the street network has orderly
// topology (every road meets at shared nodes, nothing crosses anything, intersections are
// intentional) while the geometry follows the geography. NON-GRID is not randomness:
//
//   * rows    run parallel to the river   -> riverside boulevards, gently curved
//   * columns run normal to the river     -> straight cross-town streets, and the only
//                                            places the river can be crossed
//   * node positions are then nudged by a low-frequency field and per-district jitter,
//     which is what makes the grid read as a real city rather than graph paper
//
// Generation order (mirrors the pipeline the final generator will follow):
//   Seed -> Macro geography -> District planning -> Major arterials -> Bridge planning
//        -> Secondary roads -> Block detection -> [parcels -> buildings -> landmarks ->
//        detail, not implemented yet]
//
// Everything is deterministic from a single seed.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"

class UCitixCitySettings;

/** A planned road: an ordered chain of plan nodes, with its level and role. */
struct FCitixPlanRoad
{
	/** Indices into FCitixCityPlan::Nodes. */
	TArray<int32> Nodes;
	ECitixRoadLevel Level = ECitixRoadLevel::Secondary;
	int32 DistrictIndex = INDEX_NONE;
	/** Crosses the river (the plan's only permitted river crossings). */
	bool bBridge = false;
	/** Runs along the river bank: a boulevard or promenade road. */
	bool bRiverside = false;
	/** Deliberately bow-shaped rather than following the lattice exactly. */
	bool bCurved = false;
	/** Cuts across the lattice diagonally (a diagonal avenue). */
	bool bDiagonal = false;
};

/** A district: a region of the river band. Boundaries follow the river and arterials. */
struct FCitixPlanDistrict
{
	FString Name;
	ECitixDistrict Type = ECitixDistrict::Downtown;
	ECitixPlanZone Zone = ECitixPlanZone::MediumDensity;

	/** Region in river space: along the river (normalized), which bank, distance outward. */
	float UMin = 0.f;
	float UMax = 1.f;
	int32 Side = -1;             // -1 or +1
	float TMin = 0.f;            // 0 at the river bank, 1 at the outer growth boundary
	float TMax = 1.f;

	/** Future block character. */
	float BlockPitchScale = 1.f;      // multiplies the base cell size
	float JitterScale = 1.f;          // 0 = regular grid, 1 = old-town irregularity
	/** Typical parcel size inside this quarter, cm. Drives how finely blocks split. */
	float LotPitch = 3600.f;
	/**
	 * This quarter's own street bearing, degrees from the river-aligned lattice. Real
	 * cities get their diagonal streets this way: a district laid out to its own bearing
	 * meets its neighbours at 65-115 degrees, which is why this - and not a diagonal
	 * avenue cutting across a grid - is how diagonal streets are expressed here. (A line
	 * crossing an orthogonal grid always creates an angle below 60 degrees.)
	 */
	float RotationDegrees = 0.f;
	bool bAllowLocalStreets = false;  // level 3 streets arrive with parcel generation

	FVector2D Centroid = FVector2D::ZeroVector;
	TArray<FVector2D> Boundary;
};

/** A park or reserved open space. */
struct FCitixPlanOpenSpace
{
	FString Name;
	TArray<FVector2D> Boundary;
	/** Waterfront promenade rather than an inland park. */
	bool bWaterfront = false;
};

/** A detected city block: one lattice cell, ready for parcel generation later. */
struct FCitixPlanBlock
{
	TArray<FVector2D> Corners;
	FVector2D Centre = FVector2D::ZeroVector;
	/** Lattice address, so later stages can address cells directly. */
	int32 Column = 0;
	int32 Row = 0;
	int32 Side = -1;
	int32 DistrictIndex = INDEX_NONE;
	ECitixPlanZone Zone = ECitixPlanZone::MediumDensity;
	/** Inside a park / reserved open space: no buildings will go here. */
	bool bOpenSpace = false;
	/** In the waterfront reserve band: kept clear for the promenade. */
	bool bWaterfrontReserve = false;
	/** Cell area, cm^2. */
	float Area = 0.f;
};

/** Validation results, so the layout can be checked rather than eyeballed. */
struct FCitixPlanReport
{
	int32 NodeCount = 0;
	int32 RoadCount = 0;
	int32 ArterialCount = 0;
	int32 SecondaryCount = 0;
	int32 LocalCount = 0;
	int32 BridgeCount = 0;
	int32 DiagonalCount = 0;
	int32 BlockCount = 0;

	/** Segment crossings that are NOT at a shared node (must be 0). */
	int32 IllegalCrossings = 0;
	/** Coincident duplicate segments (must be 0). */
	int32 DuplicateSegments = 0;
	/** Lattice cells whose winding inverted (must be 0). */
	int32 InvertedCells = 0;

	/** Intersection angles, degrees. */
	float MinIntersectionAngle = 0.f;
	float MedianIntersectionAngle = 0.f;
	/** Junctions whose smallest angle is below the preferred floor. */
	int32 AcuteJunctions = 0;

	/** How square the bridges are to the river: 90 = perfectly perpendicular. */
	float WorstBridgeAngle = 0.f;
	float MeanBridgeAngle = 0.f;

	/** Road length classification, percent of total length. */
	float StraightPercent = 0.f;
	float CurvedPercent = 0.f;
	float DiagonalPercent = 0.f;

	float TotalRoadLength = 0.f;
	float ArterialLength = 0.f;
	float MeanBlockArea = 0.f;
};

/** The macro plan. */
struct FCitixCityPlan
{
	int32 Seed = 0;
	float CitySize = 0.f;
	/** Extent of everything the plan places, for sizing the ground slab. */
	FVector2D BoundsMin = FVector2D::ZeroVector;
	FVector2D BoundsMax = FVector2D::ZeroVector;

	// ---- Geography ------------------------------------------------------
	/** River centreline, densely sampled. */
	TArray<FVector2D> RiverPoints;
	TArray<FVector2D> RiverTangents;
	TArray<float> RiverWidths;
	float RiverLength = 0.f;
	float MaxRiverWidth = 0.f;

	// ---- Lattice --------------------------------------------------------
	/** Columns run across the river; rows run along it. Per bank. */
	int32 ColumnCount = 0;
	int32 RowCount = 0;
	/** Nodes are indexed column * (RowCount * 2) + slot, slot < RowCount = one bank. */
	TArray<FVector2D> Nodes;

	// ---- Content --------------------------------------------------------
	TArray<FCitixPlanDistrict> Districts;
	TArray<FCitixPlanRoad> Roads;
	TArray<FCitixPlanOpenSpace> OpenSpaces;
	TArray<FCitixPlanBlock> Blocks;

	FCitixPlanReport Report;

	// ---- Queries --------------------------------------------------------

	int32 NodeIndex(int32 Column, int32 Slot) const { return Column * (RowCount * 2) + Slot; }
	int32 SlotForBank(int32 Side, int32 Row) const
	{
		// Row 0 is the river bank; rows increase outward. Bank 0 = negative side.
		return (Side < 0) ? Row : (RowCount + Row);
	}
	bool IsValidNode(int32 Index) const { return Nodes.IsValidIndex(Index); }

	/** River frame at a normalized position along it (0..1), extrapolating past the ends. */
	void GetRiverFrame(float U, FVector2D& OutPoint, FVector2D& OutTangent) const;

	/** River half width at a normalized position. */
	float GetHalfWidth(float U) const;

	int32 GetDistrictAt(const FVector2D& WorldPoint) const;
	ECitixDistrict GetDistrictTypeAt(const FVector2D& WorldPoint) const;
};

/**
 * Builds the macro plan, stage by stage. Each stage is a separate function so the
 * hierarchy is explicit and the pipeline can be extended without touching the others.
 */
class CITIX_API FCitixPlanGenerator
{
public:
	static FCitixCityPlan Generate(const UCitixCitySettings& Settings, FRandomStream& Rng);

	/** Re-run the checks and fill Report. Safe to call on a finished plan. */
	static void Validate(FCitixCityPlan& Plan);

private:
	/**
	 * One planning attempt. RotationScale scales every district's street bearing, and is
	 * reduced and retried if an attempt produces folded cells or accidental crossings.
	 */
	static FCitixCityPlan GenerateAttempt(const UCitixCitySettings& Settings, FRandomStream& Rng,
		float RotationScale);
};
