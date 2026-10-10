#include "City/CitixHillsideLayout.h"

FCitixRoadNetwork FCitixHillsideLayout::BuildRoadNetwork() const
{
 FCitixRoadNetwork Network; Network.CitySize=Settings.Footprint.GetMax();
 auto Node=[&](const FVector& P)
 {
  const int32 Existing=Network.Nodes.IndexOfByPredicate([&](const auto& N){return FVector(N.Position,N.Elevation).Equals(P,.01f);});
  if(Existing!=INDEX_NONE) return Existing;
  FCitixRoadNode N; N.Position=FVector2D(P); N.Elevation=P.Z; return Network.Nodes.Add(N);
 };
 for(const auto& Route:Routes) for(int32 I=1;I<Route.Points.Num();++I)
 {
  FCitixRoadEdge E; E.NodeA=Node(Route.Points[I-1]); E.NodeB=Node(Route.Points[I]);
  E.SurfaceWidth=E.CorridorWidth=Route.Width; E.RoadClass=ECitixRoadClass::Local; E.bBridge=Route.bBridge;
  if(E.NodeA!=E.NodeB) Network.Edges.Add(E);
 }
 Network.GeometryHash=HashCombine(GetTypeHash(Seed),GetTypeHash(Settings.PeakHeight));
 for(float V:{float(Settings.Footprint.X),float(Settings.Footprint.Y),Settings.TerrainSpacing,Settings.VegetationDensity,Settings.InitialHour,float(Settings.HouseCount)}) Network.GeometryHash=HashCombine(Network.GeometryHash,GetTypeHash(V));
 Network.RebuildAdjacency(); return Network;
}

