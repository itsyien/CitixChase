#include "City/CitixHillsideLayout.h"

FCitixRoadNetwork FCitixHillsideLayout::BuildRoadNetwork() const
{
 FCitixRoadNetwork Network; Network.CitySize=75000.f;
 auto Node=[&Network](const FVector& Point)
 {
  const int32 Existing=Network.Nodes.IndexOfByPredicate([&](const FCitixRoadNode& N){return FVector(N.Position,N.Elevation).Equals(Point,.01f);});
  if(Existing!=INDEX_NONE) return Existing;
  FCitixRoadNode N; N.Position=FVector2D(Point); N.Elevation=Point.Z;
  return Network.Nodes.Add(N);
 };
 for(const auto& Route:Routes) for(int32 I=1;I<Route.Points.Num();++I)
 {
  FCitixRoadEdge Edge; Edge.NodeA=Node(Route.Points[I-1]); Edge.NodeB=Node(Route.Points[I]);
  Edge.CorridorWidth=Route.Width; Edge.SurfaceWidth=Route.Width; Edge.RoadClass=ECitixRoadClass::Local;
  if(Edge.NodeA!=Edge.NodeB) Network.Edges.Add(Edge);
 }
 Network.RebuildAdjacency();
 return Network;
}

FCitixHillsideLayout FCitixHillsideLayout::Build()
{
 FCitixHillsideLayout Layout;
 auto Road=[&Layout](const TCHAR* Name,std::initializer_list<FVector> Points,bool Tunnel=false)
 {
  FCitixHillsideRoute Route; Route.Name=Name; Route.bTunnel=Tunnel; Route.Width=Tunnel ? 1600.f : 1300.f;
  for(const FVector& Point:Points) Route.Points.Add(Point);
  Layout.Routes.Add(MoveTemp(Route));
 };
 Road(TEXT("Coastal Bypass"),{{-35000,-24000,100},{-18000,-22000,100},{-6000,-24000,100},{8000,-22000,100},{18000,-24000,100},{35000,-22000,100}});
 Road(TEXT("West Terrace Approach"),{{-18000,-22000,100},{-21000,-10000,1000},{-11000,0,2200}});
 Road(TEXT("East Terrace Approach"),{{18000,-24000,100},{26000,-10000,900},{9000,0,2200}});
 Road(TEXT("Terrace Loop"),{{-11000,0,2200},{-6000,0,2200},{9000,0,2200},{9000,10000,2200},{-11000,10000,2200},{-11000,0,2200}});
 Road(TEXT("Switchback Climb"),{{-11000,10000,2200},{-28000,10000,2800},{-30000,12000,2800},{-30000,16000,3000},{-28000,18000,3200},{-12000,18000,3800},{-10000,20000,3800},{-10000,24000,4100},{-12000,26000,4300},{-24000,26000,4800},{-26000,28000,4800},{-26000,32000,5000},{-24000,34000,5200},{-8000,34000,6000},{-8000,28000,6000}});
 Road(TEXT("Ridge Connector"),{{-8000,28000,6000},{10000,28000,5400},{24000,24000,4500},{28000,12000,3400},{9000,10000,2200}});
 Road(TEXT("Summit Loop"),{{-8000,34000,6000},{10000,34000,6000},{10000,28000,5400}});
 Road(TEXT("Lower Tunnel Approach"),{{-6000,-24000,100},{-6000,-10000,1100}});
 // Level out before the terrace's transverse carriageway, rather than climb
 // underneath its leading edge. The graded portion remains below twelve percent.
 Road(TEXT("Tunnel Bypass"),{{-6000,-10000,1100},{-6000,-800,2200},{-6000,0,2200}},true);
 // Each escape gate has a second approach through its harbour service loop.
 Road(TEXT("West Harbour Loop"),{{-35000,-24000,100},{-35000,-16000,100},{-21000,-10000,1000}});
 Road(TEXT("East Harbour Loop"),{{35000,-22000,100},{35000,-14000,100},{26000,-10000,900}});
 Road(TEXT("Marina Access"),{{-18000,-22000,100},{-27000,-24000,100}});
 Road(TEXT("Summit Service Road"),{{10000,34000,6000},{1000,32000,6000}});
 Layout.RelaySites={{-18000,-22000,100},{8000,-22000,100},{-21000,-10000,1000},{26000,-10000,900},{-11000,0,2200},{9000,0,2200},{9000,10000,2200},{-11000,10000,2200},{-28000,18000,3200},{-12000,26000,4300},{-8000,34000,6000},{10000,34000,6000},{24000,24000,4500},{-6000,-10000,1100}};
 Layout.Exits={{-35000,-24000,100},{35000,-22000,100}};
 // Straight approach sections give both cars a full footprint away from bend seams.
 Layout.Spawns={{-16000,-5000,1600},{17500,-5000,1550}};
 Layout.RecoveryParking={{-18000,-22000,100},{9000,0,2200},{-28000,18000,3200},{10000,34000,6000}};
 // Round ordinary bends; shared junctions retain identical coordinates in every
 // route. Authored objective sites follow their nearest new curve vertex.
 const auto OriginalRoutes=Layout.Routes;
 for(auto& Route:Layout.Routes)
 {
  if(Route.bTunnel) continue;
  const auto Original=Route.Points; TArray<FVector> Rounded;
  Rounded.Add(Original[0]);
  for(int32 I=1;I<Original.Num()-1;++I)
  {
   const FVector P=Original[I];
   const bool Shared=OriginalRoutes.ContainsByPredicate([&](const auto& Other){return Other.Name!=Route.Name && Other.Points.ContainsByPredicate([&](const FVector& Point){return Point.Equals(P,.01f);});});
   const FVector2D Before=FVector2D(P-Original[I-1]),After=FVector2D(Original[I+1]-P);
   const FVector2D In=Before.GetSafeNormal(),Out=After.GetSafeNormal();
   const float Angle=FMath::Acos(FMath::Clamp(FVector2D::DotProduct(In,Out),-1.,1.));
   if(Shared || Angle<.01f || Angle>PI-.01f) {Rounded.Add(P); continue;}
   const float Radius=2400.f,Setback=Radius*FMath::Tan(Angle*.5f);
   if(Setback>FMath::Min(Before.Size(),After.Size())*.45f) {Rounded.Add(P); continue;}
   const FVector Start=FMath::Lerp(P,Original[I-1],Setback/Before.Size());
   const FVector End=FMath::Lerp(P,Original[I+1],Setback/After.Size());
   const float Sign=(In.X*Out.Y-In.Y*Out.X)>=0 ? 1.f : -1.f;
   const FVector2D Centre=FVector2D(Start)+FVector2D(-In.Y,In.X)*(Sign*Radius);
   const FVector2D Radial=FVector2D(Start)-Centre;
   const int32 Steps=FMath::Max(2,FMath::CeilToInt(FMath::RadiansToDegrees(Angle)/5.f));
   for(int32 Step=0;Step<=Steps;++Step)
   {
    const float T=float(Step)/Steps;
    const FVector2D XY=Centre+Radial.GetRotated(FMath::RadiansToDegrees(Angle)*Sign*T);
    Rounded.Add(FVector(XY,FMath::Lerp(Start.Z,End.Z,T)));
   }
  }
  Rounded.Add(Original.Last()); Route.Points=MoveTemp(Rounded);
 }
 auto Reproject=[&](TArray<FVector>& Sites)
 {
  for(FVector& Site:Sites)
  {
   float Distance=MAX_flt; FVector Nearest=Site;
   for(const auto& Route:Layout.Routes) for(const FVector& Point:Route.Points)
   {
    const float D=FVector::DistSquared(Point,Site);
    if(D<Distance) {Distance=D; Nearest=Point;}
   }
   Site=Nearest;
  }
 };
 Reproject(Layout.RelaySites); Reproject(Layout.RecoveryParking);
 return Layout;
}
