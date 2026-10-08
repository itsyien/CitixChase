#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixIceWave.generated.h"
class UInstancedStaticMeshComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UMaterialInterface;

/** Car-local scan with immutable world-space frost stamps; clients share a server start time. */
UCLASS()
class CITIX_API ACitixIceWave : public AActor
{
 GENERATED_BODY()
public:
 ACitixIceWave();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
 UPROPERTY(Replicated) float StartedAt=0.f;
 UPROPERTY(Replicated) TObjectPtr<AActor> EmitterCar;
 UPROPERTY(Replicated) TObjectPtr<AActor> FrozenTarget;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Scan;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Crystals;
 static constexpr float SweepSeconds=.85f;
private:
 UPROPERTY() TObjectPtr<UMaterialInterface> FrostMaster;
 UPROPERTY() TObjectPtr<UMaterialInterface> ShellMaster;
 UPROPERTY() TArray<TObjectPtr<UDecalComponent>> Frost;
 UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FrostMaterials;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FrozenShell;
 UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;
 bool bShellCreated=false;
 int32 LastFrostStep=0;
 float LastFrostRadius=160.f;
 FTransform LastFrostPose;
 void StampFrost(const FTransform& Pose,float Radius,int32 Step);
};
