// Copyright Epic Games, Inc. All Rights Reserved.
// Player-centred traffic simulation.
//
// Design:
//   * The road graph is the lane network. Agents travel edge-by-edge.
//   * Simulation data lives in a flat array, not in actors. A fixed pool of
//     kinematic vehicle actors is only used for presentation.
//   * Only agents inside a radius around the player are simulated; the rest are
//     recycled. Cost therefore stays roughly constant as the city grows.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "City/CitixRoadNetwork.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixVisibility.h"
#include "CitixTrafficSystem.generated.h"

class ACitixTrafficVehicle;

/**
 * Everything the speed controller needs to know about the road ahead of one car.
 * Plain data: rebuilt every frame from the lane ahead, never stored.
 */
struct FTrafficAhead
{
	/** Comfortable speed for the sharpest upcoming corner, with its braking curve. */
	float CurveSpeed = FLT_MAX;
	/** Speed allowed by lights and by keeping the junction clear. */
	float JunctionSpeed = FLT_MAX;
	/** Speed allowed by the vehicle ahead (speed-dependent time headway). */
	float FollowSpeed = FLT_MAX;
	float LeadSpeed = -1.f;
	/** Bumper-to-bumper gap to the nearest vehicle ahead along the lane. */
	float Gap = -1.f;
	/** Raw edge distance of the same-edge leader, or -1 when there is none. */
	float SameEdgeLeaderDistance = -1.f;
	float SameEdgeLeadSpeed = -1.f;
	float TurnDegrees = 0.f;
	float JunctionDistance = -1.f;
	bool bLeaderBeyondJunction = false;
	/** How far beyond the junction that queued leader sits, cm. -1 when there is none. */
	float LeaderBeyondJunctionDistance = -1.f;
	/** Index into Agents of the vehicle being followed (debug draw only). */
	int32 LeaderIndex = INDEX_NONE;
};

/** One simulated vehicle. Plain data so thousands could run if needed. */
USTRUCT()
struct FCitixTrafficAgent
{
	GENERATED_BODY()

	UPROPERTY() int32 EdgeIndex = INDEX_NONE;
	UPROPERTY() bool bForward = true;
	UPROPERTY() float Distance = 0.f;
	UPROPERTY() float Speed = 0.f;
	UPROPERTY() float DesiredSpeed = 0.f;
	UPROPERTY() float SpeedFactor = 1.f;
	/** Seconds this agent has existed, used to stop instant re-spawn churn. */
	UPROPERTY() float Age = 0.f;
	UPROPERTY() float LaneOffset = 0.f;
	/** Lane index within this edge's half of the carriageway (0 = kerbside). */
	UPROPERTY() int32 Lane = 0;
	UPROPERTY() bool bActive = false;
	UPROPERTY() int32 PoolIndex = INDEX_NONE;
	UPROPERTY() bool bWheelsVisible = true;

	/**
	 * The turn this car has committed to at the end of the current edge. Picked when it
	 * enters the edge, so braking, junction keep-clear and steering all plan against the
	 * same future rather than each guessing separately.
	 */
	UPROPERTY() int32 NextEdgeIndex = INDEX_NONE;
	UPROPERTY() bool bNextForward = true;

	/** How far past the node it last crossed a car must be before that junction counts as
	 *  cleared: used to suppress stop demands while the car is still inside a crossing. */
	UPROPERTY() float JunctionClearDistance = 0.f;

	/** Free-flow speed for the road this car is on, eased so a class change is not a step. */
	UPROPERTY() float CruiseTarget = 0.f;

	/** Held after a junction so the car does not floor it while the body is rotating. */
	UPROPERTY() float TurnHoldTimer = 0.f;
	UPROPERTY() float TurnHoldSpeed = 0.f;

	/** Startled by the pulse tool: pull over and wait this many seconds. */
	UPROPERTY() float StopTimer = 0.f;

	// ---- Diagnostics (only read by LogDiagnostics / the debug draw) ----
	UPROPERTY() float DiagTargetSpeed = 0.f;
	UPROPERTY() float DiagGap = -1.f;
	UPROPERTY() float DiagLeadSpeed = -1.f;
	UPROPERTY() float DiagTurnDegrees = 0.f;
	UPROPERTY() float DiagJunctionDistance = -1.f;
	UPROPERTY() bool bDiagBraking = false;

