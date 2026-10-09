#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "City/CitixCityGenerator.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixGroundBoundaryTest,
 "CitixChase.City.VisibleGroundBoundary",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixGroundBoundaryTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* City=World->SpawnActor<ACitixCityGenerator>();
 City->SeedOverride=1337; City->bLogStats=false;
 City->bSpawnTraffic=false; City->bSpawnPedestrians=false; City->bSpawnStreetLights=false;
 City->GenerateCity();
 UStaticMeshComponent* Ground=nullptr;
 TArray<UStaticMeshComponent*> Meshes; City->GetComponents(Meshes);
 for(auto* Mesh:Meshes) if(Mesh->GetName()==TEXT("Ground")) { Ground=Mesh; break; }
 if(!TestNotNull(TEXT("The test uses the actual generated ground slab"),Ground)) { World->DestroyWorld(false); return false; }
 const auto& Plan=City->GetCityPlan();
 const FVector2D Half(230,95);
 const FVector Center=Ground->Bounds.Origin;
 const FVector Extent=Ground->Bounds.BoxExtent;
 // The corners are outside the city plan and safely away from the river.
 // A footprint whose outermost corner rests on the visible edge remains dry.
 for(float SX : {-1.f,1.f}) for(float SY : {-1.f,1.f}) {
  const FVector P(Center.X+SX*(Extent.X-Half.X-1),Center.Y+SY*(Extent.Y-Half.Y-1),100);
  TestTrue(TEXT("Visible ground apron beyond plan bounds is drivable"),City->IsDryFootprint(P,Half,0));
  FTransform Surface;
  TestTrue(TEXT("The actual slab supports a complete validated car footprint"),ACitixCityGenerator::ValidateChaseSurface(World,P,FVector(Half,60),0,nullptr,Surface,false));
  const FVector Outside=P+FVector(SX*3,0,0);
  TestFalse(TEXT("Crossing the actual slab edge remains unsafe"),City->IsDryFootprint(Outside,Half,0));
  TestFalse(TEXT("A rotated body must also fit on the slab"),City->IsDryFootprint(P,Half,45));
 }
 int32 WaterRejected=0,Bridges=0;
 for(const FVector2D& P:Plan.RiverPoints) WaterRejected+=!City->IsDryFootprint(FVector(P,0),Half,0);
 TestTrue(TEXT("The ground slab beneath the river does not make water drivable"),WaterRejected>0);
 const auto& Roads=City->GetRoadNetwork();
 for(const auto& Edge:Roads.Edges) if(Edge.bBridge && Edge.bDrivable) {
  const FVector A(Roads.Nodes[Edge.NodeA].Position,0),B(Roads.Nodes[Edge.NodeB].Position,0);
  TestTrue(TEXT("Existing bridge route stays dry and usable"),City->IsDryFootprint((A+B)*.5f,Half,(B-A).Rotation().Yaw));
  ++Bridges;
 }
 TestTrue(TEXT("The river preservation check includes authored bridge routes"),Bridges>0);
 World->DestroyWorld(false);
 return true;
}
#endif
