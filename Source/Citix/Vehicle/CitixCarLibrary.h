// Copyright Epic Games, Inc. All Rights Reserved.
// Builds low-poly car visuals from primitives, shared by the player vehicle and
// traffic. Everything is defined in "car space": origin on the ground between the
// wheels, +X forward, +Y right, Z up. That makes placement identical whether the
// car sits on the ground (traffic) or under a suspended physics chassis (player).

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"
#include "CitixCarLibrary.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class AActor;

UENUM(BlueprintType)
enum class ECitixCarType : uint8
{
	Sedan			UMETA(DisplayName = "Sedan"),
	Hatchback		UMETA(DisplayName = "Hatchback"),
	Van				UMETA(DisplayName = "Van"),
	Pickup			UMETA(DisplayName = "Pickup"),
	Truck			UMETA(DisplayName = "Box Truck"),
	Bus				UMETA(DisplayName = "Bus"),

	Count			UMETA(Hidden)
};

/** One primitive part of a car. */
struct FCitixCarPartDesc
{
	FVector Center = FVector::ZeroVector;
	FVector Size = FVector(100.f);
	FRotator Rotation = FRotator::ZeroRotator;
	ECitixSurface Surface = ECitixSurface::CarPaint;
	/** Tinted with the car's paint colour. */
	bool bPaint = false;
	bool bWheel = false;
	int32 WheelIndex = INDEX_NONE;
	/** The part that carries collision (the main body). */
	bool bPrimary = false;
	/** Small trim (bumpers, mirrors, individual lights) dropped for distant traffic. */
	bool bHighDetailOnly = false;
};

/** Everything needed to animate / hide / collide a built car. */
struct FCitixCarVisual
{
	TArray<UStaticMeshComponent*> Parts;
	/** 0 = front left, 1 = front right, 2 = rear left, 3 = rear right. */
	TArray<UStaticMeshComponent*> Wheels;
	/** Transforms of the wheels in car space (wheel centre, at radius height). */
	FVector WheelCenters[4] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	UStaticMeshComponent* Body = nullptr;
	/** Indices into Parts that carry the car's paint colour. */
	TArray<int32> PaintedParts;
	float WheelRadius = 33.f;
	float HalfLength = 225.f;
	float HalfWidth = 91.f;
	float Height = 140.f;

	void SetWheelsVisible(bool bVisible) const;
	void SetVisibility(bool bVisible) const;
	int32 GetComponentCount() const { return Parts.Num(); }
};

/**
 * Driving and durability attributes for one car type. This is what makes a hatchback feel
 * different from a bus: mass, power, top speed, hull strength, and how the car behaves when
 * the handbrake is pulled.
 *
 * Suspension springing is NOT listed here: it is derived from the mass in
 * UCitixVehicleMovementComponent::ApplyCarPerformance, so a heavy vehicle automatically
 * gets a stiffer spring and the ride height stays the same for every car.
 */
struct FCitixCarPerformance
{
	/** Kerb weight, kg. Drives acceleration, suspension and how the car is pushed around. */
	float MassKg = 1500.f;

	/** Top speed at full throttle, cm/s. */
	float MaxSpeed = 6600.f;

	/** Engine force at full throttle, kg cm/s^2 (before the speed falloff). */
	float EngineForce = 1350000.f;

	/** Brake force, kg cm/s^2. */
	float BrakeForce = 2600000.f;

	/** Hull points. */
	float MaxHealth = 400.f;

	/** Lateral force per cm/s of tyre slip. Higher = more grip, less slide. */
	float LateralStiffness = 850.f;

	// ---- Drifting character -------------------------------------------
	/** Lateral grip the rear tyres keep while the handbrake is held. Lower = slides more. */
	float HandbrakeGripScale = 0.40f;
	/** Steering authority while drifting (1 = full lock). */
	float DriftSteerAuthority = 1.0f;
	/** Stability assist while drifting, as a fraction of the normal assist. */
	float DriftSlideAssistScale = 0.30f;
	/** Yaw damping while drifting. Low = the car keeps rotating. */
	float DriftAngularDamping = 0.80f;
};

class CITIX_API FCitixCarLibrary
{
public:
	/** Fill the primitive description list for a car type. */
	static void BuildPartDescs(ECitixCarType Type, TArray<FCitixCarPartDesc>& OutParts);

	/**
	 * Create the car's components under Root.
	 * @param bCollision   enable collision on the primary body part (traffic).
	 * @param bIncludeWheels  skip wheels for cheap distant presentation.
	 * @param bHighDetail  include small trim; false builds a cheap traffic variant.
	 */
	static FCitixCarVisual BuildCar(AActor* Owner, USceneComponent* Root, ECitixCarType Type,
		const FLinearColor& PaintColor, bool bCollision, bool bIncludeWheels, bool bHighDetail = true);

	static FLinearColor GetRandomPaintColor(FRandomStream& Rng);
	static const TCHAR* GetTypeName(ECitixCarType Type);
	static void GetFootprint(ECitixCarType Type, float& OutHalfLength, float& OutHalfWidth, float& OutHeight);

	/** Relative likelihood of each type appearing in general traffic. */
	static ECitixCarType GetRandomTrafficType(FRandomStream& Rng);

	/** Driving and durability attributes for a type. */
	static FCitixCarPerformance GetPerformance(ECitixCarType Type);
};
