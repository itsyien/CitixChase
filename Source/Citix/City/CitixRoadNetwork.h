// Copyright Epic Games, Inc. All Rights Reserved.
// The routable road graph. Built from the master plan (see CitixCityMap.h) rather than
// from a grid: the plan supplies polylines with classes, and this welds them into a
// planar graph that traffic and pedestrians can route over.
//
// Pure data: no scene objects, so it can be generated, tested and traversed without
// touching the renderer.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"
#include "CitixRoadNetwork.generated.h"

class UCitixCitySettings;
struct FCitixCityPlan;

USTRUCT(BlueprintType)
struct FCitixRoadNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	FVector2D Position = FVector2D::ZeroVector;

	/** Road surface height; the original city retains its zero-height graph. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	float Elevation = 0.f;
};

USTRUCT(BlueprintType)
struct FCitixRoadEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 NodeA = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 NodeB = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	ECitixRoadClass RoadClass = ECitixRoadClass::Local;

	/** Total corridor width including sidewalks, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	float CorridorWidth = 1000.f;

	/** Whether traffic can drive along this edge. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bDrivable = true;

	/**
	 * How far to pull junction furniture (sidewalks, curbs, markings) back from each
	 * end, cm. Set at junctions only, so sidewalks run continuously through bends.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	float TrimA = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	float TrimB = 0.f;

	/** Index into the plan's district list, for palettes, props and signage. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	int32 DistrictIndex = INDEX_NONE;

	/** True when this street crosses the river on a bridge. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	bool bBridge = false;
	/** Authored driving surface width; zero uses the existing city class defaults. */
	UPROPERTY(BlueprintReadOnly, Category = "Road")
	float SurfaceWidth = 0.f;
};

/**
 * The road graph. The adjacency list is intentionally not reflected: it is derived data.
 */
USTRUCT(BlueprintType)
struct FCitixRoadNetwork
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FCitixRoadNode> Nodes;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	TArray<FCitixRoadEdge> Edges;

	/** Edges touching each node. Derived from Edges. */
	TArray<TArray<int32>> NodeEdgeIndices;

	UPROPERTY(BlueprintReadOnly, Category = "Road")
	float CitySize = 0.f;

	void Reset();

	/** Convert the macro plan into a routable graph. */
	void BuildFromPlan(const FCitixCityPlan& Plan, const UCitixCitySettings& Settings);

	bool IsValidNode(int32 Index) const { return Nodes.IsValidIndex(Index); }
	bool IsValidEdge(int32 Index) const { return Edges.IsValidIndex(Index); }

	float EdgeLength(int32 EdgeIndex) const;
	FVector2D EdgeDirection(int32 EdgeIndex) const;
	FVector EdgePoint3D(int32 EdgeIndex, float Fraction) const;
	FVector EdgeTangent3D(int32 EdgeIndex) const;
	FTransform EdgeSurfacePose(int32 EdgeIndex, const FVector2D& Point, float Yaw) const;
	FCitixRoadSpec GetTrafficRoadSpec(int32 EdgeIndex) const;
	uint32 GetLayoutHash(int32 MapRevision = 0) const;
	/** Nearest centreline projection in XY; height follows that edge's grade. */
	bool FindSurfaceHeight(const FVector2D& Point, float& Height, float& Distance) const;

	/** Number of roads meeting at a node (2 = a bend, 3+ = a junction). */
	int32 GetNodeDegree(int32 NodeIndex) const;

	/** Rebuilds NodeEdgeIndices from Edges. */
	void RebuildAdjacency();

protected:
	/** Junction insets, so sidewalks and markings stop short of an intersection. */
	void ComputeTrims();
};
