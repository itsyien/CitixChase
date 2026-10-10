#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "City/CitixHillsideLayout.h"
#include "City/CitixRoadNetwork.h"
#include "Core/CitixCitySettings.h"
#include "Sandbox/CitixRouteHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixElevatedRoadTest,"CitixChase.Hillside.ElevatedRoadSampling",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixElevatedRoadTest::RunTest(const FString& Parameters)
{
 FCitixRoadNetwork Roads;
 FCitixRoadNode A; A.Position=FVector2D(100,200); A.Elevation=300;
 FCitixRoadNode B; B.Position=FVector2D(1100,200); B.Elevation=400;
 Roads.Nodes={A,B}; FCitixRoadEdge Edge; Edge.NodeA=0; Edge.NodeB=1; Roads.Edges.Add(Edge);
 TestTrue(TEXT("Midpoint follows grade"),Roads.EdgePoint3D(0,.5f).Equals(FVector(600,200,350)));
 TestTrue(TEXT("Sampling clamps before start"),Roads.EdgePoint3D(0,-1).Equals(FVector(100,200,300)));
 TestTrue(TEXT("Sampling clamps beyond end"),Roads.EdgePoint3D(0,2).Equals(FVector(1100,200,400)));
 TestTrue(TEXT("Vehicle heading includes grade"),Roads.EdgeTangent3D(0).Equals(FVector(1000,0,100).GetSafeNormal()));
 TestTrue(TEXT("Invalid edge cannot dereference nodes"),Roads.EdgePoint3D(INDEX_NONE,.5f).IsZero());
 TestEqual(TEXT("Original city nodes keep zero height"),FCitixRoadNode().Elevation,0.f);
 float Height,Distance;
 TestTrue(TEXT("Graded surface query finds road"),Roads.FindSurfaceHeight(FVector2D(600,250),Height,Distance));
 TestEqual(TEXT("Surface query keeps interpolated height"),Height,350.f);
 TestEqual(TEXT("Surface query reports distance from centreline"),Distance,50.f);
 TArray<FVector> Guidance;
 FCitixRouteHelper::BuildRoutePoints(Roads,FVector(200,200,380),FVector(1000,200,470),Guidance);
 TestTrue(TEXT("Guidance starts above the graded driving surface"),Guidance.Num()>=2 && FMath::IsNearlyEqual(Guidance[0].Z,370.,.01));
 TestTrue(TEXT("Guidance endpoint follows the grade rather than ground zero"),Guidance.Num()>=2 && FMath::IsNearlyEqual(Guidance[1].Z,450.,.01));
 FCitixRoadNetwork Stacked;
 Stacked.Nodes={};
 for(float Z:{0.f,1000.f}) for(float X:{0.f,1000.f}) {FCitixRoadNode Node; Node.Position=FVector2D(X,0); Node.Elevation=Z; Stacked.Nodes.Add(Node);}
 FCitixRoadEdge Lower; Lower.NodeA=0; Lower.NodeB=1; FCitixRoadEdge Upper; Upper.NodeA=2; Upper.NodeB=3; Stacked.Edges={Lower,Upper}; Stacked.RebuildAdjacency();
 FCitixRouteHelper::BuildRoutePoints(Stacked,FVector(200,0,1080),FVector(800,0,1080),Guidance);
 TestTrue(TEXT("Road projection selects the correct elevation at overlapping XY"),Guidance.Num()>=2 && FMath::IsNearlyEqual(Guidance[0].Z,1060.,.01));
 const FTransform Uphill=Roads.EdgeSurfacePose(0,FVector2D(600,350),0);
 TestTrue(TEXT("Lane offset does not change road height"),Uphill.GetLocation().Equals(FVector(600,350,350)));
 TestTrue(TEXT("Traffic body tilts up the grade"),FMath::IsNearlyEqual(Uphill.Rotator().Pitch,FMath::RadiansToDegrees(FMath::Atan(.1f)),.001f));
 TestTrue(TEXT("Reverse traffic tilts down the grade"),Roads.EdgeSurfacePose(0,FVector2D(600,50),180).Rotator().Pitch< -5.f);
 TestTrue(TEXT("Parking across the grade has no longitudinal pitch"),FMath::IsNearlyZero(Roads.EdgeSurfacePose(0,FVector2D(600,350),90).Rotator().Pitch,.001f));
 Roads.Nodes[0].Elevation=Roads.Nodes[1].Elevation=0;
 TestTrue(TEXT("Existing flat-city traffic keeps its original pose"),Roads.EdgeSurfacePose(0,FVector2D(600,350),40).Equals(FTransform(FRotator(0,40,0),FVector(600,350,0))));
 const uint32 Identity=Roads.GetLayoutHash();
 auto Changed=Roads; Changed.Nodes[1].Elevation=100;
 TestNotEqual(TEXT("Same-count maps with different heights cannot share identity"),Changed.GetLayoutHash(),Identity);
 Changed=Roads; Changed.Nodes[1].Position.X+=100;
 TestNotEqual(TEXT("Same-count maps with different geometry cannot share identity"),Changed.GetLayoutHash(),Identity);
 Changed=Roads; Changed.Edges[0].bDrivable=false;
 TestNotEqual(TEXT("Road connectivity flags participate in identity"),Changed.GetLayoutHash(),Identity);
 Changed=Roads; Changed.Edges[0].SurfaceWidth=1300;
 TestNotEqual(TEXT("Traffic surface width participates in identity"),Changed.GetLayoutHash(),Identity);
 TestEqual(TEXT("Copy retains deterministic identity"),FCitixRoadNetwork(Roads).GetLayoutHash(),Identity);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixHillsideLayoutTest,"CitixChase.Hillside.AuthoredRoutes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixHillsideLayoutTest::RunTest(const FString& Parameters)
{
 const auto Layout=FCitixHillsideLayout::Build();
 const auto Network=Layout.BuildRoadNetwork();
 for(int32 I=0;I<Network.Edges.Num();++I)
 {
  const auto& Edge=Network.Edges[I]; const auto Spec=Network.GetTrafficRoadSpec(I);
  TestEqual(TEXT("Hillside traffic has one lane per direction"),Spec.NumLanes,2);
  const float LaneCentre=Spec.LaneWidth*.5f,ParkedCentre=Edge.SurfaceWidth*.5f-130.f;
  TestTrue(TEXT("Moving and parked car volumes fit without overlap"),ParkedCentre-LaneCentre>=220.f && ParkedCentre+110.f<Edge.SurfaceWidth*.5f);
 }
 FCitixRoadNetwork CityRoads; FCitixRoadEdge CityEdge; CityEdge.RoadClass=ECitixRoadClass::Arterial; CityRoads.Edges.Add(CityEdge);
 TestEqual(TEXT("Existing city retains its arterial lane count"),CityRoads.GetTrafficRoadSpec(0).NumLanes,UCitixCitySettings::Get().GetRoadSpec(ECitixRoadClass::Arterial).NumLanes);
 TestTrue(TEXT("Layout produces a routable elevated graph"),Network.Nodes.Num()>20 && Network.Edges.Num()>20);
 for (const TCHAR* Name:{TEXT("Coastal Bypass"),TEXT("Terrace Loop"),TEXT("Switchback Climb"),TEXT("Ridge Connector"),TEXT("Tunnel Bypass"),TEXT("Marina Access"),TEXT("Summit Service Road")})
  TestTrue(FString::Printf(TEXT("Playable named route: %s"),Name),Layout.Routes.ContainsByPredicate([Name](const auto& Route){return Route.Name==Name && Route.Points.Num()>=2;}));
 TestTrue(TEXT("Authored relay pool offers geographic choice"),Layout.RelaySites.Num()>=12);
 TestEqual(TEXT("Two separated escapes"),Layout.Exits.Num(),2);
 TestEqual(TEXT("Two role-swappable starts"),Layout.Spawns.Num(),2);
 TestTrue(TEXT("Recovery parking is distributed"),Layout.RecoveryParking.Num()>=4);
 float MinimumZ=MAX_flt,MaximumZ=-MAX_flt;
 for(const auto& Route:Layout.Routes)
 {
  TestTrue(TEXT("Road supports two-way driving"),Route.Width>=1300.f);
  for(int32 I=0;I<Route.Points.Num();++I)
  {
   const FVector P=Route.Points[I];
   TestFalse(TEXT("Finite authored geometry"),P.ContainsNaN());
   MinimumZ=FMath::Min(MinimumZ,float(P.Z)); MaximumZ=FMath::Max(MaximumZ,float(P.Z));
   if(I==0) continue;
   const FVector Previous=Route.Points[I-1];
   const float Run=FVector::Dist2D(P,Previous);
   TestTrue(TEXT("No degenerate road spans"),Run>1.f);
   TestTrue(FString::Printf(TEXT("Road grade <=12%%: %s span %d grade %.3f from %s to %s"),*Route.Name,I,float(FMath::Abs(P.Z-Previous.Z)/Run),*Previous.ToCompactString(),*P.ToCompactString()),FMath::Abs(P.Z-Previous.Z)<=Run*.12f+.01f);
  }
 }
 TestTrue(TEXT("Genuine hillside elevation reaches sixty metres"),MaximumZ-MinimumZ>=5900.f);
 int32 CurvedSamples=0;
 for(const auto& Route:Layout.Routes) if(!Route.bTunnel) for(int32 I=1;I<Route.Points.Num()-1;++I)
 {
  const FVector P=Route.Points[I];
  const bool Junction=Layout.Routes.ContainsByPredicate([&](const auto& Other){return Other.Name!=Route.Name && Other.Points.ContainsByPredicate([&](const FVector& N){return N.Equals(P,.01f);});});
  if(Junction) continue;
  const FVector2D A(Route.Points[I-1]),B(P),C(Route.Points[I+1]);
  const FVector2D AB=B-A,BC=C-B;
  const double Cross=FMath::Abs(AB.X*BC.Y-AB.Y*BC.X);
  if(Cross<.01) continue;
  const double Radius=AB.Size()*BC.Size()*(C-A).Size()/(2*Cross);
  TestTrue(FString::Printf(TEXT("Ordinary bend radius at least 24 metres: %s point %d"),*Route.Name,I),Radius>=2390.);
  ++CurvedSamples;
 }
 TestTrue(TEXT("Map contains sampled curves rather than only sharp authored corners"),CurvedSamples>=50);
 TArray<FVector> Nodes;
 TArray<TArray<int32>> Links;
 auto Node=[&](const FVector& P)
 {
  const int32 Existing=Nodes.IndexOfByPredicate([&](const FVector& N){return N.Equals(P,.01f);});
  if(Existing!=INDEX_NONE) return Existing;
  const int32 Added=Nodes.Add(P); Links.AddDefaulted(); return Added;
 };
 for(const auto& Route:Layout.Routes) for(int32 I=1;I<Route.Points.Num();++I)
 {
  const int32 A=Node(Route.Points[I-1]),B=Node(Route.Points[I]);
  Links[A].AddUnique(B); Links[B].AddUnique(A);
 }
 if(!Nodes.IsEmpty())
 {
  TSet<int32> Visited; TArray<int32> Pending={0};
  for(int32 I=0;I<Pending.Num();++I)
  {
   const int32 Current=Pending[I];
   if(Visited.Contains(Current)) continue;
   Visited.Add(Current);
   for(int32 Next:Links[Current]) if(!Visited.Contains(Next)) Pending.Add(Next);
  }
  TestEqual(TEXT("Every authored road belongs to one connected driving network"),Visited.Num(),Nodes.Num());
 }
 for(const FVector& Exit:Layout.Exits)
 {
  const int32 Index=Nodes.IndexOfByPredicate([&](const FVector& N){return N.Equals(Exit,.01f);});
  TestTrue(TEXT("Each escape gate has two actual road approaches"),Index!=INDEX_NONE && Links[Index].Num()>=2);
 }
 for(const FVector& Site:Layout.RelaySites)
  TestTrue(TEXT("Every relay candidate is on the authored road network"),Nodes.ContainsByPredicate([&](const FVector& N){return N.Equals(Site,.01f);}));
 if(Layout.Spawns.Num()==2) TestTrue(TEXT("Driver starts are separated by at least three hundred metres"),FVector::Dist2D(Layout.Spawns[0],Layout.Spawns[1])>=30000.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixCoastalCompositionTest,"CitixChase.Hillside.CoastalComposition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixCoastalCompositionTest::RunTest(const FString&)
{
 const auto Layout=FCitixHillsideLayout::Build();
 float Summit=0;
 for(const auto& Route:Layout.Routes) for(const auto& P:Route.Points) Summit=FMath::Max(Summit,float(P.Z));
 TestTrue(TEXT("Mountain road hierarchy reaches approximately ninety metres"),Summit>=8500.f);
 TestTrue(TEXT("Compact Mediterranean neighborhoods contain twenty-five to thirty-five houses"),Layout.Houses.Num()>=25 && Layout.Houses.Num()<=35);
 const auto Again=FCitixHillsideLayout::Build(Layout.Seed);
 TestEqual(TEXT("Seed reproduces environmental geometry"),Layout.BuildRoadNetwork().GetLayoutHash(),Again.BuildRoadNetwork().GetLayoutHash());
 auto OtherLighting=Layout; OtherLighting.Settings.InitialHour+=1.f;
 TestNotEqual(TEXT("Different daylight settings cannot pass multiplayer identity"),OtherLighting.BuildRoadNetwork().GetLayoutHash(),Layout.BuildRoadNetwork().GetLayoutHash());
 const auto* Climb=Layout.Routes.FindByPredicate([](const auto& Route){return Route.Name==TEXT("Switchback Climb");});
 TestTrue(TEXT("Five dominant hairpins have genuinely sampled climbing curves"),Climb && Climb->Points.Num()>=170);
 return true;
}
#endif
