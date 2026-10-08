// Copyright Epic Games, Inc. All Rights Reserved.
// Player-centred pedestrian population.
//
// Pedestrians walk along the road network's sidewalks (the lane network offset
// laterally onto the pavement). Like traffic, only agents near the player are
// simulated; a pool of low-poly actor presentations is recycled.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "City/CitixRoadNetwork.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixVisibility.h"
#include "CitixPedestrianSystem.generated.h"

class ACitixPedestrian;

/** One simulated pedestrian. */
USTRUCT()
struct FCitixPedestrianAgent
{
	GENERATED_BODY()

	UPROPERTY() int32 EdgeIndex = INDEX_NONE;
	UPROPERTY() bool bForward = true;
	UPROPERTY() float Distance = 0.f;
	UPROPERTY() float Speed = 130.f;
	UPROPERTY() float LaneOffset = 0.f;
	UPROPERTY() float WalkPhase = 0.f;
	UPROPERTY() float SpeedFactor = 1.f;
	/** Seconds this pedestrian has existed, used to stop instant re-spawn churn. */
	UPROPERTY() float Age = 0.f;
	UPROPERTY() bool bActive = false;
	UPROPERTY() int32 PoolIndex = INDEX_NONE;

	/** True while this pedestrian is a physics ragdoll rather than a walker. */
	UPROPERTY() bool bRagdoll = false;

	/**
	 * Cheap per-citizen health. Deliberately just a float on the agent: no component, no
	 * UI, no ticking. Only used to decide "knocked down" versus "killed".
	 */
	UPROPERTY() float Health = 100.f;

	/** Short immunity so one pass of a car counts as one hit, not many. */
	UPROPERTY() float HitCooldown = 0.f;

	/** Seconds left of a kerbside pause (waiting to cross, checking a phone...). */
	UPROPERTY() float WaitTimer = 0.f;

	/** Seconds left of hurrying away from a startling event (pulse tool). */
	UPROPERTY() float FleeTimer = 0.f;

	/** Seconds left of collapsing in place after a lethal gunshot (no ragdoll). */
	UPROPERTY() float CollapseTimer = 0.f;
};

UCLASS(NotBlueprintable)
class CITIX_API ACitixPedestrianSystem : public AActor
{
	GENERATED_BODY()

public:
	ACitixPedestrianSystem();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Initialize(const FCitixRoadNetwork& InNetwork, const UCitixCitySettings& InSettings);

	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	int32 GetActivePedestrianCount() const;

	/** Scale how many pedestrians are out at once (rush hour / dead of night). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Pedestrians")
	void SetDensityScale(float InScale);

	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	float GetDensityScale() const { return DensityScale; }

	int32 GetDesiredPedestrianCount() const;

	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	int32 GetPoolSize() const { return Pool.Num(); }

	/**
	 * Knock everyone inside a radius. They lose health based on the impact speed and are
	 * knocked down into a physics ragdoll; those who survive the hit get back up and walk
	 * on, those who do not fade away.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citix|Pedestrians")
	int32 HitPedestriansInRadius(const FVector& Center, float Radius, const FVector& Impulse, float ImpactSpeed);

	/**
	 * Swept contact test: did something of this radius pass through a pedestrian's hitbox
	 * between From and To? A car crosses a whole body in one frame at speed, so a radius
	 * bubble around the car either misses them or has to be made so large that it catches
	 * people who were never touched.
	 */
	int32 HitPedestriansAlongSweep(const FVector& From, const FVector& To, float Radius,
		const FVector& Impulse, float ImpactSpeed);

	/**
	 * A pedestrian's hitbox was overlapped by the player (car or on foot). Applies the normal
	 * knock-down rules, so a physical contact and a swept contact behave identically.
	 */
	void NotifyPedestrianContact(ACitixPedestrian* Pedestrian, const FVector& OtherLocation,
		const FVector& OtherVelocity);

