#include "Player/CitixTrackerMath.h"
float FCitixTrackerMath::BearingDegrees(const FVector& Delta,float CameraYaw) {
 if(Delta.SizeSquared2D()<1.f) return 0;
 return FMath::UnwindDegrees(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X))-CameraYaw);
}
float FCitixTrackerMath::SmoothBearing(float Current,float Target,float Dt) {
 return FMath::UnwindDegrees(Current+FMath::FindDeltaAngleDegrees(Current,Target)*(1.f-FMath::Exp(-24.f*FMath::Max(0.f,Dt))));
}
int32 FCitixTrackerMath::Elevation(const FVector& Delta) {return FMath::Abs(Delta.Z)>400.f ? (Delta.Z>0 ? 1 : -1) : 0;}
bool FCitixTrackerMath::CanTrack(bool Chaser,bool Revealed,bool HasPawn,bool Pursuit) {return Chaser && Revealed && HasPawn && Pursuit;}
