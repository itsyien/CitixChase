// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/CitixTrafficSystem.h"
#include "Chase/CitixChaseGameMode.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Vehicle/CitixCarLibrary.h"
#include "Citix.h"

ACitixTrafficSystem::ACitixTrafficSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
	VehicleClass = ACitixTrafficVehicle::StaticClass();
}

void ACitixTrafficSystem::Initialize(const FCitixRoadNetwork& InNetwork, const UCitixCitySettings& InSettings)
{
	Network = InNetwork;
	TrafficSettings = InSettings.Traffic;
	CruiseSpeed = TrafficSettings.CruiseSpeed;
	const bool Chase=GetWorld() && GetWorld()->GetAuthGameMode<ACitixChaseGameMode>();
 MaxVehicles=FMath::Max(0,FMath::RoundToInt(TrafficSettings.MaxVehicles*(Chase ? 1.5f : 1.f)));
 MaxNearbyVehicles=Chase ? 39 : 26;
	SpawnRadius = FMath::Max(1000.f, TrafficSettings.SpawnRadius);
	DespawnRadius = FMath::Max(SpawnRadius + 1000.f, TrafficSettings.DespawnRadius);
	FollowDistance = FMath::Max(100.f, TrafficSettings.FollowDistance);

	Rng.Initialize(InSettings.Seed + 7717);
	ElapsedTime = 0.f;
	SpawnAccumulator = 0.f;
	EdgeAgentBuckets.Reset();
	WatchedPoolIndex = INDEX_NONE;
	WatchTimer = 0.f;
	WatchElapsed = 0.f;
	// -CitixTrafficWatch logs one car's yaw/motion/crab angle over time, so "cars turn
	// abnormally" can be checked rather than argued about.
	bWatchTraffic = FParse::Param(FCommandLine::Get(), TEXT("CitixTrafficWatch"));
	SpawnBlockedOccupied = 0;
	SpawnBlockedTooClose = 0;
	SpawnBlockedInView = 0;
	SpawnBlockedNoRoom = 0;
	bInitialized = Network.Edges.Num() > 0;

	UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic initialised: %d edges, max %d vehicles."),
		Network.Edges.Num(), MaxVehicles);
}

void ACitixTrafficSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTraffic();
	Super::EndPlay(EndPlayReason);
}

void ACitixTrafficSystem::LogDiagnostics(const FVector2D& PlayerXY) const
{
	struct FEntry { float Distance; int32 AgentIndex; };
	TArray<FEntry> Entries;
	Entries.Reserve(Agents.Num());
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		if (!Agents[Index].bActive)
		{
			continue;
		}
		Entries.Add({ static_cast<float>(FVector2D::Distance(GetAgentLocation(Agents[Index]), PlayerXY)), Index });
	}
	Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Distance < B.Distance; });

	UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic diagnostics: %d active, pool %d, brakingForPlayer %d, avoidEvents %d."),
		Entries.Num(), Pool.Num(), LastPlayerAvoidCount, PlayerAvoidEvents);
	UE_LOG(LogCitix, Log, TEXT("[Citix]   spawn rejections: occupied=%d tooClose=%d inView=%d noRoom=%d"),
		SpawnBlockedOccupied, SpawnBlockedTooClose, SpawnBlockedInView, SpawnBlockedNoRoom);

	const int32 Count = FMath::Min(6, Entries.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const FCitixTrafficAgent& Agent = Agents[Entries[i].AgentIndex];
		UE_LOG(LogCitix, Log,
			TEXT("[Citix]   agent %d: %.0fcm speed=%.0f->%.0f gap=%.0f lead=%.0f turn=%.0f deg junction=%.0f %s lane=%d"),
			Entries[i].AgentIndex, Entries[i].Distance, Agent.Speed, Agent.DiagTargetSpeed,
			Agent.DiagGap, Agent.DiagLeadSpeed, Agent.DiagTurnDegrees, Agent.DiagJunctionDistance,
			Agent.bDiagBraking ? TEXT("BRAKE") : TEXT("     "), Agent.Lane);
		// yaw = where the body points; motion = where it is going; crab = the difference.
		// Small crab and a bounded yaw rate are what "behaving normally" means.
		UE_LOG(LogCitix, Log,
			TEXT("[Citix]     yaw=%.1f motion=%.1f crab=%.1f yawRate=%.1f deg/s pos=(%.0f, %.0f)"),
			Agent.DiagYaw, Agent.DiagMoveYaw, Agent.DiagHeadingError, Agent.DiagYawRate,
			GetAgentLocation(Agent).X, GetAgentLocation(Agent).Y);
	}

	// Tightest same-lane spacing in the population: this is the number that proves
	// whether cars are being allowed to overlap one another.
	float MinCentreGap = TNumericLimits<float>::Max();
	int32 StoppedInJunction = 0;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		if (!Agents[Index].bActive)
		{
			continue;
		}
		// A car that is stopped, or nearly, while still inside the crossing it entered.
		if (Agents[Index].Speed < 50.f
			&& Agents[Index].BlendFromEdge != INDEX_NONE
			&& Agents[Index].Distance < Agents[Index].JunctionClearDistance)
		{
			++StoppedInJunction;
		}
		for (int32 Other = Index + 1; Other < Agents.Num(); ++Other)
		{
			if (!Agents[Other].bActive
				|| Agents[Other].EdgeIndex != Agents[Index].EdgeIndex
				|| Agents[Other].bForward != Agents[Index].bForward
				|| Agents[Other].Lane != Agents[Index].Lane)
			{
				continue;
			}
			MinCentreGap = FMath::Min(MinCentreGap, FMath::Abs(Agents[Other].Distance - Agents[Index].Distance));
		}
	}
	if (MinCentreGap < TNumericLimits<float>::Max())
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix]   tightest centre-to-centre spacing in a lane: %.0f cm (car length %.0f cm)."),
			MinCentreGap, VehicleHalfLength * 2.f);
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix]   cars stopped inside a junction: %d (should be 0)."), StoppedInJunction);
}

