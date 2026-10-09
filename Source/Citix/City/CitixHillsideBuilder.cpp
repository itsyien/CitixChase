#include "City/CitixHillsideBuilder.h"
#include "City/CitixHillsideLayout.h"
#include "ProceduralMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"

namespace
{
 struct FSurface
 {
  TArray<FVector> Vertices,Normals;
  TArray<int32> Indices;
  TArray<FVector2D> UV;
  void Triangle(const FVector& A,const FVector& B,const FVector& C)
  {
   const int32 Base=Vertices.Num();
   const FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
   if(Normal.IsNearlyZero()) return;
   for(const FVector& P:{A,B,C}) {Vertices.Add(P); Normals.Add(Normal); UV.Add(FVector2D(P)/500.f);}
   // Unreal's procedural surface front-face winding is clockwise. Keep the
   // explicit outward normals while submitting the visible side accordingly.
   Indices.Append({Base,Base+2,Base+1});
  }
  void Quad(const FVector& A,const FVector& B,const FVector& C,const FVector& D)
  { Triangle(A,B,C); Triangle(C,B,D); }
  void Apply(UProceduralMeshComponent* Mesh,ECitixSurface Material,bool Collision=true)
  {
   Mesh->ClearAllMeshSections();
   Mesh->CreateMeshSection(0,Vertices,Indices,Normals,UV,TArray<FColor>(),TArray<FProcMeshTangent>(),Collision);
   Mesh->SetMaterial(0,FCitixSurfaceLibrary::GetMaterial(Material));
  }
 };
 FVector RoadRight(const FVector& Delta)
 { return FVector(-Delta.Y,Delta.X,0).GetSafeNormal(); }
}

ACitixHillsideBuilder::ACitixHillsideBuilder()
{
 Roads=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HillsideRoads"));
 SetRootComponent(Roads);
 Terrain=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HillsideTerrain")); Terrain->SetupAttachment(Roads);
 Tunnel=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HillsideTunnel")); Tunnel->SetupAttachment(Roads);
 Water=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HillsideWater")); Water->SetupAttachment(Roads);
 Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 for(auto* Mesh:{Roads.Get(),Terrain.Get(),Tunnel.Get()})
 {
  Mesh->SetCollisionObjectType(ECC_WorldStatic);
  Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
  Mesh->SetCollisionResponseToAllChannels(ECR_Block);
  Mesh->bUseComplexAsSimpleCollision=true;
 }
}

