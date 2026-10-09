#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixHillsideBuilder.generated.h"

class UProceduralMeshComponent;
struct FCitixHillsideLayout;

// Locally generated static world, driven by the same authored map data on peers.
UCLASS(NotBlueprintable)
class CITIX_API ACitixHillsideBuilder : public AActor
{
 GENERATED_BODY()
public:
 ACitixHillsideBuilder();
 void Build(const FCitixHillsideLayout& Layout);
 const TArray<FVector>& GetLandmarkLocations() const { return LandmarkLocations; }
 const TArray<FVector>& GetLampLocations() const { return LampLocations; }
private:
 void BuildDetails(const FCitixHillsideLayout& Layout);
 UPROPERTY() TArray<TObjectPtr<class UHierarchicalInstancedStaticMeshComponent>> Details;
 TArray<FVector> LandmarkLocations,LampLocations;
 UPROPERTY() TObjectPtr<UProceduralMeshComponent> Roads;
 UPROPERTY() TObjectPtr<UProceduralMeshComponent> Terrain;
 UPROPERTY() TObjectPtr<UProceduralMeshComponent> Tunnel;
 UPROPERTY() TObjectPtr<UProceduralMeshComponent> Water;
};
