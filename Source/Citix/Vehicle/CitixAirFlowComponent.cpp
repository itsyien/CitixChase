#include "Vehicle/CitixAirFlowComponent.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Chase/CitixChaseRules.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UCitixAirFlowComponent::UCitixAirFlowComponent()
{
 PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostPhysics;
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Citix/Materials/M_CitixAirFlow.M_CitixAirFlow"));
 RibbonMaterial=Material.Object;
}
float UCitixAirFlowComponent::StrengthForSpeed(float SpeedKmh,float TopSpeedKmh)
{
 if (TopSpeedKmh<=0 || SpeedKmh<TopSpeedKmh*.9f) return 0;
 return FMath::GetMappedRangeValueClamped(FVector2D(.9f,1.f),FVector2D(.25f,1.f),SpeedKmh/TopSpeedKmh);
}
void UCitixAirFlowComponent::BeginPlay()
{
 Super::BeginPlay();
 if (GetNetMode()==NM_DedicatedServer || !RibbonMaterial || !GetOwner()) return;
 AddTickPrerequisiteActor(GetOwner());
 Ribbons=NewObject<UInstancedStaticMeshComponent>(GetOwner(),TEXT("AirflowRibbons"));
 Ribbons->SetupAttachment(GetOwner()->GetRootComponent());
 Ribbons->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));
 Ribbons->SetMaterial(0,RibbonMaterial); Ribbons->NumCustomDataFloats=2;
 Ribbons->SetCollisionEnabled(ECollisionEnabled::NoCollision); Ribbons->SetCastShadow(false);
 Ribbons->SetCanEverAffectNavigation(false); Ribbons->SetGenerateOverlapEvents(false);
 Ribbons->RegisterComponent(); Ribbons->SetVisibility(false);
 auto Point=[](int32 Ribbon,float T) {
  const float Bow=FMath::Sin(T*PI);
  return FVector(245.f-570.f*T,Ribbon==2 ? 0.f : (Ribbon==0 ? -1.f : 1.f)*(80.f+60.f*Bow),Ribbon==2 ? 82.f+35.f*Bow : 18.f+25.f*Bow);
 };
 for (int32 Ribbon=0;Ribbon<3;++Ribbon) for (int32 I=0;I<12;++I) {
  const FVector A=Point(Ribbon,I/12.f),B=Point(Ribbon,(I+1)/12.f);
  Ribbons->AddInstance(FTransform((B-A).Rotation(),(A+B)*.5f,FVector(FVector::Distance(A,B)*.011f,Ribbon==2 ? .10f : .16f,1)));
  Ribbons->SetCustomDataValue(Ribbon*12+I,1,I*.5f+Ribbon*.8f,false);
 }
}
void UCitixAirFlowComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(Dt,TickType,TickFunction);
 auto* Car=Cast<ACitixVehiclePawn>(GetOwner());
 if (!Car || !Ribbons) return;
 const float TopSpeed=(Car->bChasePerformanceApplied ? FCitixChaseRules::SpeedLimit(Car->bChaseRunner) : FCitixCarLibrary::GetPerformance(Car->GetCarType()).MaxSpeed)*.036f;
 const float Target=Car->IsOccupied() && !Car->IsDisplayDestroyed() ? StrengthForSpeed(Car->GetDisplaySpeedKmh(),TopSpeed) : 0.f;
 Strength=FMath::FInterpTo(Strength,Target,Dt,Target>Strength ? 8.f : 12.f);
 if (Strength<.005f) Strength=0;
 Ribbons->SetVisibility(Strength>0);
 // Time-based pulses run in the material; only upload a visible intensity change.
 if (Strength>0 && FMath::Abs(Strength-SubmittedStrength)>.002f) {
  for (int32 I=0;I<Ribbons->GetInstanceCount();++I) Ribbons->SetCustomDataValue(I,0,Strength,false);
  Ribbons->MarkRenderStateDirty();
  SubmittedStrength=Strength;
 }
}
