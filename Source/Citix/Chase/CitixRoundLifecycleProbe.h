#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixRoundLifecycleProbe.generated.h"

class ACitixVehiclePawn;

/** Opt-in test actor: owner injects actual keys, server observes accepted movement. */
UCLASS(NotBlueprintable)
class CITIX_API ACitixRoundLifecycleProbe : public AActor
{
 GENERATED_BODY()
public:
 ACitixRoundLifecycleProbe();
 virtual void Tick(float DeltaSeconds) override;
 bool IsRoundVerified() const { return bVerified; }
 int32 VerifiedRounds() const { return RoundsPassed; }
 void DumpState();
private:
 UFUNCTION(Server,Reliable) void ServerAcknowledgePawn(ACitixVehiclePawn* Pawn);
 TWeakObjectPtr<ACitixVehiclePawn> ObservedPawn;
 FVector StartLocation=FVector::ZeroVector;
 float LocalSeconds=0.f;
 int32 InputStep=0;
 int32 RoundsPassed=0;
 bool bThrottle=false, bSteer=false, bHandbrake=false, bReverse=false, bMoved=false, bAcknowledged=false, bVerified=false;
};
