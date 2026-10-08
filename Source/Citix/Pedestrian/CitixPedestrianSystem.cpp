// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pedestrian/CitixPedestrianSystem.h"

#include "Character/CitixCharacterLibrary.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Pedestrian/CitixPedestrian.h"
#include "Citix.h"

namespace
{
	constexpr float StrideLength = 145.f;
	constexpr float WalkSwingDegrees = 25.f;
	constexpr float WalkBobCm = 1.9f;
	constexpr float SpawnsPerSecond = 14.f;
}

ACitixPedestrianSystem::ACitixPedestrianSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
}

void ACitixPedestrianSystem::Initialize(const FCitixRoadNetwork& InNetwork, const UCitixCitySettings& InSettings)
{
	Network = InNetwork;
	SidewalkHeight = InSettings.SidewalkHeight;

	const FCitixPedestrianSettings& PedestrianSettings = InSettings.Pedestrians;
	MaxPedestrians = FMath::Max(0, PedestrianSettings.MaxPedestrians);
	SpawnRadius = FMath::Max(1000.f, PedestrianSettings.SpawnRadius);
	DespawnRadius = FMath::Max(SpawnRadius + 1000.f, PedestrianSettings.DespawnRadius);
	VisibleDistance = FMath::Min(SpawnRadius, DespawnRadius * 0.55f);

	const float BaseSpeed = FMath::Max(10.f, PedestrianSettings.WalkSpeed);
	const float Variation = FMath::Max(0.f, PedestrianSettings.SpeedVariation);
	WalkSpeedMin = BaseSpeed * (1.f - Variation);
	WalkSpeedMax = BaseSpeed * (1.f + Variation);

	Seed = InSettings.Seed + 4241;
	Rng.Initialize(Seed);
	SpawnAccumulator = 0.f;
	bInitialized = PedestrianSettings.bEnabled && Network.Edges.Num() > 0;

	UE_LOG(LogCitix, Log, TEXT("[Citix] Pedestrians initialised: max %d, spawn radius %.0f cm."),
		MaxPedestrians, SpawnRadius);
}

void ACitixPedestrianSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPedestrians();
	Super::EndPlay(EndPlayReason);
}

namespace
{
	/** 2D distance from a point to a line segment (the car's swept path). */
	float PointSegmentDistance(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Span = B - A;
		const float SpanSquared = Span.SizeSquared();
		if (SpanSquared < 1.f)
		{
			return FVector2D::Distance(Point, A);
		}
		const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, Span) / SpanSquared, 0.f, 1.f);
		return FVector2D::Distance(Point, A + Span * T);
	}
}

int32 ACitixPedestrianSystem::FindAgentByPoolIndex(int32 PoolIndex) const
{
	if (PoolIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		if (Agents[Index].bActive && Agents[Index].PoolIndex == PoolIndex)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void ACitixPedestrianSystem::ApplyHit(int32 AgentIndex, const FVector& Impulse,
	const FVector& Center, float ImpactSpeed, float FlatDamage, float CooldownOverride)
{
	if (!Agents.IsValidIndex(AgentIndex))
	{
		return;
	}
	FCitixPedestrianAgent& Agent = Agents[AgentIndex];
	if (!Agent.bActive || Agent.bRagdoll || Agent.HitCooldown > 0.f)
	{
		return;
	}
	if (!Pool.IsValidIndex(Agent.PoolIndex) || !Pool[Agent.PoolIndex])
	{
		return;
	}

	// Damage scales with impact speed, so a gentle nudge does nothing while a fast impact is
	// fatal. Below the threshold there is no damage at all. Gunfire passes flat damage
	// instead (hits-to-kill per gun) with its own short cooldown.
	Agent.Health -= (FlatDamage >= 0.f) ? FlatDamage
		: FMath::Max(0.f, (ImpactSpeed - MinDamageSpeed) * DamagePerSpeed);
	Agent.HitCooldown = (CooldownOverride >= 0.f) ? CooldownOverride : HitCooldownTime;

	// Everyone who is hit goes down as a physics ragdoll. Whether they get back up is decided
	// by their remaining health.
	const ECitixRagdollKind Kind = (Agent.Health > 0.f)
		? ECitixRagdollKind::Knockdown : ECitixRagdollKind::Death;
	if (Kind == ECitixRagdollKind::Death)
	{
		// Low-poly fade-out, no gore: counted so gun kills escalate to the police.
		++DeathCount;
	}

	if (ActiveRagdollCount >= MaxConcurrentRagdolls)
	{
		// Prefer making someone stand up over deleting them: a knock-down survivor vanishing
		// would be far more noticeable than one getting up early.
		if (!FastForwardOldestKnockdown())
		{
			ReleaseOldestRagdoll();
		}
	}

	if (Kind == ECitixRagdollKind::Knockdown)
	{
		// Face the way they were walking when they get up.
		const FVector2D Direction = GetAgentDirection(Agent);
		Pool[Agent.PoolIndex]->SetRecoveryFacing(
			FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)), SidewalkHeight);
	}

	// Make sure the victim is actually on screen for the whole sequence: it may have been
	// culled as "distant and unseen" a moment before the hit.
	Pool[Agent.PoolIndex]->SetActiveVisual(true);
	Pool[Agent.PoolIndex]->Ragdoll(Impulse, Center, Kind);
	Agent.bRagdoll = true;
	++ActiveRagdollCount;
}

