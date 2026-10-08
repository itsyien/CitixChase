#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "CitixSmokeCloud.generated.h"
class UInstancedStaticMeshComponent;
UCLASS()
class CITIX_API ACitixSmokeCloud : public AActor
{
 GENERATED_BODY()
public:
 ACitixSmokeCloud();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
 UPROPERTY(Replicated) float StartedAt=0.f;
 UPROPERTY(Replicated) int32 CloudSeed=1;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Billows;
 UPROPERTY(Replicated) TArray<FVector_NetQuantize> EmissionPositions;
 TWeakObjectPtr<AController> EmitterController;
 static constexpr float DiameterScale=1.3f;
 static constexpr int32 PuffCount=100;
 static constexpr float HorizontalSpread=1.5f;
 static constexpr float ReleaseSeconds=5.f;
 static constexpr float Lifetime=15.f;
};