void ACitixTrafficSystem::WatchTick(float DeltaSeconds, const FVector2D& PlayerXY)
{
	if (!bWatchTraffic)
	{
		return;
	}
	WatchElapsed += DeltaSeconds;

	// Pick one car, once, and prefer one with a turn coming up - a single trajectory through
	// a corner is what needs judging, and a car that happens to be on a long straight proves
	// only half the case.
	if (WatchedPoolIndex == INDEX_NONE)
	{
		if (WatchElapsed < 3.f)
		{
			return;
		}
		float Nearest = TNumericLimits<float>::Max();
		float NearestTurning = TNumericLimits<float>::Max();
		int32 TurningPoolIndex = INDEX_NONE;
		for (const FCitixTrafficAgent& Candidate : Agents)
		{
			if (!Candidate.bActive || Candidate.Speed < 500.f)
			{
				continue;
			}
			const float DistanceSquared = static_cast<float>(
				FVector2D::DistSquared(GetAgentLocation(Candidate), PlayerXY));
			if (DistanceSquared < Nearest)
			{
				Nearest = DistanceSquared;
				WatchedPoolIndex = Candidate.PoolIndex;
			}
			const bool bTurningSoon = Candidate.DiagTurnDegrees > 25.f
				&& Candidate.DiagJunctionDistance > 0.f
				&& Candidate.DiagJunctionDistance < 20000.f;
			if (bTurningSoon && DistanceSquared < NearestTurning)
			{
				NearestTurning = DistanceSquared;
				TurningPoolIndex = Candidate.PoolIndex;
			}
		}
		if (TurningPoolIndex != INDEX_NONE)
		{
			WatchedPoolIndex = TurningPoolIndex;
		}
		if (WatchedPoolIndex == INDEX_NONE)
		{
			return;
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic watch: monitoring pool slot %d."), WatchedPoolIndex);
		WatchTimer = 0.f;
	}

	WatchTimer += DeltaSeconds;
	if (WatchTimer < 0.25f)
	{
		return;
	}
	WatchTimer = 0.f;

	const FCitixTrafficAgent* Watched = nullptr;
	for (const FCitixTrafficAgent& Candidate : Agents)
	{
		if (Candidate.bActive && Candidate.PoolIndex == WatchedPoolIndex)
		{
			Watched = &Candidate;
			break;
		}
	}
	if (!Watched)
	{
		// Recycled: choose another one.
		WatchedPoolIndex = INDEX_NONE;
		return;
	}

	const FVector2D Location = GetAgentLocation(*Watched);
	UE_LOG(LogCitix, Log,
		TEXT("[watch] t=%.1f pos=(%.0f, %.0f) speed=%.0fkm/h yaw=%.1f motion=%.1f crab=%.1f yawRate=%.0f/s edge=%d lane=%d gap=%.0f target=%.0f"),
		WatchElapsed, Location.X, Location.Y, Watched->Speed * 0.036f,
		Watched->DiagYaw, Watched->DiagMoveYaw, Watched->DiagHeadingError, Watched->DiagYawRate,
		Watched->EdgeIndex, Watched->Lane, Watched->DiagGap, Watched->DiagTargetSpeed);
}

void ACitixTrafficSystem::RefreshParkedVehicles(const FVector2D& PlayerXY)
{
	UWorld* World = GetWorld();
	if (!World || MaxParkedVehicles <= 0 || Network.Edges.Num() == 0)
	{
		return;
	}

	// Only reconsider once the player has moved away from where the cars were placed.
	if (bParkedInitialised && FVector2D::DistSquared(PlayerXY, ParkedAnchor)
		< FMath::Square(ParkedRefreshDistance))
	{
		return;
	}
	ParkedAnchor = PlayerXY;
	bParkedInitialised = true;

	while (ParkedPool.Num() > MaxParkedVehicles)
	{
		if (ACitixTrafficVehicle* Extra = ParkedPool.Pop())
		{
			Extra->Destroy();
		}
	}
	while (ParkedPool.Num() < MaxParkedVehicles)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACitixTrafficVehicle* Vehicle = World->SpawnActor<ACitixTrafficVehicle>(
			VehicleClass ? VehicleClass.Get() : ACitixTrafficVehicle::StaticClass(),
			FTransform::Identity, Params);
		if (!Vehicle)
		{
			break;
		}
		Vehicle->InitializeVehicle(FCitixCarLibrary::GetRandomTrafficType(Rng),
			FCitixCarLibrary::GetRandomPaintColor(Rng), Rng.RandRange(1, MAX_int32 - 1));
		Vehicle->SetVehicleVisible(false);
		ParkedPool.Add(Vehicle);
	}

	// Move ONLY the cars the player cannot currently see. A hidden car counts as
	// unplaced. This is what stops parked cars teleporting in front of the player.
	int32 Relocated = 0;
	for (ACitixTrafficVehicle* Vehicle : ParkedPool)
	{
		if (!Vehicle)
		{
			continue;
		}

		const bool bUnplaced = Vehicle->IsHidden();
		const FVector2D CurrentLocation(Vehicle->GetActorLocation().X, Vehicle->GetActorLocation().Y);
		const bool bFarEnough = FVector2D::DistSquared(CurrentLocation, PlayerXY)
			> FMath::Square(ParkedRecycleDistance);
		// Only move a parked car that is far away AND not currently visible.
		if (!bUnplaced && !(bFarEnough && !IsPointVisible(CurrentLocation, 80.f)))
		{
			continue; // still in sight: leave it exactly where it is
		}

		// Find a new kerbside slot that is also out of sight.
		bool bFound = false;
		FVector2D SlotLocation = FVector2D::ZeroVector;
		float SlotYaw = 0.f;

		for (int32 Attempt = 0; Attempt < 24 && !bFound; ++Attempt)
		{
			if (Rng.FRand() < ParkedSkipChance)
			{
				continue;
			}

			const int32 EdgeIndex = Rng.RandRange(0, Network.Edges.Num() - 1);
			const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
			if (!Edge.bDrivable || Network.EdgeLength(EdgeIndex) < 1500.f)
			{
				continue;
			}

			const FCitixRoadSpec Spec = UCitixCitySettings::Get().GetRoadSpec(Edge.RoadClass);
			if (Spec.CarriagewayWidth() < 400.f)
			{
				continue;
			}

			const FVector2D A = Network.Nodes[Edge.NodeA].Position;
			const FVector2D B = Network.Nodes[Edge.NodeB].Position;
			const FVector2D Direction = Network.EdgeDirection(EdgeIndex);
			const bool bForward = Rng.FRand() < 0.5f;
			const FVector2D Travel = bForward ? Direction : -Direction;
			const FVector2D Origin = bForward ? A : B;
			const float Distance = Rng.FRandRange(300.f, FMath::Max(400.f, Network.EdgeLength(EdgeIndex) - 300.f));
			const FVector2D Right(-Travel.Y, Travel.X);
			const float KerbOffset = Spec.CarriagewayWidth() * 0.5f - 130.f;
			const FVector2D Candidate = Origin + Travel * Distance + Right * KerbOffset;

			if (static_cast<float>(FVector2D::Distance(Candidate, PlayerXY)) > ParkedRadius)
			{
				continue;
			}
			if (!IsOutOfView(Candidate, ParkedSpawnClearance))
			{
				continue; // would pop into view
			}
			if (IsPointVisible(Candidate, 80.f))
			{
				continue; // the player would see it appear
			}

			SlotLocation = Candidate;
			SlotYaw = FMath::RadiansToDegrees(FMath::Atan2(Travel.Y, Travel.X));
			bFound = true;
		}

		if (!bFound)
		{
			if (bUnplaced)
			{
				Vehicle->SetVehicleVisible(false);
			}
			continue;
		}

		Vehicle->SetVehicleVisible(true);
		Vehicle->SetWheelsVisible(true);
		Vehicle->SetActorLocationAndRotation(FVector(SlotLocation.X, SlotLocation.Y, 0.f),
			FRotator(0.f, SlotYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		++Relocated;
	}

	UE_LOG(LogCitix, Verbose, TEXT("[Citix] Parked cars relocated: %d of %d."), Relocated, ParkedPool.Num());
}

void ACitixTrafficSystem::RefreshPlayerSet()
{
	DriverAnchors.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		FCitixDriverAnchor Anchor;
		const FVector Location = Pawn->GetActorLocation();
		Anchor.Position = FVector2D(Location.X, Location.Y);
		Anchor.Pawn = Pawn;
		// Camera snapshot per driver (the pawn's own facing; widened like the
		// single-player cone test so pooling stays conservative).
		Anchor.View.Location = Location;
		const FVector Forward = Pawn->GetActorForwardVector();
		Anchor.View.Forward = Forward.GetSafeNormal();
		Anchor.View.CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(58.5f));
		Anchor.View.bValid = true;
		DriverAnchors.Add(Anchor);
	}
}

FVector2D ACitixTrafficSystem::NearestPlayerTo(const FVector2D& P, float& OutDistance) const
{
	FVector2D Best = P;
	OutDistance = TNumericLimits<float>::Max();
	for (const FCitixDriverAnchor& Anchor : DriverAnchors)
	{
		const float Distance = FVector2D::Distance(Anchor.Position, P);
		if (Distance < OutDistance)
		{
			OutDistance = Distance;
			Best = Anchor.Position;
		}
	}
	return Best;
}

bool ACitixTrafficSystem::IsFarFromAllPlayers(const FVector2D& P, float Dist) const
{
	const float DistSquared = Dist * Dist;
	for (const FCitixDriverAnchor& Anchor : DriverAnchors)
	{
		if (FVector2D::DistSquared(Anchor.Position, P) <= DistSquared)
		{
			return false;
		}
	}
	return true;
}

bool ACitixTrafficSystem::IsOutOfAllViews(const FVector2D& Point, float FarDistance) const
{
	for (const FCitixDriverAnchor& Anchor : DriverAnchors)
	{
		if (!Anchor.View.bValid)
		{
			return false;
		}
		const FVector2D ToPoint = Point - Anchor.Position;
		if (ToPoint.SizeSquared() <= FarDistance * FarDistance)
		{
			// Close: only counts as out of view when behind this driver.
			const FVector2D Forward2D(Anchor.View.Forward.X, Anchor.View.Forward.Y);
			if (Forward2D.SizeSquared() < 0.01f
				|| FVector2D::DotProduct(ToPoint, Forward2D.GetSafeNormal()) >= 0.f)
			{
				return false;
			}
		}
	}
	return true;
}

bool ACitixTrafficSystem::IsVisibleToAny(const FVector2D& XY, float Z) const
{
	UWorld* World = GetWorld();
	for (const FCitixDriverAnchor& Anchor : DriverAnchors)
	{
		if (!Anchor.View.bValid)
		{
			return true;
		}
		if (FCitixVisibility::IsVisible(World, Anchor.View, FVector(XY.X, XY.Y, Z),
			Anchor.Pawn.Get()))
		{
			return true;
		}
	}
	return false;
}

bool ACitixTrafficSystem::IsPointVisible(const FVector2D& XY, float Z) const
{
	return IsVisibleToAny(XY, Z);
}

bool ACitixTrafficSystem::IsOutOfView(const FVector2D& Point, float FarDistance) const
{
	return IsOutOfAllViews(Point, FarDistance);
}

void ACitixTrafficSystem::SetDensityScale(float InScale)
{
	// Never drop below a trickle, never exceed the pool. The pool is the hard cap.
	DensityScale = FMath::Clamp(InScale, 0.15f, 1.35f);
}

int32 ACitixTrafficSystem::GetDesiredVehicleCount() const
{
	// Clamped to MaxVehicles (not the current pool size): the pool only grows as
	// agents spawn, so clamping to it would deadlock spawning at zero.
	return FMath::Clamp(FMath::RoundToInt(MaxVehicles * DensityScale), 0, MaxVehicles);
}

bool ACitixTrafficSystem::TakeVehicle(ACitixTrafficVehicle* Vehicle)
{
	if (!Vehicle)
	{
		return false;
	}

	for (int32 Index = Agents.Num() - 1; Index >= 0; --Index)
	{
		if (Agents[Index].bActive && Pool.IsValidIndex(Agents[Index].PoolIndex)
			&& Pool[Agents[Index].PoolIndex] == Vehicle)
		{
			const int32 PoolIndex = Agents[Index].PoolIndex;
			ReleaseAgent(Index);
			// The source car is gone for everyone: destroy (replicates) and retire
			// its pool slot so a stale index can never be reused.
			FreePoolIndices.Remove(PoolIndex);
			if (Pool.IsValidIndex(PoolIndex))
			{
				Pool[PoolIndex] = nullptr;
			}
			Vehicle->SetTakenByPlayer(true);
			Vehicle->Destroy();
			return true;
		}
	}

	// Parked car (not a traffic agent): destroy it too.
	for (int32 ParkedIndex = ParkedPool.Num() - 1; ParkedIndex >= 0; --ParkedIndex)
	{
		if (ParkedPool[ParkedIndex] == Vehicle)
		{
			ParkedPool.RemoveAt(ParkedIndex);
			Vehicle->SetTakenByPlayer(true);
			Vehicle->Destroy();
			return true;
		}
	}
	return false;
}

int32 ACitixTrafficSystem::GetActiveVehicleCount() const
{
	int32 Count = 0;
	for (const FCitixTrafficAgent& Agent : Agents)
	{
		if (Agent.bActive)
		{
			++Count;
		}
	}
	return Count;
}

