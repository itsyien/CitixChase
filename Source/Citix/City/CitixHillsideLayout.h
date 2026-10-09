#pragma once

#include "CoreMinimal.h"
#include "City/CitixRoadNetwork.h"

struct FCitixHillsideRoute
{
 FString Name;
 TArray<FVector> Points;
 float Width=1300.f;
 bool bTunnel=false;
};

// Authored data shared by generation, route validation and multiplayer identity.
// Coordinates and widths are centimetres. Cosmetic randomness never changes it.
struct FCitixHillsideLayout
{
 static constexpr int32 Revision=3;
 TArray<FCitixHillsideRoute> Routes;
 TArray<FVector> RelaySites;
 TArray<FVector> Exits;
 TArray<FVector> Spawns;
 TArray<FVector> RecoveryParking;
 static FCitixHillsideLayout Build();
 FCitixRoadNetwork BuildRoadNetwork() const;
};
