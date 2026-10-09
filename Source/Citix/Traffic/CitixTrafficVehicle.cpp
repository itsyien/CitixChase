// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/CitixTrafficVehicle.h"

#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"

ACitixTrafficVehicle::ACitixTrafficVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
 // Initial false replication values need not trigger a RepNotify. Match the
 // actual actor state before any appearance builds a blocking body at origin.
 SetActorHiddenInGame(true);
 SetActorEnableCollision(false);

	// Multiplayer stage 1: server drives, everyone interpolates. Cull far
	// copies so distant traffic costs no bandwidth.
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(15.f);
	SetNetCullDistanceSquared(70000.f * 70000.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

void ACitixTrafficVehicle::BeginPlay()
{
	Super::BeginPlay();

	// Remote copies build from the replicated look (spawn-bunch values land first).
	if (!bInitialized && !HasAuthority() && BuildSeed != 0)
	{
		InitializeVehicle(CarType, PaintColor, BuildSeed);
	}
}

void ACitixTrafficVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixTrafficVehicle, Motion);
 DOREPLIFETIME(ACitixTrafficVehicle, CarType);
	DOREPLIFETIME(ACitixTrafficVehicle, PaintColor);
	DOREPLIFETIME(ACitixTrafficVehicle, BuildSeed);
}

void ACitixTrafficVehicle::InitializeVehicle(ECitixCarType InType, const FLinearColor& Color, int32 Seed)
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;
	CarType = InType;
	PaintColor = Color;
	BuildSeed = Seed;

	// Scene root is on the ground; the car library works in ground-space too.
	CarVisual = FCitixCarLibrary::BuildCar(this, SceneRoot, CarType, Color,
		/*bCollision*/ true, /*bIncludeWheels*/ true, /*bHighDetail*/ false);
 SetActorHiddenInGame(!bVisibleNet);
 SetActorEnableCollision(bVisibleNet);
}

void ACitixTrafficVehicle::OnRep_Appearance()
{
	// Replicated look arrives with the spawn bunch; build once on remotes.
	// (Guarded: BeginPlay may already have built from initial values.)
	if (!bInitialized && BuildSeed != 0)
	{
		InitializeVehicle(CarType, PaintColor, BuildSeed);
	}
}

void ACitixTrafficVehicle::SetVehicleVisible(bool bVisible)
{
 const bool bChanged=bVisibleNet != bVisible;
 if (bChanged) { ++Motion.Generation; History.Reset(); }
 bVisibleNet = bVisible;
 Motion.bVisible=bVisible;
	SetActorHiddenInGame(!bVisible);
	SetActorEnableCollision(bVisible);
 if(bChanged && HasAuthority()) {
  PublishMotion(0.f);
  ForceNetUpdate();
 }
}

void ACitixTrafficVehicle::OnRep_Visible()
{
	SetActorHiddenInGame(!bVisibleNet);
 SetActorEnableCollision(bVisibleNet);
}

void ACitixTrafficVehicle::SetWheelsVisible(bool bVisible)
{
	if (bInitialized)
	{
		CarVisual.SetWheelsVisible(bVisible);
	}
}

void ACitixTrafficVehicle::PublishMotion(float SpeedKmh)
{
 if (!HasAuthority()) return;
 const float Now=GetWorld()->GetTimeSeconds();
 const FVector P=GetActorLocation();
 Motion.Velocity=SpeedKmh>0.f && Now>Motion.Timestamp && FVector::DistSquared(P,Motion.Location)<FMath::Square(2000.f) ? (P-Motion.Location)/(Now-Motion.Timestamp) : FVector::ZeroVector;
 Motion.Location=P; Motion.Rotation=GetActorRotation(); Motion.Timestamp=Now; Motion.SpeedKmh=SpeedKmh;
}
void ACitixTrafficVehicle::OnRep_Motion()
{
 const bool bVisibilityChanged=bVisibleNet != Motion.bVisible;
 if(bVisibilityChanged) History.Reset();
 bVisibleNet=Motion.bVisible;
 if(!bVisibleNet) {
  History.Reset();
  OnRep_Visible();
  return;
 }
 if (!History.IsEmpty() && (History.Last().Generation != Motion.Generation || FVector::DistSquared(History.Last().Location,Motion.Location)>FMath::Square(2000.f))) History.Reset();
 // Snap a new pool lifecycle before enabling its collider. Interpolating from
 // the previous lifecycle creates a car/blocker travelling across open roads.
 if (History.IsEmpty()) SetActorLocationAndRotation(Motion.Location,Motion.Rotation);
 if (History.IsEmpty() || Motion.Timestamp>History.Last().Timestamp) History.Add(Motion);
 if (History.Num()>12) History.RemoveAt(0);
 OnRep_Visible();
}
FCitixTrafficSnapshot ACitixTrafficVehicle::SampleMotion(const TArray<FCitixTrafficSnapshot>& Samples,float Time)
{
 if (Samples.IsEmpty()) return {};
 FCitixTrafficSnapshot Result=Samples.Last();
 if (Samples.Num()>1 && Time<=Result.Timestamp) {
  int32 I=1; while (I<Samples.Num()-1 && Samples[I].Timestamp<Time) ++I;
  const auto& A=Samples[I-1]; const auto& B=Samples[I];
  const float Alpha=FMath::Clamp((Time-A.Timestamp)/FMath::Max(.001f,B.Timestamp-A.Timestamp),0.f,1.f);
  Result.Location=FMath::Lerp(A.Location,B.Location,Alpha); Result.Rotation=FQuat::Slerp(A.Rotation.Quaternion(),B.Rotation.Quaternion(),Alpha).Rotator();
 } else Result.Location+=Result.Velocity*FMath::Clamp(Time-Result.Timestamp,0.f,.1f);
 return Result;
}
void ACitixTrafficVehicle::Tick(float Dt)
{
 Super::Tick(Dt);
 if (HasAuthority()) { if (Motion.Timestamp<=0.f || !Motion.Location.Equals(GetActorLocation(),.1f) || !Motion.Rotation.Equals(GetActorRotation(),.1f)) PublishMotion(0.f); return; }
 if (!bVisibleNet || History.IsEmpty()) return;
 const AGameStateBase* GS=GetWorld()->GetGameState();
 const float T=(GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds())-.1f;
 while (History.Num()>2 && History[1].Timestamp<=T) History.RemoveAt(0);
 const FCitixTrafficSnapshot Pose=SampleMotion(History,T);
 SetActorLocationAndRotation(Pose.Location,Pose.Rotation);
}
