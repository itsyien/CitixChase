// Copyright Epic Games, Inc. All Rights Reserved.
// A chunk owns the instanced geometry for one square of the city.
// Keeping geometry in chunks (rather than one giant actor) is what lets the city
// scale: components stay spatially clustered, culling stays effective, and chunks
// can later be converted to World Partition cells / level instances.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixTypes.h"
#include "CitixCityChunk.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixCityChunk : public AActor
{
	GENERATED_BODY()

public:
	ACitixCityChunk();

	/** Initialise the chunk. Must be called before QueueBox. */
	void Init(const FIntPoint& InCoord, float InSize);

	/** Queue a world-space box. Cheap: only stores a transform until Finish(). */
	void QueueBox(const FCitixBoxInstance& Box);

	/** Queue a world-space box by components. */
	void QueueBox(ECitixSurface Surface, const FVector& WorldCenter, const FVector& Size, float YawDegrees = 0.f,
		const FVector2D& Flow = FVector2D::ZeroVector);

	/** Create instanced components and upload all queued transforms. */
	void Finish();

	FIntPoint GetCoord() const { return Coord; }
	float GetChunkSize() const { return Size; }
	int32 GetInstanceCount() const { return TotalQueued; }
	int32 GetComponentCount() const { return SurfaceComponents.Num(); }
	int32 GetSurfaceInstanceCount(ECitixSurface Surface) const;

	/** Average world position of the chunk (chunk centre). */
	FVector GetChunkCenter() const;

	/** Refresh existing bush ranges after a graphics preset change. */
	void RefreshDetailDrawDistance(int32 Preset);
	static FVector2D DetailDrawDistance(int32 Preset);

	static FVector ComputeInstanceScale(ECitixSurface Surface, const FVector& Size);

protected:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TMap<ECitixSurface, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> SurfaceComponents;

private:
	/** Pending transforms in chunk-local space. */
	TMap<ECitixSurface, TArray<FTransform>> PendingInstances;

	/** Per-instance flow direction, index-aligned with PendingInstances. */
	TMap<ECitixSurface, TArray<FVector2D>> PendingFlow;

	/** Instance counts per surface (diagnostics). */
	TMap<ECitixSurface, int32> SurfaceCounts;

	FIntPoint Coord = FIntPoint::ZeroValue;
	float Size = 0.f;
	int32 TotalQueued = 0;
	bool bFinished = false;

	UHierarchicalInstancedStaticMeshComponent* GetOrCreateComponent(ECitixSurface Surface);
};