	/** Presentation smoothing so lane changes and junction turns do not snap. */
	UPROPERTY() float CurrentYaw = 0.f;
	/** Lateral offset from the edge centreline, in WORLD space, eased as a vector so a
	 *  turn is a swept curve rather than a jump when the edge's lane direction changes. */
	UPROPERTY() FVector2D CurrentOffset = FVector2D::ZeroVector;

	/** Junction path blend: where the car came from, so its presented path can ease from the
	 *  old edge's line onto this one instead of kinking at the node. */
	UPROPERTY() int32 BlendFromEdge = INDEX_NONE;
	UPROPERTY() bool bBlendFromForward = true;
	UPROPERTY() FVector2D BlendFromOffset = FVector2D::ZeroVector;
	/** How far the corner is spread over, chosen per junction so the body can follow the
	 *  path within its yaw rate limit. */
	UPROPERTY() float BlendLength = 250.f;

	UPROPERTY() FVector SmoothedLocation = FVector::ZeroVector;
	UPROPERTY() bool bPresentationStarted = false;

	// ---- Monitored behaviour (see bWatchTraffic) ----
	UPROPERTY() float DiagYaw = 0.f;
	/** Direction the presented car is actually travelling, degrees. */
	UPROPERTY() float DiagMoveYaw = 0.f;
	/** Signed yaw minus motion direction: the "crab angle". Near 0 = the car faces where
	 *  it is going, which is what "not behaving abnormally" means. */
	UPROPERTY() float DiagHeadingError = 0.f;
	UPROPERTY() float DiagYawRate = 0.f;
};

UCLASS(NotBlueprintable)
class CITIX_API ACitixTrafficSystem : public AActor
{
	GENERATED_BODY()

public:
	ACitixTrafficSystem();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Bind the system to a generated road network. */
	void Initialize(const FCitixRoadNetwork& InNetwork, const UCitixCitySettings& InSettings);

	/** Take a vehicle out of traffic (the player got in). Releases its agent. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Traffic")
	bool TakeVehicle(ACitixTrafficVehicle* Vehicle);

	/**
	 * Scale how many vehicles are out at once (rush hour / dead of night). The pool
	 * itself is sized once, so changing this never allocates during play.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citix|Traffic")
	void SetDensityScale(float InScale);

	UFUNCTION(BlueprintPure, Category = "Citix|Traffic")
	float GetDensityScale() const { return DensityScale; }

	int32 GetDesiredVehicleCount() const;

	UFUNCTION(BlueprintPure, Category = "Citix|Traffic")
	int32 GetActiveVehicleCount() const;

	UFUNCTION(BlueprintPure, Category = "Citix|Traffic")
	int32 GetPoolSize() const { return Pool.Num(); }

	/** Remove all traffic. */
	void ClearTraffic();

	/**
	 * Startle traffic near a point (pulse tool): cars pull over and wait.
	 * No damage, no AI changes - just a timed stop. Returns cars affected.
	 */
	int32 StartleVehicles(const FVector& Center, float Radius, float Duration);

	/** How many active cars are inside a radius (witness checks). */
	int32 CountAgentsNear(const FVector& Center, float Radius) const;

	/** Log the nearest active agents and where their presentation actors are. */
	void LogDiagnostics(const FVector2D& PlayerXY) const;

	/** Toggle the look-ahead / following debug draw. */
	void SetDrawDebug(bool bInDraw) { bDrawDebug = bInDraw; }
	bool IsDrawDebug() const { return bDrawDebug; }

	/** Turn behaviour monitoring on/off (also settable with -CitixTrafficWatch). */
	void SetWatchTraffic(bool bInWatch) { bWatchTraffic = bInWatch; }
	bool IsWatchingTraffic() const { return bWatchTraffic; }

	/** One connected driver (server-side anchor for spawning/pooling). */
	struct FCitixDriverAnchor
	{
		FVector2D Position = FVector2D::ZeroVector;
		FCitixPlayerView View;
		TWeakObjectPtr<APawn> Pawn;
	};

	/** Refresh the driver set from connected controllers (server only). */
	void RefreshPlayerSet();

	/** Nearest driver to a point (with distance). Falls back to the anchor. */
	FVector2D NearestPlayerTo(const FVector2D& P, float& OutDistance) const;

	/** True when a point is beyond Dist of every connected driver. */
	bool IsFarFromAllPlayers(const FVector2D& P, float Dist) const;

	/** True when a point is outside every connected driver's view. */
	bool IsOutOfAllViews(const FVector2D& Point, float FarDistance) const;

