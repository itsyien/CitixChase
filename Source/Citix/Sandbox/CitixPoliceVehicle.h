// Copyright Epic Games, Inc. All Rights Reserved.
// Police pursuit vehicle: a traffic car with a flashing lightbar.
// No point lights, no new materials, no per-instance cost beyond two tiny boxes:
// the bar halves simply toggle visibility on a timer (1-3 actors max).

#pragma once

#include "CoreMinimal.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "CitixPoliceVehicle.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class CITIX_API ACitixPoliceVehicle : public ACitixTrafficVehicle
{
	GENERATED_BODY()

public:
	ACitixPoliceVehicle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Build the lightbar. Call once after InitializeVehicle. */
	void InitializePolice();

	void SetFlashEnabled(bool bEnabled);

protected:
	UFUNCTION()
	void OnRep_FlashEnabled();

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> LightBarRed;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> LightBarBlue;

	/** Replicated: a disabled unit goes dark on every screen. */
	UPROPERTY(ReplicatedUsing = OnRep_FlashEnabled)
	bool bFlashEnabled = true;

	float FlashTimer = 0.f;
	bool bFlashState = false;
	bool bPoliceInitialized = false;
};
