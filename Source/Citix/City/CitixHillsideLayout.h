#pragma once

#include "CoreMinimal.h"
#include "City/CitixRoadNetwork.h"
#include "Core/CitixCitySettings.h"

struct FCitixHillsideRoute
{
 FString Name;
 TArray<FVector> Points;
 float Width=1300.f;
 bool bTunnel=false;
 bool bBridge=false;
};

struct FCitixHillsideHouse
{
 FVector Position;
 FVector2D Size;
 float Yaw=0;
};

// Authored data shared by generation, route validation and multiplayer identity.
// Coordinates and widths are centimetres. Cosmetic randomness never changes it.
struct FCitixHillsideLayout
{
 static constexpr int32 Revision=4;
 FCitixHillsideSettings Settings;
 int32 Seed=47821;
 TArray<FVector2D> CoastBoundary;
 TArray<FCitixHillsideRoute> Routes;
 TArray<FVector> RelaySites;
 TArray<FVector> Exits;
 TArray<FVector> Spawns;
 TArray<FVector> RecoveryParking;
 TArray<FCitixHillsideHouse> Houses;
 TArray<FVector> Landmarks;
 static FCitixHillsideLayout Build(int32 Seed=47821);
 float BaseTerrainHeight(FVector2D XY) const;
 bool ContainsLand(FVector2D XY) const;
 FCitixRoadNetwork BuildRoadNetwork() const;
};
