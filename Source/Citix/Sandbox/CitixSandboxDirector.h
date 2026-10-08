// Copyright Epic Games, Inc. All Rights Reserved.
// Sandbox director: the "game" layer on top of the generated city.
//
// Owns everything that turns free driving into play:
//   * Points of interest and discovery  - exploration has progress
//   * District entry cards              - the city reads as neighbourhoods
//   * Activities (drive / tour)         - driving has a purpose and a reward
//   * Delivery jobs + daily contracts   - the money economy
//   * Weapon shops, loadout and ammo    - buy guns, keep them fed
//   * Hitscan guns + ADS + sniper scope - crosshair shooting, no gore
//   * Stunts                            - driving has skill expression
//   * Photo scoring                     - viewpoint play, no image analysis
//   * Wanted level + police pursuit     - kills and chaos bring heat
//
// It also owns the navigation waypoint and the road-graph route, which the HUD draws.
// Everything here is intentionally cheap: one actor, a low-frequency tick, and no
// per-frame work proportional to the city size. All event grants are debounced so a
// reward or notification can only fire once per transition.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "City/CitixRoadNetwork.h"
#include "City/CitixCityPlan.h"
#include "CitixTypes.h"
#include "Sandbox/CitixWeaponDefs.h"
#include "CitixSandboxDirector.generated.h"

class UStaticMeshComponent;
class ACitixPoliceVehicle;
class ACitixPlayerState;
class ACitixGameState;
class ACitixPulseBolt;
class ACitixHitSpark;
class ACitixShopkeeper;
class ACitixWeaponProp;
class ACitixRouteGuide;
class ACitixPoliceOfficer;
class UCameraComponent;

/** What kind of place a POI is. Drives cards, prompts and photo scoring. */
UENUM(BlueprintType)
enum class ECitixPOIKind : uint8
{
	District,
	Landmark,
	Riverside,
	Crossing
};

/** A discoverable place in the city. */
USTRUCT(BlueprintType)
struct FCitixPointOfInterest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Sandbox")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Sandbox")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Sandbox")
	ECitixDistrict District = ECitixDistrict::Downtown;

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Sandbox")
	ECitixPOIKind Kind = ECitixPOIKind::Crossing;

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Sandbox")
	bool bDiscovered = false;
};

UENUM(BlueprintType)
enum class ECitixJobState : uint8
{
	Idle,
	Active,
	Complete,
	Failed
};

/** Reusable activity shape. Delivery jobs stay separate and legacy. */
UENUM(BlueprintType)
enum class ECitixActivityType : uint8
{
	None,
	Drive,
	Tour
};

UENUM(BlueprintType)
enum class ECitixActivityState : uint8
{
	Idle,
	Active,
	Complete,
	Failed
};

/** Police pursuit state, for the HUD. */
UENUM(BlueprintType)
enum class ECitixWantedState : uint8
{
	Calm,
	Searching,
	Pursuit
};

/** Shared race lifecycle (rank 7). Solo practice out of scope: countdown needs 2+. */
UENUM(BlueprintType)
enum class ECitixRaceState : uint8
{
	Idle,
	Countdown,
	Racing,
	Finished
};

/** One race participant (server-only; snapshots publish per owner). */
USTRUCT()
struct FRaceRacer
{
	GENERATED_BODY()

	TWeakObjectPtr<AController> PC;
	int32 CheckpointsDone = 0;
	float FinishTime = 0.f;
	int32 FinishPlace = 0;
	bool bFinished = false;
	bool bDNF = false;
	bool bRewardPaid = false;
};

/** One police car and its pursuit bookkeeping. Capped at 3, always. */
USTRUCT()
struct FCitixPoliceUnit
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ACitixPoliceVehicle> Car = nullptr;

	/** The wanted driver this unit hunts (never the nearest innocent). */
	TWeakObjectPtr<AController> SuspectPC;

	/** Node path to the player's node, recomputed at 1 Hz (staggered). */
	TArray<int32> Path;
	int32 PathCursor = 0;
	float RepathTimer = 0.f;
	/** Seconds the player has been slow inside catch range. */
	float CatchTimer = 0.f;
	/** Patrol health: gun hits disable the car (pistol 3 / smg 4 / rifle 2 / sniper 1). */
	float Health = 100.f;
	bool bDisabled = false;
	/** Cooldown between aimed shots at the player. */
	float FireTimer = 0.f;
	/** Smoothed pursuit heading (per-frame turns, no snapping). */
	float PursuitYaw = 0.f;
	/** Ram contact damage cooldown. */
	float RamCooldown = 0.f;
	/** Current pursuit speed: driven with accel/brake limits like traffic. */
	float Speed = 0.f;
	/** Road height under the car (ground trace, refreshed on repath). */
	float GroundZ = 0.f;
};