int32 ACitixTrafficSystem::StartleVehicles(const FVector& Center, float Radius, float Duration)
{
	const float RadiusSquared = Radius * Radius;
	const FVector2D CenterXY(Center.X, Center.Y);
	int32 Startled = 0;

	for (FCitixTrafficAgent& Agent : Agents)
	{
		if (!Agent.bActive || !Network.IsValidEdge(Agent.EdgeIndex))
		{
			continue;
		}
		if (FVector2D::DistSquared(GetAgentLocation(Agent), CenterXY) > RadiusSquared)
		{
			continue;
		}
		Agent.StopTimer = FMath::Max(Agent.StopTimer, Duration);
		++Startled;
	}
	return Startled;
}

int32 ACitixTrafficSystem::CountAgentsNear(const FVector& Center, float Radius) const
{
	const float RadiusSquared = Radius * Radius;
	const FVector2D CenterXY(Center.X, Center.Y);
	int32 Count = 0;

	for (const FCitixTrafficAgent& Agent : Agents)
	{
		if (!Agent.bActive || !Network.IsValidEdge(Agent.EdgeIndex))
		{
			continue;
		}
		if (FVector2D::DistSquared(GetAgentLocation(Agent), CenterXY) <= RadiusSquared)
		{
			++Count;
		}
	}
	return Count;
}

void ACitixTrafficSystem::ClearTraffic()
{
	for (TObjectPtr<ACitixTrafficVehicle>& Vehicle : Pool)
	{
		if (Vehicle)
		{
			Vehicle->Destroy();
		}
	}
	Pool.Reset();

	for (TObjectPtr<ACitixTrafficVehicle>& Vehicle : ParkedPool)
	{
		if (Vehicle)
		{
			Vehicle->Destroy();
		}
	}
	ParkedPool.Reset();
	bParkedInitialised = false;
	ParkedAnchor = FVector2D(FLT_MAX);

	FreePoolIndices.Reset();
	Agents.Reset();
}

void ACitixTrafficSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bInitialized || MaxVehicles <= 0)
	{
		return;
	}

	// The simulation lives on the server. Remote copies only interpolate the
	// replicated pool actors and never spawn, recycle or drive anything.
	// NOTE: this must be a net-mode check, not HasAuthority(): locally-spawned
	// actors report local authority even on clients.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	ElapsedTime += DeltaSeconds;

	// Every connected driver anchors pooling: spawns spread across drivers,
	// recycling only happens out of sight of all of them.
	RefreshPlayerSet();
	if (DriverAnchors.Num() == 0)
	{
		return;
	}
	const int32 AnchorIndex = SpawnAnchorCursor++ % DriverAnchors.Num();
	const FVector2D AnchorXY = DriverAnchors[AnchorIndex].Position;
	PlayerPawnWeak = DriverAnchors[AnchorIndex].Pawn;
	// Legacy single-view members track the anchor driver for debug paths.
	CachedPlayerView = DriverAnchors[AnchorIndex].View;
	++VisibilityPhase;

	SpawnAgents(DeltaSeconds, AnchorXY);
	UpdateAgents(DeltaSeconds);
	WatchTick(DeltaSeconds, AnchorXY);
	RefreshParkedVehicles(AnchorXY);

	// Periodic diagnostic during startup so traffic behaviour is easy to verify;
	// it stops after the warm-up window to avoid log spam in normal play.
	TrafficLogTimer += DeltaSeconds;
	if (ElapsedTime < 20.f && TrafficLogTimer >= 5.f)
	{
		TrafficLogTimer = 0.f;
		UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic: %d active / %d pool"),
			GetActiveVehicleCount(), Pool.Num());
	}
}

int32 ACitixTrafficSystem::AcquirePoolIndex()
{
	if (FreePoolIndices.Num() > 0)
	{
		return FreePoolIndices.Pop();
	}
	if (Pool.Num() >= MaxVehicles)
	{
		return INDEX_NONE;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return INDEX_NONE;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACitixTrafficVehicle* Vehicle = World->SpawnActor<ACitixTrafficVehicle>(
		VehicleClass ? VehicleClass.Get() : ACitixTrafficVehicle::StaticClass(), FTransform::Identity, Params);
	if (!Vehicle)
	{
		return INDEX_NONE;
	}

	const FLinearColor Color = FCitixCarLibrary::GetRandomPaintColor(Rng);
	Vehicle->InitializeVehicle(FCitixCarLibrary::GetRandomTrafficType(Rng), Color, Rng.RandRange(1, MAX_int32 - 1));
	Vehicle->SetVehicleVisible(false);

	Pool.Add(Vehicle);
	return Pool.Num() - 1;
}

void ACitixTrafficSystem::ReleaseAgent(int32 AgentIndex)
{
	if (!Agents.IsValidIndex(AgentIndex))
	{
		return;
	}

	const FCitixTrafficAgent& Agent = Agents[AgentIndex];
	if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
	{
		Pool[Agent.PoolIndex]->SetVehicleVisible(false);
		FreePoolIndices.Add(Agent.PoolIndex);
	}

	Agents.RemoveAtSwap(AgentIndex);
}

void ACitixTrafficSystem::SpawnAgents(float DeltaSeconds, const FVector2D& PlayerXY)
{
	const int32 ActiveCount = GetActiveVehicleCount();
	const int32 DesiredCount = GetDesiredVehicleCount();
	if (ActiveCount >= DesiredCount)
	{
		return;
	}

	// Local density cap: once the area around the player is already busy, let cars drive
	// away before adding more. Without this, a player who stops still has cars arriving on
	// every approach, and because in-view cars are never recycled the roads near them fill
	// up into an obvious pool.
	int32 Nearby = 0;
	const float NearbySquared = FMath::Square(NearbyVehicleRadius);
	for (const FCitixTrafficAgent& Agent : Agents)
	{
		if (Agent.bActive
			&& FVector2D::DistSquared(GetAgentLocation(Agent), PlayerXY) < NearbySquared)
		{
			++Nearby;
		}
	}
	if (Nearby >= FMath::Max(4, FMath::RoundToInt(MaxNearbyVehicles * DensityScale)))
	{
		return;
	}

	constexpr float SpawnsPerSecond = 15.f;
	SpawnAccumulator += SpawnsPerSecond * DeltaSeconds;

	int32 Spawned = 0;
	while (SpawnAccumulator >= 1.f)
	{
		SpawnAccumulator -= 1.f;
		if (ActiveCount + Spawned >= DesiredCount)
		{
			break;
		}
		if (TrySpawnAgent(PlayerXY))
		{
			++Spawned;
		}
	}
}

bool ACitixTrafficSystem::TrySpawnAgent(const FVector2D& PlayerXY)
{
	if (Network.Edges.Num() == 0)
	{
		return false;
	}

	// Try a batch of candidate placements and keep the most isolated one that passes
	// every gate. Choosing the roomiest candidate (rather than the first that happens to
	// survive the random loop) is what spreads traffic out instead of dropping new cars
	// into whatever cluster the dice landed on.
	int32 BestEdge = INDEX_NONE;
	int32 BestLane = 0;
	bool BestForward = true;
	float BestAlong = 0.f;
	FVector2D BestLocation = FVector2D::ZeroVector;
	float BestClearance = -1.f;
	float BestRingError = FLT_MAX;

	// Aim new cars at the middle of the spawn ring so they neither appear on top of the
	// player nor all pile up on the outermost road.
	const float RingMid = (SpawnRadius + 2500.f) * 0.5f;

	for (int32 Attempt = 0; Attempt < 14; ++Attempt)
	{
		const int32 EdgeIndex = Rng.RandRange(0, Network.Edges.Num() - 1);
		const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
		if (!Edge.bDrivable)
		{
			continue;
		}

		const float EdgeLength = Network.EdgeLength(EdgeIndex);
		if (EdgeLength < 1500.f)
		{
			continue;
		}

		const FVector2D Mid = (Network.Nodes[Edge.NodeA].Position + Network.Nodes[Edge.NodeB].Position) * 0.5f;
		const float DistanceToPlayer = (Mid - PlayerXY).Size();
		if (DistanceToPlayer < 2500.f || DistanceToPlayer > SpawnRadius)
		{
			++SpawnBlockedTooClose;
			continue;
		}
		// Never spawn in front of the camera: only far ahead or behind the player.
		if (!IsOutOfView(Mid, SpawnClearance))
		{
			++SpawnBlockedInView;
			continue;
		}

		const FCitixRoadSpec Spec = UCitixCitySettings::Get().GetRoadSpec(Edge.RoadClass);
		const int32 HalfLanes = FMath::Max(1, Spec.NumLanes / 2);
		const int32 Lane = Rng.RandRange(0, HalfLanes - 1);
		const bool bForward = Rng.FRand() < 0.5f;
		const float Along = Rng.FRandRange(600.f, FMath::Max(700.f, EdgeLength - 600.f));
		const FVector2D LaneOffset = EdgeRight(EdgeIndex, bForward)
			* (Spec.LaneWidth * (0.5f + static_cast<float>(Lane)));
		const FVector2D Candidate = EdgePoint(EdgeIndex, bForward, Along, LaneOffset);

		// Never spawn a car where the player can see it appear. Test the exact spot,
		// not just the edge midpoint.
		if (IsPointVisible(Candidate, 80.f))
		{
			++SpawnBlockedInView;
			continue;
		}

		// Clearance from every existing car, and free lane ahead, so a new car is never
		// dropped on top of traffic or nose-to-tail with the car in front of the slot.
		float NearestClearance = FLT_MAX;
		bool bLaneBlocked = false;
		for (const FCitixTrafficAgent& Other : Agents)
		{
			if (!Other.bActive)
			{
				continue;
			}
			NearestClearance = FMath::Min(NearestClearance,
				static_cast<float>(FVector2D::Distance(Candidate, GetAgentLocation(Other))));
			if (Other.EdgeIndex == EdgeIndex && Other.bForward == bForward && Other.Lane == Lane)
			{
				const float AlongOther = bForward ? Other.Distance : (EdgeLength - Other.Distance);
				if (FMath::Abs(AlongOther - Along) < SpawnLaneClearance)
				{
					bLaneBlocked = true;
				}
			}
		}
		if (NearestClearance < SpawnClearanceDistance)
		{
			++SpawnBlockedOccupied;
			continue;
		}
		if (bLaneBlocked)
		{
			++SpawnBlockedNoRoom;
			continue;
		}

		const float RingError = FMath::Abs(DistanceToPlayer - RingMid);
		if (NearestClearance > BestClearance + 100.f
			|| (NearestClearance > BestClearance - 100.f && RingError < BestRingError))
		{
			BestClearance = NearestClearance;
			BestRingError = RingError;
			BestEdge = EdgeIndex;
			BestLane = Lane;
			BestForward = bForward;
			BestAlong = Along;
			BestLocation = Candidate;
		}
	}

	if (BestEdge == INDEX_NONE)
	{
		return false;
	}

	const int32 PoolIndex = AcquirePoolIndex();
	if (PoolIndex == INDEX_NONE)
	{
		return false;
	}

	const FCitixRoadSpec Spec = UCitixCitySettings::Get().GetRoadSpec(Network.Edges[BestEdge].RoadClass);

	FCitixTrafficAgent Agent;
	Agent.EdgeIndex = BestEdge;
	Agent.bForward = BestForward;
	Agent.Distance = BestAlong;
	Agent.SpeedFactor = Rng.FRandRange(0.82f, 1.15f);
	Agent.Lane = BestLane;
	Agent.LaneOffset = Spec.LaneWidth * (0.5f + static_cast<float>(BestLane));
	Agent.Speed = CruiseSpeed * 0.35f;
	Agent.CruiseTarget = CruiseSpeed * Agent.SpeedFactor;
	Agent.DesiredSpeed = Agent.CruiseTarget;
	Agent.bActive = true;
	Agent.PoolIndex = PoolIndex;
	// Presentation starts already aligned so a newly spawned car does not slide in.
	Agent.CurrentOffset = EdgeRight(BestEdge, BestForward) * Agent.LaneOffset;
	Agent.CurrentYaw = FMath::RadiansToDegrees(FMath::Atan2(
		Network.EdgeDirection(BestEdge).Y, Network.EdgeDirection(BestEdge).X));
	if (!BestForward)
	{
		Agent.CurrentYaw += 180.f;
	}
	Agent.DiagYaw = Agent.CurrentYaw;
	Agent.DiagMoveYaw = Agent.CurrentYaw;
	Agent.SmoothedLocation = FVector(BestLocation.X, BestLocation.Y, 0.f);
	Agent.bPresentationStarted = true;
	ChoosePlanForAgent(Agent);

	Agents.Add(Agent);
	return true;
}

FVector2D ACitixTrafficSystem::GetAgentDirection(const FCitixTrafficAgent& Agent) const
{
	if (!Network.IsValidEdge(Agent.EdgeIndex))
	{
		return FVector2D(1.f, 0.f);
	}
	const FVector2D Dir = Network.EdgeDirection(Agent.EdgeIndex);
	return Agent.bForward ? Dir : -Dir;
}

FVector2D ACitixTrafficSystem::GetAgentLocation(const FCitixTrafficAgent& Agent) const
{
	const FVector2D Right = EdgeRight(Agent.EdgeIndex, Agent.bForward);
	return GetAgentLocationWithOffset(Agent, Right * Agent.LaneOffset);
}

FVector2D ACitixTrafficSystem::GetAgentLocationWithOffset(const FCitixTrafficAgent& Agent, const FVector2D& Offset) const
{
	return EdgePoint(Agent.EdgeIndex, Agent.bForward, Agent.Distance, Offset);
}

FVector2D ACitixTrafficSystem::EdgeRight(int32 EdgeIndex, bool bForward) const
{
	const FVector2D Travel = bForward ? Network.EdgeDirection(EdgeIndex) : -Network.EdgeDirection(EdgeIndex);
	return FVector2D(-Travel.Y, Travel.X);
}

bool ACitixTrafficSystem::IsEdgeGreen(int32 EdgeIndex, float Time) const
{
	if (!Network.IsValidEdge(EdgeIndex))
	{
		return true;
	}
	const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
	// Roads are no longer axis-aligned, so the light phase splits on the road's own
	// bearing: roughly east-west traffic gets one phase, roughly north-south the other.
	const FVector2D Direction = Network.EdgeDirection(EdgeIndex);
	const bool bHorizontal = FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y);

	const float Cycle = FMath::Max(2.f, TrafficSettings.LightCycleSeconds);
	const float Phase = FMath::Fmod(FMath::Max(0.f, Time), Cycle);
	const bool bPhaseA = Phase < Cycle * 0.5f;
	return bHorizontal ? bPhaseA : !bPhaseA;
}

