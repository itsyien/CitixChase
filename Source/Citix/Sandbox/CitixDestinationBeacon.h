// Copyright Epic Games, Inc. All Rights Reserved.
// Destination beacon: one glowing pillar per player (server or local), tinted
// like its objective. Replaces the old single shared beacon component.

#pragma once

#include "CoreMinimal.h"
#include "Core/CitixTypes.h"
#include "GameFramework/Actor.h"
#include "CitixDestinationBeacon.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixDestinationBeacon : public AActor
{
	GENERATED_BODY()

public:
	ACitixDestinationBeacon();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetDestination(const FVector& Location, ECitixSurface Surface);
	void SetBreakawayStation(const FVector& Location,int32 Index,float Width=1200.f);
 void SetRelayProjection(const FVector& Location);
 void SetExitProjection(const FVector& Location);
	void Hide();

	/** Respawn beam (server): tall blue pillar, auto-destroyed. Replicates. */
	void ShowRespawnBeam(const FVector& Location, float Duration = 6.f);

protected:
	UFUNCTION()
	void OnRep_BeamVisible();

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BeaconMesh;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StationFrame;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GateEdges;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StationLights;
	UPROPERTY(ReplicatedUsing = OnRep_BeamVisible) bool bBreakawayStation = false;
	UPROPERTY(Replicated) int32 StationIndex = INDEX_NONE;
 UPROPERTY(ReplicatedUsing=OnRep_BeamVisible) float GateWidth = 1200.f;
 UPROPERTY(ReplicatedUsing=OnRep_BeamVisible) bool bRelayProjection = false;
 UPROPERTY(ReplicatedUsing=OnRep_BeamVisible) bool bExitProjection=false;
 FLinearColor LastGlow=FLinearColor::Transparent;
 bool bSymbolsInitialized=false;
 bool bLastChaser=false;

	UPROPERTY(ReplicatedUsing = OnRep_BeamVisible)
	bool bBeamVisible = false;

	UPROPERTY(Replicated)
	ECitixSurface BeamSurface = ECitixSurface::EmissiveCool;

	float BeamLife = 0.f;
};