/** One dismounted officer and their bookkeeping. Capped separately. */
USTRUCT()
struct FCitixPoliceOfficerState
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ACitixPoliceOfficer> Officer = nullptr;

	float Health = 100.f;
	bool bDying = false;
	float FireTimer = 0.f;
	/** The car they stepped out of (index-free: unit array compacts). */
	TWeakObjectPtr<ACitixPoliceVehicle> UnitCar;
};

/** One weapon shop: a roadside stop with a stationed employee. */
USTRUCT()
struct FCitixWeaponShop
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	FString Name;

	UPROPERTY()
	TObjectPtr<ACitixShopkeeper> Keeper = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Marker = nullptr;
};

/** Everything about one driver's game: jobs, route, wanted, loadout, feed. */
USTRUCT()
struct FCitixPlayerGame
{
	GENERATED_BODY()

	TWeakObjectPtr<AController> PC;

	// ---- Waypoint / route ----
	FVector WaypointLocation = FVector::ZeroVector;
	FString WaypointName;
	bool bHasWaypoint = false;
	int32 WaypointCursor = -1;

	UPROPERTY()
	TArray<FVector> RoutePoints;

	// ---- Delivery job ----
	ECitixJobState JobState = ECitixJobState::Idle;
	FVector JobDestination = FVector::ZeroVector;
	float JobTimeRemaining = 0.f;
	float JobDistanceRemaining = 0.f;
	float JobRewardMult = 1.f;

	// ---- Activity ----
	ECitixActivityType ActivityType = ECitixActivityType::None;
	ECitixActivityState ActivityState = ECitixActivityState::Idle;
	FString ActivityName;

	UPROPERTY()
	TArray<FVector> ActivityStops;

	TArray<FString> ActivityStopNames;
	int32 ActivityStopsDone = 0;
	float ActivityTimeRemaining = 0.f;
	float ActivityDistanceRemaining = 0.f;
	bool bActivityRushRoute = false;
	bool bOffRoute = false;
	float OffRouteTimer = 0.f;
	float OffRouteToastCooldown = 0.f;

	// ---- Wanted ----
	float Heat = 0.f;
	int32 WantedStars = 0;
	float TimeSinceIncident = 1000.f;
	float CrashCooldown = 0.f;
	float LastHullFraction = 1.f;
	float EscapeProgress = 0.f;

	// ---- Progression ----
	int32 Score = 0;
	int32 Money = 0;
	int32 JobsCompleted = 0;
	int32 ActivitiesCompleted = 0;
	int32 PhotosTaken = 0;
	int32 LastPhotoScore = 0;
	FString LastPhotoBreakdown;
	float PhotoCooldown = 0.f;
	bool bMinimapDetailUnlocked = false;
	bool bVariantBonusesUnlocked = false;
	TSet<int32> VisitedDistricts;
	/** Personally discovered POI indices (personal rewards, rank 16). */
	TSet<int32> DiscoveredPOIs;

	// ---- District tracking ----
	ECitixDistrict CurrentDistrict = ECitixDistrict::OuterCity;
	int32 CurrentDistrictIndex = INDEX_NONE;
	float DistrictCardCooldown = 0.f;

	// ---- Daily contract ----
	float LastHour = -1.f;
	bool bQuestOffered = false;
	bool bQuestAccepted = false;

	// ---- Weapons ----
	bool bOwnedWeapons[4] = { false, false, false, false };
	int32 MagAmmo[4] = { 0, 0, 0, 0 };
	int32 ReserveAmmo[4] = { 0, 0, 0, 0 };
	int32 CurrentWeapon = -1;
	float FireCooldownTimer = 0.f;
	float ReloadTimer = 0.f;
	bool bTriggerHeld = false;
	bool bTriggerFresh = false;
	bool bAdsHeld = false;
	float AdsAmount = 0.f;
	float NoWeaponToastCooldown = 0.f;

	// ---- Stunt transient (per driver; shared copies interfered) ----
	float StuntAirTime = 0.f;
	FVector StuntAirStart = FVector::ZeroVector;
	bool bStuntWasGrounded = true;

	// ---- Spawn protection (server time until which damage is ignored) ----
	float SpawnProtectedUntil = -1000.f;

	// ---- Event feed (serial bumps; clients show each once, locally timed) ----
	FString CardTitle;
	FString CardSub;
	int32 CardSerial = 0;
	FString ToastText;
	int32 ToastSerial = 0;
};

