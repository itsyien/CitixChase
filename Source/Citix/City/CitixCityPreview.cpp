// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixCityPreview.h"

#include "City/CitixCityPlan.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Citix.h"

namespace
{
	/** Debug colours. These are the plan's legend. */
	const FLinearColor ColourRiver(0.05f, 0.30f, 0.85f);
	const FLinearColor ColourBridge(0.75f, 0.92f, 1.00f);
	const FLinearColor ColourArterial(0.95f, 0.12f, 0.10f);
	const FLinearColor ColourSecondary(1.00f, 0.55f, 0.08f);
	const FLinearColor ColourLocal(0.98f, 0.80f, 0.35f);
	const FLinearColor ColourPark(0.15f, 0.75f, 0.25f);
	const FLinearColor ColourPrimary(0.62f, 0.18f, 0.95f);
	const FLinearColor ColourSecondaryCluster(0.88f, 0.40f, 0.95f);
	const FLinearColor ColourHigh(1.00f, 0.90f, 0.15f);
	const FLinearColor ColourMedium(0.55f, 0.56f, 0.60f);
	const FLinearColor ColourLow(0.30f, 0.31f, 0.34f);
	const FLinearColor ColourOutline(0.85f, 0.86f, 0.90f);

	/** Road surface colour used for tinting (the whiter surfaces show tint best). */
	const FLinearColor WaterTintBase(0.02f, 0.10f, 0.30f);

	FLinearColor LayerColour(ECitixPreviewLayer Layer)
	{
		switch (Layer)
		{
		case ECitixPreviewLayer::River:           return ColourRiver;
		case ECitixPreviewLayer::Bridge:          return ColourBridge;
		case ECitixPreviewLayer::Arterial:        return ColourArterial;
		case ECitixPreviewLayer::Secondary:       return ColourSecondary;
		case ECitixPreviewLayer::Local:           return ColourLocal;
		case ECitixPreviewLayer::Park:            return ColourPark;
		case ECitixPreviewLayer::ZonePrimary:     return ColourPrimary;
		case ECitixPreviewLayer::ZoneSecondary:   return ColourSecondaryCluster;
		case ECitixPreviewLayer::ZoneHigh:        return ColourHigh;
		case ECitixPreviewLayer::ZoneMedium:      return ColourMedium;
		case ECitixPreviewLayer::ZoneLow:         return ColourLow;
		default:                                  return ColourOutline;
		}
	}

	const TCHAR* LayerName(ECitixPreviewLayer Layer)
	{
		switch (Layer)
		{
		case ECitixPreviewLayer::River:           return TEXT("River");
		case ECitixPreviewLayer::Bridge:          return TEXT("Bridge");
		case ECitixPreviewLayer::Arterial:        return TEXT("Arterial");
		case ECitixPreviewLayer::Secondary:       return TEXT("Secondary");
		case ECitixPreviewLayer::Local:           return TEXT("Local");
		case ECitixPreviewLayer::Park:            return TEXT("Park");
		case ECitixPreviewLayer::ZonePrimary:     return TEXT("PrimaryCluster");
		case ECitixPreviewLayer::ZoneSecondary:   return TEXT("SecondaryCluster");
		case ECitixPreviewLayer::ZoneHigh:        return TEXT("HighDensity");
		case ECitixPreviewLayer::ZoneMedium:      return TEXT("MediumDensity");
		case ECitixPreviewLayer::ZoneLow:         return TEXT("LowDensity");
		default:                                  return TEXT("Outline");
		}
	}

	FORCEINLINE uint64 EdgeKey(int32 A, int32 B)
	{
		return (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
	}
}

ACitixCityPreview::ACitixCityPreview()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

void ACitixCityPreview::BeginPlay()
{
	Super::BeginPlay();
}

UInstancedStaticMeshComponent* ACitixCityPreview::GetLayer(ECitixPreviewLayer Layer)
{
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = PreviewLayers.Find(Layer))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this,
		*FString::Printf(TEXT("Preview_%s"), LayerName(Layer)));
	if (!Component)
	{
		return nullptr;
	}
	Component->SetupAttachment(SceneRoot);
	Component->RegisterComponent();
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->SetGenerateOverlapEvents(false);

	if (!BoxMesh)
	{
		BoxMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeWhite);
	}
	Component->SetStaticMesh(BoxMesh);

	// The river reads better as water (glossy) than as a painted slab.
	const bool bWater = (Layer == ECitixPreviewLayer::River);
	const ECitixSurface Surface = bWater ? ECitixSurface::Water : ECitixSurface::FacadeWhite;
	const FLinearColor Colour = bWater ? WaterTintBase : LayerColour(Layer);
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetTintedMaterial(Surface, Colour))
	{
		Component->SetMaterial(0, Material);
		LayerMaterials.Add(Layer, Material);
	}

	PreviewLayers.Add(Layer, Component);
	return Component;
}

