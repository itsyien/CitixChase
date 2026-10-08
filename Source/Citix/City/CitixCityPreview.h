// Copyright Epic Games, Inc. All Rights Reserved.
// Planning preview: draws the macro plan as a simple, highly readable debug diagram.
//
// This is a DESIGN TOOL, not the city. It draws coloured placeholder geometry for the
// river, the road hierarchy, bridges, open space and the future skyline zones, plus
// floating labels. No buildings, no props, no detail.
//
// Everything is instanced: one instanced mesh component per preview layer, so the whole
// plan is a dozen draw calls.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixTypes.h"
#include "CitixCityPreview.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
struct FCitixCityPlan;
class UCitixCitySettings;

UCLASS(NotBlueprintable)
class CITIX_API ACitixCityPreview : public AActor
{
	GENERATED_BODY()

public:
	ACitixCityPreview();

	virtual void BeginPlay() override;

	/** Draw the plan. Replaces anything already drawn. */
	void Build(const FCitixCityPlan& Plan, const UCitixCitySettings& Settings);

	/** Remove all preview geometry and labels. */
	void Clear();

	/** Total instances drawn (diagnostics). */
	int32 GetPreviewInstanceCount() const;

protected:
	UInstancedStaticMeshComponent* GetLayer(ECitixPreviewLayer Layer);
	void AddBox(ECitixPreviewLayer Layer, const FVector& Centre, const FVector& Size, float YawDegrees);
	void AddLabel(const FString& Text, const FVector& Location, const FLinearColor& Colour, float WorldSize);

	/** Emit a polyline of boxes (river, district outlines). */
	void AddPolyline(ECitixPreviewLayer Layer, const TArray<FVector2D>& Points, float Width,
		float Z, float Height, bool bClosed);

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TMap<ECitixPreviewLayer, TObjectPtr<UInstancedStaticMeshComponent>> PreviewLayers;

	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> Labels;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BoxMesh;

	/** Base material per layer, tinted with the debug colour. */
	UPROPERTY()
	TMap<ECitixPreviewLayer, TObjectPtr<UMaterialInterface>> LayerMaterials;
};