UCLASS(NotBlueprintable)
class CITIX_API ACitixSandboxDirector : public AActor
{
	GENERATED_BODY()

public:
	ACitixSandboxDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called by the generator once the city exists. */
	void Configure(const FCitixRoadNetwork& InNetwork, const FCitixCityPlan& InPlan,
		const TArray<FVector>& LandmarkLocations, float CitySize);

	// ---- Discovery -----------------------------------------------------
	const TArray<FCitixPointOfInterest>& GetPOIs() const { return POIs; }
	int32 GetPOICount() const { return POIs.Num(); }
	int32 GetDiscoveredCount() const;

	/** Latest discovery card for the HUD (name / sub-line / progress). */
	bool HasDiscoveryCard() const { return DiscoveryCardRemaining > 0.f; }
	const FString& GetDiscoveryCardTitle() const { return DiscoveryCardTitle; }
	const FString& GetDiscoveryCardSub() const { return DiscoveryCardSub; }
	float GetDiscoveryCardRemaining() const { return DiscoveryCardRemaining; }

	// ---- Districts -----------------------------------------------------
	ECitixDistrict GetCurrentDistrict() const { return CurrentDistrict; }
	bool HasDistrictCard() const { return DistrictCardRemaining > 0.f; }
	const FString& GetDistrictCardTitle() const { return DistrictCardTitle; }
	const FString& GetDistrictCardSub() const { return DistrictCardSub; }
	float GetDistrictCardRemaining() const { return DistrictCardRemaining; }
	int32 GetVisitedDistrictCount() const { return VisitedDistricts.Num(); }

	/** One-line flavour for a district type. Shared table, no per-place cases. */
	static FString DistrictFlavor(ECitixDistrict District);

	// ---- Per-player replication (stage 2, additive) ----------------------
	/** Find (or create, server-side) one driver's game state. */
	FCitixPlayerGame* FindPlayerGame(AController* PC);
	FCitixPlayerGame* FindPlayerGame(APawn* Pawn);

	/** Read-only lookup (never creates; safe for const display paths). */
	const FCitixPlayerGame* FindPlayerGameConst(AController* PC) const;

	/** Drop a departed driver's record (logout cleanup). */
	void RemovePlayerGame(AController* PC);

	/** Copy one driver's results into their player state (server). */
	void PublishPlayerGame(FCitixPlayerGame& Game);

	/**
	 * Focus mirror: copy one record into the legacy global fields so existing
	 * local readers (HUD, tests, host view) keep working unchanged. Dedicated
	 * servers mirror the first driver (no local view); listen hosts mirror the
	 * host. Every remote client renders its own snapshot instead.
	 */
	void CopyGameToGlobals(const FCitixPlayerGame& Game);

	/** Copy shared city knowledge into the game state (server, on change). */
	void PublishSharedState();

	/** Publish the authoritative city seed once the city exists. */
	void PublishCitySeed();

	/** Remember a violence instigator for kill-heat attribution (3 s window). */
	void NoteViolence(AController* InInstigator);

	class ACitixPlayerState* PlayerStateFor(FCitixPlayerGame& Game) const;
	void ShowCard(FCitixPlayerGame& Game, const FString& Title, const FString& Sub);
	void ShowToast(FCitixPlayerGame& Game, const FString& Text);
	void AddScore(FCitixPlayerGame& Game, int32 Points, const TCHAR* Reason);
	void AddMoney(FCitixPlayerGame& Game, int32 Amount, const TCHAR* Reason);
	void CheckProgressUnlocks(FCitixPlayerGame& Game);

	/** Server: mirror the shared sim into every PlayerState + the GameState. */
	void MirrorToReplication();

	/** Client: presentation-only copy from replication + local route/beacon/guide. */
	void UpdateFromReplication(const ACitixPlayerState* PS, const ACitixGameState* GS,
		const FVector& PlayerLocation, float DeltaSeconds);

	// ---- Navigation (per-driver records; legacy readers use focus) ------
	void SetWaypoint(AController* PC, const FVector& InLocation, const FString& InName);
	void ClearWaypoint(AController* PC);
	void SetWaypoint(const FVector& InLocation, const FString& InName);
	void ClearWaypoint();
	bool HasWaypoint() const { return bHasWaypoint; }
	const FVector& GetWaypoint() const { return WaypointLocation; }
	const FString& GetWaypointName() const { return WaypointName; }

	/** Road-graph route from the player to the waypoint (world space, cm). */
	const TArray<FVector>& GetRoutePoints() const { return RoutePoints; }

	/** Display color of the current objective (waypoint/activity/delivery). */
	FLinearColor GetObjectiveColor() const;

/** Display color of one tour stop (cycles cyan/pink/yellow). */
	static FLinearColor GetTourStopColor(int32 StopIndex);