void ACitixPedestrianSystem::NotifyPedestrianContact(ACitixPedestrian* Pedestrian,
	const FVector& OtherLocation, const FVector& OtherVelocity)
{
	// Resolve by actor, since the caller knows the body it hit rather than its slot.
	int32 Found = INDEX_NONE;
	for (int32 Candidate = 0; Candidate < Agents.Num(); ++Candidate)
	{
		if (Agents[Candidate].bActive && Pool.IsValidIndex(Agents[Candidate].PoolIndex)
			&& Pool[Agents[Candidate].PoolIndex] == Pedestrian)
		{
			Found = Candidate;
			break;
		}
	}
	if (Found == INDEX_NONE)
	{
		return;
	}

	// Same knock-down rules as being run into: impulse along the player's motion, damage from
	// their speed. Contact with a person never damages the car.
	const float Speed = OtherVelocity.Size();
	FVector Impulse = OtherVelocity;
	Impulse.Z = 0.f;
	Impulse = Impulse.GetSafeNormal() * FMath::Clamp(Speed * 0.9f, 300.f, 2600.f);
	Impulse.Z = FMath::FRandRange(150.f, 420.f);

	UE_LOG(LogCitix, Log, TEXT("[Citix] Hitbox contact: a pedestrian was hit at %.0f km/h."),
		Speed * 0.036f);
	ApplyHit(Found, Impulse, OtherLocation, Speed);
}

int32 ACitixPedestrianSystem::HitPedestriansInRadius(const FVector& Center, float Radius,
	const FVector& Impulse, float ImpactSpeed)
{
	const float RadiusSquared = Radius * Radius;
	const FVector2D CenterXY(Center.X, Center.Y);
	int32 Hit = 0;

	// Collect the victims first, then apply. Releasing a ragdoll under budget pressure
	// reshuffles the agent array, and doing that mid-iteration left a stale reference on
	// whichever agent had been swapped into the slot. Keying on the pool slot instead of
	// the array index keeps every victim addressing the right actor.
	TArray<int32, TInlineAllocator<16>> Victims;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bActive || Agent.bRagdoll || Agent.HitCooldown > 0.f)
		{
			continue;
		}
		const FVector2D Location = GetAgentLocation(Agent);
		if (FVector2D::DistSquared(Location, CenterXY) > RadiusSquared)
		{
			continue;
		}
		if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
		{
			Victims.Add(Agent.PoolIndex);
		}
	}

	for (int32 PoolIndex : Victims)
	{
		const int32 Found = FindAgentByPoolIndex(PoolIndex);
		if (Found != INDEX_NONE)
		{
			ApplyHit(Found, Impulse, Center, ImpactSpeed);
			++Hit;
		}
	}
	return Hit;
}

int32 ACitixPedestrianSystem::FleeFrom(const FVector& Center, float Radius)
{
	const float RadiusSquared = Radius * Radius;
	const FVector2D CenterXY(Center.X, Center.Y);
	int32 Fled = 0;

	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bActive || Agent.bRagdoll)
		{
			continue;
		}
		const FVector2D Location = GetAgentLocation(Agent);
		if (FVector2D::DistSquared(Location, CenterXY) > RadiusSquared)
		{
			continue;
		}
		Agent.FleeTimer = FleeDuration;
		Agent.WaitTimer = 0.f;
		++Fled;
	}
	return Fled;
}

int32 ACitixPedestrianSystem::GunHit(const FVector& From, const FVector& To,
	float Radius, float Damage, int32& OutKills)
{
	OutKills = 0;
	const FVector2D A(From.X, From.Y);
	const FVector2D B(To.X, To.Y);
	const FVector2D AB = B - A;
	const float LengthSq = static_cast<float>(AB.SizeSquared());

	// One bullet, one victim: the nearest walker to the shot segment.
	int32 Best = INDEX_NONE;
	float BestDistanceSq = Radius * Radius;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bActive || Agent.bRagdoll)
		{
			continue;
		}
		const FVector2D Location = GetAgentLocation(Agent);
		const float T = (LengthSq > 1.f)
			? FMath::Clamp(FVector2D::DotProduct(Location - A, AB) / LengthSq, 0.f, 1.f)
			: 0.f;
		const float DistanceSq = static_cast<float>(FVector2D::DistSquared(Location, A + AB * T));
		// Height gate: shots over heads or into the ground do not count.
		const float Z = FMath::Lerp(From.Z, To.Z, T);
		if (DistanceSq < BestDistanceSq && FMath::Abs(Z - 96.f) < 260.f)
		{
			BestDistanceSq = DistanceSq;
			Best = Index;
		}
	}
	if (Best == INDEX_NONE)
	{
		return 0;
	}

	FCitixPedestrianAgent& Victim = Agents[Best];
	Victim.Health -= Damage;
	Victim.HitCooldown = 0.f;
	// No ragdoll for gunfire, ever: a non-lethal hit sends them running, a
	// lethal one collapses them in place (stand-and-fade, no physics).
	const bool bLethal = Victim.Health <= 0.f;
	if (bLethal)
	{
		Victim.CollapseTimer = CollapseDuration;
		Victim.WaitTimer = 0.f;
		Victim.FleeTimer = 0.f;
		++DeathCount;
		OutKills = 1;
	}
	else
	{
		Victim.FleeTimer = FleeDuration;
		Victim.WaitTimer = 0.f;
		OutKills = 0;
	}
	return 1;
}

