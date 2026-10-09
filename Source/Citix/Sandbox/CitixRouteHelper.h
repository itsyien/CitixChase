// Copyright Epic Games, Inc. All Rights Reserved.
// Shared road-graph routing: Dijkstra over the plan graph, usable from the
// server director and from client controllers (which rebuild their own routes
// locally from the replicated waypoint). The graph is tiny (~100-200 nodes),
// so a linear scan beats a heap and either side can run it at a few Hz.

#pragma once

#include "CoreMinimal.h"
#include "City/CitixRoadNetwork.h"
#include "Algo/Reverse.h"

class CITIX_API FCitixRouteHelper
{
public:
	/** Node path from a world point to a world point. False when unroutable. */
	static bool FindRouteNodes(const FCitixRoadNetwork& Network, const FVector& From,
		const FVector& To, TArray<int32>& OutPath)
	{
		OutPath.Reset();
		if (Network.Nodes.Num() == 0 || Network.NodeEdgeIndices.Num() == 0)
		{
			return false;
		}

		auto NearestNode = [&Network](const FVector& Location)
		{
			int32 Best = INDEX_NONE;
			float BestDistanceSq = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Network.Nodes.Num(); ++Index)
			{
				const FVector2D& P = Network.Nodes[Index].Position;
				const float DistanceSq = FVector::DistSquared(FVector(P,Network.Nodes[Index].Elevation),Location);
				if (DistanceSq < BestDistanceSq)
				{
					BestDistanceSq = DistanceSq;
					Best = Index;
				}
			}
			return Best;
		};

		const int32 StartNode = NearestNode(From);
		const int32 GoalNode = NearestNode(To);
		if (StartNode == INDEX_NONE || GoalNode == INDEX_NONE)
		{
			return false;
		}
		if (StartNode == GoalNode)
		{
			OutPath.Add(StartNode);
			return true;
		}

		const int32 NodeCount = Network.Nodes.Num();
		TArray<float> Cost;
		TArray<int32> Previous;
		Cost.Init(TNumericLimits<float>::Max(), NodeCount);
		Previous.Init(INDEX_NONE, NodeCount);
		TArray<bool> Visited;
		Visited.Init(false, NodeCount);
		Cost[StartNode] = 0.f;

		for (int32 Iteration = 0; Iteration < NodeCount; ++Iteration)
		{
			int32 Current = INDEX_NONE;
			float BestCost = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < NodeCount; ++Index)
			{
				if (!Visited[Index] && Cost[Index] < BestCost)
				{
					BestCost = Cost[Index];
					Current = Index;
				}
			}
			if (Current == INDEX_NONE || Current == GoalNode)
			{
				break;
			}
			Visited[Current] = true;

			for (int32 EdgeIndex : Network.NodeEdgeIndices[Current])
			{
				if (!Network.Edges.IsValidIndex(EdgeIndex) || !Network.Edges[EdgeIndex].bDrivable)
				{
					continue;
				}
				const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
				const int32 Other = (Edge.NodeA == Current) ? Edge.NodeB : Edge.NodeA;
				const float Step = FVector::Distance(Network.EdgePoint3D(EdgeIndex,0),Network.EdgePoint3D(EdgeIndex,1));
				if (Cost[Current] + Step < Cost[Other])
				{
					Cost[Other] = Cost[Current] + Step;
					Previous[Other] = Current;
				}
			}
		}

		if (Previous[GoalNode] == INDEX_NONE)
		{
			return false;
		}

		for (int32 Node = GoalNode; Node != INDEX_NONE; Node = Previous[Node])
		{
			OutPath.Add(Node);
			if (Node == StartNode)
			{
				break;
			}
		}
		Algo::Reverse(OutPath);
		return true;
	}

	/** Project onto drivable edges first, then choose the shorter endpoint route. */
 static void BuildRoutePoints(const FCitixRoadNetwork& Network, const FVector& From, const FVector& To, TArray<FVector>& OutPoints)
 {
  OutPoints.Reset();
  if(From.ContainsNaN() || To.ContainsNaN()) return;
  auto NodePoint=[&](int32 Node){return FVector(Network.Nodes[Node].Position,Network.Nodes[Node].Elevation+60.f);};
  auto Project=[&](const FVector& P,FVector& OnRoad) {
   int Best=INDEX_NONE; float Cost=FLT_MAX;
   for (int I=0; I<Network.Edges.Num(); ++I) {
    const FCitixRoadEdge& E=Network.Edges[I]; if (!E.bDrivable || !Network.IsValidNode(E.NodeA) || !Network.IsValidNode(E.NodeB)) continue;
    const FVector2D A=Network.Nodes[E.NodeA].Position,AB=Network.Nodes[E.NodeB].Position-A;
    const float T=FMath::Clamp(FVector2D::DotProduct(FVector2D(P)-A,AB)/FMath::Max(1.,AB.SizeSquared()),0.,1.);
    const FVector Q=Network.EdgePoint3D(I,T)+FVector(0,0,60); const float D=FVector::DistSquared(P,Q);
    if (D<Cost) { Cost=D; Best=I; OnRoad=Q; }
   } return Best;
  };
  FVector Start,End; const int A=Project(From,Start), B=Project(To,End);
  if (A==INDEX_NONE || B==INDEX_NONE) return;
  if (A==B) { OutPoints={Start,End,To}; return; }
  const FCitixRoadEdge& First=Network.Edges[A]; const FCitixRoadEdge& Last=Network.Edges[B];
  float Best=FLT_MAX; TArray<int32> BestPath;
  for (int Begin : {First.NodeA,First.NodeB}) for (int Finish : {Last.NodeA,Last.NodeB}) {
   TArray<int32> Path;
   if (!FindRouteNodes(Network,NodePoint(Begin),NodePoint(Finish),Path)) continue;
   float Length=FVector::Distance(Start,NodePoint(Begin))+FVector::Distance(End,NodePoint(Finish));
   for (int I=1; I<Path.Num(); ++I) Length+=FVector::Distance(NodePoint(Path[I-1]),NodePoint(Path[I]));
   if (Length<Best) { Best=Length; BestPath=MoveTemp(Path); }
  }
  if (BestPath.IsEmpty()) return;
  OutPoints.Add(Start); for (int Node:BestPath) OutPoints.Add(NodePoint(Node));
  OutPoints.Add(End); OutPoints.Add(To);
 }
};
