#include "Chase/CitixIceProbe.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixIceWave.h"
#include "City/CitixCityGenerator.h"
#include "City/CitixYienBillboard.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Citix.h"

// Explicit command-line integration fixtures, isolated from normal match behavior.
void CitixIceProbeTick(ACitixChaseGameMode* Mode)
{
 auto* World=Mode->GetWorld(); auto* S=World->GetGameState<ACitixChaseGameState>();
 static int32 Step=0,SignCount=0,ScreenCount=0; static float Activated=0,Limit=0; static bool Passed=true;
 if (!S || S->Phase!=ECitixChasePhase::Pursuit || Step==99) return;
 ACitixDrivingPlayerController *Chaser=nullptr,*Runner=nullptr;
 for (APlayerState* State:S->PlayerArray) if (auto* PS=Cast<ACitixChasePlayerState>(State)) {
  auto* PC=Cast<ACitixDrivingPlayerController>(PS->GetOwner());
  (PS->ChaseRole==ECitixChaseRole::Runner ? Runner : Chaser)=PC;
 }
 auto* C=Chaser ? Cast<ACitixVehiclePawn>(Chaser->GetPawn()) : nullptr;
 auto* R=Runner ? Cast<ACitixVehiclePawn>(Runner->GetPawn()) : nullptr;
 if (!C || !R) return;
 auto* CP=Chaser->GetPlayerState<ACitixChasePlayerState>(); auto* RP=Runner->GetPlayerState<ACitixChasePlayerState>();
 auto* Body=Cast<UPrimitiveComponent>(R->GetRootComponent()); const float Now=World->GetTimeSeconds();
 auto Finish=[&](const TCHAR* Why) {
  UE_LOG(LogCitix,Log,TEXT("[CitixIceProbe] %s %s"),Passed ? TEXT("PASS") : TEXT("FAIL"),Why);
  const FString Receipt=FString::Printf(TEXT("{\"passed\":%s,\"immediate_speed_kmh\":%.2f,\"freeze_seconds\":5,\"recharge_seconds\":50,\"billboards\":%d,\"building_screens\":%d}"),Passed ? TEXT("true") : TEXT("false"),Limit*.036f,SignCount,ScreenCount);
  FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/TEXT("ChaseIceProbe.json"))); Step=99;
 };
 if (Step==0) {
  if (S->PhaseSecondsRemaining>297.f) return;
  int32 Signs=0,Screens=0; for(TActorIterator<ACitixYienBillboard> It(World);It;++It) { ++Signs; if(It->bBuildingScreen)++Screens; }
  SignCount=Signs; ScreenCount=Screens;
  Passed &= Signs==5 && Screens==3 && CP->IceCharges==1 && RP->IceCharges==0;
  // Place both vehicles on a long, verified stretch of existing road.
  bool Placed=false;
  for (TActorIterator<ACitixCityGenerator> It(World);It && !Placed;++It) {
   const auto& Roads=It->GetRoadNetwork();
   for (int32 I=0;I<Roads.Edges.Num();++I) {
    const auto& E=Roads.Edges[I]; if(!E.bDrivable || E.bBridge || Roads.EdgeLength(I)<6000)continue;
    const FVector2D D=Roads.EdgeDirection(I),Mid=(Roads.Nodes[E.NodeA].Position+Roads.Nodes[E.NodeB].Position)*.5f;
    const FVector Forward(D.X,D.Y,0),P(Mid.X,Mid.Y,150);
    FTransform A,B;
    if (!ACitixCityGenerator::ValidateChaseSurface(World,P,FVector(240,110,85),Forward.Rotation().Yaw,C,A,false) || !ACitixCityGenerator::ValidateChaseSurface(World,P+Forward*1800,FVector(240,110,85),Forward.Rotation().Yaw,R,B,false))continue;
    A.SetRotation(Forward.Rotation().Quaternion()); B.SetRotation(Forward.Rotation().Quaternion());
    C->SetActorTransform(A,false,nullptr,ETeleportType::TeleportPhysics); R->SetActorTransform(B,false,nullptr,ETeleportType::TeleportPhysics);
    C->ForceNetUpdate(); R->ForceNetUpdate(); Placed=true; break;
   }
  }
  if(!Placed) { Passed=false; Finish(TEXT("No safe fixture road"));return; }
  if (FParse::Param(FCommandLine::Get(),TEXT("CitixIceScreenshot"))) {
   auto* Camera=World->SpawnActor<ACameraActor>();
   const FVector Ahead=C->GetActorForwardVector(), Look=C->GetActorLocation()+Ahead*1500+FVector(0,0,50);
   const FVector Position=C->GetActorLocation()-Ahead*1000+FVector(0,0,700);
   Camera->SetActorLocation(Position); Camera->SetActorRotation((Look-Position).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(75);
   Chaser->SetViewTarget(Camera);
  }
  Mode->UseChaserIce(Runner); Passed &= RP->IceCharges==0 && CP->IceCharges==1;
  const FVector Velocity=R->GetActorForwardVector()*(100.f/.036f); Body->SetPhysicsLinearVelocity(Velocity);
  Chaser->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));
  Limit=Body->GetPhysicsLinearVelocity().Size2D(); Activated=Now;
  Passed &= CP->IceCharges==0 && CP->bLastIceHit && FMath::IsNearlyEqual(Limit,70.f/.036f,2.f) && FMath::IsNearlyEqual(RP->FrozenUntil-Now,FCitixChaseRules::IceDuration,.02f);
  Mode->UseChaserIce(Chaser); Passed &= CP->IceCharges==0;
  UE_LOG(LogCitix,Log,TEXT("[CitixIceProbe] immediate cut %.2f km/h, signs=%d screens=%d passed=%d"),Limit*.036f,Signs,Screens,Passed);
  Step=1;
 } else if (Step==1) {
  R->ServerSendDriveInput(1,0,false,true);
  if (Now-Activated>.2f && Now-Activated<FCitixChaseRules::IceDuration-.1f) Passed &= R->GetVehicleMovement()->bIceFrozen && !R->GetVehicleMovement()->IsBoosting() && Body->GetPhysicsLinearVelocity().Size2D()<=Limit+10;
  if (Now-Activated>FCitixChaseRules::IceDuration+.2f) { Passed &= !R->GetVehicleMovement()->bIceFrozen; Step=2; }
 } else if (Step==2) {
  R->ServerSendDriveInput(1,0,false,true);
  if (Now-Activated>FCitixChaseRules::IceDuration+1.1f) {
   Passed &= Body->GetPhysicsLinearVelocity().Size2D()>Limit+10 && R->GetVehicleMovement()->IsBoosting();
   // Recharge arithmetic uses the real rule with simulated server times; no fifty-second sleep.
   int32 Reserve=0;float Next=Activated+50;
   FCitixChaseRules::RefillIce(Activated+49.9f,Reserve,Next); Passed &= Reserve==0;
   FCitixChaseRules::RefillIce(Activated+50,Reserve,Next);Passed &= Reserve==1;
   FCitixChaseRules::RefillIce(Activated+100,Reserve,Next);Passed &= Reserve==2 && Next==0;
   CP->IceCharges=2;CP->NextIceAt=0;CP->ForceNetUpdate();
   Finish(TEXT("LMB, 30% cut, engine/boost lock, recovery, recharge, reserve cap and city advertisements"));
  }
 }
}

