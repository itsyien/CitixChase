// Copyright Epic Games, Inc. All Rights Reserved.
// City generator.
//
// PHASE 1 (done)  : the macro plan - river, districts, arterials, bridges, secondary roads,
//                   blocks - produced as data and validated.
// PHASE 2 (this)  : the city built ON that plan - roads, junctions, river frontage, block
//                   platforms, parcels, buildings, landmarks, street furniture - plus
//                   traffic, pedestrians and street lighting on the new road graph.
//
// The plan is the single source of layout: nothing here invents streets, blocks or
// orientation of its own.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "City/CitixRoadNetwork.h"
#include "City/CitixHillsideLayout.h"
#include "City/CitixCityPlan.h"
#include "CitixTypes.h"
#include "CitixCityGenerator.generated.h"

class ACitixCityPreview;
class ACitixCityChunk;
class ACitixRoadGraphDebug;
class ACitixTrafficSystem;
class ACitixPedestrianSystem;
class ACitixStreetLightSystem;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class CITIX_API ACitixCityGenerator : public AActor
{
	GENERATED_BODY()

public:
	ACitixCityGenerator();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Plan the city and build it. Safe to call repeatedly (clears first). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Citix|Generation")
	void GenerateCity();

	/** Destroy every piece of generated city content and reset state. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Citix|Generation")
	void ClearCity();

	UFUNCTION(BlueprintPure, Category = "Citix|Generation")
	bool IsGenerated() const { return bGenerated; }

	/** Seed the city was actually built from (authoritative identity). */
	int32 GetResolvedSeed() const { return ResolvedSeed; }

	/** The macro plan produced by the last GenerateCity call. */
	const FCitixCityPlan& GetCityPlan() const { return Plan; }
 bool IsDryFootprint(const FVector& Position, const FVector2D& HalfSize, float Yaw) const;
 static bool ValidateChaseSurface(UWorld* World, const FVector& Position, const FVector& HalfSize, float Yaw, const AActor* Ignore, FTransform& Out, bool Clearance = true, const AActor* AdditionalIgnore = nullptr);

	/** The routable road graph, built from the plan. */
	const FCitixRoadNetwork& GetRoadNetwork() const { return RoadNetwork; }
 const FCitixHillsideLayout& GetHillsideLayout() const {return HillsideLayout;}

	/** Instanced geometry count (city, or preview when preview mode is on). */
	UFUNCTION(BlueprintPure, Category = "Citix|Generation")
	int32 GetTotalInstanceCount() const;

	/** Human-readable summary of the last generation, for the log and the HUD. */
	UFUNCTION(BlueprintPure, Category = "Citix|Generation")
	FString GetGenerationSummary() const { return GenerationSummary; }

	/** District type for a plan district index. */
	ECitixDistrict GetDistrictType(int32 DistrictIndex) const;

	/** A good place to start the player: mid-street, on a major road, away from the river. */
	UFUNCTION(BlueprintPure, Category = "Citix|Generation")
	FTransform GetPlayerSpawnTransform() const;

	/** Find the chunk containing a world position, optionally creating it. */
	ACitixCityChunk* GetChunkForPosition(FVector2D WorldXY, bool bCreateIfMissing = true);

	UFUNCTION(BlueprintPure, Category = "Citix|Generation")
	FIntPoint WorldToChunkCoord(FVector2D WorldXY) const;

	UFUNCTION(BlueprintPure, Category = "Citix|Traffic")
	ACitixTrafficSystem* GetTrafficSystem() const { return TrafficSystem; }

	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	ACitixPedestrianSystem* GetPedestrianSystem() const { return PedestrianSystem; }

	const TArray<FVector>& GetLampLocations() const { return LampLocations; }
	const TArray<FVector>& GetBuildingLightLocations() const { return BuildingLightLocations; }
	const TArray<FLinearColor>& GetBuildingLightColors() const { return BuildingLightColors; }
	const TArray<FVector>& GetLandmarkLocations() const { return LandmarkLocations; }

	// ---- Authoring options -------------------------------------------

