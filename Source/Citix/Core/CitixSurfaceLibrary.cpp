// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/CitixSurfaceLibrary.h"

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Citix.h"

TMap<ECitixSurface, TObjectPtr<UStaticMesh>> FCitixSurfaceLibrary::MeshCache;
TMap<ECitixSurface, TObjectPtr<UMaterialInstanceDynamic>> FCitixSurfaceLibrary::MaterialCache;
TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> FCitixSurfaceLibrary::TintedMaterialCache;
TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> FCitixSurfaceLibrary::TintedEmissiveCache;
TObjectPtr<UStaticMesh> FCitixSurfaceLibrary::CubeMesh = nullptr;
TObjectPtr<UStaticMesh> FCitixSurfaceLibrary::SphereMesh = nullptr;
TObjectPtr<UStaticMesh> FCitixSurfaceLibrary::CylinderMesh = nullptr;
TObjectPtr<UStaticMesh> FCitixSurfaceLibrary::ConeMesh = nullptr;
TObjectPtr<UStaticMesh> FCitixSurfaceLibrary::PlaneMesh = nullptr;
TObjectPtr<UMaterialInterface> FCitixSurfaceLibrary::BaseMaterial = nullptr;
TObjectPtr<UMaterialInterface> FCitixSurfaceLibrary::EmissiveBaseMaterial = nullptr;
TObjectPtr<UMaterialInterface> FCitixSurfaceLibrary::WindowBaseMaterial = nullptr;
TObjectPtr<UMaterialInterface> FCitixSurfaceLibrary::WaterBaseMaterial = nullptr;
float FCitixSurfaceLibrary::CurrentEmissiveBoost = 1.f;
bool FCitixSurfaceLibrary::bLoggedMaterialChoice = false;

UStaticMesh* FCitixSurfaceLibrary::LoadMesh(const TCHAR* Path)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
	if (!Mesh)
	{
		UE_LOG(LogCitix, Error, TEXT("FCitixSurfaceLibrary: failed to load mesh '%s'"), Path);
	}
	return Mesh;
}

void FCitixSurfaceLibrary::EnsureBuilt()
{
	if (!CubeMesh)
	{
		CubeMesh = LoadMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
		SphereMesh = LoadMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		CylinderMesh = LoadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		ConeMesh = LoadMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
		PlaneMesh = LoadMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	}

	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!BaseMaterial)
		{
			UE_LOG(LogCitix, Warning, TEXT("FCitixSurfaceLibrary: BasicShapeMaterial missing, using default material."));
			BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
		}
	}

	if (!EmissiveBaseMaterial)
	{
		// Preferred: the opaque unlit emissive materials authored by the
		// CitixMaterialSetup commandlet. Opaque matters: Lumen only picks up opaque
		// (or masked) surfaces, which is what makes lit windows actually emit light
		// into the city.
		EmissiveBaseMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Citix/Materials/M_CitixEmissive.M_CitixEmissive"));
		WindowBaseMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Citix/Materials/M_CitixWindow.M_CitixWindow"));

		if (!EmissiveBaseMaterial || !WindowBaseMaterial)
		{
			// Fallback so the project still runs before the commandlet is run. This is
			// the engine's additive (translucent) material: it looks lit but contributes
			// no global illumination.
			UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
			if (!EmissiveBaseMaterial)
			{
				EmissiveBaseMaterial = Fallback;
			}
			if (!WindowBaseMaterial)
			{
				WindowBaseMaterial = Fallback;
			}
			UE_LOG(LogCitix, Warning, TEXT("[Citix] Authored emissive materials not found; using the "
				"engine fallback (no GI from windows). Run: -run=CitixMaterialSetup"));
		}
		else
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Using authored opaque emissive materials "
				"(windows contribute to Lumen GI)."));
		}
	}

	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!BaseMaterial)
		{
			UE_LOG(LogCitix, Warning, TEXT("FCitixSurfaceLibrary: BasicShapeMaterial missing, using default material."));
			BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
		}
	}

	if (!WaterBaseMaterial)
	{
		// Authored water: opaque lit surface with animated procedural normals. If it is
		// missing the water falls back to the flat tinted material, so the project always
		// runs. Run: -run=CitixMaterialSetup
		WaterBaseMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Citix/Materials/M_CitixWater.M_CitixWater"));
		if (WaterBaseMaterial)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Using authored animated water material."));
		}
		else
		{
			UE_LOG(LogCitix, Warning, TEXT("[Citix] Authored water material not found; water will be "
				"flat. Run: -run=CitixMaterialSetup"));
		}
	}
}

