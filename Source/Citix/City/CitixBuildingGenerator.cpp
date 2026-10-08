// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixBuildingGenerator.h"
#include "Core/CitixCitySettings.h"

namespace
{
	/** Defined below; used by AddFloorBands. */
	ECitixSurface PickWindowSurface(const FCitixDistrictLightingProfile& Profile, FRandomStream& Rng);

	/** Resolved facade palette for one building. */
	struct FFacade
	{
		ECitixSurface Body = ECitixSurface::FacadeConcrete;
		ECitixSurface Band = ECitixSurface::WindowWarmMid;
		ECitixSurface Accent = ECitixSurface::FacadeConcrete;
		/** District night-lighting character, used for per-band window variation. */
		FCitixDistrictLightingProfile Lighting;
	};

	/** Deterministic pick from a small palette. */
	template <int32 N>
	ECitixSurface Pick(const ECitixSurface(&Palette)[N], int32 Style)
	{
		return Palette[FMath::Abs(Style) % N];
	}

	/** Weighted pick from a surface/weight table. */
	ECitixSurface PickWeighted(const ECitixSurface* Surfaces, const int32* Weights, int32 Count, FRandomStream& Rng)
	{
		int32 Total = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Total += Weights[Index];
		}
		int32 Roll = Rng.RandRange(1, FMath::Max(1, Total));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Roll -= Weights[Index];
			if (Roll <= 0)
			{
				return Surfaces[Index];
			}
		}
		return Surfaces[0];
	}

	// --- Emission primitives (all sizes in cm) -------------------------

	void AddBox(TArray<FCitixBoxInstance>& Out, const FVector& Center, const FVector& Size,
		ECitixSurface Surface, float YawDegrees = 0.f)
	{
		FCitixBoxInstance Box;
		Box.Center = Center;
		Box.Size = Size;
		Box.Rotation = FRotator(0.f, YawDegrees, 0.f);
		Box.Surface = Surface;
		Out.Add(Box);
	}

	/** Box with a pitch rotation (used for angled roofs). */
	void AddBoxPitched(TArray<FCitixBoxInstance>& Out, const FVector& Center, const FVector& Size,
		ECitixSurface Surface, float PitchDegrees)
	{
		FCitixBoxInstance Box;
		Box.Center = Center;
		Box.Size = Size;
		Box.Rotation = FRotator(PitchDegrees, 0.f, 0.f);
		Box.Surface = Surface;
		Out.Add(Box);
	}

	/** Cylinder instances carry their diameter in X/Y and height in Z. */
	void AddCylinder(TArray<FCitixBoxInstance>& Out, const FVector& Center, float Diameter, float Height,
		ECitixSurface Surface)
	{
		AddBox(Out, Center, FVector(Diameter, Diameter, Height), Surface);
	}

	void AddCone(TArray<FCitixBoxInstance>& Out, const FVector& Center, float Diameter, float Height,
		ECitixSurface Surface)
	{
		AddBox(Out, Center, FVector(Diameter, Diameter, Height), Surface);
	}

	void AddSphere(TArray<FCitixBoxInstance>& Out, const FVector& Center, float Diameter,
		ECitixSurface Surface)
	{
		AddBox(Out, Center, FVector(Diameter, Diameter, Diameter), Surface);
	}

	/**
	 * Horizontal glazing bands at every Nth floor. This is what makes a tower read as
	 * an office building and, with the emissive window surfaces, as a lit skyline.
	 */
	void AddFloorBands(TArray<FCitixBoxInstance>& Out, const FVector2D& Center, float Width, float Depth,
		float BaseZ, int32 Floors, float FloorHeight, const FCitixDistrictLightingProfile& Profile,
		FRandomStream& Rng, float Scale, float Yaw, int32 Step)
	{
		Step = FMath::Max(1, Step);

		// Real towers are never lit uniformly: plant floors, mechanical levels and empty
		// tenancies leave whole floors dark. Sparse districts get more of them. This also
		// breaks up the "stack of identical glowing rings" the skyline used to have, and
		// costs nothing - those bands are simply never emitted. The floor is biased well
		// above the old values so the dark facade body is a real part of every tower
		// rather than a hairline between two lit slabs.
		const float DarkFloorChance = FMath::Lerp(0.60f, 0.26f, FMath::Clamp(Profile.LitChance, 0.f, 1.f));

		// A vertical stripe of floors that happens to be dark, so buildings also vary in
		// the vertical direction rather than only floor by floor.
		const int32 DarkStripe = Rng.RandRange(0, FMath::Max(2, Step * 3));
		const int32 DarkStripeWidth = Rng.RandRange(1, 3);

		for (int32 Floor = 0; Floor < Floors; Floor += Step)
		{
			if (Floor == 0)
			{
				continue; // the storefront band covers the ground floor
			}
			if (Rng.FRand() < DarkFloorChance)
			{
				continue;
			}
			if (Step > 1 && FMath::Abs(Floor - DarkStripe * Step) < DarkStripeWidth * Step)
			{
				continue;
			}

			// Each band picks its own lighting variant, so lit and dark windows mix
			// naturally down and across a facade instead of every floor looking identical.
			const ECitixSurface Band = PickWindowSurface(Profile, Rng);
			const float Z = BaseZ + (static_cast<float>(Floor) + 0.5f) * FloorHeight;
			// A ribbon window, not a slab. The lit band is a bit under half the floor
			// height, so the dark glass / charcoal facade body stays clearly visible between
			// floors and the tower keeps a readable mass. The old 0.74 let near-adjacent
			// glowing slabs merge into the "barcode" that made every tower read the same.
			AddBox(Out, FVector(Center.X, Center.Y, Z),
				FVector(Width * Scale, Depth * Scale, FloorHeight * 0.36f * Profile.BandHeightScale), Band, Yaw);
		}
	}

	/**
	 * Decorative lighting on tall buildings. This is what makes the skyline read as a real,
	 * prosperous city rather than a field of lit boxes - but every tower wearing the same
	 * four corner strips is worse than none, so the style is chosen per building: corner
	 * strips, crown-only, a lit crown section, or strips on two faces. Every tall tower
	 * still gets its red aviation light, which is what makes a skyline look inhabited.
	 */
	void AddTowerAccents(TArray<FCitixBoxInstance>& Out, const FVector2D& Center, float Width, float Depth,
		float BaseZ, float TopZ, const FCitixDistrictLightingProfile& Profile, FRandomStream& Rng)
	{
		if (TopZ - BaseZ < 6000.f)
		{
			return;
		}

		// Accent colour lives on the SOLID emissive material, never on a window surface.
		// The window material's pane mask only varies along U, so on a tall narrow box it
		// smears into full-height vertical streaks - the "stretched strips" on tower sides.
		// Solid emissive renders as a clean thin line instead. Gold dominates, cool blue is
		// the minority family. Deliberately not one colour for every tower.
		const float AccentRoll = Rng.FRand();
		const ECitixSurface AccentColour = (AccentRoll < 0.72f)
			? ECitixSurface::EmissiveWarm : ECitixSurface::EmissiveCool;

		const float StripW = FMath::Max(24.f, FMath::Min(Width, Depth) * 0.030f);
		const float HalfW = Width * 0.5f + 2.f;
		const float HalfD = Depth * 0.5f + 2.f;
		const float TotalH = TopZ - BaseZ;

		const int32 Style = Rng.RandRange(0, 3);

		// Short corner ticks in the crown zone only - never full-height strips. A glowing
		// line running 90% of a tower is what read as a stretched stripe down its side.
		if (Style == 0 || Style == 3)
		{
			const float StripH = TotalH * (Style == 0 ? 0.30f : 0.18f);
			const float StripZ = TopZ - StripH * 0.5f - TotalH * 0.04f;
			const float Corners[4][2] = { { 1.f, 1.f }, { 1.f, -1.f }, { -1.f, 1.f }, { -1.f, -1.f } };
			const int32 Count = (Style == 0) ? 4 : 2;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				AddBox(Out, FVector(Center.X + Corners[Index][0] * HalfW, Center.Y + Corners[Index][1] * HalfD, StripZ),
					FVector(StripW, StripW, StripH), AccentColour);
			}
		}

		// A lit crown: either a thin ring at the very top, or a whole glowing top section.
		if (Style == 2)
		{
			const float CrownH = TotalH * Rng.FRandRange(0.10f, 0.20f);
			AddBox(Out, FVector(Center.X, Center.Y, TopZ - CrownH * 0.5f),
				FVector(Width * 1.015f, Depth * 1.015f, CrownH), AccentColour);
		}
		else
		{
			AddBox(Out, FVector(Center.X, Center.Y, TopZ - 90.f),
				FVector(Width * 1.05f, Depth * 1.05f, 120.f), AccentColour);
		}

		// Aviation warning light: every tall roof, so the skyline reads as inhabited.
		AddSphere(Out, FVector(Center.X, Center.Y, TopZ + 60.f), 130.f, ECitixSurface::TailLight);
	}

	/** A stepped crown: two smaller rings plus a spire. */
	void AddCrown(TArray<FCitixBoxInstance>& Out, const FVector2D& Center, float Width, float Depth,
		float TopZ, float FloorHeight, FRandomStream& Rng, const FFacade& Facade)
	{
		const float Ring1H = FloorHeight * 1.6f;
		AddBox(Out, FVector(Center.X, Center.Y, TopZ + Ring1H * 0.5f),
			FVector(Width * 1.06f, Depth * 1.06f, Ring1H), Facade.Accent);

		const float Ring2H = FloorHeight * 1.1f;
		AddBox(Out, FVector(Center.X, Center.Y, TopZ + Ring1H + Ring2H * 0.5f),
			FVector(Width * 0.72f, Depth * 0.72f, Ring2H), Facade.Accent);

		const float CrownTop = TopZ + Ring1H + Ring2H;

		// Spires are a signature, not a default. At the old 60% nearly every crowned
		// support tower grew a mast, which is exactly the "too many competing spires"
		// that flattened the skyline. Most support towers now end in a flat crown ring;
		// the four heroes own the pointed silhouettes.
		if (Rng.FRand() < 0.25f)
		{
			const float SpireH = FMath::Max(600.f, Width * 0.9f);
			AddCone(Out, FVector(Center.X, Center.Y, CrownTop + SpireH * 0.5f),
				Width * 0.45f, SpireH, ECitixSurface::Spire);
		}
		else
		{
			const float MastH = FMath::Max(900.f, Width * 1.4f);
			AddBox(Out, FVector(Center.X, Center.Y, CrownTop + MastH * 0.5f),
				FVector(90.f, 90.f, MastH), ECitixSurface::Spire);
		}
	}

	/** Rooftop mechanical clutter. */
	void AddRoofClutter(TArray<FCitixBoxInstance>& Out, const FVector2D& Center, float Width, float Depth,
		float TopZ, FRandomStream& Rng, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float EquipmentW = Rng.FRandRange(Width * 0.10f, Width * 0.22f);
			const float EquipmentD = Rng.FRandRange(Depth * 0.10f, Depth * 0.22f);
			const float EquipmentH = Rng.FRandRange(140.f, 340.f);
			const float X = Center.X + Rng.FRandRange(-Width * 0.3f, Width * 0.3f);
			const float Y = Center.Y + Rng.FRandRange(-Depth * 0.3f, Depth * 0.3f);
			AddBox(Out, FVector(X, Y, TopZ + EquipmentH * 0.5f),
				FVector(EquipmentW, EquipmentD, EquipmentH), ECitixSurface::RoofDark);
		}
	}

	/** Street-level shopfront band for walkable districts. */
	void AddStorefront(TArray<FCitixBoxInstance>& Out, const FVector2D& Center, float Width, float Depth,
		float BaseZ, float FloorHeight, const FCitixDistrictLightingProfile& Profile, FRandomStream& Rng,
		bool bLobby = false)
	{
		// Street level is lit, but it is not a uniform bright band on every building. Most
		// ground floors are mid or dim glazing and some are dark; only a minority burn
		// bright. The lobby variant is the brightest, so landmark podiums own the strong
		// street-level light and ordinary blocks stay quieter.
		const float StoreH = FloorHeight * (bLobby ? 0.90f : 0.72f);
		const float Roll = Rng.FRand();
		const float BrightCut = bLobby ? 0.35f : 0.18f;
		const float MidCut = bLobby ? 0.70f : 0.50f;
		const float LitCut = bLobby ? 0.90f : 0.82f;
		ECitixSurface Surface;
		if (Roll < BrightCut)
		{
			Surface = ECitixSurface::WindowWarmBright;
		}
		else if (Roll < MidCut)
		{
			Surface = (Rng.FRand() < Profile.CoolChance)
				? ECitixSurface::WindowCoolMid : ECitixSurface::WindowWarmMid;
		}
		else if (Roll < LitCut)
		{
			Surface = PickWindowSurface(Profile, Rng);
		}
		else
		{
			Surface = ECitixSurface::WindowOff;
		}
		AddBox(Out, FVector(Center.X, Center.Y, BaseZ + StoreH * 0.5f),
			FVector(Width * 1.02f, Depth * 1.02f, StoreH), Surface);

		// A focal entrance / canopy glow on a minority of frontages, not all of them.
		if (Rng.FRand() < (bLobby ? 0.55f : 0.28f))
		{
			const ECitixSurface Entrance = (bLobby || Rng.FRand() < 0.5f)
				? ECitixSurface::EmissiveWarm : ECitixSurface::WindowWarmBright;
			const float EntranceW = FMath::Min(Width * 0.26f, 600.f);
			AddBox(Out, FVector(Center.X, Center.Y - Depth * 0.5f - 30.f, BaseZ + FloorHeight * 0.30f),
				FVector(EntranceW, 55.f, FloorHeight * 0.50f), Entrance);
		}

		// A few lit sign boxes along the frontage.
		const int32 Signs = FMath::Clamp(FMath::RoundToInt(Width / 900.f), 1, 4);
		for (int32 Index = 0; Index < Signs; ++Index)
		{
			const float Offset = (static_cast<float>(Index) + 0.5f) / static_cast<float>(Signs) - 0.5f;
			const ECitixSurface SignSurface = (Rng.FRand() < 0.5f)
				? ECitixSurface::EmissiveWarm : ECitixSurface::EmissiveCool;
			AddBox(Out, FVector(Center.X + Offset * Width, Center.Y - Depth * 0.5f - 40.f, BaseZ + FloorHeight * 1.15f),
				FVector(Width * 0.16f, 26.f, FloorHeight * 0.42f), SignSurface);
		}
	}

	/** Two trees for a courtyard / park patch. */
	void AddTrees(TArray<FCitixBoxInstance>& Out, const FVector2D& Center, float Radius, float BaseZ,
		int32 Count, FRandomStream& Rng)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float X = Center.X + Rng.FRandRange(-Radius, Radius);
			const float Y = Center.Y + Rng.FRandRange(-Radius, Radius);
			const float Scale = Rng.FRandRange(0.9f, 1.4f);
			const float TrunkH = 420.f * Scale;
			AddBox(Out, FVector(X, Y, BaseZ + TrunkH * 0.5f),
				FVector(34.f * Scale, 34.f * Scale, TrunkH), ECitixSurface::Trunk);
			AddSphere(Out, FVector(X, Y, BaseZ + TrunkH + 130.f * Scale),
				360.f * Scale, ECitixSurface::Foliage);
		}
	}
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

