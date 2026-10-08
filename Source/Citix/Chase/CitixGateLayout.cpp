#include "Chase/CitixGateLayout.h"
#include "Chase/CitixChaseRules.h"

TArray<int32> FCitixGateLayout::Select(const FCitixRoadNetwork& Roads,const TArray<FCitixGateCandidate>& Candidates)
{
 // Shortest connected-road distance prevents nearby arms of the same junction
 // receiving separate gates. Never use straight-line distance across buildings.
 TArray<TArray<TPair<int32,float>>> Adjacent; Adjacent.SetNum(Roads.Nodes.Num());
 for (const auto& Edge:Roads.Edges) if (Edge.bDrivable) {
  const float Cost=FVector2D::Distance(Roads.Nodes[Edge.NodeA].Position,Roads.Nodes[Edge.NodeB].Position);
  Adjacent[Edge.NodeA].Add({Edge.NodeB,Cost}); Adjacent[Edge.NodeB].Add({Edge.NodeA,Cost});
 }
 TArray<TArray<float>> Routes; Routes.SetNum(Roads.Nodes.Num());
 for (int32 Source=0; Source<Roads.Nodes.Num(); ++Source) {
  auto& Dist=Routes[Source]; Dist.Init(FLT_MAX,Roads.Nodes.Num()); Dist[Source]=0;
  TArray<bool> Visited; Visited.Init(false,Roads.Nodes.Num());
  for (int32 Step=0; Step<Roads.Nodes.Num(); ++Step) {
   int32 Node=INDEX_NONE; float Cost=FLT_MAX;
   for (int32 I=0; I<Dist.Num(); ++I) if (!Visited[I] && Dist[I]<Cost) { Node=I; Cost=Dist[I]; }
   if (Node==INDEX_NONE) break; Visited[Node]=true;
   for (const auto& Next:Adjacent[Node]) Dist[Next.Key]=FMath::Min(Dist[Next.Key],Cost+Next.Value);
  }
 }
 auto Length=[&](int32 Edge) { const auto& E=Roads.Edges[Edge]; return FVector2D::Distance(Roads.Nodes[E.NodeA].Position,Roads.Nodes[E.NodeB].Position); };
 TArray<TArray<int32>> Conflicts; Conflicts.SetNum(Candidates.Num());
 for (int32 A=0; A<Candidates.Num(); ++A) for (int32 B=A+1; B<Candidates.Num(); ++B) {
  const auto& P=Candidates[A]; const auto& Q=Candidates[B]; const auto& E=Roads.Edges[P.Edge]; const auto& F=Roads.Edges[Q.Edge];
  float Distance=P.Edge==Q.Edge ? FMath::Abs(P.Along-Q.Along) : FLT_MAX;
  Distance=FMath::Min(Distance,P.Along+Routes[E.NodeA][F.NodeA]+Q.Along);
  Distance=FMath::Min(Distance,P.Along+Routes[E.NodeA][F.NodeB]+Length(Q.Edge)-Q.Along);
  Distance=FMath::Min(Distance,Length(P.Edge)-P.Along+Routes[E.NodeB][F.NodeA]+Q.Along);
  Distance=FMath::Min(Distance,Length(P.Edge)-P.Along+Routes[E.NodeB][F.NodeB]+Length(Q.Edge)-Q.Along);
  // Two gates within 75 m of one junction necessarily conflict here.
  if (Distance<FCitixChaseRules::GateSpacing-1.f) { Conflicts[A].Add(B); Conflicts[B].Add(A); }
 }
 // Least-conflicting choices fill the map more evenly than farthest-point
 // placement, which strands gaps that are too small to fit another gate.
 TArray<bool> Available; Available.Init(true,Candidates.Num());
 TArray<int32> Degree; for (const auto& Neighbours:Conflicts) Degree.Add(Neighbours.Num());
 TArray<int32> Result;
 while (Result.Num()<100) {
  int32 Pick=INDEX_NONE, BestDegree=MAX_int32; float Centre=FLT_MAX;
  for (int32 I=0; I<Candidates.Num(); ++I) if (Available[I]) {
   const float D=Candidates[I].Surface.GetLocation().SizeSquared2D();
   if (Degree[I]<BestDegree || (Degree[I]==BestDegree && D<Centre)) { Pick=I; BestDegree=Degree[I]; Centre=D; }
  }
  if (Pick==INDEX_NONE) break; Result.Add(Pick);
  auto Remove=[&](int32 I) { if (!Available[I]) return; Available[I]=false; for (int32 N:Conflicts[I]) if (Available[N]) --Degree[N]; };
  Remove(Pick); for (int32 I:Conflicts[Pick]) Remove(I);
 }
 return Result;
}