void ACitixCityPreview::AddBox(ECitixPreviewLayer Layer, const FVector& Centre, const FVector& Size, float YawDegrees)
{
	if (Size.X <= 0.f || Size.Y <= 0.f || Size.Z <= 0.f)
	{
		return;
	}
	if (UInstancedStaticMeshComponent* Component = GetLayer(Layer))
	{
		const FTransform Transform(FRotator(0.f, YawDegrees, 0.f), Centre,
			Size / FCitixSurfaceLibrary::PrimitiveSize);
		Component->AddInstance(Transform, /*bWorldSpace*/ true);
	}
}

void ACitixCityPreview::AddLabel(const FString& Text, const FVector& Location, const FLinearColor& Colour, float WorldSize)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	if (!Label)
	{
		return;
	}
	Label->SetupAttachment(SceneRoot);
	Label->RegisterComponent();
	Label->SetMobility(EComponentMobility::Movable);
	Label->SetText(FText::FromString(Text));
	Label->SetTextRenderColor(Colour.ToFColor(true));
	Label->SetWorldSize(WorldSize);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetCastShadow(false);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Stand upright facing +X so they read from the planning camera, which looks in
	// from the +X / -Y quadrant.
	Label->SetWorldRotation(FRotator(0.f, 0.f, 0.f));
	Label->SetWorldLocation(Location);
	Labels.Add(Label);
}

void ACitixCityPreview::AddPolyline(ECitixPreviewLayer Layer, const TArray<FVector2D>& Points, float Width,
	float Z, float Height, bool bClosed)
{
	if (Points.Num() < 2)
	{
		return;
	}
	const int32 Count = bClosed ? Points.Num() : Points.Num() - 1;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D A = Points[Index];
		const FVector2D B = Points[(Index + 1) % Points.Num()];
		const float Length = FVector2D::Distance(A, B);
		if (Length < 1.f)
		{
			continue;
		}
		const FVector2D Direction = (B - A) / Length;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		const FVector2D Mid = (A + B) * 0.5f;
		AddBox(Layer, FVector(Mid.X, Mid.Y, Z + Height * 0.5f),
			FVector(Length, Width, Height), Yaw);
	}
}

void ACitixCityPreview::Clear()
{
	for (const TPair<ECitixPreviewLayer, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : PreviewLayers)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}
	for (UTextRenderComponent* Label : Labels)
	{
		if (Label)
		{
			Label->DestroyComponent();
		}
	}
	Labels.Reset();
}

int32 ACitixCityPreview::GetPreviewInstanceCount() const
{
	int32 Total = 0;
	for (const TPair<ECitixPreviewLayer, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : PreviewLayers)
	{
		if (Pair.Value)
		{
			Total += Pair.Value->GetInstanceCount();
		}
	}
	return Total;
}