	/** True when any connected driver can see a point (single trace). */
	bool IsVisibleToAny(const FVector2D& XY, float Z) const;

protected:
	void SpawnAgents(float DeltaSeconds, const FVector2D& PlayerXY);
	bool TrySpawnAgent(const FVector2D& PlayerXY);
	void UpdateAgents(float DeltaSeconds);
	void ReleaseAgent(int32 AgentIndex);

	int32 AcquirePoolIndex();
	int32 ChooseNextEdge(int32 CurrentEdge, int32 Node, bool& bOutForward) const;

	/** Commit to a turning choice at the end of the edge the car is on. */
	void ChoosePlanForAgent(FCitixTrafficAgent& Agent) const;

	/** The straightest continuation from a node (look-ahead beyond the committed turn). */
	int32 StraightestContinuation(int32 CurrentEdge, int32 Node, bool& bOutForward) const;

	/**
	 * Walk the lane up to three edges ahead: find the nearest vehicle, the sharpest
	 * corner and the state of the next junction. This is what makes traffic plan
	 * instead of reacting to whatever is directly underneath the car.
	 */
	void ComputeLookAhead(int32 AgentIndex, FTrafficAhead& Out) const;

	/** Comfortable speed a car can carry through a junction with this turn angle. */
	static float TurnSpeed(float TurnDegrees, float Cruise);

	/**
	 * The corner speed including the room available on the far side: turning sharply onto a
	 * short block has to be taken slowly, because the body cannot rotate faster than
	 * TurnRateDegrees however the path is drawn. Used both to brake for the corner and to
	 * hold the speed through it, so the two never disagree.
	 */
	float CornerSpeed(float Cruise, float TurnDegrees, int32 NextEdge) const;

	/**
	 * True when a car already sits within the minimum gap of this edge's entry point, in the
	 * lane a car would occupy on it. Crossing then would put two cars in the same few metres,
	 * which the per-edge distance clamp cannot detect.
	 */
	bool IsEdgeEntryBlocked(int32 EdgeIndex, bool bForward, int32 Lane) const;

	/** Hard anti-overlap clamp against the nearest same-lane car ahead on the current edge. */
	void ClampToLeader(int32 AgentIndex);

	/** Move an agent's bucket entry when it crosses onto a new edge, mid-pass. */
	void MoveAgentBucket(int32 AgentIndex, int32 FromEdge);

	/** Distance before a node at which a car must be able to stop for it, cm. */
	float NodeStopMargin(int32 Node) const;

	/** Point on an edge's lane: origin + travel*along + a world-space lateral offset. */
	FVector2D EdgePoint(int32 EdgeIndex, bool bForward, float AlongTravel, const FVector2D& Offset) const;

	/** Right-hand unit vector for the direction an edge is travelled in. */
	FVector2D EdgeRight(int32 EdgeIndex, bool bForward) const;

	FVector2D GetAgentLocation(const FCitixTrafficAgent& Agent) const;
	/** Location with an explicit world-space lateral offset (the presented position). */
	FVector2D GetAgentLocationWithOffset(const FCitixTrafficAgent& Agent, const FVector2D& Offset) const;
	FVector2D GetAgentDirection(const FCitixTrafficAgent& Agent) const;
	bool IsEdgeGreen(int32 EdgeIndex, float Time) const;

	/** Agents bucketed by edge, rebuilt once per frame for cheap look-ahead queries. */
	TMap<int32, TArray<int32>> EdgeAgentBuckets;

	/** Pick a car to monitor once, then log its state a few times a second. */
	void WatchTick(float DeltaSeconds, const FVector2D& PlayerXY);

	/** Pool slot of the car being monitored, or INDEX_NONE. */
	int32 WatchedPoolIndex = INDEX_NONE;
	float WatchTimer = 0.f;
	float WatchElapsed = 0.f;

	/** Spawn gating counters, logged by LogDiagnostics so rejections are visible. */
	mutable int32 SpawnBlockedOccupied = 0;
	mutable int32 SpawnBlockedTooClose = 0;
	mutable int32 SpawnBlockedInView = 0;
	mutable int32 SpawnBlockedNoRoom = 0;

	UPROPERTY()
	TArray<TObjectPtr<ACitixTrafficVehicle>> Pool;

	/** Kerbside parked cars; repositioned when the player moves far enough. */
	UPROPERTY()
	TArray<TObjectPtr<ACitixTrafficVehicle>> ParkedPool;

