// Copyright Epic Games, Inc. All Rights Reserved.
// Driving / on-foot controller.
//
// Owns every mode transition and the sandbox verbs so the pawns stay focused on their
// own behaviour:
//   F  enter / exit vehicle (including taking over parked and traffic cars)
//   M  full map            G  cycle navigation waypoint      J  start a delivery job
//   E  start contextual activity    X  cancel activity/job/waypoint
//   Left mouse / RT  fire pulse tool (non-graphic)    ENTER  capture photo
//   V  cycle the player's car type                            P  photo mode
//   [ / ]  scrub time of day

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/PlayerController.h"
#include "CitixDrivingPlayerController.generated.h"

class ACitixVehiclePawn;
class ACitixOnFootPawn;
class ACitixSandboxDirector;
class ACameraActor;
class UInputMappingContext;
class UInputAction;
class UCitixChaseLobbyWidget;
struct FInputActionValue;

UCLASS()
class CITIX_API ACitixDrivingPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACitixDrivingPlayerController();

	void ToggleSettingsMenu();
 bool IsSettingsMenuOpen() const { return SettingsMenu != nullptr; }
 UPROPERTY() TObjectPtr<class UCitixSettingsWidget> SettingsMenu;
 int32 GraphicsProbeStep=-1; float GraphicsProbeWait=0.f; float GraphicsProbeRoundSeconds=0.f; bool bGraphicsProbePassed=true;
 virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	// ---- Modes ---------------------------------------------------------

	/** Leave the current vehicle and continue on foot. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	bool RequestExitVehicle();

	/** Enter the nearest free vehicle (the player's own car, or any parked/traffic car). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	bool RequestEnterVehicle();

	/** Chase-only ejection that bypasses the normal low-speed exit gate. */
	bool ForceChaseEjection();
	bool FindSafeChaseExit(const class ACitixVehiclePawn* Vehicle, FTransform& OutExit) const;
	void ShowChaseLobby();
 AActor* FindChaseEntryCandidate(float Range) const;
 UFUNCTION(Client, Reliable) void ClientChaseMessage(const FString& Message);
 FString ChaseMessage;
 float ChaseMessageUntil = 0.f;
 float LastLocalChaseShot = -10.f;
 float ChaseHitConfirmedUntil = 0.f;
	void HideChaseLobby();
	void HostChaseMatch();
	void JoinChaseMatch(const FString& Address);

	UFUNCTION(Server, Reliable)
	void ServerRequestExitVehicle();

	UFUNCTION(Server, Reliable)
	void ServerRequestEnterVehicle();

	UFUNCTION(Server, Reliable)
	void ServerChaseInteract();

	UFUNCTION(Server, Reliable)
	void ServerCancelChaseInteract();

	UFUNCTION(Server, Reliable)
	void ServerReportChaseCityIdentity(int32 Seed, int32 LayoutHash);

	// ---- Shared-world verbs (stage 2): clients send, server executes --------
	UFUNCTION(Server, Reliable)
	void ServerCycleWaypoint();

	UFUNCTION(Server, Reliable)
	void ServerStartDeliveryJob();

	UFUNCTION(Server, Reliable)
	void ServerStartActivity();

	UFUNCTION(Server, Reliable)
	void ServerCancelSandbox();

	// ---- Shared-arsenal shooting (MP): clients send aim, server resolves ----
	UFUNCTION(Server, Unreliable)
	void ServerFireWeapon(FVector_NetQuantize CameraLocation,
		FVector_NetQuantizeNormal Direction, bool bAiming);

	UFUNCTION(Server, Reliable)
	void ServerReloadWeapon();
 UFUNCTION(Server,Reliable) void ServerRunnerSmoke();
 UFUNCTION(Server,Reliable) void ServerChaserIce();

	/** Buy a shop row (server validates proximity/funds/ownership). */
	UFUNCTION(Server, Reliable)
	void ServerBuyShopRow(int32 ShopIdx, int32 Row);

	/** Join (or leave) the competitive race. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Race")
	void JoinRace();

	/** Submit a photo view for server-validated scoring (rank 19). */
	UFUNCTION(Server, Reliable)
	void ServerCapturePhoto(FVector_NetQuantize ViewLocation, FVector_NetQuantizeNormal ViewDirection);

	/** Join (or leave) the race, server side. */
	UFUNCTION(Server, Reliable)
	void ServerJoinRace();

	/** Fire one client shot: RPC + immediate local tracer (no double-play). */
	void SendFireShot();

	/** Stage-1 gate: shared-world verbs stay single-player until stage 2. */
	bool NetVerbAllowed() const;

	/** Combat/shop verbs stay local-only until stage 3 (server sim, no replica). */
	bool CombatVerbAllowed() const;

	/** Client-only world copy: city, clock and weather for rendering + collision. */
	void EnsureLocalWorld();

	/** Multiplayer census line (`-CitixNetLog` test hook, runs everywhere). */
	void LogNetState();

	/** Test tag (`-CitixNetTag=X`) so shared log dirs stay attributable. */
	FString NetTag;

	UFUNCTION(BlueprintPure, Category = "Citix|Player")
	bool IsOnFoot() const;

	/** True when the world runs the chase game mode (server or client). */
	UFUNCTION(BlueprintPure, Category = "Citix|Player")
	bool IsChaseMode() const;

	UFUNCTION(BlueprintPure, Category = "Citix|Player")
	bool IsMapOpen() const { return bMapOpen; }

	UFUNCTION(BlueprintPure, Category = "Citix|Player")
	bool IsPhotoMode() const { return bPhotoMode; }

	/** Shop UI state: browsing is local per driver (server validates buys). */
	UFUNCTION(BlueprintPure, Category = "Citix|Shop")
	bool IsShopOpen() const { return bShopOpenLocal; }

	UFUNCTION(BlueprintPure, Category = "Citix|Shop")
	int32 GetShopSelected() const { return ShopSelectedLocal; }

	UFUNCTION(BlueprintPure, Category = "Citix|Shop")
	int32 GetShopIndex() const { return ShopIndexLocal; }

	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void ToggleMap();

	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void CycleWaypoint();

	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void StartDeliveryJob();

	/** Start the contextual activity (drive route or landmark tour). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void StartActivity();

	/** Cancel the activity, delivery or waypoint - whichever is active. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void CancelActivity();

	/** Fire the current gun (works in the car and on foot). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void FireWeapon();

	/** Aim down the sights (hold). Sniper shows the scope at full ADS. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void SetAdsHeld(bool bHeld);

	/** Reload the current gun. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void ReloadWeapon();

	/** Talk to the employee / buy the selected shop row. */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void InteractShop();

	/** Score the current photo-mode view (photo mode only). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void CapturePhoto();

	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void TogglePhotoMode();

	/** Swap the player's car to the next type (vehicle variety). */
	UFUNCTION(BlueprintCallable, Category = "Citix|Player")
	void CyclePlayerCar();

	/** Distance within which the interact prompt is available, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Player")
	float EnterVehicleRange = 420.f;

	/** Hours of in-game time scrubbed per real second while [ or ] is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Time")
	float TimeScrubHoursPerSecond = 6.f;

	// ---- Photo mode tuning --------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Photo")
	float PhotoMoveSpeed = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Photo")
	float PhotoLookSpeed = 1.4f;

protected:
	void HandleTimeBackStart(const FInputActionValue& Value);
	void HandleTimeBackStop(const FInputActionValue& Value);
	void HandleTimeForwardStart(const FInputActionValue& Value);
	void HandleTimeForwardStop(const FInputActionValue& Value);

	void HandleMapToggle(const FInputActionValue& Value);
	void HandleWaypoint(const FInputActionValue& Value);
	void HandleJob(const FInputActionValue& Value);
	void HandlePhotoToggle(const FInputActionValue& Value);
	void HandleCycleCar(const FInputActionValue& Value);
	void HandleInteract(const FInputActionValue& Value);
	void HandleCancel(const FInputActionValue& Value);
	void HandleFire(const FInputActionValue& Value);
	void HandleFireReleased(const FInputActionValue& Value);
	void HandleAds(const FInputActionValue& Value);
	void HandleAdsReleased(const FInputActionValue& Value);
	void HandleReload(const FInputActionValue& Value);
	void HandleRace(const FInputActionValue& Value);
	void HandleShopRow(int32 Row);
	void HandleShopRow1(const FInputActionValue& Value);
	void HandleShopRow2(const FInputActionValue& Value);
	void HandleShopRow3(const FInputActionValue& Value);
	void HandleShopRow4(const FInputActionValue& Value);
	void HandleShopRow5(const FInputActionValue& Value);
	void HandleCapturePhoto(const FInputActionValue& Value);

	/** Shop UI helpers: mouse look + click select, movement locked. */
	void LockShopInput();
	void UnlockShopInput();
	void CloseShopUI();
	void OpenShopUI(int32 ShopIndex);
	void BuyShopSelection();
	void ShopClickSelect();
	void HandlePhotoMove(const FInputActionValue& Value);
	void HandlePhotoUp(const FInputActionValue& Value);
	void HandlePhotoLook(const FInputActionValue& Value);

	void AddAlwaysOnContexts();
	/** Removes sandbox-only mappings as soon as replicated chase state arrives. */
	void RefreshModeInputContexts();
	void UpdatePhotoCamera(float DeltaSeconds);

	/** The player's own car, spawned on demand if it does not exist yet. */
	ACitixVehiclePawn* FindOrSpawnPlayerVehicle();

	ACitixSandboxDirector* GetSandbox() const;

	UPROPERTY()
	TObjectPtr<ACitixOnFootPawn> OnFootPawn;

	UPROPERTY()
	TObjectPtr<ACameraActor> PhotoCamera;

	UPROPERTY()
	TObjectPtr<APawn> SavedPawn;

	UPROPERTY()
	TObjectPtr<UCitixChaseLobbyWidget> ChaseLobby;
	UPROPERTY() TObjectPtr<class ACitixRouteGuide> ChaseRoute;
	TWeakObjectPtr<class ACitixCityGenerator> ChaseRouteCity;
	float ChaseRouteUpdateIn = 0.f;
	void UpdateChaseRoute(float DeltaSeconds);
	bool bChaseLobbyDismissed = false;
	bool bChaseInputContextsSuppressed = false;

	/** Always-on time-of-day controls ( [ and ] ). */
	UPROPERTY() TObjectPtr<UInputMappingContext> TimeContext;
	UPROPERTY() TObjectPtr<UInputAction> TimeBackAction;
	UPROPERTY() TObjectPtr<UInputAction> TimeForwardAction;

	/** Sandbox verbs (map, waypoint, job, car swap, photo). */
	UPROPERTY() TObjectPtr<UInputMappingContext> SandboxContext;
	UPROPERTY() TObjectPtr<UInputAction> MapAction;
	UPROPERTY() TObjectPtr<UInputAction> WaypointAction;
	UPROPERTY() TObjectPtr<UInputAction> JobAction;
	UPROPERTY() TObjectPtr<UInputAction> CycleCarAction;
	UPROPERTY() TObjectPtr<UInputAction> PhotoAction;
	UPROPERTY() TObjectPtr<UInputAction> InteractAction;
	UPROPERTY() TObjectPtr<UInputAction> CancelAction;
	UPROPERTY() TObjectPtr<UInputAction> FireAction;
	UPROPERTY() TObjectPtr<UInputAction> AdsAction;
	UPROPERTY() TObjectPtr<UInputAction> ReloadAction;
	UPROPERTY() TObjectPtr<UInputAction> RaceAction;
	UPROPERTY() TObjectPtr<UInputAction> ShopRow1Action;
	UPROPERTY() TObjectPtr<UInputAction> ShopRow2Action;
	UPROPERTY() TObjectPtr<UInputAction> ShopRow3Action;
	UPROPERTY() TObjectPtr<UInputAction> ShopRow4Action;
	UPROPERTY() TObjectPtr<UInputAction> ShopRow5Action;
	UPROPERTY() TObjectPtr<UInputAction> CapturePhotoAction;
	UPROPERTY() TObjectPtr<UInputAction> PhotoMoveAction;
	UPROPERTY() TObjectPtr<UInputAction> PhotoUpAction;
	UPROPERTY() TObjectPtr<UInputAction> PhotoLookAction;

