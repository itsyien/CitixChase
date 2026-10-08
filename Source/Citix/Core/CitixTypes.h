// Copyright Epic Games, Inc. All Rights Reserved.
// Core shared data types for the Citix city project.
// Keep this header free of gameplay logic - it is the vocabulary other systems speak.

#pragma once

#include "CoreMinimal.h"
#include "CitixTypes.generated.h"

/**
 * District archetypes. The city is divided into regions, each with its own
 * generation rules (building height, lot size, density, palette).
 * Inspired by Shanghai but intentionally fictional.
 */
UENUM(BlueprintType)
enum class ECitixDistrict : uint8
{
	Financial		UMETA(DisplayName = "Financial District (Pudong-like)"),
	Downtown		UMETA(DisplayName = "Dense Downtown"),
	Commercial		UMETA(DisplayName = "Commercial / Mixed Use"),
	OldTown			UMETA(DisplayName = "Old Urban District"),
	Residential		UMETA(DisplayName = "Residential"),
	Industrial		UMETA(DisplayName = "Industrial / Port"),
	Parkland		UMETA(DisplayName = "Parkland"),
	Waterfront		UMETA(DisplayName = "Waterfront Promenade"),
	University		UMETA(DisplayName = "University Campus"),
	Market			UMETA(DisplayName = "Market Quarter"),
	OuterCity		UMETA(DisplayName = "Outer City"),

	Count			UMETA(Hidden)
};

/** Road hierarchy. Higher classes are wider and carry more traffic. */
UENUM(BlueprintType)
enum class ECitixRoadClass : uint8
{
	Alley			UMETA(DisplayName = "Alley"),
	Local			UMETA(DisplayName = "Local Street"),
	Collector		UMETA(DisplayName = "Collector Road"),
	Arterial		UMETA(DisplayName = "Arterial Avenue"),
	Boulevard		UMETA(DisplayName = "Tree-lined Boulevard"),
	Highway			UMETA(DisplayName = "Ring Highway")
};

/**
 * Planning hierarchy. The plan is generated level by level, and only these three levels
 * exist: a small skeleton of arterials, secondary roads that define neighbourhoods, and
 * local streets that fill blocks in.
 */
UENUM(BlueprintType)
enum class ECitixRoadLevel : uint8
{
	Arterial		UMETA(DisplayName = "Level 1 - Major Arterial"),
	Secondary		UMETA(DisplayName = "Level 2 - Secondary Road"),
	Local			UMETA(DisplayName = "Level 3 - Local Street")
};

/** Future density role of a block, used for the skyline plan and by the building stage. */
UENUM(BlueprintType)
enum class ECitixPlanZone : uint8
{
	/** Primary skyscraper cluster (the CBD). */
	PrimaryCluster		UMETA(DisplayName = "Primary Skyline Cluster"),
	/** Secondary cluster. */
	SecondaryCluster	UMETA(DisplayName = "Secondary Skyline Cluster"),
	/** High density, but not a supertall cluster. */
	HighDensity			UMETA(DisplayName = "High Density"),
	MediumDensity		UMETA(DisplayName = "Medium Density"),
	LowDensity			UMETA(DisplayName = "Low Density"),

	Count				UMETA(Hidden)
};

/** Layers of the planning preview, each drawn with its own colour. */
UENUM(BlueprintType)
enum class ECitixPreviewLayer : uint8
{
	River,
	Arterial,
	Secondary,
	Local,
	Bridge,
	Park,
	ZonePrimary,
	ZoneSecondary,
	ZoneHigh,
	ZoneMedium,
	ZoneLow,
	DistrictOutline,

	Count				UMETA(Hidden)
};

/** How an individual lot inside a city block is used. */
UENUM(BlueprintType)
enum class ECitixLotUsage : uint8
{
	Building		UMETA(DisplayName = "Building"),
	Plaza			UMETA(DisplayName = "Plaza"),
	Park			UMETA(DisplayName = "Park"),
	Parking			UMETA(DisplayName = "Parking Lot"),
	Empty			UMETA(DisplayName = "Empty / Courtyard")
};

/**
 * Building silhouette archetypes. These give the skyline real variety, inspired by
 * the Pudong cluster: twisting supertalls, slabs with a crown opening, ziggurats,
 * twin towers, cylindrical towers, spired masts, courtyard blocks, apartment slabs,
 * shophouse rows and industrial sheds.
 */
