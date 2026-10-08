// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixShopkeeper.h"

#include "Character/CitixCharacterLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"

ACitixShopkeeper::ACitixShopkeeper()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Static);

	SignMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShopSign"));
	SignMesh->SetupAttachment(Root);
	SignMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SignMesh->SetCanEverAffectNavigation(false);
	SignMesh->SetCastShadow(false);
	SignMesh->SetReceivesDecals(false);
}

UStaticMeshComponent* ACitixShopkeeper::AddPart(const TCHAR* Name, const FVector& Offset,
	const FVector& Size, ECitixSurface Surface, bool bCollide)
{
	// Engine cube is 100 cm; Size is in cm, converted to unit-cube scale.
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, FName(Name));
	Part->SetupAttachment(GetRootComponent());
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(FCitixSurfaceLibrary::CastsShadow(Surface));
	Part->SetReceivesDecals(false);
	if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
	{
		Part->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(Surface))
	{
		Part->SetMaterial(0, Material);
	}
	Part->SetRelativeScale3D(FVector(Size.X / 100.f, Size.Y / 100.f, Size.Z / 100.f));
	Part->SetRelativeLocation(Offset);
	if (bCollide)
	{
		Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Part->SetCollisionObjectType(ECC_WorldStatic);
		Part->SetCollisionResponseToAllChannels(ECR_Block);
	}
	else
	{
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Part->RegisterComponent();
	return Part;
}

void ACitixShopkeeper::BeginPlay()
{
	Super::BeginPlay();
}

void ACitixShopkeeper::InitializeKeeper(int32 Seed)
{
	FRandomStream Rng(Seed);
	// A standing character in rest pose: no walk phase, no tick, no simulation.
	FCitixHumanoidRig Rig = FCitixCharacterLibrary::BuildCharacter(
		this, GetRootComponent(), FCitixCharacterLibrary::GetRandomStyle(Rng), Rng);
	Rig.ApplyPose(FCitixHumanoidPose::Rest());

	// The storefront behind the counter (keeper at the origin, road at +X).
	AddPart(TEXT("ShopBody"), FVector(-460.f, 0.f, 165.f),
		FVector(460.f, 440.f, 330.f), ECitixSurface::FacadeBrick, true);
	AddPart(TEXT("ShopCounter"), FVector(-130.f, 0.f, 47.f),
		FVector(90.f, 180.f, 95.f), ECitixSurface::PropMetal, true);
	AddPart(TEXT("ShopWindow"), FVector(-228.f, 20.f, 175.f),
		FVector(6.f, 340.f, 130.f), ECitixSurface::WindowWarmBright, false);
	AddPart(TEXT("ShopDoor"), FVector(-228.f, -160.f, 105.f),
		FVector(6.f, 80.f, 210.f), ECitixSurface::PropDark, false);

	UStaticMeshComponent* Awning = AddPart(TEXT("ShopAwning"), FVector(-150.f, 0.f, 350.f),
		FVector(200.f, 460.f, 10.f), ECitixSurface::RoofDark, false);
	Awning->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

	AddPart(TEXT("SignPoleL"), FVector(-460.f, -80.f, 365.f),
		FVector(12.f, 12.f, 70.f), ECitixSurface::PropDark, false);
	AddPart(TEXT("SignPoleR"), FVector(-460.f, 80.f, 365.f),
		FVector(12.f, 12.f, 70.f), ECitixSurface::PropDark, false);

	if (SignMesh)
	{
		if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
		{
			SignMesh->SetStaticMesh(Mesh);
		}
		if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveWarm))
		{
			SignMesh->SetMaterial(0, Material);
		}
		SignMesh->SetRelativeScale3D(FVector(2.f, 1.2f, 0.4f));
		SignMesh->SetRelativeLocation(FVector(-460.f, 0.f, 410.f));
	}
}
