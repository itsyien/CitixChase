#include "City/CitixHillsideVisualProbe.h"
#include "City/CitixCityGenerator.h"
#include "City/CitixHillsideLayout.h"
#include "Core/CitixCitySettings.h"
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
 const bool Performance=FParse::Param(FCommandLine::Get(),TEXT("CitixHillsidePerfProbe"));
 if(!FParse::Param(FCommandLine::Get(),TEXT("CitixHillsideVisualProbe")) && !Performance) return;
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
  for(TActorIterator<ACitixTimeOfDay> It(World);It;++It) {It->SetActorTickEnabled(false); It->SetHours(UCitixCitySettings::Get().Hillside.InitialHour); It->SetTimePaused(true);}
  for(TActorIterator<ACitixDestinationBeacon> It(World);It;++It) It->SetActorHiddenInGame(true);
  Probe.Camera=World->SpawnActor<ACameraActor>();
  Probe.Camera->GetCameraComponent()->SetFieldOfView(50.f);
  PC->SetViewTarget(Probe.Camera.Get());
  Probe.Stage=1; Probe.At=World->GetTimeSeconds();
 }
 const FVector Positions[]={{68000,-80000,90000},{45000,-45000,14000},{-45000,12000,16000},{-18388,-16189,1150},{-12000,-14500,6500},{-7000,32000,12500},{-32000,-30000,1700}};
 const FVector Targets[]={{0,8000,4500},{0,7000,3500},{-12000,22000,6400},{-14523,-15662,1167},{-1500,-6000,4200},{3000,36000,11100},{-27000,-26000,2000}};
 const TCHAR* Names[]={TEXT("Hillside-Aerial"),TEXT("Hillside-Coast"),TEXT("Hillside-Switchbacks"),TEXT("Hillside-TunnelPortal"),TEXT("Hillside-Town"),TEXT("Hillside-Summit"),TEXT("Hillside-Marina")};
 const int32 Index=Performance ? 0 : Probe.Stage-1;
 Probe.Camera->SetActorLocation(Positions[Index]);
 Probe.Camera->SetActorRotation((Targets[Index]-Positions[Index]).Rotation());
 if(!Performance && World->GetTimeSeconds()-Probe.At>(Probe.Stage==1 ? 5.f : 2.5f))
 {
  FScreenshotRequest::RequestScreenshot(Names[Index],false,false);
  ++Probe.Stage; Probe.At=World->GetTimeSeconds();
 }
#endif
}
