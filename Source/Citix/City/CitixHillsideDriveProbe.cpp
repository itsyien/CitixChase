#include "City/CitixHillsideVisualProbe.h"
#include "City/CitixHillsideLayout.h"
#include "City/CitixCityGenerator.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Traffic/CitixTrafficSystem.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "InputCoreTypes.h"
#include "Citix.h"

void CitixHillsideDriveProbeTick(ACitixDrivingPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
 auto* World=PC->GetWorld(); auto* State=World->GetGameState<ACitixChaseGameState>();
 auto* PS=PC->GetPlayerState<ACitixChasePlayerState>();
 if(!State || !PS) return;
 struct FDrive {TWeakObjectPtr<UWorld> World; TArray<FVector> Points; FString Name; int32 Next=1; float Start=0,Logged=0,Stopped=0; bool Done=false,W=false,S=false,A=false,D=false;};
 static FDrive Drive;
 if(Drive.World.Get()!=World) {Drive=FDrive(); Drive.World=World;}
 const float Now=World->GetTimeSeconds();
 if(!State->bHillsideMap)
 {
  if(PC->HasAuthority() && State->PlayerArray.Num()==2 && Now>15)
  {
   bool Verified=true;
   for(APlayerState* Player:State->PlayerArray) if(auto* Other=Cast<ACitixChasePlayerState>(Player)) Verified&=Other->bCityIdentityValid;
   if(Verified) World->GetAuthGameMode<ACitixChaseGameMode>()->SelectLobbyMap(PC,true);
  }
  return;
 }
 if(State->Phase==ECitixChasePhase::Waiting && PS->bCityIdentityValid && !PS->bReady && Now-Drive.Logged>.5f) {PC->ServerChaseInteract(); Drive.Logged=Now;}
 if(!PC->HasAuthority() || State->Phase!=ECitixChasePhase::Pursuit || Drive.Done) return;
 auto* Car=Cast<ACitixVehiclePawn>(PC->GetPawn()); if(!Car) return;
 if(Drive.Points.IsEmpty())
 {
  const auto Layout=FCitixHillsideLayout::Build(); int32 Route=4;
  FParse::Value(FCommandLine::Get(),TEXT("CitixDriveRoute="),Route);
  if(!Layout.Routes.IsValidIndex(Route)) return;
  Drive.Points=Layout.Routes[Route].Points; Drive.Name=Layout.Routes[Route].Name;
  int32 StartNode=0; FParse::Value(FCommandLine::Get(),TEXT("CitixDriveStartNode="),StartNode);
  Drive.Points.RemoveAt(0,FMath::Clamp(StartNode,0,Drive.Points.Num()-2));
  // Isolated road-handling fixture: one initial setup pose, then keyboard only.
  // Live traffic flow and driving with traffic are separate required checks.
  for(TActorIterator<ACitixTrafficSystem> It(World);It;++It) It->Destroy();
  for(TActorIterator<ACitixTrafficVehicle> It(World);It;++It) It->Destroy();
  FTransform Pose;
  const float Yaw=(Drive.Points[1]-Drive.Points[0]).Rotation().Yaw;
  if(!ACitixCityGenerator::ValidateChaseSurface(World,Drive.Points[0]+FVector(0,0,100),FVector(240,110,85),Yaw,Car,Pose,false)) return;
  Car->SetRoundStartPose(Pose); Drive.Start=Drive.Logged=Now;
  UE_LOG(LogCitix,Log,TEXT("[CitixHillsideDrive] START route=%s points=%d isolated-traffic=1 initial-pose=%s"),*Drive.Name,Drive.Points.Num(),*Pose.GetLocation().ToCompactString());
 }
 auto Key=[&](FKey K,bool Press,bool& Previous){if(Press!=Previous) {PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Press ? IE_Pressed : IE_Released,Press ? 1.f : 0.f)); Previous=Press;}};
 const FVector Location=Car->GetActorLocation();
 while(Drive.Next<Drive.Points.Num() && FVector::Dist2D(Location,Drive.Points[Drive.Next])<300.f) ++Drive.Next;
 if(Drive.Next>=Drive.Points.Num())
 {
  Key(EKeys::W,false,Drive.W); Key(EKeys::S,false,Drive.S); Key(EKeys::A,false,Drive.A); Key(EKeys::D,false,Drive.D); Drive.Done=true;
  UE_LOG(LogCitix,Log,TEXT("[CitixHillsideDrive] PASS route=%s seconds=%.1f endpoint=%s health=%.1f"),*Drive.Name,Now-Drive.Start,*Location.ToCompactString(),Car->GetVehicleMovement()->GetHealth()); return;
 }
 const FVector Segment=Drive.Points[Drive.Next]-Drive.Points[Drive.Next-1];
 const float LengthXY=Segment.Size2D();
 const float Along=FMath::Clamp(float(FVector2D::DotProduct(FVector2D(Location-Drive.Points[Drive.Next-1]),FVector2D(Segment))/FMath::Max(1.f,LengthXY)),0.f,LengthXY);
 FVector Target=FMath::Lerp(Drive.Points[Drive.Next-1],Drive.Points[Drive.Next],FMath::Min(1.f,(Along+600.f)/FMath::Max(1.f,LengthXY)));
 float Ahead=FMath::Max(0.f,600.f-(LengthXY-Along));
 for(int32 I=Drive.Next;I+1<Drive.Points.Num() && Ahead>0;++I)
 {
  const float Length=FVector::Dist2D(Drive.Points[I],Drive.Points[I+1]);
  Target=FMath::Lerp(Drive.Points[I],Drive.Points[I+1],FMath::Min(1.f,Ahead/FMath::Max(1.f,Length)));
  Ahead-=Length;
 }
 const float Error=FMath::FindDeltaAngleDegrees(Car->GetActorRotation().Yaw,(Target-Location).Rotation().Yaw);
 const float Speed=Car->GetVehicleMovement()->GetSpeedKmh();
 float Desired=FMath::Abs(Error)>12 ? 25.f : 40.f;
 if(FMath::Abs(Error)>35.f) Desired=10.f;
 if(Drive.Next+1<Drive.Points.Num())
 {
  const float Turn=FMath::Abs(FMath::FindDeltaAngleDegrees((Drive.Points[Drive.Next]-Drive.Points[Drive.Next-1]).Rotation().Yaw,(Drive.Points[Drive.Next+1]-Drive.Points[Drive.Next]).Rotation().Yaw));
  if(Turn>45.f && FVector::Dist2D(Location,Drive.Points[Drive.Next])<2500.f) Desired=10.f;
 }
 Key(EKeys::A,Error< -2.f,Drive.A); Key(EKeys::D,Error>2.f,Drive.D);
 Key(EKeys::W,Speed<Desired-1.f,Drive.W); Key(EKeys::S,Speed>Desired+3.f,Drive.S);
 if(Speed>2) Drive.Stopped=Now;
 if(Now-Drive.Logged>5)
 {
  int32 Grounded=0; for(const auto& Wheel:Car->GetVehicleMovement()->GetWheelStates()) Grounded+=Wheel.bGrounded;
  UE_LOG(LogCitix,Log,TEXT("[CitixHillsideDrive] progress=%d/%d speed=%.1f error=%.1f grounded=%d location=%s"),Drive.Next,Drive.Points.Num(),Speed,Error,Grounded,*Location.ToCompactString()); Drive.Logged=Now;
 }
 if(Now-Drive.Start>15 && Now-Drive.Stopped>12)
 {
  Key(EKeys::W,false,Drive.W); Key(EKeys::S,false,Drive.S); Key(EKeys::A,false,Drive.A); Key(EKeys::D,false,Drive.D); Drive.Done=true;
  UE_LOG(LogCitix,Error,TEXT("[CitixHillsideDrive] STUCK route=%s point=%d location=%s"),*Drive.Name,Drive.Next,*Location.ToCompactString());
 }
#endif
}