UENUM(BlueprintType)
enum class ECitixBuildingArchetype : uint8
{
	Box				UMETA(DisplayName = "Simple Box"),
	PodiumTower		UMETA(DisplayName = "Podium + Tower"),
	SetbackTower	UMETA(DisplayName = "Stepped Ziggurat"),
	TwistedTower	UMETA(DisplayName = "Twisting Tower"),
	CrownedTower	UMETA(DisplayName = "Crowned Tower"),
	HoledSlab		UMETA(DisplayName = "Crown-Opening Slab"),
	TwinTower		UMETA(DisplayName = "Twin Towers"),
	CylinderTower	UMETA(DisplayName = "Cylindrical Tower"),
	SpireTower		UMETA(DisplayName = "Spire / Mast Tower"),
	Courtyard		UMETA(DisplayName = "Courtyard Block"),
	ApartmentSlab	UMETA(DisplayName = "Apartment Slab"),
	Shophouse		UMETA(DisplayName = "Shophouse Row"),
	Warehouse		UMETA(DisplayName = "Warehouse Shed"),

	Count			UMETA(Hidden)
};

/** Deterministic landmark silhouettes layered over the normal procedural skyline. */
UENUM(BlueprintType)
enum class ECitixLandmarkStyle : uint8
{
	None,
	PearlBroadcastTower,
	TwistingSupertall,
	CrownOpeningTower,
	TieredCrownTower
};

/** How a whole city block is subdivided into lots. */
UENUM(BlueprintType)
enum class ECitixBlockPattern : uint8
{
	GridLots		UMETA(DisplayName = "Uniform Lot Grid"),
	Perimeter		UMETA(DisplayName = "Perimeter + Courtyard"),
	TwinLots		UMETA(DisplayName = "Two Large Lots"),
	SingleLot		UMETA(DisplayName = "Single Landmark Lot"),
	RowUnits		UMETA(DisplayName = "Narrow Row Units"),
	OpenBlock		UMETA(DisplayName = "Open Park Block"),

	Count			UMETA(Hidden)
};

/**
 * Logical surface / material category. Every procedural instance belongs to one.
 * A chunk keeps one instanced mesh component per surface so draw calls stay low.
 */
UENUM(BlueprintType)
enum class ECitixSurface : uint8
{
	Ground,
	Asphalt,
	AsphaltDark,
	Marking,
	MarkingDim,
	Sidewalk,
	Curb,
	Grass,

	Roof,
	RoofDark,

	FacadeConcrete,
	FacadeBeige,
	FacadeWhite,
	FacadeBrick,
	FacadeMetal,

	GlassBlue,
	GlassGreen,
	GlassDark,

	PropMetal,
	PropDark,
	Pole,
	Lamp,
	Trunk,
	Foliage,
	EmissiveWarm,
	EmissiveCool,

	// Vehicles
	CarPaint,
	CarGlass,
	Tire,
	Trim,
	HeadLight,
	TailLight,

	// Characters
	Skin,
	ClothA,
	ClothB,
	ClothC,
	ClothD,
	Hair,

	// Building variety / crowns / lighting
	GlassGold,

	// Night window lighting family. Different variants = natural variation without
	// per-instance custom data (which would need an authored material).
	WindowWarmBright,
	WindowWarmMid,
	WindowWarmDim,
	WindowCoolBright,
	WindowCoolMid,
	WindowCoolDim,
	WindowWhite,
	WindowGold,
	/** Dark, glossy glass: an unlit window at night. */
	WindowOff,
	/**
	 * Lit orbs, rendered on the sphere primitive with a uniform diameter. The broadcast
	 * tower's bulbs need these: the sphere mesh is chosen by surface, and a bulb emitted
	 * on a window surface would come out as a cube.
	 */
	OrbWarm,
	OrbPink,
	OrbCyan,
	Spire,
	Dome,
	Tube,

	// Waterfront
	Water,
	WaterDeep,
	Embankment,
	Bush,

	Count				UMETA(Hidden)
};

/** Default dimensions (in cm) for a road class. Data-driven via UCitixCitySettings. */
USTRUCT(BlueprintType)
struct FCitixRoadSpec
{
	GENERATED_BODY()

