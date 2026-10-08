// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CitixRoadGraphDebug.h"

#include "Citix.h"
#include "City/CitixRoadNetwork.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/CitixCitySettings.h"
#include "Core/CitixSurfaceLibrary.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Road hierarchy colours: one bucket each, so the hierarchy reads at a glance. */
	FLinearColor RoadClassColour(ECitixRoadClass Class)
	{
		switch (Class)
		{
		case ECitixRoadClass::Alley:     return FLinearColor(0.42f, 0.42f, 0.42f);
		case ECitixRoadClass::Local:     return FLinearColor(0.90f, 0.90f, 0.90f);
		case ECitixRoadClass::Collector: return FLinearColor(0.20f, 0.75f, 1.00f);
		case ECitixRoadClass::Arterial:  return FLinearColor(0.15f, 0.90f, 0.30f);
		case ECitixRoadClass::Boulevard: return FLinearColor(1.00f, 0.80f, 0.15f);
		case ECitixRoadClass::Highway:   return FLinearColor(1.00f, 0.28f, 0.12f);
		default:                         return FLinearColor::White;
		}
	}

	const TCHAR* RoadClassName(ECitixRoadClass Class)
	{
		switch (Class)
		{
		case ECitixRoadClass::Alley:     return TEXT("Alley");
		case ECitixRoadClass::Local:     return TEXT("Local");
		case ECitixRoadClass::Collector: return TEXT("Collector");
		case ECitixRoadClass::Arterial:  return TEXT("Arterial");
		case ECitixRoadClass::Boulevard: return TEXT("Boulevard");
		case ECitixRoadClass::Highway:   return TEXT("Highway");
		default:                         return TEXT("Unknown");
		}
	}

	/**
	 * Node colour by degree. Degree 1 is a dead end: the only case where an agent has
	 * nowhere to go but back along the edge it arrived on.
	 */
	FLinearColor NodeDegreeColour(int32 Degree)
	{
		if (Degree <= 1) { return FLinearColor(1.00f, 0.05f, 0.05f); }
		if (Degree == 2) { return FLinearColor(0.80f, 0.85f, 0.95f); }
		if (Degree == 3) { return FLinearColor(1.00f, 0.62f, 0.10f); }
		// Violet, kept well away from bridge magenta so the two never read as the same.
		return FLinearColor(0.60f, 0.15f, 1.00f);
	}

	/** Majors are drawn wider so the hierarchy survives being viewed from far away. */
	float EdgeWidth(ECitixRoadClass Class)
	{
		switch (Class)
		{
		case ECitixRoadClass::Arterial:
		case ECitixRoadClass::Boulevard:
		case ECitixRoadClass::Highway:   return 220.f;
		case ECitixRoadClass::Collector: return 140.f;
		default:                         return 90.f;
		}
	}

	FName LayerName(const TCHAR* Prefix, int32 Index)
	{
		return FName(*FString::Printf(TEXT("%s_%d"), Prefix, Index));
	}
}

ACitixRoadGraphDebug::ACitixRoadGraphDebug()
{
	// Ticks only to keep the labels facing the camera, so the interval is coarse.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

void ACitixRoadGraphDebug::BeginPlay()
{
	Super::BeginPlay();
}

void ACitixRoadGraphDebug::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FaceLabelsToCamera();
}

void ACitixRoadGraphDebug::FaceLabelsToCamera()
{
	if (Labels.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	FVector CameraLocation = FVector::ZeroVector;
	if (PC->PlayerCameraManager)
	{
		CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	}
	else if (const APawn* Pawn = PC->GetPawn())
	{
		CameraLocation = Pawn->GetActorLocation();
	}

	// A text render component faces its own +X and is invisible edge-on, so without this
	// the labels are only legible from one quadrant.
	for (TObjectPtr<UTextRenderComponent>& Label : Labels)
	{
		if (Label)
		{
			Label->SetWorldRotation((CameraLocation - Label->GetComponentLocation()).Rotation());
		}
	}
}

UInstancedStaticMeshComponent* ACitixRoadGraphDebug::GetLayer(const FName& Name, const FLinearColor& Colour)
{
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = GraphLayers.Find(Name))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, Name);
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

	// The overlay is a diagram, not city fabric: the emissive master keeps it readable
	// whatever the time of day and makes it obvious it is not part of the world.
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetTintedEmissiveMaterial(Colour))
	{
		Component->SetMaterial(0, Material);
	}

	GraphLayers.Add(Name, Component);
	return Component;
}

