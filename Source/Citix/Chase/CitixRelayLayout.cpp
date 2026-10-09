#include "Chase/CitixRelayLayout.h"
#include "Chase/CitixChaseRules.h"
TArray<FVector> FCitixRelayLayout::Select(const TArray<FVector>& Candidates,int32 Seed)
{
 if(Candidates.IsEmpty()) return {};
 FBox Bounds(ForceInit); for(const FVector& P:Candidates) Bounds+=P;
 const FVector Centre=Bounds.GetCenter();
 const float InnerRadius=FMath::Max(Bounds.GetSize().X,Bounds.GetSize().Y)*.2f;
 TArray<int32> Outer;
 for(int32 I=0;I<Candidates.Num();++I) if(FVector::Dist2D(Candidates[I],Centre)>=InnerRadius) Outer.Add(I);
 if(Outer.Num()<FCitixChaseRules::ActiveRelayCount) return {};
 FRandomStream Random(Seed);
 // Randomized farthest-point sampling spreads objectives around the perimeter.
 // Retry first points rather than silently accepting a clustered fallback.
 for(int32 Attempt=0;Attempt<32;++Attempt) {
  TArray<FVector> Result; Result.Add(Candidates[Outer[Random.RandRange(0,Outer.Num()-1)]]);
  while(Result.Num()<FCitixChaseRules::ActiveRelayCount) {
   int32 Pick=INDEX_NONE; float Best=-1;
   for(int32 Index:Outer) {
    float Nearest=FLT_MAX;
    for(const FVector& P:Result) Nearest=FMath::Min(Nearest,static_cast<float>(FVector::Dist2D(P,Candidates[Index])));
    if(Nearest<MinimumSpacing) continue;
    const float Score=Nearest*Random.FRandRange(.85f,1.15f);
    if(Score>Best) {Best=Score; Pick=Index;}
   }
   if(Pick==INDEX_NONE) break;
   Result.Add(Candidates[Pick]);
  }
  if(Result.Num()==FCitixChaseRules::ActiveRelayCount) return Result;
 }
 return {};
}
