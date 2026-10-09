#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Traffic/CitixTrafficVehicle.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixTrafficPoolVisibilityTest,
 "CitixChase.Traffic.PoolVisibilityAndCollision",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixTrafficPoolVisibilityTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Car=World->SpawnActor<ACitixTrafficVehicle>();
 TestTrue(TEXT("A pooled car starts hidden before its appearance arrives"),Car->IsHidden());
 TestFalse(TEXT("A pooled car cannot block at its initial origin"),Car->GetActorEnableCollision());
 Car->InitializeVehicle(ECitixCarType::Sedan,FLinearColor::White,1);
 TestTrue(TEXT("Building collidable components preserves pooled invisibility"),Car->IsHidden());
 TestFalse(TEXT("Building the body cannot revive hidden collision"),Car->GetActorEnableCollision());
 TArray<UPrimitiveComponent*> Parts; Car->GetComponents(Parts);
 UPrimitiveComponent* Body=nullptr;
 for(auto* Part:Parts) if(Part->GetCollisionObjectType()==ECC_Vehicle) { Body=Part; break; }
 TestNotNull(TEXT("The regression exercises a real blocking traffic body"),Body);
 auto BodyBlocks=[&]() {
  if(!Body) return false;
  const FVector Center=Body->Bounds.Origin;
  FHitResult Hit;
  return World->LineTraceSingleByChannel(Hit,Center-FVector(1000,0,0),Center+FVector(1000,0,0),ECC_Visibility) && Hit.GetActor()==Car;
 };
 TestFalse(TEXT("Hidden traffic cannot be hit by a real collision query"),BodyBlocks());
 for(int32 I=0;I<3;++I) {
  Car->SetVehicleVisible(true);
  TestFalse(TEXT("Real active traffic remains visible"),Car->IsHidden());
  TestTrue(TEXT("Real active traffic retains collision"),Car->GetActorEnableCollision());
  TestTrue(TEXT("Visible traffic still blocks a real collision query"),BodyBlocks());
  Car->SetVehicleVisible(false);
  TestTrue(TEXT("Released traffic is hidden"),Car->IsHidden());
  TestFalse(TEXT("Released traffic cannot block a driver"),Car->GetActorEnableCollision());
  TestFalse(TEXT("Released body is absent from collision queries"),BodyBlocks());
 }
 World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixTrafficRecycleSnapshotTest,
 "CitixChase.Traffic.RecycleSnapshot",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixTrafficRecycleSnapshotTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* Car=World->SpawnActor<ACitixTrafficVehicle>();
 Car->InitializeVehicle(ECitixCarType::Sedan,FLinearColor::White,1);
 Car->Motion.Location=FVector(1000,2000,0); Car->Motion.Timestamp=1; Car->Motion.Generation=1; Car->Motion.bVisible=true;
 Car->OnRep_Motion();
 TestTrue(TEXT("Receiving an active snapshot enables collision"),Car->GetActorEnableCollision());
 TestTrue(TEXT("Active pose and collider occupy the received position"),Car->GetActorLocation().Equals(FVector(1000,2000,0)));
 Car->Motion.bVisible=false; Car->Motion.Generation=2; Car->Motion.Timestamp=2;
 Car->OnRep_Motion();
 TestTrue(TEXT("A release snapshot hides the old lifecycle"),Car->IsHidden());
 TestFalse(TEXT("A release snapshot removes the old blocking body"),Car->GetActorEnableCollision());
 Car->Motion.Location=FVector(12000,3000,0); Car->Motion.bVisible=true; Car->Motion.Generation=3; Car->Motion.Timestamp=3;
 Car->OnRep_Motion();
 TestTrue(TEXT("Recycle snaps to the new pose instead of interpolating from the old car"),Car->GetActorLocation().Equals(FVector(12000,3000,0)));
 TestTrue(TEXT("Recycled real traffic is collidable"),Car->GetActorEnableCollision());
 TestFalse(TEXT("Recycled real traffic is visible"),Car->IsHidden());
 TestEqual(TEXT("Old generation interpolation samples are discarded"),Car->History.Num(),1);
 World->DestroyWorld(false);
 return true;
}
#endif