ECitixSurface FCitixBuildingGenerator::GetFacadeSurface(ECitixDistrict District, int32 Style)
{
	switch (District)
	{
	case ECitixDistrict::Financial:
	{
		// Weighted toward dark blue/teal glass and metal. The lit window ribbons are the
		// bright element; the mass they sit on has to be dark, so pale concrete and gold
		// facades are the exception rather than one tower in three.
		static const ECitixSurface Palette[] = {
			ECitixSurface::GlassBlue, ECitixSurface::GlassDark, ECitixSurface::GlassGreen,
			ECitixSurface::GlassBlue, ECitixSurface::FacadeMetal, ECitixSurface::GlassGold };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Downtown:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeConcrete, ECitixSurface::FacadeWhite, ECitixSurface::GlassBlue,
			ECitixSurface::GlassGold, ECitixSurface::FacadeMetal, ECitixSurface::FacadeBeige };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::OldTown:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeBrick, ECitixSurface::FacadeBeige, ECitixSurface::FacadeConcrete };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Residential:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeBeige, ECitixSurface::FacadeConcrete, ECitixSurface::FacadeWhite,
			ECitixSurface::FacadeBrick };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Industrial:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeMetal, ECitixSurface::FacadeConcrete, ECitixSurface::FacadeWhite };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Market:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeBrick, ECitixSurface::FacadeBeige, ECitixSurface::EmissiveWarm,
			ECitixSurface::FacadeConcrete };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Commercial:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::GlassBlue, ECitixSurface::FacadeWhite, ECitixSurface::FacadeConcrete,
			ECitixSurface::GlassGold, ECitixSurface::FacadeMetal };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Waterfront:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::GlassBlue, ECitixSurface::FacadeWhite, ECitixSurface::GlassGold,
			ECitixSurface::FacadeMetal };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::University:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeBeige, ECitixSurface::FacadeBrick, ECitixSurface::FacadeWhite };
		return Pick(Palette, Style);
	}
	case ECitixDistrict::Parkland:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeBeige, ECitixSurface::FacadeConcrete };
		return Pick(Palette, Style);
	}
	default:
	{
		static const ECitixSurface Palette[] = {
			ECitixSurface::FacadeConcrete, ECitixSurface::FacadeBeige, ECitixSurface::FacadeWhite };
		return Pick(Palette, Style);
	}
	}
}