bool FCitixSurfaceLibrary::UsesAuthoredWaterMaterial()
{
	EnsureBuilt();
	return WaterBaseMaterial != nullptr;
}

UStaticMesh* FCitixSurfaceLibrary::GetMesh(ECitixSurface Surface)
{
	EnsureBuilt();

	switch (Surface)
	{
	case ECitixSurface::Bush:
		if (!MeshCache.Contains(Surface)) MeshCache.Add(Surface,LoadMesh(TEXT("/Game/Citix/Meshes/SM_CitixBush.SM_CitixBush")));
		return MeshCache[Surface];
	case ECitixSurface::Trunk:
	case ECitixSurface::Pole:
	case ECitixSurface::Tire:
	case ECitixSurface::Tube:
		return CylinderMesh;
	case ECitixSurface::Foliage:
	case ECitixSurface::Lamp:
	case ECitixSurface::Dome:
	case ECitixSurface::OrbWarm:
	case ECitixSurface::OrbPink:
	case ECitixSurface::OrbCyan:
		return SphereMesh;
	case ECitixSurface::Spire:
		return ConeMesh;
	default:
		return CubeMesh;
	}
}

FLinearColor FCitixSurfaceLibrary::GetColor(ECitixSurface Surface)
{
	switch (Surface)
	{
	case ECitixSurface::Ground:			return FLinearColor(0.055f, 0.060f, 0.050f);
	case ECitixSurface::Asphalt:		return FLinearColor(0.045f, 0.045f, 0.052f);
	case ECitixSurface::AsphaltDark:	return FLinearColor(0.030f, 0.030f, 0.035f);
	case ECitixSurface::Marking:		return FLinearColor(0.85f, 0.84f, 0.72f);
	case ECitixSurface::MarkingDim:		return FLinearColor(0.35f, 0.34f, 0.30f);
	case ECitixSurface::Sidewalk:		return FLinearColor(0.30f, 0.30f, 0.295f);
	case ECitixSurface::Curb:			return FLinearColor(0.38f, 0.38f, 0.365f);
	case ECitixSurface::Grass:			return FLinearColor(0.065f, 0.19f, 0.06f);

	case ECitixSurface::Roof:			return FLinearColor(0.115f, 0.118f, 0.128f);
	case ECitixSurface::RoofDark:		return FLinearColor(0.070f, 0.072f, 0.080f);

	case ECitixSurface::FacadeConcrete:	return FLinearColor(0.30f, 0.29f, 0.28f);
	case ECitixSurface::FacadeBeige:	return FLinearColor(0.38f, 0.35f, 0.28f);
	case ECitixSurface::FacadeWhite:	return FLinearColor(0.46f, 0.46f, 0.45f);
	case ECitixSurface::FacadeBrick:	return FLinearColor(0.20f, 0.10f, 0.07f);
	case ECitixSurface::FacadeMetal:	return FLinearColor(0.24f, 0.26f, 0.29f);

	case ECitixSurface::GlassBlue:		return FLinearColor(0.10f, 0.20f, 0.34f);
	case ECitixSurface::GlassGreen:		return FLinearColor(0.10f, 0.24f, 0.22f);
	case ECitixSurface::GlassDark:		return FLinearColor(0.05f, 0.07f, 0.10f);

	case ECitixSurface::PropMetal:		return FLinearColor(0.30f, 0.31f, 0.33f);
	case ECitixSurface::PropDark:		return FLinearColor(0.10f, 0.10f, 0.11f);
	case ECitixSurface::Pole:			return FLinearColor(0.14f, 0.14f, 0.15f);
	case ECitixSurface::Lamp:			return FLinearColor(1.4f, 1.3f, 1.0f);
	case ECitixSurface::Trunk:			return FLinearColor(0.16f, 0.11f, 0.07f);
	case ECitixSurface::Foliage:		return FLinearColor(0.095f, 0.30f, 0.10f);
	case ECitixSurface::EmissiveWarm:	return FLinearColor(0.80f, 0.40f, 0.14f);
	case ECitixSurface::EmissiveCool:	return FLinearColor(0.18f, 0.45f, 0.85f);

	case ECitixSurface::CarPaint:		return FLinearColor(0.60f, 0.60f, 0.62f);
	case ECitixSurface::CarGlass:		return FLinearColor(0.015f, 0.020f, 0.035f);
	case ECitixSurface::Tire:			return FLinearColor(0.018f, 0.018f, 0.020f);
	case ECitixSurface::Trim:			return FLinearColor(0.060f, 0.060f, 0.070f);
	case ECitixSurface::HeadLight:		return FLinearColor(1.6f, 1.5f, 1.3f);
	case ECitixSurface::TailLight:		return FLinearColor(1.4f, 0.06f, 0.04f);

	case ECitixSurface::Skin:			return FLinearColor(0.66f, 0.47f, 0.36f);
	case ECitixSurface::ClothA:			return FLinearColor(0.35f, 0.12f, 0.12f);
	case ECitixSurface::ClothB:			return FLinearColor(0.12f, 0.18f, 0.35f);
	case ECitixSurface::ClothC:			return FLinearColor(0.15f, 0.22f, 0.16f);
	case ECitixSurface::ClothD:			return FLinearColor(0.55f, 0.52f, 0.45f);
	case ECitixSurface::Hair:			return FLinearColor(0.05f, 0.035f, 0.030f);

	// Night windows. Warm gold is the dominant interior light; cool blue is the office
	// accent. The bright tiers are kept just under white so a lit floor reads as warm
	// gold rather than a blown-out white bar, and unlit glazing is a dark blue-teal that
	// reads as glass against the facade instead of a black hole.
	case ECitixSurface::WindowWarmBright:	return FLinearColor(1.00f, 0.66f, 0.26f);
	case ECitixSurface::WindowWarmMid:		return FLinearColor(0.64f, 0.40f, 0.16f);
	case ECitixSurface::WindowWarmDim:		return FLinearColor(0.30f, 0.19f, 0.08f);
	case ECitixSurface::WindowCoolBright:	return FLinearColor(0.50f, 0.78f, 1.15f);
	case ECitixSurface::WindowCoolMid:		return FLinearColor(0.30f, 0.52f, 0.80f);
	case ECitixSurface::WindowCoolDim:		return FLinearColor(0.15f, 0.26f, 0.42f);
	case ECitixSurface::WindowWhite:		return FLinearColor(0.78f, 0.76f, 0.70f);
	case ECitixSurface::WindowGold:			return FLinearColor(1.05f, 0.76f, 0.32f);
	case ECitixSurface::WindowOff:			return FLinearColor(0.020f, 0.034f, 0.058f);
	// Broadcast-tower orbs: warm gold is dominant, pink and cyan are the restrained
	// landmark accents that echo the Oriental Pearl's lit spheres. Kept well below 1 so
	// the emissive boost does not clip them into flat white discs.
	case ECitixSurface::OrbWarm:			return FLinearColor(0.60f, 0.40f, 0.15f);
	case ECitixSurface::OrbPink:			return FLinearColor(0.62f, 0.11f, 0.32f);
	case ECitixSurface::OrbCyan:			return FLinearColor(0.13f, 0.42f, 0.60f);
	case ECitixSurface::GlassGold:		return FLinearColor(0.42f, 0.30f, 0.10f);
	case ECitixSurface::Spire:			return FLinearColor(0.30f, 0.31f, 0.34f);
	case ECitixSurface::Dome:			return FLinearColor(0.20f, 0.26f, 0.30f);
	case ECitixSurface::Tube:			return FLinearColor(0.55f, 0.56f, 0.58f);

	// Waterfront: very dark diffuse so the specular reflection does the talking. A bright
	// sky light on a mid-blue albedo would flatten the water into painted concrete.
	case ECitixSurface::Water:			return FLinearColor(0.004f, 0.011f, 0.020f);
	case ECitixSurface::WaterDeep:		return FLinearColor(0.002f, 0.006f, 0.012f);
	case ECitixSurface::Embankment:		return FLinearColor(0.30f, 0.29f, 0.27f);
	default:							return FLinearColor(0.5f, 0.5f, 0.5f);
	}
}