int32 ACitixTrafficSystem::ChooseNextEdge(int32 CurrentEdge, int32 Node, bool& bOutForward) const
{
	bOutForward = true;
	if (!Network.NodeEdgeIndices.IsValidIndex(Node))
	{
		return INDEX_NONE;
	}

	const TArray<int32>& Incident = Network.NodeEdgeIndices[Node];
	if (Incident.Num() == 0)
	{
		return INDEX_NONE;
	}

	// Incoming travel direction.
	const FCitixRoadEdge& Current = Network.Edges[CurrentEdge];
	const int32 CurrentOther = (Current.NodeA == Node) ? Current.NodeB : Current.NodeA;
	const FVector2D NodePos = Network.Nodes[Node].Position;
	const FVector2D InDir = (NodePos - Network.Nodes[CurrentOther].Position).GetSafeNormal();

	TArray<int32, TInlineAllocator<8>> Candidates;
	TArray<float, TInlineAllocator<8>> Weights;
	float TotalWeight = 0.f;

	for (int32 CandidateIndex : Incident)
	{
		if (CandidateIndex == CurrentEdge || !Network.Edges[CandidateIndex].bDrivable)
		{
			continue;
		}
		const FCitixRoadEdge& Candidate = Network.Edges[CandidateIndex];
		const int32 Other = (Candidate.NodeA == Node) ? Candidate.NodeB : Candidate.NodeA;
		const FVector2D OutDir = (Network.Nodes[Other].Position - NodePos).GetSafeNormal();
		const float Dot = FVector2D::DotProduct(InDir, OutDir);
		const float Weight = FMath::Max(0.06f, 0.5f + 0.5f * Dot);
		Candidates.Add(CandidateIndex);
		Weights.Add(Weight);
		TotalWeight += Weight;
	}

	if (Candidates.Num() == 0)
	{
		// Dead end: turn around.
		bOutForward = (Current.NodeA == Node);
		return CurrentEdge;
	}

	float Pick = Rng.FRandRange(0.f, TotalWeight);
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Pick -= Weights[Index];
		if (Pick <= 0.f)
		{
			const FCitixRoadEdge& Chosen = Network.Edges[Candidates[Index]];
			bOutForward = (Chosen.NodeA == Node);
			return Candidates[Index];
		}
	}

	const FCitixRoadEdge& Chosen = Network.Edges[Candidates.Last()];
	bOutForward = (Chosen.NodeA == Node);
	return Candidates.Last();
}

int32 ACitixTrafficSystem::StraightestContinuation(int32 CurrentEdge, int32 Node, bool& bOutForward) const
{
	bOutForward = true;
	if (!Network.NodeEdgeIndices.IsValidIndex(Node) || !Network.IsValidEdge(CurrentEdge))
	{
		return INDEX_NONE;
	}

	const FCitixRoadEdge& Current = Network.Edges[CurrentEdge];
	const int32 CurrentOther = (Current.NodeA == Node) ? Current.NodeB : Current.NodeA;
	const FVector2D NodePos = Network.Nodes[Node].Position;
	const FVector2D InDir = (NodePos - Network.Nodes[CurrentOther].Position).GetSafeNormal();

	int32 Best = INDEX_NONE;
	float BestDot = -FLT_MAX;
	for (int32 CandidateIndex : Network.NodeEdgeIndices[Node])
	{
		if (CandidateIndex == CurrentEdge || !Network.Edges[CandidateIndex].bDrivable)
		{
			continue;
		}
		const FCitixRoadEdge& Candidate = Network.Edges[CandidateIndex];
		const int32 Other = (Candidate.NodeA == Node) ? Candidate.NodeB : Candidate.NodeA;
		const FVector2D OutDir = (Network.Nodes[Other].Position - NodePos).GetSafeNormal();
		const float Dot = FVector2D::DotProduct(InDir, OutDir);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			Best = CandidateIndex;
			bOutForward = (Candidate.NodeA == Node);
		}
	}

	if (Best == INDEX_NONE)
	{
		// Dead end: the only continuation is to turn around.
		bOutForward = (Current.NodeA == Node);
		return CurrentEdge;
	}
	return Best;
}

void ACitixTrafficSystem::ChoosePlanForAgent(FCitixTrafficAgent& Agent) const
{
	if (!Network.IsValidEdge(Agent.EdgeIndex))
	{
		Agent.NextEdgeIndex = INDEX_NONE;
		return;
	}
	const FCitixRoadEdge& Edge = Network.Edges[Agent.EdgeIndex];
	const int32 EndNode = Agent.bForward ? Edge.NodeB : Edge.NodeA;
	bool bForward = true;
	Agent.NextEdgeIndex = ChooseNextEdge(Agent.EdgeIndex, EndNode, bForward);
	Agent.bNextForward = bForward;
}

FVector2D ACitixTrafficSystem::EdgePoint(int32 EdgeIndex, bool bForward, float AlongTravel, const FVector2D& Offset) const
{
	if (!Network.IsValidEdge(EdgeIndex))
	{
		return FVector2D::ZeroVector;
	}
	const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
	const FVector2D A = Network.Nodes[Edge.NodeA].Position;
	const FVector2D B = Network.Nodes[Edge.NodeB].Position;
	const FVector2D Origin = bForward ? A : B;
	const FVector2D Travel = bForward ? (B - A).GetSafeNormal() : (A - B).GetSafeNormal();
	return Origin + Travel * AlongTravel + Offset;
}

