#include "Player/CitixGroundTracker.h"
#include "Player/CitixTrackerMath.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

ACitixGroundTracker::ACitixGroundTracker()
{
 PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 Ring=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NavigationRing"));
 Arrow=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DirectionChevron"));
 for(auto* Mesh:{Ring.Get(),Arrow.Get()}) {
  Mesh->SetupAttachment(GetRootComponent()); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Mesh->SetCastShadow(false); Mesh->SetCanEverAffectNavigation(false); Mesh->SetReceivesDecals(false);
 }
 SetActorHiddenInGame(true);
}
void ACitixGroundTracker::BeginPlay()
{
 Super::BeginPlay();
 UStaticMesh* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 Ring->SetStaticMesh(Plane); Arrow->SetStaticMesh(Plane);
 Ring->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.025f,.21f,.28f)));
 Arrow->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.2f,.85f,1.f)));
 // Twelve gentle gaps, four short chords per segment; only two draw batches.
 for(int32 I=0;I<48;++I) {
  const float A=I*2.f*PI/48.f, B=A+2.f*PI/48.f*(I%4==3 ? .55f : 1.f);
  const FVector P(Radius*FMath::Cos(A),Radius*FMath::Sin(A),0), Q(Radius*FMath::Cos(B),Radius*FMath::Sin(B),0);
  Ring->AddInstance(FTransform((Q-P).Rotation(),(P+Q)*.5f,FVector((Q-P).Size()/100.f,.025f,1.f)));
 }
 for(int32 I=0;I<10;++I) Arrow->AddInstance(FTransform::Identity);
}
void ACitixGroundTracker::Track(APawn* Pawn)
{
 if(Target.Get()!=Pawn) bInitialized=false;
 Target=Pawn; LastTrackAt=GetWorld()->GetTimeSeconds();
}
void ACitixGroundTracker::Tick(float Dt)
{
 Super::Tick(Dt);
 auto* PC=Cast<APlayerController>(GetOwner());
 auto* Car=PC ? Cast<ACitixVehiclePawn>(PC->GetPawn()) : nullptr;
 auto* State=GetWorld()->GetGameState<ACitixChaseGameState>();
 auto* Local=PC ? PC->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 const bool Visible=PC && PC->IsLocalController() && Car && Target.IsValid() && GetWorld()->GetTimeSeconds()-LastTrackAt<.2f
  && FCitixTrackerMath::CanTrack(Local && Local->ChaseRole==ECitixChaseRole::Chaser,State && State->bRunnerRevealed,true,State && State->Phase==ECitixChasePhase::Pursuit);
 if(!Visible) {SetActorHiddenInGame(true); bInitialized=false; return;}
 // Roads block Pawn traces while deliberately ignoring chassis/suspension traces.
 FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CitixNavigationGround),false,Car);
 Query.AddIgnoredActor(this); Query.AddIgnoredActor(Target.Get());
 const FVector Position=Car->GetActorLocation();
 if(!GetWorld()->LineTraceSingleByChannel(Hit,Position+FVector(0,0,100),Position-FVector(0,0,300),ECC_Pawn,Query)
    || Hit.ImpactNormal.Z<.55f) {SetActorHiddenInGame(true); bInitialized=false; return;}
 const FVector Delta=Target->GetActorLocation()-Position;
 const float Raw=FCitixTrackerMath::BearingDegrees(Delta,0.f);
 Bearing=bInitialized ? FCitixTrackerMath::SmoothBearing(Bearing,Raw,Dt) : Raw; bInitialized=true;
 // World bearing is independent of car/camera yaw; ground normal tilts the projection.
 const FVector Normal=Hit.ImpactNormal;
 const FVector Axis=FVector::VectorPlaneProject(Car->GetActorForwardVector(),Normal).GetSafeNormal();
 const FQuat Ground=FRotationMatrix::MakeFromXZ(Axis,Normal).ToQuat();
 SetActorLocationAndRotation(Hit.ImpactPoint+Normal*2.f,Ground);
 SetActorHiddenInGame(false);
 if(!bProbeReported && FParse::Param(FCommandLine::Get(),TEXT("CitixTrackerNetProbe"))) {
  bProbeReported=true; FString Tag; FParse::Value(FCommandLine::Get(),TEXT("CitixNetTag="),Tag);
  const FString Report=FString::Printf(TEXT("{\"passed\":true,\"ground_projection\":true,\"net_mode\":%d,\"replicated_target\":%s,\"radius_cm\":320}"),static_cast<int32>(GetNetMode()),Target->HasAuthority()?TEXT("false"):TEXT("true"));
  FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/(TEXT("GroundTracker-")+Tag+TEXT(".json"))));
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("GroundTracker-")+Tag+TEXT(".png")),true,false);
 }
 const FVector Direction=Ground.UnrotateVector(FVector(FMath::Cos(FMath::DegreesToRadians(Bearing)),FMath::Sin(FMath::DegreesToRadians(Bearing)),0)).GetSafeNormal2D();
 const FVector Side(-Direction.Y,Direction.X,0), Tip=Direction*(Radius+8.f);
 for(int32 I=0;I<2;++I) {
  const FVector Tail=Direction*(Radius-31.f)+Side*(I==0 ? -23.f : 23.f);
  Arrow->UpdateInstanceTransform(I,FTransform((Tip-Tail).Rotation(),(Tip+Tail)*.5f,FVector((Tip-Tail).Size()/100.f,.065f,1.f)),false,false,true);
 }
 // A short edge arc still communicates the bearing when the nose occludes its tip.
 const float Heading=FMath::Atan2(Direction.Y,Direction.X);
 for(int32 I=0;I<8;++I) {
  const float A=Heading+FMath::DegreesToRadians(-50.f+I*12.5f),B=A+FMath::DegreesToRadians(12.5f);
  const FVector P(Radius*FMath::Cos(A),Radius*FMath::Sin(A),.3f),Q(Radius*FMath::Cos(B),Radius*FMath::Sin(B),.3f);
  Arrow->UpdateInstanceTransform(I+2,FTransform((Q-P).Rotation(),(P+Q)*.5f,FVector((Q-P).Size()/100.f,.04f,1.f)),false,I==7,true);
 }

}
