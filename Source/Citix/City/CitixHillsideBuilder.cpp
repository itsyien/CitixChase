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
  void Apply(UProceduralMeshComponent* Mesh,ECitixSurface Material,bool Collision=true,int32 Section=0)
  {
   if(Section==0) Mesh->ClearAllMeshSections();
   Mesh->CreateMeshSection(Section,Vertices,Indices,Normals,UV,TArray<FColor>(),TArray<FProcMeshTangent>(),Collision);
   Mesh->SetMaterial(Section,FCitixSurfaceLibrary::GetMaterial(Material));
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
 FSurface RoadGeometry,TerrainGeometry,TunnelGeometry,WaterGeometry,RockGeometry,DryGeometry;
 const auto Network=Layout.BuildRoadNetwork();
 auto WallRange=[&](FVector A,FVector B)
 {
  FVector2D Range(0,1); const FVector D=B-A; const double Length=D.Size();
  if(Length<1) return FVector2D(1,0);
  const FVector Direction=D/Length;
  for(int32 N=0;N<Network.Nodes.Num();++N) if(Network.GetNodeDegree(N)>=3)
  {
   const FVector Offset=A-FVector(Network.Nodes[N].Position,Network.Nodes[N].Elevation);
   const double Along=FVector::DotProduct(Offset,Direction),Discriminant=Along*Along-Offset.SizeSquared()+3500.*3500.;
   if(Discriminant<0) continue;
   const double Enter=(-Along-FMath::Sqrt(Discriminant))/Length,Exit=(-Along+FMath::Sqrt(Discriminant))/Length;
   if(Enter<=0 && Exit>0) Range.X=FMath::Max(Range.X,Exit);
   if(Enter<1 && Exit>=1) Range.Y=FMath::Min(Range.Y,Enter);
  }
  return Range;
 };
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
   // Dead ends need a full-car stopping apron beyond the navigation endpoint.
   const int32 EndNode=Network.Nodes.IndexOfByPredicate([&](const auto& N){return FVector(N.Position,N.Elevation).Equals(Route.Points[I],.01f);});
   if(I==Route.Points.Num()-1 && Network.GetNodeDegree(EndNode)==1)
   {
    const FVector ApronEnd=End+(End-Start).GetSafeNormal()*600.f;
    RoadGeometry.Quad(End-SpanOffset,ApronEnd-SpanOffset,End+SpanOffset,ApronEnd+SpanOffset);
   }
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
   const auto Range=WallRange(Route.Points[I-1],Route.Points[I]);
   if(Range.X<Range.Y)
   {
    const FVector L0=FMath::Lerp(Left[I-1],Left[I],Range.X),L1=FMath::Lerp(Left[I-1],Left[I],Range.Y),R0=FMath::Lerp(Right[I-1],Right[I],Range.X),R1=FMath::Lerp(Right[I-1],Right[I],Range.Y);
    RockGeometry.Quad(L0,L0-Foundation,L1,L1-Foundation);
    RockGeometry.Quad(R0-Foundation,R0,R1-Foundation,R1);
   }
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
 const float Cell=Layout.Settings.TerrainSpacing;
 auto HeightAt=[&](FVector2D XY)
 {
  float Height=Layout.BaseTerrainHeight(XY),Closest=MAX_flt,RoadHeight=Height,Bed=0;
  for(const auto& R:Layout.Routes)
  {
   if(R.bBridge) continue;
   if(R.bTunnel && FMath::Min(FVector2D::Distance(XY,FVector2D(R.Points[0])),FVector2D::Distance(XY,FVector2D(R.Points.Last())))>1800.f) continue;
   for(int32 I=1;I<R.Points.Num();++I)
   {
    const FVector2D A(R.Points[I-1]),D=FVector2D(R.Points[I])-A;
    const float T=FMath::Clamp(FVector2D::DotProduct(XY-A,D)/D.SizeSquared(),0.,1.);
    const float Distance=FVector2D::Distance(XY,A+D*T);
    if(Distance<Closest) {Closest=Distance; RoadHeight=float(FMath::Lerp(R.Points[I-1].Z,R.Points[I].Z,T))-200.f; Bed=R.Width*.5f+Cell*1.45f;}
   }
  }
  Height=FMath::Lerp(Height,RoadHeight,1.f-FMath::SmoothStep(Bed,Bed+1800.f,Closest));
  for(const auto& House:Layout.Houses)
  {
   const FVector2D Local=(XY-FVector2D(House.Position)).GetRotated(-House.Yaw);
   const float Outside=FMath::Max(FMath::Abs(float(Local.X))-float(House.Size.X)*.5f,FMath::Abs(float(Local.Y))-float(House.Size.Y)*.5f);
   if(Outside<400) Height=FMath::Lerp(Height,float(House.Position.Z),1.f-FMath::SmoothStep(100.f,400.f,Outside));
  }
  return Height;
 };
 auto EmitTerrain=[&](const FVector& P,const FVector& Q,const FVector& R)
 {
  const FVector Normal=FVector::CrossProduct(Q-P,R-P).GetSafeNormal();
  const float Elevation=(P.Z+Q.Z+R.Z)/3;
  FSurface& Surface=Normal.Z<.82f || Elevation>7600 ? RockGeometry : Elevation>4000 ? DryGeometry : TerrainGeometry;
  Surface.Triangle(P,Q,R);
 };
 auto TerrainTriangle=[&](FVector2D A,FVector2D B,FVector2D C)
 {
  TArray<FVector2D> Polygon;
  const FVector2D Corners[]={A,B,C};
  for(int32 I=0;I<3;++I)
  {
   const FVector2D P=Corners[I],Q=Corners[(I+1)%3];
   const bool InP=Layout.ContainsLand(P),InQ=Layout.ContainsLand(Q);
   if(InP) Polygon.Add(P);
   if(InP!=InQ)
   {
    FVector2D Inside=InP ? P : Q,Outside=InP ? Q : P;
    for(int32 Step=0;Step<20;++Step) {const auto Mid=(Inside+Outside)*.5; if(Layout.ContainsLand(Mid)) Inside=Mid; else Outside=Mid;}
    Polygon.Add((Inside+Outside)*.5);
   }
  }
  for(int32 I=1;I+1<Polygon.Num();++I)
  {
   const FVector P(Polygon[0],HeightAt(Polygon[0])),Q(Polygon[I],HeightAt(Polygon[I])),R(Polygon[I+1],HeightAt(Polygon[I+1]));
   bool Clipped=false;
   for(const auto& Route:Layout.Routes) if(Route.bTunnel)
   {
    const FVector2D Origin(Route.Points[0]),Delta=FVector2D(Route.Points.Last())-Origin,Forward=Delta.GetSafeNormal(),Across(-Forward.Y,Forward.X),XY((P+Q+R)/3);
    const float T=FMath::Clamp(FVector2D::DotProduct(XY-Origin,Delta)/Delta.SizeSquared(),0.,1.);
    const float Roof=FMath::Lerp(Route.Points[0].Z,Route.Points.Last().Z,T)+720;
    if(FVector2D::Distance(XY,Origin+Delta*T)>Route.Width*.5f+Cell*1.5f || FMath::Min3(P.Z,Q.Z,R.Z)>Roof+Cell) continue;
    // Polygon difference leaves the hillside exactly up to the bore walls. The
    // roof seals the removed rectangle; no whole cells disappear around it.
    TArray<FVector> Remaining={P,Q,R};
    const FVector2D Axes[]={-Forward,Forward,-Across,Across};
    const float Limits[]={0.f,float(Delta.Size()),Route.Width*.5f,Route.Width*.5f};
    for(int32 Plane=0;Plane<4 && Remaining.Num()>=3;++Plane)
    {
     TArray<FVector> Inside,Outside,Cuts;
     for(int32 V=0;V<Remaining.Num();++V)
     {
      const FVector A=Remaining[V],B=Remaining[(V+1)%Remaining.Num()];
      const double DA=FVector2D::DotProduct(FVector2D(A)-Origin,Axes[Plane])-Limits[Plane],DB=FVector2D::DotProduct(FVector2D(B)-Origin,Axes[Plane])-Limits[Plane];
      (DA<=0 ? Inside : Outside).Add(A);
      if((DA<=0)!=(DB<=0)) {const FVector Cut=FMath::Lerp(A,B,DA/(DA-DB)); Inside.Add(Cut); Outside.Add(Cut); Cuts.Add(Cut);}
     }
     if(Cuts.Num()==2)
     {
      FVector Floor[2],Top[2];
      for(int32 V=0;V<2;++V)
      {
       const float Along=FMath::Clamp(FVector2D::DotProduct(FVector2D(Cuts[V])-Origin,Delta)/Delta.SizeSquared(),0.,1.);
       Floor[V]=Cuts[V]; Floor[V].Z=FMath::Lerp(Route.Points[0].Z,Route.Points.Last().Z,Along)+720;
       Top[V]=Cuts[V]; Top[V].Z=FMath::Max(Top[V].Z,Floor[V].Z);
      }
      RockGeometry.Quad(Floor[0],Floor[1],Top[0],Top[1]);
      RockGeometry.Quad(Floor[1],Floor[0],Top[1],Top[0]);
     }
     for(int32 V=1;V+1<Outside.Num();++V) EmitTerrain(Outside[0],Outside[V],Outside[V+1]);
     Remaining=MoveTemp(Inside);
    }
    // Preserve the mountain surface above the roof; only remove material inside
    // the driving bore. This closes sloping portal headers as well as the sides.
    TArray<FVector> AboveRoof;
    auto RoofHeight=[&](FVector V){const float Along=FMath::Clamp(FVector2D::DotProduct(FVector2D(V)-Origin,Delta)/Delta.SizeSquared(),0.,1.); return FMath::Lerp(Route.Points[0].Z,Route.Points.Last().Z,Along)+720;};
    for(int32 V=0;V<Remaining.Num();++V)
    {
     const FVector A=Remaining[V],B=Remaining[(V+1)%Remaining.Num()];
     const double DA=A.Z-RoofHeight(A),DB=B.Z-RoofHeight(B);
     if(DA>=0) AboveRoof.Add(A);
     if((DA>=0)!=(DB>=0)) AboveRoof.Add(FMath::Lerp(A,B,DA/(DA-DB)));
    }
    for(int32 V=1;V+1<AboveRoof.Num();++V) EmitTerrain(AboveRoof[0],AboveRoof[V],AboveRoof[V+1]);
    Clipped=true; break;
   }
   if(!Clipped) EmitTerrain(P,Q,R);
  }
 };
 FBox2D Bounds(ForceInit); for(auto P:Layout.CoastBoundary) Bounds+=P;
 for(float X=FMath::FloorToFloat(Bounds.Min.X/Cell)*Cell;X<Bounds.Max.X;X+=Cell) for(float Y=FMath::FloorToFloat(Bounds.Min.Y/Cell)*Cell;Y<Bounds.Max.Y;Y+=Cell)
 {
  TerrainTriangle({X,Y},{X+Cell,Y},{X,Y+Cell});
  TerrainTriangle({X,Y+Cell},{X+Cell,Y},{X+Cell,Y+Cell});
 }
 // Irregular exposed coast and hillside retaining faces use the same faceted rock palette.
 for(int32 I=0;I<Layout.CoastBoundary.Num();++I)
 {
  const auto A=Layout.CoastBoundary[I],B=Layout.CoastBoundary[(I+1)%Layout.CoastBoundary.Num()];
  const int32 Steps=FMath::CeilToInt(FVector2D::Distance(A,B)/Cell);
  for(int32 S=0;S<Steps;++S)
  {
   const auto P=FMath::Lerp(A,B,float(S)/Steps),Q=FMath::Lerp(A,B,float(S+1)/Steps);
   RockGeometry.Quad(FVector(P,HeightAt(P)),FVector(Q,HeightAt(Q)),FVector(P,-1200),FVector(Q,-1200));
  }
 }
 for(const auto& R:Layout.Routes) for(int32 I=1;I<R.Points.Num();++I)
 {
  const FVector OriginalA=R.Points[I-1],OriginalB=R.Points[I];
  const auto Range=WallRange(OriginalA,OriginalB);
  if(R.bTunnel || Range.X>=Range.Y) continue;
  const FVector A=FMath::Lerp(OriginalA,OriginalB,Range.X),B=FMath::Lerp(OriginalA,OriginalB,Range.Y),Right=RoadRight(B-A);
  for(float Side:{-1.f,1.f})
  {
   const FVector P=A+Right*(R.Width*.5f*Side),Q=B+Right*(R.Width*.5f*Side);
   const float HP=Layout.BaseTerrainHeight(FVector2D(P)),HQ=Layout.BaseTerrainHeight(FVector2D(Q));
   const FVector BottomP(P.X,P.Y,FMath::Min(float(P.Z)-80,HP-80)),BottomQ(Q.X,Q.Y,FMath::Min(float(Q.Z)-80,HQ-80));
   const FVector TopP(P.X,P.Y,float(P.Z)),TopQ(Q.X,Q.Y,float(Q.Z));
   if(R.bBridge) RockGeometry.Quad(P,Q,P-FVector(0,0,120),Q-FVector(0,0,120));
   else {RockGeometry.Quad(TopP,TopQ,BottomP,BottomQ); RockGeometry.Quad(TopQ,TopP,BottomQ,BottomP);}
  }
 }
 // A few six-sided rock clusters add landmarks along slopes, with no new assets.
 FRandomStream RockRng(Layout.Seed+410);
 for(int32 I=0;I<35;++I)
 {
  const FVector2D XY(RockRng.FRandRange(Bounds.Min.X,Bounds.Max.X),RockRng.FRandRange(Bounds.Min.Y,Bounds.Max.Y));
  float RoadZ,Distance;
  if(!Layout.ContainsLand(XY) || !Network.FindSurfaceHeight(XY,RoadZ,Distance) || Distance<3500) continue;
  if(Layout.Houses.ContainsByPredicate([&](const auto& H){return FVector::Dist2D(FVector(XY,0),H.Position)<2200;})) continue;
  const float Radius=RockRng.FRandRange(200,600),Base=HeightAt(XY);
  const FVector Top(XY+FVector2D(Radius*.2f,0),Base+Radius*1.5f);
  for(int32 Face=0;Face<6;++Face)
  {
   const FVector A(XY+FVector2D(Radius,0).GetRotated(Face*60.f),Base-40),B(XY+FVector2D(Radius,0).GetRotated((Face+1)*60.f),Base-40);
   RockGeometry.Triangle(A,B,Top);
  }
 }
 WaterGeometry.Quad({-150000,-150000,0},{150000,-150000,0},{-150000,150000,0},{150000,150000,0});
 RoadGeometry.Apply(Roads,ECitixSurface::Asphalt);
 TerrainGeometry.Apply(Terrain,ECitixSurface::HillsideGrass);
 RockGeometry.Apply(Terrain,ECitixSurface::HillsideRock,true,1);
 DryGeometry.Apply(Terrain,ECitixSurface::HillsideGrassDry,true,2);
 TunnelGeometry.Apply(Tunnel,ECitixSurface::FacadeConcrete);
 WaterGeometry.Apply(Water,ECitixSurface::HillsideWater,false);
 BuildDetails(Layout);
}