ECitixSurface FCitixBuildingGenerator::GetAccentSurface(ECitixDistrict District, int32 Style)
{
	switch (District)
	{
	case ECitixDistrict::Financial:
		return (Style % 2 == 0) ? ECitixSurface::GlassDark : ECitixSurface::FacadeMetal;
	case ECitixDistrict::Downtown:
		return (Style % 2 == 0) ? ECitixSurface::GlassDark : ECitixSurface::FacadeConcrete;
	case ECitixDistrict::OldTown:
		return (Style % 3 == 0) ? ECitixSurface::EmissiveWarm : ECitixSurface::FacadeBrick;
	default:
		return ECitixSurface::GlassDark;
	}
}

namespace
{
	/** Bands are warm-lit gold or cool-blue glazing depending on district and luck. */
	/**
	 * Choose a window lighting variant for one band, driven by the district profile.
	 * Lit vs dark is a straight roll, then brightness and warm/cool are biased by the
	 * district �?so the financial core is bright and mixed while industry is sparse.
	 */
	ECitixSurface PickWindowSurface(const FCitixDistrictLightingProfile& Profile, FRandomStream& Rng)
	{
		if (Rng.FRand() > Profile.LitChance)
		{
			return ECitixSurface::WindowOff;
		}

		const bool bCool = Rng.FRand() < Profile.CoolChance;
		const float Roll = Rng.FRand();
		const float Brightness = Profile.BrightnessBias;

		// Brightness bias shifts how often each of the three brightness tiers appears.
		const float BrightThreshold = FMath::Lerp(0.28f, 0.62f, Brightness);
		const float MidThreshold = FMath::Lerp(0.70f, 0.94f, Brightness);

		if (Roll < BrightThreshold)
		{
			return bCool ? ECitixSurface::WindowCoolBright : ECitixSurface::WindowWarmBright;
		}
		if (Roll < MidThreshold)
		{
			return bCool ? ECitixSurface::WindowCoolMid : ECitixSurface::WindowWarmMid;
		}
		return bCool ? ECitixSurface::WindowCoolDim : ECitixSurface::WindowWarmDim;
	}

	FFacade MakeFacade(const FCitixBuildingSpec& Spec, FRandomStream& Rng)
	{
		FFacade Facade;
		Facade.Body = FCitixBuildingGenerator::GetFacadeSurface(Spec.District, Spec.FacadeStyle);
		Facade.Accent = FCitixBuildingGenerator::GetAccentSurface(Spec.District, Spec.FacadeStyle);
		Facade.Lighting = UCitixCitySettings::Get().GetLightingProfile(Spec.District);

		// Per-BUILDING character, not just per district. Two towers on the same street should
		// not be equally full of light: one is a busy office (dense, cool, even), the next is
		// half let (sparser, warmer, patchier). Perturbing the district profile here is free -
		// it is a copy, and every window roll already reads it.
		Facade.Lighting.LitChance = FMath::Clamp(
			Facade.Lighting.LitChance * Rng.FRandRange(0.72f, 1.12f) * Spec.NightLightScale, 0.06f, 0.99f);
		Facade.Lighting.BrightnessBias = FMath::Clamp(
			(Facade.Lighting.BrightnessBias + Rng.FRandRange(-0.14f, 0.14f)) * Spec.NightLightScale, 0.04f, 0.95f);
		Facade.Lighting.CoolChance = FMath::Clamp(
			Facade.Lighting.CoolChance + Rng.FRandRange(-0.12f, 0.12f), 0.f, 1.f);
		// A support building shadowed by a hero also keeps fewer of the decorative
		// perimeter/crown accents, so the hero owns the only strong outline nearby.
		Facade.Lighting.AccentChance = FMath::Clamp(
			Facade.Lighting.AccentChance * Spec.NightLightScale, 0.f, 1.f);

		// Used for crown/ring detail that is not per-floor glazing.
		Facade.Band = PickWindowSurface(Facade.Lighting, Rng);
		return Facade;
	}
}

// ---------------------------------------------------------------------------
// Archetype selection
// ---------------------------------------------------------------------------

ECitixBuildingArchetype FCitixBuildingGenerator::PickArchetype(ECitixDistrict District, FRandomStream& Rng)
{
	// Weights indexed by ECitixBuildingArchetype order.
	//                             Box  Pod  Set  Twi  Cro  Hol  Twi  Cyl  Spi  Cou  Apt  Sho  War
	static const int32 Financial[] = { 10,  16,  14,  10,  12,   8,   8,   8,   8,   0,   0,   0,   4 };
	static const int32 Downtown[] = { 18,  22,  12,   4,  12,   7,   8,   6,   5,   4,   4,   0,   2 };
	static const int32 OldTown[] = { 16,   0,   0,   0,   2,   0,   0,   0,   0,  22,  12,  40,   8 };
	static const int32 Residential[] = { 14,   8,   2,   0,   4,   0,   6,   0,   0,  30,  34,   2,   6 };
	static const int32 Industrial[] = { 18,   6,   2,   0,   2,   0,  10,   0,   0,   6,   4,   0,  52 };
	static const int32 OuterCity[] = { 24,  10,   4,   0,   4,   0,   8,   0,   2,  16,  18,   0,  22 };
	static const int32 Parkland[] = { 34,   0,   0,   0,   0,   0,   0,   0,   0,  30,  14,   6,  16 };
	static const int32 Waterfront[] = { 16,  22,  12,   4,  14,   6,  10,   6,   4,   4,   4,   0,   8 };
	static const int32 University[] = { 10,   4,   2,   0,   4,   0,   2,   0,   0,  46,  26,   0,   6 };
	static const int32 Market[] = { 12,   0,   0,   0,   2,   0,   0,   0,   0,  16,   8,  58,   4 };
	static const int32 Commercial[] = { 14,  20,  12,   6,  14,   8,  10,   6,   4,   4,   2,   0,   0 };

	const int32* Weights = OuterCity;
	switch (District)
	{
	case ECitixDistrict::Financial:   Weights = Financial; break;
	case ECitixDistrict::Downtown:    Weights = Downtown; break;
	case ECitixDistrict::Commercial:  Weights = Commercial; break;
	case ECitixDistrict::OldTown:     Weights = OldTown; break;
	case ECitixDistrict::Residential: Weights = Residential; break;
	case ECitixDistrict::Industrial:  Weights = Industrial; break;
	case ECitixDistrict::Parkland:    Weights = Parkland; break;
	case ECitixDistrict::Waterfront:  Weights = Waterfront; break;
	case ECitixDistrict::University:  Weights = University; break;
	case ECitixDistrict::Market:      Weights = Market; break;
	default:                          Weights = OuterCity; break;
	}

	int32 Total = 0;
	for (int32 Index = 0; Index < static_cast<int32>(ECitixBuildingArchetype::Count); ++Index)
	{
		Total += Weights[Index];
	}
	int32 Roll = Rng.RandRange(1, FMath::Max(1, Total));
	for (int32 Index = 0; Index < static_cast<int32>(ECitixBuildingArchetype::Count); ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0)
		{
			return static_cast<ECitixBuildingArchetype>(Index);
		}
	}
	return ECitixBuildingArchetype::Box;
}

