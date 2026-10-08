// Copyright Epic Games, Inc. All Rights Reserved.
// Project-wide, data-driven configuration for city generation.
// Editable under Project Settings > Game > Citix City. No code change needed to retune the city.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CitixTypes.h"
#include "CitixCitySettings.generated.h"

/** Road class mapped to its dimensions. */
USTRUCT(BlueprintType)
struct FCitixRoadClassRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	ECitixRoadClass Class = ECitixRoadClass::Local;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FCitixRoadSpec Spec;
};

/** Per-district night lighting character. */
USTRUCT(BlueprintType)
struct FCitixDistrictLightingProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Lighting")
	ECitixDistrict District = ECitixDistrict::Downtown;

	/** Chance a window band is lit at all (otherwise it is dark glass). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LitChance = 0.72f;

	/** 0 = mostly dim windows, 1 = mostly bright windows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BrightnessBias = 0.5f;

	/** Chance a lit window uses the cool (office blue) palette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoolChance = 0.3f;

	/** Chance of decorative facade/crown accent lighting on towers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AccentChance = 0.3f;

	/** Multiplier on how tall the glazing bands are (denser lit look). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Lighting", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float BandHeightScale = 1.f;
};

/** Player-centred traffic simulation budget. */
USTRUCT(BlueprintType)
struct FCitixTrafficSettings
{
	GENERATED_BODY()

	/** Hard cap on simultaneously simulated traffic vehicles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0"))
	int32 MaxVehicles = 150;

	/** Traffic is spawned inside this radius around the player, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "1000"))
	float SpawnRadius = 46000.f;

	/** Traffic beyond this radius is recycled, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "1000"))
	float DespawnRadius = 72000.f;

	/** Cruise speed on local roads, cm/s (about 40 km/h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100"))
	float CruiseSpeed = 1100.f;

	/** Multiplier applied to cruise speed on arterials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1"))
	float ArterialSpeedMultiplier = 1.8f;

	/** Base following gap kept to the vehicle ahead at a standstill, cm. The gap grows
	 *  with speed by FollowTimeHeadway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100"))
	float FollowDistance = 700.f;

	/** Traffic light cycle length, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "2.0"))
	float LightCycleSeconds = 12.f;

	/** Enable traffic lights at intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bEnableTrafficLights = true;
};

/** Player-centred pedestrian population budget. */
USTRUCT(BlueprintType)
struct FCitixPedestrianSettings
{
	GENERATED_BODY()

	/** Master switch for ambient pedestrians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians")
	bool bEnabled = true;

	/** Hard cap on simultaneously simulated pedestrians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "0"))
	int32 MaxPedestrians = 60;

	/** Pedestrians are spawned inside this radius around the player, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "1000"))
	float SpawnRadius = 22000.f;

	/** Pedestrians beyond this radius are recycled, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "1000"))
	float DespawnRadius = 30000.f;

	/** Average walking speed, cm/s (about 4.7 km/h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "10"))
	float WalkSpeed = 130.f;

	/** Random per-pedestrian speed variation, fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestrians", meta = (ClampMin = "0.0"))
	float SpeedVariation = 0.35f;
};

/**
 * Root settings object. Access via UCitixCitySettings::Get().
 */UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Citix City"))
class CITIX_API UCitixCitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCitixCitySettings();

	static const UCitixCitySettings& Get() { return *GetDefault<UCitixCitySettings>(); }

	virtual FName GetCategoryName() const override { return FName("Game"); }

	/** Resolve road dimensions for a class, falling back to a local street. */
	FCitixRoadSpec GetRoadSpec(ECitixRoadClass RoadClass) const;

	/** Resolve district generation rules, falling back to Downtown. */
	const FCitixDistrictRule& GetDistrictRule(ECitixDistrict District) const;

	/** Resolve the district night-lighting profile, falling back to Downtown. */
	const FCitixDistrictLightingProfile& GetLightingProfile(ECitixDistrict District) const;

	// ---- World layout -------------------------------------------------