bool ACitixPedestrianSystem::GetNearestWalker(const FVector& Center, float MaxDistance,
	FVector& OutLocation) const
{
	const FVector2D CenterXY(Center.X, Center.Y);
	bool bFound = false;
	float BestDistanceSq = MaxDistance * MaxDistance;
	for (const FCitixPedestrianAgent& Agent : Agents)
	{
		if (!Agent.bActive || Agent.bRagdoll)
		{
			continue;
		}
		const FVector2D Location = GetAgentLocation(Agent);
		const float DistanceSq = static_cast<float>(FVector2D::DistSquared(Location, CenterXY));
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			OutLocation = FVector(Location.X, Location.Y, 96.f);
			bFound = true;
		}
	}
	return bFound;
}

void ACitixPedestrianSystem::StaggerPedestrian(ACitixPedestrian* Pedestrian, const FVector& FromLocation)
{
	if (!Pedestrian)
	{
		return;
	}
	for (int32 Index = 0; Index < Pool.Num(); ++Index)
	{
		if (Pool[Index] != Pedestrian)
		{
			continue;
		}
		const int32 Found = FindAgentByPoolIndex(Index);
		if (Found == INDEX_NONE)
		{
			return;
		}
		FCitixPedestrianAgent& Agent = Agents[Found];
		if (!Agent.bActive || Agent.bRagdoll || Agent.CollapseTimer > 0.f)
		{
			return;
		}
		// Shoved by a person, not a car: stumble in place and hurry on.
		// Never damage, never ragdoll - those belong to vehicles.
		Agent.WaitTimer = 0.6f;
		Agent.FleeTimer = FMath::Max(Agent.FleeTimer, 2.5f);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Pedestrian shoved by the player on foot."));
		return;
	}
}

int32 ACitixPedestrianSystem::HitPedestriansAlongSweep(const FVector& From, const FVector& To,
	float Radius, const FVector& Impulse, float ImpactSpeed)
{
	const FVector2D A(From.X, From.Y);
	const FVector2D B(To.X, To.Y);
	const float MotionSize = (B - A).Size();

	int32 Hit = 0;
	TArray<int32, TInlineAllocator<16>> Victims;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bActive || Agent.bRagdoll || Agent.HitCooldown > 0.f)
		{
			continue;
		}
		if (!Pool.IsValidIndex(Agent.PoolIndex) || !Pool[Agent.PoolIndex])
		{
			continue;
		}

		// Test the swept path against the person's actual hitbox, widened by the car's own
		// half width. That is a real volume test, not a bubble around the car.
		const FVector2D Centre = GetAgentLocation(Agent);
		const float Reach = Radius + Pool[Agent.PoolIndex]->GetHitboxRadius();
		const bool bTouches = (MotionSize > 1.f)
			? (PointSegmentDistance(Centre, A, B) <= Reach)
			: (FVector2D::DistSquared(Centre, B) <= Reach * Reach);
		if (bTouches)
		{
			Victims.Add(Agent.PoolIndex);
		}
	}

	for (int32 PoolIndex : Victims)
	{
		const int32 Found = FindAgentByPoolIndex(PoolIndex);
		if (Found != INDEX_NONE)
		{
			// The impact point is where the car is now; the impulse is what shapes the fall.
			ApplyHit(Found, Impulse, To, ImpactSpeed);
			++Hit;
		}
	}
	return Hit;
}

bool ACitixPedestrianSystem::FastForwardOldestKnockdown()
{
	int32 Oldest = INDEX_NONE;
	float OldestAge = -1.f;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bRagdoll || !Pool.IsValidIndex(Agent.PoolIndex) || !Pool[Agent.PoolIndex])
		{
			continue;
		}
		if (!Pool[Agent.PoolIndex]->IsKnockedDown())
		{
			continue;
		}
		const float Age = Pool[Agent.PoolIndex]->GetRagdollAge();
		if (Age > OldestAge)
		{
			OldestAge = Age;
			Oldest = Index;
		}
	}
	if (Oldest == INDEX_NONE)
	{
		return false;
	}
	Pool[Agents[Oldest].PoolIndex]->FastForwardRecovery();
	return true;
}

