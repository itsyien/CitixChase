// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixRouteGuide.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"

ACitixRouteGuide::ACitixRouteGuide()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Markers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Markers"));
	Markers->SetupAttachment(Root);
	Markers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Markers->SetCanEverAffectNavigation(false);
	Markers->SetCastShadow(false);
	Markers->SetReceivesDecals(false);
}

void ACitixRouteGuide::BeginPlay()
{
	Super::BeginPlay();

	if (Markers && FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
	{
		Markers->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete));
	}
	Clear();
}

void ACitixRouteGuide::Clear()
{
	MarkerCount = 0;
	if (Markers)
	{
		Markers->ClearInstances();
		SetActorHiddenInGame(true);
	}
}

void ACitixRouteGuide::UpdateRoute(const TArray<FVector>& Points, ECitixSurface Surface)
{
	if (!Markers || Points.Num() < 2)
	{
		Clear();
		return;
	}

	if (!bSurfaceSet || Surface != CurrentSurface)
	{
		CurrentSurface = Surface;
		bSurfaceSet = true;
		if (UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool)->GetMaterial(), this))
		{
			Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(.03f, 2.f, 8.f));
			Material->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(.03f, 2.f, 8.f));
			Markers->SetMaterial(0, Material);
		}
	}

	Markers->ClearInstances();
	MarkerCount = 0;
	SetActorHiddenInGame(false);

	// Walk the polyline, dropping a chevron every MarkerSpacing cm, yawed along
	// travel. Two angled arms form each arrowhead.
	float Carry = 0.f;
	for (int32 Index = 1; Index < Points.Num() && MarkerCount < MaxMarkers; ++Index)
	{
		const FVector A = Points[Index - 1];
		const FVector B = Points[Index];
		const float SegmentLength = FVector::Dist2D(A, B);
		if (SegmentLength < 1.f)
		{
			continue;
		}
		const FVector Direction = (B - A).GetSafeNormal2D();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

		float Travel = MarkerSpacing - Carry;
		while (Travel < SegmentLength && MarkerCount < MaxMarkers)
		{
			const float Alpha = Travel / SegmentLength;
			const FVector Spot = FMath::Lerp(A, B, Alpha) + FVector(0.f, 0.f, 40.f);
			const float Arm=MarkerScale.X*50.f;
   const FVector Right=FVector(-Direction.Y,Direction.X,0);
   for (float Side : {-1.f,1.f}) {
    const FVector Centre=Spot-Direction*(Arm*UE_INV_SQRT_2)+Right*(Side*Arm*UE_INV_SQRT_2);
    Markers->AddInstance(FTransform(FRotator(0,Yaw-Side*45.f,0),Centre,FVector(MarkerScale.X,MarkerScale.Y,MarkerScale.Z)));
   }
			++MarkerCount;
			Travel += MarkerSpacing;
		}
		Carry = Travel - SegmentLength;
	}

	if (MarkerCount == 0)
	{
		SetActorHiddenInGame(true);
	}
}