bool FCitixBuildingGenerator::IsLandmarkArchetype(ECitixBuildingArchetype Archetype)
{
	switch (Archetype)
	{
	case ECitixBuildingArchetype::TwistedTower:
	case ECitixBuildingArchetype::HoledSlab:
	case ECitixBuildingArchetype::SetbackTower:
	case ECitixBuildingArchetype::CrownedTower:
	case ECitixBuildingArchetype::CylinderTower:
	case ECitixBuildingArchetype::SpireTower:
		return true;
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void FCitixBuildingGenerator::GenerateBuildingBoxes(const FCitixBuildingSpec& Spec,
	const UCitixCitySettings& Settings, FRandomStream& Rng, TArray<FCitixBoxInstance>& OutBoxes)
{
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float BaseZ = Spec.BaseZ;

	switch (Spec.Usage)
	{
	case ECitixLotUsage::Empty:
		return;

	case ECitixLotUsage::Plaza:
		AddBox(OutBoxes, FVector(C.X, C.Y, BaseZ + 3.f), FVector(W, D, 6.f), ECitixSurface::Sidewalk);
		AddTrees(OutBoxes, C, FMath::Min(W, D) * 0.3f, BaseZ, 2, Rng);
		return;

	case ECitixLotUsage::Park:
		AddBox(OutBoxes, FVector(C.X, C.Y, BaseZ + 2.f), FVector(W, D, 4.f), ECitixSurface::Grass);
		AddTrees(OutBoxes, C, FMath::Min(W, D) * 0.38f, BaseZ, 4, Rng);
		return;

	case ECitixLotUsage::Parking:
		AddBox(OutBoxes, FVector(C.X, C.Y, BaseZ + 2.f), FVector(W, D, 4.f), ECitixSurface::AsphaltDark);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const float X = C.X + Rng.FRandRange(-W * 0.35f, W * 0.35f);
			const float Y = C.Y + Rng.FRandRange(-D * 0.35f, D * 0.35f);
			AddBox(OutBoxes, FVector(X, Y, BaseZ + 30.f), FVector(220.f, 220.f, 60.f), ECitixSurface::Curb);
		}
		return;

	default:
		break;
	}

	if (Spec.LandmarkStyle != ECitixLandmarkStyle::None)
	{
		switch (Spec.LandmarkStyle)
		{
		case ECitixLandmarkStyle::PearlBroadcastTower: GeneratePearlBroadcastTower(Spec, Rng, OutBoxes); return;
		case ECitixLandmarkStyle::TwistingSupertall: GenerateTwistingSupertall(Spec, Rng, OutBoxes); return;
		case ECitixLandmarkStyle::CrownOpeningTower: GenerateCrownOpeningTower(Spec, Rng, OutBoxes); return;
		case ECitixLandmarkStyle::TieredCrownTower: GenerateTieredCrownTower(Spec, Rng, OutBoxes); return;
		default: break;
		}
	}

	// Decorative night lighting for towers: corner light strips, a lit crown ring and
	// a red aviation light. Placed on the archetype's effective shaft width so the
	// strips sit on the tower rather than floating off a podium.
	const FCitixDistrictLightingProfile& LightingProfile = Settings.GetLightingProfile(Spec.District);

	// Every building gets a lit ground floor - a lobby for offices, shopfronts for
	// walkable districts - because street level is what the player actually drives past,
	// and it is the strongest signal that the city is occupied. Archetypes that shape
	// their own frontage (a courtyard bar, an apartment slab) place it themselves.
	const bool bHandlesOwnGroundFloor =
		(Spec.Archetype == ECitixBuildingArchetype::Courtyard
			|| Spec.Archetype == ECitixBuildingArchetype::ApartmentSlab);
	if (!bHandlesOwnGroundFloor)
	{
		const float GroundFloorH = FMath::Max(320.f, Settings.GetDistrictRule(Spec.District).FloorHeight);
		const bool bWalkable = (Spec.District == ECitixDistrict::OldTown
			|| Spec.District == ECitixDistrict::Downtown
			|| Spec.District == ECitixDistrict::Residential
			|| Spec.District == ECitixDistrict::Commercial
			|| Spec.District == ECitixDistrict::Market
			|| Spec.District == ECitixDistrict::Waterfront
			|| Spec.District == ECitixDistrict::University);
		if (bWalkable)
		{
			AddStorefront(OutBoxes, C, W, D, Spec.BaseZ, GroundFloorH, LightingProfile, Rng);
		}
		else
		{
			// A tower lobby: a narrower, cooler, brighter band at the entrance.
			AddStorefront(OutBoxes, C, W * 0.55f, D * 0.98f, Spec.BaseZ, GroundFloorH,
				LightingProfile, Rng, /*bLobby*/ true);
		}
	}

	if (Rng.FRand() < LightingProfile.AccentChance)
	{
		float AccentShaftScale = 1.f;
		switch (Spec.Archetype)
		{
		case ECitixBuildingArchetype::PodiumTower:   AccentShaftScale = 0.62f; break;
		case ECitixBuildingArchetype::CrownedTower:  AccentShaftScale = 0.78f; break;
		case ECitixBuildingArchetype::SetbackTower:  AccentShaftScale = 0.55f; break;
		case ECitixBuildingArchetype::TwistedTower:  AccentShaftScale = 0.62f; break;
		case ECitixBuildingArchetype::SpireTower:    AccentShaftScale = 0.50f; break;
		case ECitixBuildingArchetype::TwinTower:     AccentShaftScale = 0.90f; break;
		case ECitixBuildingArchetype::CylinderTower: AccentShaftScale = 0.80f; break;
		default:                                     AccentShaftScale = 1.f; break;
		}
		AddTowerAccents(OutBoxes, C, W * AccentShaftScale, D * AccentShaftScale,
			Spec.BaseZ, Spec.BaseZ + Spec.Height(), LightingProfile, Rng);
	}

	switch (Spec.Archetype)
	{
	case ECitixBuildingArchetype::PodiumTower:   GeneratePodiumTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::SetbackTower:  GenerateSetbackTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::TwistedTower:  GenerateTwistedTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::CrownedTower:  GenerateCrownedTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::HoledSlab:     GenerateHoledSlab(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::TwinTower:     GenerateTwinTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::CylinderTower: GenerateCylinderTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::SpireTower:    GenerateSpireTower(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::Courtyard:     GenerateCourtyard(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::ApartmentSlab: GenerateApartmentSlab(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::Shophouse:     GenerateShophouse(Spec, Rng, OutBoxes); return;
	case ECitixBuildingArchetype::Warehouse:     GenerateWarehouse(Spec, Rng, OutBoxes); return;
	default:                                     GenerateBox(Spec, Rng, OutBoxes); return;
	}
}

// ---------------------------------------------------------------------------
// Archetypes
// ---------------------------------------------------------------------------

void FCitixBuildingGenerator::GeneratePearlBroadcastTower(const FCitixBuildingSpec& Spec,
	FRandomStream& Rng, TArray<FCitixBoxInstance>& Out)
{
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(1200.f, Spec.Footprint.X);
	const float D = FMath::Max(1200.f, Spec.Footprint.Y);
	const float H = Spec.Height();
	const float Base = Spec.BaseZ;
	const float Core = FMath::Min(W, D);
	const float FloorH = FMath::Max(240.f, Spec.FloorHeight);
	const FCitixDistrictLightingProfile& Lighting = UCitixCitySettings::Get().GetLightingProfile(Spec.District);

	// A broadcast tower, not "spheres on a pole": a WIDE built base with four splayed
	// legs and a lit podium, then a slim mast carrying three SMALL, well-spaced faceted
	// bulbs. The bulbs are sized from the height so they stay consistent, but kept small
	// so the gaps and the tower structure read as clearly as the spheres do. They sit on
	// the sphere orb surfaces - a bulb on a window surface would come out a cube.
	const float BulbBig = H * 0.050f;
	const float BulbMid = H * 0.036f;
	const float BulbSmall = H * 0.026f;
	const float MastD = FMath::Max(260.f, Core * 0.17f);

	// Wide support: broad podium, a narrower shaft tier, and four splayed legs.
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.030f), FVector(W * 0.98f, D * 0.98f, H * 0.06f), ECitixSurface::GlassDark);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.115f), FVector(W * 0.68f, D * 0.68f, H * 0.11f), ECitixSurface::GlassDark);
	AddFloorBands(Out, C, W * 0.68f, D * 0.68f, Base + H * 0.06f,
		FMath::Max(1, FMath::RoundToInt(H * 0.11f / FloorH)), FloorH, Lighting, Rng, 1.015f, 0.f, 1);
	{
		const float LegRadius = Core * 0.30f;
		const float LegW = FMath::Max(170.f, Core * 0.11f);
		const float LegH = H * 0.20f;
		const float Corners[4][2] = { { 1.f, 1.f }, { 1.f, -1.f }, { -1.f, 1.f }, { -1.f, -1.f } };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			AddBox(Out, FVector(C.X + Corners[Index][0] * LegRadius, C.Y + Corners[Index][1] * LegRadius,
				Base + H * 0.13f), FVector(LegW, LegW, LegH), ECitixSurface::FacadeMetal);
		}
	}

	// Slim mast: overlapping segments that pass THROUGH each bulb, so the tower is one
	// connected structure. Every joint overlaps by at least 0.02H and each bulb is
	// pierced by the shaft (bulb radius exceeds the local shaft half-width), which is
	// what keeps spheres from reading as floating. Fractions, for H = tower height:
	//   seg1 0.12-0.40, big bulb 0.345-0.395, collar 0.395-0.405,
	//   seg2 0.385-0.60, mid bulb 0.542-0.578, collar 0.595-0.605,
	//   seg3 0.585-0.76, small bulb 0.707-0.733,
	//   antenna 0.72-1.00 seated inside the small bulb, beacon at 1.005.
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.26f), FVector(MastD, MastD, H * 0.28f), ECitixSurface::GlassDark);
	AddSphere(Out, FVector(C.X, C.Y, Base + H * 0.37f), BulbBig, ECitixSurface::OrbPink);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.40f), FVector(MastD * 1.35f, MastD * 1.35f, H * 0.010f), ECitixSurface::FacadeMetal);

	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.4925f), FVector(MastD * 0.66f, MastD * 0.66f, H * 0.215f), ECitixSurface::GlassDark);
	AddSphere(Out, FVector(C.X, C.Y, Base + H * 0.56f), BulbMid, ECitixSurface::OrbCyan);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.60f), FVector(MastD * 0.95f, MastD * 0.95f, H * 0.010f), ECitixSurface::FacadeMetal);

	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.6725f), FVector(MastD * 0.50f, MastD * 0.50f, H * 0.175f), ECitixSurface::GlassDark);
	AddSphere(Out, FVector(C.X, C.Y, Base + H * 0.72f), BulbSmall, ECitixSurface::OrbWarm);

	// Antenna rises out of the small bulb: its base sits a full bulb-radius below the
	// bulb centre, so there is no floating tip.
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.86f), FVector(80.f, 80.f, H * 0.28f), ECitixSurface::Spire);
	AddSphere(Out, FVector(C.X, C.Y, Base + H * 1.005f), 130.f, ECitixSurface::TailLight);
	AddTowerAccents(Out, C, MastD, MastD, Base + H * 0.20f, Base + H * 0.66f, Lighting, Rng);
}

