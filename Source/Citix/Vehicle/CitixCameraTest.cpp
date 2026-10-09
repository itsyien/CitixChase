#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixWindowCameraTest,"CitixChase.Camera.WindowAnchor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixWindowCameraTest::RunTest(const FString&) {
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Car=World->SpawnActor<ACitixVehiclePawn>();
 auto* Camera=Car->FindComponentByClass<UCameraComponent>(); auto* Boom=Car->FindComponentByClass<USpringArmComponent>();
 const auto ChaseParent=Camera->GetAttachParent(); const float ChaseFov=Camera->FieldOfView;
 Car->ToggleCamera();
 TestTrue(TEXT("Window camera attaches directly to vehicle, bypassing speed lag"),Camera->GetAttachParent()==Car->GetRootComponent());
 const FVector WindowAnchor=Camera->GetRelativeLocation();
 Boom->TargetArmLength=1250.f; Boom->SetRelativeLocation(FVector(-400,0,60)); Boom->UpdateComponentToWorld(); Camera->UpdateComponentToWorld();
 TestTrue(TEXT("Spring arm movement cannot displace the window camera"),Camera->GetComponentLocation().Equals(Car->GetActorTransform().TransformPosition(WindowAnchor),.01));
 TestTrue(TEXT("First person uses stable FOV"),FMath::IsNearlyEqual(Camera->FieldOfView,ChaseFov));
 Car->ToggleCamera();
 TestTrue(TEXT("Third person restores its original spring arm attachment"),Camera->GetAttachParent()==ChaseParent);
 TestTrue(TEXT("Third person retains position lag"),Boom->bEnableCameraLag);
 World->DestroyWorld(false); return true;
}
#endif
