#include "Chase/CitixRoadsideVisualProbe.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixAirFlowComponent.h"
#include "Chase/CitixChaseRules.h"
#include "City/CitixYienBillboard.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Citix.h"
#include "World/CitixTimeOfDay.h"
#include "EngineUtils.h"

void CitixRoadsideVisualProbeTick(ACitixDrivingPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
 if (!PC->IsLocalController()) return;
 static TWeakObjectPtr<ACameraActor> Camera;
 static TWeakObjectPtr<ACitixVehiclePawn> Car;
 static int32 Stage=0; static float At=0;
 auto* World=PC->GetWorld(); const float Now=World->GetTimeSeconds();
 const FVector Origin(120000,0,0);
 if (Stage==0) {
  for (TActorIterator<ACitixTimeOfDay> It(World);It;++It) {It->SetHours(13.f); It->SetTimePaused(true);}
  auto* Floor=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Floor); Floor->SetRootComponent(Mesh);
  Mesh->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::Asphalt)); Mesh->SetMaterial(0,FCitixSurfaceLibrary::GetMaterial(ECitixSurface::Asphalt));
  Mesh->SetWorldLocation(Origin-FVector(0,0,25)); Mesh->SetWorldScale3D(FVector(45,45,.5)); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Mesh->SetCollisionResponseToAllChannels(ECR_Block); Mesh->RegisterComponent();
  for (int32 I=0;I<4;++I) {
   auto* A=World->SpawnActor<AActor>(); auto* B=NewObject<UStaticMeshComponent>(A); A->SetRootComponent(B);
   B->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::Bush)); B->SetMaterial(0,FCitixSurfaceLibrary::GetMaterial(ECitixSurface::Bush));
   B->SetWorldLocation(Origin+FVector(I*330-500,0,75)); B->SetWorldScale3D(FVector(3.2,1.8,1.5)); B->SetWorldRotation(FRotator(0,I*13,0)); B->SetCollisionEnabled(ECollisionEnabled::NoCollision); B->RegisterComponent();
  }
  auto* Sign=World->SpawnActor<ACitixYienBillboard>(Origin+FVector(-500,750,950),FRotator(0,-90,0)); Sign->Configure(0,false);
  Camera=World->SpawnActor<ACameraActor>(); Camera->SetActorLocation(Origin+FVector(750,-1100,380)); Camera->SetActorRotation((Origin+FVector(0,0,150)-Camera->GetActorLocation()).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(65);
  PC->SetViewTarget(Camera.Get()); At=Now; Stage=1;
 } else if (Stage==1 && Now-At>10.f) {
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Roadside-Bushes.png"),true,false);
  At=Now; Stage=2;
 } else if (Stage==2 && Now-At>1.f) {
  Car=World->SpawnActor<ACitixVehiclePawn>(Origin+FVector(0,-600,70),FRotator::ZeroRotator); Car->ApplyChasePerformance(false); Car->SetOccupied(true);
  Camera->SetActorLocation(Origin+FVector(-850,-1350,350)); Camera->SetActorRotation((Origin+FVector(70,-600,100)-Camera->GetActorLocation()).Rotation());
  At=Now; Stage=3;
 } else if (Car.IsValid() && (Stage==3 || Stage==4)) {
  // Deliberately fixed presentation pose with injected speed for FX screenshots;
  // this is isolated from ordinary driving and is not a natural driving test.
  Car->SetActorLocationAndRotation(Origin+FVector(0,-600,70),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
  auto* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent());
  Body->SetPhysicsLinearVelocity(FVector(FCitixChaseRules::SpeedLimit(false)*(Stage==3 ? .96f : .8f),0,0));
  if (Now-At>(Stage==3 ? 3.f : 1.5f)) {
   const auto* FX=Car->FindComponentByClass<UCitixAirFlowComponent>();
   UE_LOG(LogCitix,Log,TEXT("[CitixRoadsideVisual] stage=%d speed=%.1f airflow=%.3f"),Stage,Car->GetDisplaySpeedKmh(),FX ? FX->GetStrength() : -1);
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(Stage==3 ? TEXT("Screenshots/AirFlow-On.png") : TEXT("Screenshots/AirFlow-Off.png")),true,false);
   ++Stage; At=Now;
  }
 }
#endif
}
