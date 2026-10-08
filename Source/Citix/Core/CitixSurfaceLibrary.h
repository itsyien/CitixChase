// Copyright Epic Games, Inc. All Rights Reserved.
// Maps logical surfaces to engine primitive meshes and shared material instances.
// Centralising this keeps draw calls low and makes an art pass a one-file change.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Static library resolving ECitixSurface values into concrete mesh/material data.
 * Meshes and materials are cached and rooted for the lifetime of the process.
 */
class CITIX_API FCitixSurfaceLibrary
{
public:
	/** Load (and cache) the engine primitive mesh used for this surface. */
	static UStaticMesh* GetMesh(ECitixSurface Surface);

	/** Shared dynamic material instance for this surface (tinted BasicShapeMaterial). */
	static UMaterialInterface* GetMaterial(ECitixSurface Surface);

	/** True for surfaces that should glow (windows, lamps, vehicle lights). */
	static bool IsEmissive(ECitixSurface Surface);

	/**
	 * Scale the brightness of every emissive surface. Driven by time of day so
	 * windows, lamps and lights glow at night and stay subtle in daylight.
	 */
	static void SetEmissiveBoost(float Boost);

	/**
	 * How wet the streets look (0 dry, 1 soaked). Lowers roughness and darkens the
	 * road surfaces so they catch light and reflect — the classic wet-night look.
	 */
	static void SetNightWetness(float Wetness);

	/** Number of window lighting variants (natural per-window variation). */
	static int32 GetWindowVariantCount();
	static ECitixSurface GetWindowVariant(int32 Index);
	static bool IsWindowSurface(ECitixSurface Surface);

	/** True when the authored opaque window material was found (vs the engine fallback). */
	static bool UsesAuthoredWindowMaterial();

	/** True when the authored animated water material was found (vs the flat fallback). */
	static bool UsesAuthoredWaterMaterial();

	/** Base tint for a surface. */
	static FLinearColor GetColor(ECitixSurface Surface);

	/** Roughness for a surface (matte concrete vs reflective glass). */
	static float GetRoughness(ECitixSurface Surface);

	/** True when instances of this surface should block the player vehicle (curbs, facades). */
	static bool Collides(ECitixSurface Surface);

	/**
	 * True for the drive/walk surfaces: roads, sidewalks and curbs. They are solid for
	 * characters and dynamic bodies (so pedestrians and ragdolls rest on them) but must
	 * not block the car's chassis or its suspension traces, or curbs would stop the car.
	 */
	static bool BlocksCharactersOnly(ECitixSurface Surface);

	/** True for surfaces placed at ground level where shadows/lighting cost adds up. */
	static bool CastsShadow(ECitixSurface Surface);

	/** Nominal engine primitive dimension in cm (Cube/Sphere/Cylinder are 100 units). */
	static constexpr float PrimitiveSize = 100.f;

	/** Log the actual bounds of the engine primitives (validates size/pivot assumptions). */
	static void LogMeshDiagnostics();

	/** Human-readable surface name, for logs. */
	static const TCHAR* GetSurfaceName(ECitixSurface Surface);

	/**
	 * A shared material instance for a surface tinted with an arbitrary colour.
	 * Colours are quantised and cached, so per-car paint does not create unbounded MIDs.
	 */
	static UMaterialInterface* GetTintedMaterial(ECitixSurface Surface, const FLinearColor& Color);

	/**
	 * The same idea on the emissive (unlit) master, so an overlay drawn from it stays
	 * readable at night and visibly is not part of the city.
	 */
	static UMaterialInterface* GetTintedEmissiveMaterial(const FLinearColor& Color);

private:
	static UStaticMesh* LoadMesh(const TCHAR* Path);
	static void EnsureBuilt();

	static TMap<ECitixSurface, TObjectPtr<UStaticMesh>> MeshCache;
	static TMap<ECitixSurface, TObjectPtr<UMaterialInstanceDynamic>> MaterialCache;
	static TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> TintedMaterialCache;
	/** Tinted emissive instances, keyed by packed colour (for overlays readable at night). */
	static TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> TintedEmissiveCache;
	static TObjectPtr<UStaticMesh> CubeMesh;
	static TObjectPtr<UStaticMesh> SphereMesh;
	static TObjectPtr<UStaticMesh> CylinderMesh;
	static TObjectPtr<UStaticMesh> ConeMesh;
	static TObjectPtr<UStaticMesh> PlaneMesh;
	static TObjectPtr<UMaterialInterface> BaseMaterial;
	static TObjectPtr<UMaterialInterface> EmissiveBaseMaterial;
	/** Opaque emissive material with per-instance random brightness (window bands). */
	static TObjectPtr<UMaterialInterface> WindowBaseMaterial;
	/** Opaque lit water material with animated procedural normals (authored). */
	static TObjectPtr<UMaterialInterface> WaterBaseMaterial;
	static float CurrentEmissiveBoost;
	static bool bLoggedMaterialChoice;
};