float ACitixTrafficSystem::TurnSpeed(float TurnDegrees, float Cruise)
{
	// A comfort model rather than a real radius: straight on keeps the full free-flow
	// speed, a square corner drops to about a third of it. Cheap and stable, and it does
	// not depend on how finely the graph happens to be tessellated.
	const float T = FMath::Clamp(TurnDegrees / 90.f, 0.f, 1.5f);
	return Cruise * FMath::Lerp(1.f, 0.38f, FMath::Min(1.f, T));
}

float ACitixTrafficSystem::CornerSpeed(float Cruise, float TurnDegrees, int32 NextEdge) const
{
	float Vturn = TurnSpeed(TurnDegrees, Cruise);

	// The corner also has to be swept in the room available on the far side. Turning sharply
	// onto a short block has to be taken slowly: at speed the body cannot follow the path
	// within its yaw rate, which is what a large crab angle looks like.
	if (Network.IsValidEdge(NextEdge))
	{
		const float Room = Network.EdgeLength(NextEdge);
		const float SweepLimited = Room * FMath::Max(1.f, TurnRateDegrees)
			/ (FMath::Max(1.f, TurnDegrees) * FMath::Max(1.f, JunctionBlendYawFactor));
		Vturn = FMath::Min(Vturn, SweepLimited);
	}
	return Vturn;
}

bool ACitixTrafficSystem::IsEdgeEntryBlocked(int32 EdgeIndex, bool bForward, int32 Lane) const
{
	if (!Network.IsValidEdge(EdgeIndex))
	{
		return false;
	}
	const TArray<int32>* Bucket = EdgeAgentBuckets.Find(EdgeIndex);
	if (!Bucket)
	{
		return false;
	}

	// The lane this car would end up in on that edge, which is its current lane clamped.
	const FCitixRoadSpec Spec = UCitixCitySettings::Get().GetRoadSpec(Network.Edges[EdgeIndex].RoadClass);
	const int32 EntryLane = FMath::Clamp(Lane, 0, FMath::Max(0, Spec.NumLanes / 2 - 1));
	const float EdgeLength = Network.EdgeLength(EdgeIndex);

	for (int32 OtherIndex : *Bucket)
	{
		if (!Agents.IsValidIndex(OtherIndex))
		{
			continue;
		}
		const FCitixTrafficAgent& Other = Agents[OtherIndex];
		if (!Other.bActive || Other.bForward != bForward || Other.Lane != EntryLane)
		{
			continue;
		}
		const float FromEntry = bForward ? Other.Distance : (EdgeLength - Other.Distance);
		const float Needed = VehicleHalfLength * 2.f + HardBumperGap
			+ FMath::Max(0.f, Other.Speed) * 0.25f;
		if (FromEntry < Needed)
		{
			return true;
		}
	}
	return false;
}

void ACitixTrafficSystem::MoveAgentBucket(int32 AgentIndex, int32 FromEdge)
{
	const FCitixTrafficAgent& Agent = Agents[AgentIndex];
	if (TArray<int32>* From = EdgeAgentBuckets.Find(FromEdge))
	{
		From->RemoveSingleSwap(AgentIndex);
	}
	EdgeAgentBuckets.FindOrAdd(Agent.EdgeIndex).Add(AgentIndex);
}

void ACitixTrafficSystem::ClampToLeader(int32 AgentIndex)
{
	if (!Agents.IsValidIndex(AgentIndex))
	{
		return;
	}
	FCitixTrafficAgent& Agent = Agents[AgentIndex];
	const TArray<int32>* Bucket = EdgeAgentBuckets.Find(Agent.EdgeIndex);
	if (!Bucket)
	{
		return;
	}

	// Nearest car ahead in the same lane and direction. Along-travel distance is made
	// comparable for both directions by negating it when the car travels backwards.
	const float AlongSelf = Agent.bForward ? Agent.Distance : -Agent.Distance;
	float BestGap = TNumericLimits<float>::Max();
	float LeaderDistance = 0.f;
	float LeaderSpeed = 0.f;
	for (int32 OtherIndex : *Bucket)
	{
		if (OtherIndex == AgentIndex || !Agents.IsValidIndex(OtherIndex))
		{
			continue;
		}
		const FCitixTrafficAgent& Other = Agents[OtherIndex];
		if (!Other.bActive || Other.bForward != Agent.bForward || Other.Lane != Agent.Lane)
		{
			continue;
		}
		const float AlongOther = Other.bForward ? Other.Distance : -Other.Distance;
		const float Gap = AlongOther - AlongSelf;
		if (Gap <= 0.f || Gap >= BestGap)
		{
			continue;
		}
		BestGap = Gap;
		LeaderDistance = Other.Distance;
		LeaderSpeed = Other.Speed;
	}
	if (BestGap == TNumericLimits<float>::Max())
	{
		return;
	}

	// Whatever the speed model did, a car is never nearer than a full car length plus the
	// minimum bumper gap. This is the guarantee against visible clipping.
	const float CentreGap = VehicleHalfLength * 2.f + HardBumperGap
		+ FMath::Max(0.f, LeaderSpeed) * 0.25f;
	if (Agent.bForward)
	{
		const float Limit = LeaderDistance - CentreGap;
		if (Agent.Distance > Limit)
		{
			Agent.Distance = FMath::Max(0.f, Limit);
			Agent.Speed = FMath::Min(Agent.Speed, LeaderSpeed);
		}
	}
	else
	{
		const float Limit = LeaderDistance + CentreGap;
		if (Agent.Distance < Limit)
		{
			Agent.Distance = FMath::Min(Network.EdgeLength(Agent.EdgeIndex), Limit);
			Agent.Speed = FMath::Min(Agent.Speed, LeaderSpeed);
		}
	}
}

float ACitixTrafficSystem::NodeStopMargin(int32 Node) const{
	float HalfCorridor = 0.f;
	if (Network.NodeEdgeIndices.IsValidIndex(Node))
	{
		for (int32 EdgeIndex : Network.NodeEdgeIndices[Node])
		{
			if (Network.Edges.IsValidIndex(EdgeIndex))
			{
				HalfCorridor = FMath::Max(HalfCorridor, Network.Edges[EdgeIndex].CorridorWidth * 0.5f);
			}
		}
	}
	return FMath::Max(JunctionStopMargin, HalfCorridor + 350.f);
}

