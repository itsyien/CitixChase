#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CitixChasePlayerState.generated.h"
UENUM(BlueprintType) enum class ECitixChaseRole : uint8 { Runner, Chaser };
UENUM(BlueprintType) enum class ECitixChaseInteraction : uint8 { None, Relay, Capture, Breakaway, Escape };
UCLASS() class CITIX_API ACitixChasePlayerState : public APlayerState
{
	GENERATED_BODY()
public:
 UPROPERTY(Replicated) int32 RapidBrakeCharges=0;
 UPROPERTY(Replicated) float NextRapidBrakeAt=0.f;
 UPROPERTY(Replicated) float RapidBrakeUntil=0.f;
 UPROPERTY(Replicated) int32 IceCharges=0;
 UPROPERTY(Replicated) float NextIceAt=0.f;
 UPROPERTY(Replicated) float FrozenUntil=0.f;
 UPROPERTY(Replicated) float FrozenSpeedLimit=0.f;
 UPROPERTY(Replicated) float LastIceAt=0.f;
 UPROPERTY(Replicated) bool bLastIceHit=false;
	UPROPERTY(Replicated) float StaggerUntil = 0.f;
 UPROPERTY(Replicated) float GateSlowStartedAt = 0.f;
 UPROPERTY(Replicated) float GateSlowUntil = 0.f;
 UPROPERTY(Replicated) int32 SmokeCharges=1;
 UPROPERTY(Replicated) float NextSmokeAt=0.f;
 UPROPERTY(Replicated) float SmokeEmittingUntil=0.f;
 UPROPERTY(Replicated) float ReplacementReadyAt = 0.f;
 UPROPERTY(Replicated) int32 PistolHits = 0;
 UPROPERTY(Replicated) int32 Ammo = 15;
 UPROPERTY(Replicated) float NextAmmoAt = 0.f;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated, BlueprintReadOnly) ECitixChaseRole ChaseRole = ECitixChaseRole::Runner;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bReady = false;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bCityIdentityValid = false;
	UPROPERTY(Replicated, BlueprintReadOnly) float CharacterHealth = 100.f;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bReplacementUsed = false;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 RunnerCarHits = 0;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bInteractionActive = false;
	UPROPERTY(Replicated, BlueprintReadOnly) float InteractionSecondsRemaining = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly) ECitixChaseInteraction InteractionType = ECitixChaseInteraction::None;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 RoundsWon = 0;
};
