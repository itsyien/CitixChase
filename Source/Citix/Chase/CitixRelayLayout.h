#pragma once
#include "CoreMinimal.h"
/** Server selects safe road points; clients receive the authoritative result. */
struct FCitixRelayLayout
{
 static TArray<FVector> Select(const TArray<FVector>& Candidates,int32 Seed);
 static constexpr float MinimumSpacing=18000.f;
};
