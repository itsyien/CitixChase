// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/CitixVisibility.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

FCitixPlayerView FCitixVisibility::GetPlayerView(const UWorld* World)
{
	FCitixPlayerView View;
	if (!World)
	{
		return View;
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return View;
	}

	FVector Location;
	FRotator Rotation;
	PC->GetPlayerViewPoint(Location, Rotation);

	View.Location = Location;
	View.Forward = Rotation.Vector();

	float FOV = 90.f;
	if (PC->PlayerCameraManager)
	{
		FOV = PC->PlayerCameraManager->GetFOVAngle();
	}

	// Widen the cone a little so we are conservative about what counts as "in view".
	const float HalfAngle = FMath::Clamp(FOV * 0.5f * 1.3f, 15.f, 175.f);
	View.CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(HalfAngle));
	View.bValid = true;
	return View;
}

bool FCitixVisibility::IsInViewCone(const FCitixPlayerView& View, const FVector& Point)
{
	if (!View.bValid)
	{
		return false;
	}

	const FVector ToPoint = Point - View.Location;
	const float DistanceSquared = ToPoint.SizeSquared();
	if (DistanceSquared < 1.f)
	{
		return true;
	}

	return FVector::DotProduct(ToPoint / FMath::Sqrt(DistanceSquared), View.Forward) >= View.CosHalfFOV;
}

bool FCitixVisibility::IsVisible(const UWorld* World, const FCitixPlayerView& View, const FVector& Point,
	const AActor* IgnoreActor)
{
	// Fail safe: with no valid view we assume it is visible, so nothing gets pooled.
	if (!World || !View.bValid)
	{
		return true;
	}
	if (!IsInViewCone(View, Point))
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CitixVisibility), /*bTraceComplex*/ false);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, View.Location, Point, ECC_Visibility, Params);
	if (!bHit)
	{
		return true;
	}

	// Only treat it as occluded if something blocks meaningfully before the target
	// (the trace may land on the target's own geometry right at the end).
	const float Distance = FVector::Dist(View.Location, Point);
	return Hit.Distance > Distance - 150.f;
}

bool FCitixVisibility::IsPointVisible(const UWorld* World, const FVector& Point, const AActor* IgnoreActor)
{
	const FCitixPlayerView View = GetPlayerView(World);
	return IsVisible(World, View, Point, IgnoreActor);
}
