// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/CitixStreetLightSystem.h"

#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "World/CitixTimeOfDay.h"
#include "Citix.h"

ACitixStreetLightSystem::ACitixStreetLightSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
}

void ACitixStreetLightSystem::BeginPlay()
{
	Super::BeginPlay();
	EnsurePool();
}

void ACitixStreetLightSystem::EnsurePool()
{
	if (bPoolReady)
	{
		return;
	}
	bPoolReady = true;

	if (MaxLights <= 0)
	{
		return;
	}

	USceneComponent* Root = GetRootComponent();
	if (!Root)
	{
		Root = NewObject<USceneComponent>(this, TEXT("Root"));
		SetRootComponent(Root);
		Root->RegisterComponent();
	}

	Lights.Reserve(MaxLights);
	for (int32 Index = 0; Index < MaxLights; ++Index)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(
			this, *FString::Printf(TEXT("StreetLight_%d"), Index));
		if (!Light)
		{
			continue;
		}
		Light->SetupAttachment(Root);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(LightRadius);
		Light->SetLightColor(LightColor);
		Light->SetCastShadows(false);
		Light->SetCanEverAffectNavigation(false);
		// Keep indirect lighting modest: these exist to light the street, not to
		// bounce light across the city (which would be expensive at this count).
		Light->SetIndirectLightingIntensity(0.35f);
		Light->RegisterComponent();
		Light->SetVisibility(false, true);
		Lights.Add(Light);
	}

	// Building lights: fewer, larger and softer, standing in for a whole lit facade.
	BuildingLights.Reserve(MaxBuildingLights);
	for (int32 Index = 0; Index < MaxBuildingLights; ++Index)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(
			this, *FString::Printf(TEXT("BuildingLight_%d"), Index));
		if (!Light)
		{
			continue;
		}
		Light->SetupAttachment(Root);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(BuildingLightRadius);
		Light->SetCastShadows(false);
		Light->SetCanEverAffectNavigation(false);
		Light->SetIndirectLightingIntensity(0.5f);
		Light->RegisterComponent();
		Light->SetVisibility(false, true);
		BuildingLights.Add(Light);
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] Street lights: pool of %d lamps, %d building lights; "
		"%d lamp positions, %d building emitters."),
		Lights.Num(), BuildingLights.Num(), LampLocations.Num(), BuildingEmitterLocations.Num());
}

void ACitixStreetLightSystem::SetBuildingEmitters(const TArray<FVector>& InLocations,
	const TArray<FLinearColor>& InColors)
{
	BuildingEmitterLocations = InLocations;
	BuildingEmitterColors = InColors;
	LastRefreshLocation = FVector(FLT_MAX);
	RefreshTimer = RefreshInterval;
}

int32 ACitixStreetLightSystem::GetActiveLightCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<UPointLightComponent>& Light : Lights)
	{
		Count += (Light && Light->IsVisible()) ? 1 : 0;
	}
	return Count;
}

void ACitixStreetLightSystem::SetLampLocations(const TArray<FVector>& InLampLocations)
{
	LampLocations = InLampLocations;
	LastRefreshLocation = FVector(FLT_MAX);
	RefreshTimer = RefreshInterval;
}

void ACitixStreetLightSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bPoolReady)
	{
		EnsurePool();
	}
	if (Lights.Num() == 0)
	{
		return;
	}

	// Fade the pool in with darkness.
	const ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(GetWorld());
	CurrentNightAlpha = TimeOfDay ? TimeOfDay->GetNightAlpha() : 1.f;

	if (CurrentNightAlpha <= 0.01f || LampLocations.Num() == 0)
	{
		auto Disable = [](TArray<TObjectPtr<UPointLightComponent>>& Pool)
		{
			for (const TObjectPtr<UPointLightComponent>& Light : Pool)
			{
				if (Light)
				{
					Light->SetIntensity(0.f);
					Light->SetVisibility(false, true);
				}
			}
		};
		Disable(Lights);
		Disable(BuildingLights);
		return;
	}

	// Re-assign when the timer elapses or the player has moved far enough.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();

	RefreshTimer += DeltaSeconds;
	const bool bMoved = FVector::DistSquared(PlayerLocation, LastRefreshLocation)
		> FMath::Square(RefreshMoveDistance);

	if (RefreshTimer >= RefreshInterval || bMoved)
	{
		RefreshTimer = 0.f;
		LastRefreshLocation = PlayerLocation;
		RefreshAssignments();
	}

	// Intensity follows darkness (so lamps come on through dusk).
	const float Darkness = FMath::Sqrt(CurrentNightAlpha);
	const float Intensity = LightIntensity * Darkness;
	for (const TObjectPtr<UPointLightComponent>& Light : Lights)
	{
		if (Light && Light->IsVisible())
		{
			Light->SetIntensity(Intensity);
		}
	}

	const float BuildingIntensity = BuildingLightIntensity * Darkness;
	for (const TObjectPtr<UPointLightComponent>& Light : BuildingLights)
	{
		if (Light && Light->IsVisible())
		{
			Light->SetIntensity(BuildingIntensity);
		}
	}
}

