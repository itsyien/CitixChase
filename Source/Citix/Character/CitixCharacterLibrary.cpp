// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CitixCharacterLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// Body proportions (cm), feet on the ground.
	constexpr float HipZ = 88.f;
	constexpr float TorsoCenterZ = 122.f;
	constexpr float TorsoHeight = 66.f;
	constexpr float ShoulderZ = 150.f;
	constexpr float ShoulderY = 21.f;
	constexpr float HeadCenterZ = 170.f;
	constexpr float ArmLength = 62.f;
	constexpr float LegLength = 88.f;

	struct FStylePalette
	{
		ECitixSurface Top;
		ECitixSurface Bottom;
	};

	FStylePalette GetPalette(ECitixCharacterStyle Style, FRandomStream& Rng)
	{
		switch (Style)
		{
		case ECitixCharacterStyle::Business:
			return { ECitixSurface::ClothD, (Rng.FRand() < 0.5f) ? ECitixSurface::ClothB : ECitixSurface::Trim };
		case ECitixCharacterStyle::Worker:
			return { (Rng.FRand() < 0.5f) ? ECitixSurface::ClothC : ECitixSurface::ClothA, ECitixSurface::ClothB };
		case ECitixCharacterStyle::Elder:
			return { ECitixSurface::ClothD, ECitixSurface::ClothC };
		default:
		{
			const ECitixSurface Tops[] = { ECitixSurface::ClothA, ECitixSurface::ClothB, ECitixSurface::ClothC, ECitixSurface::ClothD };
			const ECitixSurface Bottoms[] = { ECitixSurface::ClothB, ECitixSurface::Trim, ECitixSurface::ClothC };
			return { Tops[Rng.RandRange(0, 3)], Bottoms[Rng.RandRange(0, 2)] };
		}
		}
	}
}

FCitixHumanoidRig FCitixCharacterLibrary::BuildCharacter(AActor* Owner, USceneComponent* Root,
	ECitixCharacterStyle Style, FRandomStream& Rng)
{
	FCitixHumanoidRig Rig;
	if (!Owner || !Root)
	{
		return Rig;
	}

	const FStylePalette Palette = GetPalette(Style, Rng);
	UStaticMesh* Cube = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete);

	auto MakePart = [&](const FVector& Center, const FVector& Size, ECitixSurface Surface) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
		if (!Component)
		{
			return nullptr;
		}
		Component->SetupAttachment(Root);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Cube);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(FCitixSurfaceLibrary::CastsShadow(Surface));
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetRelativeLocation(Center);
		Component->SetRelativeScale3D(Size / FCitixSurfaceLibrary::PrimitiveSize);
		if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(Surface))
		{
			Component->SetMaterial(0, Material);
		}
		Component->RegisterComponent();
		Rig.Parts.Add(Component);
		Rig.RestTransforms.Add(Component->GetRelativeTransform());
		return Component;
	};

	// Torso covers the hips as well, keeping the part count low.
	Rig.Torso = MakePart(FVector(0.f, 0.f, TorsoCenterZ), FVector(34.f, 40.f, TorsoHeight), Palette.Top);
	Rig.Head = MakePart(FVector(0.f, 0.f, HeadCenterZ), FVector(19.f, 21.f, 23.f), ECitixSurface::Skin);
	Rig.Hair = MakePart(FVector(-1.f, 0.f, HeadCenterZ + 11.f), FVector(20.f, 22.f, 9.f),
		(Rng.FRand() < 0.25f) ? ECitixSurface::ClothD : ECitixSurface::Hair);

	Rig.ShoulderL = FVector(0.f, -ShoulderY, ShoulderZ);
	Rig.ShoulderR = FVector(0.f, ShoulderY, ShoulderZ);
	Rig.HipL = FVector(0.f, -11.f, HipZ);
	Rig.HipR = FVector(0.f, 11.f, HipZ);

	const FVector ArmSize(12.f, 12.f, ArmLength);
	const FVector LegSize(16.f, 18.f, LegLength);

	Rig.ArmL = MakePart(Rig.ShoulderL + FVector(0.f, 0.f, -ArmLength * 0.5f), ArmSize, Palette.Top);
	Rig.ArmR = MakePart(Rig.ShoulderR + FVector(0.f, 0.f, -ArmLength * 0.5f), ArmSize, Palette.Top);
	Rig.LegL = MakePart(Rig.HipL + FVector(0.f, 0.f, -LegLength * 0.5f), LegSize, Palette.Bottom);
	Rig.LegR = MakePart(Rig.HipR + FVector(0.f, 0.f, -LegLength * 0.5f), LegSize, Palette.Bottom);

	Rig.TorsoRest = FVector(0.f, 0.f, TorsoCenterZ);
	Rig.HeadRest = FVector(0.f, 0.f, HeadCenterZ);
	Rig.HairRest = FVector(-1.f, 0.f, HeadCenterZ + 11.f);
	Rig.ArmHalfLength = ArmLength * 0.5f;
	Rig.LegHalfLength = LegLength * 0.5f;
	Rig.Height = HeadCenterZ + 11.5f;

	return Rig;
}