	/** Beacon/guide surface matching an objective (shared mapping). */
	static ECitixSurface ObjectiveBeaconSurface(bool bJobActive, bool bTourActive, int32 TourStop);

	/** All activity stops + how many are done (upcoming-stop markers). */
	const TArray<FVector>& GetActivityStops() const { return ActivityStops; }

	/** Cycle the waypoint through the nearest few POIs (keyboard friendly, no cursor). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Sandbox")
	void CycleWaypoint();
	void CycleWaypoint(AController* PC);

	// ---- Delivery jobs + money (per-driver records) ----------------------
	UFUNCTION(BlueprintCallable, Category = "Citix|Sandbox")
	bool StartDeliveryJob();
	bool StartDeliveryJob(AController* PC);

	/** Cancel an active delivery (clean state, no reward, no penalty). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Sandbox")
	void CancelDeliveryJob();
	void CancelDeliveryJob(AController* PC);

	ECitixJobState GetJobState() const { return JobState; }
	const FVector& GetJobDestination() const { return JobDestination; }
	float GetJobTimeRemaining() const { return JobTimeRemaining; }
	float GetJobDistanceRemaining() const { return JobDistanceRemaining; }
	int32 GetScore() const { return Score; }
	int32 GetJobsCompleted() const { return JobsCompleted; }
	int32 GetMoney() const { return Money; }

	/** A priority contract is waiting (one per in-game day). */
	bool HasQuestOffer() const { return bQuestOffered; }

	// ---- Activities (per-driver records) ---------------------------------
	/** Contextual prompt available: near a POI, nothing else active. */
	bool CanPromptActivity(FString& OutPrompt) const;
	bool CanPromptActivity(AController* PC, FString& OutPrompt) const;

	/** Start the contextual activity (drive, or a tour near landmarks). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Sandbox")
	bool StartActivity();
	bool StartActivity(AController* PC);

	/** Cancel the active activity (clean state, no reward, no penalty). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Sandbox")
	void CancelActivity();
	void CancelActivity(AController* PC);

	ECitixActivityType GetActivityType() const { return ActivityType; }
	ECitixActivityState GetActivityState() const { return ActivityState; }
	int32 GetActivityStopsDone() const { return ActivityStopsDone; }
	int32 GetActivityStopsTotal() const { return ActivityStops.Num(); }
	float GetActivityTimeRemaining() const { return ActivityTimeRemaining; }
	float GetActivityDistanceRemaining() const { return ActivityDistanceRemaining; }
	const FString& GetActivityName() const { return ActivityName; }
	bool IsOffRoute() const { return bOffRoute; }
	int32 GetActivitiesCompleted() const { return ActivitiesCompleted; }

	// ---- Stunts --------------------------------------------------------
	float GetLastStuntScore() const { return LastStuntScore; }
	float GetTotalAirTime() const { return TotalAirTime; }

	/** Traffic/pedestrian density for the current hour (rush hour vs dead of night). */
	UFUNCTION(BlueprintPure, Category = "Citix|Sandbox")
	float GetDensityScale() const { return AppliedDensity; }

	// ---- Progression ---------------------------------------------------
	/** Activity variant bonuses (rain / rush / night) pay out once unlocked. */
	bool AreVariantBonusesUnlocked() const { return bVariantBonusesUnlocked; }

	// ---- Photo ---------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Citix|Sandbox")
	bool CapturePhoto();
	bool CapturePhoto(AController* PC);

	/** Server-validated photo reward from a submitted view (rank 19). */
	bool ResolvePhoto(AController* PC, const FVector& CameraLocation,
		const FVector& Direction);

	/** Shared scoring core (no side effects besides the reward grant). */
	bool ApplyPhotoScore(FCitixPlayerGame& Game, const FVector& CameraLocation,
		const FVector& Forward, bool bTakeScreenshot);

	float GetPhotoCooldownFraction() const;
	int32 GetPhotosTaken() const { return PhotosTaken; }
	int32 GetLastPhotoScore() const { return LastPhotoScore; }
	const FString& GetLastPhotoBreakdown() const { return LastPhotoBreakdown; }

	// ---- Weapons (per-driver records; legacy readers use focus) ---------
	int32 GetWeaponCount() const { return static_cast<int32>(ECitixWeapon::Count); }
	const FCitixWeaponDef& GetWeaponDef(int32 Index) const { return CitixWeaponCatalogue()[Index]; }
	bool IsWeaponOwned(int32 Index) const;
	bool IsWeaponOwned(AController* PC, int32 Index) const;
	int32 GetCurrentWeapon() const { return CurrentWeapon; }
	int32 GetCurrentWeapon(AController* PC) const;
	int32 GetMagAmmo(int32 Index) const;
	int32 GetMagAmmo(AController* PC, int32 Index) const;
	int32 GetReserveAmmo(int32 Index) const;
	int32 GetReserveAmmo(AController* PC, int32 Index) const;
	bool IsReloading() const { return GetNetMode() == NM_Client ? ClientReloading : ReloadTimer > 0.f; }
	bool IsReloading(AController* PC) const;
	float GetReloadFraction() const;
	float GetReloadFraction(AController* PC) const;