	/** Deterministic generation seed. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0"))
	int32 Seed = 1337;

	/** Total city edge length in cm. 102400 = 1.024 km (the first vertical slice). */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "10000"))
	float CitySize = 102400.f;

	/** Spatial chunk edge length. Chunks own their instanced mesh components. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "4000"))
	float ChunkSize = 25600.f;

	// ---- Master plan ---------------------------------------------------
	// The city is planned first (river, districts, rings, radials, district street
	// patterns) and built from that plan. These are the plan-level dials.

	/** Base width of the river at its narrowest, cm. It widens downstream. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "4000"))
	float RiverWidth = 13000.f;

	// ---- Macro plan ----------------------------------------------------
	// The city is a band along the river, laid out on a river-relative lattice: columns
	// run across the river (and are the only place it can be crossed), rows run along it.

	/** Columns across the river. More columns = more cross-town streets. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "6", ClampMax = "40"))
	int32 PlanColumns = 14;

	/** Rows per bank, from the river outward. More rows = deeper blocks. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "3", ClampMax = "16"))
	int32 PlanRows = 6;

	/** Every Nth column is a level-1 arterial. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "2", ClampMax = "8"))
	int32 PlanArterialColumnStride = 3;

	/** Every Nth row is a level-1 arterial. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "2", ClampMax = "8"))
	int32 PlanArterialRowStride = 3;

	/**
	 * Land kept clear between the water's edge and the first street, cm. This must
	 * exceed half the widest road corridor so the riverside road never overhangs the
	 * water and the promenade has room without touching a road surface.
	 */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "200"))
	float PlanRiverGap = 2800.f;

	/** Typical land depth on each bank, as a fraction of CitySize. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0.15", ClampMax = "0.6"))
	float PlanDepthBase = 0.40f;

	/** How irregular the outer growth boundary is, as a fraction of CitySize. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float PlanDepthIrregularity = 0.085f;

	/** Number of river crossings. They always sit on an arterial column. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PlanBridgeCount = 4;

	/** How far into a district its own street bearing blends, in region units. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0.02", ClampMax = "0.4"))
	float PlanDistrictRotationBlend = 0.14f;
	/** Smooth structural nudge applied to lattice nodes, cm. 0 = perfectly regular. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0"))
	float PlanNodeJitter = 420.f;

	/** Frequency of that nudge along the river. Higher = more frequent wobble. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0.05", ClampMax = "3.0"))
	float PlanJitterWavelength = 0.55f;

	/** Fraction of the river bank left as reserved open waterfront. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlanWaterfrontReserveFraction = 0.65f;


	// ---- Planning preview ----------------------------------------------

	/** Stop after the macro plan and draw the debug preview. No buildings are generated. */
	UPROPERTY(EditAnywhere, config, Category = "World")
	bool bPlanPreviewMode = true;

	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "200"))
	float PreviewArterialWidth = 2400.f;

	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "200"))
	float PreviewSecondaryWidth = 1400.f;

	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "100"))
	float PreviewLocalWidth = 800.f;

	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "200"))
	float PreviewBridgeWidth = 2200.f;

	/** Placeholder heights for the future skyline, cm. */
	UPROPERTY(EditAnywhere, config, Category = "World")
	float PreviewHeightPrimary = 20000.f;

	UPROPERTY(EditAnywhere, config, Category = "World")
	float PreviewHeightSecondary = 12000.f;

	UPROPERTY(EditAnywhere, config, Category = "World")
	float PreviewHeightHigh = 4200.f;

	UPROPERTY(EditAnywhere, config, Category = "World")
	float PreviewHeightMedium = 1500.f;

	UPROPERTY(EditAnywhere, config, Category = "World")
	float PreviewHeightLow = 500.f;

	// ---- Build stage (Phase 2) -----------------------------------------

	/** Smallest buildable block edge after the bounding roads are removed, cm. */
	UPROPERTY(EditAnywhere, config, Category = "Build", meta = (ClampMin = "500"))
	float MinBlockSize = 2200.f;

	/** Smallest parcel worth putting a building on, cm. */
	UPROPERTY(EditAnywhere, config, Category = "Build", meta = (ClampMin = "300"))
	float MinParcelSize = 900.f;

	/** Carriageway width of a mid-block local street, cm. */
	UPROPERTY(EditAnywhere, config, Category = "Build", meta = (ClampMin = "300"))
	float LocalStreetWidth = 800.f;

	/** Chance a parcel becomes a hard plaza in a cluster or on the waterfront. */
	UPROPERTY(EditAnywhere, config, Category = "Build", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float PlazaChance = 0.07f;

	/** Chance an industrial parcel becomes a parking lot. */
	UPROPERTY(EditAnywhere, config, Category = "Build", meta = (ClampMin = "0.0", ClampMax = "0.6"))
	float ParkingChance = 0.14f;

	// Future skyline bands per density zone, cm. This is what makes the towers group by
	// zone instead of scattering: only the primary cluster reaches supertall heights.
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightPrimaryMin = 13000.f;
	/** Support towers cap well below the hero band, so the four landmarks always read. */
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightPrimaryMax = 20000.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightSecondaryMin = 7000.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightSecondaryMax = 17000.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightHighMin = 3200.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightHighMax = 11000.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightMediumMin = 1400.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightMediumMax = 6200.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightLowMin = 800.f;
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float ZoneHeightLowMax = 3000.f;

	/** Ceiling for a signature landmark tower, cm. */
	UPROPERTY(EditAnywhere, config, Category = "Build")
	float LandmarkMaxHeight = 52000.f;

	/** Landmark probability at the very centre of a primary cluster. */
	UPROPERTY(EditAnywhere, config, Category = "Build", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LandmarkCoreChance = 0.62f;

	/** Maximum number of signature landmark supertalls in the slice. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0"))
	int32 MaxLandmarks = 11;

	/** Generate the city automatically when the game starts. */
	UPROPERTY(EditAnywhere, config, Category = "World")
	bool bGenerateOnBeginPlay = true;

	/** Extra ground margin beyond the city edge, cm. */
	UPROPERTY(EditAnywhere, config, Category = "World", meta = (ClampMin = "0"))
	float GroundMargin = 6000.f;

	// ---- Vertical dimensions -----------------------------------------

	UPROPERTY(EditAnywhere, config, Category = "Dimensions")
	float GroundThickness = 100.f;

	UPROPERTY(EditAnywhere, config, Category = "Dimensions")
	float RoadThickness = 12.f;

	UPROPERTY(EditAnywhere, config, Category = "Dimensions")
	float SidewalkHeight = 16.f;

	UPROPERTY(EditAnywhere, config, Category = "Dimensions")
	float MarkingHeight = 1.5f;

	// ---- Street furniture --------------------------------------------

	UPROPERTY(EditAnywhere, config, Category = "Furniture", meta = (ClampMin = "500"))
	float LampSpacing = 3400.f;

	UPROPERTY(EditAnywhere, config, Category = "Furniture", meta = (ClampMin = "500"))
	float TreeSpacing = 1800.f;

	UPROPERTY(EditAnywhere, config, Category = "Furniture")
	float LampHeight = 900.f;

	/** Chance of a street lamp at each lamp slot. */
	UPROPERTY(EditAnywhere, config, Category = "Furniture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LampChance = 0.85f;

	// ---- Rulesets -----------------------------------------------------

	UPROPERTY(EditAnywhere, config, Category = "Rules")
	TArray<FCitixRoadClassRule> RoadRules;

	UPROPERTY(EditAnywhere, config, Category = "Rules")
	TArray<FCitixDistrictRule> DistrictRules;

	UPROPERTY(EditAnywhere, config, Category = "Traffic")
	FCitixTrafficSettings Traffic;

	UPROPERTY(EditAnywhere, config, Category = "Pedestrians")
	FCitixPedestrianSettings Pedestrians;

	UPROPERTY(EditAnywhere, config, Category = "Night Lighting")
	TArray<FCitixDistrictLightingProfile> DistrictLighting;

private:
	/** Builds the default ruleset. Called once from the constructor. */
	void PopulateDefaults();
};
