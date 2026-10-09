#include "City/CitixHillsideBuilder.h"
#include "City/CitixHillsideLayout.h"
#include "City/CitixBuildingGenerator.h"
#include "City/CitixCityChunk.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"

void ACitixHillsideBuilder::BuildDetails(const FCitixHillsideLayout& Layout)
{
 for(auto Component:Details) if(Component) Component->DestroyComponent();
 Details.Reset(); LandmarkLocations.Reset(); LampLocations.Reset();
 TMap<int32,UHierarchicalInstancedStaticMeshComponent*> Groups;
 auto Box=[&](ECitixSurface Surface,const FVector& Position,const FVector& Size,FRotator Rotation=FRotator::ZeroRotator,bool Collision=true)
 {
  const int32 Key=int32(Surface)*2+int32(Collision);
  auto*& Group=Groups.FindOrAdd(Key);
  if(!Group)
  {
   Group=NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
   AddInstanceComponent(Group); Group->SetupAttachment(GetRootComponent());
   Group->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(Surface));
   Group->SetMaterial(0,FCitixSurfaceLibrary::GetMaterial(Surface));
   Group->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
   Group->SetCollisionObjectType(ECC_WorldStatic); Group->SetCollisionResponseToAllChannels(ECR_Block);
   Group->SetCanEverAffectNavigation(false); Group->RegisterComponent(); Details.Add(Group);
  }
  Group->AddInstance(FTransform(Rotation,Position,ACitixCityChunk::ComputeInstanceScale(Surface,Size)),true);
 };
 auto Ground=[&](FVector2D XY)
 {
  FHitResult Hit;
  if(GetWorld()->LineTraceSingleByChannel(Hit,FVector(XY,20000),FVector(XY,-2000),ECC_Visibility)) return float(Hit.ImpactPoint.Z);
  return 100.f;
 };
 auto Pad=[&](FVector2D XY,FVector2D Size)
 {
  float Base=-2000.f;
  for(float SX:{-1.f,1.f}) for(float SY:{-1.f,1.f}) Base=FMath::Max(Base,Ground(XY+FVector2D(SX*Size.X*.5f,SY*Size.Y*.5f)));
  Base+=12.f;
  Box(ECitixSurface::FacadeConcrete,FVector(XY,Base-200.f),FVector(Size,400));
  return Base;
 };
 auto Network=Layout.BuildRoadNetwork();
 auto Reserved=[&](const FVector& P)
 {
  return FVector::Dist2D(P,FVector(-1000,0,2200))<1400.f || FVector::Dist2D(P,FVector(-27000,-25000,100))<2300.f || FVector::Dist2D(P,FVector(1000,31000,6000))<2300.f || Layout.RelaySites.ContainsByPredicate([&](const FVector& Site){return FVector::Dist2D(P,Site)<1600.f;}) || Layout.Exits.ContainsByPredicate([&](const FVector& Site){return FVector::Dist2D(P,Site)<1800.f;});
 };
 for(const auto& Route:Layout.Routes) for(int32 I=1;I<Route.Points.Num();++I)
 {
  const FVector A=Route.Points[I-1],B=Route.Points[I],Delta=B-A;
  const float Length=Delta.Size(); const FVector Direction=Delta.GetSafeNormal(),Right(-Direction.Y,Direction.X,0);
  const FRotator Rotation=Direction.Rotation();
  for(float S=500;S<Length-200;S+=900)
   Box(ECitixSurface::Marking,A+Direction*S+FVector(0,0,3),FVector(350,14,2),Rotation,false);
  for(float S=0;S<Length;S+=400)
  {
   const float Span=FMath::Min(400.f,Length-S);
   const FVector P=A+Direction*(S+Span*.5f);
   for(float Side:{-1.f,1.f})
    Box(ECitixSurface::MarkingDim,P+Right*(Side*(Route.Width*.5f-60.f))+FVector(0,0,3),FVector(Span,8,2),Rotation,false);
   if(Route.bTunnel || Reserved(P)) continue;
   bool Junction=false;
   for(int32 N=0;N<Network.Nodes.Num();++N) if(Network.GetNodeDegree(N)>=3 && FVector2D::Distance(Network.Nodes[N].Position,FVector2D(P))<3300.f) {Junction=true; break;}
   if(Junction) continue;
   for(float Side:{-1.f,1.f})
   {
    const FVector Edge=P+Right*(Side*(Route.Width*.5f+90.f));
    Box(ECitixSurface::PropMetal,Edge+FVector(0,0,90),FVector(Span+8,20,28),Rotation);
    Box(ECitixSurface::PropMetal,Edge+FVector(0,0,48),FVector(18,18,96));
   }
  }
  for(float S=2200;S<Length-1000;S+=6000)
  {
   const FVector P=A+Direction*S+Right*(Route.Width*.5f+220.f);
   if(Reserved(P)) continue;
   const FVector Base(P.X,P.Y,Ground(FVector2D(P)));
   Box(ECitixSurface::PropMetal,Base+FVector(0,0,300),FVector(18,18,600));
   Box(ECitixSurface::WindowWarmBright,Base+FVector(0,0,600),FVector(110,80,24),FRotator::ZeroRotator,false);
   LampLocations.Add(Base+FVector(0,0,580));
  }
 }
 // The town uses the same building archetypes and facade vocabulary as the city.
 FRandomStream Rng(47821); const auto& Settings=UCitixCitySettings::Get();
 for(int32 X=0;X<5;++X) for(int32 Y=0;Y<3;++Y)
 {
  if(X==2 && Y<=1) continue; // open the clock square toward the southern driving approach
  FCitixBuildingSpec Spec;
  Spec.Center=FVector2D(-8000+X*3500,2200+Y*2900); Spec.Footprint=FVector2D(2200,1600);
  Spec.BaseZ=Pad(Spec.Center,Spec.Footprint+FVector2D(200));
  Spec.District=ECitixDistrict::OldTown; Spec.FacadeStyle=(X+Y)%4; Spec.Floors=3+(X+Y)%3;
  Spec.FloorHeight=320; Spec.Archetype=Y==0 ? ECitixBuildingArchetype::Shophouse : ECitixBuildingArchetype::ApartmentSlab;
  TArray<FCitixBoxInstance> Pieces; FCitixBuildingGenerator::GenerateBuildingBoxes(Spec,Settings,Rng,Pieces);
  for(const auto& Piece:Pieces) Box(Piece.Surface,Piece.Center,Piece.Size,Piece.Rotation,!FCitixSurfaceLibrary::IsEmissive(Piece.Surface));
 }
 const FVector2D ClockXY(-1000,5100); const float ClockBase=Pad(ClockXY,FVector2D(2600,2200));
 const FVector Clock(ClockXY,ClockBase); LandmarkLocations.Add(Clock);
 Box(ECitixSurface::FacadeBrick,Clock+FVector(0,0,1700),FVector(900,900,3400));
 Box(ECitixSurface::FacadeWhite,Clock+FVector(0,0,3200),FVector(1200,1200,160));
 Box(ECitixSurface::RoofDark,Clock+FVector(0,0,3550),FVector(1400,1400,400));
 // Four high-contrast square clock faces and two hands per face.
 for(float Yaw:{0.f,90.f,180.f,270.f})
 {
  const FVector Forward=FRotator(0,Yaw,0).Vector(),Face=Clock+Forward*462.f+FVector(0,0,2850);
  Box(ECitixSurface::Marking,Face,FVector(20,620,620),FRotator(0,Yaw,0),false);
  Box(ECitixSurface::PropDark,Face+Forward*14.f+FVector(0,0,100),FVector(12,20,220),FRotator(0,Yaw,0),false);
  Box(ECitixSurface::PropDark,Face+Forward*14.f+FRotator(0,Yaw,0).RotateVector(FVector(0,80,0)),FVector(12,180,20),FRotator(0,Yaw,0),false);
 }
 const FVector2D SummitXY(1000,31000); const float SummitBase=Pad(SummitXY,FVector2D(3200,2800));
 const FVector Summit(SummitXY,SummitBase); LandmarkLocations.Add(Summit);
 Box(ECitixSurface::FacadeConcrete,Summit+FVector(900,0,300),FVector(1000,1400,600));
 Box(ECitixSurface::PropMetal,Summit+FVector(0,0,2100),FVector(90,90,4200));
 for(float Side:{-1.f,1.f}) for(float Axis:{-1.f,1.f})
 {
  const FVector Foot=Summit+FVector(Side*600,Axis*600,0),Top=Summit+FVector(0,0,4000),Beam=Top-Foot;
  Box(ECitixSurface::PropMetal,(Foot+Top)*.5f,FVector(Beam.Size(),36,36),Beam.Rotation());
 }
 for(int32 I=1;I<=5;++I) Box(ECitixSurface::PropMetal,Summit+FVector(0,0,I*700),FVector(1100-I*130,1100-I*130,24));
 Box(ECitixSurface::WindowWarmBright,Summit+FVector(0,0,4220),FVector(90,90,60),FRotator::ZeroRotator,false);
 const FVector2D MarinaXY(-27000,-25000); const float MarinaBase=Pad(MarinaXY,FVector2D(4200,2400));
 const FVector Marina(MarinaXY,MarinaBase); LandmarkLocations.Add(Marina);
 Box(ECitixSurface::FacadeMetal,Marina+FVector(0,0,1600),FVector(110,110,3200));
 Box(ECitixSurface::PropMetal,Marina+FVector(1000,0,3100),FVector(2600,160,160));
 Box(ECitixSurface::PropDark,Marina+FVector(2100,0,2450),FVector(24,24,1200));
 for(int32 I=0;I<4;++I)
 {
  const FVector Dock(-26000+I*1400,-26800,120);
  Box(ECitixSurface::FacadeBeige,Dock,FVector(800,1000,80));
  Box(ECitixSurface::CarPaint,Dock+FVector(0,-500,-50),FVector(500,700,120),FRotator::ZeroRotator,false);
  Box(ECitixSurface::WindowWhite,Dock+FVector(0,-500,90),FVector(320,400,160),FRotator::ZeroRotator,false);
 }
 // Shared city tree meshes, in small deterministic groves clear of roads and landmarks.
 for(int32 X=-32000;X<=32000;X+=5000) for(int32 Y=-18000;Y<=32000;Y+=5000)
 {
  if(Rng.FRand()>.65f) continue;
  const FVector2D XY(X+Rng.FRandRange(-1200,1200),Y+Rng.FRandRange(-1200,1200));
  float RoadHeight,Distance;
  if(!Network.FindSurfaceHeight(XY,RoadHeight,Distance) || Distance<2200.f || (XY.X>-13000 && XY.X<12000 && XY.Y>-2000 && XY.Y<12000)) continue;
  if(LandmarkLocations.ContainsByPredicate([&](const FVector& P){return FVector2D::Distance(FVector2D(P),XY)<3200.f;})) continue;
  const float Base=Ground(XY);
  if(Base<0 || FMath::Abs(Ground(XY+FVector2D(250,0))-Base)>180 || FMath::Abs(Ground(XY+FVector2D(0,250))-Base)>180) continue;
  const float Scale=Rng.FRandRange(1.f,1.8f),Height=420.f*Scale;
  Box(ECitixSurface::Trunk,FVector(XY,Base+Height*.5f),FVector(34*Scale,34*Scale,Height));
  Box(ECitixSurface::Foliage,FVector(XY,Base+Height+130*Scale),FVector(720*Scale),FRotator::ZeroRotator,false);
 }
}
