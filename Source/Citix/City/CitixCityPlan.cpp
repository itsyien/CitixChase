// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixCityPlan.h"
#include "Core/CitixCitySettings.h"
#include "Citix.h"

namespace
{
	// -----------------------------------------------------------------------
	// Small geometry helpers
	// -----------------------------------------------------------------------

	FORCEINLINE float Cross2D(const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; }

	FORCEINLINE float SignedArea(const TArray<FVector2D>& Poly)
	{
		float Twice = 0.f;
		for (int32 Index = 0; Index < Poly.Num(); ++Index)
		{
			const FVector2D& A = Poly[Index];
			const FVector2D& B = Poly[(Index + 1) % Poly.Num()];
			Twice += Cross2D(A, B);
		}
		return Twice * 0.5f;
	}

	FORCEINLINE FVector2D PolygonCentre(const TArray<FVector2D>& Poly)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Poly)
		{
			Sum += P;
		}
		return Poly.Num() > 0 ? Sum / static_cast<float>(Poly.Num()) : FVector2D::ZeroVector;
	}

	FORCEINLINE bool PointInQuad(const TArray<FVector2D>& Quad, const FVector2D& P)
	{
		// Works for any convex quad, in either winding.
		const float Sign = FMath::Sign(Cross2D(Quad[1] - Quad[0], P - Quad[0]));
		for (int32 Index = 0; Index < Quad.Num(); ++Index)
		{
			const FVector2D& A = Quad[Index];
			const FVector2D& B = Quad[(Index + 1) % Quad.Num()];
			const float C = Cross2D(B - A, P - A);
			if (C * Sign < -1.f)
			{
				return false;
			}
		}
		return true;
	}

	/** Closest point parameter on segment AB to P (0..1). */
	FORCEINLINE float ProjectOnSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		return (LenSq > KINDA_SMALL_NUMBER)
			? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f) : 0.f;
	}

	/** Smallest angle between two directions, degrees, always in 0..90. */
	FORCEINLINE float AcuteAngleBetween(const FVector2D& A, const FVector2D& B)
	{
		const float Dot = FMath::Clamp(FMath::Abs(FVector2D::DotProduct(A.GetSafeNormal(), B.GetSafeNormal())), 0.f, 1.f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}

	/** Angle between two directions, degrees, in 0..180 (0 = straight on). */
	FORCEINLINE float TurnAngle(const FVector2D& A, const FVector2D& B)
	{
		const float Dot = FMath::Clamp(FVector2D::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.f, 1.f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}

	/** A district's planned region, in river space. */
	struct FDistrictSpec
	{
		const TCHAR* Name;
		ECitixDistrict Type;
		ECitixPlanZone Zone;
		int32 Side;              // -1 / +1
		float UMin, UMax;
		float TMin, TMax;
		float BlockPitchScale;
		float JitterScale;
		float RotationDegrees;   // this quarter's own street bearing
		float LotPitch;          // typical parcel size inside this quarter, cm
		bool bLocalStreets;
	};

	/**
	 * The macro plan of the city. The river runs through the middle, so the city is a band
	 * along it: the historic bank carries the waterfront quarter and the dense old core,
	 * the opposite bank carries the financial cluster, and the ends of the band take the
	 * commercial and industrial quarters. Districts are a partition of the band, and their
	 * borders are river positions and arterial rows - not arbitrary lines.
	 */
	const FDistrictSpec DistrictSpecs[] =
	{
		// name                  type                         zone                          side  uMin   uMax  tMin  tMax  pitch jitter  rot  lot      local
		{ TEXT("North Port Works"),  ECitixDistrict::Industrial, ECitixPlanZone::LowDensity,        -1, -0.08f, 0.26f, 0.00f, 1.00f, 1.45f, 0.20f,   0.f, 6500.f, false },
		{ TEXT("Docklands"),         ECitixDistrict::Industrial, ECitixPlanZone::LowDensity,         1, -0.08f, 0.26f, 0.00f, 1.00f, 1.45f, 0.20f,   0.f, 6500.f, false },
		{ TEXT("Bund Promenade"),    ECitixDistrict::Waterfront, ECitixPlanZone::MediumDensity,      -1,  0.26f, 0.62f, 0.00f, 0.34f, 1.00f, 0.10f,   0.f, 2600.f, false },
		{ TEXT("Old City Core"),     ECitixDistrict::Downtown,   ECitixPlanZone::SecondaryCluster,   -1,  0.26f, 0.62f, 0.34f, 1.00f, 0.62f, 0.55f,   0.f, 1700.f, true  },
		{ TEXT("Lujiazui Reach"),    ECitixDistrict::Financial,  ECitixPlanZone::PrimaryCluster,      1,  0.26f, 0.62f, 0.00f, 0.55f, 1.15f, 0.05f,   0.f, 3600.f, false },
		{ TEXT("Central Market Mile"),ECitixDistrict::Commercial,ECitixPlanZone::HighDensity,         1,  0.26f, 1.10f, 0.55f, 1.00f, 0.85f, 0.25f,   0.f, 2600.f, false },
		{ TEXT("Riverside Gardens"), ECitixDistrict::Residential,ECitixPlanZone::MediumDensity,      -1,  0.62f, 1.10f, 0.00f, 1.00f, 0.90f, 0.30f,   0.f, 3000.f, false },
		{ TEXT("East Commercial"),   ECitixDistrict::Commercial, ECitixPlanZone::HighDensity,         1,  0.62f, 1.10f, 0.00f, 0.55f, 0.85f, 0.25f,   0.f, 2600.f, false }
	};

	constexpr int32 DistrictSpecCount = UE_ARRAY_COUNT(DistrictSpecs);

	/** River control points, normalized to CitySize. A few large smooth bends only. */
	const FVector2D RiverControlPoints[] =
	{
		FVector2D(-0.72f, -0.34f),
		FVector2D(-0.34f, -0.20f),
		FVector2D( 0.00f, -0.02f),
		FVector2D( 0.30f,  0.16f),
		FVector2D( 0.72f,  0.26f)
	};

	/** Smooth centripetal Catmull-Rom, so the river has no kinks at its control points. */
	FVector2D CatmullRom(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return 0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2
			+ (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
	}
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

void FCitixCityPlan::GetRiverFrame(float U, FVector2D& OutPoint, FVector2D& OutTangent) const
{
	if (RiverPoints.Num() < 2)
	{
		OutPoint = FVector2D::ZeroVector;
		OutTangent = FVector2D(1.f, 0.f);
		return;
	}

	// The centreline is stored at uniform arc length, so U maps straight onto an index.
	// Outside 0..1 the band is extended along the end tangent, which keeps the lattice
	// running off the map cleanly instead of collapsing.
	const float Scaled = U * (RiverPoints.Num() - 1);
	const int32 Index = FMath::Clamp(FMath::FloorToInt(Scaled), 0, RiverPoints.Num() - 2);
	const float Frac = FMath::Clamp(Scaled - Index, 0.f, 1.f);

	if (U < 0.f || U > 1.f)
	{
		// Past either end the band continues straight along the river's own travel
		// direction. (Getting this backwards folds the band over itself at the first
		// column, which is exactly what the plan validation caught.)
		const bool bBefore = (U < 0.f);
		const int32 EndIndex = bBefore ? 0 : RiverPoints.Num() - 1;
		const int32 Neighbour = bBefore ? 1 : RiverPoints.Num() - 2;
		const FVector2D Forward = (bBefore
			? (RiverPoints[Neighbour] - RiverPoints[EndIndex])
			: (RiverPoints[EndIndex] - RiverPoints[Neighbour])).GetSafeNormal();
		const float Overshoot = (bBefore ? U : (U - 1.f)) * RiverLength;
		OutPoint = RiverPoints[EndIndex] + Forward * Overshoot;
		OutTangent = Forward;
		return;
	}

	OutPoint = FMath::Lerp(RiverPoints[Index], RiverPoints[Index + 1], Frac);
	OutTangent = (RiverPoints[Index + 1] - RiverPoints[Index]).GetSafeNormal();
}

float FCitixCityPlan::GetHalfWidth(float U) const
{
	if (RiverWidths.Num() == 0)
	{
		return 600.f;
	}
	const float Scaled = FMath::Clamp(U, 0.f, 1.f) * (RiverWidths.Num() - 1);
	const int32 Index = FMath::Clamp(FMath::FloorToInt(Scaled), 0, RiverWidths.Num() - 2);
	const float Frac = FMath::Clamp(Scaled - Index, 0.f, 1.f);
	return FMath::Lerp(RiverWidths[Index], RiverWidths[Index + 1], Frac) * 0.5f;
}

int32 FCitixCityPlan::GetDistrictAt(const FVector2D& WorldPoint) const
{
	for (int32 Index = 0; Index < Districts.Num(); ++Index)
	{
		const FCitixPlanDistrict& District = Districts[Index];
		if (District.Boundary.Num() >= 3 && PointInQuad(District.Boundary, WorldPoint))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

ECitixDistrict FCitixCityPlan::GetDistrictTypeAt(const FVector2D& WorldPoint) const
{
	const int32 Index = GetDistrictAt(WorldPoint);
	return Districts.IsValidIndex(Index) ? Districts[Index].Type : ECitixDistrict::OuterCity;
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

FCitixCityPlan FCitixPlanGenerator::Generate(const UCitixCitySettings& Settings, FRandomStream& Rng)
{
	(void)Rng;

	// The plan is validated, not assumed. If an attempt folds a cell or creates an
	// accidental crossing, it is retried with a gentler district rotation rather than
	// shipped broken.
	FCitixCityPlan Plan;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		const float RotationScale = FMath::Pow(0.5f, static_cast<float>(Attempt));
		FRandomStream AttemptRng(Settings.Seed + Attempt * 7919);
		Plan = GenerateAttempt(Settings, AttemptRng, RotationScale);
		Validate(Plan);

		if (Plan.Report.IllegalCrossings == 0 && Plan.Report.InvertedCells == 0)
		{
			if (Attempt > 0)
			{
				UE_LOG(LogCitix, Warning, TEXT("[Citix] Plan needed %d attempts; district rotation scaled to %.2f."),
					Attempt + 1, RotationScale);
			}

			UE_LOG(LogCitix, Log,
				TEXT("[Citix] Plan (seed %d): %d districts, %d roads (%d arterial / %d secondary / %d local), ")
				TEXT("%d bridges, %d blocks, %d nodes."),
				Plan.Seed, Plan.Districts.Num(), Plan.Report.RoadCount, Plan.Report.ArterialCount,
				Plan.Report.SecondaryCount, Plan.Report.LocalCount, Plan.Report.BridgeCount,
				Plan.Report.BlockCount, Plan.Report.NodeCount);
			UE_LOG(LogCitix, Log,
				TEXT("[Citix] Plan checks: illegalCrossings=%d duplicateSegments=%d degenerateCells=%d ")
				TEXT("acuteJunctions=%d minAngle=%.0fdeg medianAngle=%.0fdeg bridgeWorst=%.1fdeg bridgeMean=%.1fdeg"),
				Plan.Report.IllegalCrossings, Plan.Report.DuplicateSegments, Plan.Report.InvertedCells,
				Plan.Report.AcuteJunctions, Plan.Report.MinIntersectionAngle, Plan.Report.MedianIntersectionAngle,
				Plan.Report.WorstBridgeAngle, Plan.Report.MeanBridgeAngle);
			UE_LOG(LogCitix, Log,
				TEXT("[Citix] Plan lengths: total %.0f m, arterial %.0f m; straight %.0f%%, curved %.0f%%, diagonal %.0f%%; mean block %.0f m2."),
				Plan.Report.TotalRoadLength / 100.f, Plan.Report.ArterialLength / 100.f,
				Plan.Report.StraightPercent, Plan.Report.CurvedPercent, Plan.Report.DiagonalPercent,
				Plan.Report.MeanBlockArea / 10000.f);
			return Plan;
		}

		UE_LOG(LogCitix, Warning,
			TEXT("[Citix] Plan attempt %d rejected: %d illegal crossings, %d degenerate cells, %d duplicates. Retrying gentler."),
			Attempt + 1, Plan.Report.IllegalCrossings, Plan.Report.InvertedCells, Plan.Report.DuplicateSegments);
	}

	UE_LOG(LogCitix, Error, TEXT("[Citix] Plan could not be made clean; shipping the best attempt."));
	return Plan;
}

FCitixCityPlan FCitixPlanGenerator::GenerateAttempt(const UCitixCitySettings& Settings, FRandomStream& Rng,
	float RotationScale)
{
	FCitixCityPlan Plan;
	Plan.Seed = Settings.Seed;
	Plan.CitySize = FMath::Max(20000.f, Settings.CitySize);

	const float CitySize = Plan.CitySize;
	// Column 0..ColumnCount-1 spans the river band; rows 0..RowCount-1 span one bank.
	Plan.ColumnCount = FMath::Clamp(Settings.PlanColumns, 6, 40);
	Plan.RowCount = FMath::Clamp(Settings.PlanRows, 3, 16);
	const int32 RowSlots = Plan.RowCount * 2;

	// =======================================================================
	// STAGE 1 - MACRO GEOGRAPHY: the river
	// =======================================================================
	{
		// Spline the control points, then resample at uniform arc length.
		TArray<FVector2D> Dense;
		const int32 ControlCount = UE_ARRAY_COUNT(RiverControlPoints);
		constexpr int32 PerSpan = 64;
		for (int32 Span = 0; Span < ControlCount - 1; ++Span)
		{
			const FVector2D P0 = RiverControlPoints[FMath::Max(0, Span - 1)];
			const FVector2D P1 = RiverControlPoints[Span];
			const FVector2D P2 = RiverControlPoints[Span + 1];
			const FVector2D P3 = RiverControlPoints[FMath::Min(ControlCount - 1, Span + 2)];
			for (int32 Step = 0; Step < PerSpan; ++Step)
			{
				Dense.Add(CatmullRom(P0, P1, P2, P3, static_cast<float>(Step) / PerSpan) * CitySize);
			}
		}
		Dense.Add(RiverControlPoints[ControlCount - 1] * CitySize);

		// Cumulative arc length.
		TArray<float> Cumulative;
		Cumulative.Add(0.f);
		for (int32 Index = 1; Index < Dense.Num(); ++Index)
		{
			Cumulative.Add(Cumulative[Index - 1] + FVector2D::Distance(Dense[Index - 1], Dense[Index]));
		}
		Plan.RiverLength = Cumulative.Last();

		const int32 SampleCount = 256;
		Plan.RiverPoints.Reserve(SampleCount);
		Plan.RiverWidths.Reserve(SampleCount);
		int32 Cursor = 0;
		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			const float Target = Plan.RiverLength * static_cast<float>(Sample) / (SampleCount - 1);
			while (Cursor < Cumulative.Num() - 2 && Cumulative[Cursor + 1] < Target)
			{
				++Cursor;
			}
			const float Span = FMath::Max(1.f, Cumulative[Cursor + 1] - Cumulative[Cursor]);
			const float Frac = FMath::Clamp((Target - Cumulative[Cursor]) / Span, 0.f, 1.f);
			Plan.RiverPoints.Add(FMath::Lerp(Dense[Cursor], Dense[Cursor + 1], Frac));

			// The river widens downstream toward the estuary.
			const float U = static_cast<float>(Sample) / (SampleCount - 1);
			const float Width = Settings.RiverWidth * (1.f + 0.55f * FMath::Pow(U, 1.6f));
			Plan.RiverWidths.Add(Width);
			Plan.MaxRiverWidth = FMath::Max(Plan.MaxRiverWidth, Width);
		}

		Plan.RiverTangents.Reserve(SampleCount);
		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			const int32 Previous = FMath::Max(0, Sample - 1);
			const int32 Next = FMath::Min(SampleCount - 1, Sample + 1);
			Plan.RiverTangents.Add((Plan.RiverPoints[Next] - Plan.RiverPoints[Previous]).GetSafeNormal());
		}
	}

	// =======================================================================
	// STAGE 2 - DISTRICT PLANNING
	// Boundaries are river positions and arterial rows, so they follow the geography.
	// =======================================================================
	for (int32 Index = 0; Index < DistrictSpecCount; ++Index)
	{
		const FDistrictSpec& Spec = DistrictSpecs[Index];
		FCitixPlanDistrict District;
		District.Name = Spec.Name;
		District.Type = Spec.Type;
		District.Zone = Spec.Zone;
		District.Side = (Spec.Side < 0) ? -1 : 1;
		District.UMin = Spec.UMin;
		District.UMax = Spec.UMax;
		District.TMin = Spec.TMin;
		District.TMax = Spec.TMax;
		District.BlockPitchScale = Spec.BlockPitchScale;
		District.JitterScale = Spec.JitterScale;
		District.RotationDegrees = Spec.RotationDegrees;
		District.LotPitch = Spec.LotPitch;
		District.bAllowLocalStreets = Spec.bLocalStreets;
		Plan.Districts.Add(MoveTemp(District));
	}

	// =======================================================================
	// STAGE 3 - LATTICE
	// Nodes are placed in river space: columns across the river (the only place a
	// crossing can happen), rows along it. The outer row position is an irregular
	// growth boundary, which is what gives the city an organic edge.
	// =======================================================================
	auto DistrictAtParam = [&Plan](float U, int32 Side, float T) -> int32
	{
		constexpr float Eps = 1e-4f;
		for (int32 Index = 0; Index < Plan.Districts.Num(); ++Index)
		{
			const FCitixPlanDistrict& District = Plan.Districts[Index];
			if (District.Side == Side && U >= District.UMin - Eps && U <= District.UMax + Eps
				&& T >= District.TMin - Eps && T <= District.TMax + Eps)
			{
				return Index;
			}
		}
		// Fall back to the nearest region so edge nodes are never unassigned.
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Plan.Districts.Num(); ++Index)
		{
			const FCitixPlanDistrict& District = Plan.Districts[Index];
			if (District.Side != Side)
			{
				continue;
			}
			const float Du = FMath::Max(0.f, FMath::Max(District.UMin - U, U - District.UMax));
			const float Dt = FMath::Max(0.f, FMath::Max(District.TMin - T, T - District.TMax));
			const float Score = Du + Dt + 0.001f * Index;
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	};

	// Land depth on a bank at a position along the river: a smooth, slowly varying
	// function, so the outer boundary is irregular but never doubles back.
	auto LandDepth = [&Settings, CitySize](float U, int32 Side) -> float
	{
		const float Phase = 0.5f * static_cast<float>(Side);
		const float Lobe = 0.55f * FMath::Sin(2.f * PI * (1.35f * U + 0.13f + Phase))
			+ 0.45f * FMath::Sin(2.f * PI * (0.85f * U - 0.29f - Phase));
		return CitySize * (Settings.PlanDepthBase + Settings.PlanDepthIrregularity * Lobe);
	};

	const float UStart = -0.08f;
	const float UEnd = 1.08f;
	const float RiverGap = Settings.PlanRiverGap;

	// Provisional district centroids. Needed because a district rotates its own streets
	// about its centre, and one of them is inside the other's bounds.
	for (FCitixPlanDistrict& District : Plan.Districts)
	{
		const float MidU = (District.UMin + District.UMax) * 0.5f;
		const float MidT = (District.TMin + District.TMax) * 0.5f;
		FVector2D Point;
		FVector2D Tangent;
		Plan.GetRiverFrame(MidU, Point, Tangent);
		const FVector2D Normal(-Tangent.Y, Tangent.X);
		const float Offset = Plan.GetHalfWidth(MidU) + RiverGap + MidT * LandDepth(MidU, District.Side);
		District.Centroid = Point + Normal * (Offset * static_cast<float>(District.Side));
	}

	// Street bearings as a smooth FIELD over the plan: a quarter can have its own bearing
	// without tearing at the seam. Blending the ANGLE (about one global pivot) rather than
	// blending whole transforms is what keeps the map continuous, and a continuous map
	// cannot fold.
	auto RotationField = [&Plan, &Settings](float U, int32 Side, float T) -> float
	{
		const float BlendWidth = FMath::Max(0.05f, Settings.PlanDistrictRotationBlend);
		float Weighted = 0.f;
		float Total = 0.f;
		for (const FCitixPlanDistrict& District : Plan.Districts)
		{
			if (District.Side != Side || FMath::IsNearlyZero(District.RotationDegrees))
			{
				continue;
			}
			const float Du = FMath::Max(FMath::Max(District.UMin - U, U - District.UMax), 0.f);
			const float Dt = FMath::Max(FMath::Max(District.TMin - T, T - District.TMax), 0.f);
			const float Distance = FMath::Sqrt(Du * Du + Dt * Dt);
			const float Weight = 1.f - FMath::SmoothStep(0.f, BlendWidth, Distance);
			Weighted += District.RotationDegrees * Weight;
			Total += Weight;
		}
		return (Total > 1e-4f) ? Weighted / Total : 0.f;
	};
	const FVector2D RotationPivot = FVector2D::ZeroVector;

	Plan.Nodes.SetNum(Plan.ColumnCount * RowSlots);
	for (int32 Column = 0; Column < Plan.ColumnCount; ++Column)
	{
		const float U = FMath::Lerp(UStart, UEnd, static_cast<float>(Column) / (Plan.ColumnCount - 1));
		FVector2D Point;
		FVector2D Tangent;
		Plan.GetRiverFrame(U, Point, Tangent);
		const FVector2D Normal(-Tangent.Y, Tangent.X);
		const float HalfWidth = Plan.GetHalfWidth(U);

		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float Depth = LandDepth(U, Side);
			for (int32 Row = 0; Row < Plan.RowCount; ++Row)
			{
				const float T = static_cast<float>(Row) / (Plan.RowCount - 1);
				const float Offset = HalfWidth + RiverGap + T * Depth;

				FVector2D Position = Point + Normal * (Offset * static_cast<float>(Side));

				// Structured irregularity: two smooth, low-frequency waves. Deliberately
				// NOT white noise - randomness is what makes a plan look chaotic. The
				// second wave is scaled by the district, so the old core gets wavier
				// streets than the CBD while the lattice stays orderly.
				const int32 DistrictIndex = DistrictAtParam(U, Side, T);
				const float JitterScale = Plan.Districts.IsValidIndex(DistrictIndex)
					? Plan.Districts[DistrictIndex].JitterScale : 0.f;

				const float Wave1 = 2.f * PI * (U * Settings.PlanJitterWavelength + 0.18f * T);
				const float Wave2 = 2.f * PI * (U * Settings.PlanJitterWavelength * 2.7f + 0.31f * T + 0.5f);
				const float Amplitude = Settings.PlanNodeJitter * (0.35f + JitterScale);

				FVector2D Nudge = Tangent * (Amplitude * FMath::Sin(Wave1))
					+ Normal * (Amplitude * 0.55f * FMath::Sin(Wave1 * 1.6f + 1.1f));
				Nudge += Tangent * (Amplitude * 0.7f * JitterScale * FMath::Sin(Wave2))
					+ Normal * (Amplitude * 0.5f * JitterScale * FMath::Sin(Wave2 * 1.3f - 0.7f));
				Position += Nudge;

				// Give this quarter its own street bearing, blended in smoothly.
				const float RotationDegrees = RotationField(U, Side, T) * RotationScale;
				if (!FMath::IsNearlyZero(RotationDegrees))
				{
					const float Angle = FMath::DegreesToRadians(RotationDegrees);
					const FVector2D Relative = Position - RotationPivot;
					const float Cos = FMath::Cos(Angle);
					const float Sin = FMath::Sin(Angle);
					Position = RotationPivot + FVector2D(
						Relative.X * Cos - Relative.Y * Sin,
						Relative.X * Sin + Relative.Y * Cos);
				}

				Plan.Nodes[Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row))] = Position;
			}
		}
	}

	// =======================================================================
	// STAGE 4 - MAJOR ARTERIALS  (level 1: the skeleton)
	// =======================================================================
	const int32 ColumnStride = FMath::Max(2, Settings.PlanArterialColumnStride);
	const int32 RowStride = FMath::Max(2, Settings.PlanArterialRowStride);

	// Rows: the riverside boulevard, plus rows on the stride, plus the outer bypass.
	TArray<int32> ArterialRows;
	for (int32 Row = 0; Row < Plan.RowCount; Row += RowStride)
	{
		ArterialRows.AddUnique(Row);
	}
	ArterialRows.AddUnique(Plan.RowCount - 1);

	// Bridges use arterial columns, so crossings always land on an important corridor.
	TArray<int32> BridgeColumns;
	if (Settings.PlanBridgeCount > 0)
	{
		const int32 Spread = FMath::Max(1, (Plan.ColumnCount - 2) / (Settings.PlanBridgeCount + 1));
		for (int32 Index = 0; Index < Settings.PlanBridgeCount; ++Index)
		{
			const int32 Column = 2 + Spread * Index;
			if (Column < Plan.ColumnCount - 1)
			{
				BridgeColumns.Add(Column);
			}
		}
	}

	auto IsArterialColumn = [ColumnStride, &BridgeColumns](int32 Column)
	{
		return (Column % ColumnStride) == 0 || BridgeColumns.Contains(Column);
	};

	// --- Level 1 rows ---
	for (int32 Row : ArterialRows)
	{
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			FCitixPlanRoad Road;
			Road.Level = ECitixRoadLevel::Arterial;
			Road.bRiverside = (Row == 0);
			for (int32 Column = 0; Column < Plan.ColumnCount; ++Column)
			{
				Road.Nodes.Add(Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row)));
			}
			Road.DistrictIndex = DistrictAtParam(0.5f, Side, static_cast<float>(Row) / (Plan.RowCount - 1));
			Plan.Roads.Add(MoveTemp(Road));
		}
	}

	// --- Level 1 columns ---
	for (int32 Column = 0; Column < Plan.ColumnCount; ++Column)
	{
		const bool bBridge = BridgeColumns.Contains(Column);
		if (!IsArterialColumn(Column))
		{
			continue;
		}

		if (bBridge)
		{
			// One continuous corridor: outward on one bank, across the river, outward on
			// the other. Because both bank nodes sit on the same river normal, the crossing
			// is exactly perpendicular to the river.
			FCitixPlanRoad Road;
			Road.Level = ECitixRoadLevel::Arterial;
			for (int32 Row = Plan.RowCount - 1; Row >= 0; --Row)
			{
				Road.Nodes.Add(Plan.NodeIndex(Column, Plan.SlotForBank(-1, Row)));
			}
			for (int32 Row = 0; Row < Plan.RowCount; ++Row)
			{
				Road.Nodes.Add(Plan.NodeIndex(Column, Plan.SlotForBank(1, Row)));
			}

			// Split it into three roads so the bridge itself can be drawn and counted
			// separately from the approaches.
			const int32 BankAOuter = 0;
			const int32 BankAIn = Plan.RowCount - 1;
			const int32 BankBIn = Plan.RowCount;
			const int32 BankBOuter = 2 * Plan.RowCount - 1;

			FCitixPlanRoad ApproachA = Road;
			ApproachA.Nodes = TArray<int32>(Road.Nodes.GetData() + BankAOuter, BankAIn + 1);
			FCitixPlanRoad Bridge = Road;
			Bridge.Nodes = { Road.Nodes[BankAIn], Road.Nodes[BankBIn] };
			Bridge.bBridge = true;
			FCitixPlanRoad ApproachB = Road;
			ApproachB.Nodes = TArray<int32>(Road.Nodes.GetData() + BankBIn, Plan.RowCount);

			Plan.Roads.Add(MoveTemp(ApproachA));
			Plan.Roads.Add(MoveTemp(Bridge));
			Plan.Roads.Add(MoveTemp(ApproachB));
		}
		else
		{
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				FCitixPlanRoad Road;
				Road.Level = ECitixRoadLevel::Arterial;
				for (int32 Row = 0; Row < Plan.RowCount; ++Row)
				{
					Road.Nodes.Add(Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row)));
				}
				Road.DistrictIndex = DistrictAtParam(
					FMath::Lerp(UStart, UEnd, static_cast<float>(Column) / (Plan.ColumnCount - 1)), Side, 0.f);
				Plan.Roads.Add(MoveTemp(Road));
			}
		}
	}

	// --- Level 1 street bearings ---
	// There are no diagonal avenues crossing the grid: a straight line crossing an
	// orthogonal grid always produces an angle below 60 degrees, which is exactly the
	// geometry this plan is supposed to avoid. Diagonal streets are expressed instead as
	// districts with their own bearing (see the node rotation above), so they meet their
	// neighbours at 65-115 degrees. That also means bridges stay 'bBridge' on a level-1
	// corridor and nothing else crosses the river.

	// =======================================================================
	// STAGE 5 - BRIDGE PLANNING
	// Bridges were created with the arterial columns above; here we only record which
	// corridors are crossings so the report and the preview can treat them specially.
	// =======================================================================

	// =======================================================================
	// STAGE 6 - SECONDARY ROADS  (level 2: define the neighbourhoods)
	// =======================================================================
	bool bArterialRowTaken[64] = { false };
	for (int32 Row : ArterialRows)
	{
		if (Row >= 0 && Row < 64)
		{
			bArterialRowTaken[Row] = true;
		}
	}

	for (int32 Row = 0; Row < Plan.RowCount; ++Row)
	{
		if (bArterialRowTaken[Row])
		{
			continue;
		}
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			FCitixPlanRoad Road;
			Road.Level = ECitixRoadLevel::Secondary;
			for (int32 Column = 0; Column < Plan.ColumnCount; ++Column)
			{
				Road.Nodes.Add(Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row)));
			}
			Road.DistrictIndex = DistrictAtParam(0.5f, Side, static_cast<float>(Row) / (Plan.RowCount - 1));
			Plan.Roads.Add(MoveTemp(Road));
		}
	}

	for (int32 Column = 0; Column < Plan.ColumnCount; ++Column)
	{
		if (IsArterialColumn(Column))
		{
			continue;
		}
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			FCitixPlanRoad Road;
			Road.Level = ECitixRoadLevel::Secondary;
			for (int32 Row = 0; Row < Plan.RowCount; ++Row)
			{
				Road.Nodes.Add(Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row)));
			}
			Road.DistrictIndex = DistrictAtParam(
				FMath::Lerp(UStart, UEnd, static_cast<float>(Column) / (Plan.ColumnCount - 1)), Side, 0.f);
			Plan.Roads.Add(MoveTemp(Road));
		}
	}

	// =======================================================================
	// STAGE 7 - LOCAL STREETS  (level 3)
	// Deliberately NOT generated here. Local streets belong with parcel generation:
	// they exist to subdivide a block, so they only make sense once parcels do. The
	// level exists in the hierarchy (ECitixRoadLevel::Local) and the road graph already
	// carries it, so the parcel stage can start emitting them without a refactor.
	// =======================================================================

	// Mark the streets of a quarter that has its own bearing: they are the plan's
	// diagonal connections, meeting the main grid at 65-115 degrees rather than 90.
	for (FCitixPlanRoad& Road : Plan.Roads)
	{
		if (Plan.Districts.IsValidIndex(Road.DistrictIndex)
			&& !FMath::IsNearlyZero(Plan.Districts[Road.DistrictIndex].RotationDegrees))
		{
			Road.bDiagonal = true;
		}
	}

	// =======================================================================
	// STAGE 8 - BLOCK DETECTION
	// Every lattice cell becomes a block with its district and future density zone.
	// No parcels or buildings yet.
	// =======================================================================
	for (int32 Column = 0; Column + 1 < Plan.ColumnCount; ++Column)
	{
		for (int32 Bank = 0; Bank < 2; ++Bank)
		{
			const int32 Side = (Bank == 0) ? -1 : 1;
			for (int32 Row = 0; Row + 1 < Plan.RowCount; ++Row)
			{
				const int32 SlotA = Plan.SlotForBank(Side, Row);
				const int32 SlotB = Plan.SlotForBank(Side, Row + 1);

				FCitixPlanBlock Block;
				Block.Corners.Add(Plan.Nodes[Plan.NodeIndex(Column, SlotA)]);
				Block.Corners.Add(Plan.Nodes[Plan.NodeIndex(Column + 1, SlotA)]);
				Block.Corners.Add(Plan.Nodes[Plan.NodeIndex(Column + 1, SlotB)]);
				Block.Corners.Add(Plan.Nodes[Plan.NodeIndex(Column, SlotB)]);
				Block.Centre = PolygonCentre(Block.Corners);
				Block.Area = FMath::Abs(SignedArea(Block.Corners));
				Block.Column = Column;
				Block.Row = Row;
				Block.Side = Side;
				Block.DistrictIndex = DistrictAtParam(
					FMath::Lerp(UStart, UEnd, (static_cast<float>(Column) + 0.5f) / (Plan.ColumnCount - 1)),
					Side, (static_cast<float>(Row) + 0.5f) / (Plan.RowCount - 1));

				const FCitixPlanDistrict* District = Plan.Districts.IsValidIndex(Block.DistrictIndex)
					? &Plan.Districts[Block.DistrictIndex] : nullptr;
				Block.Zone = District ? District->Zone : ECitixPlanZone::MediumDensity;

				// Reserve the first row of blocks along the river as waterfront open space �?				// but only along PARTS of the bank, not the whole edge. The financial
				// quarter keeps some of its river frontage buildable so the skyline can
				// rise at the water, which is the point of a CBD site.
				if (Row == 0)
				{
					const float U = FMath::Clamp((static_cast<float>(Column) + 0.5f) / (Plan.ColumnCount - 1), 0.f, 1.f);
					const float Pattern = FMath::Frac(FMath::Abs(U * 3.7f));

					bool bReserve = false;
					if (District && District->Type == ECitixDistrict::Waterfront)
					{
						bReserve = true;                       // the promenade quarter
					}
					else if (District && District->Type == ECitixDistrict::Financial)
					{
						bReserve = Pattern < 0.45f;            // a plaza at the water's edge
					}
					else
					{
						bReserve = Pattern < Settings.PlanWaterfrontReserveFraction * 0.7f;
					}

					if (bReserve)
					{
						Block.bWaterfrontReserve = true;
						Block.bOpenSpace = true;
					}
				}

				Plan.Blocks.Add(MoveTemp(Block));
			}
		}
	}

	// =======================================================================
	// STAGE 9 - OPEN SPACE AND SKYLINE CLUSTERING
	// =======================================================================
	// A large park: a compact cluster of cells on the residential bank.
	{
		const int32 ParkColumn = FMath::Clamp(Plan.ColumnCount - 5, 1, Plan.ColumnCount - 2);
		const int32 ParkRow = FMath::Max(1, Plan.RowCount / 2);
		for (FCitixPlanBlock& Block : Plan.Blocks)
		{
			if (Block.Side == -1
				&& Block.Column >= ParkColumn && Block.Column < ParkColumn + 2
				&& Block.Row >= ParkRow && Block.Row < ParkRow + 2)
			{
				Block.bOpenSpace = true;
			}
		}
	}

	// Skyline clusters: only a compact core of a cluster district gets the top zone, so
	// supertalls group instead of scattering across the whole quarter.
	for (int32 Index = 0; Index < Plan.Districts.Num(); ++Index)
	{
		FCitixPlanDistrict& District = Plan.Districts[Index];
		if (District.Zone != ECitixPlanZone::PrimaryCluster && District.Zone != ECitixPlanZone::SecondaryCluster)
		{
			continue;
		}

		// Cluster core: the middle of the district, on the river side.
		FVector2D Core = FVector2D::ZeroVector;
		int32 Count = 0;
		for (const FCitixPlanBlock& Block : Plan.Blocks)
		{
			if (Block.DistrictIndex == Index)
			{
				Core += Block.Centre;
				++Count;
			}
		}
		if (Count == 0)
		{
			continue;
		}
		Core /= static_cast<float>(Count);
		District.Centroid = Core;

		// Radius expressed in cell sizes so it scales with the plan resolution.
		const float CellSpan = FVector2D::Distance(
			Plan.Nodes[Plan.NodeIndex(0, Plan.SlotForBank(District.Side, 0))],
			Plan.Nodes[Plan.NodeIndex(1, Plan.SlotForBank(District.Side, 0))]);
		const float ClusterRadius = CellSpan * ((District.Zone == ECitixPlanZone::PrimaryCluster) ? 1.1f : 0.8f);

		for (FCitixPlanBlock& Block : Plan.Blocks)
		{
			if (Block.DistrictIndex != Index)
			{
				continue;
			}
			if (FVector2D::Distance(Block.Centre, Core) > ClusterRadius)
			{
				Block.Zone = ECitixPlanZone::HighDensity;
			}
		}
	}

	// District boundaries for the preview outline and the centroid labels.
	{
		const float ColumnStep = (UEnd - UStart) / (Plan.ColumnCount - 1);
		for (FCitixPlanDistrict& District : Plan.Districts)
		{
			const int32 FirstColumn = FMath::Clamp(
				FMath::FloorToInt((District.UMin - UStart) / ColumnStep), 0, Plan.ColumnCount - 1);
			const int32 LastColumn = FMath::Clamp(
				FMath::CeilToInt((District.UMax - UStart) / ColumnStep), 0, Plan.ColumnCount - 1);
			const int32 FirstRow = FMath::Clamp(
				FMath::RoundToInt(District.TMin * (Plan.RowCount - 1)), 0, Plan.RowCount - 1);
			const int32 LastRow = FMath::Clamp(
				FMath::RoundToInt(District.TMax * (Plan.RowCount - 1)), 0, Plan.RowCount - 1);

			if (LastColumn <= FirstColumn || LastRow <= FirstRow)
			{
				continue;
			}

			District.Boundary.Add(Plan.Nodes[Plan.NodeIndex(FirstColumn, Plan.SlotForBank(District.Side, FirstRow))]);
			District.Boundary.Add(Plan.Nodes[Plan.NodeIndex(LastColumn, Plan.SlotForBank(District.Side, FirstRow))]);
			District.Boundary.Add(Plan.Nodes[Plan.NodeIndex(LastColumn, Plan.SlotForBank(District.Side, LastRow))]);
			District.Boundary.Add(Plan.Nodes[Plan.NodeIndex(FirstColumn, Plan.SlotForBank(District.Side, LastRow))]);
			District.Centroid = PolygonCentre(District.Boundary);
		}
	}

	// Named open spaces for labels.
	for (FCitixPlanBlock& Block : Plan.Blocks)
	{
		if (!Block.bOpenSpace)
		{
			continue;
		}
		if (Block.bWaterfrontReserve)
		{
			continue;
		}
		// Group inland open space under a single label the first time we see one.
		bool bNamed = false;
		for (const FCitixPlanOpenSpace& Space : Plan.OpenSpaces)
		{
			if (FVector2D::Distance(PolygonCentre(Space.Boundary), Block.Centre) < Plan.CitySize * 0.12f)
			{
				bNamed = true;
				break;
			}
		}
		if (!bNamed)
		{
			FCitixPlanOpenSpace Space;
			Space.Name = TEXT("Central Park");
			Space.Boundary = Block.Corners;
			Plan.OpenSpaces.Add(MoveTemp(Space));
		}
	}

	// Extent of everything placed, for the ground slab.
	Plan.BoundsMin = FVector2D(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	Plan.BoundsMax = FVector2D(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (const FVector2D& Node : Plan.Nodes)
	{
		Plan.BoundsMin.X = FMath::Min(Plan.BoundsMin.X, Node.X);
		Plan.BoundsMin.Y = FMath::Min(Plan.BoundsMin.Y, Node.Y);
		Plan.BoundsMax.X = FMath::Max(Plan.BoundsMax.X, Node.X);
		Plan.BoundsMax.Y = FMath::Max(Plan.BoundsMax.Y, Node.Y);
	}
	// Include the river so the water is fully on the slab.
	for (const FVector2D& Point : Plan.RiverPoints)
	{
		const float Half = Plan.MaxRiverWidth * 0.5f + 4000.f;
		Plan.BoundsMin.X = FMath::Min(Plan.BoundsMin.X, static_cast<float>(Point.X - Half));
		Plan.BoundsMin.Y = FMath::Min(Plan.BoundsMin.Y, static_cast<float>(Point.Y - Half));
		Plan.BoundsMax.X = FMath::Max(Plan.BoundsMax.X, static_cast<float>(Point.X + Half));
		Plan.BoundsMax.Y = FMath::Max(Plan.BoundsMax.Y, static_cast<float>(Point.Y + Half));
	}

	return Plan;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

void FCitixPlanGenerator::Validate(FCitixCityPlan& Plan)
{
	FCitixPlanReport Report;
	Report.NodeCount = Plan.Nodes.Num();
	Report.RoadCount = Plan.Roads.Num();
	Report.BlockCount = Plan.Blocks.Num();

	// ---- Flatten to segments -------------------------------------------
	struct FSegment { int32 A, B; ECitixRoadLevel Level; bool bBridge; bool bDiagonal; };
	TArray<FSegment> Segments;
	for (const FCitixPlanRoad& Road : Plan.Roads)
	{
		switch (Road.Level)
		{
		case ECitixRoadLevel::Arterial:  ++Report.ArterialCount;  break;
		case ECitixRoadLevel::Secondary: ++Report.SecondaryCount; break;
		default:                         ++Report.LocalCount;     break;
		}
		if (Road.bBridge)
		{
			++Report.BridgeCount;
		}
		if (Road.bDiagonal)
		{
			++Report.DiagonalCount;
		}

		for (int32 Index = 0; Index + 1 < Road.Nodes.Num(); ++Index)
		{
			FSegment Segment;
			Segment.A = Road.Nodes[Index];
			Segment.B = Road.Nodes[Index + 1];
			Segment.Level = Road.Level;
			Segment.bBridge = Road.bBridge;
			Segment.bDiagonal = Road.bDiagonal;
			Segments.Add(Segment);

			if (Plan.IsValidNode(Segment.A) && Plan.IsValidNode(Segment.B))
			{
				const float Length = FVector2D::Distance(Plan.Nodes[Segment.A], Plan.Nodes[Segment.B]);
				Report.TotalRoadLength += Length;
				if (Road.Level == ECitixRoadLevel::Arterial)
				{
					Report.ArterialLength += Length;
				}
			}
		}
	}

	// ---- Duplicate segments -------------------------------------------
	{
		TSet<uint64> Seen;
		for (const FSegment& Segment : Segments)
		{
			const uint64 Key = (static_cast<uint64>(FMath::Min(Segment.A, Segment.B)) << 32)
				| static_cast<uint32>(FMath::Max(Segment.A, Segment.B));
			if (Seen.Contains(Key))
			{
				++Report.DuplicateSegments;
			}
			Seen.Add(Key);
		}
	}

	// ---- Crossings that are not at a shared node ----------------------
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FSegment& S = Segments[Index];
		if (!Plan.IsValidNode(S.A) || !Plan.IsValidNode(S.B))
		{
			continue;
		}
		const FVector2D& A0 = Plan.Nodes[S.A];
		const FVector2D& A1 = Plan.Nodes[S.B];
		for (int32 Other = Index + 1; Other < Segments.Num(); ++Other)
		{
			const FSegment& T = Segments[Other];
			if (!Plan.IsValidNode(T.A) || !Plan.IsValidNode(T.B))
			{
				continue;
			}
			// Sharing a node is a legitimate intersection.
			if (S.A == T.A || S.A == T.B || S.B == T.A || S.B == T.B)
			{
				continue;
			}
			const FVector2D& B0 = Plan.Nodes[T.A];
			const FVector2D& B1 = Plan.Nodes[T.B];
			const FVector2D RA = A1 - A0;
			const FVector2D RB = B1 - B0;
			const float Denom = Cross2D(RA, RB);
			if (FMath::Abs(Denom) < 1e-6f)
			{
				continue;
			}
			const FVector2D BA = B0 - A0;
			const float U = Cross2D(BA, RB) / Denom;
			const float V = Cross2D(BA, RA) / Denom;
			if (U > 0.002f && U < 0.998f && V > 0.002f && V < 0.998f)
			{
				++Report.IllegalCrossings;
				if (Report.IllegalCrossings <= 6)
				{
					UE_LOG(LogCitix, Warning,
						TEXT("[Citix]   crossing %d: seg(%d-%d)=%.0f,%.0f->%.0f,%.0f  x  seg(%d-%d)=%.0f,%.0f->%.0f,%.0f"),
						Report.IllegalCrossings, S.A, S.B, A0.X, A0.Y, A1.X, A1.Y,
						T.A, T.B, B0.X, B0.Y, B1.X, B1.Y);
				}
			}
		}
	}

	// ---- Cell winding ---------------------------------------------------
	// The two banks run in opposite directions, so the test is for a *degenerate* cell
	// (collapsed or folded), not for a particular winding.
	{
		float MinArea = TNumericLimits<float>::Max();
		float MaxArea = 0.f;
		for (int32 Column = 0; Column + 1 < Plan.ColumnCount; ++Column)
		{
			for (int32 Bank = 0; Bank < 2; ++Bank)
			{
				const int32 Side = (Bank == 0) ? -1 : 1;
				for (int32 Row = 0; Row + 1 < Plan.RowCount; ++Row)
				{
					TArray<FVector2D> Quad;
					Quad.Add(Plan.Nodes[Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row))]);
					Quad.Add(Plan.Nodes[Plan.NodeIndex(Column + 1, Plan.SlotForBank(Side, Row))]);
					Quad.Add(Plan.Nodes[Plan.NodeIndex(Column + 1, Plan.SlotForBank(Side, Row + 1))]);
					Quad.Add(Plan.Nodes[Plan.NodeIndex(Column, Plan.SlotForBank(Side, Row + 1))]);
					const float Area = FMath::Abs(SignedArea(Quad));
					MinArea = FMath::Min(MinArea, Area);
					MaxArea = FMath::Max(MaxArea, Area);
					if (Area < 1.f)
					{
						++Report.InvertedCells;
					}
				}
			}
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix]   cell area: min %.0f m2, max %.0f m2"),
			MinArea / 10000.f, MaxArea / 10000.f);
	}

	// ---- Intersection angles -------------------------------------------
	{
		TArray<TArray<FVector2D>> IncidentDirections;
		IncidentDirections.SetNum(Plan.Nodes.Num());
		for (const FSegment& Segment : Segments)
		{
			if (!Plan.IsValidNode(Segment.A) || !Plan.IsValidNode(Segment.B))
			{
				continue;
			}
			const FVector2D Direction = (Plan.Nodes[Segment.B] - Plan.Nodes[Segment.A]).GetSafeNormal();
			IncidentDirections[Segment.A].Add(Direction);
			IncidentDirections[Segment.B].Add(-Direction);
		}

		TArray<float> JunctionAngles;
		for (int32 Node = 0; Node < IncidentDirections.Num(); ++Node)
		{
			const TArray<FVector2D>& Directions = IncidentDirections[Node];
			if (Directions.Num() < 3)
			{
				continue;
			}
			float Smallest = 180.f;
			for (int32 Index = 0; Index < Directions.Num(); ++Index)
			{
				for (int32 Other = Index + 1; Other < Directions.Num(); ++Other)
				{
					// The angle between two incident road directions, as seen at the junction.
					Smallest = FMath::Min(Smallest, TurnAngle(Directions[Index], Directions[Other]));
				}
			}
			JunctionAngles.Add(Smallest);
			if (Smallest < 55.f)
			{
				++Report.AcuteJunctions;
			}
		}

		if (JunctionAngles.Num() > 0)
		{
			JunctionAngles.Sort();
			Report.MinIntersectionAngle = JunctionAngles[0];
			Report.MedianIntersectionAngle = JunctionAngles[JunctionAngles.Num() / 2];
		}
	}

	// ---- Bridge squareness ----------------------------------------------
	{
		float Worst = 0.f;
		float Sum = 0.f;
		int32 Count = 0;
		for (const FCitixPlanRoad& Road : Plan.Roads)
		{
			if (!Road.bBridge || Road.Nodes.Num() < 2)
			{
				continue;
			}
			const FVector2D A = Plan.Nodes[Road.Nodes[0]];
			const FVector2D B = Plan.Nodes[Road.Nodes[1]];
			const FVector2D BridgeDirection = (B - A).GetSafeNormal();

			// River tangent nearest the bridge midpoint.
			const FVector2D Mid = (A + B) * 0.5f;
			int32 Nearest = 0;
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Plan.RiverPoints.Num(); ++Index)
			{
				const float Distance = FVector2D::Distance(Plan.RiverPoints[Index], Mid);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Nearest = Index;
				}
			}
			const FVector2D RiverDirection = Plan.RiverTangents[Nearest];

			// 90 degrees means the bridge crosses square to the river.
			const float Deviation = FMath::Abs(90.f - TurnAngle(BridgeDirection, RiverDirection));
			Worst = FMath::Max(Worst, Deviation);
			Sum += Deviation;
			++Count;
		}
		Report.WorstBridgeAngle = Worst;
		Report.MeanBridgeAngle = (Count > 0) ? Sum / Count : 0.f;
	}

	// ---- Length classification ------------------------------------------
	// A road counts as diagonal when it runs at an angle to the river-aligned frame that
	// the plan's main grid uses. That is a property of the geometry, not of which district
	// a road happens to be labelled with, so it measures the real mix.
	{
		float Straight = 0.f;
		float Curved = 0.f;
		float Diagonal = 0.f;
		for (const FCitixPlanRoad& Road : Plan.Roads)
		{
			if (Road.Nodes.Num() < 2)
			{
				continue;
			}
			float Length = 0.f;
			for (int32 Index = 0; Index + 1 < Road.Nodes.Num(); ++Index)
			{
				Length += FVector2D::Distance(Plan.Nodes[Road.Nodes[Index]], Plan.Nodes[Road.Nodes[Index + 1]]);
			}
			if (Length < 1.f)
			{
				continue;
			}

			const FVector2D Start = Plan.Nodes[Road.Nodes[0]];
			const FVector2D End = Plan.Nodes[Road.Nodes.Last()];
			const FVector2D Chord = End - Start;
			const float ChordLength = Chord.Size();
			if (ChordLength < 1.f)
			{
				continue;
			}
			const FVector2D ChordDirection = Chord / ChordLength;

			// Angle to the river tangent nearest the road's midpoint: 0 or 90 degrees
			// means the road belongs to the main grid.
			const FVector2D Mid = (Start + End) * 0.5f;
			FVector2D Nearest;
			FVector2D RiverDirection;
			Plan.GetRiverFrame(0.f, Nearest, RiverDirection);
			{
				float BestDistance = TNumericLimits<float>::Max();
				for (int32 Index = 0; Index < Plan.RiverPoints.Num(); ++Index)
				{
					const float Distance = FVector2D::Distance(Plan.RiverPoints[Index], Mid);
					if (Distance < BestDistance)
					{
						BestDistance = Distance;
						RiverDirection = Plan.RiverTangents[Index];
					}
				}
			}
			const float OffAxis = FMath::Min(
				AcuteAngleBetween(ChordDirection, RiverDirection),
				90.f - AcuteAngleBetween(ChordDirection, RiverDirection));
			if (OffAxis > 15.f && OffAxis < 75.f)
			{
				Diagonal += Length;
				continue;
			}

			// Classify by how much the road bends from end to end: a chain that stays
			// close to its chord is straight, one that bows away is a curve.
			float MaxDeviation = 0.f;
			for (int32 Index = 1; Index + 1 < Road.Nodes.Num(); ++Index)
			{
				const FVector2D Offset = Plan.Nodes[Road.Nodes[Index]] - Start;
				MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(Cross2D(Offset, ChordDirection)));
			}
			if (MaxDeviation > ChordLength * 0.03f)
			{
				Curved += Length;
			}
			else
			{
				Straight += Length;
			}
		}
		const float Total = FMath::Max(1.f, Straight + Curved + Diagonal);
		Report.StraightPercent = Straight / Total * 100.f;
		Report.CurvedPercent = Curved / Total * 100.f;
		Report.DiagonalPercent = Diagonal / Total * 100.f;
	}

	// ---- Block statistics ------------------------------------------------
	if (Plan.Blocks.Num() > 0)
	{
		float Sum = 0.f;
		for (const FCitixPlanBlock& Block : Plan.Blocks)
		{
			Sum += Block.Area;
		}
		Report.MeanBlockArea = Sum / Plan.Blocks.Num();
	}

	Plan.Report = Report;
}
