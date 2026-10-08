// Copyright Epic Games, Inc. All Rights Reserved.
// Low-poly humanoid built from primitives, with a procedural walk cycle.
// Used by the on-foot player pawn and by ambient pedestrians.
//
// Character space: origin between the feet on the ground, +X forward, +Y right, Z up.
// Limbs are single boxes; UpdateWalk rotates them about their joint by offsetting the
// mesh so the joint stays put without needing extra pivot components.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"
#include "CitixCharacterLibrary.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class AActor;

UENUM(BlueprintType)
enum class ECitixCharacterStyle : uint8
{
	Casual			UMETA(DisplayName = "Casual"),
	Business		UMETA(DisplayName = "Business"),
	Worker			UMETA(DisplayName = "Worker"),
	Elder			UMETA(DisplayName = "Elder"),

	Count			UMETA(Hidden)
};

/**
 * A full-body pose for the box rig. Every rotation is about the joint it names, so the
 * rig bends rather than just rotating as one board:
 *   Torso     rotates the whole upper body about the hip pivot (positive = bend forward)
 *   Shoulder  rotates an arm about its shoulder (negative = swing forward)
 *   Hip       rotates a leg about its hip (negative = swing the knee forward/up)
 * Arms inherit the torso bend; legs do not. BodyOffset shifts the whole body, which is
 * how a crouch lowers without needing jointed limbs.
 */
struct FCitixHumanoidPose
{
	FVector BodyOffset = FVector::ZeroVector;
	FRotator Torso = FRotator::ZeroRotator;
	FRotator Head = FRotator::ZeroRotator;
	FRotator ShoulderL = FRotator::ZeroRotator;
	FRotator ShoulderR = FRotator::ZeroRotator;
	FRotator HipL = FRotator::ZeroRotator;
	FRotator HipR = FRotator::ZeroRotator;

	static const FCitixHumanoidPose& Rest()
	{
		static const FCitixHumanoidPose RestPose;
		return RestPose;
	}
};

/** A built humanoid plus the data needed to animate it. */
struct FCitixHumanoidRig
{
	TArray<UStaticMeshComponent*> Parts;
	UStaticMeshComponent* Torso = nullptr;
	UStaticMeshComponent* Head = nullptr;
	UStaticMeshComponent* Hair = nullptr;
	UStaticMeshComponent* ArmL = nullptr;
	UStaticMeshComponent* ArmR = nullptr;
	UStaticMeshComponent* LegL = nullptr;
	UStaticMeshComponent* LegR = nullptr;

	FVector TorsoRest = FVector::ZeroVector;
	FVector HeadRest = FVector::ZeroVector;
	FVector HairRest = FVector::ZeroVector;

	FVector ShoulderL = FVector::ZeroVector;
	FVector ShoulderR = FVector::ZeroVector;
	FVector HipL = FVector::ZeroVector;
	FVector HipR = FVector::ZeroVector;

	/** Rest pose per part, so a ragdolled body can be restored when the actor is reused. */
	TArray<FTransform> RestTransforms;

	float ArmHalfLength = 31.f;
	float LegHalfLength = 44.f;

	/** Total standing height (top of head), cm. */
	float Height = 180.f;

	void UpdateWalk(float PhaseRadians, float SwingDegrees, float BobCm);

	/** Apply an articulated pose (used by the get-up animation). */
	void ApplyPose(const FCitixHumanoidPose& Pose);

	void SetVisibility(bool bVisible) const;
	int32 GetComponentCount() const { return Parts.Num(); }
};

class CITIX_API FCitixCharacterLibrary
{
public:
	static FCitixHumanoidRig BuildCharacter(AActor* Owner, USceneComponent* Root,
		ECitixCharacterStyle Style, FRandomStream& Rng);

	static ECitixCharacterStyle GetRandomStyle(FRandomStream& Rng);

	/** Camera height for an on-foot pawn, cm. */
	static constexpr float EyeHeight = 165.f;

	/** Collision capsule for a humanoid. */
	static constexpr float CapsuleRadius = 34.f;
	static constexpr float CapsuleHalfHeight = 88.f;
};
