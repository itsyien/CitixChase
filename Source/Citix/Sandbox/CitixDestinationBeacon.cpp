// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixDestinationBeacon.h"

#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixChasePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Net/UnrealNetwork.h"

ACitixDestinationBeacon::ACitixDestinationBeacon()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
	bReplicates = true;
	SetReplicateMovement(true);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(Root);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetCanEverAffectNavigation(false);
	BeaconMesh->SetCastShadow(false);
	BeaconMesh->SetReceivesDecals(false);
	BeaconMesh->SetRelativeScale3D(FVector(3.f, 3.f, 60.f));
	BeaconMesh->SetVisibility(false, true);
	StationFrame = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StationFrame"));
	GateEdges = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GateEdges"));
	StationLights = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StationLights"));
	for (auto* Parts : {StationFrame.Get(), StationLights.Get(), GateEdges.Get()})
	{
		Parts->SetupAttachment(Root);
		Parts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Parts->SetCanEverAffectNavigation(false);
		Parts->SetVisibility(false);
	}
}

void ACitixDestinationBeacon::BeginPlay()
{
	Super::BeginPlay();

	if (BeaconMesh)
	{
		if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
		{
			BeaconMesh->SetStaticMesh(Mesh);
		}
	}
	OnRep_BeamVisible();
}

void ACitixDestinationBeacon::SetBreakawayStation(const FVector& Location,int32 Index,float Width)
{
	bBreakawayStation = true; GateWidth=Width;
 PrimaryActorTick.TickInterval=.1f;
	bAlwaysRelevant = true;
	StationIndex = Index;
	SetDestination(Location, ECitixSurface::EmissiveWarm);
	ForceNetUpdate();
}

void ACitixDestinationBeacon::SetExitProjection(const FVector& Location) { bExitProjection=true; SetRelayProjection(Location); }

void ACitixDestinationBeacon::SetRelayProjection(const FVector& Location)
{
 bRelayProjection=true; bAlwaysRelevant=true;
 SetDestination(Location,ECitixSurface::EmissiveCool); ForceNetUpdate();
}

void ACitixDestinationBeacon::SetDestination(const FVector& Location, ECitixSurface Surface)
{
	SetActorLocation(Location + FVector(0.f, 0.f, 3000.f));
	BeamSurface = Surface;
	bBeamVisible = true;
	if (BeaconMesh)
	{
		if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(Surface))
		{
			BeaconMesh->SetMaterial(0, Material);
		}
		BeaconMesh->SetVisibility(true, true);
	}
	OnRep_BeamVisible();
}

void ACitixDestinationBeacon::Hide()
{
	bBeamVisible = false;
	if (BeaconMesh)
	{
		BeaconMesh->SetVisibility(false, true);
	}
 StationFrame->SetVisibility(false); StationLights->SetVisibility(false); if(bRelayProjection) GateEdges->SetVisibility(false);
}

void ACitixDestinationBeacon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixDestinationBeacon, bBeamVisible);
	DOREPLIFETIME(ACitixDestinationBeacon, BeamSurface);
	DOREPLIFETIME(ACitixDestinationBeacon, bBreakawayStation);
	DOREPLIFETIME(ACitixDestinationBeacon, StationIndex); DOREPLIFETIME(ACitixDestinationBeacon,GateWidth); DOREPLIFETIME(ACitixDestinationBeacon,bRelayProjection); DOREPLIFETIME(ACitixDestinationBeacon,bExitProjection);
}

