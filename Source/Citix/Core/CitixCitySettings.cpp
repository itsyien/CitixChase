// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/CitixCitySettings.h"

UCitixCitySettings::UCitixCitySettings()
{
	PopulateDefaults();
}

void UCitixCitySettings::PopulateDefaults()
{
	if (RoadRules.Num() > 0 || DistrictRules.Num() > 0)
	{
		return;
	}

	auto AddRoad = [this](ECitixRoadClass Class, float LaneWidth, int32 Lanes, float Sidewalk, float Median)
	{
		FCitixRoadClassRule Rule;
		Rule.Class = Class;
		Rule.Spec.LaneWidth = LaneWidth;
		Rule.Spec.NumLanes = Lanes;
		Rule.Spec.SidewalkWidth = Sidewalk;
		Rule.Spec.MedianWidth = Median;
		RoadRules.Add(Rule);
	};

	// Alleys are pedestrian-scale service lanes: no sidewalks.
	AddRoad(ECitixRoadClass::Alley,	250.f, 1,   0.f,   0.f);
	AddRoad(ECitixRoadClass::Local,		320.f, 2,   300.f, 0.f);
	AddRoad(ECitixRoadClass::Collector,	340.f, 4,   450.f, 0.f);
	AddRoad(ECitixRoadClass::Arterial,	350.f, 6,   600.f, 200.f);
	// Grand tree-lined avenue: wide, with a planted median and broad pavements.
	AddRoad(ECitixRoadClass::Boulevard,	360.f, 6,   900.f, 600.f);
	AddRoad(ECitixRoadClass::Highway,	370.f, 8,   0.f,   500.f);

	auto AddDistrict = [this](ECitixDistrict D, float MinH, float MaxH, float FloorH,
		float LotMin, float LotMax, float Coverage, float Empty, float Park,
		float Podium, float RoofEq, float Tree, float Detail)
	{
		FCitixDistrictRule R;
		R.District = D;
		R.MinHeight = MinH;
		R.MaxHeight = MaxH;
		R.FloorHeight = FloorH;
		R.LotMinSize = LotMin;
		R.LotMaxSize = LotMax;
		R.BuildCoverage = Coverage;
		R.EmptyLotChance = Empty;
		R.ParkChance = Park;
		R.PodiumChance = Podium;
		R.RoofEquipmentChance = RoofEq;
		R.TreeChance = Tree;
		R.DetailLevel = Detail;
		DistrictRules.Add(R);
	};

	//            district                     minH    maxH   floor  lotMin lotMax cov  empty park podium roof tree detail
	AddDistrict(ECitixDistrict::Financial,	12000.f, 36000.f, 400.f, 3000.f, 6500.f, 0.70f, 0.02f, 0.01f, 0.60f, 0.75f, 0.05f, 0.95f);
	AddDistrict(ECitixDistrict::Downtown,	4000.f,  18500.f, 390.f, 1900.f, 4500.f, 0.80f, 0.05f, 0.02f, 0.40f, 0.50f, 0.10f, 0.85f);
	AddDistrict(ECitixDistrict::Commercial,	3500.f,  14500.f, 400.f, 1800.f, 4400.f, 0.72f, 0.08f, 0.06f, 0.42f, 0.45f, 0.18f, 0.85f);
	AddDistrict(ECitixDistrict::OldTown,	900.f,   4200.f,  330.f, 650.f,  1750.f, 0.90f, 0.09f, 0.02f, 0.10f, 0.20f, 0.12f, 0.75f);
	AddDistrict(ECitixDistrict::Residential,1800.f,  9500.f,  300.f, 1100.f, 2900.f, 0.58f, 0.12f, 0.10f, 0.18f, 0.30f, 0.45f, 0.55f);
	AddDistrict(ECitixDistrict::Industrial,	1200.f,  6500.f,  700.f, 2400.f, 6000.f, 0.74f, 0.16f, 0.02f, 0.00f, 0.35f, 0.05f, 0.40f);
	AddDistrict(ECitixDistrict::Parkland,	600.f,   2600.f,  330.f, 1500.f, 3600.f, 0.30f, 0.35f, 0.75f, 0.00f, 0.05f, 0.85f, 0.50f);
	AddDistrict(ECitixDistrict::Waterfront,	3000.f,  16000.f, 380.f, 2200.f, 5200.f, 0.62f, 0.09f, 0.14f, 0.45f, 0.50f, 0.55f, 0.85f);
	AddDistrict(ECitixDistrict::University,	900.f,   4500.f,  340.f, 1600.f, 4200.f, 0.45f, 0.20f, 0.35f, 0.10f, 0.25f, 0.70f, 0.60f);
	AddDistrict(ECitixDistrict::Market,		900.f,   5200.f,  320.f, 700.f,  1900.f, 0.90f, 0.07f, 0.02f, 0.15f, 0.25f, 0.10f, 0.80f);
	AddDistrict(ECitixDistrict::OuterCity,	1000.f,  6000.f,  320.f, 1900.f, 4800.f, 0.52f, 0.20f, 0.08f, 0.10f, 0.25f, 0.30f, 0.40f);

	// ---- Night lighting character per district --------------------------
	// Shanghai-like: the financial core is dazzling, downtown is busy, the old town
	// is warm and dense, residential is softer, industry is mostly functional.
	auto AddLighting = [this](ECitixDistrict D, float LitChance, float Brightness, float Cool,
		float Accent, float BandHeight)
	{
		FCitixDistrictLightingProfile P;
		P.District = D;
		P.LitChance = LitChance;
		P.BrightnessBias = Brightness;
		P.CoolChance = Cool;
		P.AccentChance = Accent;
		P.BandHeightScale = BandHeight;
		DistrictLighting.Add(P);
	};

	//                district                      lit   bright cool  accent band
	// The financial core is still the brightest district, but "bright" now means a high
	// proportion of warm-gold windows rather than a high proportion of blown-out white
	// ones, and the accent chance is low so the four heroes are not lost in a field of
	// identically crowned support towers.
	AddLighting(ECitixDistrict::Financial,			0.78f, 0.58f, 0.24f, 0.30f, 1.00f);
	AddLighting(ECitixDistrict::Downtown,			0.74f, 0.52f, 0.20f, 0.28f, 0.95f);
	AddLighting(ECitixDistrict::Commercial,			0.76f, 0.50f, 0.18f, 0.24f, 0.95f);
	AddLighting(ECitixDistrict::OldTown,			0.74f, 0.36f, 0.04f, 0.18f, 0.92f);
	AddLighting(ECitixDistrict::Residential,		0.64f, 0.38f, 0.10f, 0.12f, 0.90f);
	AddLighting(ECitixDistrict::Industrial,			0.50f, 0.32f, 0.10f, 0.06f, 0.88f);
	AddLighting(ECitixDistrict::Parkland,			0.34f, 0.32f, 0.14f, 0.06f, 0.85f);
	AddLighting(ECitixDistrict::Waterfront,			0.78f, 0.52f, 0.20f, 0.26f, 0.95f);
	AddLighting(ECitixDistrict::University,			0.66f, 0.42f, 0.22f, 0.10f, 0.92f);
	AddLighting(ECitixDistrict::Market,				0.86f, 0.46f, 0.03f, 0.22f, 0.95f);
	AddLighting(ECitixDistrict::OuterCity,			0.58f, 0.38f, 0.12f, 0.10f, 0.90f);
}

FCitixRoadSpec UCitixCitySettings::GetRoadSpec(ECitixRoadClass RoadClass) const
{
	for (const FCitixRoadClassRule& Rule : RoadRules)
	{
		if (Rule.Class == RoadClass)
		{
			return Rule.Spec;
		}
	}
	return FCitixRoadSpec();
}

const FCitixDistrictRule& UCitixCitySettings::GetDistrictRule(ECitixDistrict District) const
{
	for (const FCitixDistrictRule& Rule : DistrictRules)
	{
		if (Rule.District == District)
		{
			return Rule;
		}
	}
	static const FCitixDistrictRule Fallback;
	return Fallback;
}

const FCitixDistrictLightingProfile& UCitixCitySettings::GetLightingProfile(ECitixDistrict District) const
{
	for (const FCitixDistrictLightingProfile& Profile : DistrictLighting)
	{
		if (Profile.District == District)
		{
			return Profile;
		}
	}
	static const FCitixDistrictLightingProfile Fallback;
	return Fallback;
}
