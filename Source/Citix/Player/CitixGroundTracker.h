#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixGroundTracker.generated.h"
class UInstancedStaticMeshComponent;
UCLASS(NotBlueprintable)
class CITIX_API ACitixGroundTracker : public AActor
{
 GENERATED_BODY()
public:
 ACitixGroundTracker();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 void Track(APawn* Pawn);
 static constexpr float Radius = 320.f;
private:
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Arrow;
 TWeakObjectPtr<APawn> Target;
 float LastTrackAt=-1.f, Bearing=0.f;
 bool bInitialized=false, bProbeReported=false;
};