void ACitixDestinationBeacon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
 if (bBreakawayStation || bRelayProjection) {
  const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>();
  const auto* PC=GetWorld()->GetFirstPlayerController();
  const auto* PS=PC ? PC->GetPlayerState<ACitixChasePlayerState>() : nullptr;
  const bool Chaser=PS && PS->ChaseRole==ECitixChaseRole::Chaser;
  const bool Cooling=S && S->BreakawayReadyAt>S->GetServerWorldTimeSeconds();
  const FLinearColor Color=bRelayProjection ? bExitProjection ? FLinearColor(.05f,.6f,.32f) : FLinearColor(.02f,1.5f,4.f) : Chaser ? FLinearColor(4.f,.015f,.03f) : Cooling ? FLinearColor(.12f,.14f,.16f) : FLinearColor(4.f,2.f,.025f);
  if (Color!=LastGlow) {
   LastGlow=Color;
   if (auto* Material=Cast<UMaterialInstanceDynamic>(StationLights->GetMaterial(0))) { Material->SetVectorParameterValue(TEXT("Color"),Color); Material->SetVectorParameterValue(TEXT("BaseColor"),Color); }
  }
  // The ground symbols change locally with the viewer's role.
  if (bBreakawayStation && (!bSymbolsInitialized || bLastChaser!=Chaser)) {
   bSymbolsInitialized=true; bLastChaser=Chaser;
   for (int32 I=0; I<6; ++I) {
   const int32 Pair=I/2; const float Side=I%2==0 ? -1.f : 1.f;
   const float Angle=Chaser ? Side*45.f : -Side*45.f;
   StationLights->UpdateInstanceTransform(6+I,FTransform(FRotator(0,Angle,0),FVector((Pair-1)*240.f+(Chaser ? 0.f : -70.f),Chaser ? 0.f : Side*70.f,-3085.f),FVector(2.f,.18f,.05f)),false,I==5);
  }
  }
  if (bRelayProjection) StationLights->SetRelativeRotation(FRotator(0,FMath::Fmod(S ? S->GetServerWorldTimeSeconds()*18.f : 0.f,360.f),0));
 }

	// Server-side fuse; destruction replicates.
	if (HasAuthority() && bBeamVisible && BeamLife > 0.f)
	{
		BeamLife -= DeltaSeconds;
		if (BeamLife <= 0.f)
		{
			Destroy();
		}
	}
}

void ACitixDestinationBeacon::ShowRespawnBeam(const FVector& Location, float Duration)
{
	// Tall and blue: visible across the district (same pillar convention as
	// SetDestination: mesh centred 30 m up, twice the job beacon's girth).
	SetActorLocation(Location + FVector(0.f, 0.f, 3000.f));
	if (BeaconMesh)
	{
		BeaconMesh->SetRelativeScale3D(FVector(6.f, 6.f, 120.f));
		BeaconMesh->SetVisibility(true, true);
	}
	bBeamVisible = true;
	BeamLife = Duration;
	OnRep_BeamVisible();
}

