// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixCityGenerator.h"
#include "City/CitixHillsideBuilder.h"
#include "City/CitixHillsideLayout.h"

#include "City/CitixYienBillboard.h"
#include "City/CitixCityBuilder.h"
#include "City/CitixCityChunk.h"
#include "City/CitixCityPreview.h"
#include "City/CitixRoadGraphDebug.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Pedestrian/CitixPedestrian.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "Traffic/CitixTrafficSystem.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "World/CitixStreetLightSystem.h"
#include "Citix.h"

ACitixCityGenerator::ACitixCityGenerator()
{
	// Ticks only to notice a change of RoadGraphDebug made from the details panel while
	// playing, so the interval is deliberately coarse. (In the editor, use the
	// Citix.RoadGraph console command - it rebuilds immediately.)
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

void ACitixCityGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Ticking exists only to notice RoadGraphDebug changing from somewhere other than
	// SetRoadGraphDebug (the details panel), so the interval is deliberately coarse.
	if (RoadGraphDebug != RoadGraphDrawnMode)
	{
		SetRoadGraphDebug(RoadGraphDebug);
	}
}

void ACitixCityGenerator::SetRoadGraphDebug(int32 InMode)
{
	RoadGraphDebug = FMath::Clamp(InMode, 0, 4);
	RoadGraphDrawnMode = RoadGraphDebug;
	RefreshRoadGraphOverlay();
	UE_LOG(LogCitix, Log, TEXT("[Citix] Road graph overlay: mode %d (%s)."),
		RoadGraphDebug, RoadGraphDebug > 0 ? TEXT("on") : TEXT("off"));
}

void ACitixCityGenerator::RefreshRoadGraphOverlay()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (RoadGraphDebug <= 0)
	{
		if (RoadGraphOverlay)
		{
			RoadGraphOverlay->Destroy();
			RoadGraphOverlay = nullptr;
		}
		return;
	}

	if (!RoadGraphOverlay)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		RoadGraphOverlay = World->SpawnActor<ACitixRoadGraphDebug>(
			ACitixRoadGraphDebug::StaticClass(), FTransform::Identity, Params);
	}

	if (RoadGraphOverlay)
	{
		// Rebuilt from scratch, so switching modes never leaves stale pieces behind.
		RoadGraphOverlay->Build(RoadNetwork, UCitixCitySettings::Get(), RoadGraphDebug);
	}
}

// ---------------------------------------------------------------------------
// Debugging
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GCitixRoadGraphCmd(
	TEXT("Citix.RoadGraph"),
	TEXT("Draw the road graph overlay. 0 = off, 1 = edges + direction + nodes, "
		"2 = + lane centrelines, 3 = + node labels, 4 = + edge labels. "
		"Nodes are coloured by degree (red and large = dead end)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ACitixCityGenerator* Generator = nullptr;
		if (World)
		{
			for (TActorIterator<ACitixCityGenerator> It(World); It; ++It)
			{
				Generator = *It;
				break;
			}
		}
		if (!Generator)
		{
			UE_LOG(LogCitix, Warning, TEXT("Citix.RoadGraph: no ACitixCityGenerator in the world."));
			return;
		}

		if (Args.Num() == 0)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Road graph overlay is mode %d."),
				Generator->GetRoadGraphDebug());
			return;
		}
		Generator->SetRoadGraphDebug(FCString::Atoi(*Args[0]));
	}));

void ACitixCityGenerator::BeginPlay()
{
	Super::BeginPlay();

	// Optional startup override so a run can be launched straight into the overlay:
	//   -CitixRoadGraph=2
	FString ModeString;
	if (FParse::Value(FCommandLine::Get(), TEXT("CitixRoadGraph="), ModeString))
	{
		RoadGraphDebug = FMath::Clamp(FCString::Atoi(*ModeString), 0, 4);
		RoadGraphDrawnMode = -1;
	}

	if (bAutoGenerateOnBeginPlay && UCitixCitySettings::Get().bGenerateOnBeginPlay)
	{
		GenerateCity();
	}
}

void ACitixCityGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearCity();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Chunks and emission
// ---------------------------------------------------------------------------