	/** Current spread in degrees (drives the crosshair gap). */
	float GetCurrentSpreadDegrees() const;
	float GetCurrentSpreadDegrees(AController* PC) const;
	float GetCurrentSpreadDegrees(const FCitixPlayerGame& Game) const;

	/** 0 = hip, 1 = full ADS. */
	float GetAdsAmount() const { return AdsAmount; }
	float GetAdsAmount(AController* PC) const;
	/** Sniper scope overlay visible (scoped weapon at near-full ADS). */
	bool IsScopeActive() const;
	bool IsScopeActive(AController* PC) const;
	bool IsScopeActive(const FCitixPlayerGame& Game) const;

	/** Fire one shot (semi) or hold via SetTriggerHeld (auto). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Weapons")
	bool FireWeapon();

	/** Server-authoritative shot from one driver's viewpoint (MP verb RPCs). */
	bool FireWeapon(AController* Shooter);

	/** Shared resolution core: trace, pools, peds, police, players, ammo, feed. */
	bool ResolveShot(AController* Shooter, const FVector& CameraLocation,
		const FVector& Direction, bool bAiming);

	/** Play tracer + sparks locally (server sees its own; remotes via feed). */
	void PlayShotVisual(const FVector& Muzzle, const FVector& Impact,
		ECitixSurface Surface);

	/** Starter pistol for multiplayer joins (single-player stays unarmed). */
	void GrantStarterPistol();
	void GrantStarterPistol(AController* PC);

	bool SetTriggerHeld(bool bHeld);
	bool SetTriggerHeld(AController* PC, bool bHeld);
	bool SetAdsHeld(bool bHeld);
	bool SetAdsHeld(AController* PC, bool bHeld);
	bool IsAdsHeld() const { return bAdsHeld; }

	/** Reload the current weapon (also auto-starts on an empty mag). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Weapons")
	bool StartReload();
	bool StartReload(AController* PC);

	// ---- Shops (static sites; open-state lives on controllers) ---------
	const TArray<FCitixWeaponShop>& GetShops() const { return Shops; }
	float GetShopInteractRadius() const { return ShopInteractRadius; }
	float GetWeaponRange() const { return WeaponRange; }
	/** E prompt near an employee, when nothing else needs E. */
	bool CanPromptShop(FString& OutPrompt) const;
	bool CanPromptShop(const FVector& PlayerLocation, FString& OutPrompt) const;
	/** Buy the selected row (weapon or ammo refill). Validates server-side. */
	bool BuySelected(AController* PC, int32 ShopIdx, int32 Row);
	/** Equip already-owned guns without charging (shop UI helper). */
	bool BuyWeapon(AController* PC, int32 Index);

	// ---- Wanted / police -----------------------------------------------
	int32 GetWantedStars() const { return WantedStars; }

	/** In-scene guide chevrons currently laid (0 when hidden). */
	int32 GetGuideMarkers() const;
	ECitixWantedState GetWantedState() const;
	/** 0..1 escape progress (far from police + quiet = fills). */
	float GetEscapeProgress() const { return EscapeProgress; }
	int32 GetPoliceCount() const;
	/** Per-suspect pursuit status (replicated to the owner's HUD). */
	ECitixWantedState GetWantedStateFor(const FCitixPlayerGame& Game) const;
	float GetHeat() const { return Heat; }
	const TArray<FCitixPoliceUnit>& GetPoliceUnits() const { return PoliceUnits; }
	const TArray<FCitixPoliceOfficerState>& GetOfficers() const { return Officers; }
	/** Nearest active pursuit unit distance, cm (-1 when none hunt). */
	float GetNearestPoliceDistance() const;
	/** Shortest remaining drive for any active unit, cm (-1 when none hunt). */
	float GetNearestPursuitRemaining() const;

	/** Add heat from a gameplay event. Debounced by the caller. */
	void AddHeat(float Amount, const TCHAR* Reason);
	void AddHeat(FCitixPlayerGame& Game, float Amount, const TCHAR* Reason);

	// ---- PvP health / death / respawn (server-authoritative) -------------
	/** Damage one driver (ram or gunshot). Handles death + killfeed. */
	void DamagePlayer(AController* Victim, float Amount, AController* KillerPC,
		const FString& KillerName, const TCHAR* Cause);