void ACitixPedestrianSystem::ReleaseOldestRagdoll()
{
	// Only ever remove a body the player cannot see. A ragdoll disappearing in view is
	// exactly the pop this system exists to avoid, so if every ragdoll is visible this
	// simply does nothing and the caller exceeds the physics budget for a moment instead.
	int32 Oldest = INDEX_NONE;
	float OldestAge = -1.f;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		const FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bRagdoll || !Pool.IsValidIndex(Agent.PoolIndex) || !Pool[Agent.PoolIndex])
		{
			continue;
		}
		if (IsPointVisible(GetAgentLocation(Agent), SidewalkHeight + 80.f))
		{
			continue;
		}
		const float Age = Pool[Agent.PoolIndex]->GetRagdollAge();
		if (Age > OldestAge)
		{
			OldestAge = Age;
			Oldest = Index;
		}
	}
	if (Oldest != INDEX_NONE)
	{
		ReleaseAgent(Oldest);
	}
}

int32 ACitixPedestrianSystem::GetActivePedestrianCount() const{
	int32 Count = 0;
	for (const FCitixPedestrianAgent& Agent : Agents)
	{
		Count += Agent.bActive ? 1 : 0;
	}
	return Count;
}

void ACitixPedestrianSystem::RefreshPlayerSet()
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
		FCitixPedAnchor Anchor;
		const FVector Location = Pawn->GetActorLocation();
		Anchor.Position = FVector2D(Location.X, Location.Y);
		Anchor.Pawn = Pawn;
		Anchor.View.Location = Location;
		Anchor.View.Forward = Pawn->GetActorForwardVector().GetSafeNormal();
		Anchor.View.CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(58.5f));
		Anchor.View.bValid = true;
		DriverAnchors.Add(Anchor);
	}
	// Legacy single-player view (spawn gating below still reads it): mirror the
	// first driver. These members were written by the pre-multi-anchor path;
	// without this refresh every spawn attempt rejects and nobody ever spawns.
	if (DriverAnchors.Num() > 0)
	{
		const FCitixPedAnchor& First = DriverAnchors[0];
		PlayerAnchorXY = First.Position;
		PlayerForward = FVector2D(First.View.Forward.X, First.View.Forward.Y);
		bHasPlayerForward = PlayerForward.SizeSquared() > 0.01f;
		PlayerPawnWeak = First.Pawn.Get();
		CachedPlayerView = First.View;
	}
	else
	{
		bHasPlayerForward = false;
	}
}

