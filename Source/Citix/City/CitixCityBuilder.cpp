// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixCityBuilder.h"

#include "City/CitixCityPlan.h"
#include "City/CitixRoadNetwork.h"
#include "City/CitixBuildingGenerator.h"
#include "City/CitixPropGenerator.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Citix.h"

namespace
{
	/** Vertical stack. The road top is the reference surface; everything else sits on it. */
	constexpr float RoadSurfaceZ = 12.f;
	constexpr float PadZ = 12.35f;
	constexpr float MarkingZ = 12.9f;

	FORCEINLINE uint64 EdgeKey(int32 A, int32 B)
	{
		return (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
	}

	FORCEINLINE FVector2D Rotate2D(const FVector2D& V, float Cos, float Sin)
	{
		return FVector2D(V.X * Cos - V.Y * Sin, V.X * Sin + V.Y * Cos);
	}

	/** A block's own frame plus how far in the bounding roads push the buildable area. */
	struct FBlockLayout
	{
		FVector2D Centre = FVector2D::ZeroVector;
		FVector2D AxisU = FVector2D(1.f, 0.f);
		FVector2D AxisV = FVector2D(0.f, 1.f);
		float Yaw = 0.f;
		/** Half extents of the buildable rectangle, after the roads are taken out. */
		float HalfU = 0.f;
		float HalfV = 0.f;
		int32 DistrictIndex = INDEX_NONE;
		ECitixPlanZone Zone = ECitixPlanZone::MediumDensity;
		bool bOpenSpace = false;
		bool bWaterfront = false;
		float LandmarkChance = 0.f;
		/** Mid-block local streets: how many strips to cut across each axis. */
		int32 SplitU = 0;
		int32 SplitV = 0;
		bool bValid = false;
	};

	/** Rotate a whole lot's boxes about the lot centre, so buildings follow their block. */
	void RotateBoxesIntoBlock(TArray<FCitixBoxInstance>& Boxes, const FVector2D& Pivot, float YawDegrees)
	{
		if (FMath::IsNearlyZero(YawDegrees))
		{
			return;
		}
		const float Rad = FMath::DegreesToRadians(YawDegrees);
		const float Cos = FMath::Cos(Rad);
		const float Sin = FMath::Sin(Rad);
		for (FCitixBoxInstance& Box : Boxes)
		{
			const FVector2D Offset(static_cast<float>(Box.Center.X - Pivot.X),
				static_cast<float>(Box.Center.Y - Pivot.Y));
			const FVector2D Rotated = Rotate2D(Offset, Cos, Sin);
			Box.Center.X = Pivot.X + Rotated.X;
			Box.Center.Y = Pivot.Y + Rotated.Y;
			Box.Rotation.Yaw += YawDegrees;
		}
	}

	FBlockLayout ComputeBlockLayout(const FCitixCityPlan& Plan, const FCitixRoadNetwork& Graph,
		const UCitixCitySettings& Settings, const FCitixPlanBlock& Block)
	{
		FBlockLayout Layout;
		if (Block.Corners.Num() < 4)
		{
			return Layout;
		}

		const FVector2D C0 = Block.Corners[0];
		const FVector2D C1 = Block.Corners[1];
		const FVector2D C2 = Block.Corners[2];
		const FVector2D C3 = Block.Corners[3];

		const FVector2D EdgeU = C1 - C0;
		const FVector2D EdgeV = C3 - C0;
		const float Width = EdgeU.Size();
		const float Depth = EdgeV.Size();
		if (Width < 100.f || Depth < 100.f)
		{
			return Layout;
		}
		Layout.AxisU = EdgeU / Width;
		Layout.AxisV = EdgeV / Depth;
		Layout.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Layout.AxisU.Y, Layout.AxisU.X));

		// How far the roads that bound this block push into it.
		const int32 SlotA = Plan.SlotForBank(Block.Side, Block.Row);
		const int32 SlotB = Plan.SlotForBank(Block.Side, Block.Row + 1);
		const int32 CornerNodes[4][2] =
		{
			{ Plan.NodeIndex(Block.Column, SlotA), Plan.NodeIndex(Block.Column + 1, SlotA) },
			{ Plan.NodeIndex(Block.Column + 1, SlotA), Plan.NodeIndex(Block.Column + 1, SlotB) },
			{ Plan.NodeIndex(Block.Column + 1, SlotB), Plan.NodeIndex(Block.Column, SlotB) },
			{ Plan.NodeIndex(Block.Column, SlotB), Plan.NodeIndex(Block.Column, SlotA) }
		};

		auto HalfCorridor = [&Graph](int32 A, int32 B) -> float
		{
			for (const FCitixRoadEdge& Edge : Graph.Edges)
			{
				if ((Edge.NodeA == A && Edge.NodeB == B) || (Edge.NodeA == B && Edge.NodeB == A))
				{
					return Edge.CorridorWidth * 0.5f;
				}
			}
			return 0.f;
		};

		// Edge 0 and 2 run along U (they bound the V extent); edges 1 and 3 run along V.
		const float InsetV0 = HalfCorridor(CornerNodes[0][0], CornerNodes[0][1]);
		const float InsetU1 = HalfCorridor(CornerNodes[1][0], CornerNodes[1][1]);
		const float InsetV1 = HalfCorridor(CornerNodes[2][0], CornerNodes[2][1]);
		const float InsetU0 = HalfCorridor(CornerNodes[3][0], CornerNodes[3][1]);

		const float UsableMinU = InsetU0;
		const float UsableMaxU = Width - InsetU1;
		const float UsableMinV = InsetV0;
		const float UsableMaxV = Depth - InsetV1;
		if (UsableMaxU - UsableMinU < Settings.MinBlockSize || UsableMaxV - UsableMinV < Settings.MinBlockSize)
		{
			return Layout;
		}

		const FVector2D UsableCentre = C0
			+ Layout.AxisU * ((UsableMinU + UsableMaxU) * 0.5f)
			+ Layout.AxisV * ((UsableMinV + UsableMaxV) * 0.5f);

		Layout.Centre = UsableCentre;
		Layout.HalfU = (UsableMaxU - UsableMinU) * 0.5f;
		Layout.HalfV = (UsableMaxV - UsableMinV) * 0.5f;
		Layout.DistrictIndex = Block.DistrictIndex;
		Layout.Zone = Block.Zone;
		Layout.bOpenSpace = Block.bOpenSpace;
		Layout.bWaterfront = Block.bWaterfrontReserve;

		// Landmark chance falls off from the quarter's centre, so signature towers form a
		// cluster at the heart of the CBD instead of spreading across it.
		if (Layout.Zone == ECitixPlanZone::PrimaryCluster
			&& Plan.Districts.IsValidIndex(Block.DistrictIndex))
		{
			const FVector2D Centroid = Plan.Districts[Block.DistrictIndex].Centroid;
			const float Distance = static_cast<float>(FVector2D::Distance(Block.Centre, Centroid));
			Layout.LandmarkChance = FMath::Clamp(1.f - Distance / (Plan.CitySize * 0.16f), 0.f, 1.f)
				* Settings.LandmarkCoreChance;
		}

		// Cut mid-block local streets through superblocks. This is where level 3 of the
		// hierarchy actually appears: a block that is more than ~2.5 lots deep needs a
		// street through it, exactly as a real superblock does.
		const float LotPitch = Plan.Districts.IsValidIndex(Block.DistrictIndex)
			? FMath::Max(1200.f, Plan.Districts[Block.DistrictIndex].LotPitch) : 3600.f;
		if (!Layout.bOpenSpace && !Layout.bWaterfront)
		{
			if (Layout.HalfU * 2.f > LotPitch * 3.0f)
			{
				Layout.SplitV = FMath::Min(2, FMath::FloorToInt(Layout.HalfU * 2.f / (LotPitch * 2.0f)));
			}
			if (Layout.HalfV * 2.f > LotPitch * 3.0f)
			{
				Layout.SplitU = FMath::Min(2, FMath::FloorToInt(Layout.HalfV * 2.f / (LotPitch * 2.0f)));
			}
		}