	/**
	 * Draw the road graph as a world-space overlay, as a diagnostic tool:
	 *   0 = off
	 *   1 = edges (coloured by class, wider for majors) + direction chevron + nodes
	 *   2 = 1 + lane centrelines for both directions (the geometry traffic drives on)
	 *   3 = 2 + node labels (index and degree)
	 *   4 = 3 + edge labels (index, class, length) - the heaviest mode
	 *
	 * Also settable at runtime with `Citix.RoadGraph 2` (works in PIE, which is where F8
	 * leaves you), at launch with `-CitixRoadGraph=2`, or by ticking it in the details panel
	 * while playing. Nodes are coloured by degree and a dead end (degree 1) is drawn red and
	 * large - the one case where an agent has nowhere to go but back the way it came.
	 * Rebuilt only when the mode changes or the graph is regenerated.
	 *
	 * Label modes are sized for the overview camera (large up close, legible from a few
	 * hundred metres); modes 1-2 are the street-level views.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Debug", meta = (ClampMin = "0", ClampMax = "4"))
	int32 RoadGraphDebug = 0;

	UFUNCTION(BlueprintCallable, Category = "Citix|Debug")
	void SetRoadGraphDebug(int32 InMode);

	UFUNCTION(BlueprintPure, Category = "Citix|Debug")
	int32 GetRoadGraphDebug() const { return RoadGraphDebug; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Generation")
	bool bAutoGenerateOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Generation")
	bool bHillsideMap = false;

	/** Overrides UCitixCitySettings::Seed when >= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Generation", meta = (ClampMin = "-1"))
	int32 SeedOverride = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Content")
	bool bGenerateGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Content")
	bool bGenerateRoads = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Content")
	bool bGenerateBlocks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Content")
	bool bGenerateBuildings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Content")
	bool bGenerateProps = true;

	/** Draw the planning diagram instead of the city. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Content")
	bool bShowPlanningPreview = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic")
	bool bSpawnTraffic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic")
	TSubclassOf<ACitixTrafficSystem> TrafficSystemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians")
	bool bSpawnPedestrians = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians")
	TSubclassOf<ACitixPedestrianSystem> PedestrianSystemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bSpawnStreetLights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	TSubclassOf<ACitixStreetLightSystem> StreetLightSystemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|World")
	bool bRecordBuildingLights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Generation")
	bool bLogStats = true;

protected:
	void CreateChunks();
	void CreateGroundPlane();
	void BuildRoadGraph();
	void BuildCity();
	void SpawnTraffic();
	void SpawnPedestrians();
	void SpawnStreetLights();
	void FinishChunks();

	/** Queue a box into whichever chunk contains it. */
	void EmitBoxInstance(const FCitixBoxInstance& Box);

	/** Spawn, rebuild or destroy the road graph overlay to match RoadGraphDebug. */
	void RefreshRoadGraphOverlay();

	/** Destroy every generated city actor in the level, tracked or not. */
	void DestroyGeneratedActors();

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TMap<FIntPoint, TObjectPtr<ACitixCityChunk>> Chunks;

	UPROPERTY()
	TObjectPtr<ACitixCityPreview> Preview;

	UPROPERTY()
	TObjectPtr<class ACitixHillsideBuilder> HillsideBuilder;

	UPROPERTY()
	TObjectPtr<ACitixTrafficSystem> TrafficSystem;

	UPROPERTY()
	TObjectPtr<ACitixPedestrianSystem> PedestrianSystem;

	UPROPERTY()
	TObjectPtr<ACitixStreetLightSystem> StreetLightSystem;

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> GroundMeshComponent;

	FCitixCityPlan Plan;
	FCitixRoadNetwork RoadNetwork;
 FCitixHillsideLayout HillsideLayout;
	FRandomStream Rng;

	TArray<FVector> LampLocations;
	TArray<FVector> BuildingLightLocations;
	TArray<FLinearColor> BuildingLightColors;
	TArray<FVector> LandmarkLocations;

	FString GenerationSummary;
	int32 ResolvedSeed = 0;
	bool bGenerated = false;

	/** The road graph overlay actor, created on demand by RefreshRoadGraphOverlay. */
	UPROPERTY()
	TObjectPtr<ACitixRoadGraphDebug> RoadGraphOverlay;

	/** Mode the overlay was last built at, so the tick only rebuilds it when it changes. */
	int32 RoadGraphDrawnMode = -1;
};