FVector2D ACitixPedestrianSystem::NearestPlayerTo(const FVector2D& P, float& OutDistance) const
{
	FVector2D Best = P;
	OutDistance = TNumericLimits<float>::Max();
	for (const FCitixPedAnchor& Anchor : DriverAnchors)
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

bool ACitixPedestrianSystem::IsFarFromAllPlayers(const FVector2D& P, float Dist) const
{
	const float DistSquared = Dist * Dist;
	for (const FCitixPedAnchor& Anchor : DriverAnchors)
	{
		if (FVector2D::DistSquared(Anchor.Position, P) <= DistSquared)
		{
			return false;
		}
	}
	return true;
}

bool ACitixPedestrianSystem::IsOutOfAllViews(const FVector2D& Point, float FarDistance) const
{
	for (const FCitixPedAnchor& Anchor : DriverAnchors)
	{
		if (!Anchor.View.bValid)
		{
			return false;
		}
		const FVector2D ToPoint = Point - Anchor.Position;
		if (ToPoint.SizeSquared() <= FarDistance * FarDistance)
		{
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

bool ACitixPedestrianSystem::IsVisibleToAny(const FVector2D& XY, float Z) const
{
	UWorld* World = GetWorld();
	for (const FCitixPedAnchor& Anchor : DriverAnchors)
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

void ACitixPedestrianSystem::ClearPedestrians()
{
	for (TObjectPtr<ACitixPedestrian>& Pedestrian : Pool)
	{
		if (Pedestrian)
		{
			Pedestrian->Destroy();
		}
	}
	Pool.Reset();
	FreePoolIndices.Reset();
	Agents.Reset();
}

void ACitixPedestrianSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bInitialized || MaxPedestrians <= 0)
	{
		return;
	}

	// The simulation lives on the server. Remote copies only interpolate the
	// replicated pool actors and never spawn, recycle or walk anything.
	// NOTE: net-mode check, not HasAuthority(): locally-spawned actors report
	// local authority even on clients.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	RefreshPlayerSet();
	if (DriverAnchors.Num() == 0)
	{
		return;
	}
	const FVector2D AnchorXY = DriverAnchors[SpawnAnchorCursor++ % DriverAnchors.Num()].Position;
	++VisibilityPhase;

	SpawnAgents(DeltaSeconds, AnchorXY);
	UpdateAgents(DeltaSeconds);
}

int32 ACitixPedestrianSystem::AcquirePoolIndex()
{
	if (FreePoolIndices.Num() > 0)
	{
		return FreePoolIndices.Pop();
	}
	if (Pool.Num() >= MaxPedestrians)
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
	ACitixPedestrian* Pedestrian = World->SpawnActor<ACitixPedestrian>(
		ACitixPedestrian::StaticClass(), FTransform::Identity, Params);
	if (!Pedestrian)
	{
		return INDEX_NONE;
	}

	Pedestrian->InitializeAppearance(FCitixCharacterLibrary::GetRandomStyle(Rng), Rng.RandRange(1, MAX_int32 - 1));
	// So a hitbox overlap can report back here without a global lookup.
	Pedestrian->SetOwningSystem(this);
	Pedestrian->SetActiveVisual(false);

	Pool.Add(Pedestrian);
	return Pool.Num() - 1;
}

void ACitixPedestrianSystem::ReleaseAgent(int32 AgentIndex)
{
	if (!Agents.IsValidIndex(AgentIndex))
	{
		return;
	}

	const FCitixPedestrianAgent& Agent = Agents[AgentIndex];
	if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
	{
		if (Agent.bRagdoll)
		{
			// Hand the actor back in a clean walking state before it is reused.
			ActiveRagdollCount = FMath::Max(0, ActiveRagdollCount - 1);
			Pool[Agent.PoolIndex]->ResetFromRagdoll();
		}
		else
		{
			Pool[Agent.PoolIndex]->SetActiveVisual(false);
		}
		FreePoolIndices.Add(Agent.PoolIndex);
	}

	Agents.RemoveAtSwap(AgentIndex);
}

bool ACitixPedestrianSystem::IsPointVisible(const FVector2D& XY, float Z) const
{
	return FCitixVisibility::IsVisible(GetWorld(), CachedPlayerView,
		FVector(XY.X, XY.Y, Z), PlayerPawnWeak.Get());
}

bool ACitixPedestrianSystem::IsOutOfView(const FVector2D& Point, float FarDistance) const
{
	const FVector2D ToPoint = Point - PlayerAnchorXY;
	if (ToPoint.SizeSquared() > FarDistance * FarDistance)
	{
		return true;
	}
	return bHasPlayerForward && FVector2D::DotProduct(ToPoint, PlayerForward) < 0.f;
}

float ACitixPedestrianSystem::ComputeSidewalkOffset(int32 EdgeIndex, bool bForward) const
{
	const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
	const FCitixRoadSpec Spec = UCitixCitySettings::Get().GetRoadSpec(Edge.RoadClass);
	if (Spec.SidewalkWidth <= 1.f)
	{
		return 0.f;
	}
	return Spec.CarriagewayWidth() * 0.5f + Spec.SidewalkWidth * 0.5f;
}

void ACitixPedestrianSystem::SetDensityScale(float InScale)
{
	DensityScale = FMath::Clamp(InScale, 0.15f, 1.35f);
}

int32 ACitixPedestrianSystem::GetDesiredPedestrianCount() const
{
	// Clamped to MaxPedestrians (not the pool size): see the traffic system comment.
	return FMath::Clamp(FMath::RoundToInt(MaxPedestrians * DensityScale), 0, MaxPedestrians);
}

void ACitixPedestrianSystem::SpawnAgents(float DeltaSeconds, const FVector2D& PlayerXY)
{
	const int32 ActiveCount = GetActivePedestrianCount();
	const int32 DesiredCount = GetDesiredPedestrianCount();
	if (ActiveCount >= DesiredCount)
	{
		return;
	}

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

bool ACitixPedestrianSystem::TrySpawnAgent(const FVector2D& PlayerXY)
{
	if (Network.Edges.Num() == 0)
	{
		return false;
	}

	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		const int32 EdgeIndex = Rng.RandRange(0, Network.Edges.Num() - 1);
		const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
		if (!Edge.bDrivable || ComputeSidewalkOffset(EdgeIndex, true) <= 0.f)
		{
			continue;
		}

		const float EdgeLength = Network.EdgeLength(EdgeIndex);
		if (EdgeLength < 1200.f)
		{
			continue;
		}

		const FVector2D Mid = (Network.Nodes[Edge.NodeA].Position + Network.Nodes[Edge.NodeB].Position) * 0.5f;
		const float DistanceToPlayer = (Mid - PlayerXY).Size();
		if (DistanceToPlayer < 1500.f || DistanceToPlayer > SpawnRadius)
		{
			continue;
		}
		// Never spawn a pedestrian in front of the camera.
		if (!IsOutOfView(Mid, VisibilityClearance))
		{
			continue;
		}

		const int32 PoolIndex = AcquirePoolIndex();
		if (PoolIndex == INDEX_NONE)
		{
			return false;
		}

		FCitixPedestrianAgent Agent;
		Agent.EdgeIndex = EdgeIndex;
		Agent.bForward = Rng.FRand() < 0.5f;
		Agent.Distance = Rng.FRandRange(0.f, EdgeLength);
		Agent.SpeedFactor = Rng.FRandRange(0.85f, 1.18f);
		Agent.Speed = Rng.FRandRange(WalkSpeedMin, WalkSpeedMax);
		Agent.WalkPhase = Rng.FRandRange(0.f, 2.f * PI);
		// Stay on the pavement, with a little lateral variation.
		Agent.LaneOffset = ComputeSidewalkOffset(EdgeIndex, Agent.bForward) * Rng.FRandRange(0.78f, 1.0f);
		Agent.bActive = true;
		Agent.PoolIndex = PoolIndex;
		Agent.Health = PedestrianMaxHealth;

		// Never spawn a pedestrian where the player can see them appear.
		const FVector2D SpawnLocation = GetAgentLocation(Agent);
		if (IsPointVisible(SpawnLocation, SidewalkHeight + 80.f))
		{
			if (Pool.IsValidIndex(PoolIndex) && Pool[PoolIndex])
			{
				Pool[PoolIndex]->SetActiveVisual(false);
			}
			FreePoolIndices.Add(PoolIndex);
			continue;
		}

		Agents.Add(Agent);
		return true;
	}
	return false;
}

FVector2D ACitixPedestrianSystem::GetAgentDirection(const FCitixPedestrianAgent& Agent) const
{
	if (!Network.IsValidEdge(Agent.EdgeIndex))
	{
		return FVector2D(1.f, 0.f);
	}
	const FVector2D Direction = Network.EdgeDirection(Agent.EdgeIndex);
	return Agent.bForward ? Direction : -Direction;
}

FVector2D ACitixPedestrianSystem::GetAgentLocation(const FCitixPedestrianAgent& Agent) const
{
	if (!Network.IsValidEdge(Agent.EdgeIndex))
	{
		return FVector2D::ZeroVector;
	}
	const FCitixRoadEdge& Edge = Network.Edges[Agent.EdgeIndex];
	const FVector2D A = Network.Nodes[Edge.NodeA].Position;
	const FVector2D B = Network.Nodes[Edge.NodeB].Position;
	const FVector2D Origin = Agent.bForward ? A : B;
	const FVector2D Travel = GetAgentDirection(Agent);
	const FVector2D Right(-Travel.Y, Travel.X);
	return Origin + Travel * Agent.Distance + Right * Agent.LaneOffset;
}

float ACitixPedestrianSystem::ProjectDistanceOntoEdge(const FCitixPedestrianAgent& Agent, const FVector& WorldLocation) const
{
	if (!Network.IsValidEdge(Agent.EdgeIndex))
	{
		return Agent.Distance;
	}
	const FCitixRoadEdge& Edge = Network.Edges[Agent.EdgeIndex];
	const FVector2D Origin = Agent.bForward
		? Network.Nodes[Edge.NodeA].Position : Network.Nodes[Edge.NodeB].Position;
	const FVector2D Travel = GetAgentDirection(Agent);
	const FVector2D Local(WorldLocation.X - Origin.X, WorldLocation.Y - Origin.Y);
	// Stay strictly inside the edge so the walker does not immediately advance to the next one.
	return FMath::Clamp(FVector2D::DotProduct(Local, Travel), 0.f, Network.EdgeLength(Agent.EdgeIndex) * 0.98f);
}

float ACitixPedestrianSystem::ProjectLateralOntoEdge(const FCitixPedestrianAgent& Agent, const FVector& WorldLocation) const
{
	if (!Network.IsValidEdge(Agent.EdgeIndex))
	{
		return Agent.LaneOffset;
	}
	const FCitixRoadEdge& Edge = Network.Edges[Agent.EdgeIndex];
	const FVector2D Origin = Agent.bForward
		? Network.Nodes[Edge.NodeA].Position : Network.Nodes[Edge.NodeB].Position;
	const FVector2D Travel = GetAgentDirection(Agent);
	const FVector2D Right(-Travel.Y, Travel.X);
	const FVector2D Local(WorldLocation.X - Origin.X, WorldLocation.Y - Origin.Y);
	return FVector2D::DotProduct(Local, Right);
}

int32 ACitixPedestrianSystem::ChooseNextEdge(int32 CurrentEdge, int32 Node, bool& bOutForward) const
{
	bOutForward = true;
	if (!Network.NodeEdgeIndices.IsValidIndex(Node))
	{
		return INDEX_NONE;
	}

	const TArray<int32>& Incident = Network.NodeEdgeIndices[Node];
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
		if (ComputeSidewalkOffset(CandidateIndex, true) <= 0.f)
		{
			continue;
		}
		const FCitixRoadEdge& Candidate = Network.Edges[CandidateIndex];
		const int32 Other = (Candidate.NodeA == Node) ? Candidate.NodeB : Candidate.NodeA;
		const FVector2D OutDir = (Network.Nodes[Other].Position - NodePos).GetSafeNormal();
		const float Weight = FMath::Max(0.08f, 0.5f + 0.5f * FVector2D::DotProduct(InDir, OutDir));
		Candidates.Add(CandidateIndex);
		Weights.Add(Weight);
		TotalWeight += Weight;
	}

	if (Candidates.Num() == 0)
	{
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

void ACitixPedestrianSystem::UpdateAgents(float DeltaSeconds)
{

	for (int32 Index = Agents.Num() - 1; Index >= 0; --Index)
	{
		FCitixPedestrianAgent& Agent = Agents[Index];
		if (!Agent.bActive || !Network.IsValidEdge(Agent.EdgeIndex))
		{
			ReleaseAgent(Index);
			continue;
		}

		// Ragdolls are pure physics: let the body settle and either fade away (death) or
		// play its get-up animation and walk on (survivable knock-down).
		if (Agent.bRagdoll)
		{
			ACitixPedestrian* Pedestrian = Pool.IsValidIndex(Agent.PoolIndex) ? Pool[Agent.PoolIndex] : nullptr;
			const bool bFinished = Pedestrian ? Pedestrian->UpdateRagdoll(DeltaSeconds) : true;
			if (!bFinished)
			{
				continue;
			}

			if (!Pedestrian || Pedestrian->IsFatalRagdoll())
			{
				// ReleaseAgent decrements the ragdoll budget and resets the actor.
				ReleaseAgent(Index);
				continue;
			}

			// Recovered: back on their feet, and the simulation resumes from wherever the
			// body actually ended up (they were knocked off the pavement, not teleported
			// back onto it).
			ActiveRagdollCount = FMath::Max(0, ActiveRagdollCount - 1);
			++RecoveredCount;
			Agent.bRagdoll = false;
			Agent.HitCooldown = FMath::Max(Agent.HitCooldown, RecoveryGraceTime);
			const FVector Recovered = Pedestrian->GetActorLocation();
			Agent.Distance = ProjectDistanceOntoEdge(Agent, Recovered);
			Agent.LaneOffset = ProjectLateralOntoEdge(Agent, Recovered);
			continue;
		}

		const FVector2D Location = GetAgentLocation(Agent);

		// Gunshot collapse: stand and shrink away in place. No physics, no
		// ragdoll - the lethal-ragdoll path belongs to vehicles only.
		if (Agent.CollapseTimer > 0.f)
		{
			Agent.CollapseTimer = FMath::Max(0.f, Agent.CollapseTimer - DeltaSeconds);
			if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
			{
				ACitixPedestrian* Collapsing = Pool[Agent.PoolIndex];
				Collapsing->SetActiveVisual(true);
				const float Shrink = FMath::Clamp(Agent.CollapseTimer / FMath::Max(0.05f, CollapseDuration), 0.f, 1.f);
				Collapsing->SetActorScale3D(FVector(FMath::Max(0.01f, Shrink)));
				Collapsing->SetActorLocation(
					FVector(Location.X, Location.Y, SidewalkHeight),
					false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (Agent.CollapseTimer <= 0.f)
			{
				if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
				{
					Pool[Agent.PoolIndex]->SetActorScale3D(FVector(1.f));
				}
				ReleaseAgent(Index);
			}
			continue;
		}

		// Recycle only when the player cannot see the pedestrian: beyond a hard radius,
		// or past a minimum distance AND proven occluded/off-screen. Visibility checks
		// are staggered so only a few traces happen per frame. A walker is only
		// recycled out of sight of EVERY driver.
		bool bRecycle = false;
		if (IsFarFromAllPlayers(Location, DespawnRadius * 1.7f))
		{
			bRecycle = true;
		}
		else if (IsFarFromAllPlayers(Location, RecycleMinDistance)
			&& Agent.Age > MinRecycleAge
			&& (Index % FMath::Max(1, VisibilityCheckStride)) == (VisibilityPhase % FMath::Max(1, VisibilityCheckStride)))
		{
			bRecycle = !IsVisibleToAny(Location, SidewalkHeight + 80.f);
		}

		if (bRecycle)
		{
			ReleaseAgent(Index);
			continue;
		}

		// Timers.
		if (Agent.HitCooldown > 0.f)
		{
			Agent.HitCooldown = FMath::Max(0.f, Agent.HitCooldown - DeltaSeconds);
		}
		if (Agent.WaitTimer > 0.f)
		{
			Agent.WaitTimer = FMath::Max(0.f, Agent.WaitTimer - DeltaSeconds);
		}
		if (Agent.FleeTimer > 0.f)
		{
			Agent.FleeTimer = FMath::Max(0.f, Agent.FleeTimer - DeltaSeconds);
			Agent.WaitTimer = 0.f;
		}

		// People pause when a driver is right on top of them and when they are waiting
		// at a corner. A startled pedestrian hurries on instead: nothing stops the flee.
		// Getting knocked down is handled entirely by the ragdoll path above.
		float NearestDriver = 0.f;
		NearestPlayerTo(Location, NearestDriver);
		const bool bFleeing = Agent.FleeTimer > 0.f;
		const float MoveScale = bFleeing ? 1.f : ((NearestDriver < PlayerLingerDistance
			|| Agent.WaitTimer > 0.f) ? 0.f : 1.f);

		// Walk forward (run, while fleeing).
		const float Speed = bFleeing ? FleeRunSpeed : Agent.Speed * Agent.SpeedFactor * MoveScale;
		Agent.Age += DeltaSeconds;
		Agent.Distance += Speed * DeltaSeconds;
		Agent.WalkPhase = FMath::Fmod(
			Agent.WalkPhase + (Speed * DeltaSeconds) / StrideLength * PI, 2.f * PI);

		// Cross to the next edge at junctions.
		int32 Safety = 0;
		while (Agent.Distance >= Network.EdgeLength(Agent.EdgeIndex) && Safety++ < 4)
		{
			const FCitixRoadEdge& Current = Network.Edges[Agent.EdgeIndex];
			const float CurrentLength = Network.EdgeLength(Agent.EdgeIndex);
			const float Overflow = Agent.Distance - CurrentLength;
			const int32 EndNode = Agent.bForward ? Current.NodeB : Current.NodeA;

			bool bNextForward = true;
			const int32 NextEdge = ChooseNextEdge(Agent.EdgeIndex, EndNode, bNextForward);
			if (NextEdge == INDEX_NONE || Network.EdgeLength(NextEdge) < 1.f)
			{
				Agent.bActive = false;
				break;
			}
			Agent.EdgeIndex = NextEdge;
			Agent.bForward = bNextForward;
			Agent.Distance = FMath::Max(0.f, Overflow);
			Agent.LaneOffset = ComputeSidewalkOffset(NextEdge, bNextForward) * Rng.FRandRange(0.78f, 1.0f);

			// Some of them stop at the corner for a moment before carrying on: cheap
			// life, and it breaks up the otherwise perfectly even flow of walkers.
			// Never while fleeing: a startled pedestrian does not stop for the view.
			if (Agent.FleeTimer <= 0.f && Rng.FRand() < KerbWaitChance)
			{
				Agent.WaitTimer = Rng.FRandRange(KerbWaitMin, KerbWaitMax);
			}
		}

		if (!Agent.bActive)
		{
			ReleaseAgent(Index);
			continue;
		}

		// Presentation. Distant pedestrians are hidden to keep draw calls down, but only
		// once they are behind the player or far away, so they never blink out in view.
		if (Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex])
		{
			ACitixPedestrian* Pedestrian = Pool[Agent.PoolIndex];
			// Show unless no driver can see them (off-screen/occluded for all) and
			// they are beyond the visible range of all. Nothing in any view hides.
			const bool bBeyondRange = IsFarFromAllPlayers(Location, VisibleDistance);
			const bool bShouldShow = !bBeyondRange || IsVisibleToAny(Location, SidewalkHeight + 80.f);
			Pedestrian->SetActiveVisual(bShouldShow);
			if (!bShouldShow)
			{
				continue;
			}

			const FVector2D Direction = GetAgentDirection(Agent);
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

			Pedestrian->SetActorLocationAndRotation(
				FVector(Location.X, Location.Y, SidewalkHeight),
				FRotator(0.f, Yaw, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);

			Pedestrian->UpdatePose(Agent.WalkPhase, WalkSwingDegrees, WalkBobCm);
		}
	}
}

void ACitixPedestrianSystem::LogDiagnostics(const FVector2D& PlayerXY) const
{
	UE_LOG(LogCitix, Log, TEXT("[Citix] Pedestrians: %d active / %d pool."),
		GetActivePedestrianCount(), Pool.Num());

	float Nearest = TNumericLimits<float>::Max();
	for (const FCitixPedestrianAgent& Agent : Agents)
	{
		if (Agent.bActive)
		{
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector2D::Distance(GetAgentLocation(Agent), PlayerXY)));
		}
	}
	if (Nearest < TNumericLimits<float>::Max())
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix]   nearest pedestrian: %.0f cm"), Nearest);
	}

	// Living hitboxes: the ones the car's swept contact test can actually hit.
	int32 Hitboxes = 0;
	for (const FCitixPedestrianAgent& Agent : Agents)
	{
		if (Agent.bActive && Pool.IsValidIndex(Agent.PoolIndex) && Pool[Agent.PoolIndex]
			&& Pool[Agent.PoolIndex]->IsHitboxEnabled())
		{
			++Hitboxes;
		}
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix]   hitboxes live: %d"), Hitboxes);
}