	/** Where the parked cars are currently scattered around. */
	FVector2D ParkedAnchor = FVector2D(FLT_MAX);
	bool bParkedInitialised = false;

	/** Agents that braked for the player on the last update (diagnostics). */
	int32 LastPlayerAvoidCount = 0;

	/** Connected drivers refreshed every tick (server-side pooling anchors). */
	TArray<FCitixDriverAnchor> DriverAnchors;
	int32 SpawnAnchorCursor = 0;

	/** Cumulative count of agents braking for the player (diagnostics). */
	int32 PlayerAvoidEvents = 0;

	/** Multiplier on the active vehicle budget set by the time of day. */
	float DensityScale = 1.f;

	/**
	 * True when a point is far enough away, or behind the player, to be spawned or
	 * recycled without the player seeing it pop. This is what stops traffic and
	 * parked cars appearing/disappearing in view while driving forward.
	 */
	bool IsOutOfView(const FVector2D& Point, float FarDistance) const;

	/** Player heading, used by IsOutOfView. */
	FVector2D PlayerForward = FVector2D(1.f, 0.f);
	FVector2D PlayerAnchorXY = FVector2D::ZeroVector;
	bool bHasPlayerForward = false;

	/** Cached player camera, refreshed each tick, used to gate all pooling. */
	FCitixPlayerView CachedPlayerView;
	int32 VisibilityPhase = 0;

	/**
	 * Minimum seconds a car must live before it can be recycled for being unseen.
	 * Without this, cars spawned behind the player are recycled instantly (behind is
	 * both the spawn zone and an invisible zone) and the population never builds up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "0"))
	float MinRecycleAge = 8.f;
	TWeakObjectPtr<AActor> PlayerPawnWeak;
	FVector2D CachedPlayerXY = FVector2D::ZeroVector;

	/** True when the player currently has line of sight to a world point. */
	bool IsPointVisible(const FVector2D& XY, float Z = 80.f) const;

	void RefreshParkedVehicles(const FVector2D& PlayerXY);

	UPROPERTY()
	TArray<FCitixTrafficAgent> Agents;

	UPROPERTY()
	FCitixRoadNetwork Network;

	UPROPERTY()
	FCitixTrafficSettings TrafficSettings;

	float CruiseSpeed = 1100.f;
	int32 MaxVehicles = 48;
	float SpawnRadius = 42000.f;
	float DespawnRadius = 65000.f;
	float FollowDistance = 700.f;

	/** Beyond this distance a traffic car drops its wheels, cm. */
	float WheelLodDistance = 9000.f;

