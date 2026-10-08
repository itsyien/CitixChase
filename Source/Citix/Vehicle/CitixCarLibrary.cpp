// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CitixCarLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	void AddPart(TArray<FCitixCarPartDesc>& Parts, const FVector& Center, const FVector& Size,
		ECitixSurface Surface, bool bPaint = false, const FRotator& Rotation = FRotator::ZeroRotator,
		bool bPrimary = false, bool bHighDetailOnly = false)
	{
		FCitixCarPartDesc Part;
		Part.Center = Center;
		Part.Size = Size;
		Part.Rotation = Rotation;
		Part.Surface = Surface;
		Part.bPaint = bPaint;
		Part.bPrimary = bPrimary;
		Part.bHighDetailOnly = bHighDetailOnly;
		Parts.Add(Part);
	}

	void AddWheel(TArray<FCitixCarPartDesc>& Parts, int32 Index, const FVector& Center, float Radius, float Width)
	{
		FCitixCarPartDesc Part;
		Part.Center = Center;
		Part.Size = FVector(Radius * 2.f, Radius * 2.f, Width);
		// Roll 90 swings the cylinder's axis onto Y so it reads as a wheel.
		Part.Rotation = FRotator(0.f, 0.f, 90.f);
		Part.Surface = ECitixSurface::Tire;
		Part.bWheel = true;
		Part.WheelIndex = Index;
		Parts.Add(Part);
	}

	/** Shared lights/bumpers/mirrors so every car type is consistent. */
	void AddCommonTrim(TArray<FCitixCarPartDesc>& Parts, float HalfLength, float HalfWidth,
		float BumperZ, float LightZ, float TailZ, float MirrorX, float MirrorZ)
	{
		// Small trim is high-detail only; distant traffic uses merged light bars.
		AddPart(Parts, FVector(HalfLength - 8.f, 0.f, BumperZ), FVector(26.f, HalfWidth * 1.9f, 26.f), ECitixSurface::Trim, false, FRotator::ZeroRotator, false, true);
		AddPart(Parts, FVector(-HalfLength + 8.f, 0.f, BumperZ), FVector(26.f, HalfWidth * 1.9f, 26.f), ECitixSurface::Trim, false, FRotator::ZeroRotator, false, true);

		AddPart(Parts, FVector(HalfLength - 6.f, -HalfWidth * 0.62f, LightZ), FVector(12.f, 44.f, 16.f), ECitixSurface::HeadLight, false, FRotator::ZeroRotator, false, true);
		AddPart(Parts, FVector(HalfLength - 6.f, HalfWidth * 0.62f, LightZ), FVector(12.f, 44.f, 16.f), ECitixSurface::HeadLight, false, FRotator::ZeroRotator, false, true);
		AddPart(Parts, FVector(-HalfLength + 6.f, -HalfWidth * 0.62f, TailZ), FVector(10.f, 40.f, 14.f), ECitixSurface::TailLight, false, FRotator::ZeroRotator, false, true);
		AddPart(Parts, FVector(-HalfLength + 6.f, HalfWidth * 0.62f, TailZ), FVector(10.f, 40.f, 14.f), ECitixSurface::TailLight, false, FRotator::ZeroRotator, false, true);

		if (MirrorX != 0.f)
		{
			AddPart(Parts, FVector(MirrorX, -HalfWidth - 6.f, MirrorZ), FVector(16.f, 14.f, 12.f), ECitixSurface::Trim, false, FRotator::ZeroRotator, false, true);
			AddPart(Parts, FVector(MirrorX, HalfWidth + 6.f, MirrorZ), FVector(16.f, 14.f, 12.f), ECitixSurface::Trim, false, FRotator::ZeroRotator, false, true);
		}
	}

	/** Merged front/rear light bars used by the low-detail traffic variant. */
	void AddMergedLights(TArray<FCitixCarPartDesc>& Parts, const FCitixCarPartDesc& Primary,
		float LightZ, float TailZ)
	{
		const float HalfLength = Primary.Size.X * 0.5f;
		const float HalfWidth = Primary.Size.Y * 0.5f;
		AddPart(Parts, FVector(HalfLength - 5.f, 0.f, LightZ), FVector(10.f, HalfWidth * 1.35f, 15.f), ECitixSurface::HeadLight);
		AddPart(Parts, FVector(-HalfLength + 5.f, 0.f, TailZ), FVector(9.f, HalfWidth * 1.25f, 13.f), ECitixSurface::TailLight);
	}

	/** Remove high-detail-only trim and add merged light bars. */
	void MakeLowDetail(TArray<FCitixCarPartDesc>& Parts)
	{
		// Keep only the most prominent glass panel (the windscreen).
		int32 BestGlass = INDEX_NONE;
		float BestGlassVolume = 0.f;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			if (Parts[Index].Surface == ECitixSurface::CarGlass)
			{
				const FVector& Size = Parts[Index].Size;
				const float Volume = Size.X * Size.Y * Size.Z;
				if (Volume > BestGlassVolume)
				{
					BestGlassVolume = Volume;
					BestGlass = Index;
				}
			}
		}

		TArray<FCitixCarPartDesc> Filtered;
		Filtered.Reserve(Parts.Num());
		const FCitixCarPartDesc* Primary = nullptr;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			const FCitixCarPartDesc& Part = Parts[Index];
			if (Part.bPrimary)
			{
				Primary = &Part;
			}
			if (Part.bHighDetailOnly)
			{
				continue;
			}
			if (Part.Surface == ECitixSurface::CarGlass && Index != BestGlass)
			{
				continue;
			}
			Filtered.Add(Part);
		}

		float LightZ = 50.f;
		float TailZ = 52.f;
		if (Primary)
		{
			LightZ = Primary->Center.Z + Primary->Size.Z * 0.15f;
			TailZ = LightZ + 6.f;
			AddMergedLights(Filtered, *Primary, LightZ, TailZ);
		}
		Parts = MoveTemp(Filtered);
	}
}