void ACitixTrafficSystem::ComputeLookAhead(int32 AgentIndex, FTrafficAhead& Out) const
{
	Out = FTrafficAhead();
	const FCitixTrafficAgent& Self = Agents[AgentIndex];
	if (!Network.IsValidEdge(Self.EdgeIndex))
	{
		return;
	}

	const float FirstLength = Network.EdgeLength(Self.EdgeIndex);
	const float FirstAlong = Self.bForward ? Self.Distance : (FirstLength - Self.Distance);
	/** Distance from the car to the end of the edge it is on: the next junction. */
	const float FirstRemainder = FMath::Max(0.f, FirstLength - FirstAlong);
	// Path distance is measured from the car. Seeding the base at -FirstAlong lets every
	// hop share one formula: path = PathBase + distance-along-that-edge.
	float PathBase = -FirstAlong;

	int32 EdgeIndex = Self.EdgeIndex;
	bool bForward = Self.bForward;

	for (int32 Hop = 0; Hop < 3 && Network.IsValidEdge(EdgeIndex); ++Hop)
	{
		const float EdgeLength = Network.EdgeLength(EdgeIndex);

		// The lane this car will be in on THIS hop's edge. On the first hop that is simply
		// its lane; beyond a junction it is its lane clamped to what the road there allows,
		// which is exactly what the car does when it crosses. Comparing against that (rather
		// than the current lane) is what stops a car from driving into one that is already
		// sitting in the lane it is about to occupy.
		const FCitixRoadSpec HopSpec = UCitixCitySettings::Get().GetRoadSpec(Network.Edges[EdgeIndex].RoadClass);
		const int32 HopHalfLanes = FMath::Max(1, HopSpec.NumLanes / 2);
		const int32 HopLane = FMath::Clamp(Self.Lane, 0, HopHalfLanes - 1);

		// ---- Nearest vehicle ahead in the same lane, across this and later edges ----
		if (const TArray<int32>* Bucket = EdgeAgentBuckets.Find(EdgeIndex))
		{
			for (int32 OtherIndex : *Bucket)
			{
				if (OtherIndex == AgentIndex || !Agents.IsValidIndex(OtherIndex))
				{
					continue;
				}
				const FCitixTrafficAgent& Other = Agents[OtherIndex];
				if (!Other.bActive || Other.bForward != bForward || Other.Lane != HopLane)
				{
					continue;
				}
				const float AlongOther = bForward ? Other.Distance : (EdgeLength - Other.Distance);
				const float Path = PathBase + AlongOther;
				if (Path <= 0.f)
				{
					continue;
				}
				const float Gap = Path - VehicleHalfLength * 2.f;
				if (Out.Gap < 0.f || Gap < Out.Gap)
				{
					Out.Gap = Gap;
					Out.LeadSpeed = Other.Speed;
					Out.LeaderIndex = OtherIndex;
					if (Hop == 0)
					{
						Out.SameEdgeLeaderDistance = Other.Distance;
						Out.SameEdgeLeadSpeed = Other.Speed;
					}
					else
					{
						Out.bLeaderBeyondJunction = true;
						// How far past the crossing that leader sits: a queue right beyond the
						// far side is what makes entering the junction a bad idea.
						Out.LeaderBeyondJunctionDistance = Path - FMath::Max(0.f, FirstRemainder);
					}
				}
			}
		}

		// ---- The corner at the end of this hop ----
		int32 NextEdge = INDEX_NONE;
		bool bNextForward = true;
		if (Hop == 0)
		{
			NextEdge = Self.NextEdgeIndex;
			bNextForward = Self.bNextForward;
		}
		else
		{
			const int32 EndNode = bForward ? Network.Edges[EdgeIndex].NodeB : Network.Edges[EdgeIndex].NodeA;
			NextEdge = StraightestContinuation(EdgeIndex, EndNode, bNextForward);
		}

		const float PathToJunction = PathBase + EdgeLength;
		if (Hop == 0)
		{
			Out.JunctionDistance = PathToJunction;
		}

		if (Network.IsValidEdge(NextEdge) && NextEdge != EdgeIndex)
		{
			const int32 EndNode = bForward ? Network.Edges[EdgeIndex].NodeB : Network.Edges[EdgeIndex].NodeA;
			const int32 NextOther = (Network.Edges[NextEdge].NodeA == EndNode)
				? Network.Edges[NextEdge].NodeB : Network.Edges[NextEdge].NodeA;
			const FVector2D HopDir = bForward ? Network.EdgeDirection(EdgeIndex) : -Network.EdgeDirection(EdgeIndex);
			const FVector2D OutDir = (Network.Nodes[NextOther].Position - Network.Nodes[EndNode].Position).GetSafeNormal();
			const float Turn = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector2D::DotProduct(HopDir, OutDir), -1.f, 1.f)));
			if (Hop == 0)
			{
				Out.TurnDegrees = Turn;
			}

			// Braking curve: be at the corner speed when the corner arrives, which makes
			// the car slow before the turn and pick the speed back up after it.
			const float Vturn = CornerSpeed(Self.CruiseTarget, Turn, NextEdge);
			const float Approach = FMath::Max(0.f, PathToJunction - TurnBrakeMargin);
			Out.CurveSpeed = FMath::Min(Out.CurveSpeed,
				FMath::Sqrt(Vturn * Vturn + 2.f * Deceleration * Approach));
		}

		// Advance to the next hop.
		PathBase += EdgeLength;
		if (!Network.IsValidEdge(NextEdge) || NextEdge == EdgeIndex)
		{
			break;
		}
		EdgeIndex = NextEdge;
		bForward = bNextForward;
	}

	// ---- Lights and keeping the junction clear (needs the leader search done) ----
	if (Out.JunctionDistance >= 0.f)
	{
		// Stop before the crossing, not on top of it: the junction pad is as wide as the
		// widest road joining it, so the margin has to be measured from that, not fixed.
		const int32 EndNode = Self.bForward
			? Network.Edges[Self.EdgeIndex].NodeB : Network.Edges[Self.EdgeIndex].NodeA;
		const float StopMargin = NodeStopMargin(EndNode);

		// Distance to the stop line. If the car is already past it - inside the junction, or
		// the next junction's stop line falls behind its current position on a short block -
		// then there is nothing to stop for HERE. Demanding a stop in that case is what made
		// cars brake to a halt in the middle of an intersection.
		const float ToStopLine = Out.JunctionDistance - StopMargin;
		if (ToStopLine > 0.f)
		{
			const float Allowance = FMath::Sqrt(2.f * Deceleration * ToStopLine);

			if (TrafficSettings.bEnableTrafficLights && !IsEdgeGreen(Self.EdgeIndex, ElapsedTime))
			{
				Out.JunctionSpeed = FMath::Min(Out.JunctionSpeed, Allowance);
			}

			// Do not enter a crossing that cannot be cleared: if the queue beyond it is
			// stopped or crawling close to the far side, wait at the stop line.
			const bool bExitBlocked = Out.bLeaderBeyondJunction
				&& Out.LeaderBeyondJunctionDistance >= 0.f
				&& Out.LeaderBeyondJunctionDistance < JunctionExitClearance;
			if (bExitBlocked && Out.LeadSpeed >= 0.f && Out.LeadSpeed < JunctionBlockSpeed)
			{
				Out.JunctionSpeed = FMath::Min(Out.JunctionSpeed, Allowance);
			}
		}
	}

	// ---- Speed-dependent following distance ----
	if (Out.Gap >= 0.f && Out.LeadSpeed >= 0.f)
	{
		const float MinGap = FollowDistance + Self.Speed * FollowTimeHeadway;
		const float Usable = Out.Gap - MinGap;
		if (Usable <= 0.f)
		{
			// Too close: shed speed so the gap opens back up, and never close further.
			Out.FollowSpeed = FMath::Min(Out.LeadSpeed * 0.85f, Self.Speed);
		}
		else
		{
			// Match the leader's speed, then allow only what the remaining room can absorb.
			const float Match = FMath::Sqrt(Out.LeadSpeed * Out.LeadSpeed + 2.f * Deceleration * Usable);
			const float Close = Out.LeadSpeed + Usable / FMath::Max(0.35f, FollowTimeHeadway);
			Out.FollowSpeed = FMath::Min(Match, Close);
		}
	}
}