void CitixIceVisualProbeTick(ACitixDrivingPlayerController* PC)
{
 if (!PC->IsLocalController())return;
 auto* World=PC->GetWorld(); auto* S=World->GetGameState<ACitixChaseGameState>();
 auto* PS=PC->GetPlayerState<ACitixChasePlayerState>(); if(!S || !PS)return;
 FString Tag;FParse::Value(FCommandLine::Get(),TEXT("CitixNetTag="),Tag);
 static bool Saw=false,Shot=false,Recovered=false,ClientPassed=true; static float FreezeSeen=0;
 if(PS->ChaseRole==ECitixChaseRole::Runner && PS->FrozenUntil>S->GetServerWorldTimeSeconds()) {
  if(!Saw){Saw=true;FreezeSeen=S->GetServerWorldTimeSeconds();}
  if(auto* Car=Cast<ACitixVehiclePawn>(PC->GetPawn())) {
   Car->ServerSendDriveInput(1,0,false,true);
   if(S->GetServerWorldTimeSeconds()-FreezeSeen>.15f)ClientPassed &= Car->GetVehicleMovement()->bIceFrozen && !Car->GetVehicleMovement()->IsBoosting();
  }
 } else if (Saw && !Recovered) {
  Recovered=true;
  FFileHelper::SaveStringToFile(ClientPassed ? TEXT("{\"passed\":true,\"replicated_freeze\":true,\"released\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/(TEXT("ChaseIceClient-")+Tag+TEXT(".json"))));
  UE_LOG(LogCitix,Log,TEXT("[CitixIceClient] %s replicated freeze and release"),ClientPassed?TEXT("PASS"):TEXT("FAIL"));
 }
 for(TActorIterator<ACitixIceWave> It(World);It;++It) {
  const float Age=S->GetServerWorldTimeSeconds()-It->StartedAt;
  if(!Shot && It->FrozenTarget && Age>.36f && Age<.8f){Shot=true;FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("IceWave-")+Tag+TEXT(".png")),true,false);}
 }
 // Render one building screen and one freestanding sign using the actual scene.
 static int32 Stage=0;static float At=0;static TWeakObjectPtr<ACameraActor> Camera;
 if(!Recovered && PS->ChaseRole==ECitixChaseRole::Runner)return;
 if(!Shot)return;
 if(Stage==0 || Stage==2) {
  if(Stage==0 && S->GetServerWorldTimeSeconds()<PS->LastIceAt+5.f)return;
  for(TActorIterator<ACitixYienBillboard> It(World);It;++It)if(It->bBuildingScreen==(Stage==0)) {
   Camera=World->SpawnActor<ACameraActor>();
   const FVector Look=It->GetActorLocation(), P=Look+It->GetActorForwardVector()*1900+FVector(0,0,140);
   Camera->SetActorLocation(P); Camera->SetActorRotation((Look-P).Rotation());Camera->GetCameraComponent()->SetFieldOfView(65);
   PC->SetViewTarget(Camera.Get());At=World->GetTimeSeconds();++Stage;break;
  }
 } else if(Stage==1 || Stage==3) {
  if(World->GetTimeSeconds()-At>1.f) {
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/((Stage==1?TEXT("YienScreen-"):TEXT("YienBillboard-"))+Tag+TEXT(".png")),true,false);
   ++Stage; At=World->GetTimeSeconds();
  }
 } else if(Stage==4 && World->GetTimeSeconds()-At>.2f) { PC->SetViewTarget(PC->GetPawn());if(Camera.IsValid())Camera->Destroy();++Stage;
 }
}