void ACitixDestinationBeacon::OnRep_BeamVisible()
{
	if (!BeaconMesh)
	{
		return;
	}
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(BeamSurface))
	{
		BeaconMesh->SetMaterial(0, Material);
	}
	BeaconMesh->SetVisibility(bBeamVisible, true);
	if (bBreakawayStation)
	{
		BeaconMesh->SetVisibility(false);
  PrimaryActorTick.TickInterval=.1f;
  StationFrame->SetCullDistances(0,0); StationLights->SetCullDistances(0,0);
  StationLights->SetCastShadow(false);
		StationFrame->ClearInstances(); StationLights->ClearInstances(); GateEdges->ClearInstances(); LastGlow=FLinearColor::Transparent; bSymbolsInitialized=false;
		if (StationFrame->GetInstanceCount() == 0)
		{
			UStaticMesh* Cube = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);
			StationFrame->SetStaticMesh(Cube);
			StationLights->SetStaticMesh(Cube);
			StationFrame->SetMaterial(0, FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface::Trim,FLinearColor(.025f,.04f,.065f)));
			auto* Glow = UMaterialInstanceDynamic::Create(FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveWarm)->GetMaterial(), this);
			if (Glow) { Glow->SetVectorParameterValue(TEXT("Color"),FLinearColor(6.f,3.f,.04f)); Glow->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(6.f,3.f,.04f)); StationLights->SetMaterial(0,Glow); }
			// Only edge posts collide: the overhead visual must not intercept dry-road traces.
			const float Ground = -3090.f; const float SideOffset=GateWidth*.5f+40.f;
   StationFrame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   GateEdges->SetStaticMesh(Cube); GateEdges->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); GateEdges->SetCollisionResponseToAllChannels(ECR_Block);
			for (float Side : {-1.f,1.f})
			{
				StationFrame->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,Side*SideOffset,Ground+180),FVector(.7f,.7f,3.6f)));
    GateEdges->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,Side*SideOffset,Ground+180),FVector(.7f,.7f,3.6f)));
				StationFrame->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,Side*SideOffset,Ground+15),FVector(1.3f,1.3f,.3f)));
				for (float Face : {-1.f,1.f})
     StationLights->AddInstance(FTransform(FRotator::ZeroRotator,FVector(Face*37,Side*SideOffset,Ground+180),FVector(.06f,.35f,2.6f)));
			}
			StationFrame->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,0,Ground+370),FVector(.8f,(GateWidth+160.f)/100.f,.5f)));
			for (float Face : {-1.f,1.f})
    StationLights->AddInstance(FTransform(FRotator::ZeroRotator,FVector(Face*42,0,Ground+370),FVector(.06f,GateWidth/100.f,.16f)));
			for (float X : {-120.f,0.f,120.f})
				for (float Side : {-1.f,1.f})
					StationLights->AddInstance(FTransform(FRotator(0,-Side*45,0),FVector(X-35,Side*35,Ground+5),FVector(1.f,.15f,.04f)));
		}
		StationFrame->SetVisibility(bBeamVisible);
		StationLights->SetVisibility(bBeamVisible);
	}
 if (bRelayProjection) {
  BeaconMesh->SetVisibility(false);
  StationFrame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  StationFrame->ClearInstances(); StationLights->ClearInstances(); GateEdges->ClearInstances(); LastGlow=FLinearColor::Transparent;
  auto* Cube=FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);
  StationFrame->SetStaticMesh(Cube); StationLights->SetStaticMesh(Cube);
  StationFrame->SetMaterial(0,FCitixSurfaceLibrary::GetMaterial(ECitixSurface::Trim));
  auto* Glow=UMaterialInstanceDynamic::Create(FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool)->GetMaterial(),this);
  const FLinearColor Ink=bExitProjection ? FLinearColor(.05f,.6f,.32f) : FLinearColor(.02f,1.5f,4.f); Glow->SetVectorParameterValue(TEXT("Color"),Ink); Glow->SetVectorParameterValue(TEXT("BaseColor"),Ink); StationLights->SetMaterial(0,Glow);
  StationFrame->AddInstance(FTransform(FRotator::ZeroRotator,FVector(0,0,-3080),FVector(3.8f,3.8f,.2f)));
  GateEdges->SetStaticMesh(Cube); GateEdges->SetCollisionEnabled(ECollisionEnabled::NoCollision); GateEdges->SetCastShadow(false);
  GateEdges->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(bExitProjection ? FLinearColor(.08f,.6f,.32f) : FLinearColor(.08f,.45f,.7f))); GateEdges->SetVisibility(bBeamVisible);
  for(int32 I=0;I<32;++I) {
   const float Angle=I*2*PI/32, Radius=bExitProjection ? 800.f : FCitixChaseRules::RelayInteractionRadius;
   GateEdges->AddInstance(FTransform(FRotator(0,FMath::RadiansToDegrees(Angle)+90,0),FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,-3095),FVector(Radius*2*PI/32*.82f/100.f,.04f,.025f)));
  }
  for (float Z : {60.f,160.f,260.f}) for (int32 I=0; I<6; ++I) {
   const float Angle=I*60.f; const float R=FMath::DegreesToRadians(Angle);
   StationLights->AddInstance(FTransform(FRotator(0,Angle+90.f,0),FVector(FMath::Cos(R)*170.f,FMath::Sin(R)*170.f,-3090.f+Z),FVector(1.75f,.035f,.04f)));
   if (Z==160.f) StationLights->AddInstance(FTransform(FRotator::ZeroRotator,FVector(FMath::Cos(R)*170.f,FMath::Sin(R)*170.f,-2930),FVector(.04f,.04f,2.f)));
  }
  StationLights->AddInstance(FTransform(FRotator(35.f,45.f,45.f),FVector(0,0,-2930),FVector(.6f,.6f,.6f)));
  for (int32 I=0; I<4; ++I) {
   const float R=FMath::DegreesToRadians(I*90.f);
   StationLights->AddInstance(FTransform(FRotator(0,I*90.f,45.f),FVector(FMath::Cos(R)*95.f,FMath::Sin(R)*95.f,-2930),FVector(.15f,.15f,.15f)));
  }
  StationFrame->SetVisibility(bBeamVisible); StationLights->SetVisibility(bBeamVisible);
 }

}
