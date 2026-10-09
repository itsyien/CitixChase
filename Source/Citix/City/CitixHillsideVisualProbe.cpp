#include "City/CitixHillsideVisualProbe.h"
#include "City/CitixCityGenerator.h"
#include "City/CitixHillsideLayout.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Citix.h"
#include "World/CitixTimeOfDay.h"
#include "Sandbox/CitixDestinationBeacon.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

void CitixHillsideVisualProbeTick(ACitixDrivingPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
 if(!PC || !PC->IsLocalController()) return;
 if(FParse::Param(FCommandLine::Get(),TEXT("CitixHillsideDriveProbe"))) {CitixHillsideDriveProbeTick(PC); return;}
 if(FParse::Param(FCommandLine::Get(),TEXT("CitixMapSelectionProbe")))
 {
  static int32 SelectionStage=0;
  auto* World=PC->GetWorld(); auto* State=World->GetGameState<ACitixChaseGameState>();
  if(!State) return;
  if(!PC->HasAuthority())
  {
   if(SelectionStage==0 && World->GetTimeSeconds()>3) {PC->ServerSelectLobbyMap(true); SelectionStage=1;}
   return;
  }
  int32 Verified=0;
  for(APlayerState* Player:State->PlayerArray) if(auto* PS=Cast<ACitixChasePlayerState>(Player);PS && PS->bCityIdentityValid) ++Verified;
  if(Verified!=2 || World->GetTimeSeconds()<(SelectionStage==0 ? 30.f : 12.f)) return;
  auto* Mode=World->GetAuthGameMode<ACitixChaseGameMode>();
  if(SelectionStage==0 && !State->bHillsideMap)
  {
   Mode->BeginInteraction(PC); // Set an actual ready state before changing maps.
   if(Mode->SelectLobbyMap(PC,true)) SelectionStage=1;
   return;
  }
  if(SelectionStage==1 && State->bHillsideMap)
  {
   bool ReadyCleared=true;
   for(APlayerState* Player:State->PlayerArray) if(auto* PS=Cast<ACitixChasePlayerState>(Player);PS && PS->bReady) ReadyCleared=false;
   const bool Passed=ReadyCleared && State->MapRevision==FCitixHillsideLayout::Revision && State->LayoutRelayLocations.Num()==6 && State->LayoutSpawnLocations.Num()==2 && State->LayoutSpawnLocations[0].Z>900 && State->BreakawayLocations.ContainsByPredicate([](const FVector& P){return P.Z>3000;});
   UE_LOG(LogCitix,Log,TEXT("[CitixMapProbe] Hillside verified peers=%d layout=%d"),Verified,Passed);
   if(Passed && Mode->SelectLobbyMap(PC,false)) SelectionStage=2;
   return;
  }
  if(SelectionStage==2 && !State->bHillsideMap) {UE_LOG(LogCitix,Log,TEXT("[CitixMapProbe] City restored verified peers=%d result=1"),Verified); SelectionStage=3;}
  return;
 }
 if(!FParse::Param(FCommandLine::Get(),TEXT("CitixHillsideVisualProbe"))) return;
 struct FProbe { TWeakObjectPtr<UWorld> World; TWeakObjectPtr<ACameraActor> Camera; int32 Stage=0; float At=0; };
 static FProbe Probe;
 auto* World=PC->GetWorld();
 if(Probe.World.Get()!=World) {Probe=FProbe(); Probe.World=World;}
 if(Probe.Stage==8) return;
 if(!Probe.Camera.IsValid())
 {
  ACitixCityGenerator* City=nullptr;
  for(TActorIterator<ACitixCityGenerator> It(World);It;++It) {City=*It; break;}
  if(!City) return;
  City->bHillsideMap=true;
  // Isolated visual fixture; traffic adaptation is verified separately.
  City->bSpawnTraffic=false; City->bSpawnPedestrians=false; City->GenerateCity();
  for(TActorIterator<ACitixTimeOfDay> It(World);It;++It) {It->SetActorTickEnabled(false); It->SetHours(11.f); It->SetTimePaused(true);}
  for(TActorIterator<ACitixDestinationBeacon> It(World);It;++It) It->SetActorHiddenInGame(true);
  Probe.Camera=World->SpawnActor<ACameraActor>();
  Probe.Camera->GetCameraComponent()->SetFieldOfView(50.f);
  PC->SetViewTarget(Probe.Camera.Get());
  Probe.Stage=1; Probe.At=World->GetTimeSeconds();
 }
 const FVector Positions[]={{68000,-80000,90000},{22000,-33000,1200},{-45000,18000,10000},{-6000,-14500,1500},{-1000,-4000,3200},{-5000,26000,7200},{-32000,-30000,1700}};
 const FVector Targets[]={{0,8000,2200},{16000,-24000,100},{-19000,23000,3800},{-6000,-9000,1700},{-1000,5100,4000},{1000,31000,8500},{-27000,-25000,2000}};
 const TCHAR* Names[]={TEXT("Hillside-Aerial"),TEXT("Hillside-Coast"),TEXT("Hillside-Switchbacks"),TEXT("Hillside-TunnelPortal"),TEXT("Hillside-Town"),TEXT("Hillside-Summit"),TEXT("Hillside-Marina")};
 const int32 Index=Probe.Stage-1;
 Probe.Camera->SetActorLocation(Positions[Index]);
 Probe.Camera->SetActorRotation((Targets[Index]-Positions[Index]).Rotation());
 if(World->GetTimeSeconds()-Probe.At>(Probe.Stage==1 ? 5.f : 2.5f))
 {
  FScreenshotRequest::RequestScreenshot(Names[Index],false,false);
  ++Probe.Stage; Probe.At=World->GetTimeSeconds();
 }
#endif
}
