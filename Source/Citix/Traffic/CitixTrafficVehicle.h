// Copyright Epic Games, Inc. All Rights Reserved.
// Lightweight kinematic traffic vehicle. No physics, no tick, no AI controller:
// the traffic system writes its transform directly, which keeps many background
// cars cheap. The body is built from the shared low-poly car library.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vehicle/CitixCarLibrary.h"
#include "CitixTrafficVehicle.generated.h"

class USceneComponent;
USTRUCT() struct FCitixTrafficSnapshot {
 GENERATED_BODY()
 UPROPERTY() FVector Location = FVector::ZeroVector;
 UPROPERTY() FRotator Rotation = FRotator::ZeroRotator;
 UPROPERTY() FVector Velocity = FVector::ZeroVector;
 UPROPERTY() float Timestamp = 0.f;
 UPROPERTY() float SpeedKmh = 0.f;
 UPROPERTY() int32 Generation = 0;
 // Pose, lifecycle and visibility must arrive as one replicated snapshot.
 UPROPERTY() bool bVisible = false;
};

UCLASS(NotBlueprintable)
class CITIX_API ACitixTrafficVehicle : public AActor
{
	GENERATED_BODY()

public:
	ACitixTrafficVehicle();

	virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 UFUNCTION() void OnRep_Motion();
 void PublishMotion(float SpeedKmh);
 static FCitixTrafficSnapshot SampleMotion(const TArray<FCitixTrafficSnapshot>& Samples,float Time);
 float GetAuthoritativeSpeedKmh() const { return Motion.SpeedKmh; }
 UPROPERTY(ReplicatedUsing=OnRep_Motion) FCitixTrafficSnapshot Motion;
 TArray<FCitixTrafficSnapshot> History;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Build the car. Must be called once after spawning. */
	void InitializeVehicle(ECitixCarType InType, const FLinearColor& Color, int32 Seed);

	void SetVehicleVisible(bool bVisible);

	/** Wheels are dropped beyond a distance to save components. */
	void SetWheelsVisible(bool bVisible);

	ECitixCarType GetCarType() const { return CarType; }

	/** Paint colour this car was built with (so the player can adopt it). */
	const FLinearColor& GetPaintColor() const { return PaintColor; }

	UFUNCTION()
	void OnRep_Appearance();

	UFUNCTION()
	void OnRep_Visible();

	/** Mark the car as taken over by the player. */
	void SetTakenByPlayer(bool bTaken) { bTakenByPlayer = bTaken; }
	bool IsTakenByPlayer() const { return bTakenByPlayer; }
	int32 GetComponentCount() const { return CarVisual.GetComponentCount(); }

protected:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	ECitixCarType CarType = ECitixCarType::Sedan;

	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	FLinearColor PaintColor = FLinearColor(0.5f, 0.5f, 0.5f);

	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	int32 BuildSeed = 0;

	bool bVisibleNet = false;
	bool bTakenByPlayer = false;
	FCitixCarVisual CarVisual;
	bool bInitialized = false;
};
