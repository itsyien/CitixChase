// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixCityChunk.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Citix.h"

ACitixCityChunk::ACitixCityChunk()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

void ACitixCityChunk::Init(const FIntPoint& InCoord, float InSize)
{
	Coord = InCoord;
	Size = FMath::Max(100.f, InSize);

	const float Half = Size * 0.5f;
	const FVector Center(
		(static_cast<float>(Coord.X) + 0.5f) * Size - Half,
		(static_cast<float>(Coord.Y) + 0.5f) * Size - Half,
		0.f);
	SetActorLocation(Center);
}

FVector ACitixCityChunk::GetChunkCenter() const
{
	return GetActorLocation();
}

FVector ACitixCityChunk::ComputeInstanceScale(ECitixSurface Surface, const FVector& Size)
{
	if (Surface == ECitixSurface::Foliage || Surface == ECitixSurface::Lamp
		|| Surface == ECitixSurface::Dome || Surface == ECitixSurface::OrbWarm
		|| Surface == ECitixSurface::OrbPink || Surface == ECitixSurface::OrbCyan)
	{
		// Sphere meshes: use a single uniform diameter so they stay round.
		const float D = FMath::Max(1.f, Size.X) / FCitixSurfaceLibrary::PrimitiveSize;
		return FVector(D, D, D);
	}
	return Size / FCitixSurfaceLibrary::PrimitiveSize;
}

void ACitixCityChunk::QueueBox(const FCitixBoxInstance& Box)
{
	QueueBox(Box.Surface, Box.Center, Box.Size, Box.Rotation.Yaw, Box.Flow);
}

void ACitixCityChunk::QueueBox(ECitixSurface Surface, const FVector& WorldCenter, const FVector& Size3D,
	float YawDegrees, const FVector2D& Flow)
{
	if (bFinished)
	{
		UE_LOG(LogCitix, Warning, TEXT("ACitixCityChunk::QueueBox after Finish() ignored."));
		return;
	}
	if (Surface == ECitixSurface::Count || Size3D.GetAbsMin() <= 0.f)
	{
		return;
	}

	const FVector Local = WorldCenter - GetActorLocation();
	const FVector Scale = ComputeInstanceScale(Surface, Size3D);
	const FTransform Transform(FRotator(0.f, YawDegrees, 0.f), Local, Scale);

	PendingInstances.FindOrAdd(Surface).Add(Transform);
	// Always appended, so the flow array stays index-aligned with the transforms.
	PendingFlow.FindOrAdd(Surface).Add(Flow);
	SurfaceCounts.FindOrAdd(Surface)++;
	++TotalQueued;
}

int32 ACitixCityChunk::GetSurfaceInstanceCount(ECitixSurface Surface) const
{
	if (const int32* Found = SurfaceCounts.Find(Surface))
	{
		return *Found;
	}
	return 0;
}

UHierarchicalInstancedStaticMeshComponent* ACitixCityChunk::GetOrCreateComponent(ECitixSurface Surface)
{
	if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = SurfaceComponents.Find(Surface))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(Surface);
	if (!Mesh)
	{
		return nullptr;
	}

	UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	if (!Comp)
	{
		return nullptr;
	}

	Comp->SetupAttachment(SceneRoot);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	if (UMaterialInterface* Mat = FCitixSurfaceLibrary::GetMaterial(Surface))
	{
		Comp->SetMaterial(0, Mat);
	}

	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(FCitixSurfaceLibrary::CastsShadow(Surface));

	// Distance culling. Small, dense detail (props, foliage, street furniture) is not
	// readable from far away, so it stops drawing entirely; big surfaces and the lit
	// window bands are never culled because they carry the skyline.
	{
		float CullStart = 0.f;
		float CullEnd = 0.f;
		switch (Surface)
		{
		case ECitixSurface::Foliage:
		case ECitixSurface::Trunk:      CullStart = 20000.f; CullEnd = 32000.f; break;
		case ECitixSurface::Bush:      CullStart = 10000.f; CullEnd = 18000.f; break;
		case ECitixSurface::PropMetal:
		case ECitixSurface::PropDark:   CullStart = 22000.f; CullEnd = 34000.f; break;
		case ECitixSurface::Pole:       CullStart = 32000.f; CullEnd = 48000.f; break;
		case ECitixSurface::Lamp:       CullStart = 40000.f; CullEnd = 60000.f; break;
		default: break;
		}
		if (CullEnd > 0.f)
		{
			Comp->SetCullDistances(FMath::RoundToInt(CullStart), FMath::RoundToInt(CullEnd));
		}
	}

	if (Surface==ECitixSurface::Bush) {
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Comp->SetCollisionObjectType(ECC_GameTraceChannel2);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->ComponentTags.Add(TEXT("CitixBush"));
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(false);
	}
	else if (FCitixSurfaceLibrary::Collides(Surface))
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionResponseToAllChannels(ECR_Block);
		Comp->SetCollisionObjectType(ECC_WorldStatic);
	}
	else if (FCitixSurfaceLibrary::BlocksCharactersOnly(Surface))
	{
		// Roads, pavements and kerbs: solid underfoot for characters and dynamic bodies
		// but invisible to the car and its suspension traces so curbs never stop the
		// vehicle. Dynamic bodies are blocked so pedestrians and ragdolls rest on the
		// surface they landed on instead of sinking through it.
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionObjectType(ECC_WorldStatic);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Comp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Comp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	}
	else
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	Comp->RegisterComponent();
	SurfaceComponents.Add(Surface, Comp);
	return Comp;
}

void ACitixCityChunk::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	for (const TPair<ECitixSurface, TArray<FTransform>>& Pair : PendingInstances)
	{
		if (Pair.Value.Num() == 0)
		{
			continue;
		}
		if (UHierarchicalInstancedStaticMeshComponent* Comp = GetOrCreateComponent(Pair.Key))
		{
			const TArray<FVector2D>* Flow = PendingFlow.Find(Pair.Key);
			bool bHasFlow = false;
			if (Flow)
			{
				for (const FVector2D& Direction : *Flow)
				{
					if (!Direction.IsNearlyZero())
					{
						bHasFlow = true;
						break;
					}
				}
			}

			if (!bHasFlow)
			{
				// Bulk add: one tree rebuild instead of one per instance.
				Comp->AddInstances(Pair.Value, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
				continue;
			}

			// Two custom floats per instance: the flow direction, read by the water
			// material to orient its wave field. Only the water surface pays for this.
			Comp->SetNumCustomDataFloats(2);
			Comp->AddInstances(Pair.Value, false, false, false);

			TArray<float> CustomData;
			CustomData.SetNumUninitialized(2);
			for (int32 Index = 0; Index < Pair.Value.Num(); ++Index)
			{
				const FVector2D Direction = (Flow && Flow->IsValidIndex(Index))
					? (*Flow)[Index] : FVector2D(1.f, 0.f);
				CustomData[0] = Direction.X;
				CustomData[1] = Direction.Y;
				Comp->SetCustomData(Index, CustomData, /*bMarkRenderStateDirty*/ false);
			}
		}
	}

	PendingInstances.Empty();
	PendingFlow.Empty();
}
