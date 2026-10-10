#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "City/CitixHillsideBuilder.h"
#include "City/CitixHillsideLayout.h"
#include "City/CitixCityGenerator.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Sandbox/CitixRouteHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixHillsideSurfaceTest,"CitixChase.Hillside.PhysicalRoadSurfaces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixHillsideSurfaceTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Builder=World->SpawnActor<ACitixHillsideBuilder>();
 const auto Layout=FCitixHillsideLayout::Build(); Builder->Build(Layout);
 TestEqual(TEXT("Three authored orientation landmarks are built"),Builder->GetLandmarkLocations().Num(),3);
 TestTrue(TEXT("Night route has actual lamp emitter positions"),Builder->GetLampLocations().Num()>=10);
 for(const FVector& Landmark:Builder->GetLandmarkLocations())
 {
  FHitResult Model;
  const bool Found=World->LineTraceSingleByChannel(Model,Landmark+FVector(0,0,5000),Landmark+FVector(0,0,100),ECC_Visibility);
  TestTrue(TEXT("Landmark identity has actual tall model collision"),Found && Model.ImpactPoint.Z-Landmark.Z>1000.f);
 }
 const auto& Climb=*Layout.Routes.FindByPredicate([](const auto& R){return R.Name==TEXT("Switchback Climb");});
 const FVector MiddleRail=(Climb.Points[1]+Climb.Points[2])*.5f,RailTravel=(Climb.Points[2]-Climb.Points[1]).GetSafeNormal();
 const FVector RailPoint=MiddleRail+FVector(-RailTravel.Y,RailTravel.X,0)*(Climb.Width*.5f+90)+FVector(0,0,90);
 FHitResult Rail;
 TestTrue(TEXT("Exposed climb has real visible guardrail collision"),World->LineTraceSingleByChannel(Rail,RailPoint+FVector(0,0,100),RailPoint-FVector(0,0,40),ECC_Visibility) && Rail.ImpactPoint.Z>RailPoint.Z-30);
 int32 Checked=0;
 for(const auto& Route:Layout.Routes) for(int32 I=1;I<Route.Points.Num();++I)
 {
  const FVector Middle=(Route.Points[I-1]+Route.Points[I])*.5f;
  FHitResult Hit;
  const bool Found=World->LineTraceSingleByChannel(Hit,Middle+FVector(0,0,100),Middle-FVector(0,0,300),ECC_Visibility);
  TestTrue(FString::Printf(TEXT("Actual collidable road: %s span %d"),*Route.Name,I),Found);
  if(Found)
  {
   if(FMath::Abs(Hit.ImpactPoint.Z-Middle.Z)>10.f || Hit.GetComponent()->GetName()!=TEXT("HillsideRoads")) AddInfo(FString::Printf(TEXT("Surface diagnostic %s span %d at %s hit %s component %s"),*Route.Name,I,*Middle.ToCompactString(),*Hit.ImpactPoint.ToCompactString(),*GetNameSafe(Hit.GetComponent())));
   TestTrue(TEXT("Road height matches authored grade"),FMath::Abs(Hit.ImpactPoint.Z-Middle.Z)<10.f);
   TestTrue(TEXT("Road normal supports driving"),Hit.ImpactNormal.Z>.98f);
   TestTrue(TEXT("Trace hits road geometry, not terrain intruding through it"),Hit.GetComponent() && Hit.GetComponent()->GetName()==TEXT("HillsideRoads"));
  }
  ++Checked;
 }
 TestTrue(TEXT("Physical verification spans the full route network"),Checked>30);
 const auto Roads=Layout.BuildRoadNetwork();
 auto* TrafficCar=World->SpawnActor<ACitixTrafficVehicle>();
 TrafficCar->InitializeVehicle(ECitixCarType::Sedan,FLinearColor::White,123);
 TArray<FCitixCarPartDesc> CarParts;
 FCitixCarLibrary::BuildPartDescs(ECitixCarType::Sedan,CarParts);
 FCollisionQueryParams TrafficQuery; TrafficQuery.AddIgnoredActor(TrafficCar);
 for(int32 E=0;E<Roads.Edges.Num();++E)
 {
  const FVector Centre=Roads.EdgePoint3D(E,.5f);
  const FVector2D Travel=Roads.EdgeDirection(E),Right(-Travel.Y,Travel.X);
  const auto Spec=Roads.GetTrafficRoadSpec(E);
  for(float Direction:{-1.f,1.f}) for(bool Parked:{false,true})
  {
   // RelocateParkedCars rejects short edges; curve tessellation is not parking.
   if(Parked && Roads.EdgeLength(E)<1500.f) continue;
   const float Offset=Parked ? Roads.Edges[E].SurfaceWidth*.5f-130.f : Spec.LaneWidth*.5f;
   const FVector2D XY=FVector2D(Centre)+Right*(Offset*Direction);
   const float Yaw=(FVector(Travel*Direction,0)).Rotation().Yaw;
   const FTransform Pose=Roads.EdgeSurfacePose(E,XY,Yaw);
   TrafficCar->SetActorTransform(Pose); TrafficCar->SetVehicleVisible(true); TrafficCar->PublishMotion(Parked ? 0.f : 30.f);
   TestTrue(TEXT("Replicated traffic snapshot retains elevated slope pose"),TrafficCar->Motion.Location.Equals(Pose.GetLocation(),.01f) && TrafficCar->Motion.Rotation.Equals(Pose.Rotator(),.01f));
   for(const auto& Part:CarParts) if(Part.bWheel)
   {
    const FVector Contact=Pose.TransformPosition(Part.Center-FVector(0,0,Part.Size.X*.5f));
    FHitResult Ground;
    const bool Supported=World->LineTraceSingleByChannel(Ground,Contact+FVector(0,0,40),Contact-FVector(0,0,40),ECC_Visibility,TrafficQuery);
    // A rigid axle spans several small curve facets; straight roads retain the tighter tolerance.
    const float ContactTolerance=Roads.EdgeLength(E)<500.f ? 8.f : 3.f;
    if(!Supported || FMath::Abs(Ground.ImpactPoint.Z-Contact.Z)>=ContactTolerance) AddInfo(FString::Printf(TEXT("Contact diagnostic edge=%d contact=%s hit=%s delta=%.3f component=%s"),E,*Contact.ToCompactString(),*Ground.ImpactPoint.ToCompactString(),Ground.ImpactPoint.Z-Contact.Z,*GetNameSafe(Ground.GetComponent())));
    TestTrue(FString::Printf(TEXT("Traffic tyre on road: edge=%d direction=%.0f parked=%d"),E,Direction,Parked),Supported && Ground.GetComponent() && Ground.GetComponent()->GetName()==TEXT("HillsideRoads") && FMath::Abs(Ground.ImpactPoint.Z-Contact.Z)<ContactTolerance);
   }
  }
 }
 TrafficCar->Destroy();
 for(const FVector& Site:Layout.RelaySites)
 {
  TArray<FVector> RoutePoints;
  FCitixRouteHelper::BuildRoutePoints(Roads,Layout.Spawns[0]+FVector(0,0,90),Site+FVector(0,0,90),RoutePoints);
  TestTrue(TEXT("Every relay receives actual route guidance"),RoutePoints.Num()>=2);
  for(int32 I=0;I<RoutePoints.Num()-1;++I)
  {
   FHitResult Road;
   const FVector P=RoutePoints[I];
   const bool Found=World->LineTraceSingleByChannel(Road,P+FVector(0,0,100),P-FVector(0,0,200),ECC_Visibility);
   if(!Found || !Road.GetComponent() || Road.GetComponent()->GetName()!=TEXT("HillsideRoads") || FMath::Abs(P.Z-Road.ImpactPoint.Z-60.f)>=3.f) AddInfo(FString::Printf(TEXT("Guidance diagnostic point=%s hit=%s component=%s delta=%.3f"),*P.ToCompactString(),*Road.ImpactPoint.ToCompactString(),*GetNameSafe(Road.GetComponent()),P.Z-Road.ImpactPoint.Z));
   TestTrue(TEXT("Navigation points stay sixty centimetres over real roads"),Found && Road.GetComponent() && Road.GetComponent()->GetName()==TEXT("HillsideRoads") && FMath::Abs(P.Z-Road.ImpactPoint.Z-60.f)<3.f);
  }
 }
 for(const auto& Route:Layout.Routes) if(Route.bTunnel)
 {
  const FVector A=Route.Points[0],B=Route.Points.Last();
  const FVector Across=FVector(-(B-A).Y,(B-A).X,0).GetSafeNormal();
  for(float T:{.04f,.96f}) for(float Side:{-1.f,1.f})
  {
   const FVector Shoulder=FMath::Lerp(A,B,T)+Across*(Side*(Route.Width*.5f+200));
   FHitResult ShoulderGround;
   TestTrue(TEXT("Terrain shell beside each tunnel portal is sealed and collidable"),World->LineTraceSingleByChannel(ShoulderGround,FVector(Shoulder.X,Shoulder.Y,20000),FVector(Shoulder.X,Shoulder.Y,-1000),ECC_Visibility) && ShoulderGround.GetComponent()->GetName()==TEXT("HillsideTerrain"));
  }
  FHitResult Roof;
  const FVector Middle=(A+B)*.5f;
  TestTrue(TEXT("Tunnel has an actual collision roof"),World->LineTraceSingleByChannel(Roof,Middle+FVector(0,0,100),Middle+FVector(0,0,800),ECC_Visibility));
  if(Roof.bBlockingHit) TestTrue(TEXT("Tunnel provides at least five metres of headroom"),Roof.ImpactPoint.Z-Middle.Z>=500.f);
  for(int32 I=1;I<Route.Points.Num();++I)
  {
   const FVector Start=Route.Points[I-1],End=Route.Points[I];
   FHitResult Blocker;
   const bool Blocked=World->SweepSingleByChannel(Blocker,Start+FVector(0,0,110),End+FVector(0,0,110),(End-Start).Rotation().Quaternion(),ECC_Visibility,FCollisionShape::MakeBox(FVector(210,95,70)));
   TestFalse(FString::Printf(TEXT("Car volume passes through tunnel span %d; blocker=%s"),I,*GetNameSafe(Blocker.GetComponent())),Blocked);
   if(Blocked) AddInfo(FString::Printf(TEXT("Tunnel sweep time=%.5f point=%s normal=%s penetration=%d depth=%.3f"),Blocker.Time,*Blocker.ImpactPoint.ToCompactString(),*Blocker.ImpactNormal.ToCompactString(),Blocker.bStartPenetrating,Blocker.PenetrationDepth));
  }
 }
 World->DestroyWorld(false);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixHillsideGenerationTest,"CitixChase.Hillside.WorldGeneration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixHillsideGenerationTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* City=World->SpawnActor<ACitixCityGenerator>();
 City->bHillsideMap=true; City->bSpawnTraffic=false; City->bSpawnPedestrians=false; City->bSpawnStreetLights=false; City->bLogStats=false;
 City->GenerateCity();
 const auto Layout=FCitixHillsideLayout::Build();
 TestTrue(TEXT("Production generator creates hillside graph"),City->GetRoadNetwork().Nodes.ContainsByPredicate([](const auto& Node){return Node.Elevation>=6000.f;}));
 TestTrue(TEXT("Player spawn uses hillside elevation"),City->GetPlayerSpawnTransform().GetLocation().Z>900.f);
 for(const FVector& Site:Layout.Spawns)
 {
  FTransform Surface;
  for(float Yaw:{0.f,45.f,90.f,135.f})
   TestTrue(TEXT("Graded player start passes rotated full vehicle clearance"),ACitixCityGenerator::ValidateChaseSurface(World,Site,FVector(240,110,85),Yaw,nullptr,Surface));
 }
 for(const FVector& Site:Layout.RelaySites)
 {
  FTransform Surface;
  TestTrue(TEXT("Authored relay footprint is dry"),City->IsDryFootprint(Site,FVector2D(230,95),0));
  TestTrue(TEXT("Actual map supports objective surface recovery"),ACitixCityGenerator::ValidateChaseSurface(World,Site+FVector(0,0,100),FVector(230,95,60),0,nullptr,Surface,false));
 }
 TestFalse(TEXT("Coastal water is unsafe"),City->IsDryFootprint(FVector(0,-29000,0),FVector2D(230,95),0));
 for(const auto& Route:Layout.Routes) if(Route.Name==TEXT("Marina Access") || Route.Name==TEXT("Summit Service Road"))
 {
  FTransform Surface;
  const float Yaw=(Route.Points.Last()-Route.Points[0]).Rotation().Yaw;
  const bool Valid=ACitixCityGenerator::ValidateChaseSurface(World,Route.Points.Last(),FVector(240,110,85),Yaw,nullptr,Surface);
  TestTrue(FString::Printf(TEXT("Landmark access endpoint admits a complete car: %s"),*Route.Name),Valid);
  if(!Valid)
  {
   FHitResult Ground,Blocker; const FVector P=Route.Points.Last();
   World->LineTraceSingleByChannel(Ground,P+FVector(0,0,300),P-FVector(0,0,1500),ECC_Visibility);
   World->SweepSingleByChannel(Blocker,Ground.ImpactPoint+FVector(0,0,90),Ground.ImpactPoint+FVector(0,0,90),FRotator(0,Yaw,0).Quaternion(),ECC_WorldStatic,FCollisionShape::MakeBox(FVector(240,110,85)));
   AddInfo(FString::Printf(TEXT("Endpoint %s ground=%s at=%s blocker=%s at=%s dry=%d"),*Route.Name,*GetNameSafe(Ground.GetComponent()),*Ground.ImpactPoint.ToCompactString(),*GetNameSafe(Blocker.GetComponent()),*Blocker.ImpactPoint.ToCompactString(),City->IsDryFootprint(P,FVector2D(240,110),Yaw)));
  }
 }
 City->ClearCity();
 FHitResult Remaining;
 const FVector Check=Layout.RelaySites[0];
 TestFalse(TEXT("Map teardown removes generated road collision"),World->LineTraceSingleByChannel(Remaining,Check+FVector(0,0,100),Check-FVector(0,0,300),ECC_Visibility));
 World->DestroyWorld(false);
 return true;
}
#endif
