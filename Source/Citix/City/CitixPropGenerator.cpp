// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixPropGenerator.h"

namespace
{
	FORCEINLINE void Emit(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Center, const FVector& Size,
		ECitixSurface Surface, float YawDegrees = 0.f)
	{
		FCitixBoxInstance Box;
		Box.Center = Center;
		Box.Size = Size;
		Box.Rotation = FRotator(0.f, YawDegrees, 0.f);
		Box.Surface = Surface;
		OutBoxes.Add(Box);
	}
}

void FCitixPropGenerator::AddStreetLamp(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base,
	float Height, float YawDegrees)
{
	const float PoleDiameter = 20.f;
	Emit(OutBoxes, Base + FVector(0.f, 0.f, Height * 0.5f), FVector(PoleDiameter, PoleDiameter, Height),
		ECitixSurface::Pole, YawDegrees);

	// Arm reaching over the road (local +X before yaw).
	const float ArmLength = 180.f;
	const FVector ArmDir = FRotator(0.f, YawDegrees, 0.f).Vector();
	Emit(OutBoxes, Base + FVector(0.f, 0.f, Height) + ArmDir * (ArmLength * 0.5f),
		FVector(ArmLength, 16.f, 16.f), ECitixSurface::PropDark, YawDegrees);

	// Lamp head: bright sphere so it reads at night once lighting is tuned.
	Emit(OutBoxes, Base + FVector(0.f, 0.f, Height - 30.f) + ArmDir * ArmLength,
		FVector(55.f, 55.f, 55.f), ECitixSurface::Lamp);
}

void FCitixPropGenerator::AddTree(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base,
	float Scale, FRandomStream& Rng)
{
	const float TrunkH = 420.f * Scale;
	const float TrunkD = 34.f * Scale;
	Emit(OutBoxes, Base + FVector(0.f, 0.f, TrunkH * 0.5f), FVector(TrunkD, TrunkD, TrunkH), ECitixSurface::Trunk);

	const float CanopyD = 380.f * Scale;
	const int32 Blobs = Rng.RandRange(1, 2);
	for (int32 i = 0; i < Blobs; ++i)
	{
		const FVector Offset(
			Rng.FRandRange(-CanopyD * 0.2f, CanopyD * 0.2f),
			Rng.FRandRange(-CanopyD * 0.2f, CanopyD * 0.2f),
			0.f);
		const float BlobScale = (i == 0) ? 1.f : Rng.FRandRange(0.6f, 0.85f);
		Emit(OutBoxes, Base + Offset + FVector(0.f, 0.f, TrunkH + CanopyD * 0.35f * BlobScale),
			FVector(CanopyD * BlobScale), ECitixSurface::Foliage);
	}
}

void FCitixPropGenerator::AddTrafficLight(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base,
	float ArmLength, float YawDegrees, bool bWarm)
{
	const float Height = 620.f;
	const float PoleDiameter = 24.f;
	Emit(OutBoxes, Base + FVector(0.f, 0.f, Height * 0.5f), FVector(PoleDiameter, PoleDiameter, Height),
		ECitixSurface::Pole, YawDegrees);

	const FVector ArmDir = FRotator(0.f, YawDegrees, 0.f).Vector();
	Emit(OutBoxes, Base + FVector(0.f, 0.f, Height) + ArmDir * (ArmLength * 0.5f),
		FVector(ArmLength, 20.f, 20.f), ECitixSurface::PropDark, YawDegrees);

	const FVector HeadPos = Base + FVector(0.f, 0.f, Height - 40.f) + ArmDir * ArmLength;
	Emit(OutBoxes, HeadPos, FVector(70.f, 70.f, 180.f), ECitixSurface::PropDark, YawDegrees);
	Emit(OutBoxes, HeadPos + FVector(0.f, 0.f, -40.f), FVector(46.f, 46.f, 46.f),
		bWarm ? ECitixSurface::EmissiveWarm : ECitixSurface::EmissiveCool);
}

void FCitixPropGenerator::AddPlanter(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, FRandomStream& Rng)
{
	AddBush(OutBoxes,Base,Rng.FRandRange(.7f,.95f),Rng.FRandRange(0.f,360.f),Rng);
}
void FCitixPropGenerator::AddBush(TArray<FCitixBoxInstance>& OutBoxes,const FVector& Base,float Scale,float YawDegrees,FRandomStream& Rng)
{
	const float Height=Rng.FRandRange(120.f,160.f)*Scale;
	Emit(OutBoxes,Base+FVector(0,0,Height*.5f),FVector(300.f*Scale,170.f*Scale,Height),ECitixSurface::Bush,YawDegrees);
}

void FCitixPropGenerator::AddBench(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, float YawDegrees)
{
	// Seat slab at sitting height, two leg blocks, all dark timber/steel tones.
	Emit(OutBoxes, Base + FVector(0.f, 0.f, 55.f), FVector(220.f, 70.f, 14.f), ECitixSurface::Trunk, YawDegrees);
	Emit(OutBoxes, Base + FVector(-80.f, 0.f, 24.f), FVector(16.f, 60.f, 48.f), ECitixSurface::PropDark, YawDegrees);
	Emit(OutBoxes, Base + FVector(80.f, 0.f, 24.f), FVector(16.f, 60.f, 48.f), ECitixSurface::PropDark, YawDegrees);
}

void FCitixPropGenerator::AddACUnit(TArray<FCitixBoxInstance>& OutBoxes, const FVector& Base, FRandomStream& Rng)
{
	const float W = Rng.FRandRange(90.f, 160.f);
	const float D = Rng.FRandRange(70.f, 120.f);
	const float H = Rng.FRandRange(70.f, 110.f);
	Emit(OutBoxes, Base, FVector(W, D, H), ECitixSurface::PropMetal, Rng.FRandRange(0.f, 360.f));
}