float FCitixSurfaceLibrary::GetRoughness(ECitixSurface Surface)
{
	switch (Surface)
	{
	case ECitixSurface::Ground:			return 0.95f;
	case ECitixSurface::Asphalt:		return 0.82f;
	case ECitixSurface::AsphaltDark:	return 0.82f;
	case ECitixSurface::Marking:		return 0.55f;
	case ECitixSurface::MarkingDim:		return 0.60f;
	case ECitixSurface::Sidewalk:		return 0.90f;
	case ECitixSurface::Curb:			return 0.88f;
	case ECitixSurface::Grass:			return 0.95f;

	case ECitixSurface::Roof:			return 0.85f;
	case ECitixSurface::RoofDark:		return 0.85f;

	case ECitixSurface::FacadeConcrete:	return 0.90f;
	case ECitixSurface::FacadeBeige:	return 0.90f;
	case ECitixSurface::FacadeWhite:	return 0.88f;
	case ECitixSurface::FacadeBrick:	return 0.92f;
	case ECitixSurface::FacadeMetal:	return 0.32f;

	case ECitixSurface::GlassBlue:		return 0.12f;
	case ECitixSurface::GlassGreen:		return 0.12f;
	case ECitixSurface::GlassDark:		return 0.10f;

	case ECitixSurface::PropMetal:		return 0.40f;
	case ECitixSurface::PropDark:		return 0.65f;
	case ECitixSurface::Pole:			return 0.50f;
	case ECitixSurface::Lamp:			return 0.30f;
	case ECitixSurface::Trunk:			return 0.90f;
	case ECitixSurface::Foliage:		return 0.85f;
	case ECitixSurface::EmissiveWarm:	return 0.40f;
	case ECitixSurface::EmissiveCool:	return 0.40f;

	case ECitixSurface::CarPaint:		return 0.28f;
	case ECitixSurface::CarGlass:		return 0.08f;
	case ECitixSurface::Tire:			return 0.90f;
	case ECitixSurface::Trim:			return 0.45f;
	case ECitixSurface::HeadLight:		return 0.15f;
	case ECitixSurface::TailLight:		return 0.15f;

	case ECitixSurface::Skin:			return 0.75f;
	case ECitixSurface::ClothA:			return 0.85f;
	case ECitixSurface::ClothB:			return 0.85f;
	case ECitixSurface::ClothC:			return 0.85f;
	case ECitixSurface::ClothD:			return 0.85f;
	case ECitixSurface::Hair:			return 0.85f;
	case ECitixSurface::WindowWarmBright:	return 0.35f;
	case ECitixSurface::WindowWarmMid:		return 0.35f;
	case ECitixSurface::WindowWarmDim:		return 0.35f;
	case ECitixSurface::WindowCoolBright:	return 0.35f;
	case ECitixSurface::WindowCoolMid:		return 0.35f;
	case ECitixSurface::WindowCoolDim:		return 0.35f;
	case ECitixSurface::WindowWhite:		return 0.35f;
	case ECitixSurface::WindowGold:			return 0.35f;
	case ECitixSurface::WindowOff:			return 0.12f;
	case ECitixSurface::OrbWarm:			return 0.30f;
	case ECitixSurface::OrbPink:			return 0.30f;
	case ECitixSurface::OrbCyan:			return 0.30f;
	case ECitixSurface::GlassGold:		return 0.10f;
	case ECitixSurface::Spire:			return 0.35f;
	case ECitixSurface::Dome:			return 0.15f;
	case ECitixSurface::Tube:			return 0.28f;

	case ECitixSurface::Water:			return 0.06f;
	case ECitixSurface::WaterDeep:		return 0.05f;
	case ECitixSurface::Embankment:		return 0.88f;
	default:							return 0.85f;
	}
}