void ACitixTrafficSystem::UpdateAgents(float DeltaSeconds)
{
	int32 BrakingForPlayer = 0;
	// ---- Pass 1: recycling -------------------------------------------------
	// Done before the edge buckets are built, because ReleaseAgent reshuffles the agent
	// array. Building the buckets first and releasing during the drive pass left bucket
	// entries pointing past the end of the array.
	// A car is only recycled out of sight of EVERY driver: one nearby driver keeps it.
	for (int32 Index = Agents.Num() - 1; Index >= 0; --Index)
	{
		const FCitixTrafficAgent& Agent = Agents[Index];
		if (!Agent.bActive || !Network.IsValidEdge(Agent.EdgeIndex))
		{
			ReleaseAgent(Index);
			continue;
		}

		const FVector2D AgentLocation = GetAgentLocation(Agent);
		// Recycle only when no driver can see it: beyond a hard radius from all of
		// them, or past a minimum distance from all AND proven occluded/off-screen.
		// Checks are staggered across frames so only a handful of traces happen per
		// frame, and the cone test rejects anything behind the drivers first.
		bool bRecycle = false;
		if (IsFarFromAllPlayers(AgentLocation, DespawnRadius * 1.7f))
		{
			bRecycle = true;
		}
		else if (IsFarFromAllPlayers(AgentLocation, RecycleMinDistance)
			&& Agent.Age > MinRecycleAge
			&& (Index % FMath::Max(1, VisibilityCheckStride)) == (VisibilityPhase % FMath::Max(1, VisibilityCheckStride)))
		{
			bRecycle = !IsVisibleToAny(AgentLocation, 80.f);
		}

		if (bRecycle)
		{
			ReleaseAgent(Index);
		}
	}

	// ---- Pass 2: bucket the survivors by edge ------------------------------
	// Looking up a lane ahead is then a bucket fetch instead of a scan of the whole
	// population for every car.
	EdgeAgentBuckets.Reset();
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FCitixTrafficAgent& Agent = Agents[Index];
		if (Agent.bActive && Network.IsValidEdge(Agent.EdgeIndex))
		{
			EdgeAgentBuckets.FindOrAdd(Agent.EdgeIndex).Add(Index);
		}
	}

	// ---- Pass 3: drive -----------------------------------------------------
	// Nothing in here releases an agent, so every index in the buckets stays valid.
	for (int32 Index = Agents.Num() - 1; Index >= 0; --Index)
	{
		FCitixTrafficAgent& Agent = Agents[Index];
		if (!Agent.bActive || !Network.IsValidEdge(Agent.EdgeIndex))
		{
			// Retired this frame (dead end); pass 1 collects it next frame.
			continue;
		}

		const FVector2D AgentLocation = GetAgentLocation(Agent);
		const FCitixRoadEdge& Edge = Network.Edges[Agent.EdgeIndex];

		// Free-flow speed for the road class, eased rather than applied at once so
		// crossing onto a different class is not a step change in the target speed.
		const float BaseSpeed = (Edge.RoadClass == ECitixRoadClass::Arterial ||
			Edge.RoadClass == ECitixRoadClass::Highway || Edge.RoadClass == ECitixRoadClass::Collector)
			? CruiseSpeed * TrafficSettings.ArterialSpeedMultiplier
			: CruiseSpeed;
		const float FreeSpeed = BaseSpeed * Agent.SpeedFactor;
		if (Agent.CruiseTarget <= 0.f)
		{
			Agent.CruiseTarget = FreeSpeed;
		}
		Agent.CruiseTarget = FMath::FInterpTo(Agent.CruiseTarget, FreeSpeed, DeltaSeconds, CruiseSmoothingSpeed);
		Agent.DesiredSpeed = Agent.CruiseTarget;

		// Keep a committed turn so braking, junction handling and steering all plan
		// against the same future instead of each guessing separately.
		if (!Network.IsValidEdge(Agent.NextEdgeIndex))
		{
			ChoosePlanForAgent(Agent);
		}

		FTrafficAhead Ahead;
		ComputeLookAhead(Index, Ahead);

		float TargetSpeed = Agent.CruiseTarget;
		TargetSpeed = FMath::Min(TargetSpeed, Ahead.CurveSpeed);
		TargetSpeed = FMath::Min(TargetSpeed, Ahead.JunctionSpeed);
		TargetSpeed = FMath::Min(TargetSpeed, Ahead.FollowSpeed);

		// Exit-of-corner hold: keep the low corner speed for as long as the rotation takes, so
		// the car cannot accelerate away while the body is still swinging round. It is held
		// flat and then released: the speed itself is rate-limited, so there is no jump when
		// the hold ends.
		if (Agent.TurnHoldTimer > 0.f)
		{
			Agent.TurnHoldTimer = FMath::Max(0.f, Agent.TurnHoldTimer - DeltaSeconds);
			TargetSpeed = FMath::Min(TargetSpeed, Agent.TurnHoldSpeed);
		}

		// Startled (pulse tool): pull over and wait out the timer, then drive on.
		if (Agent.StopTimer > 0.f)
		{
			Agent.StopTimer = FMath::Max(0.f, Agent.StopTimer - DeltaSeconds);
			TargetSpeed = 0.f;
		}

		// Player avoidance: brake for the nearest driver's car or character when
		// they are in this agent's path. Costs one relative-position test per
		// agent, which is far cheaper than adding them as simulated traffic.
		if (bAvoidPlayer)
		{
			float NearestDist = 0.f;
			const FVector2D NearestXY = NearestPlayerTo(AgentLocation, NearestDist);
			const FVector2D Travel = GetAgentDirection(Agent);
			const FVector2D Right(-Travel.Y, Travel.X);
			const FVector2D ToPlayer = NearestXY - AgentLocation;
			const float AheadDistance = FVector2D::DotProduct(ToPlayer, Travel);
			const float Side = FMath::Abs(FVector2D::DotProduct(ToPlayer, Right));
			if (AheadDistance > 0.f && AheadDistance < PlayerAvoidDistance && Side < PlayerAvoidHalfWidth)
			{
				const float Usable = FMath::Max(0.f, AheadDistance - PlayerAvoidMargin);
				TargetSpeed = FMath::Min(TargetSpeed, FMath::Sqrt(2.f * Deceleration * Usable));
				++BrakingForPlayer;
				++PlayerAvoidEvents;
			}
		}

		const float SpeedBefore = Agent.Speed;
		const float Rate = (TargetSpeed > Agent.Speed) ? Acceleration : Deceleration;
		Agent.Speed = FMath::FInterpConstantTo(Agent.Speed, TargetSpeed, DeltaSeconds, Rate);
		Agent.Distance += Agent.Speed * DeltaSeconds;
		Agent.Age += DeltaSeconds;

		// Hard anti-overlap clamp is applied after the advance below, so it also covers the
		// edge the car has just crossed onto (a per-edge distance clamp cannot see across a
		// node, which is how two cars used to end up sharing the same few metres of road).

		// Advance across nodes if we ran past the end of this edge.
		int32 SafetyCounter = 0;
		while (Agent.Distance >= Network.EdgeLength(Agent.EdgeIndex) && SafetyCounter++ < 4)
		{
			const FCitixRoadEdge& CurrentEdge = Network.Edges[Agent.EdgeIndex];
			const float CurrentLength = Network.EdgeLength(Agent.EdgeIndex);
			const float Overflow = Agent.Distance - CurrentLength;
			const int32 EndNode = Agent.bForward ? CurrentEdge.NodeB : CurrentEdge.NodeA;

			// Take the corner the car already committed to. This is the turn its braking,
			// its corner-speed hold and its steering were all planned against; drawing a
			// fresh random edge here (as this used to) meant a car would brake as if it were
			// going straight and then swing through a hard turn at full speed.
			int32 NextEdge = Agent.NextEdgeIndex;
			bool bNextForward = true;
			const bool bPlanConnects = Network.IsValidEdge(NextEdge)
				&& (Network.Edges[NextEdge].NodeA == EndNode || Network.Edges[NextEdge].NodeB == EndNode);
			if (!bPlanConnects)
			{
				// No usable commitment (first step, or the graph changed under it): pick one.
				NextEdge = ChooseNextEdge(Agent.EdgeIndex, EndNode, bNextForward);
			}
			else
			{
				// Derive the direction from the node so it can never disagree with the plan.
				bNextForward = (Network.Edges[NextEdge].NodeA == EndNode);
			}
			if (NextEdge == INDEX_NONE || Network.EdgeLength(NextEdge) < 1.f)
			{
				Agent.bActive = false;
				break;
			}

			// Do not cross if there is already a car sitting just beyond the node in the lane
			// this car will occupy. Two cars crossing into the same short piece of road in the
			// same frame otherwise end up on top of each other, because the distance clamp
			// works per edge and cannot see across the node.
			if (IsEdgeEntryBlocked(NextEdge, bNextForward, Agent.Lane))
			{
				Agent.Distance = FMath::Min(Agent.Distance, Network.EdgeLength(Agent.EdgeIndex) - 1.f);
				break;
			}

			const FVector2D InDir = GetAgentDirection(Agent);
			const FVector2D OutDir = bNextForward
				? Network.EdgeDirection(NextEdge)
				: -Network.EdgeDirection(NextEdge);
			const float Turn = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector2D::DotProduct(InDir, OutDir), -1.f, 1.f)));

			// Remember where the car came from: the presented path blends off this line.
			const int32 EdgeBeforeCrossing = Agent.EdgeIndex;
			Agent.BlendFromEdge = EdgeBeforeCrossing;
			Agent.bBlendFromForward = Agent.bForward;
			Agent.BlendFromOffset = Agent.CurrentOffset;
			Agent.EdgeIndex = NextEdge;
			Agent.bForward = bNextForward;
			Agent.Distance = FMath::Max(0.f, Overflow);
			// Spread the corner over enough distance that this car can follow it: at least
			// angle * JunctionBlendAngleScale (so a sharp corner is never crammed into a few
			// metres, however slowly the car crossed), and more when it is moving fast. Never
			// longer than the edge it turns onto, or it would still be mid-corner next time.
			Agent.BlendLength = FMath::Clamp(
				FMath::Min(
					FMath::Max(Turn * JunctionBlendAngleScale,
						Turn * Agent.Speed * JunctionBlendYawFactor / FMath::Max(1.f, TurnRateDegrees)),
					Network.EdgeLength(NextEdge) * 0.8f),
				JunctionBlendMinDistance, 4000.f);

			// Keep the lane instead of drawing a new one at every junction. Jumping lane
			// mid-block made cars weave for no reason and broke the following model
			// across edges (cars could no longer see who was in front of them).
			const FCitixRoadSpec Spec = UCitixCitySettings::Get().GetRoadSpec(Network.Edges[NextEdge].RoadClass);
			const int32 HalfLanes = FMath::Max(1, Spec.NumLanes / 2);
			Agent.Lane = FMath::Clamp(Agent.Lane, 0, HalfLanes - 1);
			Agent.LaneOffset = Spec.LaneWidth * (0.5f + static_cast<float>(Agent.Lane));
			// Until it is this far past the node, the car is still inside the crossing and
			// must not be given a reason to stop there.
			Agent.JunctionClearDistance = NodeStopMargin(EndNode);

			// The car has moved edges: move it in the bucket too, so a car processed later in
			// this same pass sees it on the edge it is actually on.
			MoveAgentBucket(Index, EdgeBeforeCrossing);
			// Remember the corner just taken so the car holds its corner speed while the
			if (Turn > 18.f)
			{
				Agent.TurnHoldSpeed = CornerSpeed(Agent.CruiseTarget, Turn, Agent.EdgeIndex);
				Agent.TurnHoldTimer = FMath::Clamp(Turn / FMath::Max(20.f, TurnRateDegrees), 0.25f, 1.6f);
			}
			ChoosePlanForAgent(Agent);
		}

		if (!Agent.bActive)
		{
			// Dead end with nowhere to go: retire it next frame (pass 1), not here, so
			// the bucket indices stay valid for the agents still to be driven.
			continue;
		}

		// The absolute guarantee against visible overlap, applied now that the car is on its
		// final edge for this frame.
		ClampToLeader(Index);

		Agent.DiagTargetSpeed = TargetSpeed;
		Agent.DiagGap = Ahead.Gap;
		Agent.DiagLeadSpeed = Ahead.LeadSpeed;
		Agent.DiagTurnDegrees = Ahead.TurnDegrees;
		Agent.DiagJunctionDistance = Ahead.JunctionDistance;
		Agent.bDiagBraking = Agent.Speed < SpeedBefore - 1.f;

		// ---- Presentation -----------------------------------------------------
		// The simulation position is the lane line. What is drawn is that line plus a
		// lateral offset that is eased in WORLD SPACE, so when the car crosses onto an edge
		// whose lane points a different way the offset rotates smoothly and the car follows
		// a curve. (Easing the scalar offset per edge instead made the position jump
		// sideways at every junction.)
		if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
		{
			// Lateral target is simply this edge's lane, except on the approach to a junction
			// the car is about to leave: there it moves into the lane it will hold on the far
			// side, so it is exactly on its lane as it crosses and stays on it. Doing that
			// after the crossing instead would leave it drifting across the road inside the
			// junction, which also looks like driving off-lane.
			FVector2D TargetOffset = EdgeRight(Agent.EdgeIndex, Agent.bForward) * Agent.LaneOffset;
			if (Network.IsValidEdge(Agent.NextEdgeIndex) && Agent.NextEdgeIndex != Agent.EdgeIndex)
			{
				const FCitixRoadSpec NextSpec = UCitixCitySettings::Get().GetRoadSpec(
					Network.Edges[Agent.NextEdgeIndex].RoadClass);
				const int32 NextLane = FMath::Clamp(Agent.Lane, 0,
					FMath::Max(0, NextSpec.NumLanes / 2 - 1));
				const float NextLaneOffset = NextSpec.LaneWidth * (0.5f + static_cast<float>(NextLane));

				// Correct the lane on THIS edge's lateral axis: "get into the right lane before
				// the corner", not a diagonal move toward where the next edge's lane happens to
				// be. Using the next edge's direction here made turning cars drift 12 m across
				// the road before the junction and arrive at the corner sideways.
				const FVector2D Lined = EdgeRight(Agent.EdgeIndex, Agent.bForward) * NextLaneOffset;
				const float Drift = (Lined - Agent.CurrentOffset).Size();
				const float DriftLength = FMath::Max(LaneChangeDistance, Drift * 5.f);
				const float ToEnd = FMath::Max(0.f, Network.EdgeLength(Agent.EdgeIndex) - Agent.Distance);
				if (Drift > 1.f && ToEnd < DriftLength)
				{
					TargetOffset = Lined;
				}
			}

			const float PrevYaw = Agent.CurrentYaw;
			const FVector2D PrevSmoothed(Agent.SmoothedLocation.X, Agent.SmoothedLocation.Y);
			if (!Agent.bPresentationStarted)
			{
				Agent.CurrentOffset = TargetOffset;
				Agent.bPresentationStarted = true;
			}
			else
			{
				// Eased per unit DISTANCE travelled, so the arc shape is the same at any speed
				// (a time-based ease makes a slow car swerve). Bounded by two things: how far
				// the offset has to move, and how fast it would have to ROTATE - a 90-degree
				// change of lane direction rotates the path too, and has to respect the same
				// yaw budget as the junction blend or the body cannot keep up with it.
				const float OffsetDeltaSize = (TargetOffset - Agent.CurrentOffset).Size();
				float DemandLength = FMath::Max(LaneChangeDistance, OffsetDeltaSize * 5.f);

				const float CurrentSize = Agent.CurrentOffset.Size();
				const float TargetSize = TargetOffset.Size();
				if (CurrentSize > 1.f && TargetSize > 1.f)
				{
					const float Cos = FMath::Clamp(FVector2D::DotProduct(Agent.CurrentOffset, TargetOffset)
						/ (CurrentSize * TargetSize), -1.f, 1.f);
					const float OffsetAngle = FMath::RadiansToDegrees(FMath::Acos(Cos));
					DemandLength = FMath::Max(DemandLength,
						OffsetAngle * Agent.Speed * JunctionBlendYawFactor / FMath::Max(1.f, TurnRateDegrees));
				}

				const float Step = (Agent.Speed * DeltaSeconds) / FMath::Max(50.f, DemandLength);
				Agent.CurrentOffset = FMath::Lerp(Agent.CurrentOffset, TargetOffset,
					FMath::Clamp(Step, 0.f, 1.f));
			}

			// Presented path: this edge's lane, blended with the extrapolated line of the edge
			// the car just left. A junction is then a curve rather than a kink, so the body
			// never has to crab to keep up. For a straight-through the two lines coincide and
			// this is exactly a straight line.
			FVector2D PresentedTarget = GetAgentLocationWithOffset(Agent, Agent.CurrentOffset);
			if (Network.IsValidEdge(Agent.BlendFromEdge) && Agent.BlendFromEdge != Agent.EdgeIndex)
			{
				const float Alpha = FMath::Clamp(
					Agent.Distance / FMath::Max(100.f, Agent.BlendLength), 0.f, 1.f);
				if (Alpha < 1.f)
				{
					// Where the car would be if it had carried straight on past the node.
					const float FromAlong = Network.EdgeLength(Agent.BlendFromEdge) + Agent.Distance;
					const FVector2D From = EdgePoint(Agent.BlendFromEdge, Agent.bBlendFromForward,
						FromAlong, Agent.BlendFromOffset);
					PresentedTarget = FMath::Lerp(From, PresentedTarget, FMath::SmoothStep(0.f, 1.f, Alpha));
				}
			}

			const FVector TargetLocation(PresentedTarget.X, PresentedTarget.Y, 0.f);
			Agent.SmoothedLocation = Agent.bPresentationStarted
				? FMath::VInterpTo(Agent.SmoothedLocation, TargetLocation, DeltaSeconds, PositionInterpSpeed)
				: TargetLocation;

			// Heading follows the presented motion. This is the whole trick: a car on a
			// straight path can only ever have a straight heading, so "goes straight but
			// spins" is impossible; through a curve the heading rotates exactly as fast as
			// the path turns.
			FVector2D MoveDir = FVector2D(Agent.SmoothedLocation.X, Agent.SmoothedLocation.Y) - PrevSmoothed;
			const float MoveDistance = MoveDir.Size();
			if (MoveDistance > 0.5f)
			{
				MoveDir /= MoveDistance;
				Agent.DiagMoveYaw = FMath::RadiansToDegrees(FMath::Atan2(MoveDir.Y, MoveDir.X));
			}
			else
			{
				// Stopped: there is no motion direction, so report none rather than a crab
				// that is not there. (The body's own rotation is also scaled to zero below.)
				Agent.DiagMoveYaw = Agent.CurrentYaw;
			}

			// Yaw rate is scaled by speed, so a car only turns while it is moving. Without
			// this a car that stopped at a junction would pivot on the spot to face its new
			// road, which no real car does.
			const float SteerScale = FMath::Clamp(Agent.Speed / FMath::Max(1.f, MinSteerSpeed), 0.f, 1.f);
			const float DeltaYaw = FMath::FindDeltaAngleDegrees(Agent.CurrentYaw, Agent.DiagMoveYaw);
			const float MaxStep = TurnRateDegrees * DeltaSeconds * SteerScale;
			Agent.CurrentYaw = FRotator::NormalizeAxis(Agent.CurrentYaw
				+ FMath::Clamp(DeltaYaw, -MaxStep, MaxStep));
			Agent.DiagYaw = Agent.CurrentYaw;
			Agent.DiagHeadingError = FMath::FindDeltaAngleDegrees(Agent.DiagMoveYaw, Agent.CurrentYaw);
			Agent.DiagYawRate = (DeltaSeconds > KINDA_SMALL_NUMBER)
				? FMath::FindDeltaAngleDegrees(PrevYaw, Agent.CurrentYaw) / DeltaSeconds
				: 0.f;

			ACitixTrafficVehicle* Vehicle = Pool[Agent.PoolIndex];
			Vehicle->SetVehicleVisible(true);
			const FVector PreviousVehicleLocation = Vehicle->GetActorLocation();
			Vehicle->SetActorLocationAndRotation(
				Agent.SmoothedLocation,
				FRotator(0.f, Agent.CurrentYaw, 0.f),
				false, nullptr, ETeleportType::None);
			if (ACitixChaseGameMode* Chase = GetWorld()->GetAuthGameMode<ACitixChaseGameMode>())
				Chase->TryRunOver(Vehicle, PreviousVehicleLocation, Agent.SmoothedLocation, Agent.Speed*.036f);
   Vehicle->PublishMotion(Agent.Speed*.036f);

			// Level of detail: drop wheels far from every driver. Hysteresis (show
			// close, keep until further) stops them flickering on and off.
			float NearestDriver = 0.f;
			NearestPlayerTo(PresentedTarget, NearestDriver);
			const float WheelDistanceSquared = NearestDriver * NearestDriver;
			const bool bWheelsNow = Agent.bWheelsVisible
				? (WheelDistanceSquared < FMath::Square(WheelLodDistance * 1.5f))
				: (WheelDistanceSquared < FMath::Square(WheelLodDistance));
			if (bWheelsNow != Agent.bWheelsVisible)
			{
				Agent.bWheelsVisible = bWheelsNow;
				Vehicle->SetWheelsVisible(bWheelsNow);
			}

			// Optional debugging: the car's heading, the direction its path is actually
			// taking, and who it is following. If the yellow and blue lines diverge the car
			// is crabbing, which is the thing to avoid.
			float NearestDebug = 0.f;
			NearestPlayerTo(AgentLocation, NearestDebug);
			const float DebugDistanceSquared = NearestDebug * NearestDebug;
			if (bDrawDebug && DebugDistanceSquared < FMath::Square(15000.f))
			{
				const FVector Car(Agent.SmoothedLocation.X, Agent.SmoothedLocation.Y, 60.f);
				const float YawRad = FMath::DegreesToRadians(Agent.CurrentYaw);
				const FVector Heading = Car + FVector(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f) * 600.f;
				const float MoveRad = FMath::DegreesToRadians(Agent.DiagMoveYaw);
				const FVector Motion = Car + FVector(FMath::Cos(MoveRad), FMath::Sin(MoveRad), 0.f) * 450.f;
				DrawDebugLine(GetWorld(), Car, Heading, FColor::Yellow, false, -1.f, 0, 5.f);
				DrawDebugLine(GetWorld(), Car, Motion, FColor::Cyan, false, -1.f, 0, 3.f);
				if (Agents.IsValidIndex(Ahead.LeaderIndex))
				{
					const FVector2D Lead = GetAgentLocation(Agents[Ahead.LeaderIndex]);
					DrawDebugLine(GetWorld(), Car, FVector(Lead.X, Lead.Y, 60.f),
						Agent.bDiagBraking ? FColor::Red : FColor::Green, false, -1.f, 0, 2.f);
				}
			}
		}
	}

	LastPlayerAvoidCount = BrakingForPlayer;
}