	/** Respawn a dead driver on foot within 100 m + blue sky beam. */
	void RespawnPlayer(AController* PC, const FVector& DeathLocation);

	/** Regen + PlayerState health publish (server slow tick). */
	void UpdatePlayerHealth(float DeltaSeconds);

	// ---- Competitive race (rank 7, server-validated) ----------------------
	/** Join (toggle: join while idle, leave while joined/racing as DNF). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Race")
	bool JoinRace(AController* PC);

	/** True while the driver is on the roster (joined/countdown/racing). */
	bool IsRacing(AController* PC) const;

	/** Leave/DNF the race (also on disconnect/death). */
	void LeaveRace(AController* PC, bool bSilent = false);

	/** Publish per-racer snapshots (positions live, results on finish). */
	void PublishRaceSnapshots();

	/** m:ss formatting for race times. */
	static FString FormatRaceTime(float Seconds);

	/** Test hook: teleport one racer onto their next checkpoint. */
	bool TestTeleportRacerToCheckpoint(AController* PC);

	/** Server tick for countdown/progress/results. */
	void UpdateRace(float DeltaSeconds);

	/** Fail a driver's job + activity on death (clean, no reward). */
	void FailObjectivesOnDeath(FCitixPlayerGame& Game);

	// ---- Transient message for the HUD ---------------------------------
	const FString& GetToast() const { return ToastText; }
	float GetToastRemaining() const { return ToastRemaining; }

	// ---- Test hooks (headless verification) -----------------------------
	void TestForceDiscovery(int32 Count);
	void TestForceCaught();
	void TestClearWanted();
	void TestStartleAroundPlayer();
	void TestGiveMoney(int32 Amount);
	bool TestBuyWeapon(int32 Index);
	int32 TestGunHit();
	FString TestWeaponPropState() const;
	int32 TestSpawnOfficer();
	bool TestDamageOfficer();

	// ---- Tuning --------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Sandbox")
	float DiscoveryRadius = 22000.f;

	/** How often discovery / jobs / stunts are evaluated, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Sandbox")
	float UpdateInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Sandbox")
	float JobMinDistance = 45000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Sandbox")
	float JobMaxDistance = 95000.f;

	/** Assume this cruise speed when sizing the job timer, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Sandbox")
	float JobAssumedSpeed = 1500.f;

	/** Air time needed to register a stunt, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Sandbox")
	float StuntMinAirTime = 0.55f;

	/** Score per discovery (granted once). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Progress")
	int32 DiscoveryScore = 50;

	/** Completed activities needed for rain/rush/night bonuses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Progress")
	int32 VariantBonusActivities = 3;

	/** Arrival radius for activity stops, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Activity")
	float ActivityArrivalRadius = 2500.f;

	/** Off-route warning distance from the active route, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Activity")
	float OffRouteDistance = 6000.f;

	/** Hitscan range for guns, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weapons")
	float WeaponRange = 8000.f;

	/** Reload duration, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Weapons")
	float ReloadDuration = 1.4f;

	/** Employee interaction radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Shop")
	float ShopInteractRadius = 400.f;

	/** Max police pursuit units. Hard cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Wanted", meta = (ClampMin = "1", ClampMax = "3"))
	int32 MaxPoliceUnits = 3;

	/** Max dismounted officers on foot at once. Hard cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Wanted", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaxOfficers = 2;

	/** Dismount radius: a car this close to an on-foot player sends an officer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Wanted", meta = (ClampMin = "500"))
	float OfficerDismountRadius = 2500.f;

protected:
	void BuildPointsOfInterest(const TArray<FVector>& LandmarkLocations, float CitySize);
	void BuildShops();
	void RebuildRoute(const FVector& PlayerLocation);
	void RebuildRoute(FCitixPlayerGame& Game, const FVector& PlayerLocation);
	/** Shared Dijkstra helper: node path from a world point to a world point. */
	bool FindPathNodes(const FVector& From, const FVector& To, TArray<int32>& OutPath) const;
	int32 NearestNode(const FVector& Location) const;
	void UpdateJob(FCitixPlayerGame& Game, float DeltaSeconds, const FVector& PlayerLocation);
	void UpdateStunts(FCitixPlayerGame& Game, float DeltaSeconds, const AActor* PlayerPawn);
	void UpdateDensity();
	void ShowToast(const FString& Text, float Duration);

