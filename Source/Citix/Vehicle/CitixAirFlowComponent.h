#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CitixAirFlowComponent.generated.h"
class UInstancedStaticMeshComponent;
class UMaterialInterface;
UCLASS(ClassGroup=(Citix),meta=(BlueprintSpawnableComponent))
class CITIX_API UCitixAirFlowComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UCitixAirFlowComponent();
 virtual void BeginPlay() override;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
 static float StrengthForSpeed(float SpeedKmh,float TopSpeedKmh);
 float GetStrength() const {return Strength;}
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ribbons;
private:
 UPROPERTY() TObjectPtr<UMaterialInterface> RibbonMaterial;
 float Strength=0.f;
};