protected:
	float TimeScrubDirection = 0.f;
	bool bMapOpen = false;
	bool bPhotoMode = false;
	bool bShopOpenLocal = false;
	int32 ShopIndexLocal = INDEX_NONE;
	int32 ShopSelectedLocal = 0;

	/** Client presentation refresh accumulator (route/beacon/guide at 4 Hz). */
	float SandboxMirrorTimer = 0.f;

	/** Client trigger latch (auto resend) + ADS feel (fed per shot). */
	bool bTriggerHeldLocal = false;
	bool bAdsHeldLocal = false;
	float TriggerResendTimer = 0.f;
 int32 SmokeProbeStep=0;
 float SmokeProbeWait=0.f;

	/** Last played shot serial per driver (remote tracer feed). */
	TMap<TWeakObjectPtr<APlayerState>, int32> LastShotSerial;

	/** Client ADS zoom bookkeeping (base FOV per pawn). */
	TMap<TWeakObjectPtr<APawn>, float> AdsCameraBaseFov;

	/** Rank 9: city identity check accumulator + mismatch latch. */
	float CitySeedCheckTimer = 0.f;
	bool bCitySeedMismatch = false;
	bool bReportedChaseCityIdentity = false;

	FVector2D PhotoMoveInput = FVector2D::ZeroVector;
	FVector2D PhotoLookInput = FVector2D::ZeroVector;
	float PhotoUpInput = 0.f;
};