bool FCitixSurfaceLibrary::Collides(ECitixSurface Surface)
{
	switch (Surface)
	{
	case ECitixSurface::Ground:
	case ECitixSurface::FacadeConcrete:
	case ECitixSurface::FacadeBeige:
	case ECitixSurface::FacadeWhite:
	case ECitixSurface::FacadeBrick:
	case ECitixSurface::FacadeMetal:
	case ECitixSurface::GlassBlue:
	case ECitixSurface::GlassGreen:
	case ECitixSurface::GlassDark:
	case ECitixSurface::PropMetal:
	case ECitixSurface::Pole:
	case ECitixSurface::Trunk:
	case ECitixSurface::CarPaint:
	case ECitixSurface::Tube:
	case ECitixSurface::Embankment:
	// Style-rolled masses must still stop cars, feet and bullets: glass-gold towers,
	// roof caps, spires, domes and landmark bulbs are all solid building, not trim.
	case ECitixSurface::GlassGold:
	case ECitixSurface::Roof:
	case ECitixSurface::RoofDark:
	case ECitixSurface::Spire:
	case ECitixSurface::Dome:
	case ECitixSurface::OrbWarm:
	case ECitixSurface::OrbPink:
	case ECitixSurface::OrbCyan:
		return true;
	default:
		return false;
	}
}

