// Copyright Epic Games, Inc. All Rights Reserved.
// Road graph overlay: draws the routable graph the traffic and pedestrians walk on.
//
// This is a DIAGNOSTIC tool. The graph is normally invisible - agents reference edge
// indices and lane offsets, never geometry - which makes traffic behaviour hard to reason
// about. This draws it in world space so it can be inspected while flying around.
//
// Everything is instanced: one instanced mesh component per colour bucket, so the whole
// overlay is about a dozen draw calls and there is no per-frame cost. It is rebuilt only
// when the mode changes or the graph is regenerated.
//
// Why not DrawDebugLine? The line batcher draws through the translucent pass, and lines
// added to it do not appear in this project's standalone (-game) runs. Instanced slabs are
// deterministic, occlude correctly, and show up in the editor viewport too.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixRoadGraphDebug.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UTextRenderComponent;
struct FCitixRoadNetwork;
class UCitixCitySettings;

UCLASS(NotBlueprintable)
class CITIX_API ACitixRoadGraphDebug : public AActor
{
	GENERATED_BODY()

public:
	ACitixRoadGraphDebug();

	virtual void BeginPlay() override;

	/** Keeps the floating labels turned towards the camera so they read from any angle. */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Rebuild the overlay from the graph.
	 *   1 = edges (coloured by class) + direction + nodes
	 *   2 = 1 + lane centrelines for both directions
	 *   3 = 2 + node labels (index and degree)
	 *   4 = 3 + edge labels (index, class, length) - the heaviest mode
	 */
	void Build(const FCitixRoadNetwork& Network, const UCitixCitySettings& Settings, int32 Mode);

	/** Total instanced pieces drawn (diagnostics). */
	int32 GetInstanceCount() const;

protected:
	/** Layer for one colour bucket, created on first use. */
	UInstancedStaticMeshComponent* GetLayer(const FName& Name, const FLinearColor& Colour);

	/** One box, yawed about Z. The primitive used for every piece of the overlay. */
	void AddBox(const FName& Layer, const FVector& Centre, const FVector& Size, float YawDegrees = 0.f);

	/** A box between two ground points, at a height, so it reads as a line. */
	void AddSlab(const FName& Layer, const FVector2D& A, const FVector2D& B,
		float Z, float Width, float Height);

	void AddLabel(const FString& Text, const FVector& Location, const FLinearColor& Colour, float WorldSize);

	/** Drop every instance and label, keeping the components for reuse. */
	void Clear();

	/** Point every label at the current camera. */
	void FaceLabelsToCamera();

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> GraphLayers;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> Labels;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BoxMesh;

	/** The mode the last Build used, so BeginPlay can redraw after a level load. */
	int32 BuiltMode = 0;
};
