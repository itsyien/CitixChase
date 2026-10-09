#include "Vehicle/CitixAirFlowComponent.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Chase/CitixChaseRules.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// One shared, immutable mesh: eight curved sheets rather than dozens of moving
// rectangles. Tips are actual geometry, so the silhouette remains a cutting blade.
UStaticMesh* GetAirCutMesh()
{
 static TWeakObjectPtr<UStaticMesh> SharedMesh;
 if (SharedMesh.IsValid()) return SharedMesh.Get();
 UStaticMesh* Mesh=NewObject<UStaticMesh>(GetTransientPackage(),NAME_None,RF_Transient);
 FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
 Attributes.GetVertexInstanceUVs().SetNumChannels(1);
 auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
 auto Tangents=Attributes.GetVertexInstanceTangents(); auto BinormalSigns=Attributes.GetVertexInstanceBinormalSigns();
 auto Colours=Attributes.GetVertexInstanceColors(); auto UVs=Attributes.GetVertexInstanceUVs();
 const auto Group=Description.CreatePolygonGroup(); Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("AirCut");
 constexpr int32 Segments=24;
 for (int32 Ribbon=0;Ribbon<8;++Ribbon)
 {
  const int32 Layer=Ribbon/2; const float Side=Ribbon%2 ? 1.f : -1.f;
  const bool Roof=Layer==3;
  const float Length=Layer==0 ? 1150.f : Layer==1 ? 900.f : Layer==2 ? 830.f : 1100.f;
  const float Width=Layer==0 ? 180.f : Layer==1 ? 10.f : Layer==2 ? 16.f : 190.f;
  const FLinearColor Tint=(Layer==1 || Layer==2) ? FLinearColor(.78f,.96f,1.f) : FLinearColor(.015f,.30f,.65f);
  FVector P[Segments+1][2];
  for (int32 I=0;I<=Segments;++I)
  {
   const float T=I/static_cast<float>(Segments);
   const float Bow=FMath::Sin(T*PI);
   // A broad shoulder flows around the bonnet/cabin into a long fine wake.
   const float Taper=(I==0 || I==Segments) ? 0.f : FMath::Pow(FMath::Max(0.f,FMath::Sin(T*PI)),.85f)*(1.f-.62f*T);
   const float RoofBow=FMath::Sin(FMath::Min(T*(Roof ? 1.5f : 1.35f),1.f)*PI*.75f);
   const float Y=Layer==0 ? 80.f+115.f*Bow*(1.f-.3f*T) : Layer==1 ? 100.f+80.f*Bow : Layer==2 ? 55.f+30.f*Bow : 35.f+50.f*Bow;
   const float Z=Layer==0 ? -30.f+65.f*Bow : Layer==1 ? -5.f+25.f*Bow : Layer==2 ? 5.f+110.f*RoofBow : 10.f+155.f*RoofBow;
   const FVector Center((Layer==0 ? 350.f : 310.f)-Length*T,Side*Y,Z);
   const FVector Across=Roof ? FVector(0,Side*.95f,.3f) : Layer==2 ? FVector(0,Side*.7f,.7f) : FVector(0,Side*.35f,.94f);
   for (int32 Edge=0;Edge<2;++Edge) P[I][Edge]=Center+Across*(Edge ? 1.f : -1.f)*Width*Taper*.5f;
  }
  for (int32 I=0;I<Segments;++I) for (int32 Triangle=0;Triangle<2;++Triangle)
  {
   if ((I==0 && Triangle==1) || (I==Segments-1 && Triangle==0)) continue;
   const int32 Rows[2][3]={{I,I+1,I+1},{I,I+1,I}};
   const int32 Edges[2][3]={{0,0,1},{0,1,1}};
   const FVector Normal=FVector::CrossProduct(P[Rows[Triangle][1]][Edges[Triangle][1]]-P[I][0],P[Rows[Triangle][2]][Edges[Triangle][2]]-P[I][0]).GetSafeNormal();
   TArray<FVertexInstanceID> Instances;
   for (int32 V=0;V<3;++V)
   {
    const int32 Row=Rows[Triangle][V],Edge=Edges[Triangle][V];
    const auto Vertex=Description.CreateVertex(); Positions[Vertex]=FVector3f(P[Row][Edge]);
    const auto Instance=Description.CreateVertexInstance(Vertex);
    Normals[Instance]=FVector3f(Normal);
    Tangents[Instance]=FVector3f((P[I+1][0]-P[I][0]).GetSafeNormal()); BinormalSigns[Instance]=1.f;
    Colours[Instance]=FVector4f(Tint.R,Tint.G,Tint.B,1.f);
    UVs.Set(Instance,0,FVector2f(Row/static_cast<float>(Segments),static_cast<float>(Edge))); Instances.Add(Instance);
   }
   Description.CreatePolygon(Group,Instances);
  }
 }
 Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr,TEXT("AirCut")));
 UStaticMesh::FBuildMeshDescriptionsParams Build; Build.bBuildSimpleCollision=false; Build.bFastBuild=true; Build.bCommitMeshDescription=false; Build.bMarkPackageDirty=false;
 if (!Mesh->BuildFromMeshDescriptions({&Description},Build)) return nullptr;
 SharedMesh=Mesh;
 return Mesh;
}
}