void FCitixBuildingGenerator::GenerateTwistingSupertall(const FCitixBuildingSpec& Spec,
	FRandomStream& Rng, TArray<FCitixBoxInstance>& Out)
{
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(1200.f, Spec.Footprint.X);
	const float D = FMath::Max(1200.f, Spec.Footprint.Y);
	const float H = Spec.Height();
	const float Base = Spec.BaseZ;
	const float FloorH = FMath::Max(240.f, Spec.FloorHeight);
	const FCitixDistrictLightingProfile& Lighting = UCitixCitySettings::Get().GetLightingProfile(Spec.District);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.05f), FVector(W * 0.92f, D * 0.92f, H * 0.10f), ECitixSurface::GlassDark);
	constexpr int32 Segments = 9;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / Segments;
		const float SegmentH = H * 0.095f;
		const float Scale = FMath::Lerp(0.72f, 0.32f, Alpha);
		const float Z = Base + H * 0.10f + SegmentH * (Segment + 0.5f);
		AddBox(Out, FVector(C.X, C.Y, Z), FVector(W * Scale, D * Scale, SegmentH), ECitixSurface::GlassBlue,
			Alpha * 35.f);
		AddFloorBands(Out, C, W, D, Z - SegmentH * 0.5f, FMath::Max(1, FMath::RoundToInt(SegmentH / FloorH)),
			FloorH, Lighting, Rng, Scale * 1.015f, Alpha * 35.f, 1);
	}
	AddCrown(Out, C, W * 0.26f, D * 0.26f, Base + H * 0.96f, FloorH, Rng,
		{ ECitixSurface::GlassBlue, ECitixSurface::WindowGold, ECitixSurface::WindowGold, Lighting });
}

void FCitixBuildingGenerator::GenerateCrownOpeningTower(const FCitixBuildingSpec& Spec,
	FRandomStream& Rng, TArray<FCitixBoxInstance>& Out)
{
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(1400.f, Spec.Footprint.X);
	const float D = FMath::Max(1400.f, Spec.Footprint.Y);
	const float H = Spec.Height();
	const float Base = Spec.BaseZ;
	const float FloorH = FMath::Max(240.f, Spec.FloorHeight);
	const FCitixDistrictLightingProfile& Lighting = UCitixCitySettings::Get().GetLightingProfile(Spec.District);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.07f), FVector(W * 0.95f, D * 0.90f, H * 0.14f), ECitixSurface::GlassDark);
	// Two legs with a wide void between them, so the crown reads as an opening from a
	// distance rather than a solid slab with a notch.
	const float ShoulderW = W * 0.22f;
	const float ShoulderOffset = W * 0.34f;
	const float ShoulderH = H * 0.86f;
	const float OpeningWidth = FMath::Max(80.f, ShoulderOffset * 2.f - ShoulderW);
	for (float Side : { -1.f, 1.f })
	{
		AddBox(Out, FVector(C.X + Side * ShoulderOffset, C.Y, Base + H * 0.14f + ShoulderH * 0.5f),
			FVector(ShoulderW, D * 0.62f, ShoulderH), ECitixSurface::GlassDark);
		AddFloorBands(Out, FVector2D(C.X + Side * ShoulderOffset, C.Y), ShoulderW, D * 0.62f,
			Base + H * 0.14f, FMath::Max(1, FMath::RoundToInt(ShoulderH / FloorH)), FloorH, Lighting, Rng, 1.015f, 0.f, 2);

		// A thin, restrained red-orange strip down the inner edge of each leg: the only
		// warm accent on the tower, and what makes the void legible at night.
		AddBox(Out, FVector(C.X + Side * (ShoulderOffset - ShoulderW * 0.5f - 35.f), C.Y,
			Base + H * 0.14f + ShoulderH * 0.5f),
			FVector(70.f, D * 0.64f, ShoulderH * 0.92f), ECitixSurface::EmissiveWarm);
	}

	const float CrownH = H * 0.16f;
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.88f), FVector(W * 0.92f, D * 0.72f, CrownH), ECitixSurface::FacadeMetal);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.85f), FVector(W * 0.44f, D * 0.70f, H * 0.05f), ECitixSurface::GlassDark);
	AddBox(Out, FVector(C.X, C.Y, Base + H * 0.965f), FVector(OpeningWidth * 1.05f, D * 0.60f, 80.f), ECitixSurface::EmissiveWarm);
}