void ACitixStreetLightSystem::RefreshAssignments()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();

	// Collect nearby lamps, nearest first. A partial selection is enough.
	struct FLampCandidate
	{
		float DistanceSq;
		int32 Index;
	};
	TArray<FLampCandidate> Candidates;
	Candidates.Reserve(LampLocations.Num() / 4 + 8);

	const float MaxDistanceSq = MaxDistance * MaxDistance;
	for (int32 Index = 0; Index < LampLocations.Num(); ++Index)
	{
		const float DistanceSq = FVector::DistSquared(LampLocations[Index], PlayerLocation);
		if (DistanceSq <= MaxDistanceSq)
		{
			Candidates.Add({ DistanceSq, Index });
		}
	}

	Candidates.Sort([](const FLampCandidate& A, const FLampCandidate& B) { return A.DistanceSq < B.DistanceSq; });

	// Assign the nearest lamps to the pool.
	const int32 Count = FMath::Min(Lights.Num(), Candidates.Num());
	for (int32 Slot = 0; Slot < Lights.Num(); ++Slot)
	{
		UPointLightComponent* Light = Lights[Slot];
		if (!Light)
		{
			continue;
		}
		if (Slot < Count)
		{
			Light->SetWorldLocation(LampLocations[Candidates[Slot].Index]);
			Light->SetLightColor(LightColor);
			Light->SetVisibility(true, true);
		}
		else
		{
			Light->SetIntensity(0.f);
			Light->SetVisibility(false, true);
		}
	}

	// --- Building lights: same nearest-first approach, tinted per building --------
	if (BuildingLights.Num() == 0 || BuildingEmitterLocations.Num() == 0)
	{
		return;
	}

	TArray<FLampCandidate> BuildingCandidates;
	BuildingCandidates.Reserve(BuildingEmitterLocations.Num() / 4 + 8);
	for (int32 Index = 0; Index < BuildingEmitterLocations.Num(); ++Index)
	{
		const float DistanceSq = FVector::DistSquared(BuildingEmitterLocations[Index], PlayerLocation);
		if (DistanceSq <= MaxDistanceSq)
		{
			BuildingCandidates.Add({ DistanceSq, Index });
		}
	}
	BuildingCandidates.Sort([](const FLampCandidate& A, const FLampCandidate& B)
	{
		return A.DistanceSq < B.DistanceSq;
	});

	const int32 BuildingCount = FMath::Min(BuildingLights.Num(), BuildingCandidates.Num());
	for (int32 Slot = 0; Slot < BuildingLights.Num(); ++Slot)
	{
		UPointLightComponent* Light = BuildingLights[Slot];
		if (!Light)
		{
			continue;
		}
		if (Slot < BuildingCount)
		{
			const int32 EmitterIndex = BuildingCandidates[Slot].Index;
			Light->SetWorldLocation(BuildingEmitterLocations[EmitterIndex]);
			if (BuildingEmitterColors.IsValidIndex(EmitterIndex))
			{
				Light->SetLightColor(BuildingEmitterColors[EmitterIndex]);
			}
			Light->SetVisibility(true, true);
		}
		else
		{
			Light->SetIntensity(0.f);
			Light->SetVisibility(false, true);
		}
	}
}
