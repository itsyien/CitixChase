#include "Chase/CitixSmokeCloud.h"
#include "Chase/CitixChaseGameState.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACitixSmokeCloud::ACitixSmokeCloud()
{
 bReplicates=true; bAlwaysRelevant=true; NetUpdateFrequency=10.f; SetCanBeDamaged(false);
 PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.05f;
 Billows=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SmokeBillows")); SetRootComponent(Billows);
 Billows->SetMobility(EComponentMobility::Movable);
 Billows->SetCollisionEnabled(ECollisionEnabled::NoCollision); Billows->SetCastShadow(false);
 Billows->SetCanEverAffectNavigation(false); Billows->SetReceivesDecals(false);
 Billows->NumCustomDataFloats=2;
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Game/Citix/Meshes/SM_CitixSmokeBillow.SM_CitixSmokeBillow"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Citix/Materials/M_CitixChaseSmoke.M_CitixChaseSmoke"));
 Billows->SetStaticMesh(Plane.Object); if (Material.Succeeded()) Billows->SetMaterial(0,Material.Object);
}
void ACitixSmokeCloud::BeginPlay()
{
 Super::BeginPlay();
 if (HasAuthority() && EmitterController.IsValid()) EmitterPlayerState=EmitterController->PlayerState;
 // Shader is authored by the editor commandlet; hard reference cooks it into the game.
 if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Citix/Materials/M_CitixChaseSmoke.M_CitixChaseSmoke"))) Billows->SetMaterial(0,Material);
 for (int32 I=0; I<PuffCount; ++I) Billows->AddInstance(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector(.001f)));
 if (HasAuthority()) SetLifeSpan(Lifetime+.2f);
}
void ACitixSmokeCloud::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ACitixSmokeCloud,StartedAt); DOREPLIFETIME(ACitixSmokeCloud,CloudSeed); DOREPLIFETIME(ACitixSmokeCloud,EmissionPositions); DOREPLIFETIME(ACitixSmokeCloud,EmitterPlayerState);
}
float ACitixSmokeCloud::OpacityForViewer(const APlayerState* Viewer) const
{
 return Viewer && EmitterPlayerState==Viewer ? 1.f/3.f : 1.f;
}
float ACitixSmokeCloud::PuffOpacityForViewer(float PuffOpacity,const APlayerState* Viewer) const
{
 if (!Viewer || EmitterPlayerState!=Viewer) return PuffOpacity;
 // Budget optical depth across the entire convex billow cloud. Multiplying
 // every puff by 1/3 still becomes opaque when dozens overlap. This instead
 // bounds the composite of all 100 front-facing puffs to a one-third veil.
 return 1.f-FMath::Pow(2.f/3.f,FMath::Clamp(PuffOpacity,0.f,1.f)/PuffCount);
}
void ACitixSmokeCloud::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 const auto* State=GetWorld()->GetGameState<ACitixChaseGameState>();
 const float Age=(State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds())-StartedAt;
 // Sample the authoritative pawn throughout release; previous puffs stay at their birth positions.
 if (HasAuthority() && Age>=0.f) {
  bool Added=false;
  while (EmissionPositions.Num()<PuffCount && EmissionPositions.Num()*ReleaseSeconds/PuffCount<=FMath::Min(Age,ReleaseSeconds)) {
   const APawn* Emitter=EmitterController.IsValid() ? EmitterController->GetPawn() : nullptr;
   if (!Emitter || (Age>ReleaseSeconds && EmissionPositions.Num()==0)) break;
   EmissionPositions.Add(Emitter->GetActorLocation()); Added=true;
  }
  if (Added) ForceNetUpdate();
 }
 if (Age<0.f) return;
 const APlayerState* ViewerState=nullptr;
 // Rendering stays client-local; replicated puff positions and gameplay coverage
 // always retain the full cloud. Late PlayerState resolution is picked up next tick.
 for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It) {
  const APlayerController* Viewer=It->Get();
  if (Viewer && Viewer->IsLocalController()) { ViewerState=Viewer->PlayerState; break; }
 }
 FRandomStream Random(CloudSeed);
 for (int32 I=0; I<PuffCount; ++I) {
  const float Birth=I*ReleaseSeconds/PuffCount;
  const float Angle=Random.FRandRange(0,2*PI), Radius=FMath::Sqrt(Random.FRand())*550.f*DiameterScale*HorizontalSpread;
  const float Size=Random.FRandRange(650,900)*DiameterScale, Height=Random.FRandRange(100,200)*DiameterScale, Spin=Random.FRandRange(-PI,PI);
  const float Growth=EmissionPositions.IsValidIndex(I) ? FMath::SmoothStep(Birth,Birth+1.2f,Age) : 0.f;
  const float Fade=1.f-FMath::SmoothStep(Birth+8.f,Birth+10.f,Age);
  const FVector Origin=EmissionPositions.IsValidIndex(I) ? FVector(EmissionPositions[I]) : GetActorLocation();
  const FVector Offset(FMath::Cos(Angle)*Radius*Growth,FMath::Sin(Angle)*Radius*Growth,Height+FMath::Min(65.f,FMath::Max(0.f,Age-Birth)*6.f));
  const FVector Local=Origin+Offset-GetActorLocation();
  const FQuat Rotation=FQuat(FVector::UpVector,Spin+FMath::Max(0.f,Age-Birth)*.04f);
  Billows->UpdateInstanceTransform(I,FTransform(Rotation,Local,FVector(1.15f,1.15f,.55f)*(Size*.01f*FMath::Max(.001f,Growth))),false,false,true);
  Billows->SetCustomDataValue(I,0,PuffOpacityForViewer(Growth*Fade,ViewerState),false); Billows->SetCustomDataValue(I,1,I*.731f,false);
 }
 Billows->MarkRenderStateDirty();
}

