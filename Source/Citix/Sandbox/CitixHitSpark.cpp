// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixHitSpark.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"

ACitixHitSpark::ACitixHitSpark()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SparkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SparkMesh"));
	SparkMesh->SetupAttachment(Root);
	SparkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SparkMesh->SetCanEverAffectNavigation(false);
	SparkMesh->SetCastShadow(false);
	SparkMesh->SetReceivesDecals(false);
	SparkMesh->SetVisibility(false, true);
}

void ACitixHitSpark::BeginPlay()
{
	Super::BeginPlay();

	if (SparkMesh)
	{
		if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
		{
			SparkMesh->SetStaticMesh(Mesh);
		}
	}
	SetActorTickEnabled(false);
}

void ACitixHitSpark::Fire(const FVector& From, const FVector& Velocity, ECitixSurface Surface)
{
	SparkVelocity = Velocity;
	PixelScale = .09f; ShapeScale=FVector::OneVector;
	Life = .45f;
	Gravity = 1200.f;
	Spin = FVector::ZeroVector;
	Age = 0.f;
	bActive = true;

	SetActorLocation(From);
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(Surface))
	{
		SparkMesh->SetMaterial(0, Material);
	}
	SparkMesh->SetRelativeScale3D(FVector(0.09f));
	SparkMesh->SetVisibility(true, true);
	SetActorTickEnabled(true);
}

void ACitixHitSpark::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bActive)
	{
		return;
	}

	Age += DeltaSeconds;
	const float Alpha = FMath::Clamp(Age / FMath::Max(0.01f, Life), 0.f, 1.f);
	SparkVelocity.Z -= Gravity * DeltaSeconds;
	SetActorLocation(GetActorLocation() + SparkVelocity * DeltaSeconds);
	AddActorLocalRotation(FRotator(Spin.X, Spin.Y, Spin.Z) * DeltaSeconds);
	const float Scale = PixelScale * (1.f - Alpha);
	SparkMesh->SetRelativeScale3D(ShapeScale*FMath::Max(0.001f,Scale));
	if (Alpha >= 1.f)
	{
		bActive = false;
		SparkMesh->SetVisibility(false, true);
		SetActorTickEnabled(false);
	}
}

void ACitixHitSpark::SpawnPixelBurst(UWorld* World, const FVector& Location, bool bExplosion, bool bRedImpact)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	const int32 Count = bExplosion ? 112 : 18;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ACitixHitSpark* Pixel = World->SpawnActor<ACitixHitSpark>();
		if (!Pixel) continue;
		const bool bSmoke = bExplosion && Index >= 76 && Index < 100;
		const FLinearColor Colour = bRedImpact ? FLinearColor(1.f,.025f,.055f)
			: bSmoke ? FLinearColor(.09f,.10f,.13f)
			: Index < 16 ? FLinearColor(1.f,.93f,.75f)
			: Index >= 100 ? FLinearColor(.25f,.70f,1.f) : FLinearColor(1.f,.22f,.025f);
		FVector Direction = FMath::VRand();
		Direction.Z = FMath::Abs(Direction.Z) + .2f;
		Pixel->Fire(Location + FMath::VRand()* (bExplosion ? 100.f : 15.f),
			Direction * (bSmoke ? 180.f : FMath::FRandRange(bExplosion ? 350.f : 130.f, bExplosion ? 1400.f : 420.f)), ECitixSurface::EmissiveWarm);
		Pixel->PixelScale = bSmoke ? FMath::FRandRange(.4f,.85f) : FMath::FRandRange(.045f,bExplosion ? .22f : .11f);
		Pixel->Life = bSmoke ? 1.8f : FMath::FRandRange(.3f,bExplosion ? 1.25f : .55f);
		Pixel->Gravity = bSmoke ? -80.f : 850.f;
		Pixel->Spin = FMath::VRand()*240.f;
		Pixel->SparkMesh->SetRelativeScale3D(FVector(Pixel->PixelScale));
		Pixel->SparkMesh->SetMaterial(0, bSmoke ? FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface::PropDark, Colour) : FCitixSurfaceLibrary::GetTintedEmissiveMaterial(Colour));
		Pixel->SetLifeSpan(Pixel->Life + .1f);
	}
}

void ACitixHitSpark::SpawnPursuitPulse(UWorld* World,const FVector& Location,const FVector& Velocity,bool Energy)
{
 if (!World || World->GetNetMode()==NM_DedicatedServer) return;
 const FVector Forward=Velocity.IsNearlyZero() ? FVector::ForwardVector : Velocity.GetSafeNormal2D();
 const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
 for (int32 I=0; I<6; ++I) if (auto* Pixel=World->SpawnActor<ACitixHitSpark>()) {
  const FVector P=Location-Forward*130.f+Right*(I%2==0 ? -100.f : 100.f)+FVector(0,0,FMath::FRandRange(-20.f,65.f));
  Pixel->Fire(P,-Forward*FMath::FRandRange(100.f,Energy ? 550.f : 250.f)+FVector(0,0,Energy ? 100.f : 40.f),ECitixSurface::EmissiveWarm);
  Pixel->PixelScale=.1f; Pixel->ShapeScale=FVector(Energy ? 5.f : 2.f,.65f,.65f); Pixel->Life=.55f; Pixel->Gravity=Energy ? -80.f : 180.f;
  Pixel->SetActorRotation(Forward.Rotation()); Pixel->Spin=FVector(0,0,Energy ? 0.f : 180.f);
  Pixel->SparkMesh->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(Energy ? FLinearColor(4.f,2.f,.025f) : FLinearColor(4.f,.02f,.04f)));
  Pixel->SetLifeSpan(.65f);
 }
}
