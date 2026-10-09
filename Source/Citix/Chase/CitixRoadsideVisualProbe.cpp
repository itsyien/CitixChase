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
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "City/CitixCityChunk.h"
#include "Core/CitixGraphicsSettings.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "HAL/IConsoleManager.h"

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

// Isolated visual fixtures use injected transforms and reveal state. Normal play
// continues to use the replicated target and server reveal rules exclusively.
void CitixTrackerVisualProbeTick(ACitixDrivingPlayerController* PC) {
#if !UE_BUILD_SHIPPING
 if(!PC->IsLocalController() || !PC->GetPawn())return;
 auto* World=PC->GetWorld(); auto* State=World->GetGameState<ACitixChaseGameState>(); auto* Local=PC->GetPlayerState<ACitixChasePlayerState>();
 if(!State || !Local)return;
 static TWeakObjectPtr<ACameraActor> Camera; static TWeakObjectPtr<ACitixVehiclePawn> Runner;
 static TWeakObjectPtr<UStaticMeshComponent> Backdrop; static int32 Stage=0; static float At=0; static bool Shot=false; static bool Sprinkles=false;
 const FVector Origin(120000,0,0); const float Now=World->GetTimeSeconds();
 if(!Camera.IsValid()) {
  if(auto* Settings=UCitixGraphicsSettings::Get()) Settings->SelectPreset(3);
  auto Box=[&](FVector Position,FVector Scale,UMaterialInterface* Material) {
   auto* Actor=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Actor); Actor->SetRootComponent(Mesh); Mesh->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete)); Mesh->SetMaterial(0,Material); Mesh->SetWorldLocation(Position); Mesh->SetWorldScale3D(Scale); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->RegisterComponent(); return Mesh;
  };
  auto* Ground=Box(Origin-FVector(0,0,25),FVector(900,900,.5),FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.07f,.08f,.09f)));
  Ground->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Ground->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
  Backdrop=Box(Origin+FVector(60000,0,10000),FVector(1,2000,1000),FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.009f,.014f,.025f)));
  Runner=World->SpawnActor<ACitixVehiclePawn>(Origin+FVector(2000,700,850),FRotator::ZeroRotator);
  auto* TargetState=World->SpawnActor<ACitixChasePlayerState>(); TargetState->ChaseRole=ECitixChaseRole::Runner; Runner->SetPlayerState(TargetState); State->AddPlayerState(TargetState);
  Cast<UPrimitiveComponent>(Runner->GetRootComponent())->SetSimulatePhysics(false);
  Camera=World->SpawnActor<ACameraActor>(); Camera->SetActorLocation(Origin+FVector(-950,-600,600)); Camera->SetActorRotation((Origin+FVector(120,0,230)-Camera->GetActorLocation()).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(65); PC->SetViewTarget(Camera.Get());
  auto* Chunk=World->SpawnActor<ACitixCityChunk>(); Chunk->Init(FIntPoint(0,0),10000);
  for(float Distance:{3500.f,8000.f,16000.f,35000.f}) for(int32 I=0;I<9;++I) Chunk->QueueBox(ECitixSurface::Bush,Origin+FVector(Distance,(I-4)*300,75),FVector(300,170,150)); Chunk->Finish();
  auto* Mesh=FCitixSurfaceLibrary::GetMesh(ECitixSurface::Bush); UE_LOG(LogCitix,Log,TEXT("[TrackerFixture] bush bounds=%.2f render bounds=%.2f min screen=%.8f"),Mesh->GetBounds().SphereRadius,Mesh->GetRenderData()->Bounds.SphereRadius,IConsoleManager::Get().FindConsoleVariable(TEXT("foliage.MinimumScreenSize"))->GetFloat());
  At=Now;
 }
 if(Stage==0 && Now-At>3.75f && !Sprinkles) {Sprinkles=true; State->MulticastRelaySprinkles(PC->GetPawn()->GetActorLocation(),FVector::ZeroVector);}
 Local->ChaseRole=ECitixChaseRole::Chaser; Local->RapidBrakeCharges=1; Local->NextRapidBrakeAt=0; State->Phase=ECitixChasePhase::Pursuit; State->RoundNumber=1; State->PhaseSecondsRemaining=270; State->bRunnerRevealed=Stage<4; State->RevealSecondsRemaining=5; State->StatusText=TEXT("");
 if(auto* Body=Cast<UPrimitiveComponent>(PC->GetPawn()->GetRootComponent())) Body->SetSimulatePhysics(false);
 PC->GetPawn()->SetActorLocationAndRotation(Origin+FVector(0,0,70),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
 const FVector Positions[]={FVector(2200,800,850),FVector(-3500,1600,-900),FVector(2000,1200,75),FVector(2000,1200,75)};
 if(Stage<4)Runner->SetActorLocation(Origin+Positions[Stage]);
 if(!Shot && Now-At>4.f && Stage<6) {
  const TCHAR* Names[]={TEXT("Tracker-Above-Dark"),TEXT("Tracker-Below-Bright"),TEXT("Tracker-Visible"),TEXT("Tracker-Occluded"),TEXT("Bush-Max-Range"),TEXT("Bush-Low-Range")};
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(FString(Names[Stage])+TEXT(".png")),true,false); Shot=true;
 }
 if(Shot && Now-At>4.3f && Stage<6) {
  ++Stage; At=Now; Shot=false;
  if(Stage==1) Backdrop->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.65f,.7f,.75f)));
  if(Stage==2) Runner->SetActorLocation(Origin+FVector(2000,1200,75));
  if(Stage==3) {
   auto* Wall=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Wall); Wall->SetRootComponent(Mesh);
   Mesh->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete)); Mesh->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.12f,.16f,.21f)));
   Mesh->SetWorldLocation(Origin+FVector(1100,750,600)); Mesh->SetWorldScale3D(FVector(1,35,12)); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->RegisterComponent();
  }
  if(Stage==4 || Stage==5) {
   Camera->SetActorLocation(Origin+FVector(0,0,200)); Camera->SetActorRotation(FRotator(0,0,0)); Camera->GetCameraComponent()->SetFieldOfView(50); State->bRunnerRevealed=false;
   if(auto* Settings=UCitixGraphicsSettings::Get()) Settings->SelectPreset(Stage==4 ? 3 : 0);
  }
  if(Stage==6) UE_LOG(LogCitix,Log,TEXT("[TrackerFixture] Complete: above, below, on-screen, hidden, Max and Low bush range captures"));
 }
#endif
}