void ACitixRoadGraphDebug::AddBox(const FName& Layer, const FVector& Centre, const FVector& Size, float YawDegrees)
{
	if (Size.X <= 0.f || Size.Y <= 0.f || Size.Z <= 0.f)
	{
		return;
	}
	if (UInstancedStaticMeshComponent* Component = GetLayer(Layer, FLinearColor::White))
	{
		const FTransform Transform(FRotator(0.f, YawDegrees, 0.f), Centre,
			Size / FCitixSurfaceLibrary::PrimitiveSize);
		Component->AddInstance(Transform, /*bWorldSpace*/ true);
	}
}

void ACitixRoadGraphDebug::AddSlab(const FName& Layer, const FVector2D& A, const FVector2D& B,
	float Z, float Width, float Height)
{
	const FVector2D Delta = B - A;
	const float Length = Delta.Size();
	if (Length < 1.f)
	{
		return;
	}
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	const FVector2D Mid = (A + B) * 0.5f;
	AddBox(Layer, FVector(Mid.X, Mid.Y, Z), FVector(Length, Width, Height), Yaw);
}

void ACitixRoadGraphDebug::AddLabel(const FString& Text, const FVector& Location,
	const FLinearColor& Colour, float WorldSize)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	if (!Label)
	{
		return;
	}
	Label->SetupAttachment(SceneRoot);
	Label->RegisterComponent();
	Label->SetMobility(EComponentMobility::Movable);
	Label->SetWorldLocation(Location);
	Label->SetText(FText::FromString(Text));
	Label->SetTextRenderColor(Colour.ToFColor(true));
	Label->SetWorldSize(WorldSize);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetCastShadow(false);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Rotation is set every tick by FaceLabelsToCamera, so the label always faces the
	// viewer instead of being edge-on and invisible from most angles.
	Labels.Add(Label);
}

void ACitixRoadGraphDebug::Clear()
{
	for (TPair<FName, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : GraphLayers)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}
	for (TObjectPtr<UTextRenderComponent>& Label : Labels)
	{
		if (Label)
		{
			Label->DestroyComponent();
		}
	}
	Labels.Reset();
	BuiltMode = 0;
}

int32 ACitixRoadGraphDebug::GetInstanceCount() const
{
	int32 Total = 0;
	for (const TPair<FName, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : GraphLayers)
	{
		if (Pair.Value)
		{
			Total += Pair.Value->GetInstanceCount();
		}
	}
	return Total;
}

