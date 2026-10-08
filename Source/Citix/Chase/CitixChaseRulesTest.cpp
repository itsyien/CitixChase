#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/CitixGraphicsSettings.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixSmokeCloud.h"
#include "Engine/StaticMesh.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixGateLayout.h"
#include "Player/CitixDrivingHUD.h"
#include "Sandbox/CitixRouteHelper.h"
#include "World/CitixTimeOfDay.h"
#include "City/CitixCityGenerator.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Sandbox/CitixRouteGuide.h"
#include "Sandbox/CitixDestinationBeacon.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Materials/MaterialInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixChaseRulesTest, "CitixChase.Rules.RoundRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCitixChaseRulesTest::RunTest(const FString& Parameters)
{
 TestEqual(TEXT("Weak GPU chooses low"),UCitixGraphicsSettings::Recommend(0,3),0);
 TestEqual(TEXT("Weak CPU chooses low"),UCitixGraphicsSettings::Recommend(3,0),0);
 TestEqual(TEXT("Strong hardware preserves maximum graphics"),UCitixGraphicsSettings::Recommend(3,3),3);
 TestEqual(TEXT("Low renders fifty percent with native UI"),UCitixGraphicsSettings::ResolutionForPreset(0),50.f);
 TestEqual(TEXT("Medium renders eighty-five percent"),UCitixGraphicsSettings::ResolutionForPreset(1),85.f);
 TestEqual(TEXT("Maximum retains full resolution"),UCitixGraphicsSettings::ResolutionForPreset(3),100.f);
 TestEqual(TEXT("Briefing countdown lasts ten seconds"),FCitixChaseRules::CountdownSeconds,10.f);
 TestEqual(TEXT("Smoke density gains 150 percent"),ACitixSmokeCloud::PuffCount,100);
 TestEqual(TEXT("Smoke spreads fifty percent farther along the ground"),ACitixSmokeCloud::HorizontalSpread,1.5f);
 TestEqual(TEXT("Smoke diameter increases thirty percent"),ACitixSmokeCloud::DiameterScale,1.3f);
 TestFalse(TEXT("First wreck allows recovery"),FCitixChaseRules::IsFinalWreck(false,50.f));
 TestTrue(TEXT("Used reserve makes destruction final even if health is unexpectedly restored"),FCitixChaseRules::IsFinalWreck(true,50.f));
 TestTrue(TEXT("Zero health ends immediately"),FCitixChaseRules::IsFinalWreck(false,0.f));
 auto* Smoke=NewObject<ACitixSmokeCloud>();
 TestEqual(TEXT("Skill smoke uses faceted low-poly billows"), (Smoke->Billows->GetStaticMesh() ? Smoke->Billows->GetStaticMesh()->GetName() : FString(TEXT("missing"))), FString(TEXT("SM_CitixSmokeBillow")));
 if (Smoke->Billows->GetStaticMesh()) TestEqual(TEXT("Smoke billow has only twenty triangles"),Smoke->Billows->GetStaticMesh()->GetNumTriangles(0),20);
 TestEqual(TEXT("Gate spacing is two hundred metres"),FCitixChaseRules::GateSpacing,20000.f);
 auto* Agile=NewObject<ACitixVehiclePawn>(); Agile->ApplyChasePerformance(true);
 TestTrue(TEXT("Runner normal steering has stronger authority"),Agile->GetVehicleMovement()->NormalSteerAuthority>=.95f);
	TestFalse(TEXT("Exactly 40 km/h is not a lethal run-over"), FCitixChaseRules::IsLethalRunOver(40.f, false));
	TestTrue(TEXT("Over 40 km/h kills an unprotected on-foot runner"), FCitixChaseRules::IsLethalRunOver(40.1f, false));
	TestFalse(TEXT("Shockwave recovery protects from lethal run-over"), FCitixChaseRules::IsLethalRunOver(80.f, true));
	TestEqual(TEXT("Non-chase sandbox clock retains its legacy default"), GetDefault<ACitixTimeOfDay>()->DayLengthMinutes, 3.f);
	ACitixVehiclePawn* Car = NewObject<ACitixVehiclePawn>();
	Car->GetVehicleMovement()->RepairFull(); // NewObject does not run the spawned vehicle BeginPlay initialization.
	const FCitixCarPerformance Baseline = FCitixCarLibrary::GetPerformance(Car->GetCarType());
	Car->ApplyChasePerformance(true);
 TestTrue(TEXT("Chase drift has stronger steering authority"),Car->GetVehicleMovement()->DriftSteerAuthority>1.1f);
 TestTrue(TEXT("Chase drift counters forward slide"),Car->GetVehicleMovement()->DriftSlideAssistScale>=.6f);
 TestEqual(TEXT("Runner vehicle health is normalized"),Car->GetVehicleMovement()->GetMaxHealth(),100.f);
 Car->GetVehicleMovement()->ApplyDamage(25); Car->ApplyChasePerformance(true);
 TestEqual(TEXT("Role tuning preserves damage"),Car->GetVehicleMovement()->GetHealth(),75.f);
	TestEqual(TEXT("Runner engine force gains twenty percent"), Car->GetVehicleMovement()->MaxEngineForce, Baseline.EngineForce * (2.5f * 1.2f));
	Car->ApplyChasePerformance(false);
	Car->GetVehicleMovement()->ReconcileBoostCharge(2.f);
	TestEqual(TEXT("Boost reconciliation clamps the server reserve"), Car->GetVehicleMovement()->GetBoostCharge(), 1.f);
	Car->GetVehicleMovement()->ReconcileBoostCharge(.4f);
	TestEqual(TEXT("Guest recovers the authoritative parked reserve"), Car->GetVehicleMovement()->GetBoostCharge(), .4f);
	TestEqual(TEXT("Chaser has an absolute 292.5 km/h ceiling"), Car->GetVehicleMovement()->MaxSpeed, 292.5f / .036f);
	TestEqual(TEXT("Chaser engine force increased thirty percent"), Car->GetVehicleMovement()->MaxEngineForce, Baseline.EngineForce * 1.3f);
	TestEqual(TEXT("Chaser engine force increased thirty percent"), Car->GetVehicleMovement()->MaxEngineForce, Baseline.EngineForce * 1.3f);
	TestEqual(TEXT("Four 25-point rams wreck a 100-point runner car"), FCitixChaseRules::RamsToWreck(100.f), 4);
	TestEqual(TEXT("One wreck leaves the runner at 50 health"), FCitixChaseRules::HealthAfterWreck(100.f), 50.f);
	TestEqual(TEXT("A second wreck reaches zero health"), FCitixChaseRules::HealthAfterWreck(50.f), 0.f);
	TestFalse(TEXT("A 14.9 km/h closing speed is rejected"), FCitixChaseRules::IsValidRam(14.9f, 2.f));
	TestTrue(TEXT("A 15 km/h closing speed is a valid ram"), FCitixChaseRules::IsValidRam(15.f, 2.f));
	TestFalse(TEXT("A 1.49 second ram interval is rejected"), FCitixChaseRules::IsValidRam(60.f, 1.49f));
	TestTrue(TEXT("A 1.5 second ram interval is valid after separation"), FCitixChaseRules::IsValidRam(60.f, 1.5f));
	TestFalse(TEXT("Repeated contact before separation is rejected"), FCitixChaseRules::IsValidRam(60.f, 1.f));
	TestFalse(TEXT("Contact held through the cooldown is rejected"), FCitixChaseRules::IsValidRam(60.f, 2.f, false));
	TestFalse(TEXT("Four relays keep exits locked"),FCitixChaseRules::AreExitsUnlocked(4));
 TestTrue(TEXT("Five completed relays unlock exits"),FCitixChaseRules::AreExitsUnlocked(5));
 const float GateStart=10.f, GateEnd=GateStart+FCitixChaseRules::ChaserGateDragDuration;
 TestEqual(TEXT("Chaser gate drag lasts 1.2 seconds"), FCitixChaseRules::ChaserGateDragDuration,1.2f);
 TestEqual(TEXT("Gate slowdown starts without a speed jump"),FCitixChaseRules::GateSlowScale(GateStart,GateStart,GateEnd),1.f);
 TestTrue(TEXT("Gate slowdown ramps gradually"),FCitixChaseRules::GateSlowScale(10.35f,GateStart,GateEnd)>.7f && FCitixChaseRules::GateSlowScale(10.35f,GateStart,GateEnd)<1.f);
 TestTrue(TEXT("Gate reaches thirty percent slowdown"),FMath::IsNearlyEqual(FCitixChaseRules::GateSlowScale(10.8f,GateStart,GateEnd),.7f));
 TestTrue(TEXT("Gate releases smoothly before expiry"),FCitixChaseRules::GateSlowScale(11.1f,GateStart,GateEnd)>.7f);
 TestEqual(TEXT("Gate slowdown expires at 1.2 seconds"),FCitixChaseRules::GateSlowScale(GateEnd,GateStart,GateEnd),1.f);
 TestEqual(TEXT("Relay interaction radius increased by 200 percent"),FCitixChaseRules::RelayInteractionRadius,1350.f);
 auto* Gate=NewObject<ACitixDestinationBeacon>(); Gate->SetBreakawayStation(FVector::ZeroVector,0);
 TArray<UInstancedStaticMeshComponent*> GateParts; Gate->GetComponents(GateParts);
 for (auto* Parts:GateParts) if (Parts->GetName()==TEXT("GateEdges")) {
  TestTrue(TEXT("Two physical gate edges"),Parts->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics && Parts->GetInstanceCount()==2);
 } else if (Parts->GetName()==TEXT("StationFrame")) {
  TestTrue(TEXT("Overhead frame cannot become a false road surface"),Parts->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
  FTransform Bar; Parts->GetInstanceTransform(4,Bar);
  TestTrue(TEXT("Gate spans a full street instead of a narrow opening"),Bar.GetScale3D().Y*100.f>=1200.f);
 }
 ACitixRouteGuide* Guide=NewObject<ACitixRouteGuide>();
 Guide->UpdateRoute({FVector::ZeroVector,FVector(3000,0,0)},ECitixSurface::EmissiveCool);
 auto* Markers=Guide->FindComponentByClass<UInstancedStaticMeshComponent>();
 TestEqual(TEXT("One arrow has two arms"),Markers->GetInstanceCount(),2);
 TestTrue(TEXT("Arrow uses the emissive shader rather than the default fallback"),Markers->GetMaterial(0)->GetMaterial()==FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool)->GetMaterial());
 for (int32 I=0; I<2; ++I) {
  FTransform Arm; Markers->GetInstanceTransform(I,Arm);
  TestTrue(TEXT("Arrow arms end at the shared tip without crossing"),Arm.TransformPosition(FVector(50,0,0)).Equals(FVector(1500,0,40),.01f));
 }
	TestTrue(TEXT("Pistol is available to an on-foot chaser in pursuit"), FCitixChaseRules::CanFirePistol(true, true, true));
	TestFalse(TEXT("Runner cannot fire pistol"), FCitixChaseRules::CanFirePistol(false, true, true));
	TestFalse(TEXT("Chaser cannot fire in a vehicle"), FCitixChaseRules::CanFirePistol(true, false, true));
	ACitixChaseGameMode* ChaseGameMode = NewObject<ACitixChaseGameMode>();
	TestTrue(TEXT("Chase mode selects the chase HUD"), ChaseGameMode->HUDClass == ACitixDrivingHUD::StaticClass());
	FCitixRoadNetwork LayoutRoads;
	LayoutRoads.Nodes = { { FVector2D(0.f, 0.f) }, { FVector2D(1000.f, 0.f) }, { FVector2D(2000.f, 0.f) } };
	LayoutRoads.Edges = { { 0, 1, ECitixRoadClass::Local, 1000.f, true }, { 1, 2, ECitixRoadClass::Local, 1000.f, false } };
	LayoutRoads.RebuildAdjacency();
	TArray<int32> LayoutPath;
	TestTrue(TEXT("Drivable layout route is found"), FCitixRouteHelper::FindRouteNodes(LayoutRoads, FVector::ZeroVector, FVector(900.f, 0.f, 0.f), LayoutPath));
	TestFalse(TEXT("Layout routing rejects a non-drivable edge"), FCitixRouteHelper::FindRouteNodes(LayoutRoads, FVector::ZeroVector, FVector(2000.f, 0.f, 0.f), LayoutPath));
 TestTrue(TEXT("Runner ceiling gains twenty percent to 228 km/h"),FMath::IsNearlyEqual(FCitixChaseRules::SpeedLimit(true)*.036f,228.f,.001f));
 TestTrue(TEXT("Runner low-speed force gains twenty percent"),FMath::IsNearlyEqual(FCitixChaseRules::EngineScale(true,94.f),3.f));
 TestTrue(TEXT("Runner high-speed force gains twenty percent"),FMath::IsNearlyEqual(FCitixChaseRules::EngineScale(true,180.f),.78f,.0001f));
 TestTrue(TEXT("Engine taper is smooth"), FCitixChaseRules::EngineScale(true,132.f) > .78f && FCitixChaseRules::EngineScale(true,132.f) < 3.f);
 TestEqual(TEXT("Runner walks fifty percent faster than chaser"),FCitixChaseRules::OnFootSpeedScale(true,false),FCitixChaseRules::OnFootSpeedScale(false,false)*1.5f);
 TestEqual(TEXT("Runner sprints fifty percent faster than chaser"),FCitixChaseRules::OnFootSpeedScale(true,true),FCitixChaseRules::OnFootSpeedScale(false,true)*1.5f);
 TestEqual(TEXT("Chaser walking unchanged"),FCitixChaseRules::OnFootSpeedScale(false,false),1.f);
 TestEqual(TEXT("Chaser sprint unchanged"),FCitixChaseRules::OnFootSpeedScale(false,true),1.1f);
 const FVector Capped = FCitixChaseRules::LimitVelocity(FVector(10000,10000,700), FCitixChaseRules::SpeedLimit(true));
 TestTrue(TEXT("Horizontal cap preserves vertical motion"), FMath::IsNearlyEqual(Capped.Size2D(), FCitixChaseRules::SpeedLimit(true),.01f) && Capped.Z == 700.f);
 TestFalse(TEXT("No replacement before forty seconds"), FCitixChaseRules::ReplacementReady(39.99f,40.f,false,0,300));
 TestTrue(TEXT("Replacement boundary"), FCitixChaseRules::ReplacementReady(40.f,40.f,false,9.99f,300));
 TestFalse(TEXT("Ten km/h car rejected"), FCitixChaseRules::ReplacementReady(40.f,40.f,false,10.f,300));
 TestFalse(TEXT("Used reserve rejected"), FCitixChaseRules::ReplacementReady(40.f,40.f,true,0,300));
 { FCitixRoadNetwork Cross; Cross.Nodes.SetNum(5); Cross.Edges.SetNum(4); TArray<FCitixGateCandidate> Choices;
 for (int32 I=0; I<4; ++I) { const FVector2D D=I==0 ? FVector2D(1,0) : I==1 ? FVector2D(-1,0) : I==2 ? FVector2D(0,1) : FVector2D(0,-1); Cross.Nodes[I+1].Position=D*10000.f; Cross.Edges[I].NodeA=0; Cross.Edges[I].NodeB=I+1; Choices.Add({FTransform(FVector(D*5000.f,0)),I,5000,1200}); }
 TestEqual(TEXT("Four junction arms select at most one nearby gate"),FCitixGateLayout::Select(Cross,Choices).Num(),1);
 FCitixRoadNetwork Line; Line.Nodes.SetNum(2); Line.Nodes[1].Position=FVector2D(100000,0); Line.Edges.SetNum(1); Line.Edges[0].NodeA=0; Line.Edges[0].NodeB=1;
 Choices.Reset(); for (int32 I=0; I<20; ++I) Choices.Add({FTransform(FVector(I*5000.f+2500,0,0)),0,I*5000.f+2500,1200});
 const auto Picks=FCitixGateLayout::Select(Line,Choices);
 for (int32 A=0; A<Picks.Num(); ++A) for (int32 B=A+1; B<Picks.Num(); ++B) TestTrue(TEXT("No pair closer than two hundred metres along a road"),FMath::Abs(Choices[Picks[A]].Along-Choices[Picks[B]].Along)>=19999.f);
 }
 TestTrue(TEXT("Runner retains high speed steering lock"),Agile->GetVehicleMovement()->HighSpeedSteerFraction>=.8f);
 Agile->ApplyChasePerformance(false); TestEqual(TEXT("Chaser high speed steering unchanged"),Agile->GetVehicleMovement()->HighSpeedSteerFraction,.55f);
 { int32 Charges=1; float Next=60.f;
 FCitixChaseRules::RefillSmoke(59.99f,Charges,Next); TestEqual(TEXT("Smoke has no early charge"),Charges,1);
 FCitixChaseRules::RefillSmoke(60.f,Charges,Next); TestEqual(TEXT("Sixty seconds restores one charge"),Charges,2); TestEqual(TEXT("Full smoke cap clears refill"),Next,0.f);
 --Charges; FCitixChaseRules::RefillSmoke(61.f,Charges,Next); TestEqual(TEXT("Using from full starts a fresh minute"),Next,121.f);
 --Charges; FCitixChaseRules::RefillSmoke(65.f,Charges,Next); TestEqual(TEXT("Extra use does not restart refill"),Next,121.f);
 FCitixChaseRules::RefillSmoke(181.f,Charges,Next); TestEqual(TEXT("Delayed ticks cannot exceed two charges"),Charges,2);
 }
 int32 Ammo = 14; float Next = 8;
 FCitixChaseRules::RefillAmmo(7.99f,Ammo,Next); TestEqual(TEXT("No early refill"),Ammo,14);
 --Ammo; FCitixChaseRules::RefillAmmo(8.f,Ammo,Next); TestEqual(TEXT("Shot did not restart refill"),Ammo,14);
 FCitixChaseRules::RefillAmmo(16.f,Ammo,Next); TestEqual(TEXT("Refill reaches capacity"),Ammo,15); TestEqual(TEXT("Full reserve clears timer"),Next,0.f);
 TestEqual(TEXT("Starting hour retained"),FCitixChaseRules::SceneHour(0.f),19.f);
 TestEqual(TEXT("Night starts after remaining dusk"),FCitixChaseRules::SceneHour(11.25f),19.5f);
 TestEqual(TEXT("Night lasts 101.25 seconds"),FCitixChaseRules::SceneHour(112.5f),4.5f);
 TestEqual(TEXT("Dawn lasts 67.5 seconds"),FCitixChaseRules::SceneHour(180.f),7.5f);
 TestEqual(TEXT("Day lasts 33.75 seconds"),FCitixChaseRules::SceneHour(213.75f),16.5f);
 TestEqual(TEXT("270 second cycle"),FCitixChaseRules::SceneHour(270.f),19.f);
 TArray<FVector> DirectRoute;
 FCitixRouteHelper::BuildRoutePoints(LayoutRoads,FVector(400,0,60),FVector(700,0,60),DirectRoute);
 bool Direct=true; for (const FVector& Point:DirectRoute) Direct &= Point.X>=400 && Point.X<=700;
 TestTrue(TEXT("Same-road guidance does not route behind the driver"),Direct);
 { FCitixTrafficSnapshot A,B; A.Timestamp=1; B.Timestamp=1.1f; B.Location=FVector(100,0,0); B.Velocity=FVector(1000,0,0);
 const TArray<FCitixTrafficSnapshot> Samples={A,B};
 TestTrue(TEXT("Traffic interpolates timestamped midpoint"),FMath::IsNearlyEqual(ACitixTrafficVehicle::SampleMotion(Samples,1.05f).Location.X,50.f,.01f));
 TestTrue(TEXT("Traffic extrapolation holds after 100 ms"),ACitixTrafficVehicle::SampleMotion(Samples,2.f).Location.Equals(FVector(200,0,0),.01f));
 ACitixTrafficVehicle* Traffic=NewObject<ACitixTrafficVehicle>(); Traffic->History={A,B}; Traffic->Motion=B; ++Traffic->Motion.Generation; Traffic->OnRep_Motion();
 TestEqual(TEXT("Recycled traffic clears history"),Traffic->History.Num(),1);
 Traffic->History={A,B}; Traffic->Motion.Location=FVector(3000,0,0); Traffic->OnRep_Motion(); TestEqual(TEXT("Teleported traffic clears history"),Traffic->History.Num(),1);
 }
 for (int32 Seed:{7,1337,2026}) {
  ACitixCityGenerator* City=NewObject<ACitixCityGenerator>(); City->SeedOverride=Seed; City->bShowPlanningPreview=true; City->bLogStats=false; City->GenerateCity();
  const auto& Plan=City->GetCityPlan(); const auto& Roads=City->GetRoadNetwork(); int32 WaterRejected=0,DryRoads=0,Bridges=0;
  for (const FVector2D& P:Plan.RiverPoints) WaterRejected+=!City->IsDryFootprint(FVector(P,0),FVector2D(230,95),0);
  for (const auto& E:Roads.Edges) if (E.bDrivable) { const FVector A(Roads.Nodes[E.NodeA].Position,0),B(Roads.Nodes[E.NodeB].Position,0); const bool Dry=City->IsDryFootprint((A+B)*.5f,FVector2D(230,95),(B-A).Rotation().Yaw); DryRoads+=Dry; if (E.bBridge) { ++Bridges; TestTrue(FString::Printf(TEXT("Seed %d bridge remains usable"),Seed),Dry); } }
  TestTrue(FString::Printf(TEXT("Seed %d rejects river and retains roads/bridges"),Seed),WaterRejected>0 && DryRoads>0 && Bridges>0);
 }
 return true;
}
#endif


