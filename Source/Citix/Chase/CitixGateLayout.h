#pragma once
#include "CoreMinimal.h"
#include "City/CitixRoadNetwork.h"

struct FCitixGateCandidate
{
 FTransform Surface;
 int32 Edge=0;
 float Along=0;
 float Width=1200;
};

/** Select spread-out road gates; nearby arms of a junction share one slot. */
struct FCitixGateLayout
{
 static TArray<int32> Select(const FCitixRoadNetwork& Roads,const TArray<FCitixGateCandidate>& Candidates);
};