	void UpdateDiscovery(FCitixPlayerGame& Game, AController* PC, const FVector& PlayerLocation);
	void UpdateDistricts(FCitixPlayerGame& Game, const FVector& PlayerLocation);
	void UpdateActivity(FCitixPlayerGame& Game, float DeltaSeconds, const FVector& PlayerLocation);
	void UpdatePhoto(FCitixPlayerGame& Game, float DeltaSeconds);
	void UpdateWeapon(FCitixPlayerGame& Game, AController* PC, float DeltaSeconds,
		const AActor* PlayerPawn);
	void UpdateWeaponCamera(FCitixPlayerGame& Game, float DeltaSeconds, const AActor* PlayerPawn);
	void UpdateQuestDay(FCitixPlayerGame& Game);
	void UpdateKillWatch();
	void UpdateWantedForDrivers(float DeltaSeconds, const TArray<APawn*>& Pawns);
	void UpdatePoliceForDrivers(float DeltaSeconds, const TArray<APawn*>& Pawns);
	void UpdatePoliceMovementForDrivers(float DeltaSeconds, const TArray<APawn*>& Pawns);
	void UpdateOfficersSlow(float DeltaSeconds, const TArray<APawn*>& Pawns);
	void SpawnOfficer(int32 UnitIndex, const FVector& CarLocation);
	bool HasOfficerForUnit(int32 UnitIndex) const;
	void UpdateCrashWatch(FCitixPlayerGame& Game, const APawn* PlayerPawn);

	void AddScore(int32 Points, const TCHAR* Reason);
	void AddMoney(int32 Amount, const TCHAR* Reason);
	void CheckProgressUnlocks();
	void PoliceFireAtPlayer(FCitixPoliceUnit& Unit, APawn* TargetPawn, AController* TargetPC);
	void SpawnPoliceUnit(const FVector& AnchorLocation, const FVector& AnchorVelocity,
		AController* SuspectPC);
public:
	void ReleasePoliceUnits();
	/** Release only one suspect's units (escape/bust/death/disconnect). */
	void ReleasePoliceUnits(AController* SuspectPC);
	/** Units currently hunting one suspect. */
	int32 PoliceUnitsFor(AController* SuspectPC) const;
protected:
	int32 LivingOfficerCount() const;
	void PoliceCaughtPlayer(APawn* CaughtPawn);
	void RefreshBeaconMaterial();
	FVector FindSafeRoadLocation(const FVector& PlayerLocation) const;
	bool IsOutOfCameraView(const FVector& Point) const;
	bool IsOutOfAllCameras(const FVector& Point) const;
	float RiverDistance(const FVector& Location) const;

	/** First connected controller: focus for legacy local readers. */
	AController* FocusPC() const;

	/** Road graph, copied so this actor is independent of the generator. */
	FCitixRoadNetwork Network;

	/** Master plan, copied so points of interest use the real district layout. */
	FCitixCityPlan CityPlan;

	/** Shared city knowledge (discovery flags replicate to everyone). */
	UPROPERTY()
	TArray<FCitixPointOfInterest> POIs;

	/** Destination beacon shared by delivery jobs and activities (one at a time). */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JobBeacon;

	/** In-scene route chevrons (the map you never have to open). */
	UPROPERTY()
	TObjectPtr<ACitixRouteGuide> RouteGuide;

	UPROPERTY()
	TArray<FVector> RoutePoints;

	FVector WaypointLocation = FVector::ZeroVector;
	FString WaypointName;
	bool bHasWaypoint = false;
	int32 WaypointCursor = -1;

	ECitixJobState JobState = ECitixJobState::Idle;
	FVector JobDestination = FVector::ZeroVector;
	float JobTimeRemaining = 0.f;
	float JobDistanceRemaining = 0.f;
	float JobRewardMult = 1.f;
	int32 Score = 0;
	int32 JobsCompleted = 0;
	int32 Money = 0;

	// ---- Daily contract ----
	float LastHour = -1.f;
	bool bQuestOffered = false;
	bool bQuestAccepted = false;

	// ---- Discovery cards ----
	FString DiscoveryCardTitle;
	FString DiscoveryCardSub;
	float DiscoveryCardRemaining = 0.f;
	float DiscoveryCardDuration = 4.f;

	// ---- District cards ----
	ECitixDistrict CurrentDistrict = ECitixDistrict::OuterCity;
	int32 CurrentDistrictIndex = INDEX_NONE;
	TSet<int32> VisitedDistricts;
	FString DistrictCardTitle;
	FString DistrictCardSub;
	float DistrictCardRemaining = 0.f;
	float DistrictCardCooldown = 0.f;

