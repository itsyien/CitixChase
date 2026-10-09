// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixRoadNetwork.h"
#include "City/CitixCityPlan.h"
#include "Core/CitixCitySettings.h"
#include "Citix.h"

namespace
{
	/** Components smaller than this many nodes are closed to traffic (isolated stubs). */
	constexpr int32 MinDrivableComponentNodes = 6;
}

void FCitixRoadNetwork::Reset()
{
	Nodes.Reset();
	Edges.Reset();
	NodeEdgeIndices.Reset();
	CitySize = 0.f;
}

void FCitixRoadNetwork::BuildFromPlan(const FCitixCityPlan& Plan, const UCitixCitySettings& Settings)
{
	Reset();
	CitySize = Plan.CitySize;

	// The plan's lattice already has clean topology: every road is a chain of shared
	// nodes, so two roads only ever meet at a node. That means no intersection splitting
	// and no T-junction snapping is needed - the graph is a direct conversion.
	Nodes.SetNum(Plan.Nodes.Num());
	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		Nodes[Index].Position = Plan.Nodes[Index];
	}

	auto ClassForLevel = [](ECitixRoadLevel Level) -> ECitixRoadClass
	{
		switch (Level)
		{
		case ECitixRoadLevel::Arterial:  return ECitixRoadClass::Boulevard;
		case ECitixRoadLevel::Secondary: return ECitixRoadClass::Collector;
		default:                         return ECitixRoadClass::Local;
		}
	};

	// One edge per node pair: where two roads share a segment, the more important wins.
	TMap<uint64, int32> EdgeLookup;
	int32 Duplicates = 0;

	for (const FCitixPlanRoad& Road : Plan.Roads)
	{
		const ECitixRoadClass Class = ClassForLevel(Road.Level);
		for (int32 Index = 0; Index + 1 < Road.Nodes.Num(); ++Index)
		{
			const int32 A = Road.Nodes[Index];
			const int32 B = Road.Nodes[Index + 1];
			if (A == B || !Nodes.IsValidIndex(A) || !Nodes.IsValidIndex(B))
			{
				continue;
			}

			const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
			if (int32* Existing = EdgeLookup.Find(Key))
			{
				FCitixRoadEdge& Edge = Edges[*Existing];
				if (static_cast<int32>(Class) > static_cast<int32>(Edge.RoadClass))
				{
					Edge.RoadClass = Class;
					Edge.CorridorWidth = Settings.GetRoadSpec(Class).CorridorWidth();
				}
				Edge.bBridge |= Road.bBridge;
				if (Edge.DistrictIndex == INDEX_NONE)
				{
					Edge.DistrictIndex = Road.DistrictIndex;
				}
				++Duplicates;
				continue;
			}

			FCitixRoadEdge Edge;
			Edge.NodeA = A;
			Edge.NodeB = B;
			Edge.RoadClass = Class;
			Edge.CorridorWidth = Settings.GetRoadSpec(Class).CorridorWidth();
			Edge.DistrictIndex = Road.DistrictIndex;
			Edge.bBridge = Road.bBridge;
			Edge.bDrivable = true;
			EdgeLookup.Add(Key, Edges.Add(Edge));
		}
	}

	RebuildAdjacency();
	ComputeTrims();

	// ---- Connectivity ---------------------------------------------------
	// The two banks only connect where bridges are, which is correct, so this checks the
	// graph rather than assuming it is one piece.
	TArray<int32> Component;
	Component.Init(-1, Nodes.Num());
	TArray<int32> Stack;
	int32 Largest = 0;
	int32 ComponentCount = 0;
	for (int32 Seed = 0; Seed < Nodes.Num(); ++Seed)
	{
		if (Component[Seed] != -1)
		{
			continue;
		}
		++ComponentCount;
		int32 Size = 0;
		Stack.Reset();
		Stack.Add(Seed);
		Component[Seed] = ComponentCount;
		while (Stack.Num() > 0)
		{
			const int32 Node = Stack.Pop();
			++Size;
			for (int32 EdgeIndex : NodeEdgeIndices[Node])
			{
				const FCitixRoadEdge& Edge = Edges[EdgeIndex];
				const int32 Other = (Edge.NodeA == Node) ? Edge.NodeB : Edge.NodeA;
				if (Nodes.IsValidIndex(Other) && Component[Other] == -1)
				{
					Component[Other] = ComponentCount;
					Stack.Add(Other);
				}
			}
		}
		Largest = FMath::Max(Largest, Size);
	}

	TMap<int32, int32> ComponentSizes;
	for (int32 Value : Component)
	{
		if (Value > 0)
		{
			ComponentSizes.FindOrAdd(Value)++;
		}
	}
	int32 ClosedEdges = 0;
	for (FCitixRoadEdge& Edge : Edges)
	{
		const int32 ComponentId = Component.IsValidIndex(Edge.NodeA) ? Component[Edge.NodeA] : -1;
		const int32* Size = ComponentSizes.Find(ComponentId);
		if (Size && *Size < MinDrivableComponentNodes)
		{
			Edge.bDrivable = false;
			++ClosedEdges;
		}
	}

	UE_LOG(LogCitix, Log,
		TEXT("[Citix] Road graph: %d nodes, %d edges, %d duplicate plan segments; ")
		TEXT("largest component %d of %d nodes (%d components, %d stubs closed)."),
		Nodes.Num(), Edges.Num(), Duplicates, Largest, Nodes.Num(), ComponentCount, ClosedEdges);
}

