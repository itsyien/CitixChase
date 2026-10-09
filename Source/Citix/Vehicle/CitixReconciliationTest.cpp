#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixReconciliationTest,"CitixChase.Network.OwnerHeadingAndVelocity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixReconciliationTest::RunTest(const FString&) {
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Car=World->SpawnActor<ACitixVehiclePawn>(); auto* PC=World->SpawnActor<APlayerController>(); PC->SetPlayer(NewObject<ULocalPlayer>(GEngine)); PC->Possess(Car);
 auto* Body=CastChecked<UBoxComponent>(Car->GetRootComponent()); Body->SetSimulatePhysics(true);
 TestTrue(TEXT("Exercise the locally controlled physics correction branch"),Car->IsLocallyControlled() && Body->IsSimulatingPhysics());
 for(float SideSpeed:{0.f,600.f}) for(float Speed:{3000.f,-1200.f}) {
  Car->SetActorLocationAndRotation(FVector::ZeroVector,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
  Body->SetPhysicsLinearVelocity(FVector(Speed,SideSpeed,-200)); Car->RemoteMovementBuffer.Reset();
  Car->PushRemoteSnapshot(FVector(0,200,0),FRotator(0,45,0),FRotator(0,45,0).RotateVector(FVector(Speed,SideSpeed,-200)));
  Car->InterpolateRemoteMovement(1.f/60);
  const FVector V=Body->GetPhysicsLinearVelocity();const float Side=FVector::DotProduct(V,Car->GetActorRightVector());
  AddInfo(FString::Printf(TEXT("OWNER_CORRECTION speed=%.0f side=%.3f yaw=%.3f"),Speed,Side,Car->GetActorRotation().Yaw));
  TestTrue(TEXT("Heading correction cannot invent sideways velocity from position error"),FMath::Abs(Side-SideSpeed)<1.f);
  TestTrue(TEXT("Reverse retains its sign"),FVector::DotProduct(V,Car->GetActorForwardVector())*Speed>0);
  TestTrue(TEXT("Correction preserves vertical velocity"),FMath::IsNearlyEqual(V.Z,-200.,.01));
 }
 World->DestroyWorld(false);return true;
}
#endif