FCitixHillsideLayout FCitixHillsideLayout::Build(int32 Seed)
{
 FCitixHillsideLayout L; L.Settings=UCitixCitySettings::Get().Hillside; L.Seed=Seed;
 L.Settings.RoadWidth=FMath::Clamp(L.Settings.RoadWidth,1300.f,1600.f);
 L.Settings.BendRadius=FMath::Clamp(L.Settings.BendRadius,2400.f,3000.f);
 L.Settings.TerrainSpacing=FMath::Clamp(L.Settings.TerrainSpacing,400.f,1200.f);
 L.Settings.HouseCount=FMath::Clamp(L.Settings.HouseCount,25,35);
 L.Settings.MaxGrade=FMath::Clamp(L.Settings.MaxGrade,.01f,.12f);
 auto Road=[&](const TCHAR* Name,std::initializer_list<FVector> Points,bool Tunnel=false,bool Bridge=false)
 {
  FCitixHillsideRoute R; R.Name=Name; R.Width=Tunnel ? 1600.f : L.Settings.RoadWidth; R.bTunnel=Tunnel; R.bBridge=Bridge;
  for(auto P:Points) R.Points.Add(P); L.Routes.Add(MoveTemp(R));
 };
 Road(TEXT("Coastal Bypass"),{{-32000,-23000,200},{-18000,-26000,200},{-5000,-25000,300},{10000,-24000,300},{24000,-23000,200},{32000,-24000,200}});
 Road(TEXT("West Terrace Approach"),{{-18000,-26000,200},{-17000,-16000,900},{-13000,4000,2400}});
 Road(TEXT("East Terrace Approach"),{{24000,-23000,200},{26000,-19000,500}});
 Road(TEXT("Coastal Ravine Bridge"),{{26000,-19000,500},{27000,-15000,850}},false,true);
 Road(TEXT("East Village Approach"),{{27000,-15000,850},{27000,-10000,1300},{24000,-4000,2000},{10000,-1000,3500}});
 Road(TEXT("Terrace Loop"),{{-13000,4000,2400},{-4000,-3000,2500},{10000,-1000,3500},{17000,0,4100},{16000,8000,4100},{4000,9000,3600},{-13000,4000,2400}});
 FCitixHillsideRoute Climb; Climb.Name=TEXT("Switchback Climb"); Climb.Width=L.Settings.RoadWidth;
 Climb.Points.Add({-13000,4000,2400});
 auto Hairpin=[&](FVector2D Centre,bool West,float StartHeight,float EndHeight)
 {
  const float Radius=L.Settings.BendRadius;
  for(int32 I=0;I<=36;++I)
  {
   const float T=float(I)/36,Angle=FMath::DegreesToRadians(-90.f+(West ? -180.f : 180.f)*T);
   Climb.Points.Add(FVector(Centre+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius,FMath::Lerp(StartHeight,EndHeight,T)));
  }
 };
 Hairpin({-24000,7000},true,3400,3600);
 Hairpin({0,13000},false,4500,4900);
 Hairpin({-22000,19000},true,5700,6100);
 Hairpin({2000,25000},false,6900,7300);
 Hairpin({-17000,31000},true,8100,8500);
 Climb.Points.Add({-2000,34000,9000}); L.Routes.Add(MoveTemp(Climb));
 Road(TEXT("Ridge Connector"),{{-2000,34000,9000},{8500,31000,8100},{18000,24000,6900},{24000,16000,5800},{24000,6000,4800},{16400,4800,4100}});
 Road(TEXT("Summit Service Road"),{{-2000,34000,9000},{1000,33500,9000}});

 Road(TEXT("Tunnel Bypass"),{{-17000,-16000,900},{27000,-10000,1300}},true);
 Road(TEXT("West Harbour Loop"),{{-32000,-23000,200},{-33000,-12000,1400},{-24000,-14000,1200},{-18000,-26000,200}});
 FCitixHillsideRoute Harbour; Harbour.Name=TEXT("East Harbour Loop"); Harbour.Width=L.Settings.RoadWidth;
 const FVector2D HarbourCentre(28000,-23500),HarbourRadial(4000,-500);
 for(int32 I=0;I<=36;++I) Harbour.Points.Add(FVector(HarbourCentre+HarbourRadial.GetRotated(-180.f*I/36.f),200));
 L.Routes.Add(MoveTemp(Harbour));
 Road(TEXT("Marina Access"),{{-18000,-26000,200},{-25500,-24000,200}});
 L.CoastBoundary={{-35000,-21000},{-33000,-25000},{-19000,-28000},{-6000,-27500},{8000,-26500},{21000,-25000},{24000,-29000},{31000,-29500},{34000,-25000},{35000,-16000},{33000,-2000},{34000,14000},{25000,30000},{10000,37000},{-11000,37500},{-28000,33000},{-34000,15000}};
 L.RelaySites={{-18000,-26000,200},{10000,-24000,300},{-13000,4000,2400},{10000,-1000,3500},{17000,0,4100},{-27000,7000,3500},{3000,13000,4700},{-25000,19000,5900},{5000,25000,7100},{-20000,31000,8300},{-2000,34000,9000},{24000,16000,5800},{-17000,-16000,900},{27000,-10000,1300}};
 L.Exits={{-32000,-23000,200},{32000,-24000,200}};
 L.Spawns={{-15000,-6000,1650},{17000,-2500,2750}};
 L.RecoveryParking={{-18000,-26000,200},{17000,0,4100},{-25000,19000,5900},{1000,33500,9000}};
 const FVector2D Scale=L.Settings.Footprint/FVector2D(70000,64000); const float ZScale=L.Settings.SummitRoadHeight/9000.f;
 auto ScalePoint=[&](FVector& P){P.X*=Scale.X; P.Y*=Scale.Y; P.Z=200+(P.Z-200)*ZScale;};
 for(auto& R:L.Routes) for(auto& P:R.Points) ScalePoint(P);
 for(auto* Sites:{&L.RelaySites,&L.Exits,&L.Spawns,&L.RecoveryParking}) for(auto& P:*Sites) ScalePoint(P);
 for(auto& P:L.CoastBoundary) P*=Scale;
 // Reuse circular fillets, keeping the five authored hairpins intact.
 const auto OriginalRoutes=L.Routes;
 for(auto& R:L.Routes)
 {
  if(R.bTunnel || R.bBridge || R.Name==TEXT("Switchback Climb") || R.Name==TEXT("East Harbour Loop")) continue;
  const auto Original=R.Points; TArray<FVector> Rounded={Original[0]};
  for(int32 I=1;I<Original.Num()-1;++I)
  {
   const FVector P=Original[I];
   const bool Shared=OriginalRoutes.ContainsByPredicate([&](const auto& Other){return Other.Name!=R.Name && Other.Points.ContainsByPredicate([&](const FVector& N){return N.Equals(P,.01f);});});
   const FVector2D Before(P-Original[I-1]),After(Original[I+1]-P),In=Before.GetSafeNormal(),Out=After.GetSafeNormal();
   const float Angle=FMath::Acos(FMath::Clamp(FVector2D::DotProduct(In,Out),-1.,1.));
   const float Radius=L.Settings.BendRadius,Setback=Radius*FMath::Tan(Angle*.5f);
   if(Shared || Angle<.01f || Angle>PI-.01f || Setback>FMath::Min(Before.Size(),After.Size())*.45f) {Rounded.Add(P); continue;}
   const FVector Start=FMath::Lerp(P,Original[I-1],Setback/Before.Size()),End=FMath::Lerp(P,Original[I+1],Setback/After.Size());
   const float Sign=In.X*Out.Y-In.Y*Out.X>=0 ? 1.f : -1.f;
   const FVector2D Centre=FVector2D(Start)+FVector2D(-In.Y,In.X)*(Sign*Radius),Radial=FVector2D(Start)-Centre;
   const int32 Steps=FMath::Max(2,FMath::CeilToInt(FMath::RadiansToDegrees(Angle)/5.f));
   for(int32 Step=0;Step<=Steps;++Step) {const float T=float(Step)/Steps; Rounded.Add(FVector(Centre+Radial.GetRotated(FMath::RadiansToDegrees(Angle)*Sign*T),FMath::Lerp(Start.Z,End.Z,T)));}
  }
  Rounded.Add(Original.Last()); R.Points=MoveTemp(Rounded);
 }
 // Join the ridge onto the straight village stretch, preserving the loop's fillets.
 const FVector RidgeJunction=L.Routes.FindByPredicate([](const auto& R){return R.Name==TEXT("Ridge Connector");})->Points.Last();
 auto& Village=*L.Routes.FindByPredicate([](const auto& R){return R.Name==TEXT("Terrace Loop");});
 for(int32 I=1;I<Village.Points.Num();++I) if(FMath::PointDistToSegment(RidgeJunction,Village.Points[I-1],Village.Points[I])<1.f) {Village.Points.Insert(RidgeJunction,I); break;}
 // Shared junctions have short level landings. All connected road arms use the
 // same elevation there, avoiding crossing grade planes and their vertical lips.
 const auto JunctionGraph=L.BuildRoadNetwork();
 auto AtJunction=[&](FVector P){for(int32 N=0;N<JunctionGraph.Nodes.Num();++N) if(JunctionGraph.GetNodeDegree(N)>=3 && FVector(JunctionGraph.Nodes[N].Position,JunctionGraph.Nodes[N].Elevation).Equals(P,.01f)) return true; return false;};
 for(auto& R:L.Routes)
 {
  TArray<FVector> Landings={R.Points[0]};
  for(int32 I=1;I<R.Points.Num();++I)
  {
   const FVector A=R.Points[I-1],B=R.Points[I],D=B-A; const float Run=FVector::Dist2D(A,B);
   const bool Start=AtJunction(A),End=AtJunction(B); const int32 Count=int32(Start)+int32(End);
   const float Available=FMath::Max(0.f,Run-FMath::Abs(float(D.Z))/L.Settings.MaxGrade-5.f);
   const float Flat=Count && FMath::Abs(D.Z)>.01f ? FMath::Min(3000.f,Available/Count) : 0.f;
   if(Flat>10 && Start) {FVector P=A+D*(Flat/Run); P.Z=A.Z; Landings.Add(P);}
   if(Flat>10 && End) {FVector P=B-D*(Flat/Run); P.Z=B.Z; Landings.Add(P);}
   Landings.Add(B);
  }
  R.Points=MoveTemp(Landings);
 }
 for(auto* Sites:{&L.RelaySites,&L.RecoveryParking}) for(auto& Site:*Sites)
 {
  float Best=MAX_flt; FVector Nearest=Site;
  for(const auto& R:L.Routes) for(const auto& P:R.Points) {const float D=FVector::DistSquared(P,Site); if(D<Best) {Best=D; Nearest=P;}}
  Site=Nearest;
 }
 L.Landmarks={FVector(-1500,-6000,2700),FVector(3000,36000,9000),FVector(-27000,-26000,200)};
 for(auto& P:L.Landmarks) ScalePoint(P);
 // Small, road-facing terraces follow three neighborhoods instead of a town grid.
 FRandomStream Rng(Seed);
 const auto Network=L.BuildRoadNetwork();
 TMap<FString,int32> HouseCounts;
 for(int32 Pass=0;Pass<3 && L.Houses.Num()<L.Settings.HouseCount;++Pass)
 for(const auto& R:L.Routes)
 {
  if(R.Name!=TEXT("Terrace Loop") && R.Name!=TEXT("Ridge Connector") && R.Name!=TEXT("Coastal Bypass")) continue;
  const int32 Quota=FMath::RoundToInt(L.Settings.HouseCount*(R.Name==TEXT("Coastal Bypass") ? .2f : R.Name==TEXT("Terrace Loop") ? .5f : .3f));
  for(int32 I=1;I<R.Points.Num() && L.Houses.Num()<L.Settings.HouseCount && HouseCounts.FindOrAdd(R.Name)<Quota;++I)
  {
   const FVector A=R.Points[I-1],D=R.Points[I]-A,Dir=D.GetSafeNormal(),Right(-Dir.Y,Dir.X,0);
   const float Length=D.Size();
   for(float S=Rng.FRandRange(1400,2200)+Pass*950;S<Length-1400 && L.Houses.Num()<L.Settings.HouseCount && HouseCounts.FindOrAdd(R.Name)<Quota;S+=Rng.FRandRange(2700,4300))
   {
    const FVector RoadPoint=A+Dir*S;
    const float Side=Pass==1 ? -1.f : 1.f;
    FCitixHillsideHouse House; House.Position=RoadPoint+Right*(Side*(R.Width*.5f+Rng.FRandRange(1400,2400)));
    House.Position.Z=RoadPoint.Z+20; House.Size=FVector2D(Rng.FRandRange(1200,1800),Rng.FRandRange(900,1250)); House.Yaw=Dir.Rotation().Yaw+Rng.FRandRange(-8,8);
    float Height,Distance;
    if(!L.ContainsLand(FVector2D(House.Position)) || !Network.FindSurfaceHeight(FVector2D(House.Position),Height,Distance) || Distance<1450) continue;
    if(L.Houses.ContainsByPredicate([&](const auto& H){return FVector::Dist2D(H.Position,House.Position)<2600;})) continue;
    if(L.RelaySites.ContainsByPredicate([&](const auto& P){return FVector::Dist2D(P,House.Position)<2400;})) continue;
    if(L.Landmarks.ContainsByPredicate([&](const auto& P){return FVector::Dist2D(P,House.Position)<3500;})) continue;
    L.Houses.Add(House); ++HouseCounts.FindOrAdd(R.Name);
   }
  }
 }
 return L;
}