	// ---- Activities ----
	ECitixActivityType ActivityType = ECitixActivityType::None;
	ECitixActivityState ActivityState = ECitixActivityState::Idle;
	FString ActivityName;
	TArray<FVector> ActivityStops;
	TArray<FString> ActivityStopNames;
	int32 ActivityStopsDone = 0;
	float ActivityTimeRemaining = 0.f;
	float ActivityDistanceRemaining = 0.f;
	bool bActivityRushRoute = false;
	int32 ActivitiesCompleted = 0;
	bool bOffRoute = false;
	float OffRouteTimer = 0.f;
	float OffRouteToastCooldown = 0.f;

	// ---- Progression ----
	bool bMinimapDetailUnlocked = false;
	bool bVariantBonusesUnlocked = false;

	// ---- Photo ----
	float PhotoCooldown = 0.f;
	int32 PhotosTaken = 0;
	int32 LastPhotoScore = 0;
	FString LastPhotoBreakdown;

	// ---- Weapons ----
	bool bOwnedWeapons[4] = { false, false, false, false };
	int32 MagAmmo[4] = { 0, 0, 0, 0 };
	int32 ReserveAmmo[4] = { 0, 0, 0, 0 };
	int32 CurrentWeapon = -1;
	float FireCooldownTimer = 0.f;
	float ReloadTimer = 0.f;
	bool bTriggerHeld = false;
	bool bTriggerFresh = false;
	bool bAdsHeld = false;
	float AdsAmount = 0.f;
	float NoWeaponToastCooldown = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<ACitixPulseBolt>> TracerBolts;

	/** Pooled impact sparks shared by gunfire, rams and kills. */
	UPROPERTY()
	TArray<TObjectPtr<ACitixHitSpark>> SparkPool;

	void FireSparks(const FVector& Location, ECitixSurface Surface, int32 Count);

	UPROPERTY()
	TObjectPtr<ACitixWeaponProp> WeaponProp;

	struct FCachedCamera
	{
		TWeakObjectPtr<UCameraComponent> Component;
		float BaseFov = 90.f;
	};
	TArray<FCachedCamera> AdsCameras;
	TWeakObjectPtr<APawn> AdsPawn;

	// ---- Shops (static sites; open-state lives on controllers) ----
	UPROPERTY()
	TArray<FCitixWeaponShop> Shops;

	// ---- Wanted / police ----
	float Heat = 0.f;
	int32 WantedStars = 0;
	float TimeSinceIncident = 1000.f;
	float CrashCooldown = 0.f;
	float LastHullFraction = 1.f;
	float EscapeProgress = 0.f;
	float PoliceSpawnTimer = 0.f;
	int32 SpawnAnchorCursor = 0;
	int32 LastDeathCount = 0;
	/** Most recent gunshot instigator (ped-kill heat attribution window). */
	TWeakObjectPtr<AController> LastShooterPC;
	float LastShotTime = -1000.f;

	// ---- Competitive race (rank 7, server-only roster) ----
	ECitixRaceState RaceState = ECitixRaceState::Idle;
	TArray<FRaceRacer> RaceRacers;
	UPROPERTY()
	TArray<FVector> RaceCheckpoints;
	float RaceCountdown = 0.f;
	float RaceElapsed = 0.f;
	float RaceTimeout = 240.f;
	int32 RaceFinishCount = 0;
	float RaceEndTimer = 0.f;
	int32 LastCountdownSecond = -1;
	UPROPERTY()
	TArray<FCitixPoliceUnit> PoliceUnits;
	UPROPERTY()
	TArray<FCitixPoliceOfficerState> Officers;

	// ---- Per-player replication (stage 2, additive mirroring) ----
	TArray<FCitixPlayerGame> PlayerGames;

	float LastStuntScore = 0.f;
	float TotalAirTime = 0.f;

	FString ToastText;
	float ToastRemaining = 0.f;

	// ---- Client presentation bookkeeping (serials already shown locally) ----
	int32 LastSeenCardSerial = 0;
	int32 LastSeenToastSerial = 0;

	/** Client reload presentation (server timers do not tick locally). */
	bool ClientReloading = false;
	float ClientReloadFraction = 0.f;

	/** Client race checkpoint override (rank 7). */
	FVector RaceCheckpointLocal = FVector::ZeroVector;
	ECitixRaceState RaceStateLocal = ECitixRaceState::Idle;

	// ---- Replication change gate (what MirrorToReplication last published) --
	int32 PublishedPOICount = INDEX_NONE;
	int32 PublishedDiscovered = INDEX_NONE;
	int32 PublishedDistricts = INDEX_NONE;
	int32 PublishedShops = INDEX_NONE;

	float AppliedDensity = 1.f;
	TWeakObjectPtr<class ACitixTrafficSystem> TrafficSystem;
	TWeakObjectPtr<class ACitixPedestrianSystem> PedestrianSystem;

	float UpdateTimer = 0.f;
	FRandomStream Rng;
	bool bConfigured = false;
};