void ACitixRoadGraphDebug::Build(const FCitixRoadNetwork& Network, const UCitixCitySettings& Settings, int32 Mode)
{
	Clear();

	const int32 Clamped = FMath::Clamp(Mode, 0, 4);
	if (Clamped <= 0 || Network.Edges.Num() == 0)
	{
		return;
	}
	BuiltMode = Clamped;

	const bool bLanes = Clamped >= 2;
	const bool bNodeLabels = Clamped >= 3;
	const bool bEdgeLabels = Clamped >= 4;

	// Heights: above the road (12) and pavement (16), below the traffic, so the overlay
	// reads as a diagram laid on the city rather than a tangle floating over it.
	constexpr float LaneZ = 50.f;
	constexpr float EdgeZ = 80.f;
	constexpr float EdgeHeight = 26.f;
	constexpr float LaneHeight = 10.f;

	int32 DegreeCounts[5] = { 0, 0, 0, 0, 0 };

	// ---- Edges ------------------------------------------------------------
	for (int32 EdgeIndex = 0; EdgeIndex < Network.Edges.Num(); ++EdgeIndex)
	{
		const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
		if (!Network.IsValidNode(Edge.NodeA) || !Network.IsValidNode(Edge.NodeB))
		{
			continue;
		}

		const FVector2D A = Network.Nodes[Edge.NodeA].Position;
		const FVector2D B = Network.Nodes[Edge.NodeB].Position;
		const FVector2D Delta = B - A;
		const float Length = Delta.Size();
		if (Length < 1.f)
		{
			continue;
		}
		const FVector2D Direction = Delta / Length;
		const FVector2D Right(-Direction.Y, Direction.X);

		const int32 ClassIndex = static_cast<int32>(Edge.RoadClass);
		FLinearColor Colour = RoadClassColour(Edge.RoadClass);
		FName Layer = LayerName(TEXT("Edge"), ClassIndex);
		if (Edge.bBridge)
		{
			Colour = FLinearColor(1.00f, 0.20f, 1.00f);   // bridges in magenta
			Layer = TEXT("Edge_Bridge");
		}
		if (!Edge.bDrivable)
		{
			Colour = FLinearColor(0.35f, 0.05f, 0.05f);   // a closed stub
			Layer = TEXT("Edge_Closed");
		}
		// Make sure the bucket exists with the right colour before the first AddBox.
		GetLayer(Layer, Colour);

		AddSlab(Layer, A, B, EdgeZ, EdgeWidth(Edge.RoadClass), EdgeHeight);

		// Direction chevron at the midpoint, pointing NodeA -> NodeB. Every edge is
		// two-way; the reverse flow is what the lane centrelines show.
		const FVector2D Mid = (A + B) * 0.5f;
		const float TipLength = FMath::Min(320.f, Length * 0.28f);
		const float BackLength = TipLength * 0.75f;
		const float HalfWidth = FMath::Min(220.f, Length * 0.18f);
		const FVector2D Tip = Mid + Direction * TipLength;
		AddSlab(Layer, Tip, Tip - Direction * BackLength + Right * HalfWidth, EdgeZ, 70.f, EdgeHeight);
		AddSlab(Layer, Tip, Tip - Direction * BackLength - Right * HalfWidth, EdgeZ, 70.f, EdgeHeight);

		// ---- Lane centrelines: exactly the geometry the traffic drives on --------
		if (bLanes)
		{
			const FCitixRoadSpec Spec = Settings.GetRoadSpec(Edge.RoadClass);
			const int32 HalfLanes = FMath::Max(0, Spec.NumLanes / 2);
			if (HalfLanes > 0)
			{
				const FLinearColor LaneColour(Colour.R * 0.45f, Colour.G * 0.45f, Colour.B * 0.45f);
				const FName LaneLayer = LayerName(TEXT("Lane"), ClassIndex);
				GetLayer(LaneLayer, LaneColour);

				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					// Lane offsets are always positive and measured right of travel, so
					// the two directions occupy opposite halves of the carriageway.
					const FVector2D Travel = (Sign > 0) ? Direction : -Direction;
					const FVector2D TravelRight(-Travel.Y, Travel.X);
					const FVector2D Origin = (Sign > 0) ? A : B;

					for (int32 Lane = 0; Lane < HalfLanes; ++Lane)
					{
						const float Offset = Spec.LaneWidth * (0.5f + static_cast<float>(Lane));
						const FVector2D From = Origin + TravelRight * Offset;
						AddSlab(LaneLayer, From, From + Travel * Length, LaneZ, 55.f, LaneHeight);
					}
				}
			}
		}

		if (bEdgeLabels)
		{
			const FVector2D LabelAt = Mid;
			AddLabel(FString::Printf(TEXT("E%d %s %.0fm%s"), EdgeIndex, RoadClassName(Edge.RoadClass),
				Length * 0.01f, Edge.bBridge ? TEXT(" BRIDGE") : TEXT("")),
				FVector(LabelAt.X, LabelAt.Y, 900.f), Colour, 600.f);
		}
	}

	// ---- Nodes ------------------------------------------------------------
	// Degree histogram: whether an agent can ever be left with nowhere to go but back the
	// way it came. Logged because it is the first question any weird turning raises.
	for (int32 NodeIndex = 0; NodeIndex < Network.Nodes.Num(); ++NodeIndex)
	{
		const FVector2D P = Network.Nodes[NodeIndex].Position;
		const int32 Degree = Network.GetNodeDegree(NodeIndex);
		DegreeCounts[FMath::Clamp(Degree, 0, 4)]++;

		const FLinearColor Colour = NodeDegreeColour(Degree);
		const bool bDeadEnd = Degree <= 1;
		const FName Layer = bDeadEnd ? FName(TEXT("Node_Dead"))
			: LayerName(TEXT("Node"), FMath::Clamp(Degree, 2, 4));
		GetLayer(Layer, Colour);

		// A marker whose size encodes the degree, on a shaft so it stays findable from a
		// low camera as well as from above.
		const float Size = bDeadEnd ? 520.f : (Degree == 2 ? 190.f : (Degree == 3 ? 290.f : 400.f));
		const float ShaftTop = 220.f + Size;
		AddBox(Layer, FVector(P.X, P.Y, ShaftTop * 0.5f), FVector(60.f, 60.f, ShaftTop));
		AddBox(Layer, FVector(P.X, P.Y, ShaftTop + Size * 0.5f), FVector(Size, Size, Size));

		if (bNodeLabels)
		{
			// Sized for the overview camera: legible from a few hundred metres, and
			// deliberately large up close. Modes 1-2 are the street-level views.
			AddLabel(FString::Printf(TEXT("N%d  deg %d%s"), NodeIndex, Degree,
				bDeadEnd ? TEXT("  DEAD END") : TEXT("")),
				FVector(P.X, P.Y, ShaftTop + Size + 400.f), Colour, 900.f);
		}
	}

	UE_LOG(LogCitix, Log,
		TEXT("[Citix] Road graph overlay built: mode %d, %d instances, %d labels; degree 1/2/3/4+: %d/%d/%d/%d."),
		Clamped, GetInstanceCount(), Labels.Num(),
		DegreeCounts[1], DegreeCounts[2], DegreeCounts[3], DegreeCounts[4] + DegreeCounts[0]);

	FaceLabelsToCamera();
}
