#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CitixChaseGameMode.generated.h"
class ACitixChaseGameState;
class ACitixChasePlayerState;
class ACitixVehiclePawn;
class ACitixDrivingPlayerController;
class ACitixDestinationBeacon;
class APlayerStart;
struct FCitixRoadNetwork;
struct FCitixChaseHold
{
	int32 RelayIndex = INDEX_NONE;
 int32 ExitIndex = INDEX_NONE;
 float StartedAt=0.f;
	float SecondsRemaining = 0.f;
	bool bCapture = false;
	int32 BreakawayIndex = INDEX_NONE;
};
UCLASS() class CITIX_API ACitixChaseGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACitixChaseGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	void CompleteRelay(AController* Runner);
	void BeginInteraction(AController* InteractingController);
	void CancelInteraction(AController* InteractingController);
	void TryRam(ACitixVehiclePawn* Source, ACitixVehiclePawn* Target, float ClosingSpeedKmh);
	void UseRunnerSmoke(APlayerController* Player);
 void UseChaserIce(APlayerController* Player);
 void UseChaserRapidBrake(APlayerController* Player);
	void TryRunOver(AActor* Car, const FVector& From, const FVector& To, float SpeedKmh);
	bool CanClaimReplacement(const AController* Controller, const ACitixVehiclePawn* Vehicle) const;
	bool IsReplacementVehicle(const ACitixVehiclePawn* Vehicle) const;
	void ClaimReplacement(AController* Controller, ACitixVehiclePawn* Vehicle);
	void FinishRound(bool bRunnerWon, const FString& Reason);
	bool CanAcceptDriveInput() const;
	bool SelectLobbyMap(APlayerController* Host, bool Hillside);
	void FireChasePistol(APlayerController* Shooter, const FVector& Origin, const FVector& Direction);
private:
	void InitializeBreakaways();
	bool IsAtBreakaway(AController* Controller, int32 Index) const;
	void ActivateBreakaway(int32 Index);
	UPROPERTY() TArray<TObjectPtr<ACitixDestinationBeacon>> BreakawayBeacons;
	float LastPistolShot = -10.f;
	void StartRound();
	ACitixChaseGameState* ChaseState() const;
	float PhaseTime = 0.f;
	int32 RoundNumber = 0;
	TArray<FVector> RelayLocations;
	TArray<FVector> ValidatedRelaySites;
	int32 RelaySelectionSeed = 0;
	bool SelectMatchRelays();
	TArray<FVector> ExitLocations;
	TArray<FVector> SpawnLocations;
	TArray<FVector> ReplacementLocations;
	UPROPERTY() TArray<TObjectPtr<ACitixDestinationBeacon>> RelayBeacons;
	UPROPERTY() TArray<TObjectPtr<ACitixDestinationBeacon>> ExitBeacons;
	UPROPERTY() TArray<TObjectPtr<ACitixDestinationBeacon>> ReplacementBeacons;
	UPROPERTY() TArray<TObjectPtr<ACitixVehiclePawn>> ReplacementCars;
	TArray<bool> ActivatedRelays;
	TMap<TWeakObjectPtr<AController>, FCitixChaseHold> ActiveHolds;
 TMap<TWeakObjectPtr<AController>,TSet<int32>> GateOverlaps;
	TMap<TWeakObjectPtr<ACitixDrivingPlayerController>, float> PendingEjections;
	TSet<TObjectPtr<AController>> RematchReady;
	TSet<TObjectPtr<AController>> LobbyReady;
	bool IsNearRelay(const AController* Controller, int32& OutRelay) const;
	bool IsAtRelay(const AController* Controller, int32 RelayIndex) const;
	void CompleteRelayAt(AController* Runner, int32 Relay);
	void ClearHold(AController* Controller, bool bInterrupted);
	void UpdateInteractionPresentation();
	bool IsCaptureRange(const AController* Controller) const;
	bool IsAtExit(const AController* Controller,int32 ExitIndex=INDEX_NONE) const;
	float LastRamTime = -100.f;
	TWeakObjectPtr<ACitixVehiclePawn> LastRamSource;
	TWeakObjectPtr<ACitixVehiclePawn> LastRamTarget;
	/** Latched true once the last ram pair has moved apart again (see Tick). */
	bool bRamSeparated = true;
	float CaptureProtectionUntil = 0.f;
	void UpdateRunnerReveal();
	void SpawnReplacementCars();
	void SnapChaseLocationsToRoads(const FCitixRoadNetwork& Roads);
	void PlacePlayerAtRoundSpawn(AController* Controller) const;
	bool ResetPlayerForRound(AController* Controller, const FVector& SpawnLocation);
	void FinishMatch();
	void CancelMatch(const FString& Reason);

	/** Spawn the lobby player starts (round spawns are applied separately). */
	void EnsurePlayerStarts();

	/** Headless end-to-end chase test (`-CitixChaseTest` on the host). */
	void ChaseTestTick();

	int32 ChaseTestPhase = 0;
	float ChaseTestTimer = 0.f;
 TWeakObjectPtr<class ACitixTrafficVehicle> GateProbeTraffic;
 float GateProbeReady=0.f;
 bool bPistolReentryChecked = false;
 bool bStationTestPassed = false;
 bool bSpeedProbeStarted = false;
 float CountdownProbeStartedAt=-1.f;
 bool bCountdownProbePassed=true;
 bool bCountdownProbeWritten=false;
 int32 SpeedProbeBody = 0;
 FTransform SpeedProbeStarts[2];
 float SpeedProbeMax[2] = {0,0};
	int32 ChaseTestRams = 0;
	int32 ChaseTestAttempts = 0;
	bool bChaseTest = false;
	UPROPERTY() TArray<TObjectPtr<class ACitixRoundLifecycleProbe>> LifecycleProbes;
	int32 LifecycleCompletedMatches=0;
	float LifecycleDeadline=0.f;
	bool bLifecycleReceiptWritten=false;
	TArray<FVector> LifecycleMatchSites;
	/** -CitixChaseTestCapture: verify the capture win instead of the second wreck. */
	bool bChaseTestCapture = false;
	/** -CitixChaseTestTimeout: verify the pursuit timer awards the chaser round. */
	bool bChaseTestTimeout = false;
	/** -CitixChaseTestSecondExit: complete round one through the alternate exit. */
	bool bChaseTestSecondExit = false;
	/** -CitixChaseTestRelayHold: verify interrupted and completed on-foot relay holds. */
	bool bChaseTestRelayHold = false;

	/** Sticky start claim per controller so a short drop does not reshuffle slots. */
	TArray<TPair<TWeakObjectPtr<AController>, int32>> PlayerStartClaims;

	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> GeneratedPlayerStarts;
};