int32 FCitixRoadNetwork::GetNodeDegree(int32 NodeIndex) const
{
	return NodeEdgeIndices.IsValidIndex(NodeIndex) ? NodeEdgeIndices[NodeIndex].Num() : 0;
}

void FCitixRoadNetwork::ComputeTrims()
{
	for (FCitixRoadEdge& Edge : Edges)
	{
		const int32 Ends[2] = { Edge.NodeA, Edge.NodeB };
		for (int32 Which = 0; Which < 2; ++Which)
		{
			const int32 Node = Ends[Which];
			if (!NodeEdgeIndices.IsValidIndex(Node) || NodeEdgeIndices[Node].Num() < 3)
			{
				// A bend in a road, not a junction: sidewalks run straight through it.
				continue;
			}

			float HalfCorridor = 0.f;
			for (int32 OtherIndex : NodeEdgeIndices[Node])
			{
				if (Edges.IsValidIndex(OtherIndex))
				{
					HalfCorridor = FMath::Max(HalfCorridor, Edges[OtherIndex].CorridorWidth * 0.5f);
				}
			}
			(Which == 0 ? Edge.TrimA : Edge.TrimB) = HalfCorridor;
		}
	}
}

void FCitixRoadNetwork::RebuildAdjacency()
{
	NodeEdgeIndices.Reset();
	NodeEdgeIndices.SetNum(Nodes.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		const FCitixRoadEdge& Edge = Edges[EdgeIndex];
		if (NodeEdgeIndices.IsValidIndex(Edge.NodeA))
		{
			NodeEdgeIndices[Edge.NodeA].Add(EdgeIndex);
		}
		if (NodeEdgeIndices.IsValidIndex(Edge.NodeB))
		{
			NodeEdgeIndices[Edge.NodeB].Add(EdgeIndex);
		}
	}
}

float FCitixRoadNetwork::EdgeLength(int32 EdgeIndex) const
{
	if (!IsValidEdge(EdgeIndex))
	{
		return 0.f;
	}
	const FCitixRoadEdge& Edge = Edges[EdgeIndex];
	return FVector2D::Distance(Nodes[Edge.NodeA].Position, Nodes[Edge.NodeB].Position);
}

FVector FCitixRoadNetwork::EdgePoint3D(int32 EdgeIndex, float Fraction) const
{
 if(!IsValidEdge(EdgeIndex)) return FVector::ZeroVector;
 const auto& Edge=Edges[EdgeIndex];
 if(!IsValidNode(Edge.NodeA) || !IsValidNode(Edge.NodeB)) return FVector::ZeroVector;
 const auto& A=Nodes[Edge.NodeA]; const auto& B=Nodes[Edge.NodeB];
 const float T=FMath::IsFinite(Fraction) ? FMath::Clamp(Fraction,0.f,1.f) : 0.f;
 return FMath::Lerp(FVector(A.Position,A.Elevation),FVector(B.Position,B.Elevation),T);
}

FVector FCitixRoadNetwork::EdgeTangent3D(int32 EdgeIndex) const
{
 const FVector Direction=(EdgePoint3D(EdgeIndex,1.f)-EdgePoint3D(EdgeIndex,0.f)).GetSafeNormal();
 return Direction.IsNearlyZero() ? FVector::ForwardVector : Direction;
}

FVector2D FCitixRoadNetwork::EdgeDirection(int32 EdgeIndex) const
{
	if (!IsValidEdge(EdgeIndex))
	{
		return FVector2D(1.f, 0.f);
	}
	const FCitixRoadEdge& Edge = Edges[EdgeIndex];
	return (Nodes[Edge.NodeB].Position - Nodes[Edge.NodeA].Position).GetSafeNormal();
}