bool FCitixHillsideLayout::ContainsLand(FVector2D P) const
{
 bool Inside=false;
 for(int32 I=0,J=CoastBoundary.Num()-1;I<CoastBoundary.Num();J=I++)
 {
  const auto A=CoastBoundary[I],B=CoastBoundary[J];
  if((A.Y>P.Y)!=(B.Y>P.Y) && P.X<(B.X-A.X)*(P.Y-A.Y)/(B.Y-A.Y)+A.X) Inside=!Inside;
 }
 return Inside;
}

float FCitixHillsideLayout::BaseTerrainHeight(FVector2D P) const
{
 P/=Settings.Footprint/FVector2D(70000,64000);
 auto Ridge=[&](FVector2D Centre,FVector2D Extent,float Height){const FVector2D D=(P-Centre)/Extent; return Height*FMath::Exp(-float(D.SizeSquared()));};
 const float Mountain=FMath::Max3(Ridge({-3000,37000},{26000,26000},Settings.PeakHeight),Ridge({-21000,20000},{18000,18000},4600),Ridge({24000,14000},{15000,20000},7600));
 const float Coast=FMath::SmoothStep(-27000.f,-11000.f,float(P.Y));
 const float Variation=160.f*FMath::Sin(float(P.X)/7100.f+Seed*.001f)*FMath::Sin(float(P.Y)/9300.f);
 const float Ravine=FMath::Exp(-FMath::Square(float(P.X-26500)/1800.f)-FMath::Square(float(P.Y+17000)/3200.f));
 float Height=FMath::Lerp(FMath::Max(Mountain*Coast,Ridge({0,-12000},{30000,6500},3500))+Variation*Coast,-120.f,Ravine*.95f);
 const FVector2D WorldXY=P*(Settings.Footprint/FVector2D(70000,64000));
 auto Terrace=[&](FVector Centre,FVector2D Size,float Yaw)
 {
  const FVector2D Local=(WorldXY-FVector2D(Centre)).GetRotated(-Yaw);
  const float Outside=FMath::Max(FMath::Abs(float(Local.X))-float(Size.X)*.5f,FMath::Abs(float(Local.Y))-float(Size.Y)*.5f);
  if(Outside<1000) Height=FMath::Lerp(Height,float(Centre.Z),1.f-FMath::SmoothStep(100.f,1000.f,Outside));
 };
 for(const auto& House:Houses) Terrace(House.Position,House.Size,House.Yaw);
 for(const auto& Landmark:Landmarks) Terrace(Landmark,FVector2D(2200,1800),0);
 return Height;
}