bool FCitixSurfaceLibrary::BlocksCharactersOnly(ECitixSurface Surface)
{
	switch (Surface)
	{
	// Roads, kerbs and pavements are solid underfoot for characters and dynamic bodies
	// (pedestrians, ragdolls) but invisible to the physics-body channel, because the
	// player's car is itself a physics body and a 16 cm curb must never stop it.
	case ECitixSurface::Asphalt:
	case ECitixSurface::AsphaltDark:
	case ECitixSurface::Sidewalk:
	case ECitixSurface::Curb:
		return true;
	default:
		return false;
	}
}

bool FCitixSurfaceLibrary::CastsShadow(ECitixSurface Surface)
{
	switch (Surface)
	{
	case ECitixSurface::Marking:
	case ECitixSurface::MarkingDim:
	case ECitixSurface::Asphalt:
	case ECitixSurface::AsphaltDark:
	case ECitixSurface::Ground:
	case ECitixSurface::Lamp:
	case ECitixSurface::Grass:
	case ECitixSurface::EmissiveWarm:
	case ECitixSurface::EmissiveCool:
	case ECitixSurface::HeadLight:
	case ECitixSurface::TailLight:
	case ECitixSurface::WindowWarmBright:
	case ECitixSurface::WindowWarmMid:
	case ECitixSurface::WindowWarmDim:
	case ECitixSurface::WindowCoolBright:
	case ECitixSurface::WindowCoolMid:
	case ECitixSurface::WindowCoolDim:
	case ECitixSurface::WindowWhite:
	case ECitixSurface::WindowGold:
	case ECitixSurface::OrbWarm:
	case ECitixSurface::OrbPink:
	case ECitixSurface::OrbCyan:
	case ECitixSurface::Foliage:
	case ECitixSurface::Water:
	case ECitixSurface::WaterDeep:
	case ECitixSurface::Embankment:
		return false;
	default:
		return true;
	}
}

bool FCitixSurfaceLibrary::IsEmissive(ECitixSurface Surface)
{
	switch (Surface)
	{
	case ECitixSurface::WindowWarmBright:
	case ECitixSurface::WindowWarmMid:
	case ECitixSurface::WindowWarmDim:
	case ECitixSurface::WindowCoolBright:
	case ECitixSurface::WindowCoolMid:
	case ECitixSurface::WindowCoolDim:
	case ECitixSurface::WindowWhite:
	case ECitixSurface::WindowGold:
	case ECitixSurface::OrbWarm:
	case ECitixSurface::OrbPink:
	case ECitixSurface::OrbCyan:
	case ECitixSurface::EmissiveWarm:
	case ECitixSurface::EmissiveCool:
	case ECitixSurface::HeadLight:
	case ECitixSurface::TailLight:
	case ECitixSurface::Lamp:
		return true;
	default:
		return false;
	}
}

int32 FCitixSurfaceLibrary::GetWindowVariantCount()
{
	return 9;
}

ECitixSurface FCitixSurfaceLibrary::GetWindowVariant(int32 Index)
{
	static const ECitixSurface Variants[] =
	{
		ECitixSurface::WindowWarmBright,
		ECitixSurface::WindowWarmMid,
		ECitixSurface::WindowWarmDim,
		ECitixSurface::WindowCoolBright,
		ECitixSurface::WindowCoolMid,
		ECitixSurface::WindowCoolDim,
		ECitixSurface::WindowWhite,
		ECitixSurface::WindowGold,
		ECitixSurface::WindowWarmMid
	};
	return Variants[FMath::Abs(Index) % UE_ARRAY_COUNT(Variants)];
}

bool FCitixSurfaceLibrary::UsesAuthoredWindowMaterial()
{
	EnsureBuilt();
	return WindowBaseMaterial != nullptr
		&& WindowBaseMaterial->GetPathName().StartsWith(TEXT("/Game/Citix/Materials/"));
}