FIntPoint ACitixCityGenerator::WorldToChunkCoord(FVector2D WorldXY) const
{
	const float ChunkSize = FMath::Max(100.f, UCitixCitySettings::Get().ChunkSize);
	return FIntPoint(
		FMath::FloorToInt(WorldXY.X / ChunkSize),
		FMath::FloorToInt(WorldXY.Y / ChunkSize));
}

ACitixCityChunk* ACitixCityGenerator::GetChunkForPosition(FVector2D WorldXY, bool bCreateIfMissing)
{
	const FIntPoint Coord = WorldToChunkCoord(WorldXY);
	if (TObjectPtr<ACitixCityChunk>* Found = Chunks.Find(Coord))
	{
		if (*Found)
		{
			return *Found;
		}
	}
	if (!bCreateIfMissing)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const float ChunkSize = FMath::Max(100.f, UCitixCitySettings::Get().ChunkSize);
	const FVector Centre((static_cast<float>(Coord.X) + 0.5f) * ChunkSize,
		(static_cast<float>(Coord.Y) + 0.5f) * ChunkSize, 0.f);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACitixCityChunk* Chunk = World->SpawnActor<ACitixCityChunk>(
		ACitixCityChunk::StaticClass(), FTransform(Centre), Params);
	if (Chunk)
	{
		Chunks.Add(Coord, Chunk);
	}
	return Chunk;
}

void ACitixCityGenerator::EmitBoxInstance(const FCitixBoxInstance& Box)
{
	ACitixCityChunk* Chunk = GetChunkForPosition(FVector2D(Box.Center.X, Box.Center.Y), true);
	if (Chunk)
	{
		Chunk->QueueBox(Box.Surface, Box.Center, Box.Size, Box.Rotation.Yaw);
	}
}

void ACitixCityGenerator::CreateChunks()
{
	// Chunks are created lazily by emission; this only pre-creates the ones the plan
	// occupies so the first frame does not pay for allocating them.
	const float ChunkSize = FMath::Max(100.f, UCitixCitySettings::Get().ChunkSize);
	const FIntPoint Min = WorldToChunkCoord(Plan.BoundsMin - FVector2D(ChunkSize));
	const FIntPoint Max = WorldToChunkCoord(Plan.BoundsMax + FVector2D(ChunkSize));
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			GetChunkForPosition(FVector2D(
				(static_cast<float>(X) + 0.5f) * ChunkSize,
				(static_cast<float>(Y) + 0.5f) * ChunkSize), true);
		}
	}
}

void ACitixCityGenerator::FinishChunks()
{
	for (const TPair<FIntPoint, TObjectPtr<ACitixCityChunk>>& Pair : Chunks)
	{
		if (Pair.Value)
		{
			Pair.Value->Finish();
		}
	}
}

int32 ACitixCityGenerator::GetTotalInstanceCount() const
{
	if (Preview)
	{
		return Preview->GetPreviewInstanceCount();
	}
	int32 Total = 0;
	for (const TPair<FIntPoint, TObjectPtr<ACitixCityChunk>>& Pair : Chunks)
	{
		if (Pair.Value)
		{
			Total += Pair.Value->GetInstanceCount();
		}
	}
	return Total;
}

// ---------------------------------------------------------------------------
// Clearing
// ---------------------------------------------------------------------------

void ACitixCityGenerator::DestroyGeneratedActors()
{
 if (GetWorld()) for (TActorIterator<ACitixYienBillboard> It(GetWorld()); It; ++It) if (It->GetOwner()==this) It->Destroy();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (Preview)
	{
		Preview->Clear();
		Preview->Destroy();
		Preview = nullptr;
	}
	if (HillsideBuilder)
	{
		HillsideBuilder->Destroy();
		HillsideBuilder = nullptr;
	}
	if (TrafficSystem)
	{
		TrafficSystem->Destroy();
		TrafficSystem = nullptr;
	}
	if (PedestrianSystem)
	{
		PedestrianSystem->Destroy();
		PedestrianSystem = nullptr;
	}
	if (StreetLightSystem)
	{
		StreetLightSystem->Destroy();
		StreetLightSystem = nullptr;
	}
	if (GroundMeshComponent)
	{
		GroundMeshComponent->DestroyComponent();
		GroundMeshComponent = nullptr;
	}
	for (const TPair<FIntPoint, TObjectPtr<ACitixCityChunk>>& Pair : Chunks)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	Chunks.Reset();

	// Anything left over from an earlier run (including a level saved with the old city).
	for (TActorIterator<ACitixCityChunk> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<ACitixTrafficVehicle> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<ACitixPedestrian> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<ACitixCityPreview> It(World); It; ++It)
	{
		It->Clear();
		It->Destroy();
	}
}