// ---------------------------------------------------------------------------
// Debugging
// ---------------------------------------------------------------------------

static ACitixTrafficSystem* FindTrafficSystem(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACitixTrafficSystem> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorldAndArgs GCitixTrafficCmd(
	TEXT("Citix.Traffic"),
	TEXT("Traffic debugging. 'Citix.Traffic' dumps per-car target speed, gap, leader speed, "
		"junction state and the crab angle (plus spawn rejection counts). "
		"'Citix.Traffic watch 1' logs one car's yaw/motion/crab over time. "
		"'Citix.Traffic debug 1' draws each nearby car's heading and following link."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ACitixTrafficSystem* Traffic = FindTrafficSystem(World);
		if (!Traffic)
		{
			UE_LOG(LogCitix, Warning, TEXT("Citix.Traffic: no ACitixTrafficSystem in the world."));
			return;
		}

		if (Args.Num() > 0 && Args[0].Equals(TEXT("debug"), ESearchCase::IgnoreCase))
		{
			const bool bOn = Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0;
			Traffic->SetDrawDebug(bOn);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic debug draw %s."), bOn ? TEXT("on") : TEXT("off"));
			return;
		}

		if (Args.Num() > 0 && Args[0].Equals(TEXT("watch"), ESearchCase::IgnoreCase))
		{
			const bool bOn = Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0;
			Traffic->SetWatchTraffic(bOn);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Traffic watch %s."), bOn ? TEXT("on") : TEXT("off"));
			return;
		}

		FVector PlayerLocation = FVector::ZeroVector;
		if (World)
		{
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				if (const APawn* Pawn = PC->GetPawn())
				{
					PlayerLocation = Pawn->GetActorLocation();
				}
			}
		}
		Traffic->LogDiagnostics(FVector2D(PlayerLocation.X, PlayerLocation.Y));
	}));
