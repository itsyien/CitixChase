// Copyright Epic Games, Inc. All Rights Reserved.
// Stationed shop employee: a low-poly character behind the counter of a real
// sidewalk storefront that never simulates, never ticks, never ragdolls.
// The shop body and counter are solid (cars stop, bullets hit); trim, glass,
// awning and sign are visual only. Local space: the keeper stands at the origin
// facing +X (the road); the shop is built behind at -X.

#pragma once

#include "CoreMinimal.h"
#include "Core/CitixTypes.h"
#include "GameFramework/Actor.h"
#include "CitixShopkeeper.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixShopkeeper : public AActor
{
	GENERATED_BODY()

public:
	ACitixShopkeeper();

	virtual void BeginPlay() override;

	/** Build the character + the storefront. Call once after spawning (already rotated). */
	void InitializeKeeper(int32 Seed);

private:
	UStaticMeshComponent* AddPart(const TCHAR* Name, const FVector& Offset,
		const FVector& Size, ECitixSurface Surface, bool bCollide);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SignMesh;
};