	/**
	 * Startle everyone inside a radius: walkers hurry on for a few seconds instead
	 * of strolling. No damage, no ragdolls - this is the pulse tool's crowd reaction.
	 * Returns how many pedestrians reacted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citix|Pedestrians")
	int32 FleeFrom(const FVector& Center, float Radius);

	/**
	 * Gunfire: nearest walker to a shot segment takes flat damage (hits-to-kill per
	 * gun). No cooldown games: one bullet, one victim, decided immediately.
	 * Returns hits applied; OutKills counts the lethal ones.
	 */
	int32 GunHit(const FVector& From, const FVector& To, float Radius, float Damage, int32& OutKills);

	/** Nearest active walker to a point (shops, tests). False when the streets are empty. */
	bool GetNearestWalker(const FVector& Center, float MaxDistance, FVector& OutLocation) const;

	/** Shove from an on-foot player: stumble and hurry on, never a ragdoll. */
	void StaggerPedestrian(ACitixPedestrian* Pedestrian, const FVector& FromLocation);

	/** Physics ragdolls (knock-downs and deaths) currently simulated. */
	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	int32 GetRagdollCount() const { return ActiveRagdollCount; }

	/** Knock-downs that have already got back on their feet this session (diagnostics). */
	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	int32 GetRecoveredCount() const { return RecoveredCount; }

	/** Lethal hits this session (gun kills escalate the wanted level). */
	UFUNCTION(BlueprintPure, Category = "Citix|Pedestrians")
	int32 GetDeathCount() const { return DeathCount; }

	/** Health every pedestrian starts with. Damage is scaled by impact speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "1"))
	float PedestrianMaxHealth = 100.f;
	/** Impact speed below which a bump does no damage at all, cm/s (~12 km/h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float MinDamageSpeed = 350.f;

	/** Damage per cm/s of impact speed above the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float DamagePerSpeed = 0.09f;

	/** Total length of the knocked-down reaction (fall + lie + stand up), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0.2"))
	float StaggerDuration = 2.0f;

	/** Immunity after a hit, so a single car pass counts once, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float HitCooldownTime = 0.7f;

	/** Extra immunity after standing back up, so they are not immediately hit again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float RecoveryGraceTime = 1.5f;

	/** Speed of a hurrying (startled) pedestrian, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float FleeRunSpeed = 320.f;

	/** How long a startled pedestrian hurries on, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float FleeDuration = 4.f;

	/** How long a gunshot victim takes to collapse in place, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0.2"))
	float CollapseDuration = 0.6f;

	void ClearPedestrians();

	/** One connected driver (server-side anchor for spawning/pooling). */
	struct FCitixPedAnchor
	{
		FVector2D Position = FVector2D::ZeroVector;
		FCitixPlayerView View;
		TWeakObjectPtr<APawn> Pawn;
	};

	/** Refresh the driver set from connected controllers (server only). */
	void RefreshPlayerSet();

	/** Nearest driver to a point (with distance). */
	FVector2D NearestPlayerTo(const FVector2D& P, float& OutDistance) const;

	/** True when a point is beyond Dist of every connected driver. */
	bool IsFarFromAllPlayers(const FVector2D& P, float Dist) const;

	/** True when a point is outside every connected driver's view. */
	bool IsOutOfAllViews(const FVector2D& Point, float FarDistance) const;

	/** True when any connected driver can see a point (single trace). */
	bool IsVisibleToAny(const FVector2D& XY, float Z) const;

	/** Log the nearest agents (diagnostics). */
	void LogDiagnostics(const FVector2D& PlayerXY) const;

protected:
	void SpawnAgents(float DeltaSeconds, const FVector2D& PlayerXY);
	bool TrySpawnAgent(const FVector2D& PlayerXY);
	void UpdateAgents(float DeltaSeconds);
	void ReleaseAgent(int32 AgentIndex);

	int32 AcquirePoolIndex();
	int32 ChooseNextEdge(int32 CurrentEdge, int32 Node, bool& bOutForward) const;
	FVector2D GetAgentLocation(const FCitixPedestrianAgent& Agent) const;
	FVector2D GetAgentDirection(const FCitixPedestrianAgent& Agent) const;

