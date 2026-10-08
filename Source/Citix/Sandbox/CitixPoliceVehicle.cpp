// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixPoliceVehicle.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Net/UnrealNetwork.h"

ACitixPoliceVehicle::ACitixPoliceVehicle()
{
	// A handful of actors at most; the flash timer is trivial.
	PrimaryActorTick.bCanEverTick = true;
}

void ACitixPoliceVehicle::BeginPlay()
{
	Super::BeginPlay();

	// Cosmetics build everywhere (the lightbar is local presentation driven by
	// replicated state). Idempotent: the server path calls it explicitly too.
	InitializePolice();
	OnRep_FlashEnabled();
}

void ACitixPoliceVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixPoliceVehicle, bFlashEnabled);
}

void ACitixPoliceVehicle::SetFlashEnabled(bool bEnabled)
{
	bFlashEnabled = bEnabled;
	OnRep_FlashEnabled();
}

void ACitixPoliceVehicle::OnRep_FlashEnabled()
{
	if (!bFlashEnabled)
	{
		if (LightBarRed)
		{
			LightBarRed->SetVisibility(false, true);
		}
		if (LightBarBlue)
		{
			LightBarBlue->SetVisibility(false, true);
		}
	}
}

void ACitixPoliceVehicle::InitializePolice()
{
	if (bPoliceInitialized)
	{
		return;
	}
	bPoliceInitialized = true;

	UStaticMesh* BoxMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);
	UMaterialInterface* RedMat = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveWarm);
	UMaterialInterface* BlueMat = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool);

	auto MakeHalf = [this, BoxMesh](const TCHAR* Name, const FVector& Offset, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Half = NewObject<UStaticMeshComponent>(this, FName(Name));
		Half->SetupAttachment(GetRootComponent());
		Half->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Half->SetCanEverAffectNavigation(false);
		Half->SetCastShadow(false);
		Half->SetReceivesDecals(false);
		if (BoxMesh)
		{
			Half->SetStaticMesh(BoxMesh);
		}
		if (Material)
		{
			Half->SetMaterial(0, Material);
		}
		// Engine cube is 100 cm: a 46 x 42 x 22 cm half-bar sitting on the roof.
		Half->SetRelativeScale3D(FVector(0.46f, 0.42f, 0.22f));
		Half->SetRelativeLocation(Offset);
		Half->RegisterComponent();
		return Half;
	};

	// Roof height of a sedan body is roughly 110 cm; the bar straddles the centreline.
	LightBarRed = MakeHalf(TEXT("LightBarRed"), FVector(0.f, -24.f, 128.f), RedMat);
	LightBarBlue = MakeHalf(TEXT("LightBarBlue"), FVector(0.f, 24.f, 128.f), BlueMat);

	// Livery: white door stripes over the black body. Same box pattern, plain
	// white, visual only - the doors read as police from any angle.
	UMaterialInterface* WhiteMat = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::FacadeWhite);
	auto MakeStripe = [this, BoxMesh, WhiteMat](const TCHAR* Name, float Side)
	{
		UStaticMeshComponent* Stripe = NewObject<UStaticMeshComponent>(this, FName(Name));
		Stripe->SetupAttachment(GetRootComponent());
		Stripe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Stripe->SetCanEverAffectNavigation(false);
		Stripe->SetCastShadow(false);
		Stripe->SetReceivesDecals(false);
		if (BoxMesh)
		{
			Stripe->SetStaticMesh(BoxMesh);
		}
		if (WhiteMat)
		{
			Stripe->SetMaterial(0, WhiteMat);
		}
		Stripe->SetRelativeScale3D(FVector(1.7f, 0.03f, 0.3f));
		Stripe->SetRelativeLocation(FVector(-10.f, Side * 48.f, 72.f));
		Stripe->RegisterComponent();
	};
	MakeStripe(TEXT("DoorStripeL"), -1.f);
	MakeStripe(TEXT("DoorStripeR"), 1.f);
}

void ACitixPoliceVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFlashEnabled || !LightBarRed || !LightBarBlue)
	{
		return;
	}

	FlashTimer += DeltaSeconds;
	if (FlashTimer >= 0.4f)
	{
		FlashTimer = 0.f;
		bFlashState = !bFlashState;
		LightBarRed->SetVisibility(bFlashState, true);
		LightBarBlue->SetVisibility(!bFlashState, true);
	}
}