	/** Width of a single lane, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float LaneWidth = 350.f;

	/** Number of lanes (both directions counted together). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	int32 NumLanes = 2;

	/** Width of each sidewalk, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float SidewalkWidth = 400.f;

	/** Extra width added to the centre (medians / turn lanes), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float MedianWidth = 0.f;

	/** Carriageway width without sidewalks, cm. */
	FORCEINLINE float CarriagewayWidth() const { return LaneWidth * static_cast<float>(NumLanes) + MedianWidth; }

	/** Full corridor width including both sidewalks, cm. */
	FORCEINLINE float CorridorWidth() const { return CarriagewayWidth() + SidewalkWidth * 2.f; }

	/** Half width used to inset blocks from the road centre line. */
	FORCEINLINE float HalfCorridor() const { return CorridorWidth() * 0.5f; }
};

/** Per-district generation rules. */
USTRUCT(BlueprintType)
struct FCitixDistrictRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District")
	ECitixDistrict District = ECitixDistrict::Downtown;

	/** Minimum building height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "300"))
	float MinHeight = 3000.f;

	/** Maximum building height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "300"))
	float MaxHeight = 12000.f;

	/** Typical floor height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "200"))
	float FloorHeight = 380.f;

	/** Minimum lot edge length, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "400"))
	float LotMinSize = 1600.f;

	/** Maximum lot edge length, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "400"))
	float LotMaxSize = 4200.f;

	/** Fraction of a lot covered by the building footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float BuildCoverage = 0.82f;

	/** Chance a lot stays empty (courtyard / gap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmptyLotChance = 0.06f;

	/** Chance a lot becomes a small park instead of a building. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ParkChance = 0.02f;

	/** Chance of a wider podium at street level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodiumChance = 0.3f;

	/** Chance of rooftop mechanical equipment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoofEquipmentChance = 0.4f;

	/** Probability weight for trees along sidewalks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TreeChance = 0.15f;

	/** Relative visual richness (0 = plain, 1 = maximum detail). Controls optional detail instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DetailLevel = 0.6f;
};

/** A single box to be emitted as an instance. Produced by generators, consumed by chunks. */
struct FCitixBoxInstance
{
	FVector Center = FVector::ZeroVector;
	FVector Size = FVector(100.f);
	FRotator Rotation = FRotator::ZeroRotator;
	ECitixSurface Surface = ECitixSurface::FacadeConcrete;

	/**
	 * Optional per-instance direction (world XY, unit length). Used by the water material
	 * to orient its wave field along the river rather than along a fixed world axis. Zero
	 * means "not used", and the surface is then uploaded without custom data.
	 */
	FVector2D Flow = FVector2D::ZeroVector;
};

/** Description of a procedural building before it becomes geometry. */
USTRUCT(BlueprintType)
struct FCitixBuildingSpec
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	FVector2D Center = FVector2D::ZeroVector;

	/** Base footprint size (X = width, Y = depth), cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Building")
	FVector2D Footprint = FVector2D(2000.f, 2000.f);

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	float FloorHeight = 380.f;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	int32 Floors = 4;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	ECitixDistrict District = ECitixDistrict::Downtown;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	ECitixLotUsage Usage = ECitixLotUsage::Building;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	ECitixBuildingArchetype Archetype = ECitixBuildingArchetype::Box;

	/** A hero silhouette chosen by the city plan; None leaves the normal archetype untouched. */
	ECitixLandmarkStyle LandmarkStyle = ECitixLandmarkStyle::None;

	/** Index of the facade palette entry. */
	UPROPERTY(BlueprintReadWrite, Category = "Building")
	int32 FacadeStyle = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	bool bPodium = false;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	bool bRoofEquipment = true;

	/** Ground height the building sits on (top of sidewalk), cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Building")
	float BaseZ = 0.f;

	/**
	 * Night-lighting multiplier, 1 = the district's full profile. Support buildings close
	 * to a hero landmark are dimmed so the hero keeps visual dominance and a pocket of
	 * empty sky. Set by the city builder, read when the facade palette is resolved.
	 */
	float NightLightScale = 1.f;

	FORCEINLINE float Height() const { return FloorHeight * static_cast<float>(Floors); }
};