void ACitixCityGenerator::ClearCity()
{
	DestroyGeneratedActors();

	RoadNetwork.Reset();
	Plan = FCitixCityPlan();
	LampLocations.Reset();
	BuildingLightLocations.Reset();
	BuildingLightColors.Reset();
	LandmarkLocations.Reset();
	GenerationSummary.Reset();
	bGenerated = false;

	// The graph the overlay was drawn from is gone, so drop the overlay with it.
	if (RoadGraphOverlay)
	{
		RoadGraphOverlay->Destroy();
		RoadGraphOverlay = nullptr;
	}
	RoadGraphDrawnMode = INDEX_NONE;
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

ECitixDistrict ACitixCityGenerator::GetDistrictType(int32 DistrictIndex) const
{
	return Plan.Districts.IsValidIndex(DistrictIndex) ? Plan.Districts[DistrictIndex].Type
		: ECitixDistrict::OuterCity;
}

FTransform ACitixCityGenerator::GetPlayerSpawnTransform() const
{
	if (bHillsideMap)
	{
		const auto Layout=FCitixHillsideLayout::Build(GetResolvedSeed());
		return FTransform(FRotator(0,45,0),Layout.Spawns[0]+FVector(0,0,110));
	}
	const FVector2D Centre = (Plan.BoundsMin + Plan.BoundsMax) * 0.5f;

	// Start mid-street on a major road, clear of the river, so the player begins on the
	// network rather than on a block or a bridge.
	FTransform Best;
	float BestScore = TNumericLimits<float>::Max();
	bool bFound = false;

	for (const FCitixRoadEdge& Edge : RoadNetwork.Edges)
	{
		if (!RoadNetwork.IsValidNode(Edge.NodeA) || !RoadNetwork.IsValidNode(Edge.NodeB)
			|| Edge.bBridge || !Edge.bDrivable)
		{
			continue;
		}
		if (Edge.RoadClass != ECitixRoadClass::Boulevard && Edge.RoadClass != ECitixRoadClass::Arterial
			&& Edge.RoadClass != ECitixRoadClass::Highway)
		{
			continue;
		}

		const FVector2D A = RoadNetwork.Nodes[Edge.NodeA].Position;
		const FVector2D B = RoadNetwork.Nodes[Edge.NodeB].Position;
		const FVector2D Mid = (A + B) * 0.5f;

		// Keep clear of the water: nearest point on the river centreline must be further
		// than the river half width plus the road corridor and a margin.
		FVector2D Nearest;
		FVector2D RiverDirection;
		Plan.GetRiverFrame(0.f, Nearest, RiverDirection);
		{
			float BestRiverDistance = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Plan.RiverPoints.Num(); ++Index)
			{
				const float Distance = static_cast<float>(FVector2D::Distance(Plan.RiverPoints[Index], Mid));
				if (Distance < BestRiverDistance)
				{
					BestRiverDistance = Distance;
					Nearest = Plan.RiverPoints[Index];
				}
			}
			const float Clearance = Plan.GetHalfWidth(0.5f) + Edge.CorridorWidth * 0.5f + 3000.f;
			if (FVector2D::Distance(Nearest, Mid) < Clearance)
			{
				continue;
			}
		}

		const float Score = static_cast<float>(FVector2D::Distance(Mid, Centre));
		if (Score < BestScore)
		{
			BestScore = Score;
			const FVector2D Direction = (B - A).GetSafeNormal();
			const FVector2D Right(-Direction.Y, Direction.X);
			// Sit in the right-hand lane rather than on the centre line, so the player
			// does not start straddling a raised median.
			const FVector2D LaneCentre = Mid + Right * (Edge.CorridorWidth * 0.18f);
			Best = FTransform(
				FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)), 0.f),
				FVector(LaneCentre.X, LaneCentre.Y, 140.f));
			bFound = true;
		}
	}

	if (!bFound)
	{
		Best = FTransform(FRotator::ZeroRotator, FVector(Centre.X, Centre.Y, 140.f));
	}
	return Best;
}