bool FCitixSurfaceLibrary::IsWindowSurface(ECitixSurface Surface)
{
	return Surface >= ECitixSurface::WindowWarmBright && Surface <= ECitixSurface::WindowOff;
}

void FCitixSurfaceLibrary::SetNightWetness(float Wetness)
{
	const float W = FMath::Clamp(Wetness, 0.f, 1.f);

	auto ApplyWetness = [W](ECitixSurface Surface, float DryRoughness, float WetRoughness)
	{
		TObjectPtr<UMaterialInstanceDynamic>* Found = MaterialCache.Find(Surface);
		if (!Found || !*Found)
		{
			return;
		}
		(*Found)->SetScalarParameterValue(TEXT("Roughness"), FMath::Lerp(DryRoughness, WetRoughness, W));
		// Damp asphalt darkens when wet, which makes reflections read.
		const FLinearColor Colour = GetColor(Surface) * FMath::Lerp(1.f, 0.68f, W);
		(*Found)->SetVectorParameterValue(TEXT("Color"), Colour);
		(*Found)->SetVectorParameterValue(TEXT("BaseColor"), Colour);
	};

	ApplyWetness(ECitixSurface::Asphalt, 0.82f, 0.22f);
	ApplyWetness(ECitixSurface::AsphaltDark, 0.82f, 0.20f);
	ApplyWetness(ECitixSurface::Marking, 0.55f, 0.42f);
	ApplyWetness(ECitixSurface::MarkingDim, 0.60f, 0.46f);
	ApplyWetness(ECitixSurface::Sidewalk, 0.90f, 0.55f);
	ApplyWetness(ECitixSurface::Curb, 0.88f, 0.55f);
	ApplyWetness(ECitixSurface::Ground, 0.95f, 0.72f);
}

void FCitixSurfaceLibrary::SetEmissiveBoost(float Boost)
{
	CurrentEmissiveBoost = Boost;
	for (TPair<ECitixSurface, TObjectPtr<UMaterialInstanceDynamic>>& Pair : MaterialCache)
	{
		if (!Pair.Value || !IsEmissive(Pair.Key))
		{
			continue;
		}
		const FLinearColor Color = GetColor(Pair.Key) * Boost;
		Pair.Value->SetVectorParameterValue(TEXT("Color"), Color);
		Pair.Value->SetVectorParameterValue(TEXT("BaseColor"), Color);
	}
}

UMaterialInterface* FCitixSurfaceLibrary::GetMaterial(ECitixSurface Surface)
{
	EnsureBuilt();
	if (Surface==ECitixSurface::Bush) {
		static UMaterialInterface* Bush=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Citix/Materials/M_CitixBush.M_CitixBush"));
		if (Bush) { if (!Bush->IsRooted()) Bush->AddToRoot(); return Bush; }
	}

	if (TObjectPtr<UMaterialInstanceDynamic>* Found = MaterialCache.Find(Surface))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	const bool bEmissive = IsEmissive(Surface);
	const bool bWindow = IsWindowSurface(Surface);
	const bool bWater = (Surface == ECitixSurface::Water || Surface == ECitixSurface::WaterDeep)
		&& WaterBaseMaterial != nullptr;

	UMaterialInterface* Base = bWater
		? WaterBaseMaterial
		: (bWindow
			? (WindowBaseMaterial ? WindowBaseMaterial : EmissiveBaseMaterial)
			: (bEmissive ? EmissiveBaseMaterial : BaseMaterial));
	if (!Base)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	if (MID)
	{
		MID->AddToRoot();
		const FLinearColor Color = GetColor(Surface) * (bEmissive ? CurrentEmissiveBoost : 1.f);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		MID->SetScalarParameterValue(TEXT("Roughness"), GetRoughness(Surface));
		if (bWater)
		{
			// The authored water material exposes its own parameters. The deep colour and
			// the gloss range come from the surface library so Water and WaterDeep differ;
			// everything else about the water (wave scales, macro breakup, fresnel) is
			// tuned on the material itself.
			const float Roughness = GetRoughness(Surface);
			MID->SetVectorParameterValue(TEXT("WaterColor"), Color);
			MID->SetVectorParameterValue(TEXT("WaterShallowColor"), GetColor(ECitixSurface::Water) * 2.6f);
			MID->SetScalarParameterValue(TEXT("RoughnessMin"), Roughness);
			MID->SetScalarParameterValue(TEXT("RoughnessMax"), Roughness * 3.1f);
			MID->SetScalarParameterValue(TEXT("RoughnessAtGrazing"), Roughness * 0.62f);
		}
		if (bEmissive)
		{
			// The additive emissive material exposes an Opacity scalar.
			MID->SetScalarParameterValue(TEXT("Opacity"), 1.f);
		}
		MaterialCache.Add(Surface, MID);
	}
	return MID;
}