void FCitixBuildingGenerator::GenerateTieredCrownTower(const FCitixBuildingSpec& Spec,
	FRandomStream& Rng, TArray<FCitixBoxInstance>& Out)
{
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(1300.f, Spec.Footprint.X);
	const float D = FMath::Max(1300.f, Spec.Footprint.Y);
	const float H = Spec.Height();
	const float Base = Spec.BaseZ;
	const float FloorH = FMath::Max(240.f, Spec.FloorHeight);
	const FCitixDistrictLightingProfile& Lighting = UCitixCitySettings::Get().GetLightingProfile(Spec.District);
	const float Heights[] = { H * 0.46f, H * 0.26f, H * 0.18f };
	const float Scales[] = { 0.84f, 0.58f, 0.34f };
	float Cursor = Base;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Heights); ++Index)
	{
		AddBox(Out, FVector(C.X, C.Y, Cursor + Heights[Index] * 0.5f), FVector(W * Scales[Index], D * Scales[Index], Heights[Index]), ECitixSurface::GlassDark);
		AddFloorBands(Out, C, W, D, Cursor, FMath::Max(1, FMath::RoundToInt(Heights[Index] / FloorH)), FloorH,
			Lighting, Rng, Scales[Index] * 1.02f, 0.f, 2);
		Cursor += Heights[Index];
		AddBox(Out, FVector(C.X, C.Y, Cursor + 65.f), FVector(W * Scales[Index] * 1.08f, D * Scales[Index] * 1.08f, 130.f), ECitixSurface::WindowGold);
	}
	AddBox(Out, FVector(C.X, C.Y, Cursor + H * 0.075f), FVector(100.f, 100.f, H * 0.15f), ECitixSurface::Spire);
}

void FCitixBuildingGenerator::GenerateBox(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(1, Spec.Floors);
	const float TotalH = FloorH * Floors;

	float CursorZ = Spec.BaseZ;
	float BodyH = TotalH;

	if (Spec.bPodium && Floors >= 3)
	{
		const float PodiumH = FMath::Min(2.f * FloorH, TotalH * 0.28f);
		AddBox(Out, FVector(C.X, C.Y, CursorZ + PodiumH * 0.5f),
			FVector(W * 1.14f, D * 1.14f, PodiumH), Facade.Body);
		CursorZ += PodiumH;
		BodyH -= PodiumH;
	}

	const float TopZ = Spec.BaseZ + TotalH;
	// Lit floors are scattered, not a continuous ladder: the tall band towers skip every
	// other floor, so the facade reads as room grids and dark glass mass rather than a
	// stack of repeated bright strips.
	const int32 BandStep = (Floors > 44) ? 2 : 1;
	AddBox(Out, FVector(C.X, C.Y, CursorZ + BodyH * 0.5f), FVector(W, D, BodyH), Facade.Body);
	AddFloorBands(Out, C, W, D, Spec.BaseZ, Floors, FloorH, Facade.Lighting, Rng, 1.01f, 0.f, BandStep);

	AddBox(Out, FVector(C.X, C.Y, TopZ + 40.f), FVector(W * 1.04f, D * 1.04f, 80.f), ECitixSurface::Roof);

	if (Spec.bRoofEquipment)
	{
		AddRoofClutter(Out, C, W, D, TopZ + 80.f, Rng, Rng.RandRange(1, 3));
	}
	else if (Rng.FRand() < 0.25f)
	{
		// Occasional rooftop mast.
		const float MastH = Rng.FRandRange(400.f, 1400.f);
		AddBox(Out, FVector(C.X, C.Y, TopZ + MastH * 0.5f), FVector(70.f, 70.f, MastH), ECitixSurface::Spire);
	}

	// The ground floor (shopfront or lobby) is placed once by GenerateBuildingBoxes, so
	// every archetype gets the same street-level treatment.
}

void FCitixBuildingGenerator::GeneratePodiumTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(3, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// Wide retail podium.
	const float PodiumH = FMath::Clamp(TotalH * 0.16f, 2.f * FloorH, 4.f * FloorH);
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + PodiumH * 0.5f),
		FVector(W * 1.3f, D * 1.3f, PodiumH), Facade.Body);
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + PodiumH * 0.55f),
		FVector(W * 1.32f, D * 1.32f, PodiumH * 0.5f), Facade.Band);

	// Slim tower.
	const float TowerH = TotalH - PodiumH;
	const float TowerW = W * 0.62f;
	const float TowerD = D * 0.62f;
	const float TowerBase = Spec.BaseZ + PodiumH;
	AddBox(Out, FVector(C.X, C.Y, TowerBase + TowerH * 0.5f),
		FVector(TowerW, TowerD, TowerH), Facade.Body);

	const int32 BandStep = (Floors > 36) ? 2 : 1;
	AddFloorBands(Out, C, TowerW, TowerD, TowerBase, Floors - 3, FloorH, Facade.Lighting, Rng, 1.012f, 0.f, BandStep);

	AddCrown(Out, C, TowerW, TowerD, Spec.BaseZ + TotalH, FloorH, Rng, Facade);
	AddRoofClutter(Out, C, TowerW * 0.5f, TowerD * 0.5f, Spec.BaseZ + TotalH - FloorH, Rng, 2);
}

void FCitixBuildingGenerator::GenerateSetbackTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(4, Spec.Floors);
	const float TotalH = FloorH * Floors;

	const int32 Steps = FMath::Clamp(Floors / 6, 3, 7);
	float CursorZ = Spec.BaseZ;
	float Remaining = TotalH;

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const float T = static_cast<float>(Step) / static_cast<float>(Steps);
		const float Scale = FMath::Lerp(1.f, 0.42f, T);
		// Lower steps are taller than upper ones.
		const float StepH = Remaining * (0.30f + 0.22f * (1.f - T));
		const float ClampedH = FMath::Min(StepH, Remaining - (Steps - Step - 1) * FloorH * 0.8f);
		const float Height = FMath::Max(FloorH, (Step == Steps - 1) ? Remaining : ClampedH);

		AddBox(Out, FVector(C.X, C.Y, CursorZ + Height * 0.5f),
			FVector(W * Scale, D * Scale, Height), Facade.Body);

		const int32 StepFloors = FMath::Max(1, FMath::RoundToInt(Height / FloorH));
		AddFloorBands(Out, C, W * Scale, D * Scale, CursorZ, StepFloors, FloorH, Facade.Lighting, Rng, 1.012f, 0.f,
			StepFloors > 20 ? 2 : 1);

		CursorZ += Height;
		Remaining -= Height;
		if (Remaining <= FloorH)
		{
			break;
		}
	}

	AddCrown(Out, C, W * 0.45f, D * 0.45f, CursorZ, FloorH, Rng, Facade);
}

void FCitixBuildingGenerator::GenerateTwistedTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(8, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// A stack of segments, each rotated a little further: the signature twist. The twist
	// is deliberately pronounced and the taper continuous, so the tallest hero reads as a
	// twisted, narrowing shaft even in a low-poly silhouette.
	const int32 Segments = FMath::Clamp(Floors / 4, 8, 14);
	const float TotalTwist = Rng.FRandRange(90.f, 170.f) * (Rng.FRand() < 0.5f ? -1.f : 1.f);
	const float SegmentH = TotalH / static_cast<float>(Segments);

	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float T = static_cast<float>(Segment) / static_cast<float>(Segments - 1);
		// Narrow profile, tapering hard toward the top, with the classic rounded shoulders
		// faked by a stepped taper. At skyline distance it is the strong taper, more than
		// the twist, that separates this hero from the rectangular support towers.
		const float Scale = FMath::Lerp(1.f, 0.30f, FMath::Pow(T, 0.62f));
		const float Yaw = TotalTwist * T;
		const float Z = Spec.BaseZ + SegmentH * (static_cast<float>(Segment) + 0.5f);

		AddBox(Out, FVector(C.X, C.Y, Z), FVector(W * Scale, D * Scale, SegmentH * 1.005f),
			Facade.Body, Yaw);

		// Glazing band shared with the segment rotation.
		const int32 SegFloors = FMath::Max(1, FMath::RoundToInt(SegmentH / FloorH));
		AddFloorBands(Out, C, W * Scale, D * Scale, Z - SegmentH * 0.5f, SegFloors, FloorH,
			Facade.Lighting, Rng, 1.012f, Yaw, 1);
	}

	AddCrown(Out, C, W * 0.62f, D * 0.62f, Spec.BaseZ + TotalH, FloorH, Rng, Facade);
}