void ACitixHillsideBuilder::Build(const FCitixHillsideLayout& Layout)
{
 FSurface RoadGeometry,TerrainGeometry,TunnelGeometry,WaterGeometry;
 const auto Network=Layout.BuildRoadNetwork();
 for(const auto& Route:Layout.Routes)
 {
  TArray<FVector> Left,Right;
  for(int32 I=0;I<Route.Points.Num();++I)
  {
   const FVector Before=Route.Points[I]-Route.Points[FMath::Max(0,I-1)];
   const FVector After=Route.Points[FMath::Min(I+1,Route.Points.Num()-1)]-Route.Points[I];
   FVector Across=RoadRight(Before)+RoadRight(After);
   Across.Normalize();
   const FVector Reference=RoadRight(After.IsNearlyZero() ? Before : After);
   const float Miter=FMath::Clamp(1.f/FMath::Max(.5f,float(FVector::DotProduct(Across,Reference))),1.f,2.f);
   const FVector Offset=Across*(Route.Width*.5f*Miter);
   Left.Add(Route.Points[I]-Offset); Right.Add(Route.Points[I]+Offset);
  }
  for(int32 I=1;I<Route.Points.Num();++I)
  {
   // Each span is a grade plane across the full driving width. Using miter
   // endpoints here warps the lanes even when the centreline height is correct.
   const FVector SpanOffset=RoadRight(Route.Points[I]-Route.Points[I-1])*(Route.Width*.5f);
   // Tiny longitudinal overlap closes collision cracks caused by float mesh
   // vertices at exact shared seams; extend on the same grade plane.
   const FVector SeamAllowance=(Route.Points[I]-Route.Points[I-1]).GetSafeNormal()*2.f;
   const FVector Start=Route.Points[I-1]-SeamAllowance,End=Route.Points[I]+SeamAllowance;
   RoadGeometry.Quad(Start-SpanOffset,End-SpanOffset,Start+SpanOffset,End+SpanOffset);
   if(I<Route.Points.Num()-1)
   {
    const FVector NextOffset=RoadRight(Route.Points[I+1]-Route.Points[I])*(Route.Width*.5f);
    auto Join=[&](FVector A,FVector B,FVector C)
    {
     if(FVector::CrossProduct(B-A,C-A).Z<0) Swap(B,C);
     RoadGeometry.Triangle(A,B,C);
    };
    // Flat corner wedges meet both grade planes at their shared endpoint.
    for(float Side:{-1.f,1.f})
    {
     const FVector Miter=Side<0 ? Left[I] : Right[I];
     Join(Route.Points[I],Route.Points[I]+SpanOffset*Side,Miter);
     Join(Route.Points[I],Miter,Route.Points[I]+NextOffset*Side);
    }
   }
   const FVector Foundation(0,0,220);
   RoadGeometry.Quad(Left[I-1],Left[I-1]-Foundation,Left[I],Left[I]-Foundation);
   RoadGeometry.Quad(Right[I-1]-Foundation,Right[I-1],Right[I]-Foundation,Right[I]);
   if(!Route.bTunnel) continue;
   const FVector Roof(0,0,600);
   // Interior-facing roof and walls, with an exterior shell. The route remains
   // a continuous graded road; both portals are open and car-accessible.
   TunnelGeometry.Quad(Left[I-1]+Roof,Right[I-1]+Roof,Left[I]+Roof,Right[I]+Roof);
   TunnelGeometry.Quad(Left[I-1],Left[I-1]+Roof,Left[I],Left[I]+Roof);
   TunnelGeometry.Quad(Right[I-1]+Roof,Right[I-1],Right[I]+Roof,Right[I]);
   const FVector Cap(0,0,120);
   TunnelGeometry.Quad(Left[I-1]+Roof+Cap,Left[I]+Roof+Cap,Right[I-1]+Roof+Cap,Right[I]+Roof+Cap);
   TunnelGeometry.Quad(Left[I-1]+Roof,Left[I]+Roof,Left[I-1]+Roof+Cap,Left[I]+Roof+Cap);
   TunnelGeometry.Quad(Right[I-1]+Roof,Right[I-1]+Roof+Cap,Right[I]+Roof,Right[I]+Roof+Cap);
  }
 }
 // Cap route junctions using the nearest graded centreline height, avoiding
 // flat discs that protrude into ascending approaches.
 for(int32 I=0;I<Network.Nodes.Num();++I) if(Network.GetNodeDegree(I)>=3)
 {
  const auto& Node=Network.Nodes[I]; const FVector Centre(Node.Position,Node.Elevation);
  // The narrow road strips alone cannot support a car's ninety-degree turn.
  float Radius=3000.f;
  for(int32 Edge:Network.NodeEdgeIndices[I]) Radius=FMath::Max(Radius,Network.Edges[Edge].CorridorWidth*.5f);
  // Keep widened caps inside short level approaches, especially the tunnel lip.
  for(int32 Edge:Network.NodeEdgeIndices[I]) Radius=FMath::Min(Radius,FMath::Max(Network.EdgeLength(Edge),Network.Edges[Edge].CorridorWidth*.5f));
  for(int32 Side=0;Side<24;++Side)
  {
   auto Ring=[&](int32 Index)
   {
    const float Angle=2.f*PI*Index/24.f;
    const FVector2D XY=Node.Position+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius;
    float Height=MAX_flt;
    // A nearest-edge tie can pick a level cross street and raise this cap
    // above the ascending lane. Keep the infill beneath every approach;
    // the actual road strips retain their independently correct surfaces.
    for(int32 EdgeIndex:Network.NodeEdgeIndices[I])
    {
     const auto& Edge=Network.Edges[EdgeIndex];
     const FVector2D A=Network.Nodes[Edge.NodeA].Position,Delta=Network.Nodes[Edge.NodeB].Position-A;
     const float T=FMath::Clamp(FVector2D::DotProduct(XY-A,Delta)/Delta.SizeSquared(),0.,1.);
     Height=FMath::Min(Height,float(Network.EdgePoint3D(EdgeIndex,T).Z));
    }
    return FVector(XY,Height);
   };
   RoadGeometry.Triangle(Centre,Ring(Side),Ring(Side+1));
  }
 }
 auto HeightAt=[&](const FVector2D& XY)
 {
  if(XY.Y<-27500) return -500.f;
  float Height,Distance;
  if(!Network.FindSurfaceHeight(XY,Height,Distance)) return -500.f;
  float TerrainHeight=Height-180.f+FMath::SmoothStep(1300.f,9000.f,Distance)*2000.f;
  // Raise the hillside shell above the middle of the tunnel, retaining open
  // approach cuttings at both ends. Floor traces start inside this shell.
  for(const auto& Route:Layout.Routes) if(Route.bTunnel) for(int32 I=1;I<Route.Points.Num();++I)
  {
   const FVector2D A(Route.Points[I-1]),AB=FVector2D(Route.Points[I])-A;
   const float T=FMath::Clamp(FVector2D::DotProduct(XY-A,AB)/AB.SizeSquared(),0.,1.);
   const float Near=FVector2D::Distance(XY,A+AB*T);
   const float Cover=FMath::SmoothStep(.08f,.2f,T)*(1.f-FMath::SmoothStep(.8f,.92f,T))*(1.f-FMath::SmoothStep(900.f,2300.f,Near));
   TerrainHeight=FMath::Lerp(TerrainHeight,float(FMath::Lerp(Route.Points[I-1].Z,Route.Points[I].Z,T))+1100.f,Cover);
  }
  if(XY.X>-11000 && XY.X<9000 && XY.Y>0 && XY.Y<10000)
   TerrainHeight=FMath::Min(TerrainHeight,2180.f);
  return TerrainHeight;
 };
 // Broad faceted triangles give a coherent hillside silhouette, with no height
 // texture or imported landscape assets added to the build.
 constexpr float Cell=1000.f;
 struct FCell { bool bCut=false; TArray<FVector> Original,Actual; };
 TMap<FIntPoint,FCell> Cells;
 for(float X=-40000;X<40000;X+=Cell) for(float Y=-30000;Y<38000;Y+=Cell)
 {
  const FVector2D A(X,Y),B(X+Cell,Y),C(X,Y+Cell),D(X+Cell,Y+Cell);
  const float HA=HeightAt(A),HB=HeightAt(B),HC=HeightAt(C),HD=HeightAt(D);
  FCell Record; Record.Original={FVector(A,HA),FVector(B,HB),FVector(C,HC),FVector(D,HD)};
  bool Cutting=false;
  for(const auto& Route:Layout.Routes) if(Route.bTunnel) for(int32 I=1;I<Route.Points.Num();++I)
  {
   const FVector2D Start(Route.Points[I-1]),Delta=FVector2D(Route.Points[I])-Start,Middle=(A+D)*.5f;
   const float T=FVector2D::DotProduct(Middle-Start,Delta)/Delta.SizeSquared();
   const float Distance=FVector2D::Distance(Middle,Start+Delta*FMath::Clamp(T,0.f,1.f));
   const float Floor=FMath::Lerp(Route.Points[I-1].Z,Route.Points[I].Z,FMath::Clamp(T,0.f,1.f));
   // Terrain transitioning from below the road to above its roof otherwise
   // creates a diagonal blocking sheet across the portal. Carve the approach
   // volume while retaining hillside triangles entirely above the shell.
   if(T>-.08f && T<1.08f && Distance<Route.Width*.5f+Cell && FMath::Min(FMath::Min(HA,HB),FMath::Min(HC,HD))<Floor+750.f)
    Cutting=true;
  }
  Record.bCut=Cutting;
  if(!Cutting) {Record.Actual=Record.Original; TerrainGeometry.Quad(Record.Actual[0],Record.Actual[1],Record.Actual[2],Record.Actual[3]);}
  else
  {
   auto Floor=[&](const FVector2D& XY) {float Height,Distance; Network.FindSurfaceHeight(XY,Height,Distance); return FVector(XY,Height-180.f);};
   // A carved approach has an excavation floor, never a hole into the sky.
   Record.Actual={Floor(A),Floor(B),Floor(C),Floor(D)};
   TerrainGeometry.Quad(Record.Actual[0],Record.Actual[1],Record.Actual[2],Record.Actual[3]);
  }
  Cells.Add(FIntPoint(FMath::RoundToInt((X+40000)/Cell),FMath::RoundToInt((Y+30000)/Cell)),MoveTemp(Record));
 }
 const FIntPoint Neighbours[]={{0,-1},{1,0},{0,1},{-1,0}};
 const int32 Ends[][2]={{0,1},{1,3},{3,2},{2,0}};
 for(const auto& Pair:Cells) if(Pair.Value.bCut) for(int32 Edge=0;Edge<4;++Edge)
 {
  const auto* Adjacent=Cells.Find(Pair.Key+Neighbours[Edge]);
  if(!Adjacent || Adjacent->bCut) continue;
  FVector BottomA=Pair.Value.Actual[Ends[Edge][0]],BottomB=Pair.Value.Actual[Ends[Edge][1]];
  FVector TopA=Pair.Value.Original[Ends[Edge][0]],TopB=Pair.Value.Original[Ends[Edge][1]];
  for(const auto& Route:Layout.Routes) if(Route.bTunnel) for(int32 I=1;I<Route.Points.Num();++I)
  {
   const FVector2D Start(Route.Points[I-1]),Delta=FVector2D(Route.Points[I])-Start,Middle((BottomA+BottomB)*.5f);
   const float T=FMath::Clamp(FVector2D::DotProduct(Middle-Start,Delta)/Delta.SizeSquared(),0.,1.);
   if(FVector2D::Distance(Middle,Start+Delta*T)>Route.Width*.5f+100.f) continue;
   // A boundary crossing over the tunnel stays above the roof. Side cutting
   // faces reach the excavation floor; neither can block the driving volume.
   const float Roof=FMath::Lerp(Route.Points[I-1].Z,Route.Points[I].Z,T)+720.f;
   BottomA.Z=FMath::Max(BottomA.Z,double(Roof)); BottomB.Z=FMath::Max(BottomB.Z,double(Roof));
   TopA.Z=FMath::Max(TopA.Z,BottomA.Z); TopB.Z=FMath::Max(TopB.Z,BottomB.Z);
  }
  TerrainGeometry.Quad(TopA,TopB,BottomA,BottomB);
  TerrainGeometry.Quad(TopB,TopA,BottomB,BottomA);
 }
 auto Skirt=[&](const FVector2D& A,const FVector2D& B)
 {
  TerrainGeometry.Quad(FVector(A,HeightAt(A)),FVector(B,HeightAt(B)),FVector(A,-1500),FVector(B,-1500));
 };
 for(float Y=-30000;Y<38000;Y+=Cell) {Skirt({-40000,Y},{-40000,Y+Cell}); Skirt({40000,Y+Cell},{40000,Y});}
 for(float X=-40000;X<40000;X+=Cell) {Skirt({X,38000},{X+Cell,38000}); Skirt({X+Cell,-30000},{X,-30000});}
 WaterGeometry.Quad({-100000,-150000,-100},{100000,-150000,-100},{-100000,-26000,-100},{100000,-26000,-100});
 RoadGeometry.Apply(Roads,ECitixSurface::Asphalt);
 TerrainGeometry.Apply(Terrain,ECitixSurface::Grass);
 TunnelGeometry.Apply(Tunnel,ECitixSurface::FacadeConcrete);
 WaterGeometry.Apply(Water,ECitixSurface::Water,false);
 BuildDetails(Layout);
}