void FCitixHumanoidRig::UpdateWalk(float PhaseRadians, float SwingDegrees, float BobCm)
{
	const float LegSwing = FMath::Sin(PhaseRadians) * SwingDegrees;
	const float ArmSwing = -LegSwing * 0.85f;

	const float Bob = FMath::Sin(PhaseRadians * 2.f) * BobCm;

	if (Torso)
	{
		Torso->SetRelativeLocation(TorsoRest + FVector(0.f, 0.f, Bob));
	}
	if (Head)
	{
		Head->SetRelativeLocation(HeadRest + FVector(0.f, 0.f, Bob));
	}
	if (Hair)
	{
		Hair->SetRelativeLocation(HairRest + FVector(0.f, 0.f, Bob));
	}

	auto PoseLimb = [](UStaticMeshComponent* Limb, const FVector& Joint, float HalfLength, float AngleDegrees)
	{
		if (!Limb)
		{
			return;
		}
		const FRotator Rotation(AngleDegrees, 0.f, 0.f);
		// Keep the top of the limb at the joint while rotating about it.
		Limb->SetRelativeLocationAndRotation(Joint + Rotation.RotateVector(FVector(0.f, 0.f, -HalfLength)), Rotation);
	};

	PoseLimb(LegL, HipL, LegHalfLength, LegSwing);
	PoseLimb(LegR, HipR, LegHalfLength, -LegSwing);
	PoseLimb(ArmL, ShoulderL, ArmHalfLength, ArmSwing);
	PoseLimb(ArmR, ShoulderR, ArmHalfLength, -ArmSwing);
}

void FCitixHumanoidRig::ApplyPose(const FCitixHumanoidPose& Pose)
{
	// Everything above the waist swings about the hip pivot, which is what makes a bend
	// look like a bend instead of the whole body tilting.
	const FVector Pivot(0.f, 0.f, HipZ);
	auto UpperBodyPoint = [&Pose, &Pivot](const FVector& Rest)
	{
		return Pivot + Pose.Torso.RotateVector(Rest - Pivot) + Pose.BodyOffset;
	};

	if (Torso)
	{
		Torso->SetRelativeLocationAndRotation(UpperBodyPoint(TorsoRest), Pose.Torso);
	}
	if (Head)
	{
		const FRotator HeadRotation = (Pose.Torso + Pose.Head).GetNormalized();
		Head->SetRelativeLocationAndRotation(UpperBodyPoint(HeadRest), HeadRotation);
	}
	if (Hair)
	{
		const FRotator HairRotation = (Pose.Torso + Pose.Head).GetNormalized();
		Hair->SetRelativeLocationAndRotation(UpperBodyPoint(HairRest), HairRotation);
	}

	// Arms hang from the shoulders, so they inherit the torso bend in both position and
	// rotation: raise the chest and the hands come with it.
	auto PoseArm = [this, &Pose, &UpperBodyPoint](UStaticMeshComponent* Arm, const FVector& Shoulder, const FRotator& ShoulderRotation)
	{
		if (!Arm)
		{
			return;
		}
		const FRotator Rotation = (Pose.Torso + ShoulderRotation).GetNormalized();
		const FVector Joint = UpperBodyPoint(Shoulder);
		Arm->SetRelativeLocationAndRotation(
			Joint + Rotation.RotateVector(FVector(0.f, 0.f, -ArmHalfLength)), Rotation);
	};

	// Legs hang from the hips, which only translate with the body.
	auto PoseLeg = [this, &Pose](UStaticMeshComponent* Leg, const FVector& Hip, const FRotator& HipRotation)
	{
		if (!Leg)
		{
			return;
		}
		const FVector Joint = Hip + Pose.BodyOffset;
		Leg->SetRelativeLocationAndRotation(
			Joint + HipRotation.RotateVector(FVector(0.f, 0.f, -LegHalfLength)), HipRotation);
	};

	PoseArm(ArmL, ShoulderL, Pose.ShoulderL);
	PoseArm(ArmR, ShoulderR, Pose.ShoulderR);
	PoseLeg(LegL, HipL, Pose.HipL);
	PoseLeg(LegR, HipR, Pose.HipR);
}

void FCitixHumanoidRig::SetVisibility(bool bVisible) const
{
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(bVisible, true);
		}
	}
}

ECitixCharacterStyle FCitixCharacterLibrary::GetRandomStyle(FRandomStream& Rng)
{
	// Casual dominates, a few business commuters, some workers and elders.
	static const int32 Weights[] = { 46, 24, 18, 12 };
	int32 Total = 0;
	for (int32 Weight : Weights)
	{
		Total += Weight;
	}
	int32 Roll = Rng.RandRange(1, Total);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Weights); ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0)
		{
			return static_cast<ECitixCharacterStyle>(Index);
		}
	}
	return ECitixCharacterStyle::Casual;
}