void ACitixCityGenerator::CreateGroundPlane()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UCitixCitySettings& Settings = UCitixCitySettings::Get();

	if (!GroundMeshComponent)
	{
		GroundMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("Ground"));
		if (!GroundMeshComponent)
		{
			return;
		}
		GroundMeshComponent->SetupAttachment(SceneRoot);
		GroundMeshComponent->RegisterComponent();
		GroundMeshComponent->SetMobility(EComponentMobility::Movable);
		GroundMeshComponent->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::Ground));
		if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::Ground))
		{
			GroundMeshComponent->SetMaterial(0, Material);
		}
		GroundMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GroundMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
		GroundMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		GroundMeshComponent->SetCanEverAffectNavigation(false);
		GroundMeshComponent->SetCastShadow(false);
	}

	// One slab sized to the plan: one draw call, one collider.
	const FVector2D Centre = (Plan.BoundsMin + Plan.BoundsMax) * 0.5f;
	const FVector2D Extent = (Plan.BoundsMax - Plan.BoundsMin) * 0.5f + FVector2D(Settings.GroundMargin);
	GroundMeshComponent->SetWorldLocation(
		FVector(Centre.X, Centre.Y, -Settings.GroundThickness * 0.5f));
	GroundMeshComponent->SetWorldScale3D(FVector(
		FMath::Max(100.f, Extent.X * 2.f) / FCitixSurfaceLibrary::PrimitiveSize,
		FMath::Max(100.f, Extent.Y * 2.f) / FCitixSurfaceLibrary::PrimitiveSize,
		Settings.GroundThickness / FCitixSurfaceLibrary::PrimitiveSize));
}

void ACitixCityGenerator::BuildRoadGraph()
{
	RoadNetwork.BuildFromPlan(Plan, UCitixCitySettings::Get());
}

void ACitixCityGenerator::BuildCity()
{
	const UCitixCitySettings& Settings = UCitixCitySettings::Get();

	// The lamp pool needs to know which lamps are inside lit buildings; record one
	// emitter at the base of every tall building as the city is emitted.
	TArray<FVector> EmitterLocations;
	TArray<FLinearColor> EmitterColours;

	FCitixBuildResult BuildResult;
	{
		FCitixBuildResult Result = FCitixCityBuilder::Build(
			Plan, RoadNetwork, Settings, Rng,
			[this](const FCitixBoxInstance& Box) { EmitBoxInstance(Box); },
			LampLocations, LandmarkLocations);
		BuildResult = Result;
	}

	// Building emitters, so the night light pool has somewhere to point at night.
	if (bRecordBuildingLights)
	{
		for (const FVector& Landmark : LandmarkLocations)
		{
			BuildingLightLocations.Add(FVector(Landmark.X, Landmark.Y, 1400.f));
			BuildingLightColors.Add(FLinearColor(1.15f, 0.76f, 0.36f));
		}
	}

	FinishChunks();

	GenerationSummary = FString::Printf(
		TEXT("roads %d, junctions %d, local streets %d, blocks %d, parcels %d, buildings %d, landmarks %d, hero instances %d, lamps %d"),
		BuildResult.RoadSegments, BuildResult.Junctions, BuildResult.LocalStreets,
		BuildResult.Blocks, BuildResult.Parcels, BuildResult.Buildings,
		BuildResult.Landmarks, BuildResult.HeroInstances, BuildResult.LampLocations);

	UE_LOG(LogCitix, Log, TEXT("[Citix] PHASE 2 BUILD: %s, instances %d."),
		*GenerationSummary, GetTotalInstanceCount());
}

