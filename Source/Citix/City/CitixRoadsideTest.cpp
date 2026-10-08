#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "City/CitixYienBillboard.h"
#include "City/CitixPropGenerator.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixAirFlowComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixRoadsideArtTest,"CitixChase.Roadside.CollisionAndBushArt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixRoadsideArtTest::RunTest(const FString& Parameters)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* Sign=World->SpawnActor<ACitixYienBillboard>(); Sign->Configure(0,false);
 TestTrue(TEXT("Billboard structure has physical collision"),Sign->Frame->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
 TestTrue(TEXT("Billboard structure blocks the car body"),Sign->Frame->GetCollisionResponseToChannel(ECC_PhysicsBody)==ECR_Block);
 const int64 BushValue=StaticEnum<ECitixSurface>()->GetValueByNameString(TEXT("Bush"));
 TestTrue(TEXT("Roadside bush is distinct from spherical foliage"),BushValue>=0);
 if (BushValue>=0) {
  const auto Surface=static_cast<ECitixSurface>(BushValue);
  auto* Mesh=FCitixSurfaceLibrary::GetMesh(Surface);
  TestTrue(TEXT("Bush uses its authored branching/leaf mesh"),Mesh && Mesh->GetPathName().Contains(TEXT("SM_CitixBush")));
  TestTrue(TEXT("Bush has detailed leaf geometry"),Mesh && Mesh->GetNumVertices(0)>300);
  FRandomStream Rng(1337); TArray<FCitixBoxInstance> Boxes; FCitixPropGenerator::AddPlanter(Boxes,FVector::ZeroVector,Rng);
  TestTrue(TEXT("Roadside planter is replaced by passable bushes"),Boxes.Num()>0 && Boxes[0].Surface==Surface);
  TestFalse(TEXT("Bush is not a solid obstacle"),FCitixSurfaceLibrary::Collides(Surface));
 }
 auto* Car=World->SpawnActor<ACitixVehiclePawn>();
 TestNotNull(TEXT("Player car includes an airflow effect"),Car->FindComponentByClass<UCitixAirFlowComponent>());
 World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixAirFlowTest,"CitixChase.FX.TopSpeedAirFlow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixAirFlowTest::RunTest(const FString& Parameters)
{
 TestEqual(TEXT("No airflow below ninety percent of top speed"),UCitixAirFlowComponent::StrengthForSpeed(179,200),0.f);
 TestTrue(TEXT("Airflow appears at ninety percent"),UCitixAirFlowComponent::StrengthForSpeed(180,200)>0.f);
 TestTrue(TEXT("Airflow builds toward full speed"),UCitixAirFlowComponent::StrengthForSpeed(190,200)>UCitixAirFlowComponent::StrengthForSpeed(180,200));
 TestEqual(TEXT("Boost cannot make airflow intensity unbounded"),UCitixAirFlowComponent::StrengthForSpeed(300,200),1.f);
 TestEqual(TEXT("Invalid speed cap cannot create an effect"),UCitixAirFlowComponent::StrengthForSpeed(200,0),0.f);
 return true;
}
#endif