	/** Parked cars along the kerb: how many, and within what radius of the player.
	 *  Disabled for now (0) - kerbside parked cars are not wanted at the moment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Parking", meta = (ClampMin = "0"))
	int32 MaxParkedVehicles = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Parking", meta = (ClampMin = "1000"))
	float ParkedRadius = 22000.f;

	/** Re-scatter parked cars once the player has moved this far, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Parking", meta = (ClampMin = "500"))
	float ParkedRefreshDistance = 7000.f;

	/** A parked car is only repositioned once it is at least this far away, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Parking", meta = (ClampMin = "1000"))
	float ParkedRecycleDistance = 18000.f;

	/** New parked cars are only placed this far away (or behind the player), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Parking", meta = (ClampMin = "1000"))
	float ParkedSpawnClearance = 13000.f;

	/** Chance a parked slot is skipped, so kerbsides are not perfectly full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Parking", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ParkedSkipChance = 0.35f;

	// ---- Player avoidance -------------------------------------------------

	/** Traffic brakes for the player's car or character instead of driving through them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance")
	bool bAvoidPlayer = true;

	/** Look-ahead distance for the player obstacle, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "200"))
	float PlayerAvoidDistance = 3500.f;

	/** Stop this far short of the player, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "0"))
	float PlayerAvoidMargin = 900.f;

	/** Lateral half-width treated as "in my lane", cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "100"))
	float PlayerAvoidHalfWidth = 420.f;

	/**
	 * Traffic is only spawned at least this far away, or behind the player, so cars
	 * never pop into existence in front of the camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "1000"))
	float SpawnClearance = 38000.f;

	/** A car must be at least this far away before it can be recycled, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "1000"))
	float RecycleMinDistance = 9000.f;

	/** Check 1 in N agents per frame for line of sight, so only a few traces happen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Avoidance", meta = (ClampMin = "1"))
	int32 VisibilityCheckStride = 4;

	/** Max heading change per second, degrees (stops junction turns snapping). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic")
	float TurnRateDegrees = 85.f;

	/** Below this speed the yaw rate is scaled down proportionally, so a car that is
	 *  stopping cannot pivot on the spot - a real car only turns while it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "1"))
	float MinSteerSpeed = 300.f;

	/** Distance over which a car's lateral offset eases to its new lane, cm. Making this a
	 *  distance rather than a rate keeps the arc shape (and so the heading swing) the same
	 *  at any speed - a time-based ease makes a slow car swerve to change lane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "50"))
	float LaneChangeDistance = 900.f;

	/** How fast the presented transform catches up to the simulated position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic")
	float PositionInterpSpeed = 16.f;

	/** Seconds of following distance added per cm/s of speed (time headway). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0.2"))
	float FollowTimeHeadway = 1.0f;

	/** How fast the free-flow speed chases a new road class (1/s). Keeps class changes smooth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0.1"))
	float CruiseSmoothingSpeed = 1.4f;

	/** Start the corner braking curve this many cm earlier than strictly needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float TurnBrakeMargin = 200.f;

	/** Floor for how far short of a junction a car must be able to stop, cm. The real
	 *  margin grows with the width of the junction so cars never stop on the crossing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float JunctionStopMargin = 900.f;

	/** A car treats the junction as blocked when the leader beyond it is below this speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float JunctionBlockSpeed = 350.f;

	/** A queue within this distance past the junction counts as blocking it, so cars stop at
	 *  the stop line instead of entering and stopping in the middle of the crossing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float JunctionExitClearance = 1400.f;

	/** Never spawn within this distance of an existing vehicle, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float SpawnClearanceDistance = 2400.f;

	/** Free lane required ahead of a spawn point, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float SpawnLaneClearance = 1800.f;

	/** Stop growing the population once this many cars are within NearbyVehicleRadius of
	 *  the player, so traffic cannot pool around a player who is sitting still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	int32 MaxNearbyVehicles = 26;

	/** Radius of the local population cap, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "1000"))
	float NearbyVehicleRadius = 12000.f;

	/** Draw look-ahead points, leader links and target speeds for nearby cars. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic")
	bool bDrawDebug = false;

	/** Shortest junction blend, cm. The real length is the larger of an angle-based floor and
	 *  a speed-based term, so a corner is always swept over enough distance for the body to
	 *  follow it: turn angle alone can demand a wide sweep at walking pace, and speed alone
	 *  can demand one when a car crosses at 2 km/h and then accelerates through the corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "100"))
	float JunctionBlendMinDistance = 250.f;

	/** Corner length allowed per degree of turn, cm. 12 gives a 90-degree corner about
	 *  11 m of sweep, which is roughly what a real junction looks like. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "0"))
	float JunctionBlendAngleScale = 12.f;

	/** Safety margin on the yaw rate a junction blend may demand. > 1 means the body always
	 *  has headroom, so the heading never lags the path. The blend and the lane-offset
	 *  rotation happen at the same time and their yaw demands add up, so this has to be
	 *  comfortably more than 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic", meta = (ClampMin = "1.0"))
	float JunctionBlendYawFactor = 2.0f;

	/** Log one car's yaw, motion direction, crab angle and yaw rate over time
	 *  (`-CitixTrafficWatch`). Used to prove cars are not turning abnormally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Traffic")
	bool bWatchTraffic = false;

	/** Movement model constants (cm, s). Soft enough to read as city driving: the aim is
	 *  smooth, believable flow, not maximum-effort braking. */
	static constexpr float Acceleration = 400.f;
	static constexpr float Deceleration = 650.f;

	/** Nominal half length of a traffic car, cm (bumper-plane used by the gap maths). */
	static constexpr float VehicleHalfLength = 250.f;

	/** Absolute bumper-to-bumper minimum, the hard anti-overlap clamp, cm. */
	static constexpr float HardBumperGap = 320.f;

	FRandomStream Rng;
	float ElapsedTime = 0.f;
	float SpawnAccumulator = 0.f;
	float TrafficLogTimer = 0.f;
	TArray<int32> FreePoolIndices;
	bool bInitialized = false;

	UPROPERTY()
	TSubclassOf<ACitixTrafficVehicle> VehicleClass;
};
