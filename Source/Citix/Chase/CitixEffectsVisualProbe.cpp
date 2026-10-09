#include "Chase/CitixEffectsVisualProbe.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixAirFlowComponent.h"
#include "Chase/CitixSmokeCloud.h"
#include "Chase/CitixChaseGameState.h"
#include "Sandbox/CitixBulletTracer.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Core/CitixGraphicsSettings.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "World/CitixTimeOfDay.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Citix.h"

void CitixEffectsVisualProbeTick(ACitixDrivingPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
 if (!PC || !PC->IsLocalController() || !PC->PlayerState || !FParse::Param(FCommandLine::Get(),TEXT("CitixEffectsVisualProbe"))) return;
 struct FFixture
 {
  TWeakObjectPtr<UWorld> World;
  TWeakObjectPtr<ACameraActor> Camera;
  TWeakObjectPtr<ACitixVehiclePawn> Car;
  TWeakObjectPtr<ACitixSmokeCloud> Smoke;
  TArray<TWeakObjectPtr<AActor>> Props;
  int32 Stage=0;
  float At=0.f;
  float OnSpeed=0.f,OnStrength=0.f,OffSpeed=0.f,OffStrength=0.f;
  float OwnerAlpha=0.f,OtherAlpha=0.f;
  float OwnerComposite=0.f;
  bool bPassed=true;
 };
 static FFixture F;
 auto* World=PC->GetWorld();
 if (F.World.Get()!=World) {F=FFixture(); F.World=World;}
 if (F.Stage==9) return;
 const float Now=World->GetTimeSeconds();
 for (TActorIterator<ACitixTimeOfDay> It(World);It;++It) {It->SetHours(18.f); It->SetTimePaused(true);}
 const FVector Origin(160000,0,500);
 const FVector CarPosition=Origin+FVector(0,0,70);
 auto Capture=[&](const TCHAR* Name,bool ShowUI=false) {
  const FString Tag=World->GetNetMode()==NM_Client ? TEXT("Guest") : TEXT("Host");
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(FString(Name)+TEXT("-")+Tag+TEXT(".png")),ShowUI,false);
 };
 auto Box=[&](const FVector& Position,const FVector& Size,ECitixSurface Surface,bool Collision) {
  auto* Actor=World->SpawnActor<AActor>();
  auto* Mesh=NewObject<UStaticMeshComponent>(Actor); Actor->SetRootComponent(Mesh);
  Mesh->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete));
  Mesh->SetMaterial(0,FCitixSurfaceLibrary::GetMaterial(Surface));
  Mesh->SetWorldLocation(Position); Mesh->SetWorldScale3D(Size/100.f);
  Mesh->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
  Mesh->SetCollisionResponseToAllChannels(ECR_Block); Mesh->RegisterComponent();
  F.Props.Add(Actor); return Actor;
 };
 if (!F.Camera.IsValid())
 {
  for (TActorIterator<ACitixTimeOfDay> It(World);It;++It) {It->SetHours(18.f); It->SetTimePaused(true);}
  Box(Origin-FVector(0,0,25),FVector(5500,5500,50),ECitixSurface::Asphalt,true);
  Box(Origin+FVector(-1700,0,600),FVector(100,5000,1250),ECitixSurface::PropDark,false);
  // A few restrained architectural planes give the luminous ribbons a dark,
  // legible background without competing with the car silhouette.
  for (int32 I=0;I<5;++I) Box(Origin+FVector(-1550,(I-2)*650,620),FVector(180,350,1300+I*100),ECitixSurface::FacadeConcrete,false);
  F.Car=World->SpawnActor<ACitixVehiclePawn>(CarPosition,FRotator::ZeroRotator);
  F.Car->SetReplicates(false); F.Car->SetCarAppearance(ECitixCarType::Sedan,FLinearColor(.8f,.015f,.025f));
  F.Car->SetOccupied(true);
  // Keep ordinary effect evaluation intact. Only this fixture's mechanical
  // forces are disabled; its chassis remains a real simulated physics body.
  F.Car->GetVehicleMovement()->SetComponentTickEnabled(false);
  auto* Body=CastChecked<UPrimitiveComponent>(F.Car->GetRootComponent());
  Body->SetSimulatePhysics(true); Body->SetEnableGravity(false);
  F.Camera=World->SpawnActor<ACameraActor>();
  F.Camera->SetActorLocation(Origin+FVector(950,-1300,420));
  F.Camera->SetActorRotation((Origin+FVector(-130,0,100)-F.Camera->GetActorLocation()).Rotation());
  F.Camera->GetCameraComponent()->SetFieldOfView(60.f); PC->SetViewTarget(F.Camera.Get());
  F.At=Now;
 }
 if (!F.Car.IsValid()) return;
 const float Cap=F.Car->GetVehicleMovement()->MaxSpeed;
 auto* Body=CastChecked<UPrimitiveComponent>(F.Car->GetRootComponent());
 // Lock only the presentation pose, then set genuine physical velocity. The
 // production GetDisplaySpeedKmh/StrengthForSpeed chain supplies all intensity.
 F.Car->SetActorLocationAndRotation(CarPosition,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
 Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
 Body->SetPhysicsLinearVelocity(FVector(Cap*((F.Stage==0 || F.Stage==7) ? .96f : F.Stage==1 ? .79f : 0.f),0,0));
 if (F.Stage==0 && Now-F.At>5.f)
 {
  const auto* FX=F.Car->FindComponentByClass<UCitixAirFlowComponent>();
  F.OnSpeed=F.Car->GetDisplaySpeedKmh(); F.OnStrength=FX ? FX->GetStrength() : 0.f;
  F.bPassed &= F.OnSpeed>Cap*.036f*.8f && F.OnStrength>.5f;
  Capture(TEXT("Effects-AirCut-On")); F.Stage=1; F.At=Now;
 }
 else if (F.Stage==1 && Now-F.At>2.f)
 {
  const auto* FX=F.Car->FindComponentByClass<UCitixAirFlowComponent>();
  F.OffSpeed=F.Car->GetDisplaySpeedKmh(); F.OffStrength=FX ? FX->GetStrength() : -1.f;
  F.bPassed &= F.OffSpeed<Cap*.036f*.8f && F.OffStrength==0.f;
  Capture(TEXT("Effects-AirCut-Off")); F.Stage=2; F.At=Now;
 }
 else if (F.Stage==2 && Now-F.At>.5f)
 {
  F.Smoke=World->SpawnActor<ACitixSmokeCloud>(CarPosition,FRotator::ZeroRotator);
  F.Smoke->SetReplicates(false); F.Smoke->SetLifeSpan(0.f);
  F.Smoke->EmitterPlayerState=PC->PlayerState;
  for (int32 I=0;I<ACitixSmokeCloud::PuffCount;++I) F.Smoke->EmissionPositions.Add(CarPosition-FVector(200.f+I*4.f,0,0));
  F.Stage=3; F.At=Now;
 }
 if (F.Smoke.IsValid() && (F.Stage==3 || F.Stage==4))
 {
  const auto* State=World->GetGameState<ACitixChaseGameState>();
  const float Clock=State ? State->GetServerWorldTimeSeconds() : Now;
  F.Smoke->StartedAt=Clock-6.f; // Freeze identical birth/growth/fade for paired renders.
  // Screenshot requests complete after this tick. Keep the deployer identity
  // unchanged until its requested frame has actually reached the renderer.
  if (F.Stage==4 && Now-F.At>.3f) F.Smoke->EmitterPlayerState=nullptr;
  if (Now-F.At>2.f)
  {
   const float Opacity=F.Smoke->OpacityForViewer(PC->PlayerState);
   if (F.Stage==3) {
    F.OwnerAlpha=Opacity; Capture(TEXT("Effects-Smoke-Deployer"));
    float Transmittance=1.f;
    const auto& Data=F.Smoke->Billows->PerInstanceSMCustomData;
    for (int32 I=0;I<ACitixSmokeCloud::PuffCount;++I) if (Data.IsValidIndex(I*2)) Transmittance*=1.f-Data[I*2];
    F.OwnerComposite=1.f-Transmittance;
    F.bPassed &= F.OwnerComposite>.1f && F.OwnerComposite<=1.f/3.f+.00001f;
    F.Stage=4; F.At=Now;
   } else {
    F.OtherAlpha=Opacity; Capture(TEXT("Effects-Smoke-OtherViewer"));
    F.bPassed &= FMath::IsNearlyEqual(F.OwnerAlpha,1.f/3.f) && FMath::IsNearlyEqual(F.OtherAlpha,1.f);
    F.Stage=5; F.At=Now;
   }
  }
 }
 if (F.Stage==5 && Now-F.At>.3f) F.Smoke->SetActorHiddenInGame(true);
 // Allow exposure to recover from the opaque comparison cloud before shooting.
 if (F.Stage==5 && Now-F.At>3.f)
 {
  F.Smoke->SetActorHiddenInGame(true);
  const FVector Muzzle=Origin+FVector(-450,-450,125),End=Origin+FVector(700,-450,125);
  Box(Origin+FVector(550,-450,125),FVector(50,180,250),ECitixSurface::PropDark,true);
  const FVector Resolved=ACitixBulletTracer::ResolveVisibleEndpoint(World,Muzzle,End,nullptr);
  F.bPassed &= Resolved.X<End.X-100.f;
  ACitixBulletTracer::Spawn(World,Muzzle,End);
  // Hold only the diagnostic shot for its requested screenshot: asynchronous
  // capture can arrive after the production 90 ms shot has already expired.
  // Production spawn/clipping/material remain intact; lifetime is not measured
  // by this screenshot fixture.
  for (TActorIterator<ACitixBulletTracer> It(World);It;++It)
  { It->SetActorTickEnabled(false); It->SetLifeSpan(.6f); }
  Capture(TEXT("Effects-BulletTrail"));
  const FString Receipt=FString::Printf(TEXT("{\"passed\":%s,\"fixture\":\"isolated_pose_real_chassis_velocity; smoke_same_cloud_identity_comparison; tracer_real_cover_clip\",\"cap_kmh\":%.3f,\"on_speed_kmh\":%.3f,\"on_strength\":%.5f,\"off_speed_kmh\":%.3f,\"off_strength\":%.5f,\"deployer_alpha_budget\":%.6f,\"deployer_all_puffs_composite_bound\":%.6f,\"other_alpha_scale\":%.6f,\"tracer_length_cm\":%.3f,\"tracer_duration_seconds\":%.3f}"),F.bPassed ? TEXT("true") : TEXT("false"),Cap*.036f,F.OnSpeed,F.OnStrength,F.OffSpeed,F.OffStrength,F.OwnerAlpha,F.OwnerComposite,F.OtherAlpha,FVector::Distance(Muzzle,Resolved),ACitixBulletTracer::Duration);
  const FString Tag=World->GetNetMode()==NM_Client ? TEXT("Guest") : TEXT("Host");
  FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/(TEXT("EffectsVisualProbe-")+Tag+TEXT(".json"))));
  UE_LOG(LogCitix,Log,TEXT("[CitixEffectsVisualProbe] %s %s"),F.bPassed ? TEXT("PASS") : TEXT("FAIL"),*Receipt);
  F.Stage=6; F.At=Now;
 }
 else if (F.Stage==6 && Now-F.At>1.f)
 {
  F.Car->ToggleCamera(); PC->SetViewTarget(F.Car.Get());
  for (int32 I=0;I<3;++I) for (float Side:{-1.f,1.f})
   Box(Origin+FVector(1500+I*700,Side*750,500),FVector(300,300,1000),ECitixSurface::FacadeConcrete,false);
  F.Stage=7; F.At=Now;
 }
 else if (F.Stage==7 && Now-F.At>3.f)
 {
  Capture(TEXT("Effects-WindowCamera-HighSpeed")); F.Stage=8; F.At=Now;
 }
 else if (F.Stage==8 && Now-F.At>1.f)
 {
  if (!PC->IsSettingsMenuOpen()) PC->ToggleSettingsMenu();
  if (Now-F.At>2.f) {Capture(TEXT("Effects-Settings-AudioCard"),true); F.Stage=10; F.At=Now;}
 }
 else if (F.Stage>=10 && F.Stage<=12 && Now-F.At>.3f)
 {
  // Exercise the actual world's audio device without saving or changing the
  // player's settings object. The getter must run on the audio thread.
  const float Percent=F.Stage==10 ? 0.f : F.Stage==11 ? 37.f : 100.f;
  auto* ProbeSettings=NewObject<UCitixGraphicsSettings>();
  const FAudioDeviceHandle Device=World->GetAudioDevice();
  if (Device.IsValid())
  {
   ProbeSettings->SetMasterVolumePercent(Percent,World);
   FAudioThread::RunCommandOnAudioThread([Device,Percent]()
   {
    const float Gain=Device->GetTransientPrimaryVolume();
    UE_LOG(LogCitix,Log,TEXT("[CitixAudioDeviceProbe] %s volume=%.0f device_gain=%.2f"),FMath::IsNearlyEqual(Gain,Percent/100.f) ? TEXT("PASS") : TEXT("FAIL"),Percent,Gain);
   });
  }
  else UE_LOG(LogCitix,Error,TEXT("[CitixAudioDeviceProbe] FAIL no audio device."));
  if (F.Stage==12)
  {
   if (auto* PlayerSettings=UCitixGraphicsSettings::Get()) PlayerSettings->ApplyAudioSettings(World);
   F.Stage=9;
  }
  else {++F.Stage; F.At=Now;}
 }
#endif
}