const TCHAR* FCitixSurfaceLibrary::GetSurfaceName(ECitixSurface Surface)
{
	switch (Surface)
	{
	case ECitixSurface::Ground: return TEXT("Ground");
	case ECitixSurface::Asphalt: return TEXT("Asphalt");
	case ECitixSurface::AsphaltDark: return TEXT("AsphaltDark");
	case ECitixSurface::Marking: return TEXT("Marking");
	case ECitixSurface::MarkingDim: return TEXT("MarkingDim");
	case ECitixSurface::Sidewalk: return TEXT("Sidewalk");
	case ECitixSurface::Curb: return TEXT("Curb");
	case ECitixSurface::Grass: return TEXT("Grass");
	case ECitixSurface::Roof: return TEXT("Roof");
	case ECitixSurface::RoofDark: return TEXT("RoofDark");
	case ECitixSurface::FacadeConcrete: return TEXT("FacadeConcrete");
	case ECitixSurface::FacadeBeige: return TEXT("FacadeBeige");
	case ECitixSurface::FacadeWhite: return TEXT("FacadeWhite");
	case ECitixSurface::FacadeBrick: return TEXT("FacadeBrick");
	case ECitixSurface::FacadeMetal: return TEXT("FacadeMetal");
	case ECitixSurface::GlassBlue: return TEXT("GlassBlue");
	case ECitixSurface::GlassGreen: return TEXT("GlassGreen");
	case ECitixSurface::GlassDark: return TEXT("GlassDark");
	case ECitixSurface::PropMetal: return TEXT("PropMetal");
	case ECitixSurface::PropDark: return TEXT("PropDark");
	case ECitixSurface::Pole: return TEXT("Pole");
	case ECitixSurface::Lamp: return TEXT("Lamp");
	case ECitixSurface::Trunk: return TEXT("Trunk");
	case ECitixSurface::Foliage: return TEXT("Foliage");
	case ECitixSurface::EmissiveWarm: return TEXT("EmissiveWarm");
	case ECitixSurface::EmissiveCool: return TEXT("EmissiveCool");
	case ECitixSurface::CarPaint: return TEXT("CarPaint");
	case ECitixSurface::CarGlass: return TEXT("CarGlass");
	case ECitixSurface::Tire: return TEXT("Tire");
	case ECitixSurface::Trim: return TEXT("Trim");
	case ECitixSurface::HeadLight: return TEXT("HeadLight");
	case ECitixSurface::TailLight: return TEXT("TailLight");
	case ECitixSurface::Skin: return TEXT("Skin");
	case ECitixSurface::ClothA: return TEXT("ClothA");
	case ECitixSurface::ClothB: return TEXT("ClothB");
	case ECitixSurface::ClothC: return TEXT("ClothC");
	case ECitixSurface::ClothD: return TEXT("ClothD");
	case ECitixSurface::Hair: return TEXT("Hair");
	case ECitixSurface::WindowWarmBright: return TEXT("WindowWarmBright");
	case ECitixSurface::WindowWarmMid: return TEXT("WindowWarmMid");
	case ECitixSurface::WindowWarmDim: return TEXT("WindowWarmDim");
	case ECitixSurface::WindowCoolBright: return TEXT("WindowCoolBright");
	case ECitixSurface::WindowCoolMid: return TEXT("WindowCoolMid");
	case ECitixSurface::WindowCoolDim: return TEXT("WindowCoolDim");
	case ECitixSurface::WindowWhite: return TEXT("WindowWhite");
	case ECitixSurface::WindowGold: return TEXT("WindowGold");
	case ECitixSurface::WindowOff: return TEXT("WindowOff");
	case ECitixSurface::OrbWarm: return TEXT("OrbWarm");
	case ECitixSurface::OrbPink: return TEXT("OrbPink");
	case ECitixSurface::OrbCyan: return TEXT("OrbCyan");
	case ECitixSurface::GlassGold: return TEXT("GlassGold");
	case ECitixSurface::Spire: return TEXT("Spire");
	case ECitixSurface::Dome: return TEXT("Dome");
	case ECitixSurface::Tube: return TEXT("Tube");
	default: return TEXT("Unknown");
	}
}