	/** Distance along the agent's edge nearest to a world position (clamped to the edge). */
	float ProjectDistanceOntoEdge(const FCitixPedestrianAgent& Agent, const FVector& WorldLocation) const;

	/** Lateral offset from the edge centre nearest to a world position. */
	float ProjectLateralOntoEdge(const FCitixPedestrianAgent& Agent, const FVector& WorldLocation) const;

	/** Free a ragdoll slot by making the oldest knock-down stand up. False if none can. */
	bool FastForwardOldestKnockdown();

	/** Free a ragdoll slot by recycling the oldest death. */
	void ReleaseOldestRagdoll();

	/** Sidewalk lateral offset for a travel direction on an edge, cm. */
	float ComputeSidewalkOffset(int32 EdgeIndex, bool bForward) const;

	/** True when a point is far enough away, or behind the player, to change without popping. */
	bool IsOutOfView(const FVector2D& Point, float FarDistance) const;

	/** True when the player currently has line of sight to a world point. */
	bool IsPointVisible(const FVector2D& XY, float Z) const;

	/** A pedestrian must be at least this far away before it can be hidden/recycled, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "500"))
	float RecycleMinDistance = 6000.f;

	/** Check 1 in N pedestrians per frame for line of sight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "1"))
	int32 VisibilityCheckStride = 4;

	/** Physics ragdolls are expensive; only this many are simulated at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	int32 MaxConcurrentRagdolls = 14;

	int32 ActiveRagdollCount = 0;

	/** Knock-downs that have stood back up (diagnostics). */
	int32 RecoveredCount = 0;

	/** Lethal hits this session. */
	int32 DeathCount = 0;

	/** Minimum seconds before a pedestrian can be recycled for being unseen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Pedestrians", meta = (ClampMin = "0"))
	float MinRecycleAge = 8.f;

	FCitixPlayerView CachedPlayerView;
	int32 VisibilityPhase = 0;
	TWeakObjectPtr<AActor> PlayerPawnWeak;

	/** Apply one knock-down (health, ragdoll choice, budget, impulse) to one agent. */
	void ApplyHit(int32 AgentIndex, const FVector& Impulse, const FVector& Center, float ImpactSpeed,
		float FlatDamage = -1.f, float CooldownOverride = -1.f);

	/** Index of the live agent owning a pool slot, or INDEX_NONE. */
	int32 FindAgentByPoolIndex(int32 PoolIndex) const;

	FVector2D PlayerForward = FVector2D(1.f, 0.f);
	FVector2D PlayerAnchorXY = FVector2D::ZeroVector;
	bool bHasPlayerForward = false;

	/** Connected drivers refreshed every tick (server-side pooling anchors). */
	TArray<FCitixPedAnchor> DriverAnchors;
	int32 SpawnAnchorCursor = 0;

	UPROPERTY()
	TArray<TObjectPtr<ACitixPedestrian>> Pool;

	UPROPERTY()
	TArray<FCitixPedestrianAgent> Agents;

	UPROPERTY()
	FCitixRoadNetwork Network;

	/** Height of the pavement surface, cm. */
	float SidewalkHeight = 16.f;

	int32 MaxPedestrians = 90;
	float SpawnRadius = 22000.f;

	/** Multiplier on the active pedestrian budget set by the time of day. */
	float DensityScale = 1.f;
	float DespawnRadius = 30000.f;
	/** Beyond this distance pedestrians are hidden (but still simulated). */
	float VisibleDistance = 14000.f;

	/** Pedestrians only appear/vanish this far away, or behind the player, cm. */
	float VisibilityClearance = 16000.f;

	/** Pedestrians pause when the player is this close, cm. */
	float PlayerLingerDistance = 260.f;

	/** Chance a pedestrian pauses at a junction before carrying on. */
	float KerbWaitChance = 0.35f;
	float KerbWaitMin = 0.7f;
	float KerbWaitMax = 3.4f;
	float WalkSpeedMin = 95.f;
	float WalkSpeedMax = 175.f;

	FRandomStream Rng;
	int32 Seed = 0;
	float SpawnAccumulator = 0.f;
	TArray<int32> FreePoolIndices;
	bool bInitialized = false;
};
