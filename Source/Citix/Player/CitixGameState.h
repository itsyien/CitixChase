// Copyright Epic Games, Inc. All Rights Reserved.
// Shared city knowledge: which points of interest are discovered, which
// districts have been visited. Personal progress lives in the player state.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "CitixGameState.generated.h"

UCLASS(Blueprintable)
class CITIX_API ACitixGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Index-aligned with the sandbox director's POI list (shared discovery). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<FCitixPointOfInterest> SharedPOIs;

	/** District indices visited by anyone (shared entry cards). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<int32> VisitedDistricts;

	/** Weapon shop sites (static: locations + names for maps and prompts). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<FVector> ShopLocations;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|Sandbox")
	TArray<FString> ShopNames;

	/** Authoritative city identity (rank 9): seed the server built from. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Citix|World")
	int32 CitySeed = 0;
};