		Layout.bValid = true;
		return Layout;
	}

	/** Future density band for a zone, cm. */
	void GetZoneHeights(ECitixPlanZone Zone, const UCitixCitySettings& Settings, float& OutMin, float& OutMax)
	{
		switch (Zone)
		{
		case ECitixPlanZone::PrimaryCluster:
			OutMin = Settings.ZoneHeightPrimaryMin;
			OutMax = Settings.ZoneHeightPrimaryMax;
			break;
		case ECitixPlanZone::SecondaryCluster:
			OutMin = Settings.ZoneHeightSecondaryMin;
			OutMax = Settings.ZoneHeightSecondaryMax;
			break;
		case ECitixPlanZone::HighDensity:
			OutMin = Settings.ZoneHeightHighMin;
			OutMax = Settings.ZoneHeightHighMax;
			break;
		case ECitixPlanZone::MediumDensity:
			OutMin = Settings.ZoneHeightMediumMin;
			OutMax = Settings.ZoneHeightMediumMax;
			break;
		default:
			OutMin = Settings.ZoneHeightLowMin;
			OutMax = Settings.ZoneHeightLowMax;
			break;
		}
	}

	/** Archetype appropriate to the future density, so the skyline groups by zone. */
	ECitixBuildingArchetype PickZoneArchetype(ECitixPlanZone Zone, FRandomStream& Rng)
	{
		auto Pick = [&Rng](const ECitixBuildingArchetype* Options, int32 Count)
		{
			return Options[Rng.RandRange(0, Count - 1)];
		};

		static const ECitixBuildingArchetype Towers[] =
		{
			// Flat-topped boxes and podium towers dominate the primary cluster. The
			// pointed, crowned and holed silhouettes stay in the mix but as a minority,
			// so the four named heroes are the only ones that read as landmarks.
			ECitixBuildingArchetype::Box, ECitixBuildingArchetype::Box,
			ECitixBuildingArchetype::Box, ECitixBuildingArchetype::PodiumTower,
			ECitixBuildingArchetype::PodiumTower, ECitixBuildingArchetype::SetbackTower,
			ECitixBuildingArchetype::CrownedTower, ECitixBuildingArchetype::HoledSlab,
			ECitixBuildingArchetype::TwinTower
		};

		static const ECitixBuildingArchetype Secondary[] =
		{
			ECitixBuildingArchetype::PodiumTower, ECitixBuildingArchetype::CrownedTower,
			ECitixBuildingArchetype::SetbackTower, ECitixBuildingArchetype::ApartmentSlab,
			ECitixBuildingArchetype::Box, ECitixBuildingArchetype::TwistedTower
		};
		static const ECitixBuildingArchetype High[] =
		{
			ECitixBuildingArchetype::PodiumTower, ECitixBuildingArchetype::Box,
			ECitixBuildingArchetype::ApartmentSlab, ECitixBuildingArchetype::CrownedTower,
			ECitixBuildingArchetype::Courtyard
		};
		static const ECitixBuildingArchetype Medium[] =
		{
			ECitixBuildingArchetype::ApartmentSlab, ECitixBuildingArchetype::Courtyard,
			ECitixBuildingArchetype::Box, ECitixBuildingArchetype::Shophouse
		};
		static const ECitixBuildingArchetype Low[] =
		{
			ECitixBuildingArchetype::Warehouse, ECitixBuildingArchetype::Box,
			ECitixBuildingArchetype::Shophouse, ECitixBuildingArchetype::ApartmentSlab
		};

		switch (Zone)
		{
		case ECitixPlanZone::PrimaryCluster:   return Pick(Towers, UE_ARRAY_COUNT(Towers));
		case ECitixPlanZone::SecondaryCluster: return Pick(Secondary, UE_ARRAY_COUNT(Secondary));
		case ECitixPlanZone::HighDensity:      return Pick(High, UE_ARRAY_COUNT(High));
		case ECitixPlanZone::MediumDensity:    return Pick(Medium, UE_ARRAY_COUNT(Medium));
		default:                               return Pick(Low, UE_ARRAY_COUNT(Low));
		}
	}

	const TCHAR* LandmarkStyleName(ECitixLandmarkStyle Style)
	{
		switch (Style)
		{
		case ECitixLandmarkStyle::PearlBroadcastTower: return TEXT("PearlBroadcastTower");
		case ECitixLandmarkStyle::TwistingSupertall: return TEXT("TwistingSupertall");
		case ECitixLandmarkStyle::CrownOpeningTower: return TEXT("CrownOpeningTower");
		case ECitixLandmarkStyle::TieredCrownTower: return TEXT("TieredCrownTower");
		default: return TEXT("None");
		}
	}

	void AssignShanghaiHeroLandmarks(const FCitixCityPlan& Plan, TArray<FCitixParcel>& Parcels)
	{
		int32 DistrictIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Plan.Districts.Num(); ++Index)
		{
			const FCitixPlanDistrict& District = Plan.Districts[Index];
			if (District.Type == ECitixDistrict::Financial && District.Zone == ECitixPlanZone::PrimaryCluster)
			{
				DistrictIndex = Index;
				break;
			}
		}
		if (DistrictIndex == INDEX_NONE)
		{
			return;
		}

		struct FHeroTarget { ECitixLandmarkStyle Style; float U; float Outward; };
		// Three supertalls read as a tight Lujiazui-style trio, and the pearl is pushed
		// further along the river with a clear gap so it never merges into them. The
		// outward depths are staggered, so from the river the three do not stack into one
		// column and each keeps its own shoulder line.
		static const FHeroTarget Targets[] =
		{
			{ ECitixLandmarkStyle::CrownOpeningTower, 0.29f, 0.17f },
			{ ECitixLandmarkStyle::TwistingSupertall, 0.40f, 0.28f },
			{ ECitixLandmarkStyle::TieredCrownTower,  0.50f, 0.20f },
			{ ECitixLandmarkStyle::PearlBroadcastTower, 0.73f, 0.07f }
		};

		const FCitixPlanDistrict& District = Plan.Districts[DistrictIndex];
		TSet<int32> Used;
		for (const FHeroTarget& Target : Targets)
		{
			const float RiverU = FMath::Lerp(District.UMin, District.UMax, Target.U);
			FVector2D RiverPoint, Tangent;
			Plan.GetRiverFrame(RiverU, RiverPoint, Tangent);
			const FVector2D Normal(-Tangent.Y, Tangent.X);
			const FVector2D Desired = RiverPoint + Normal * static_cast<float>(District.Side)
				* (Plan.GetHalfWidth(RiverU) + Plan.CitySize * Target.Outward);

			int32 BestIndex = INDEX_NONE;
			float BestDistanceSq = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Parcels.Num(); ++Index)
			{
				const FCitixParcel& Parcel = Parcels[Index];
				if (Used.Contains(Index) || Parcel.DistrictIndex != DistrictIndex
					|| Parcel.Zone != ECitixPlanZone::PrimaryCluster
					|| FMath::Min(Parcel.HalfExtents.X, Parcel.HalfExtents.Y) < 1200.f)
				{
					continue;
				}
				const float DistanceSq = FVector2D::DistSquared(Parcel.Centre, Desired);
				if (DistanceSq < BestDistanceSq)
				{
					BestDistanceSq = DistanceSq;
					BestIndex = Index;
				}
			}
			if (BestIndex != INDEX_NONE)
			{
				FCitixParcel& Parcel = Parcels[BestIndex];
				Parcel.Use = ECitixParcelUse::Building;
				Parcel.LandmarkStyle = Target.Style;
				Used.Add(BestIndex);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Parcels
// ---------------------------------------------------------------------------

TArray<FCitixParcel> FCitixCityBuilder::GenerateParcels(
	const FCitixCityPlan& Plan,
	const FCitixRoadNetwork& Graph,
	const UCitixCitySettings& Settings,
	FRandomStream& Rng,
	TArray<FCitixPlanBlock>* OutLocalStreetBlocks)
{
	TArray<FCitixParcel> Parcels;

	for (const FCitixPlanBlock& Block : Plan.Blocks)
	{
		const FBlockLayout Layout = ComputeBlockLayout(Plan, Graph, Settings, Block);
		if (!Layout.bValid)
		{
			continue;
		}

		if (OutLocalStreetBlocks && (Layout.SplitU > 0 || Layout.SplitV > 0))
		{
			OutLocalStreetBlocks->Add(Block);
		}

		// Reserved open space is one whole-block parcel: a park is not subdivided into
		// dozens of little parks.
		if (Layout.bOpenSpace)
		{
			FCitixParcel Park;
			Park.Centre = Layout.Centre;
			Park.HalfExtents = FVector2D(Layout.HalfU, Layout.HalfV);
			Park.Yaw = Layout.Yaw;
			Park.DistrictIndex = Layout.DistrictIndex;
			Park.Zone = Layout.Zone;
			Park.Use = ECitixParcelUse::Park;
			Parcels.Add(Park);
			continue;
		}

		const FCitixPlanDistrict* District = Plan.Districts.IsValidIndex(Layout.DistrictIndex)
			? &Plan.Districts[Layout.DistrictIndex] : nullptr;
		const float LotPitch = District ? FMath::Max(1200.f, District->LotPitch) : 3600.f;

		// Sub-cells between the mid-block streets.
		const int32 CellsU = Layout.SplitU + 1;
		const int32 CellsV = Layout.SplitV + 1;
		const float LocalRoadHalf = (Settings.PreviewLocalWidth * 0.5f + 400.f) * 0.5f;

		for (int32 CellU = 0; CellU < CellsU; ++CellU)
		{
			for (int32 CellV = 0; CellV < CellsV; ++CellV)
			{
				// Extent of this sub-cell along each axis.
				float MinU = -Layout.HalfU;
				float MaxU = Layout.HalfU;
				float MinV = -Layout.HalfV;
				float MaxV = Layout.HalfV;

				if (Layout.SplitU > 0)
				{
					const float Pitch = (2.f * Layout.HalfU) / CellsU;
					MinU = -Layout.HalfU + Pitch * CellU + (CellU > 0 ? LocalRoadHalf : 0.f);
					MaxU = -Layout.HalfU + Pitch * (CellU + 1) - (CellU < CellsU - 1 ? LocalRoadHalf : 0.f);
				}
				if (Layout.SplitV > 0)
				{
					const float Pitch = (2.f * Layout.HalfV) / CellsV;
					MinV = -Layout.HalfV + Pitch * CellV + (CellV > 0 ? LocalRoadHalf : 0.f);
					MaxV = -Layout.HalfV + Pitch * (CellV + 1) - (CellV < CellsV - 1 ? LocalRoadHalf : 0.f);
				}

				const float SpanU = MaxU - MinU;
				const float SpanV = MaxV - MinV;
				if (SpanU < Settings.MinParcelSize || SpanV < Settings.MinParcelSize)
				{
					continue;
				}

				// Subdivide into parcels of about LotPitch.
				const int32 LotsU = FMath::Clamp(FMath::RoundToInt(SpanU / LotPitch), 1, 6);
				const int32 LotsV = FMath::Clamp(FMath::RoundToInt(SpanV / LotPitch), 1, 6);
				const float LotSpanU = SpanU / LotsU;
				const float LotSpanV = SpanV / LotsV;

				const FCitixDistrictRule& Rule = Settings.GetDistrictRule(
					District ? District->Type : ECitixDistrict::Downtown);

				for (int32 LotU = 0; LotU < LotsU; ++LotU)
				{
					for (int32 LotV = 0; LotV < LotsV; ++LotV)
					{
						const float CentreU = MinU + (static_cast<float>(LotU) + 0.5f) * LotSpanU;
						const float CentreV = MinV + (static_cast<float>(LotV) + 0.5f) * LotSpanV;

						FCitixParcel Parcel;
						Parcel.Centre = Layout.Centre + Layout.AxisU * CentreU + Layout.AxisV * CentreV;
						Parcel.HalfExtents = FVector2D(LotSpanU * 0.5f, LotSpanV * 0.5f);
						Parcel.Yaw = Layout.Yaw;
						Parcel.DistrictIndex = Layout.DistrictIndex;
						Parcel.Zone = Layout.Zone;
						Parcel.LandmarkChance = Layout.LandmarkChance;

						const float Roll = Rng.FRand();
						if (Roll < Rule.EmptyLotChance)
						{
							Parcel.Use = ECitixParcelUse::Empty;
						}
						else if (Roll < Rule.EmptyLotChance + Rule.ParkChance)
						{
							Parcel.Use = ECitixParcelUse::Park;
						}
						else if (Roll < Rule.EmptyLotChance + Rule.ParkChance + Settings.PlazaChance
							&& (Layout.Zone == ECitixPlanZone::PrimaryCluster
								|| Layout.Zone == ECitixPlanZone::SecondaryCluster))
						{
							Parcel.Use = ECitixParcelUse::Plaza;
						}
						else if (Layout.Zone == ECitixPlanZone::LowDensity
							&& Roll > 1.f - Settings.ParkingChance)
						{
							Parcel.Use = ECitixParcelUse::Parking;
						}
						else
						{
							Parcel.Use = ECitixParcelUse::Building;
						}

						Parcels.Add(Parcel);
					}
				}
			}
		}
	}

	AssignShanghaiHeroLandmarks(Plan, Parcels);
	return Parcels;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

FCitixBuildResult FCitixCityBuilder::Build(
	const FCitixCityPlan& Plan,
	const FCitixRoadNetwork& Graph,
	const UCitixCitySettings& Settings,
	FRandomStream& Rng,
	TFunctionRef<void(const FCitixBoxInstance&)> Emit,
	TArray<FVector>& OutLampLocations,
	TArray<FVector>& OutLandmarkLocations)
{
	FCitixBuildResult Result;

	auto EmitBox = [&Emit, &Result](const FVector& Centre, const FVector& Size,
		ECitixSurface Surface, float YawDegrees, const FVector2D& Flow = FVector2D::ZeroVector)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f || Size.Z <= 0.f)
		{
			return;
		}
		FCitixBoxInstance Box;
		Box.Center = Centre;
		Box.Size = Size;
		Box.Surface = Surface;
		Box.Rotation = FRotator(0.f, YawDegrees, 0.f);
		Box.Flow = Flow;
		Emit(Box);
		++Result.TotalInstances;
	};

	// =======================================================================
	// RIVER + EMBANKMENT
	// =======================================================================
	{
		// Bridge crossings interrupt the embankment so the decks can reach the banks.
		TArray<FVector2D> BridgeCrossings;
		for (int32 EdgeIndex = 0; EdgeIndex < Graph.Edges.Num(); ++EdgeIndex)
		{
			const FCitixRoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (!Edge.bBridge)
			{
				continue;
			}
			BridgeCrossings.Add((Graph.Nodes[Edge.NodeA].Position + Graph.Nodes[Edge.NodeB].Position) * 0.5f);
		}

		// The promenade lives in the strip of land the plan leaves between the water and
		// the first row of riverside roads, so it can never end up coplanar with a road
		// surface. Size it from that gap; if the plan leaves no room, skip it (the
		// riverside road's own pavement is the promenade then).
		float MaxCorridorHalf = 0.f;
		for (const FCitixRoadEdge& RoadEdge : Graph.Edges)
		{
			MaxCorridorHalf = FMath::Max(MaxCorridorHalf, RoadEdge.CorridorWidth * 0.5f);
		}
		const float WalkwayWidth = FMath::Clamp(
			Settings.PlanRiverGap - MaxCorridorHalf - 300.f, 0.f, 2000.f);
		const bool bHasWalkway = WalkwayWidth > 250.f;

		const float ParapetHeight = 135.f;
		const float ParapetWidth = 90.f;
		constexpr float BridgeGap = 2100.f;

		for (int32 Index = 0; Index + 1 < Plan.RiverPoints.Num(); ++Index)
		{
			const FVector2D A = Plan.RiverPoints[Index];
			const FVector2D B = Plan.RiverPoints[Index + 1];
			const float Length = FVector2D::Distance(A, B);
			if (Length < 1.f)
			{
				continue;
			}
			const FVector2D Direction = (B - A) / Length;
			const FVector2D Normal(-Direction.Y, Direction.X);
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
			const float HalfA = Plan.GetHalfWidth(static_cast<float>(Index) / (Plan.RiverPoints.Num() - 1));
			const float HalfB = Plan.GetHalfWidth(static_cast<float>(Index + 1) / (Plan.RiverPoints.Num() - 1));
			const float HalfWidth = (HalfA + HalfB) * 0.5f;
			const FVector2D Mid = (A + B) * 0.5f;

			// Water plane, just above the ground slab so no ground cut is needed. The flow
			// direction rides along with each piece (as per-instance custom data) so the
			// ripples follow the river instead of a fixed world axis.
			EmitBox(FVector(Mid.X, Mid.Y, 5.f), FVector(Length * 1.06f, HalfWidth * 2.f, 10.f),
				ECitixSurface::Water, Yaw, Direction);

			bool bNearBridge = false;
			for (const FVector2D& Crossing : BridgeCrossings)
			{
				if (FVector2D::Distance(Mid, Crossing) < BridgeGap)
				{
					bNearBridge = true;
					break;
				}
			}

			// A bridge deck runs over the water's edge, straight across the promenade.
			// Leave that stretch of bank clear, otherwise the walkway (16 cm of solid
			// pavement) is drawn through the deck and buries the bridge landing.
			bool bAcrossBridge = false;
			for (const FCitixRoadEdge& BridgeEdge : Graph.Edges)
			{
				if (!BridgeEdge.bBridge)
				{
					continue;
				}
				const FVector2D BridgeA = Graph.Nodes[BridgeEdge.NodeA].Position;
				const FVector2D BridgeB = Graph.Nodes[BridgeEdge.NodeB].Position;
				const FVector2D Span = BridgeB - BridgeA;
				const float SpanSquared = Span.SizeSquared();
				float DistanceToBridge = FVector2D::Distance(Mid, BridgeA);
				if (SpanSquared > 1.f)
				{
					const float T = FMath::Clamp(
						FVector2D::DotProduct(Mid - BridgeA, Span) / SpanSquared, 0.f, 1.f);
					DistanceToBridge = FVector2D::Distance(Mid, BridgeA + Span * T);
				}
				if (DistanceToBridge < Length * 0.5f + 2500.f)
				{
					bAcrossBridge = true;
					break;
				}
			}

			// How much promenade actually fits on a bank before the nearest road corridor
			// begins. The riverside street wobbles toward the river, so the room has to be
			// measured rather than assumed, or the slab is drawn through a road exactly
			// where the plan is most irregular.
			auto BankClearance = [&](float Offset, int32 Side) -> float
			{
				const FVector2D P = Mid + Normal * (HalfWidth + Offset) * static_cast<float>(Side);
				float Best = TNumericLimits<float>::Max();
				for (const FCitixRoadEdge& Road : Graph.Edges)
				{
					if (!Road.bDrivable)
					{
						continue;
					}
					const FVector2D RoadA = Graph.Nodes[Road.NodeA].Position;
					const FVector2D RoadB = Graph.Nodes[Road.NodeB].Position;
					const FVector2D Span = RoadB - RoadA;
					const float SpanSquared = Span.SizeSquared();
					float T = 0.f;
					if (SpanSquared > 1.f)
					{
						T = FMath::Clamp(FVector2D::DotProduct(P - RoadA, Span) / SpanSquared, 0.f, 1.f);
					}
					Best = FMath::Min(Best,
						FVector2D::Distance(P, RoadA + Span * T) - Road.CorridorWidth * 0.5f);
				}
				return Best;
			};

			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const bool bOpenBank = bNearBridge || bAcrossBridge;

				float SideWalkwayWidth = WalkwayWidth;
				if (bHasWalkway && SideWalkwayWidth > 0.f)
				{
					constexpr float RoadMargin = 120.f;
					const float Inner = BankClearance(0.f, Side);
					if (Inner <= RoadMargin)
					{
						SideWalkwayWidth = 0.f;
					}
					else
					{
						const float Outer = BankClearance(SideWalkwayWidth, Side);
						if (Outer < RoadMargin)
						{
							// Clearance falls roughly linearly across the strip.
							SideWalkwayWidth *= (Inner - RoadMargin) / FMath::Max(1.f, Inner - Outer);
						}
					}
				}

				// Walkable riverside promenade on both banks, in the strip between the
				// water and the riverside road.
				if (SideWalkwayWidth > 250.f && !bOpenBank)
				{
					const FVector2D WalkCentre = Mid + Normal * (HalfWidth + SideWalkwayWidth * 0.5f) * static_cast<float>(Side);
					EmitBox(FVector(WalkCentre.X, WalkCentre.Y, Settings.SidewalkHeight * 0.5f),
						FVector(Length * 1.06f, SideWalkwayWidth, Settings.SidewalkHeight), ECitixSurface::Sidewalk, Yaw);
				}

				if (bOpenBank)
				{
					continue;
				}
				// The parapet also collides, so the river is a real barrier and the only
				// way across is a bridge.
				const FVector2D WallCentre = Mid + Normal * (HalfWidth + ParapetWidth * 0.5f + 30.f) * static_cast<float>(Side);
				EmitBox(FVector(WallCentre.X, WallCentre.Y, Settings.SidewalkHeight + ParapetHeight * 0.5f),
					FVector(Length * 1.06f, ParapetWidth, ParapetHeight), ECitixSurface::Embankment, Yaw);
			}
		}
	}

	// =======================================================================
	// ROADS
	// =======================================================================
	for (int32 EdgeIndex = 0; EdgeIndex < Graph.Edges.Num(); ++EdgeIndex)
	{
		const FCitixRoadEdge& Edge = Graph.Edges[EdgeIndex];
		if (!Graph.IsValidNode(Edge.NodeA) || !Graph.IsValidNode(Edge.NodeB))
		{
			continue;
		}
		const FVector2D A = Graph.Nodes[Edge.NodeA].Position;
		const FVector2D B = Graph.Nodes[Edge.NodeB].Position;
		const FVector2D Delta = B - A;
		const float FullLength = Delta.Size();
		if (FullLength < 1.f)
		{
			continue;
		}
		++Result.RoadSegments;

		const FVector2D Direction = Delta / FullLength;
		const FVector2D Perp(-Direction.Y, Direction.X);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

		const FCitixRoadSpec Spec = Settings.GetRoadSpec(Edge.RoadClass);
		const float Carriage = FMath::Max(120.f, Spec.CarriagewayWidth());
		const bool bBridge = Edge.bBridge;

		// Every surface stops at the junction, where one oriented pad owns the crossing.
		// Running each carriageway the full edge length (as this used to) put the top
		// faces of all the roads meeting at a junction on exactly the same Z, so they
		// z-fought across the whole crossing. Trimming leaves one road surface per Z.
		const float TrimA = FMath::Max(0.f, Edge.TrimA);
		const float TrimB = FMath::Max(0.f, Edge.TrimB);
		const FVector2D TA = A + Direction * TrimA;
		const FVector2D TB = B - Direction * TrimB;
		const float InnerLength = FVector2D::Distance(TA, TB);
		if (InnerLength < 200.f)
		{
			continue;
		}
		const FVector2D InnerMid = (TA + TB) * 0.5f;

		EmitBox(FVector(InnerMid.X, InnerMid.Y, RoadSurfaceZ - Settings.RoadThickness * 0.5f),
			FVector(InnerLength, Carriage, Settings.RoadThickness),
			bBridge ? ECitixSurface::AsphaltDark : ECitixSurface::Asphalt, Yaw);

		if (bBridge)
		{
			// Railings instead of pavements.
			const float RailHeight = 140.f;
			const float RailWidth = 80.f;
			const float Offset = Carriage * 0.5f + RailWidth * 0.5f;
			for (int32 Sign = -1; Sign <= 1; Sign += 2)
			{
				const FVector2D C = InnerMid + Perp * (Offset * static_cast<float>(Sign));
				EmitBox(FVector(C.X, C.Y, Settings.RoadThickness + RailHeight * 0.5f),
					FVector(InnerLength, RailWidth, RailHeight), ECitixSurface::Embankment, Yaw);
			}
		}
		else if (Spec.SidewalkWidth > 0.5f)
		{
			const float SidewalkWidth = Spec.SidewalkWidth;
			const float Offset = Carriage * 0.5f + SidewalkWidth * 0.5f;
			for (int32 Sign = -1; Sign <= 1; Sign += 2)
			{
				const FVector2D C = InnerMid + Perp * (Offset * static_cast<float>(Sign));
				EmitBox(FVector(C.X, C.Y, Settings.SidewalkHeight * 0.5f),
					FVector(InnerLength, SidewalkWidth, Settings.SidewalkHeight), ECitixSurface::Sidewalk, Yaw);
			}

			// Curb strip gives the road edge definition.
			const float CurbWidth = 70.f;
			const float CurbOffset = Carriage * 0.5f + CurbWidth * 0.5f;
			for (int32 Sign = -1; Sign <= 1; Sign += 2)
			{
				const FVector2D C = InnerMid + Perp * (CurbOffset * static_cast<float>(Sign));
				EmitBox(FVector(C.X, C.Y, Settings.SidewalkHeight * 0.5f + 2.f),
					FVector(InnerLength, CurbWidth, Settings.SidewalkHeight + 4.f), ECitixSurface::Curb, Yaw);
			}
		}

		// Markings: a planted median where there is one, otherwise a dashed centre line
		// plus lane dividers and edge lines.
		if (!bBridge && Spec.NumLanes >= 2 && InnerLength > 400.f)
		{
			if (Spec.MedianWidth > 1.f)
			{
				EmitBox(FVector(InnerMid.X, InnerMid.Y, Settings.SidewalkHeight * 0.5f),
					FVector(InnerLength, Spec.MedianWidth, Settings.SidewalkHeight), ECitixSurface::Curb, Yaw);
			}
			else
			{
				const float DashPitch = 900.f;
				const float DashLength = 450.f;
				const int32 Dashes = FMath::FloorToInt(InnerLength / DashPitch);
				for (int32 Dash = 0; Dash < Dashes; ++Dash)
				{
					const FVector2D P = TA + Direction * (Dash * DashPitch + DashLength * 0.5f);
					EmitBox(FVector(P.X, P.Y, MarkingZ), FVector(DashLength, 18.f, 1.5f),
						ECitixSurface::Marking, Yaw);
				}
			}

			const int32 HalfLanes = FMath::Max(1, Spec.NumLanes / 2);
			for (int32 Lane = 1; Lane < HalfLanes; ++Lane)
			{
				const float Offset = Lane * Spec.LaneWidth;
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					const FVector2D P = InnerMid + Perp * (Offset * static_cast<float>(Sign));
					EmitBox(FVector(P.X, P.Y, MarkingZ), FVector(InnerLength, 14.f, 1.5f),
						ECitixSurface::MarkingDim, Yaw);
				}
			}

			const float EdgeOffset = Carriage * 0.5f - 90.f;
			if (EdgeOffset > 50.f)
			{
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					const FVector2D P = InnerMid + Perp * (EdgeOffset * static_cast<float>(Sign));
					EmitBox(FVector(P.X, P.Y, MarkingZ), FVector(InnerLength, 16.f, 1.5f),
						ECitixSurface::Marking, Yaw);
				}
			}
		}

		// ---- Street furniture -------------------------------------------
		if (bBridge || Spec.SidewalkWidth <= 0.5f)
		{
			continue;
		}

		const ECitixDistrict District = Plan.Districts.IsValidIndex(Edge.DistrictIndex)
			? Plan.Districts[Edge.DistrictIndex].Type : ECitixDistrict::OuterCity;
		const FCitixDistrictRule& Rule = Settings.GetDistrictRule(District);
		const float SidewalkWidth = Spec.SidewalkWidth;
		const float FurnitureOffset = Carriage * 0.5f + SidewalkWidth * 0.55f;

		// Lamps, on one side, arms reaching over the carriageway. Their heads are
		// recorded so the night lighting pool can target them.
		if (Settings.LampSpacing > 100.f && Settings.LampChance > 0.f)
		{
			const int32 Slots = FMath::FloorToInt(InnerLength / Settings.LampSpacing);
			for (int32 Slot = 0; Slot < Slots; ++Slot)
			{
				const float Along = (static_cast<float>(Slot) + 0.5f) * Settings.LampSpacing;
				if (Along > InnerLength || Rng.FRand() > Settings.LampChance)
				{
					continue;
				}
				const FVector2D P = TA + Direction * Along + Perp * FurnitureOffset;
				const float LampYaw = FMath::RadiansToDegrees(FMath::Atan2(-Perp.Y, -Perp.X));
				const FVector ArmDirection = FRotator(0.f, LampYaw, 0.f).Vector();
				OutLampLocations.Add(FVector(P.X, P.Y, Settings.SidewalkHeight + Settings.LampHeight - 30.f)
					+ ArmDirection * 180.f);

				TArray<FCitixBoxInstance> Boxes;
				FCitixPropGenerator::AddStreetLamp(Boxes, FVector(P.X, P.Y, Settings.SidewalkHeight),
					Settings.LampHeight, LampYaw);
				for (const FCitixBoxInstance& Box : Boxes)
				{
					Emit(Box);
					++Result.TotalInstances;
					++Result.PropInstances;
				}
			}
		}

		// Trees, on the opposite side, where the district warrants them.
		if (Rule.TreeChance > 0.f && Settings.TreeSpacing > 100.f)
		{
			const int32 Slots = FMath::FloorToInt(InnerLength / Settings.TreeSpacing);
			for (int32 Slot = 0; Slot < Slots; ++Slot)
			{
				const float Along = (static_cast<float>(Slot) + 0.5f) * Settings.TreeSpacing;
				if (Along > InnerLength || Rng.FRand() > Rule.TreeChance)
				{
					continue;
				}
				const FVector2D P = TA + Direction * Along - Perp * FurnitureOffset;
				TArray<FCitixBoxInstance> Boxes;
				FCitixPropGenerator::AddBush(Boxes, FVector(P.X, P.Y, Settings.SidewalkHeight),
					Rng.FRandRange(0.85f, 1.3f),FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X)), Rng);
				for (const FCitixBoxInstance& Box : Boxes)
				{
					Emit(Box);
					++Result.TotalInstances;
					++Result.PropInstances;
				}
			}
		}

		// Planters fill the gaps between trees so the sidewalk never reads as an empty
		// grey strip. Same rhythm as the trees, offset by half a slot, only where the
		// pavement is wide enough to carry them.
		if (Rule.TreeChance > 0.f && Settings.TreeSpacing > 100.f && SidewalkWidth >= 250.f)
		{
			const int32 PlanterSlots = FMath::FloorToInt(InnerLength / Settings.TreeSpacing);
			for (int32 PlanterSlot = 0; PlanterSlot < PlanterSlots; ++PlanterSlot)
			{
				const float PlanterAlong = (static_cast<float>(PlanterSlot) + 1.0f) * Settings.TreeSpacing;
				if (PlanterAlong > InnerLength || Rng.FRand() > 0.38f)
				{
					continue;
				}
				const FVector2D PlanterP = TA + Direction * PlanterAlong - Perp * FurnitureOffset;
				TArray<FCitixBoxInstance> PlanterBoxes;
				FCitixPropGenerator::AddPlanter(PlanterBoxes,
					FVector(PlanterP.X, PlanterP.Y, Settings.SidewalkHeight), Rng);
				for (const FCitixBoxInstance& Box : PlanterBoxes)
				{
					Emit(Box);
					++Result.TotalInstances;
					++Result.PropInstances;
				}
			}
		}

		// Benches on walkable-district sidewalks, alternating with the lamps.
		const bool bWalkableDistrict = (District == ECitixDistrict::Downtown
			|| District == ECitixDistrict::Commercial || District == ECitixDistrict::Waterfront
			|| District == ECitixDistrict::Market || District == ECitixDistrict::OldTown
			|| District == ECitixDistrict::University);
		if (bWalkableDistrict && Settings.LampSpacing > 100.f && SidewalkWidth >= 300.f)
		{
			const int32 BenchSlots = FMath::FloorToInt(InnerLength / Settings.LampSpacing);
			for (int32 BenchSlot = 0; BenchSlot < BenchSlots; ++BenchSlot)
			{
				if (BenchSlot % 2 != 0 || Rng.FRand() > 0.55f)
				{
					continue;
				}
				const float BenchAlong = (static_cast<float>(BenchSlot) + 0.5f) * Settings.LampSpacing;
				if (BenchAlong > InnerLength)
				{
					continue;
				}
				const FVector2D BenchP = TA + Direction * BenchAlong + Perp * (FurnitureOffset + 120.f);
				const float BenchYaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
				TArray<FCitixBoxInstance> BenchBoxes;
				FCitixPropGenerator::AddBench(BenchBoxes,
					FVector(BenchP.X, BenchP.Y, Settings.SidewalkHeight), BenchYaw);
				for (const FCitixBoxInstance& Box : BenchBoxes)
				{
					Emit(Box);
					++Result.TotalInstances;
					++Result.PropInstances;
				}
			}
		}
	}

	// =======================================================================
	// JUNCTIONS
	// =======================================================================
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		const TArray<int32>& Incident = Graph.NodeEdgeIndices[NodeIndex];
		if (Incident.Num() < 3)
		{
			continue; // a bend is not a junction
		}
		++Result.Junctions;

		const FVector2D P = Graph.Nodes[NodeIndex].Position;
		float MaxCorridor = 0.f;
		bool bHasMajor = false;
		ECitixRoadClass DominantClass = ECitixRoadClass::Alley;
		FVector2D DominantDirection(1.f, 0.f);
		for (int32 EdgeIndex : Incident)
		{
			const FCitixRoadEdge& Edge = Graph.Edges[EdgeIndex];
			MaxCorridor = FMath::Max(MaxCorridor, Edge.CorridorWidth);
			if (static_cast<int32>(Edge.RoadClass) > static_cast<int32>(DominantClass))
			{
				DominantClass = Edge.RoadClass;
				DominantDirection = Graph.EdgeDirection(EdgeIndex);
			}
			bHasMajor |= Edge.RoadClass == ECitixRoadClass::Arterial
				|| Edge.RoadClass == ECitixRoadClass::Boulevard
				|| Edge.RoadClass == ECitixRoadClass::Highway;
		}
		if (MaxCorridor < 100.f)
		{
			continue;
		}

		const float PadYaw = FMath::RadiansToDegrees(FMath::Atan2(DominantDirection.Y, DominantDirection.X));
		EmitBox(FVector(P.X, P.Y, PadZ - Settings.RoadThickness * 0.5f),
			FVector(MaxCorridor, MaxCorridor, Settings.RoadThickness), ECitixSurface::AsphaltDark, PadYaw);

		// One zebra crossing per approach, laid across that approach.
		const float StripeLength = FMath::Clamp(MaxCorridor * 0.14f, 200.f, 340.f);
		const float StripeWidth = 55.f;
		const float Pitch = 120.f;
		for (int32 EdgeIndex : Incident)
		{
			const FCitixRoadEdge& Edge = Graph.Edges[EdgeIndex];
			const int32 OtherNode = (Edge.NodeA == NodeIndex) ? Edge.NodeB : Edge.NodeA;
			if (!Graph.IsValidNode(OtherNode))
			{
				continue;
			}
			const FVector2D OutDirection = (Graph.Nodes[OtherNode].Position - P).GetSafeNormal();
			const FVector2D Across(-OutDirection.Y, OutDirection.X);
			const float EdgeYaw = FMath::RadiansToDegrees(FMath::Atan2(OutDirection.Y, OutDirection.X));
			const float BandDistance = MaxCorridor * 0.5f + StripeLength * 0.5f + 60.f;
			const int32 Stripes = FMath::Clamp(FMath::FloorToInt((Edge.CorridorWidth - 240.f) / Pitch), 3, 20);
			for (int32 Stripe = 0; Stripe < Stripes; ++Stripe)
			{
				const float Along = (static_cast<float>(Stripe) - (Stripes - 1) * 0.5f) * Pitch;
				const FVector2D Centre = P + OutDirection * BandDistance + Across * Along;
				EmitBox(FVector(Centre.X, Centre.Y, MarkingZ), FVector(StripeLength, StripeWidth, 1.5f),
					ECitixSurface::Marking, EdgeYaw);
			}
		}

		// Signals where a major road is involved.
		if (bHasMajor && Settings.Traffic.bEnableTrafficLights)
		{
			const float CornerDistance = MaxCorridor * 0.5f + 220.f;
			const float ArmLength = FMath::Max(300.f, MaxCorridor * 0.42f);
			const float Rad = FMath::DegreesToRadians(PadYaw);
			const FVector2D U(FMath::Cos(Rad), FMath::Sin(Rad));
			const FVector2D V(-U.Y, U.X);
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const float SignX = (Corner & 1) ? 1.f : -1.f;
				const float SignY = (Corner & 2) ? 1.f : -1.f;
				const FVector2D Position = P + U * (SignX * CornerDistance) + V * (SignY * CornerDistance);
				const FVector2D ToCentre = (P - Position).GetSafeNormal();
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToCentre.Y, ToCentre.X));
				TArray<FCitixBoxInstance> Boxes;
				FCitixPropGenerator::AddTrafficLight(Boxes,
					FVector(Position.X, Position.Y, Settings.SidewalkHeight), ArmLength, Yaw, (SignX * SignY) > 0.f);
				for (const FCitixBoxInstance& Box : Boxes)
				{
					Emit(Box);
					++Result.TotalInstances;
					++Result.PropInstances;
				}
			}
		}
	}

	// =======================================================================
	// BLOCKS, PARCELS, BUILDINGS, LANDMARKS
	// =======================================================================
	TArray<FCitixPlanBlock> LocalStreetBlocks;
	const TArray<FCitixParcel> Parcels = GenerateParcels(Plan, Graph, Settings, Rng, &LocalStreetBlocks);
	Result.Parcels = Parcels.Num();

	// Mid-block local streets (level 3 of the hierarchy).
	{
		const float LocalCarriage = Settings.PreviewLocalWidth;
		const float LocalSidewalk = 260.f;
		for (const FCitixPlanBlock& Block : LocalStreetBlocks)
		{
			const FBlockLayout Layout = ComputeBlockLayout(Plan, Graph, Settings, Block);
			if (!Layout.bValid)
			{
				continue;
			}
			const int32 CellsU = Layout.SplitU + 1;
			const int32 CellsV = Layout.SplitV + 1;

			// A street along V cuts across U at each internal split, and vice versa.
			for (int32 Split = 1; Split < CellsU; ++Split)
			{
				const float OffsetU = -Layout.HalfU + (2.f * Layout.HalfU) * Split / CellsU;
				const FVector2D Centre = Layout.Centre + Layout.AxisU * OffsetU;
				const float Length = Layout.HalfV * 2.f;
				EmitBox(FVector(Centre.X, Centre.Y, RoadSurfaceZ - Settings.RoadThickness * 0.5f),
					FVector(LocalCarriage, Length, Settings.RoadThickness), ECitixSurface::Asphalt, Layout.Yaw + 90.f);
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					const FVector2D C = Centre + Layout.AxisU * (LocalCarriage * 0.5f + LocalSidewalk * 0.5f) * static_cast<float>(Sign);
					EmitBox(FVector(C.X, C.Y, Settings.SidewalkHeight * 0.5f),
						FVector(LocalSidewalk, Length, Settings.SidewalkHeight), ECitixSurface::Sidewalk, Layout.Yaw + 90.f);
				}
				++Result.LocalStreets;
			}
			for (int32 Split = 1; Split < CellsV; ++Split)
			{
				const float OffsetV = -Layout.HalfV + (2.f * Layout.HalfV) * Split / CellsV;
				const FVector2D Centre = Layout.Centre + Layout.AxisV * OffsetV;
				const float Length = Layout.HalfU * 2.f;
				EmitBox(FVector(Centre.X, Centre.Y, RoadSurfaceZ - Settings.RoadThickness * 0.5f),
					FVector(Length, LocalCarriage, Settings.RoadThickness), ECitixSurface::Asphalt, Layout.Yaw);
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					const FVector2D C = Centre + Layout.AxisV * (LocalCarriage * 0.5f + LocalSidewalk * 0.5f) * static_cast<float>(Sign);
					EmitBox(FVector(C.X, C.Y, Settings.SidewalkHeight * 0.5f),
						FVector(Length, LocalSidewalk, Settings.SidewalkHeight), ECitixSurface::Sidewalk, Layout.Yaw);
				}
				++Result.LocalStreets;
			}
		}
	}

	// Block ground slabs: one per block, covering the buildable rectangle so the block
	// reads as a raised platform bounded by its sidewalks.
	for (const FCitixPlanBlock& Block : Plan.Blocks)
	{
		const FBlockLayout Layout = ComputeBlockLayout(Plan, Graph, Settings, Block);
		if (!Layout.bValid)
		{
			continue;
		}
		++Result.Blocks;
		EmitBox(FVector(Layout.Centre.X, Layout.Centre.Y, Settings.SidewalkHeight * 0.5f),
			FVector(Layout.HalfU * 2.f, Layout.HalfV * 2.f, Settings.SidewalkHeight),
			ECitixSurface::Sidewalk, Layout.Yaw);
	}

	// Parcel contents.
	// The four hero landmarks are the composition. Collect their centres first so support
	// buildings near a hero can be capped and dimmed, leaving a pocket of empty sky around
	// each hero's upper silhouette instead of burying it in an even wall of tall towers.
	TArray<FVector2D> HeroCentres;
	for (const FCitixParcel& Parcel : Parcels)
	{
		if (Parcel.LandmarkStyle != ECitixLandmarkStyle::None)
		{
			HeroCentres.Add(Parcel.Centre);
		}
	}

	for (const FCitixParcel& Parcel : Parcels)
	{
		const FCitixPlanDistrict* District = Plan.Districts.IsValidIndex(Parcel.DistrictIndex)
			? &Plan.Districts[Parcel.DistrictIndex] : nullptr;
		const ECitixDistrict DistrictType = District ? District->Type : ECitixDistrict::Downtown;
		const FCitixDistrictRule& Rule = Settings.GetDistrictRule(DistrictType);

		const FVector2D Size = Parcel.HalfExtents * 2.f;
		if (Size.X < 300.f || Size.Y < 300.f)
		{
			continue;
		}

		if (Parcel.Use == ECitixParcelUse::Park)
		{
			EmitBox(FVector(Parcel.Centre.X, Parcel.Centre.Y, Settings.SidewalkHeight + 4.f),
				FVector(Size.X * 0.96f, Size.Y * 0.96f, 8.f), ECitixSurface::Grass, Parcel.Yaw);
			const int32 Trees = FMath::Clamp(FMath::RoundToInt(Size.X * Size.Y / (1100.f * 1100.f)), 1, 4);
			for (int32 Index = 0; Index < Trees; ++Index)
			{
				const FVector2D Local(Rng.FRandRange(-Size.X * 0.4f, Size.X * 0.4f),
					Rng.FRandRange(-Size.Y * 0.4f, Size.Y * 0.4f));
				const float Rad = FMath::DegreesToRadians(Parcel.Yaw);
				const FVector2D Rotated = Rotate2D(Local, FMath::Cos(Rad), FMath::Sin(Rad));
				const FVector2D P = Parcel.Centre + Rotated;
				TArray<FCitixBoxInstance> Boxes;
				FCitixPropGenerator::AddTree(Boxes, FVector(P.X, P.Y, Settings.SidewalkHeight),
					Rng.FRandRange(0.8f, 1.3f), Rng);
				for (const FCitixBoxInstance& Box : Boxes)
				{
					Emit(Box);
					++Result.TotalInstances;
					++Result.PropInstances;
				}
			}
			continue;
		}

		if (Parcel.Use == ECitixParcelUse::Plaza)
		{
			EmitBox(FVector(Parcel.Centre.X, Parcel.Centre.Y, Settings.SidewalkHeight + 2.f),
				FVector(Size.X, Size.Y, 4.f), ECitixSurface::FacadeWhite, Parcel.Yaw);
			continue;
		}

		if (Parcel.Use == ECitixParcelUse::Parking)
		{
			EmitBox(FVector(Parcel.Centre.X, Parcel.Centre.Y, Settings.SidewalkHeight + 2.f),
				FVector(Size.X, Size.Y, 4.f), ECitixSurface::AsphaltDark, Parcel.Yaw);
			continue;
		}

		if (Parcel.Use != ECitixParcelUse::Building)
		{
			continue;
		}

		// ---- Building -----------------------------------------------------
		float MinHeight = 0.f;
		float MaxHeight = 0.f;
		GetZoneHeights(Parcel.Zone, Settings, MinHeight, MaxHeight);

		const bool bHeroLandmark = Parcel.LandmarkStyle != ECitixLandmarkStyle::None;
		float Height = FMath::Lerp(MinHeight, MaxHeight, FMath::Pow(Rng.FRand(), 1.35f));
		ECitixBuildingArchetype Archetype = PickZoneArchetype(Parcel.Zone, Rng);
		if (bHeroLandmark)
		{
			// Exact, archetype-independent heights: the hero's own generator shapes the
			// geometry, so letting a random archetype's HeightScale multiply it would make
			// the four silhouettes vary in rank from build to build. This is what keeps the
			// hierarchy legible: twisting (tallest) > pearl > crown-opening > tiered crown.
			switch (Parcel.LandmarkStyle)
			{
			case ECitixLandmarkStyle::TwistingSupertall: Height = Settings.LandmarkMaxHeight; break;
			case ECitixLandmarkStyle::PearlBroadcastTower: Height = Settings.LandmarkMaxHeight * 0.92f; break;
			case ECitixLandmarkStyle::CrownOpeningTower: Height = Settings.LandmarkMaxHeight * 0.86f; break;
			case ECitixLandmarkStyle::TieredCrownTower:  Height = Settings.LandmarkMaxHeight * 0.80f; break;
			default: break;
			}
			++Result.Landmarks;
			OutLandmarkLocations.Add(FVector(Parcel.Centre.X, Parcel.Centre.Y, 0.f));
		}

		// Archetypes have characteristic footprints and height multipliers.
		float FootprintFactor = Rule.BuildCoverage;
		float HeightScale = 1.f;
		switch (Archetype)
		{
		case ECitixBuildingArchetype::Courtyard:     FootprintFactor = 0.98f; HeightScale = 0.60f; break;
		case ECitixBuildingArchetype::Shophouse:     FootprintFactor = 0.96f; HeightScale = 0.50f; break;
		case ECitixBuildingArchetype::Warehouse:     FootprintFactor = 0.94f; HeightScale = 0.60f; break;
		case ECitixBuildingArchetype::ApartmentSlab: FootprintFactor = 0.90f; HeightScale = 0.85f; break;
		case ECitixBuildingArchetype::PodiumTower:   FootprintFactor = 0.76f; HeightScale = 1.20f; break;
		case ECitixBuildingArchetype::CrownedTower:
		case ECitixBuildingArchetype::SetbackTower:
		case ECitixBuildingArchetype::TwistedTower:
		case ECitixBuildingArchetype::HoledSlab:
		case ECitixBuildingArchetype::TwinTower:
		case ECitixBuildingArchetype::CylinderTower:
		case ECitixBuildingArchetype::SpireTower:    FootprintFactor = 0.72f; HeightScale = 1.00f; break;
		default: break;
		}
		if (bHeroLandmark)
		{
			// Heroes are shaped by their own generator; a random archetype must not scale
			// them. Fixed footprint and exact height keep the silhouettes deterministic.
			FootprintFactor = 0.96f;
			HeightScale = 1.f;
		}
		FootprintFactor = FMath::Clamp(FootprintFactor * Rng.FRandRange(0.92f, 1.f), 0.30f, 0.99f);
		Height *= HeightScale;

		// Hero clearance: support buildings within a landmark's neighbourhood step down and
		// dim, so the hero's upper silhouette keeps empty sky around it. The falloff is by
		// true distance to the nearest hero centre, so the gap is a disc, not a rectangle.
		float HeroLightScale = 1.f;
		if (!bHeroLandmark && HeroCentres.Num() > 0)
		{
			float Nearest = TNumericLimits<float>::Max();
			for (const FVector2D& HeroCentre : HeroCentres)
			{
				Nearest = FMath::Min(Nearest, static_cast<float>((Parcel.Centre - HeroCentre).Size()));
			}
			constexpr float InnerClear = 14000.f;
			constexpr float OuterClear = 40000.f;
			const float T = FMath::Clamp((Nearest - InnerClear) / (OuterClear - InnerClear), 0.f, 1.f);
			Height *= FMath::Lerp(0.42f, 1.f, T);
			HeroLightScale = FMath::Lerp(0.55f, 1.f, T);
		}

		FCitixBuildingSpec Spec;
		Spec.Center = Parcel.Centre;
		Spec.Footprint = FVector2D(Size.X * FootprintFactor, Size.Y * FootprintFactor);
		Spec.FloorHeight = FMath::Max(240.f, Rule.FloorHeight);
		Spec.Floors = FMath::Max(1, FMath::RoundToInt(Height / Spec.FloorHeight));
		Spec.District = DistrictType;
		Spec.Usage = ECitixLotUsage::Building;
		Spec.Archetype = Archetype;
		Spec.LandmarkStyle = Parcel.LandmarkStyle;
		Spec.FacadeStyle = Rng.RandRange(0, 5);
		Spec.BaseZ = Settings.SidewalkHeight;
		Spec.bPodium = Rng.FRand() < Rule.PodiumChance;
		Spec.bRoofEquipment = Rng.FRand() < Rule.RoofEquipmentChance;
		Spec.NightLightScale = HeroLightScale;

		TArray<FCitixBoxInstance> Boxes;
		FCitixBuildingGenerator::GenerateBuildingBoxes(Spec, Settings, Rng, Boxes);
		if (bHeroLandmark)
		{
			Result.HeroInstances += Boxes.Num();
			UE_LOG(LogCitix, Display, TEXT("[Citix] HERO %s parcel=(%.0f,%.0f) height=%.0f instances=%d"),
				LandmarkStyleName(Parcel.LandmarkStyle), Parcel.Centre.X, Parcel.Centre.Y, Spec.Height(), Boxes.Num());
		}

		// Vertical illuminated shop signs: the signature of a Shanghai street.
		if (Rule.DetailLevel > 0.4f
			&& (DistrictType == ECitixDistrict::Downtown || DistrictType == ECitixDistrict::Commercial
				|| DistrictType == ECitixDistrict::Residential || DistrictType == ECitixDistrict::Market))
		{
			const float BuildingHeight = Spec.Height();
			const float HalfWidth = Spec.Footprint.X * 0.5f;
			const float HalfDepth = Spec.Footprint.Y * 0.5f;
			const int32 SignCount = Rng.RandRange(0, 3);
			for (int32 Index = 0; Index < SignCount; ++Index)
			{
				const float SignHeight = FMath::Clamp(BuildingHeight * 0.20f, 320.f, 1700.f);
				const float SignWidth = Rng.FRandRange(90.f, 170.f);
				const float Z = Spec.BaseZ + Rng.FRandRange(0.12f, 0.68f) * BuildingHeight + SignHeight * 0.5f;
				const ECitixSurface SignSurface = (Rng.FRand() < 0.62f)
					? ECitixSurface::EmissiveWarm : ECitixSurface::EmissiveCool;
				FCitixBoxInstance Sign;
				if (Rng.FRand() < 0.5f)
				{
					Sign.Center = FVector(Spec.Center.X + HalfWidth + 70.f,
						Spec.Center.Y + Rng.FRandRange(-HalfDepth * 0.6f, HalfDepth * 0.6f), Z);
					Sign.Size = FVector(140.f, SignWidth, SignHeight);
				}
				else
				{
					Sign.Center = FVector(Spec.Center.X + Rng.FRandRange(-HalfWidth * 0.6f, HalfWidth * 0.6f),
						Spec.Center.Y + HalfDepth + 70.f, Z);
					Sign.Size = FVector(SignWidth, 140.f, SignHeight);
				}
				Sign.Surface = SignSurface;
				Boxes.Add(Sign);
			}
		}

		// Facade AC units on believable buildings.
		if (Rule.DetailLevel > 0.5f && Spec.Floors >= 2 && Archetype != ECitixBuildingArchetype::Shophouse)
		{
			const int32 Count = Rng.RandRange(0, 3);
			const float HalfWidth = Spec.Footprint.X * 0.5f;
			const float HalfDepth = Spec.Footprint.Y * 0.5f;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Z = Settings.SidewalkHeight + Rng.FRandRange(0.15f, 0.85f) * Spec.Height();
				FVector2D Position;
				switch (Rng.RandRange(0, 3))
				{
				case 0: Position = FVector2D(HalfWidth + 40.f, Rng.FRandRange(-HalfDepth, HalfDepth)); break;
				case 1: Position = FVector2D(-HalfWidth - 40.f, Rng.FRandRange(-HalfDepth, HalfDepth)); break;
				case 2: Position = FVector2D(Rng.FRandRange(-HalfWidth, HalfWidth), HalfDepth + 40.f); break;
				default: Position = FVector2D(Rng.FRandRange(-HalfWidth, HalfWidth), -HalfDepth - 40.f); break;
				}
				Position += Spec.Center;
				FCitixPropGenerator::AddACUnit(Boxes, FVector(Position.X, Position.Y, Z), Rng);
			}
		}

		// Finally: rotate the whole lot into its block's frame. This is the single place
		// where the plan's orientation is applied to buildings, so the building generator
		// itself never needs to know about it.
		RotateBoxesIntoBlock(Boxes, Parcel.Centre, Parcel.Yaw);

		for (const FCitixBoxInstance& Box : Boxes)
		{
			Emit(Box);
			++Result.TotalInstances;
		}
		++Result.Buildings;
	}

	Result.LampLocations = OutLampLocations.Num();
	return Result;
}
