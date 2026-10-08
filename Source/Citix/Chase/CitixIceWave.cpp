#include "Chase/CitixIceWave.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChaseRules.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACitixIceWave::ACitixIceWave()
{
 bReplicates=true; bAlwaysRelevant=true; SetNetUpdateFrequency(10.f);
 PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
 auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("WaveOrigin")); SetRootComponent(Root);
 Scan=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ScanArc")); Scan->SetupAttachment(Root);
 Crystals=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("IceCrystals")); Crystals->SetupAttachment(Root);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Crystal(TEXT("/Engine/BasicShapes/Cube.Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ice(TEXT("/Game/Citix/Materials/M_CitixIceScan.M_CitixIceScan"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> FrostAsset(TEXT("/Game/Citix/Materials/M_CitixIceFrost.M_CitixIceFrost"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShellAsset(TEXT("/Game/Citix/Materials/M_CitixIceShell.M_CitixIceShell"));
 FrostMaster=FrostAsset.Object; ShellMaster=ShellAsset.Object;
 for (auto* Part : {Scan.Get(),Crystals.Get()}) {
  Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetCastShadow(false); Part->SetCanEverAffectNavigation(false);
  Part->NumCustomDataFloats=2; if (Ice.Succeeded()) Part->SetMaterial(0,Ice.Object);
 }
 Scan->SetStaticMesh(Plane.Object); Crystals->SetStaticMesh(Crystal.Object);
}
void ACitixIceWave::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME(ACitixIceWave,StartedAt); DOREPLIFETIME(ACitixIceWave,EmitterCar); DOREPLIFETIME(ACitixIceWave,FrozenTarget);
}
void ACitixIceWave::BeginPlay()
{
 Super::BeginPlay(); if (HasAuthority()) SetLifeSpan(4.f);
 if (GetNetMode()==NM_DedicatedServer) return;
 for (int32 I=0;I<96;++I) Scan->AddInstance(FTransform(FVector::ZeroVector));
 for (int32 I=0;I<48;++I) Crystals->AddInstance(FTransform(FVector::ZeroVector));
 LastFrostPose=GetActorTransform();
}
void ACitixIceWave::StampFrost(const FTransform& Pose,float Radius,int32 Step)
{
 if (!FrostMaster || Step<=LastFrostStep || Frost.Num()>=24) return;
 const FVector Origin=Pose.GetLocation(), Forward=Pose.GetRotation().GetForwardVector();
 // Include the distance swept by the moving/turning emitter since the previous
 // stamp, so fast cars and lower frame rates do not leave holes between bands.
 const float Travel=FVector::Dist2D(Origin,LastFrostPose.GetLocation());
 const float Turn=FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward,LastFrostPose.GetRotation().GetForwardVector()),-1.f,1.f));
 const float InnerRadius=Frost.IsEmpty() ? 0.f : FMath::Max(0.f,LastFrostRadius-Travel-Turn*Radius-140.f);
 auto* D=NewObject<UDecalComponent>(this); D->SetupAttachment(GetRootComponent());
 D->SetWorldLocation(Origin+Forward*(Radius*.5f)+FVector(0,0,260));
 D->SetWorldRotation(FRotator(-90.f,Pose.Rotator().Yaw,0));
 D->DecalSize=FVector(700,Radius*.87f+80.f,Radius*.5f+80.f); D->FadeScreenSize=.001f;
 auto* MID=UMaterialInstanceDynamic::Create(FrostMaster,this);
 MID->SetVectorParameterValue(TEXT("Origin"),FLinearColor(Origin.X,Origin.Y,Origin.Z,0));
 MID->SetVectorParameterValue(TEXT("Forward"),FLinearColor(Forward.X,Forward.Y,0,0));
 MID->SetScalarParameterValue(TEXT("Radius"),Radius);
 MID->SetScalarParameterValue(TEXT("InnerRadius"),InnerRadius);
 MID->SetScalarParameterValue(TEXT("Fade"),1.f);
 D->SetDecalMaterial(MID); D->RegisterComponent(); Frost.Add(D); FrostMaterials.Add(MID);
 LastFrostStep=Step; LastFrostRadius=Radius; LastFrostPose=Pose;
}
void ACitixIceWave::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (GetNetMode()==NM_DedicatedServer) return;
 const auto* State=GetWorld()->GetGameState<ACitixChaseGameState>();
 const float Age=(State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds())-StartedAt;
 if (Age<0 || StartedAt<=0.f) { Scan->SetVisibility(false); Crystals->SetVisibility(false); return; }
 // Only the scan and its leading crystals follow the current bumper pose.
 // The actor root and every deposited decal stay fixed in world space.
 if (IsValid(EmitterCar)) {
  const FVector Forward=EmitterCar->GetActorForwardVector().GetSafeNormal2D();
  const FTransform Pose(Forward.Rotation(),EmitterCar->GetActorLocation()+Forward*240.f);
  Scan->SetWorldTransform(Pose); Crystals->SetWorldTransform(Pose);
 }
 const float Progress=FMath::Clamp(Age/SweepSeconds,0.f,1.f);
 // Fast initial burst, then a smooth slowdown as the scan reaches its range.
 const float Radius=160.f+3840.f*FMath::InterpEaseOut(0.f,1.f,Progress,3.f);
 const float Opacity=(1.f-FMath::SmoothStep(.8f,1.2f,Age))*FMath::SmoothStep(0.f,.06f,Age);
 Scan->SetVisibility(Opacity>0); Crystals->SetVisibility(Opacity>0);
 if (Opacity>0) {
 for (int32 I=0;I<64;++I) {
  const float Angle=-60.f+(I+.5f)*120.f/64.f;
  const FVector P=FRotator(0,Angle,0).Vector()*Radius;
  const float Length=Radius*FMath::DegreesToRadians(120.f/64.f)*1.12f;
  Scan->UpdateInstanceTransform(I,FTransform(FRotator(0,Angle,0),P+FVector(0,0,-45),FVector(1.25f,Length*.01f,1)),false,false,true);
  Scan->SetCustomDataValue(I,0,Opacity,false); Scan->SetCustomDataValue(I,1,0.f,false);
 }
 for (int32 I=0;I<32;++I) {
  const float Angle=-58.f+I*116.f/31.f;
  const float Height=65.f+FMath::Sin(I*2.31f+Age*8)*20.f;
  Scan->UpdateInstanceTransform(64+I,FTransform(FRotator(90,Angle,0),FRotator(0,Angle,0).Vector()*(Radius-30)+FVector(0,0,Height*.5f-40),FVector(Height*.01f,Radius*.00072f,1)),false,false,true);
  Scan->SetCustomDataValue(64+I,0,Opacity*.34f,false); Scan->SetCustomDataValue(64+I,1,1.f,false);
 }
 for (int32 I=0;I<48;++I) {
  const float Angle=-59.f+I*118.f/47.f, Lift=30+70*FMath::Frac(I*.618f+Age*.6f);
  Crystals->UpdateInstanceTransform(I,FTransform(FRotator(45,Angle+Age*40,45),FRotator(0,Angle,0).Vector()*(Radius-50-FMath::Frac(I*.31f)*110)+FVector(0,0,Lift-35),FVector(.035f,.035f,.09f)),false,false,true);
  Crystals->SetCustomDataValue(I,0,Opacity*.65f,false); Crystals->SetCustomDataValue(I,1,2.f,false);
 }
 Scan->MarkRenderStateDirty(); Crystals->MarkRenderStateDirty();
 }
 const int32 FrostStep=FMath::Clamp(FMath::CeilToInt(Progress*24.f),1,24);
 StampFrost(Scan->GetComponentTransform(),Radius,FrostStep);
 // Stamp origin/heading/radius are immutable. Only their opacity changes as ice thaws.
 const float FrostFade=1.f-FMath::SmoothStep(1.3f,3.4f,Age);
 for (UMaterialInstanceDynamic* MID:FrostMaterials) {
  MID->SetScalarParameterValue(TEXT("Fade"),FrostFade);
 }
 if (FrozenTarget && !bShellCreated) {
  bShellCreated=true;
  auto* Material=ShellMaster.Get();
  if (Material) {
   ShellMaterial=UMaterialInstanceDynamic::Create(Material,this);
   TArray<UStaticMeshComponent*> Parts; FrozenTarget->GetComponents(Parts);
   for (auto* Part:Parts) if (Part->GetStaticMesh() && Part->IsVisible() && !Part->GetName().Contains(TEXT("Tracker")) && !Part->GetName().Contains(TEXT("Explosion"))) {
    auto* Shell=NewObject<UStaticMeshComponent>(this); Shell->SetStaticMesh(Part->GetStaticMesh());
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision); Shell->SetCastShadow(false); Shell->SetReceivesDecals(false);
    Shell->AttachToComponent(Part,FAttachmentTransformRules::KeepRelativeTransform); Shell->SetRelativeScale3D(FVector(1.012f));
    for(int32 Slot=0;Slot<Shell->GetNumMaterials();++Slot) Shell->SetMaterial(Slot,ShellMaterial);
    Shell->RegisterComponent(); FrozenShell.Add(Shell);
   }
  }
 }
 if (ShellMaterial) ShellMaterial->SetScalarParameterValue(TEXT("Fade"),1-FMath::SmoothStep(2.5f,3.f,Age));
}