// Match the deterministic billow layout, growth and fade used above. No transient arrays.
bool ACitixSmokeCloud::ContainsPoint(const FVector& Point,float ServerNow) const {
 return IntersectsSightline(Point,Point,ServerNow);
}

bool ACitixSmokeCloud::IntersectsSightline(const FVector& From,const FVector& To,float ServerNow) const {
 const float Age=ServerNow-StartedAt; if(Age<0 || Age>=Lifetime) return false;
 FRandomStream Random(CloudSeed);
 for(int32 I=0;I<PuffCount;++I) {
  const float Birth=I*ReleaseSeconds/PuffCount;
  const float Angle=Random.FRandRange(0,2*PI),Radius=FMath::Sqrt(Random.FRand())*550.f*DiameterScale*HorizontalSpread;
  const float Size=Random.FRandRange(650,900)*DiameterScale,Height=Random.FRandRange(100,200)*DiameterScale;
  Random.FRandRange(-PI,PI); // Consume the visual spin so subsequent billows match.
  if(!EmissionPositions.IsValidIndex(I)) continue;
  const float Growth=FMath::SmoothStep(Birth,Birth+1.2f,Age),Fade=1-FMath::SmoothStep(Birth+8.f,Birth+10.f,Age);
  if(Growth*Fade<.2f) continue;
  const FVector Center=FVector(EmissionPositions[I])+FVector(FMath::Cos(Angle)*Radius*Growth,FMath::Sin(Angle)*Radius*Growth,Height+FMath::Min(65.f,FMath::Max(0.f,Age-Birth)*6.f));
  const float Horizontal=Size*.575f*Growth,Vertical=Size*.275f*Growth;
  // Normalize the ellipsoid to a unit sphere and test the finite segment.
  // This also handles an observer/target inside a puff and a zero-length segment.
  const FVector Scale(Horizontal,Horizontal,Vertical);
  const FVector Start=(From-Center)/Scale,End=(To-Center)/Scale;
  if(FMath::ClosestPointOnSegment(FVector::ZeroVector,Start,End).SizeSquared()<=1.f) return true;
 }
 return false;
}
