// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixCityChunk.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Citix.h"
#include "Core/CitixGraphicsSettings.h"
#include "HAL/IConsoleManager.h"

FVector2D ACitixCityChunk::DetailDrawDistance(int32 Preset) {
 const FVector2D Ranges[]={FVector2D(22000,30000),FVector2D(35000,50000),FVector2D(60000,85000),FVector2D(100000,140000)};
 return Ranges[FMath::Clamp(Preset,0,3)];
}
namespace {
 bool IsDistanceDetail(ECitixSurface Surface) {
  return Surface==ECitixSurface::Bush || Surface==ECitixSurface::Foliage || Surface==ECitixSurface::Trunk || Surface==ECitixSurface::PropMetal || Surface==ECitixSurface::PropDark || Surface==ECitixSurface::Pole || Surface==ECitixSurface::Lamp;
 }
 void ApplyDetailRange(UHierarchicalInstancedStaticMeshComponent* Comp,int32 Preset) {
  const auto Range=ACitixCityChunk::DetailDrawDistance(Preset);
  const auto* Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"));
  const float ViewScale=Scale ? FMath::Max(.01f,Scale->GetFloat()) : 1.f;
  // HISM applies the global view scale again. Normalize here so the preset's
  // explicit world distance remains predictable, especially on Low.
  Comp->SetCullDistances(FMath::RoundToInt(Range.X/ViewScale),FMath::RoundToInt(Range.Y/ViewScale));
  const float LODScales[]={1.f,2.f,5.f,8.f}; Comp->SetLODDistanceScale(LODScales[FMath::Clamp(Preset,0,3)]);
 }
}
void ACitixCityChunk::RefreshDetailDrawDistance(int32 Preset) {
 for(auto& Entry:SurfaceComponents) if(IsDistanceDetail(Entry.Key) && Entry.Value) ApplyDetailRange(Entry.Value,Preset);
}

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

 // Small detail respects a world-distance floor even when Low scales other scenery.
 Comp->bEnableDensityScaling=false;
 if(IsDistanceDetail(Surface)) {
  const auto* Settings=UCitixGraphicsSettings::Get(); ApplyDetailRange(Comp,Settings ? Settings->EffectivePreset() : 2);
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