void FCitixBuildingGenerator::GenerateCrownedTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(4, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// Podium + shaft + an oversized illuminated crown.
	const float PodiumH = FMath::Min(2.f * FloorH, TotalH * 0.18f);
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + PodiumH * 0.5f),
		FVector(W * 1.18f, D * 1.18f, PodiumH), Facade.Body);

	const float ShaftH = TotalH - PodiumH;
	const float ShaftW = W * 0.78f;
	const float ShaftD = D * 0.78f;
	const float ShaftBase = Spec.BaseZ + PodiumH;
	AddBox(Out, FVector(C.X, C.Y, ShaftBase + ShaftH * 0.5f),
		FVector(ShaftW, ShaftD, ShaftH), Facade.Body);
	AddFloorBands(Out, C, ShaftW, ShaftD, ShaftBase, Floors - 2, FloorH, Facade.Lighting, Rng, 1.012f, 0.f,
		Floors > 45 ? 2 : 1);

	// Crown: a wide lit box, then a narrower one, then a spire.
	const float CrownH = FloorH * 2.2f;
	AddBox(Out, FVector(C.X, C.Y, ShaftBase + ShaftH + CrownH * 0.5f),
		FVector(ShaftW * 1.12f, ShaftD * 1.12f, CrownH), Facade.Band);
	AddCrown(Out, C, ShaftW * 0.8f, ShaftD * 0.8f, ShaftBase + ShaftH + CrownH, FloorH, Rng, Facade);
}

void FCitixBuildingGenerator::GenerateHoledSlab(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(8, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// Tapering slab with a large rectangular opening through the crown.
	const float NetH = TotalH * 0.68f;
	const float CrownH = TotalH - NetH;

	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + NetH * 0.5f), FVector(W, D, NetH), Facade.Body);
	AddFloorBands(Out, C, W, D, Spec.BaseZ, FMath::Max(1, FMath::RoundToInt(NetH / FloorH)),
		FloorH, Facade.Lighting, Rng, 1.012f, 0.f, Floors > 45 ? 2 : 1);

	// Crown: two side legs, a bottom sill and a top lintel enclose the opening.
	const float CrownBase = Spec.BaseZ + NetH;
	const float LegW = W * 0.20f;
	const float LegOffset = (W - LegW) * 0.5f;
	const float SillH = CrownH * 0.16f;
	const float LintelH = CrownH * 0.20f;
	const float OpeningH = CrownH - SillH - LintelH;

	AddBox(Out, FVector(C.X, C.Y, CrownBase + SillH * 0.5f), FVector(W, D, SillH), Facade.Body);
	AddBox(Out, FVector(C.X - LegOffset, C.Y, CrownBase + SillH + OpeningH * 0.5f),
		FVector(LegW, D, OpeningH), Facade.Body);
	AddBox(Out, FVector(C.X + LegOffset, C.Y, CrownBase + SillH + OpeningH * 0.5f),
		FVector(LegW, D, OpeningH), Facade.Body);
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH - LintelH * 0.5f),
		FVector(W, D, LintelH), Facade.Body);

	// Warm-lit edge strips make the opening read at distance.
	AddBox(Out, FVector(C.X, C.Y, CrownBase + SillH + OpeningH * 0.5f),
		FVector(W * 1.005f, D * 0.55f, OpeningH * 1.0f), Facade.Band);

	AddCrown(Out, C, W * 0.5f, D * 0.5f, Spec.BaseZ + TotalH, FloorH, Rng, Facade);
}

void FCitixBuildingGenerator::GenerateTwinTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(4, Spec.Floors);
	const float TotalH = FloorH * Floors;

	const float ShaftW = W * 0.36f;
	const float ShaftD = D * 0.72f;
	const float Offset = (W - ShaftW) * 0.5f;

	AddBox(Out, FVector(C.X - Offset, C.Y, Spec.BaseZ + TotalH * 0.5f),
		FVector(ShaftW, ShaftD, TotalH), Facade.Body);
	AddBox(Out, FVector(C.X + Offset, C.Y, Spec.BaseZ + TotalH * 0.5f),
		FVector(ShaftW, ShaftD, TotalH), Facade.Body);

	AddFloorBands(Out, FVector2D(C.X - Offset, C.Y), ShaftW, ShaftD, Spec.BaseZ, Floors, FloorH,
		Facade.Lighting, Rng, 1.014f, 0.f, Floors > 45 ? 2 : 1);
	AddFloorBands(Out, FVector2D(C.X + Offset, C.Y), ShaftW, ShaftD, Spec.BaseZ, Floors, FloorH,
		Facade.Lighting, Rng, 1.014f, 0.f, Floors > 45 ? 2 : 1);

	// Sky bridge linking the shafts.
	const float BridgeZ = Spec.BaseZ + TotalH * Rng.FRandRange(0.55f, 0.72f);
	AddBox(Out, FVector(C.X, C.Y, BridgeZ), FVector(Offset * 2.f + ShaftW, D * 0.5f, FloorH * 1.6f),
		Facade.Accent);
	AddBox(Out, FVector(C.X, C.Y, BridgeZ), FVector(Offset * 2.f + ShaftW * 1.02f, D * 0.52f, FloorH * 0.9f),
		Facade.Band);

	AddCrown(Out, FVector2D(C.X - Offset, C.Y), ShaftW, ShaftD, Spec.BaseZ + TotalH, FloorH, Rng, Facade);
	AddCrown(Out, FVector2D(C.X + Offset, C.Y), ShaftW, ShaftD, Spec.BaseZ + TotalH, FloorH, Rng, Facade);
}

void FCitixBuildingGenerator::GenerateCylinderTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(4, Spec.Floors);
	const float TotalH = FloorH * static_cast<float>(Floors);
	const float BaseZ = Spec.BaseZ;

	const float ShaftDiameter = FMath::Min(W, D) * 0.92f;
	const float ShaftH = FMath::Max(FloorH * 2.f, TotalH - FloorH * 2.f);

	AddCylinder(Out, FVector(C.X, C.Y, BaseZ + ShaftH * 0.5f), ShaftDiameter, ShaftH, ECitixSurface::Tube);

	// Ring bands every few floors.
	const int32 RingStep = FMath::Max(2, Floors / 8);
	for (int32 Floor = RingStep; Floor < Floors - 2; Floor += RingStep)
	{
		const float Z = BaseZ + (static_cast<float>(Floor) + 0.5f) * FloorH;
		AddCylinder(Out, FVector(C.X, C.Y, Z), ShaftDiameter * 1.03f, FloorH * 0.42f, PickWindowSurface(Facade.Lighting, Rng));
	}

	// Podium disc, crown disc and a dome.
	AddCylinder(Out, FVector(C.X, C.Y, BaseZ + FloorH * 0.6f), ShaftDiameter * 1.35f,
		FloorH * 1.2f, Facade.Body);
	AddCylinder(Out, FVector(C.X, C.Y, BaseZ + ShaftH + FloorH * 0.6f), ShaftDiameter * 1.15f,
		FloorH * 1.2f, Facade.Band);

	const float DomeDiameter = ShaftDiameter * Rng.FRandRange(0.7f, 0.95f);
	AddSphere(Out, FVector(C.X, C.Y, BaseZ + ShaftH + FloorH * 1.2f + DomeDiameter * 0.4f),
		DomeDiameter, ECitixSurface::Dome);

	// Occasional mast on top of the dome.
	if (Rng.FRand() < 0.5f)
	{
		const float MastH = FMath::Max(700.f, ShaftDiameter * 1.6f);
		AddBox(Out, FVector(C.X, C.Y, BaseZ + ShaftH + FloorH * 1.2f + DomeDiameter + MastH * 0.5f),
			FVector(80.f, 80.f, MastH), ECitixSurface::Spire);
	}
}

void FCitixBuildingGenerator::GenerateSpireTower(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(4, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// Slim shaft with a very tall mast, echoing a broadcast tower.
	const float ShaftW = W * 0.5f;
	const float ShaftD = D * 0.5f;
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH * 0.5f), FVector(ShaftW, ShaftD, TotalH),
		Facade.Body);
	AddFloorBands(Out, C, ShaftW, ShaftD, Spec.BaseZ, Floors, FloorH, Facade.Lighting, Rng, 1.02f, 0.f,
		Floors > 45 ? 2 : 1);

	// Observation disc part-way up.
	AddCylinder(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH * 1.02f), FMath::Max(ShaftW, ShaftD) * 1.8f,
		FloorH * 1.4f, ECitixSurface::Dome);

	const float MastBase = Spec.BaseZ + TotalH + FloorH * 1.4f;
	const float MastH = TotalH * Rng.FRandRange(0.45f, 0.75f);
	AddBox(Out, FVector(C.X, C.Y, MastBase + MastH * 0.5f), FVector(110.f, 110.f, MastH),
		ECitixSurface::Spire);
	AddCone(Out, FVector(C.X, C.Y, MastBase + MastH + 300.f), 160.f, 600.f, ECitixSurface::Spire);
}