UCitixAirFlowComponent::UCitixAirFlowComponent()
{
 PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostPhysics;
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Citix/Materials/M_CitixAirFlow.M_CitixAirFlow"));
 RibbonMaterial=Material.Object;
}
float UCitixAirFlowComponent::StrengthForSpeed(float SpeedKmh,float TopSpeedKmh)
{
 if (!FMath::IsFinite(SpeedKmh) || !FMath::IsFinite(TopSpeedKmh) || TopSpeedKmh<=0 || SpeedKmh<=TopSpeedKmh*.8f) return 0;
 return FMath::GetMappedRangeValueClamped(FVector2D(.8f,1.f),FVector2D(0.f,1.f),SpeedKmh/TopSpeedKmh);
}
void UCitixAirFlowComponent::BeginPlay()
{
 Super::BeginPlay();
 if (GetNetMode()==NM_DedicatedServer || !RibbonMaterial || !GetOwner()) return;
 AddTickPrerequisiteActor(GetOwner());
 Ribbons=NewObject<UInstancedStaticMeshComponent>(GetOwner(),TEXT("AirflowRibbons"));
 Ribbons->SetupAttachment(GetOwner()->GetRootComponent());
 RibbonMesh=GetAirCutMesh();
 Ribbons->SetStaticMesh(RibbonMesh);
 Ribbons->SetMaterial(0,RibbonMaterial); Ribbons->NumCustomDataFloats=2;
 Ribbons->SetCollisionEnabled(ECollisionEnabled::NoCollision); Ribbons->SetCastShadow(false);
 Ribbons->SetCanEverAffectNavigation(false); Ribbons->SetGenerateOverlapEvents(false);
 Ribbons->RegisterComponent(); Ribbons->SetVisibility(false);
 Ribbons->AddInstance(FTransform::Identity);
}
void UCitixAirFlowComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(Dt,TickType,TickFunction);
 auto* Car=Cast<ACitixVehiclePawn>(GetOwner());
 if (!Car || !Ribbons) return;
 // Use the car's configured nominal cap. Temporary ice/gate slowdowns must not
 // move the 80% trigger down and make a slow car look like it is cutting air.
 const float TopSpeed=(Car->bChasePerformanceApplied ? FCitixChaseRules::SpeedLimit(Car->bChaseRunner) : FCitixCarLibrary::GetPerformance(Car->GetCarType()).MaxSpeed)*.036f;
 if (FittedCarType!=static_cast<uint8>(Car->GetCarType())) {
  float Length,Width,Height; FCitixCarLibrary::GetFootprint(Car->GetCarType(),Length,Width,Height);
  Ribbons->SetRelativeScale3D(FVector(Length/225.f,Width/91.f,Height/140.f));
  FittedCarType=static_cast<uint8>(Car->GetCarType());
 }
 const float Target=Car->IsOccupied() && !Car->IsDisplayDestroyed() ? StrengthForSpeed(Car->GetDisplaySpeedKmh(),TopSpeed) : 0.f;
 Strength=FMath::FInterpTo(Strength,Target,Dt,Target>Strength ? 8.f : 12.f);
 if (Target<=0.f && Strength<.005f) Strength=0;
 Ribbons->SetVisibility(Strength>0);
 if (Strength==0.f) SubmittedStrength=-1.f;
 // Gentle flow runs in the material; only upload a visible intensity change.
 if (Strength>0 && FMath::Abs(Strength-SubmittedStrength)>.002f) {
  for (int32 I=0;I<Ribbons->GetInstanceCount();++I) Ribbons->SetCustomDataValue(I,0,Strength,false);
  Ribbons->MarkRenderStateDirty();
  SubmittedStrength=Strength;
 }
}
