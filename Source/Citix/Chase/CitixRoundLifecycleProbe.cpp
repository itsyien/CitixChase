#include "Chase/CitixRoundLifecycleProbe.h"
#include "Chase/CitixChaseGameState.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "InputCoreTypes.h"
#include "Citix.h"

ACitixRoundLifecycleProbe::ACitixRoundLifecycleProbe()
{
 bReplicates=true;
 bOnlyRelevantToOwner=true;
 PrimaryActorTick.bCanEverTick=true;
}

void ACitixRoundLifecycleProbe::DumpState()
{
 if (HasAuthority()) ServerAcknowledgePawn_Implementation(ObservedPawn.Get());
}

void ACitixRoundLifecycleProbe::ServerAcknowledgePawn_Implementation(ACitixVehiclePawn* Pawn)
{
 const auto* PC=Cast<APlayerController>(GetOwner());
 bAcknowledged=IsValid(Pawn) && PC && PC->GetPawn()==Pawn && Pawn->GetController()==PC && Pawn==ObservedPawn.Get();
 UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] client possession ack controller=%s pawn=%s valid=%d"),*GetNameSafe(PC),*GetNameSafe(Pawn),bAcknowledged);
 UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] observed throttle=%d steer=%d handbrake=%d reverse=%d moved=%d moveIgnored=%d lookIgnored=%d"),bThrottle,bSteer,bHandbrake,bReverse,bMoved,PC ? PC->IsMoveInputIgnored() : -1,PC ? PC->IsLookInputIgnored() : -1);
 if (Pawn) {
  const auto* Body=Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
  const auto* Movement=Pawn->GetVehicleMovement();
  UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] chassis location=%s simulating=%d occupied=%d maxspeed=%.0f hull=%.0f destroyed=%d"),*Pawn->GetActorLocation().ToCompactString(),Body && Body->IsSimulatingPhysics(),Pawn->IsOccupied(),Movement->MaxSpeed,Movement->GetHealth(),Pawn->IsDisplayDestroyed());
  for (int32 Index=0; Index<Movement->GetWheelStates().Num(); ++Index)
  {
   const auto& Wheel=Movement->GetWheelStates()[Index];
   UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] wheel=%d grounded=%d compression=%.3f centre=%s"),Index,Wheel.bGrounded,Wheel.CompressionRatio,*Wheel.WorldLocation.ToCompactString());
   if (!bMoved && Body && Movement->GetWheelSetups().IsValidIndex(Index))
   {
    const auto& Setup=Movement->GetWheelSetups()[Index];
    const FVector Start=Body->GetComponentTransform().TransformPosition(Setup.AttachOffset);
    const FVector End=Start-FVector::UpVector*(Movement->SuspensionRestLength+Setup.Radius);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CitixLifecycleWheelTrace),false,Pawn);
    FHitResult Hit;
    const bool Grounded=GetWorld()->LineTraceSingleByChannel(Hit,Start,End,Movement->GroundTraceChannel,Query);
    UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] wheel trace=%d driven=%d start=%s end=%s hit=%d actor=%s component=%s distance=%.2f"),Index,Setup.bDriven,*Start.ToCompactString(),*End.ToCompactString(),Grounded,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.Distance);
   }
  }
  if (!bMoved && Body) {
   TArray<FOverlapResult> Overlaps;
   FCollisionQueryParams Params(SCENE_QUERY_STAT(CitixLifecycleSpawnOverlap),false,Pawn);
   GetWorld()->OverlapMultiByObjectType(Overlaps,Body->GetComponentLocation(),Body->GetComponentQuat(),FCollisionObjectQueryParams::AllObjects,Body->GetCollisionShape(),Params);
   int32 Logged=0;
   for (const auto& Hit:Overlaps) if (Hit.bBlockingHit && Logged++<8) UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] spawn blocker actor=%s component=%s destroying=%d"),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.GetActor() && Hit.GetActor()->IsActorBeingDestroyed());
  }
 }
}

void ACitixRoundLifecycleProbe::Tick(float Dt)
{
 Super::Tick(Dt);
 auto* PC=Cast<APlayerController>(GetOwner());
 auto* Car=PC ? Cast<ACitixVehiclePawn>(PC->GetPawn()) : nullptr;
 const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>();
 if (!PC || !Car || !S) return;
 if (ObservedPawn.Get()!=Car) {
  ObservedPawn=Car; StartLocation=Car->GetActorLocation(); LocalSeconds=0; InputStep=0;
  bThrottle=bSteer=bHandbrake=bReverse=bMoved=bAcknowledged=bVerified=false;
 }
 if (S->Phase!=ECitixChasePhase::Pursuit || bVerified) return;
 if (HasAuthority()) {
  const auto* Movement=Car->GetVehicleMovement();
  bThrottle|=Movement->GetThrottleInput()>.5f;
  bSteer|=FMath::Abs(Movement->GetSteeringInput())>.2f;
  bHandbrake|=Movement->IsHandbrakeEngaged();
  bReverse|=Movement->GetThrottleInput()<-.5f;
  bMoved|=FVector::Dist2D(StartLocation,Car->GetActorLocation())>100.f;
  if (bThrottle && bSteer && bHandbrake && bReverse && bMoved && bAcknowledged) {
   bVerified=true; ++RoundsPassed;
   UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] PASS driver=%s round=%d pawn=%s throttle steer brake handbrake movement possession verified; total=%d"),*GetNameSafe(PC),S->RoundNumber,*GetNameSafe(Car),RoundsPassed);
  }
 }
 if (!PC->IsLocalController()) return;
 LocalSeconds+=Dt;
 auto Key=[PC](FKey K,bool Press){PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Press ? IE_Pressed : IE_Released,Press ? 1.f : 0.f));};
 if (InputStep==0 && LocalSeconds>1.f) {Key(EKeys::W,true); ++InputStep;}
 else if (InputStep==1 && LocalSeconds>2.f) {Key(EKeys::D,true); ++InputStep;}
 else if (InputStep==2 && LocalSeconds>2.7f) {Key(EKeys::D,false); Key(EKeys::W,false); Key(EKeys::SpaceBar,true); ++InputStep;}
 else if (InputStep==3 && LocalSeconds>3.5f) {Key(EKeys::SpaceBar,false); Key(EKeys::S,true); ++InputStep;}
 else if (InputStep==4 && LocalSeconds>4.3f) {Key(EKeys::S,false); ++InputStep;}
 else if (InputStep==5 && LocalSeconds>5.f) {ServerAcknowledgePawn(Car); ++InputStep;}
}
