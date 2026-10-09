#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "City/CitixYienBillboard.h"
#include "City/CitixPropGenerator.h"
#include "City/CitixCityChunk.h"
#include "Engine/OverlapResult.h"
#include "Core/CitixGraphicsSettings.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
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
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Sign=World->SpawnActor<ACitixYienBillboard>(); Sign->Configure(0,false);
 TestTrue(TEXT("Billboard structure has physical collision"),Sign->Frame->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
 TestTrue(TEXT("Billboard structure blocks the car body"),Sign->Frame->GetCollisionResponseToChannel(ECC_PhysicsBody)==ECR_Block);
 const int64 BushValue=StaticEnum<ECitixSurface>()->GetValueByNameString(TEXT("Bush"));
 TestTrue(TEXT("Roadside bush is distinct from spherical foliage"),BushValue>=0);
 if (BushValue>=0) {
  const auto Surface=static_cast<ECitixSurface>(BushValue);
  auto* Mesh=FCitixSurfaceLibrary::GetMesh(Surface);
  TestTrue(TEXT("Bush uses its authored clustered low-poly mesh"),Mesh && Mesh->GetPathName().Contains(TEXT("SM_CitixBush")));
  TestTrue(TEXT("Bush has multiple faceted lobes"),Mesh && Mesh->GetNumVertices(0)>300);
  TestTrue(TEXT("Bush stays within a low-poly triangle budget"),Mesh && Mesh->GetNumTriangles(0)<=300);
  FRandomStream Rng(1337); TArray<FCitixBoxInstance> Boxes; FCitixPropGenerator::AddPlanter(Boxes,FVector::ZeroVector,Rng);
  TestTrue(TEXT("Roadside planter is replaced by passable bushes"),Boxes.Num()>0 && Boxes[0].Surface==Surface);
  TestFalse(TEXT("Bush is not a solid obstacle"),FCitixSurfaceLibrary::Collides(Surface));
 }
 auto* Chunk=World->SpawnActor<ACitixCityChunk>(); Chunk->Init(FIntPoint(0,0),10000);
 Chunk->QueueBox(ECitixSurface::Bush,FVector(1500,0,75),FVector(300,170,150)); Chunk->Finish();
 auto* Scene=World->GetPhysicsScene(); const FVector Gravity(0,0,-980);
 Scene->SetUpForFrame(&Gravity,1.f/60,0,.1f,1.f/120,16,true); Scene->StartFrame(); Scene->WaitPhysScenes(); Scene->EndFrame();
 FHitResult Hit;
 TestTrue(TEXT("Ray hits the billboard support geometry"),World->LineTraceSingleByChannel(Hit,FVector(200,490,-670),FVector(-200,490,-670),ECC_PhysicsBody));
 TestFalse(TEXT("Open space between billboard supports remains passable"),World->LineTraceSingleByChannel(Hit,FVector(200,0,-670),FVector(-200,0,-670),ECC_PhysicsBody));
 TArray<FOverlapResult> Overlaps;
 World->OverlapMultiByObjectType(Overlaps,FVector(1500,0,75),FQuat::Identity,FCollisionObjectQueryParams(ECC_GameTraceChannel2),FCollisionShape::MakeBox(FVector(50,50,50)));
 TestTrue(TEXT("Production bush instance exposes its slowdown query shape"),Overlaps.ContainsByPredicate([](const FOverlapResult& Overlap){return Overlap.GetComponent() && Overlap.GetComponent()->ComponentHasTag(TEXT("CitixBush"));}));
 TestFalse(TEXT("Production bush does not block car body traces"),World->LineTraceSingleByChannel(Hit,FVector(1800,0,75),FVector(1200,0,75),ECC_PhysicsBody));
 auto* Car=World->SpawnActor<ACitixVehiclePawn>();
 TestNotNull(TEXT("Player car includes an airflow effect"),Car->FindComponentByClass<UCitixAirFlowComponent>());
 World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixBushPresetDistanceTest,"CitixChase.Roadside.PresetDrawDistance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixBushPresetDistanceTest::RunTest(const FString&) {
 auto* World=UWorld::CreateWorld(EWorldType::Game,false); auto* Chunk=World->SpawnActor<ACitixCityChunk>(); Chunk->Init(FIntPoint(0,0),10000); Chunk->QueueBox(ECitixSurface::Bush,FVector(0,0,75),FVector(300,170,150)); Chunk->Finish();
 auto* Bush=Chunk->FindComponentByClass<UHierarchicalInstancedStaticMeshComponent>(); auto* Settings=NewObject<UCitixGraphicsSettings>(); Settings->bHardwareDetected=true;
 const float Expected[]={30000,50000,85000,140000};
 for(int32 Level=0;Level<4;++Level) {
  Settings->SelectPreset(Level); const float Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"))->GetFloat();
  TestTrue(TEXT("Existing city bush updates to selected preset distance"),Bush && FMath::IsNearlyEqual(Bush->InstanceEndCullDistance*Scale,Expected[Level],2.f));
 }
 Settings->SelectPreset(0); const float Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"))->GetFloat();
 TestTrue(TEXT("Switching back to Low restores the 200m minimum detail range"),Bush && FMath::IsNearlyEqual(Bush->InstanceEndCullDistance*Scale,30000.f,2.f));
 Settings->SelectPreset(3); World->DestroyWorld(false); return true;
}
// Hosting replaces the initial city world and garbage-collects its components.
// The process-wide mesh cache must keep its asset alive across that boundary.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixBushTravelCacheTest,"CitixChase.Roadside.CacheSurvivesHostTravel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixBushTravelCacheTest::RunTest(const FString& Parameters)
{
 TWeakObjectPtr<UStaticMesh> Before=FCitixSurfaceLibrary::GetMesh(ECitixSurface::Bush);
 TestTrue(TEXT("Bush is available before host travel collection"),Before.IsValid());
 CollectGarbage(RF_NoFlags,true);
 if (!TestTrue(TEXT("Cached bush survives collection of the previous city"),Before.IsValid())) return false;
 TestEqual(TEXT("Rebuilt city gets the same live mesh"),FCitixSurfaceLibrary::GetMesh(ECitixSurface::Bush),Before.Get());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixAirFlowTest,"CitixChase.FX.TopSpeedAirFlow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixAirFlowTest::RunTest(const FString& Parameters)
{
 TestEqual(TEXT("No airflow at eighty percent of top speed"),UCitixAirFlowComponent::StrengthForSpeed(160,200),0.f);
 TestTrue(TEXT("Airflow appears above eighty percent"),UCitixAirFlowComponent::StrengthForSpeed(161,200)>0.f);
 TestTrue(TEXT("Airflow builds toward full speed"),UCitixAirFlowComponent::StrengthForSpeed(190,200)>UCitixAirFlowComponent::StrengthForSpeed(161,200));
 TestEqual(TEXT("Boost cannot make airflow intensity unbounded"),UCitixAirFlowComponent::StrengthForSpeed(300,200),1.f);
 TestEqual(TEXT("Invalid speed cap cannot create an effect"),UCitixAirFlowComponent::StrengthForSpeed(200,0),0.f);
 return true;
}
#endif
