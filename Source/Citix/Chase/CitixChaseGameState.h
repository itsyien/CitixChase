#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CitixChaseGameState.generated.h"

UENUM(BlueprintType)
enum class ECitixChasePhase : uint8 { Waiting, Countdown, Pursuit, RoundResults, MatchResults };

UCLASS()
class CITIX_API ACitixChaseGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	UPROPERTY(Replicated) FString RoomDisplayName;
	UPROPERTY(Replicated) int32 ImpactSerial = 0;
	UPROPERTY(Replicated) TArray<FVector> BreakawayLocations;
	UPROPERTY(Replicated) float BreakawayUntil = 0.f;
 UPROPERTY(Replicated) float BreakawayReadyAt = 0.f;
 UPROPERTY(Replicated) TArray<float> BreakawayWidths;
 UPROPERTY(Replicated) TArray<float> BreakawayYaws;
	UFUNCTION(NetMulticast, Unreliable) void MulticastPistolShot(FVector_NetQuantize From, FVector_NetQuantize To, bool bRunnerHit, APlayerState* Shooter);
 static void PlayShotFeedback(UWorld* World, const FVector& Muzzle, bool Dry);
	UFUNCTION(NetMulticast, Reliable) void MulticastPixelExplosion(FVector_NetQuantize Location);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated, BlueprintReadOnly) ECitixChasePhase Phase = ECitixChasePhase::Waiting;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 RoundNumber = 0;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 CitySeed = 0;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 CityConfigHash = 0;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FVector> LayoutRelayLocations;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FVector> LayoutExitLocations;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FVector> LayoutSpawnLocations;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FVector> LayoutReplacementLocations;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FVector> ActiveReplacementLocations;
	UPROPERTY(Replicated, BlueprintReadOnly) float PhaseSecondsRemaining = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 CompletedRelays = 0;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<bool> ActivatedRelays;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bExitsUnlocked = false;
	UPROPERTY(Replicated, BlueprintReadOnly) FString StatusText;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bRunnerRevealed = false;
	UPROPERTY(Replicated, BlueprintReadOnly) float RevealSecondsRemaining = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly) float NextRevealSecondsRemaining = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bInteractionActive = false;
	UPROPERTY(Replicated, BlueprintReadOnly) float InteractionSecondsRemaining = 0.f;
};
