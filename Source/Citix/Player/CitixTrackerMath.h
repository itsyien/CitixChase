#pragma once
#include "CoreMinimal.h"
struct FCitixTrackerMath {
 static float BearingDegrees(const FVector& Delta,float CameraYaw);
 static float SmoothBearing(float Current,float Target,float Dt);
 static int32 Elevation(const FVector& Delta);
 static bool CanTrack(bool Chaser,bool Revealed,bool HasPawn,bool Pursuit);
};
