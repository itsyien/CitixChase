// Copyright Epic Games, Inc. All Rights Reserved.
// Visible gun prop: three dark boxes (body + barrel + grip) that follow the
// on-foot player and slide to the centre of the view while aiming.
// Hidden in vehicles (drive-by fire) and when unarmed. No tick when hidden.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixWeaponProp.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixWeaponProp : public AActor
{
	GENERATED_BODY()

public:
	ACitixWeaponProp();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 0 = pistol .. 3 = sniper. Scales the barrel (sniper runs long). */
	void SetWeaponKind(int32 Kind);

	/** 0 = hip, 1 = full ADS. Slides the prop toward the view centre. */
	void SetAdsAmount(float Amount) { AdsAmount = FMath::Clamp(Amount, 0.f, 1.f); }

	/** Camera pitch so the barrel points where the crosshair looks. */
	void SetAimPitch(float Pitch) { AimPitch = FMath::Clamp(Pitch, -60.f, 60.f); }

	/** World position of the barrel tip (tracers start here, not at the face). */
	FVector GetMuzzleLocation() const;

	/** Follow a pawn (on foot). Pass null to hide (vehicles, unarmed). */
	void FollowPawn(APawn* Pawn);

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BarrelMesh;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> GripMesh;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SightMesh;

	TWeakObjectPtr<APawn> FollowTarget;
	float AdsAmount = 0.f;
	float AimPitch = 0.f;
	int32 WeaponKind = 0;
	float BarrelTipX = 49.f;
};