void ACitixCityGenerator::SpawnTraffic()
{
	if (!bSpawnTraffic || RoadNetwork.Edges.Num() == 0)
	{
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixNoTraffic")))
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic disabled by -CitixNoTraffic."));
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UClass* Class = TrafficSystemClass ? TrafficSystemClass.Get() : ACitixTrafficSystem::StaticClass();
	if (!Class)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TrafficSystem = World->SpawnActor<ACitixTrafficSystem>(Class, FTransform::Identity, Params);
	if (TrafficSystem)
	{
		TrafficSystem->Initialize(RoadNetwork, UCitixCitySettings::Get());
	}
}

void ACitixCityGenerator::SpawnPedestrians()
{
	if (!bSpawnPedestrians || RoadNetwork.Edges.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UClass* Class = PedestrianSystemClass ? PedestrianSystemClass.Get() : ACitixPedestrianSystem::StaticClass();
	if (!Class)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PedestrianSystem = World->SpawnActor<ACitixPedestrianSystem>(Class, FTransform::Identity, Params);
	if (PedestrianSystem)
	{
		PedestrianSystem->Initialize(RoadNetwork, UCitixCitySettings::Get());
	}
}

void ACitixCityGenerator::SpawnStreetLights()
{
	if (!bSpawnStreetLights || LampLocations.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UClass* Class = StreetLightSystemClass ? StreetLightSystemClass.Get() : ACitixStreetLightSystem::StaticClass();
	if (!Class)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	StreetLightSystem = World->SpawnActor<ACitixStreetLightSystem>(Class, FTransform::Identity, Params);
	if (StreetLightSystem)
	{
		StreetLightSystem->SetLampLocations(LampLocations);
		StreetLightSystem->SetBuildingEmitters(BuildingLightLocations, BuildingLightColors);
	}
}

void ACitixCityGenerator::GenerateCity()
{
	if (bGenerated)
	{
		ClearCity();
	}

	const UCitixCitySettings& Settings = UCitixCitySettings::Get();
	const double StartTime = FPlatformTime::Seconds();
	ResolvedSeed = (SeedOverride >= 0) ? SeedOverride : Settings.Seed;
	if (bHillsideMap)
	{
		HillsideLayout=FCitixHillsideLayout::Build(GetResolvedSeed()); const auto& Layout=HillsideLayout;
		FBox2D LandBounds(ForceInit); for(const auto& P:Layout.CoastBoundary) LandBounds+=P;
		Plan=FCitixCityPlan(); Plan.BoundsMin=LandBounds.Min; Plan.BoundsMax=LandBounds.Max;
		RoadNetwork=Layout.BuildRoadNetwork(); Plan.Report.RoadCount=RoadNetwork.Edges.Num();
        if(RoadNetwork.Edges.ContainsByPredicate([&](const auto& E){const FVector A(RoadNetwork.Nodes[E.NodeA].Position,RoadNetwork.Nodes[E.NodeA].Elevation),B(RoadNetwork.Nodes[E.NodeB].Position,RoadNetwork.Nodes[E.NodeB].Elevation); return FMath::Abs(A.Z-B.Z)>FVector::Dist2D(A,B)*Layout.Settings.MaxGrade+.01f;}))
        {
            GenerationSummary=TEXT("Hillside settings exceed the maximum road grade");
            UE_LOG(LogCitix,Error,TEXT("[CitixHillside] %s"),*GenerationSummary); return;
        }
		FActorSpawnParameters Params; Params.Owner=this;
		Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HillsideBuilder=GetWorld()->SpawnActor<ACitixHillsideBuilder>(ACitixHillsideBuilder::StaticClass(),FTransform::Identity,Params);
		if (!HillsideBuilder) return;
		HillsideBuilder->Build(Layout);
		LampLocations=HillsideBuilder->GetLampLocations();
		LandmarkLocations=HillsideBuilder->GetLandmarkLocations();
		Rng.Initialize(ResolvedSeed+7717);
		bGenerated=true;
		GenerationSummary=FString::Printf(TEXT("Hillside Switchback / %d roads / %.0f m summit"),RoadNetwork.Edges.Num(),Layout.Settings.SummitRoadHeight/100);
		SpawnTraffic(); SpawnStreetLights(); SetRoadGraphDebug(RoadGraphDebug);
		if (bLogStats) UE_LOG(LogCitix,Log,TEXT("[CitixHillside] Generated revision %d seed %d: %d nodes, %d edges"),Layout.Revision,ResolvedSeed,RoadNetwork.Nodes.Num(),RoadNetwork.Edges.Num());
		return;
	}

	// =====================================================================
	// PHASE 1 - the macro plan, and the routable graph built from it.
	// =====================================================================
	FRandomStream PlanRng(ResolvedSeed);
	Plan = FCitixPlanGenerator::Generate(Settings, PlanRng);
	BuildRoadGraph();

	// The graph is complete here, and this is the last point before either the preview or
	// the city path returns, so redraw the overlay (if on) against the new graph.
	SetRoadGraphDebug(RoadGraphDebug);

	// A separate stream for geometry, so retuning the plan does not reshuffle the city.
	Rng.Initialize(ResolvedSeed + 7717);

	UWorld* World = GetWorld();

	// =====================================================================
	// Preview mode: draw the plan diagram instead of the city.
	// =====================================================================
	if (bShowPlanningPreview)
	{
		if (World)
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Preview = World->SpawnActor<ACitixCityPreview>(
				ACitixCityPreview::StaticClass(), FTransform::Identity, Params);
			if (Preview)
			{
				Preview->Build(Plan, Settings);
			}
		}
		bGenerated = true;
		if (bLogStats)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] PHASE 1 PREVIEW: %d districts, %d roads, %d blocks, %d instances."),
				Plan.Districts.Num(), Plan.Report.RoadCount, Plan.Report.BlockCount, GetTotalInstanceCount());
		}
		return;
	}

	// =====================================================================
	// PHASE 2 - build the city on the plan.
	// =====================================================================
	CreateChunks();
	if (bGenerateGround)
	{
		CreateGroundPlane();
	}
	BuildCity();

	SpawnTraffic();
	SpawnPedestrians();
	SpawnStreetLights();

	bGenerated = true;
 ACitixYienBillboard::Populate(this);

	if (bLogStats)
	{
		const double Elapsed = FPlatformTime::Seconds() - StartTime;
		UE_LOG(LogCitix, Log,
			TEXT("[Citix] City generated (seed %d): %d districts, %d plan roads, %d blocks, ")
			TEXT("%d graph nodes, %d graph edges, %d chunks, %d instances, %.0f ms"),
			ResolvedSeed, Plan.Districts.Num(), Plan.Report.RoadCount, Plan.Report.BlockCount,
			RoadNetwork.Nodes.Num(), RoadNetwork.Edges.Num(), Chunks.Num(),
			GetTotalInstanceCount(), Elapsed * 1000.0);
	}
}