UMaterialInterface* FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface Surface, const FLinearColor& Color)
{
	EnsureBuilt();
	if (!BaseMaterial)
	{
		return nullptr;
	}

	// Quantise to 5 bits per channel so repeated colours share one instance.
	const uint32 R = static_cast<uint32>(FMath::Clamp(Color.R, 0.f, 1.f) * 31.f + 0.5f);
	const uint32 G = static_cast<uint32>(FMath::Clamp(Color.G, 0.f, 1.f) * 31.f + 0.5f);
	const uint32 B = static_cast<uint32>(FMath::Clamp(Color.B, 0.f, 1.f) * 31.f + 0.5f);
	const uint32 Packed = (R << 10) | (G << 5) | B;
	const uint64 Key = (static_cast<uint64>(Surface) << 32) | Packed;

	if (TObjectPtr<UMaterialInstanceDynamic>* Found = TintedMaterialCache.Find(Key))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	const FLinearColor Quantised(
		static_cast<float>(R) / 31.f, static_cast<float>(G) / 31.f, static_cast<float>(B) / 31.f, 1.f);

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
	if (!MID)
	{
		return nullptr;
	}
	MID->AddToRoot();
	MID->SetVectorParameterValue(TEXT("Color"), Quantised);
	MID->SetVectorParameterValue(TEXT("BaseColor"), Quantised);
	MID->SetScalarParameterValue(TEXT("Roughness"), GetRoughness(Surface));
	TintedMaterialCache.Add(Key, MID);
	return MID;
}

UMaterialInterface* FCitixSurfaceLibrary::GetTintedEmissiveMaterial(const FLinearColor& Color)
{
	EnsureBuilt();
	if (!EmissiveBaseMaterial)
	{
		return nullptr;
	}

	// Same 5-bit-per-channel quantisation as the lit path, so a handful of MIDs cover
	// every colour an overlay uses.
	const uint32 R = static_cast<uint32>(FMath::Clamp(Color.R, 0.f, 1.f) * 31.f + 0.5f);
	const uint32 G = static_cast<uint32>(FMath::Clamp(Color.G, 0.f, 1.f) * 31.f + 0.5f);
	const uint32 B = static_cast<uint32>(FMath::Clamp(Color.B, 0.f, 1.f) * 31.f + 0.5f);
	const uint32 Packed = (R << 10) | (G << 5) | B;

	if (TObjectPtr<UMaterialInstanceDynamic>* Found = TintedEmissiveCache.Find(Packed))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	const FLinearColor Quantised(
		static_cast<float>(R) / 31.f, static_cast<float>(G) / 31.f, static_cast<float>(B) / 31.f, 1.f);

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(EmissiveBaseMaterial, GetTransientPackage());
	if (!MID)
	{
		return nullptr;
	}
	MID->AddToRoot();
	MID->SetVectorParameterValue(TEXT("Color"), Quantised);
	MID->SetVectorParameterValue(TEXT("BaseColor"), Quantised);
	TintedEmissiveCache.Add(Packed, MID);
	return MID;
}

void FCitixSurfaceLibrary::LogMeshDiagnostics()
{
	auto LogSurface = [](const TCHAR* Label, ECitixSurface Surface)
	{
		if (UStaticMesh* Mesh = GetMesh(Surface))
		{
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			UE_LOG(LogCitix, Log, TEXT("[Citix] primitive '%s' (%s): origin=%s extent=%s"),
				Label, *Mesh->GetName(), *Bounds.Origin.ToCompactString(), *Bounds.BoxExtent.ToCompactString());
		}
		else
		{
			UE_LOG(LogCitix, Warning, TEXT("[Citix] primitive '%s' failed to load."), Label);
		}
	};

	LogSurface(TEXT("Cube"), ECitixSurface::FacadeConcrete);
	LogSurface(TEXT("Cylinder"), ECitixSurface::Pole);
	LogSurface(TEXT("Sphere"), ECitixSurface::Lamp);
}