void ACitixCityPreview::Build(const FCitixCityPlan& Plan, const UCitixCitySettings& Settings)
{
	Clear();

	if (!BoxMesh)
	{
		BoxMesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeWhite);
	}

	// ---- Road width per level -------------------------------------------
	auto WidthForLevel = [&Settings](ECitixRoadLevel Level) -> float
	{
		switch (Level)
		{
		case ECitixRoadLevel::Arterial:  return Settings.PreviewArterialWidth;
		case ECitixRoadLevel::Secondary: return Settings.PreviewSecondaryWidth;
		default:                         return Settings.PreviewLocalWidth;
		}
	};

	// ---- Segment bookkeeping --------------------------------------------
	// Every segment is trimmed at each end by the junction pad it meets, so no two road
	// meshes ever overlap. Pads are then drawn to fill the junction cleanly.
	struct FSegment
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
		ECitixRoadLevel Level = ECitixRoadLevel::Secondary;
		bool bBridge = false;
	};
	TArray<FSegment> Segments;
	TMap<uint64, ECitixRoadLevel> EdgeLevels;
	TArray<float> NodeRadius;
	NodeRadius.Init(0.f, Plan.Nodes.Num());

	for (const FCitixPlanRoad& Road : Plan.Roads)
	{
		for (int32 Index = 0; Index + 1 < Road.Nodes.Num(); ++Index)
		{
			FSegment Segment;
			Segment.A = Road.Nodes[Index];
			Segment.B = Road.Nodes[Index + 1];
			Segment.Level = Road.Level;
			Segment.bBridge = Road.bBridge;
			Segments.Add(Segment);

			const float Half = (Road.bBridge ? Settings.PreviewBridgeWidth : WidthForLevel(Road.Level)) * 0.5f;
			if (NodeRadius.IsValidIndex(Segment.A))
			{
				NodeRadius[Segment.A] = FMath::Max(NodeRadius[Segment.A], Half);
			}
			if (NodeRadius.IsValidIndex(Segment.B))
			{
				NodeRadius[Segment.B] = FMath::Max(NodeRadius[Segment.B], Half);
			}
			EdgeLevels.Add(EdgeKey(Segment.A, Segment.B), Road.Level);
		}
	}

	// ---- Stage: river ----------------------------------------------------
	{
		TArray<FVector2D> RiverPoints;
		for (int32 Index = 0; Index < Plan.RiverPoints.Num(); ++Index)
		{
			RiverPoints.Add(Plan.RiverPoints[Index]);
		}
		// Draw the water as boxes of the local width, following the centreline.
		for (int32 Index = 0; Index + 1 < RiverPoints.Num(); ++Index)
		{
			const FVector2D A = RiverPoints[Index];
			const FVector2D B = RiverPoints[Index + 1];
			const float Length = FVector2D::Distance(A, B);
			if (Length < 1.f)
			{
				continue;
			}
			const FVector2D Direction = (B - A) / Length;
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
			const float Width = Plan.GetHalfWidth(static_cast<float>(Index) / FMath::Max(1, RiverPoints.Num() - 1)) * 2.f;
			const FVector2D Mid = (A + B) * 0.5f;
			AddBox(ECitixPreviewLayer::River, FVector(Mid.X, Mid.Y, 4.f),
				FVector(Length * 1.05f, Width, 8.f), Yaw);
		}
	}

	// ---- Stage: roads ----------------------------------------------------
	for (const FSegment& Segment : Segments)
	{
		if (!Plan.IsValidNode(Segment.A) || !Plan.IsValidNode(Segment.B))
		{
			continue;
		}
		const FVector2D A = Plan.Nodes[Segment.A];
		const FVector2D B = Plan.Nodes[Segment.B];
		const float Length = FVector2D::Distance(A, B);
		if (Length < 1.f)
		{
			continue;
		}

		const float Half = (Segment.bBridge ? Settings.PreviewBridgeWidth : WidthForLevel(Segment.Level)) * 0.5f;
		const float TrimA = FMath::Min(NodeRadius[Segment.A], Length * 0.45f);
		const float TrimB = FMath::Min(NodeRadius[Segment.B], Length * 0.45f);

		const FVector2D Direction = (B - A) / Length;
		const FVector2D Start = A + Direction * TrimA;
		const FVector2D End = B - Direction * TrimB;
		const float CoreLength = FVector2D::Distance(Start, End);
		if (CoreLength < 10.f)
		{
			continue;
		}
		const FVector2D Mid = (Start + End) * 0.5f;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

		ECitixPreviewLayer Layer = ECitixPreviewLayer::Secondary;
		if (Segment.bBridge)
		{
			Layer = ECitixPreviewLayer::Bridge;
		}
		else
		{
			switch (Segment.Level)
			{
			case ECitixRoadLevel::Arterial:  Layer = ECitixPreviewLayer::Arterial;  break;
			case ECitixRoadLevel::Secondary: Layer = ECitixPreviewLayer::Secondary; break;
			default:                         Layer = ECitixPreviewLayer::Local;     break;
			}
		}
		AddBox(Layer, FVector(Mid.X, Mid.Y, 12.f), FVector(CoreLength, Half * 2.f, 12.f), Yaw);
	}

	// ---- Stage: junction pads -------------------------------------------
	// One pad per lattice node so bends and crossings are filled with no gaps and no
	// overlap, and each junction reads as an intentional intersection.
	{
		TArray<ECitixRoadLevel> NodeLevel;
		NodeLevel.Init(ECitixRoadLevel::Secondary, Plan.Nodes.Num());
		TArray<bool> NodeBridge;
		NodeBridge.Init(false, Plan.Nodes.Num());
		TArray<float> NodeYaw;
		NodeYaw.Init(0.f, Plan.Nodes.Num());
		TArray<bool> NodeHasYaw;
		NodeHasYaw.Init(false, Plan.Nodes.Num());

		for (const FSegment& Segment : Segments)
		{
			if (!Plan.IsValidNode(Segment.A) || !Plan.IsValidNode(Segment.B))
			{
				continue;
			}
			const FVector2D Direction = (Plan.Nodes[Segment.B] - Plan.Nodes[Segment.A]).GetSafeNormal();
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
			for (int32 Node : { Segment.A, Segment.B })
			{
				if (!NodeHasYaw[Node])
				{
					NodeYaw[Node] = Yaw;
					NodeHasYaw[Node] = true;
				}
				if (Segment.bBridge || static_cast<int32>(Segment.Level) < static_cast<int32>(NodeLevel[Node]))
				{
					NodeLevel[Node] = Segment.Level;
				}
				NodeBridge[Node] |= Segment.bBridge;
			}
		}

		for (int32 Node = 0; Node < Plan.Nodes.Num(); ++Node)
		{
			const float Radius = NodeRadius[Node];
			if (Radius <= 0.f)
			{
				continue;
			}
			ECitixPreviewLayer Layer = ECitixPreviewLayer::Secondary;
			if (NodeBridge[Node])
			{
				Layer = ECitixPreviewLayer::Bridge;
			}
			else
			{
				switch (NodeLevel[Node])
				{
				case ECitixRoadLevel::Arterial:  Layer = ECitixPreviewLayer::Arterial;  break;
				case ECitixRoadLevel::Secondary: Layer = ECitixPreviewLayer::Secondary; break;
				default:                         Layer = ECitixPreviewLayer::Local;     break;
				}
			}
			const FVector2D Position = Plan.Nodes[Node];
			AddBox(Layer, FVector(Position.X, Position.Y, 12.f),
				FVector(Radius * 2.f, Radius * 2.f, 12.f), NodeYaw[Node]);
		}
	}

	// ---- Stage: open space and skyline zones ----------------------------
	for (const FCitixPlanBlock& Block : Plan.Blocks)
	{
		if (Block.Corners.Num() < 4)
		{
			continue;
		}

		// Inset each block by the half width of the roads bounding it, so no tile extends
		// under a road.
		float Inset = 0.f;
		{
			// Corner order is (Column,Row), (Column+1,Row), (Column+1,Row+1), (Column,Row+1).
			const int32 SlotA = Plan.SlotForBank(Block.Side, Block.Row);
			const int32 SlotB = Plan.SlotForBank(Block.Side, Block.Row + 1);
			const int32 Corners[4][2] =
			{
				{ Plan.NodeIndex(Block.Column, SlotA), Plan.NodeIndex(Block.Column + 1, SlotA) },
				{ Plan.NodeIndex(Block.Column + 1, SlotA), Plan.NodeIndex(Block.Column + 1, SlotB) },
				{ Plan.NodeIndex(Block.Column + 1, SlotB), Plan.NodeIndex(Block.Column, SlotB) },
				{ Plan.NodeIndex(Block.Column, SlotB), Plan.NodeIndex(Block.Column, SlotA) }
			};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (const ECitixRoadLevel* Level = EdgeLevels.Find(EdgeKey(Corners[Index][0], Corners[Index][1])))
				{
					Inset = FMath::Max(Inset, WidthForLevel(*Level) * 0.5f);
				}
			}
		}

		// Local frame of the block, so the tile follows the block's own orientation.
		const FVector2D EdgeX = Block.Corners[1] - Block.Corners[0];
		const FVector2D EdgeY = Block.Corners[3] - Block.Corners[0];
		const float SizeX = EdgeX.Size();
		const float SizeY = EdgeY.Size();
		const float InnerX = SizeX - Inset * 2.f;
		const float InnerY = SizeY - Inset * 2.f;
		if (InnerX < 200.f || InnerY < 200.f)
		{
			continue;
		}
		const FVector2D Direction = EdgeX.GetSafeNormal();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

		if (Block.bOpenSpace)
		{
			AddBox(ECitixPreviewLayer::Park, FVector(Block.Centre.X, Block.Centre.Y, 4.f),
				FVector(InnerX, InnerY, 8.f), Yaw);
			continue;
		}

		switch (Block.Zone)
		{
		case ECitixPlanZone::PrimaryCluster:
			AddBox(ECitixPreviewLayer::ZonePrimary, FVector(Block.Centre.X, Block.Centre.Y, Settings.PreviewHeightPrimary * 0.5f),
				FVector(InnerX, InnerY, Settings.PreviewHeightPrimary), Yaw);
			break;
		case ECitixPlanZone::SecondaryCluster:
			AddBox(ECitixPreviewLayer::ZoneSecondary, FVector(Block.Centre.X, Block.Centre.Y, Settings.PreviewHeightSecondary * 0.5f),
				FVector(InnerX, InnerY, Settings.PreviewHeightSecondary), Yaw);
			break;
		case ECitixPlanZone::HighDensity:
			AddBox(ECitixPreviewLayer::ZoneHigh, FVector(Block.Centre.X, Block.Centre.Y, Settings.PreviewHeightHigh * 0.5f),
				FVector(InnerX, InnerY, Settings.PreviewHeightHigh), Yaw);
			break;
		case ECitixPlanZone::MediumDensity:
			AddBox(ECitixPreviewLayer::ZoneMedium, FVector(Block.Centre.X, Block.Centre.Y, Settings.PreviewHeightMedium * 0.5f),
				FVector(InnerX, InnerY, Settings.PreviewHeightMedium), Yaw);
			break;
		default:
			AddBox(ECitixPreviewLayer::ZoneLow, FVector(Block.Centre.X, Block.Centre.Y, Settings.PreviewHeightLow * 0.5f),
				FVector(InnerX, InnerY, Settings.PreviewHeightLow), Yaw);
			break;
		}
	}

	// ---- Stage: district boundaries + labels ----------------------------
	for (const FCitixPlanDistrict& District : Plan.Districts)
	{
		if (District.Boundary.Num() >= 2)
		{
			AddPolyline(ECitixPreviewLayer::DistrictOutline, District.Boundary, 120.f, 20.f, 40.f, /*bClosed*/ true);
		}

		// Colour the label by the district's future density so it doubles as a legend.
		FLinearColor LabelColour = ColourMedium;
		switch (District.Zone)
		{
		case ECitixPlanZone::PrimaryCluster:   LabelColour = ColourPrimary; break;
		case ECitixPlanZone::SecondaryCluster: LabelColour = ColourSecondaryCluster; break;
		case ECitixPlanZone::HighDensity:      LabelColour = ColourHigh; break;
		case ECitixPlanZone::LowDensity:       LabelColour = ColourLow; break;
		default: break;
		}
		AddLabel(District.Name.ToUpper(), FVector(District.Centroid.X, District.Centroid.Y, 16000.f),
			LabelColour, 2400.f);
	}

	// Skyline cluster labels.
	for (int32 Index = 0; Index < Plan.Districts.Num(); ++Index)
	{
		const FCitixPlanDistrict& District = Plan.Districts[Index];
		if (District.Zone == ECitixPlanZone::PrimaryCluster)
		{
			AddLabel(TEXT("CBD SKYLINE"), FVector(District.Centroid.X, District.Centroid.Y, 25000.f),
				ColourPrimary, 3000.f);
		}
		else if (District.Zone == ECitixPlanZone::SecondaryCluster)
		{
			AddLabel(TEXT("SECONDARY CLUSTER"), FVector(District.Centroid.X, District.Centroid.Y, 16000.f),
				ColourSecondaryCluster, 2600.f);
		}
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] Preview: %d instances, %d labels."),
		GetPreviewInstanceCount(), Labels.Num());
}