bool FCitixRoadNetwork::FindSurfaceHeight(const FVector2D& Point, float& Height, float& Distance) const
{
 Height=0.f; Distance=MAX_flt;
 if(Point.ContainsNaN()) return false;
 bool Found=false;
 for(int32 Index=0;Index<Edges.Num();++Index)
 {
  const auto& Edge=Edges[Index];
  if(!Edge.bDrivable || !IsValidNode(Edge.NodeA) || !IsValidNode(Edge.NodeB)) continue;
  const FVector2D A=Nodes[Edge.NodeA].Position,AB=Nodes[Edge.NodeB].Position-A;
  const double LengthSquared=AB.SizeSquared();
  if(LengthSquared<=SMALL_NUMBER) continue;
  const float T=FMath::Clamp(FVector2D::DotProduct(Point-A,AB)/LengthSquared,0.,1.);
  const float Candidate=FVector2D::Distance(Point,A+T*AB);
  if(Candidate>=Distance) continue;
  Found=true; Distance=Candidate; Height=EdgePoint3D(Index,T).Z;
 }
 return Found;
}

FTransform FCitixRoadNetwork::EdgeSurfacePose(int32 EdgeIndex,const FVector2D& Point,float Yaw) const
{
 if(!IsValidEdge(EdgeIndex) || Point.ContainsNaN() || !FMath::IsFinite(Yaw)) return FTransform::Identity;
 const auto& Edge=Edges[EdgeIndex];
 if(!IsValidNode(Edge.NodeA) || !IsValidNode(Edge.NodeB)) return FTransform::Identity;
 const FVector2D A=Nodes[Edge.NodeA].Position,Delta=Nodes[Edge.NodeB].Position-A;
 const double LengthSquared=Delta.SizeSquared();
 if(LengthSquared<=SMALL_NUMBER) return FTransform(FRotator(0,Yaw,0),FVector(Point,Nodes[Edge.NodeA].Elevation));
 const float T=FVector2D::DotProduct(Point-A,Delta)/LengthSquared;
 const float Height=FMath::Lerp(Nodes[Edge.NodeA].Elevation,Nodes[Edge.NodeB].Elevation,T);
 const FVector Heading=FRotator(0,Yaw,0).Vector();
 const float Grade=(Nodes[Edge.NodeB].Elevation-Nodes[Edge.NodeA].Elevation)*FVector2D::DotProduct(FVector2D(Heading),Delta)/LengthSquared;
 return FTransform(FRotator(FMath::RadiansToDegrees(FMath::Atan(Grade)),Yaw,0),FVector(Point,Height));
}

FCitixRoadSpec FCitixRoadNetwork::GetTrafficRoadSpec(int32 EdgeIndex) const
{
 if(!IsValidEdge(EdgeIndex)) return FCitixRoadSpec();
 const auto& Edge=Edges[EdgeIndex];
 auto Spec=UCitixCitySettings::Get().GetRoadSpec(Edge.RoadClass);
 if(Edge.SurfaceWidth>0.f)
 {
  Spec.NumLanes=2; Spec.LaneWidth=FMath::Max(250.f,(Edge.SurfaceWidth-500.f)*.5f);
  Spec.MedianWidth=0; Spec.SidewalkWidth=0;
 }
 return Spec;
}

uint32 FCitixRoadNetwork::GetLayoutHash(int32 MapRevision) const
{
 uint32 Hash=HashCombine(GetTypeHash(CitySize),HashCombine(GetTypeHash(Nodes.Num()),GetTypeHash(Edges.Num())));
 auto Value=[&](auto V){Hash=HashCombine(Hash,GetTypeHash(V));};
 // Centimetre precision keeps identity stable across harmless float roundoff.
 for(const auto& Node:Nodes)
 {
  Value(FMath::RoundToInt(Node.Position.X)); Value(FMath::RoundToInt(Node.Position.Y)); Value(FMath::RoundToInt(Node.Elevation));
 }
 for(const auto& Edge:Edges)
 {
  Value(Edge.NodeA); Value(Edge.NodeB); Value(uint8(Edge.RoadClass));
  Value(FMath::RoundToInt(Edge.CorridorWidth)); Value(FMath::RoundToInt(Edge.SurfaceWidth));
  Value(Edge.bDrivable); Value(Edge.bBridge);
 }
 return HashCombine(Hash,GetTypeHash(MapRevision));
}