// The ground slab extends under the river: floor traces alone cannot establish dry land.
bool ACitixCityGenerator::IsDryFootprint(const FVector& P, const FVector2D& Half, float Yaw) const
{
 if (!bGenerated || P.ContainsNaN()) return false;
 if (bHillsideMap)
 {
  const FRotator Rotation(0,Yaw,0);
  for(const FVector2D Offset:{FVector2D::ZeroVector,FVector2D(Half.X,Half.Y),FVector2D(Half.X,-Half.Y),FVector2D(-Half.X,Half.Y),FVector2D(-Half.X,-Half.Y)})
  {
   const FVector Corner=P+Rotation.RotateVector(FVector(Offset,0));
   if(!HillsideLayout.ContainsLand(FVector2D(Corner))) return false;
  }
  return true;
 }
 // Layout bounds describe streets, not the edge of the drivable ground. The
 // visible slab includes GroundMargin; rejecting that apron made surface
 // recovery behave like an invisible wall before the actual terrain edge.
 FVector2D GroundMin,GroundMax;
 if(GroundMeshComponent) {
  const FBoxSphereBounds& Bounds=GroundMeshComponent->Bounds;
  GroundMin=FVector2D(Bounds.Origin-Bounds.BoxExtent);
  GroundMax=FVector2D(Bounds.Origin+Bounds.BoxExtent);
 } else {
  // Planning-only generation has no components. Mirror CreateGroundPlane's
  // minimum slab size so its geometry classification stays deterministic.
  const FVector2D Center=(Plan.BoundsMin+Plan.BoundsMax)*.5f;
  FVector2D Extent=(Plan.BoundsMax-Plan.BoundsMin)*.5f+FVector2D(UCitixCitySettings::Get().GroundMargin);
  Extent.X=FMath::Max(50.,Extent.X); Extent.Y=FMath::Max(50.,Extent.Y);
  GroundMin=Center-Extent; GroundMax=Center+Extent;
 }
 const FRotator Rotation(0,Yaw,0);
 for (const FVector2D Offset : {FVector2D::ZeroVector, FVector2D(Half.X,Half.Y), FVector2D(Half.X,-Half.Y), FVector2D(-Half.X,Half.Y), FVector2D(-Half.X,-Half.Y)}) {
  const FVector Corner = P + Rotation.RotateVector(FVector(Offset,0));
  const FVector2D Point(Corner.X,Corner.Y);
  if (Point.X < GroundMin.X || Point.Y < GroundMin.Y || Point.X > GroundMax.X || Point.Y > GroundMax.Y) return false;
  bool Water = false;
  for (int32 I=1; I<Plan.RiverPoints.Num(); ++I) {
   const FVector2D A=Plan.RiverPoints[I-1], B=Plan.RiverPoints[I], AB=B-A;
   const float T=FMath::Clamp(FVector2D::DotProduct(Point-A,AB)/FMath::Max(1.f,AB.SizeSquared()),0.f,1.f);
   const float Width=FMath::Lerp(Plan.RiverWidths[I-1],Plan.RiverWidths[I],T);
   if (FVector2D::Distance(Point,A+AB*T)<Width*.5f+80.f) { Water=true; break; }
  }
  if (!Water) continue;
  bool Bridge=false;
  for (const FCitixRoadEdge& E: RoadNetwork.Edges) {
   if (!E.bBridge || !E.bDrivable) continue;
   const FVector A(RoadNetwork.Nodes[E.NodeA].Position,0), B(RoadNetwork.Nodes[E.NodeB].Position,0);
   if (FMath::PointDistToSegment(FVector(Point,0),A,B)<E.CorridorWidth*.5f-90.f) { Bridge=true; break; }
  }
  if (!Bridge) return false;
 }
 return true;
}
bool ACitixCityGenerator::ValidateChaseSurface(UWorld* World, const FVector& P, const FVector& Half, float Yaw, const AActor* Ignore, FTransform& Out, bool Clearance, const AActor* AdditionalIgnore)
{
 if (!World || P.ContainsNaN()) return false;
 ACitixCityGenerator* City=nullptr;
 for (TActorIterator<ACitixCityGenerator> It(World); It; ++It) if (It->IsGenerated()) { City=*It; break; }
 if (!City || !City->IsDryFootprint(P,FVector2D(Half.X,Half.Y),Yaw)) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaseDrySurface),false,Ignore);
 if (AdditionalIgnore) Params.AddIgnoredActor(AdditionalIgnore);
 FHitResult Ground;
 if (!World->LineTraceSingleByObjectType(Ground,P+FVector(0,0,300),P-FVector(0,0,1500),FCollisionObjectQueryParams(ECC_WorldStatic),Params) || Ground.ImpactNormal.Z < .75f) return false;
 const FVector Centre=Ground.ImpactPoint+FVector(0,0,(Half.Z+5.f)/Ground.ImpactNormal.Z);
 const FVector Forward=FVector::VectorPlaneProject(FRotator(0,Yaw,0).Vector(),Ground.ImpactNormal).GetSafeNormal();
 const FQuat Rotation=FRotationMatrix::MakeFromXZ(Forward,Ground.ImpactNormal).ToQuat();
 FHitResult Blocker;
 if (Clearance && World->SweepSingleByChannel(Blocker,Centre,Centre,Rotation,ECC_WorldStatic,FCollisionShape::MakeBox(Half),Params)) return false;
 Out=FTransform(Rotation,Centre);
 return true;
}