void FCitixCarLibrary::BuildPartDescs(ECitixCarType Type, TArray<FCitixCarPartDesc>& OutParts)
{
	OutParts.Reset();

	switch (Type)
	{
	case ECitixCarType::Sedan:
	{
		AddPart(OutParts, FVector(0.f, 0.f, 42.f), FVector(440.f, 176.f, 58.f), ECitixSurface::CarPaint, true, FRotator::ZeroRotator, true);
		AddPart(OutParts, FVector(-18.f, 0.f, 96.f), FVector(205.f, 158.f, 52.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(58.f, 0.f, 92.f), FVector(54.f, 148.f, 10.f), ECitixSurface::CarGlass, false, FRotator(-32.f, 0.f, 0.f));
		AddPart(OutParts, FVector(-98.f, 0.f, 92.f), FVector(48.f, 144.f, 10.f), ECitixSurface::CarGlass, false, FRotator(30.f, 0.f, 0.f));
		AddPart(OutParts, FVector(-18.f, -80.f, 96.f), FVector(168.f, 6.f, 30.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(-18.f, 80.f, 96.f), FVector(168.f, 6.f, 30.f), ECitixSurface::CarGlass);
		AddCommonTrim(OutParts, 225.f, 91.f, 24.f, 52.f, 56.f, 66.f, 100.f);
		AddWheel(OutParts, 0, FVector(145.f, -80.f, 33.f), 33.f, 22.f);
		AddWheel(OutParts, 1, FVector(145.f, 80.f, 33.f), 33.f, 22.f);
		AddWheel(OutParts, 2, FVector(-145.f, -80.f, 33.f), 33.f, 22.f);
		AddWheel(OutParts, 3, FVector(-145.f, 80.f, 33.f), 33.f, 22.f);
		break;
	}

	case ECitixCarType::Hatchback:
	{
		AddPart(OutParts, FVector(0.f, 0.f, 44.f), FVector(390.f, 170.f, 60.f), ECitixSurface::CarPaint, true, FRotator::ZeroRotator, true);
		AddPart(OutParts, FVector(-30.f, 0.f, 100.f), FVector(215.f, 152.f, 54.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(50.f, 0.f, 96.f), FVector(52.f, 144.f, 10.f), ECitixSurface::CarGlass, false, FRotator(-34.f, 0.f, 0.f));
		AddPart(OutParts, FVector(-112.f, 0.f, 96.f), FVector(44.f, 140.f, 10.f), ECitixSurface::CarGlass, false, FRotator(24.f, 0.f, 0.f));
		AddPart(OutParts, FVector(-30.f, -78.f, 100.f), FVector(180.f, 6.f, 30.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(-30.f, 78.f, 100.f), FVector(180.f, 6.f, 30.f), ECitixSurface::CarGlass);
		AddCommonTrim(OutParts, 200.f, 88.f, 24.f, 52.f, 58.f, 58.f, 100.f);
		AddWheel(OutParts, 0, FVector(128.f, -76.f, 32.f), 32.f, 20.f);
		AddWheel(OutParts, 1, FVector(128.f, 76.f, 32.f), 32.f, 20.f);
		AddWheel(OutParts, 2, FVector(-128.f, -76.f, 32.f), 32.f, 20.f);
		AddWheel(OutParts, 3, FVector(-128.f, 76.f, 32.f), 32.f, 20.f);
		break;
	}

	case ECitixCarType::Van:
	{
		AddPart(OutParts, FVector(0.f, 0.f, 78.f), FVector(480.f, 190.f, 130.f), ECitixSurface::CarPaint, true, FRotator::ZeroRotator, true);
		AddPart(OutParts, FVector(-20.f, 0.f, 162.f), FVector(300.f, 180.f, 58.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(112.f, 0.f, 150.f), FVector(14.f, 172.f, 56.f), ECitixSurface::CarGlass, false, FRotator(-10.f, 0.f, 0.f));
		AddPart(OutParts, FVector(-40.f, -92.f, 158.f), FVector(280.f, 6.f, 42.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(-40.f, 92.f, 158.f), FVector(280.f, 6.f, 42.f), ECitixSurface::CarGlass);
		AddCommonTrim(OutParts, 240.f, 98.f, 30.f, 58.f, 62.f, 120.f, 118.f);
		AddWheel(OutParts, 0, FVector(160.f, -84.f, 35.f), 35.f, 24.f);
		AddWheel(OutParts, 1, FVector(160.f, 84.f, 35.f), 35.f, 24.f);
		AddWheel(OutParts, 2, FVector(-160.f, -84.f, 35.f), 35.f, 24.f);
		AddWheel(OutParts, 3, FVector(-160.f, 84.f, 35.f), 35.f, 24.f);
		break;
	}

	case ECitixCarType::Pickup:
	{
		AddPart(OutParts, FVector(0.f, 0.f, 50.f), FVector(500.f, 192.f, 64.f), ECitixSurface::CarPaint, true, FRotator::ZeroRotator, true);
		AddPart(OutParts, FVector(58.f, 0.f, 108.f), FVector(172.f, 180.f, 58.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(146.f, 0.f, 102.f), FVector(50.f, 160.f, 10.f), ECitixSurface::CarGlass, false, FRotator(-30.f, 0.f, 0.f));
		AddPart(OutParts, FVector(58.f, -88.f, 106.f), FVector(140.f, 6.f, 38.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(58.f, 88.f, 106.f), FVector(140.f, 6.f, 38.f), ECitixSurface::CarGlass);
		// Load bed walls and tailgate.
		AddPart(OutParts, FVector(-150.f, -88.f, 88.f), FVector(250.f, 16.f, 46.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(-150.f, 88.f, 88.f), FVector(250.f, 16.f, 46.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(-272.f, 0.f, 88.f), FVector(16.f, 180.f, 46.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(-150.f, 0.f, 84.f), FVector(250.f, 170.f, 8.f), ECitixSurface::Trim);
		AddCommonTrim(OutParts, 250.f, 99.f, 28.f, 58.f, 62.f, 118.f, 112.f);
		AddWheel(OutParts, 0, FVector(168.f, -84.f, 36.f), 36.f, 26.f);
		AddWheel(OutParts, 1, FVector(168.f, 84.f, 36.f), 36.f, 26.f);
		AddWheel(OutParts, 2, FVector(-168.f, -84.f, 36.f), 36.f, 26.f);
		AddWheel(OutParts, 3, FVector(-168.f, 84.f, 36.f), 36.f, 26.f);
		break;
	}

	case ECitixCarType::Truck:
	{
		AddPart(OutParts, FVector(0.f, 0.f, 52.f), FVector(800.f, 244.f, 44.f), ECitixSurface::Trim, false, FRotator::ZeroRotator, true);
		AddPart(OutParts, FVector(300.f, 0.f, 132.f), FVector(220.f, 236.f, 130.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(404.f, 0.f, 178.f), FVector(14.f, 216.f, 62.f), ECitixSurface::CarGlass, false, FRotator(-8.f, 0.f, 0.f));
		AddPart(OutParts, FVector(300.f, -118.f, 174.f), FVector(180.f, 6.f, 54.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(300.f, 118.f, 174.f), FVector(180.f, 6.f, 54.f), ECitixSurface::CarGlass);
		// Cargo box.
		AddPart(OutParts, FVector(-140.f, 0.f, 172.f), FVector(500.f, 244.f, 232.f), ECitixSurface::CarPaint, true);
		AddPart(OutParts, FVector(-390.f, 0.f, 172.f), FVector(14.f, 236.f, 224.f), ECitixSurface::Trim);
		AddCommonTrim(OutParts, 400.f, 125.f, 34.f, 70.f, 70.f, 150.f, 150.f);
		AddWheel(OutParts, 0, FVector(330.f, -104.f, 44.f), 44.f, 30.f);
		AddWheel(OutParts, 1, FVector(330.f, 104.f, 44.f), 44.f, 30.f);
		AddWheel(OutParts, 2, FVector(-190.f, -104.f, 44.f), 44.f, 30.f);
		AddWheel(OutParts, 3, FVector(-190.f, 104.f, 44.f), 44.f, 30.f);
		break;
	}

	case ECitixCarType::Bus:
	{
		AddPart(OutParts, FVector(0.f, 0.f, 150.f), FVector(1080.f, 250.f, 290.f), ECitixSurface::CarPaint, true, FRotator::ZeroRotator, true);
		// Window band wrapping the sides and rear.
		AddPart(OutParts, FVector(-40.f, -127.f, 216.f), FVector(900.f, 6.f, 78.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(-40.f, 127.f, 216.f), FVector(900.f, 6.f, 78.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(-536.f, 0.f, 216.f), FVector(6.f, 236.f, 78.f), ECitixSurface::CarGlass);
		AddPart(OutParts, FVector(534.f, 0.f, 196.f), FVector(14.f, 232.f, 118.f), ECitixSurface::CarGlass, false, FRotator(-6.f, 0.f, 0.f));
		AddCommonTrim(OutParts, 540.f, 128.f, 38.f, 78.f, 78.f, 470.f, 190.f);
		AddWheel(OutParts, 0, FVector(360.f, -108.f, 46.f), 46.f, 32.f);
		AddWheel(OutParts, 1, FVector(360.f, 108.f, 46.f), 46.f, 32.f);
		AddWheel(OutParts, 2, FVector(-300.f, -108.f, 46.f), 46.f, 32.f);
		AddWheel(OutParts, 3, FVector(-300.f, 108.f, 46.f), 46.f, 32.f);
		break;
	}

	default:
		BuildPartDescs(ECitixCarType::Sedan, OutParts);
		break;
	}
}

FCitixCarVisual FCitixCarLibrary::BuildCar(AActor* Owner, USceneComponent* Root, ECitixCarType Type,
	const FLinearColor& PaintColor, bool bCollision, bool bIncludeWheels, bool bHighDetail)
{
	FCitixCarVisual Visual;
	if (!Owner || !Root)
	{
		return Visual;
	}

	TArray<FCitixCarPartDesc> Parts;
	BuildPartDescs(Type, Parts);
	if (!bHighDetail)
	{
		MakeLowDetail(Parts);
	}
	Visual.Parts.Reserve(Parts.Num());
	Visual.Wheels.SetNum(4);

	UStaticMesh* CubeMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);
	UStaticMesh* CylinderMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::Tire);

	for (const FCitixCarPartDesc& Desc : Parts)
	{
		if (Desc.bWheel && !bIncludeWheels)
		{
			continue;
		}

		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
		if (!Component)
		{
			continue;
		}
		Component->SetupAttachment(Root);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Desc.bWheel ? CylinderMesh : CubeMesh);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(FCitixSurfaceLibrary::CastsShadow(Desc.Surface));
		Component->SetRelativeLocation(Desc.Center);
		Component->SetRelativeRotation(Desc.Rotation);
		Component->SetRelativeScale3D(Desc.Size / FCitixSurfaceLibrary::PrimitiveSize);

		if (UMaterialInterface* Material = Desc.bPaint
			? FCitixSurfaceLibrary::GetTintedMaterial(Desc.Surface, PaintColor)
			: FCitixSurfaceLibrary::GetMaterial(Desc.Surface))
		{
			Component->SetMaterial(0, Material);
		}

		if (bCollision && Desc.bPrimary)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Component->SetCollisionObjectType(ECC_Vehicle);
			Component->SetCollisionResponseToAllChannels(ECR_Block);
			// Keep suspension traces clean: the player's wheels should not ride on traffic.
			Component->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
		}
		else
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		Component->RegisterComponent();
		if (Desc.bPaint)
		{
			Visual.PaintedParts.Add(Visual.Parts.Num());
		}
		Visual.Parts.Add(Component);

		if (Desc.bWheel && Desc.WheelIndex >= 0 && Desc.WheelIndex < 4)
		{
			Visual.Wheels[Desc.WheelIndex] = Component;
			Visual.WheelCenters[Desc.WheelIndex] = Desc.Center;
			Visual.WheelRadius = Desc.Size.X * 0.5f;
		}
		if (Desc.bPrimary)
		{
			Visual.Body = Component;
			Visual.HalfLength = Desc.Size.X * 0.5f;
			Visual.HalfWidth = Desc.Size.Y * 0.5f;
		}
	}

	// Height is derived from the tallest part.
	float Top = 0.f;
	for (const FCitixCarPartDesc& Desc : Parts)
	{
		Top = FMath::Max(Top, Desc.Center.Z + Desc.Size.Z * 0.5f);
	}
	Visual.Height = Top;

	return Visual;
}

void FCitixCarVisual::SetWheelsVisible(bool bVisible) const
{
	for (UStaticMeshComponent* Wheel : Wheels)
	{
		if (Wheel)
		{
			Wheel->SetVisibility(bVisible, true);
		}
	}
}

void FCitixCarVisual::SetVisibility(bool bVisible) const
{
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(bVisible, true);
		}
	}
}

FLinearColor FCitixCarLibrary::GetRandomPaintColor(FRandomStream& Rng)
{
	static const FLinearColor Palette[] =
	{
		FLinearColor(0.72f, 0.73f, 0.75f),	// silver
		FLinearColor(0.05f, 0.05f, 0.06f),	// black
		FLinearColor(0.80f, 0.80f, 0.78f),	// white
		FLinearColor(0.45f, 0.06f, 0.06f),	// red
		FLinearColor(0.06f, 0.16f, 0.45f),	// blue
		FLinearColor(0.08f, 0.30f, 0.22f),	// green
		FLinearColor(0.62f, 0.60f, 0.52f),	// beige
		FLinearColor(0.70f, 0.45f, 0.06f),	// amber
		FLinearColor(0.30f, 0.31f, 0.34f),	// graphite
		FLinearColor(0.35f, 0.10f, 0.28f),	// plum
		FLinearColor(0.55f, 0.57f, 0.60f),	// grey
		FLinearColor(0.12f, 0.35f, 0.42f)	// teal
	};
	return Palette[Rng.RandRange(0, UE_ARRAY_COUNT(Palette) - 1)];
}

const TCHAR* FCitixCarLibrary::GetTypeName(ECitixCarType Type)
{
	switch (Type)
	{
	case ECitixCarType::Sedan: return TEXT("Sedan");
	case ECitixCarType::Hatchback: return TEXT("Hatchback");
	case ECitixCarType::Van: return TEXT("Van");
	case ECitixCarType::Pickup: return TEXT("Pickup");
	case ECitixCarType::Truck: return TEXT("Truck");
	case ECitixCarType::Bus: return TEXT("Bus");
	default: return TEXT("Car");
	}
}

void FCitixCarLibrary::GetFootprint(ECitixCarType Type, float& OutHalfLength, float& OutHalfWidth, float& OutHeight)
{
	TArray<FCitixCarPartDesc> Parts;
	BuildPartDescs(Type, Parts);

	float MaxX = 0.f;
	float MaxY = 0.f;
	float MaxZ = 0.f;
	for (const FCitixCarPartDesc& Part : Parts)
	{
		MaxX = FMath::Max(MaxX, FMath::Abs(Part.Center.X) + Part.Size.X * 0.5f);
		MaxY = FMath::Max(MaxY, FMath::Abs(Part.Center.Y) + Part.Size.Y * 0.5f);
		MaxZ = FMath::Max(MaxZ, Part.Center.Z + Part.Size.Z * 0.5f);
	}
	OutHalfLength = MaxX;
	OutHalfWidth = MaxY;
	OutHeight = MaxZ;
}

ECitixCarType FCitixCarLibrary::GetRandomTrafficType(FRandomStream& Rng)
{
	// Weighted so ordinary cars dominate and buses/trucks are occasional.
	static const int32 Weights[] = { 40, 22, 14, 10, 8, 6 }; // Sedan..Bus
	int32 Total = 0;
	for (int32 Weight : Weights)
	{
		Total += Weight;
	}
	int32 Roll = Rng.RandRange(1, Total);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Weights); ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0)
		{
			return static_cast<ECitixCarType>(Index);
		}
	}
	return ECitixCarType::Sedan;
}

FCitixCarPerformance FCitixCarLibrary::GetPerformance(ECitixCarType Type)
{
	//                 mass   topSpeed  engine    brake     hull   lat  handbrake  drift  assist  damp
	//                        (cm/s)    force     force     pts    grip   grip     steer  drift   drift
	FCitixCarPerformance P;
	switch (Type)
	{
	case ECitixCarType::Hatchback:
		// Light and short: quick to turn in, the loosest rear on the handbrake, but the
		// least power and the thinnest hull.
		P.MassKg = 1100.f;
		P.MaxSpeed = 6300.f;
		P.EngineForce = 1250000.f;
		P.BrakeForce = 2300000.f;
		P.MaxHealth = 320.f;
		P.LateralStiffness = 900.f;
		P.HandbrakeGripScale = 0.34f;
		P.DriftSteerAuthority = 1.0f;
		P.DriftSlideAssistScale = 0.22f;
		P.DriftAngularDamping = 0.70f;
		break;

	case ECitixCarType::Sedan:
		// The reference car: balanced, and the one the handling was tuned on.
		P.MassKg = 1500.f;
		P.MaxSpeed = 6600.f;
		P.EngineForce = 1350000.f;
		P.BrakeForce = 2600000.f;
		P.MaxHealth = 400.f;
		P.LateralStiffness = 850.f;
		P.HandbrakeGripScale = 0.40f;
		P.DriftSteerAuthority = 1.0f;
		P.DriftSlideAssistScale = 0.30f;
		P.DriftAngularDamping = 0.80f;
		break;

	case ECitixCarType::Pickup:
		// Heavier with a longer wheelbase: more power to compensate, but it resists
		// rotating and needs a firmer handbrake to break away.
		P.MassKg = 2000.f;
		P.MaxSpeed = 6100.f;
		P.EngineForce = 1500000.f;
		P.BrakeForce = 2700000.f;
		P.MaxHealth = 480.f;
		P.LateralStiffness = 800.f;
		P.HandbrakeGripScale = 0.46f;
		P.DriftSteerAuthority = 1.0f;
		P.DriftSlideAssistScale = 0.36f;
		P.DriftAngularDamping = 0.95f;
		break;

	case ECitixCarType::Van:
		// Tall and heavy: slow to change direction, and it understeers rather than drifting.
		P.MassKg = 2400.f;
		P.MaxSpeed = 5600.f;
		P.EngineForce = 1450000.f;
		P.BrakeForce = 2800000.f;
		P.MaxHealth = 460.f;
		P.LateralStiffness = 720.f;
		P.HandbrakeGripScale = 0.58f;
		P.DriftSteerAuthority = 0.95f;
		P.DriftSlideAssistScale = 0.48f;
		P.DriftAngularDamping = 1.15f;
		break;

	case ECitixCarType::Truck:
		// Much heavier, much stronger. Barely drifts at all; it just scrubs speed.
		P.MassKg = 5000.f;
		P.MaxSpeed = 4600.f;
		P.EngineForce = 2600000.f;
		P.BrakeForce = 3400000.f;
		P.MaxHealth = 700.f;
		P.LateralStiffness = 640.f;
		P.HandbrakeGripScale = 0.70f;
		P.DriftSteerAuthority = 0.90f;
		P.DriftSlideAssistScale = 0.62f;
		P.DriftAngularDamping = 1.45f;
		break;

	case ECitixCarType::Bus:
		// The heaviest thing on the road: very slow, very tough, and practically refuses
		// to rotate.
		P.MassKg = 8000.f;
		P.MaxSpeed = 4200.f;
		P.EngineForce = 3200000.f;
		P.BrakeForce = 3600000.f;
		P.MaxHealth = 760.f;
		P.LateralStiffness = 560.f;
		P.HandbrakeGripScale = 0.78f;
		P.DriftSteerAuthority = 0.85f;
		P.DriftSlideAssistScale = 0.72f;
		P.DriftAngularDamping = 1.70f;
		break;

	default:
		break;
	}
	return P;
}