void FCitixBuildingGenerator::GenerateCourtyard(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(1, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// Perimeter bars with an open courtyard in the middle.
	const float BarDepth = FMath::Min(W, D) * 0.26f;
	AddBox(Out, FVector(C.X, C.Y - (D - BarDepth) * 0.5f, Spec.BaseZ + TotalH * 0.5f),
		FVector(W, BarDepth, TotalH), Facade.Body);
	AddBox(Out, FVector(C.X, C.Y + (D - BarDepth) * 0.5f, Spec.BaseZ + TotalH * 0.5f),
		FVector(W, BarDepth, TotalH), Facade.Body);
	AddBox(Out, FVector(C.X - (W - BarDepth) * 0.5f, C.Y, Spec.BaseZ + TotalH * 0.5f),
		FVector(BarDepth, D - BarDepth * 2.f, TotalH), Facade.Body);
	AddBox(Out, FVector(C.X + (W - BarDepth) * 0.5f, C.Y, Spec.BaseZ + TotalH * 0.5f),
		FVector(BarDepth, D - BarDepth * 2.f, TotalH), Facade.Body);

	// Courtyard surface and planting.
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + 2.f),
		FVector(W - BarDepth * 2.f, D - BarDepth * 2.f, 4.f), ECitixSurface::Grass);
	AddTrees(Out, C, FMath::Min(W, D) * 0.18f, Spec.BaseZ, 3, Rng);

	// Shopfront band on the two long faces only.
	AddStorefront(Out, FVector2D(C.X, C.Y - (D - BarDepth) * 0.5f), W, BarDepth, Spec.BaseZ, FloorH, Facade.Lighting, Rng);
	AddStorefront(Out, FVector2D(C.X, C.Y + (D - BarDepth) * 0.5f), W, BarDepth, Spec.BaseZ, FloorH, Facade.Lighting, Rng);
}

void FCitixBuildingGenerator::GenerateApartmentSlab(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(2, Spec.Floors);
	const float TotalH = FloorH * Floors;

	const float SlabD = D * 0.62f;
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH * 0.5f), FVector(W, SlabD, TotalH), Facade.Body);

	// Balcony rows on both long faces.
	const int32 BalconyFloors = FMath::Min(Floors, 22);
	const float BalconyW = FMath::Max(240.f, W * 0.16f);
	const float BalconyD = D * 0.10f;
	for (int32 Floor = 1; Floor <= BalconyFloors; ++Floor)
	{
		const float Z = Spec.BaseZ + (static_cast<float>(Floor) + 0.25f) * FloorH;
		const float BalconyZ = Z - FloorH * 0.1f;
		for (int32 Bay = 0; Bay < 3; ++Bay)
		{
			const float X = C.X + (static_cast<float>(Bay) - 1.f) * (W * 0.30f);
			AddBox(Out, FVector(X, C.Y - (SlabD * 0.5f + BalconyD * 0.5f), BalconyZ),
				FVector(BalconyW, BalconyD, FloorH * 0.42f), Facade.Accent);
			AddBox(Out, FVector(X, C.Y + (SlabD * 0.5f + BalconyD * 0.5f), BalconyZ),
				FVector(BalconyW, BalconyD, FloorH * 0.42f), Facade.Accent);
		}
	}

	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH + 40.f),
		FVector(W * 1.03f, SlabD * 1.04f, 80.f), ECitixSurface::Roof);
	AddRoofClutter(Out, C, W * 0.6f, SlabD * 0.6f, Spec.BaseZ + TotalH + 80.f, Rng, 3);
	AddStorefront(Out, C, W, SlabD, Spec.BaseZ, FloorH, Facade.Lighting, Rng);
}

void FCitixBuildingGenerator::GenerateShophouse(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);

	// A row of narrow units with individually varied heights: dense old-town fabric.
	const int32 Units = FMath::Clamp(FMath::RoundToInt(W / 600.f), 2, 7);
	const float UnitW = W / static_cast<float>(Units);
	const float UnitD = D * 0.92f;

	for (int32 Unit = 0; Unit < Units; ++Unit)
	{
		const float X = C.X + ((static_cast<float>(Unit) + 0.5f) / static_cast<float>(Units) - 0.5f) * W;
		const int32 UnitFloors = FMath::Clamp(Spec.Floors + Rng.RandRange(-1, 2), 2, 8);
		const float UnitH = FloorH * UnitFloors;
		const float UnitW2 = UnitW * 0.94f;

		AddBox(Out, FVector(X, C.Y, Spec.BaseZ + UnitH * 0.5f), FVector(UnitW2, UnitD, UnitH), Facade.Body);

		// Shopfront + signage band.
		AddBox(Out, FVector(X, C.Y, Spec.BaseZ + FloorH * 0.45f),
			FVector(UnitW2 * 1.01f, UnitD * 1.02f, FloorH * 0.8f),
			(Rng.FRand() < 0.45f) ? ECitixSurface::EmissiveWarm : ECitixSurface::GlassDark);

		// Air-conditioners and a small roof lip.
		AddBox(Out, FVector(X, C.Y, Spec.BaseZ + UnitH + 25.f),
			FVector(UnitW2 * 1.06f, UnitD * 1.06f, 50.f), ECitixSurface::Roof);
		if (Rng.FRand() < 0.6f)
		{
			const float ACz = Spec.BaseZ + FloorH * Rng.FRandRange(1.2f, static_cast<float>(UnitFloors));
			AddBox(Out, FVector(X + UnitW2 * 0.5f + 40.f, C.Y + Rng.FRandRange(-UnitD * 0.3f, UnitD * 0.3f), ACz),
				FVector(140.f, 90.f, 80.f), ECitixSurface::PropMetal);
		}
	}
}

void FCitixBuildingGenerator::GenerateWarehouse(const FCitixBuildingSpec& Spec, FRandomStream& Rng,
	TArray<FCitixBoxInstance>& Out)
{
	const FFacade Facade = MakeFacade(Spec, Rng);
	const FVector2D C = Spec.Center;
	const float W = FMath::Max(100.f, Spec.Footprint.X);
	const float D = FMath::Max(100.f, Spec.Footprint.Y);
	const float FloorH = FMath::Max(200.f, Spec.FloorHeight);
	const int32 Floors = FMath::Max(1, Spec.Floors);
	const float TotalH = FloorH * Floors;

	// Long low shed.
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH * 0.5f), FVector(W, D, TotalH), Facade.Body);
	AddBox(Out, FVector(C.X, C.Y, Spec.BaseZ + TotalH * 0.5f),
		FVector(W * 1.004f, D * 0.30f, TotalH * 0.55f), Facade.Band);

	// Sawtooth roof: angled panels along the length.
	const int32 Teeth = FMath::Clamp(FMath::RoundToInt(W / 2200.f), 2, 8);
	const float ToothW = W / static_cast<float>(Teeth);
	for (int32 Tooth = 0; Tooth < Teeth; ++Tooth)
	{
		const float X = C.X + ((static_cast<float>(Tooth) + 0.5f) / static_cast<float>(Teeth) - 0.5f) * W;
		AddBoxPitched(Out, FVector(X, C.Y, Spec.BaseZ + TotalH + 90.f),
			FVector(ToothW * 1.05f, D * 1.04f, 60.f), ECitixSurface::Roof, -18.f);
	}

	// Roller doors on the long faces.
	for (int32 Door = 0; Door < 2; ++Door)
	{
		const float X = C.X + (static_cast<float>(Door) - 0.5f) * (W * 0.45f);
		AddBox(Out, FVector(X, C.Y - D * 0.5f - 6.f, Spec.BaseZ + FloorH * 0.5f),
			FVector(600.f, 12.f, FloorH), ECitixSurface::Trim);
		AddBox(Out, FVector(X, C.Y + D * 0.5f + 6.f, Spec.BaseZ + FloorH * 0.5f),
			FVector(600.f, 12.f, FloorH), ECitixSurface::Trim);
	}
}
