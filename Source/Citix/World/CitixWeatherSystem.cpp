// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/CitixWeatherSystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "World/CitixTimeOfDay.h"
#include "Citix.h"

ACitixWeatherSystem::ACitixWeatherSystem()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RainStreaks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RainStreaks"));
	RainStreaks->SetupAttachment(Root);
	RainStreaks->SetMobility(EComponentMobility::Movable);
	RainStreaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainStreaks->SetCanEverAffectNavigation(false);
	RainStreaks->SetCastShadow(false);
	RainStreaks->bUseAsOccluder = false;
	RainStreaks->SetGenerateOverlapEvents(false);
	RainStreaks->SetVisibility(false, true);

	// Instances are updated every frame, so keep the component from rebuilding
	// anything expensive per update.
	RainStreaks->SetCanEverAffectNavigation(false);
}

void ACitixWeatherSystem::BeginPlay()
{
	Super::BeginPlay();

	Rng.Initialize(20261001);
	StateTimer = Rng.FRandRange(MinClearSeconds, MaxClearSeconds);

	for (TActorIterator<ACitixTimeOfDay> It(GetWorld()); It; ++It)
	{
		TimeOfDay = *It;
		break;
	}

	if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::Lamp))
	{
		RainStreaks->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool))
	{
		RainStreaks->SetMaterial(0, Material);
	}

	// Pre-create every instance so per-frame updates are pure transform writes.
	StreakPositions.SetNum(MaxStreaks);
	StreakTransforms.SetNum(MaxStreaks);
	for (int32 Index = 0; Index < MaxStreaks; ++Index)
	{
		StreakPositions[Index] = FVector(0.f, 0.f, -100000.f);
		RainStreaks->AddInstance(FTransform(FQuat::Identity,
			FVector(0.f, 0.f, -100000.f), FVector(0.01f)), /*bWorldSpace*/ true);
	}

	// Test hook: start a downpour immediately so it can be screenshotted headlessly.
	// Authority only: clients render the server's weather.
	UWorld* HookWorld = GetWorld();
	if (HookWorld && HookWorld->GetNetMode() != NM_Client
		&& FParse::Param(FCommandLine::Get(), TEXT("CitixRain")))
	{
		ForceRain(0.85f);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: forced rain by command line."));
	}
}

ACitixWeatherSystem* ACitixWeatherSystem::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACitixWeatherSystem> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ACitixWeatherSystem::SetRainEnabled(bool bEnabled)
{
	bRainEnabled = bEnabled;
	if (!bEnabled)
	{
		TargetIntensity = 0.f;
		State = ECitixWeatherState::Stopping;
		StateTimer = RampSeconds;
	}
	else if (State == ECitixWeatherState::Clear)
	{
		StateTimer = Rng.FRandRange(MinClearSeconds, MaxClearSeconds);
	}
}

void ACitixWeatherSystem::ForceRain(float Intensity)
{
	const float Clamped = FMath::Clamp(Intensity, 0.f, 1.f);
	if (Clamped <= 0.f)
	{
		bRainEnabled = false;
		TargetIntensity = 0.f;
		State = ECitixWeatherState::Stopping;
		StateTimer = RampSeconds;
		return;
	}
	bRainEnabled = true;
	CurrentIntensity = Clamped;
	TargetIntensity = Clamped;
	State = ECitixWeatherState::Raining;
	StateTimer = Rng.FRandRange(MinRainSeconds, MaxRainSeconds);
	ApplyIntensityToWorld();
}

void ACitixWeatherSystem::ApplyIntensityToWorld()
{
	if (!TimeOfDay)
	{
		for (TActorIterator<ACitixTimeOfDay> It(GetWorld()); It; ++It)
		{
			TimeOfDay = *It;
			break;
		}
	}
	if (TimeOfDay)
	{
		TimeOfDay->SetRainIntensity(CurrentIntensity);
	}
}

void ACitixWeatherSystem::PlaceStreak(int32 Index, const FVector& Centre, bool bAtTop)
{
	const float X = Centre.X + Rng.FRandRange(-RainAreaCm, RainAreaCm);
	const float Y = Centre.Y + Rng.FRandRange(-RainAreaCm, RainAreaCm);
	const float Z = bAtTop
		? Centre.Z + RainHeightCm * 0.5f
		: Centre.Z + Rng.FRandRange(-RainHeightCm * 0.25f, RainHeightCm * 0.5f);
	StreakPositions[Index] = FVector(X, Y, Z);
}

void ACitixWeatherSystem::UpdateStreaks(float DeltaSeconds)
{
	APawn* Pawn = nullptr;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Pawn = PC->GetPawn();
	}
	const FVector Centre = Pawn ? Pawn->GetActorLocation() : GetActorLocation();

	// How many streaks this intensity needs: partial counts wrap around the pool.
	const int32 Count = FMath::Clamp(FMath::CeilToInt(MaxStreaks * CurrentIntensity), 0, MaxStreaks);
	const float FloorZ = Centre.Z - RainHeightCm * 0.25f;
	const float CeilingZ = Centre.Z + RainHeightCm * 0.5f;

	for (int32 Index = 0; Index < MaxStreaks; ++Index)
	{
		if (Index >= Count)
		{
			StreakTransforms[Index] = FTransform(FQuat::Identity,
				FVector(0.f, 0.f, -100000.f), FVector(0.01f));
			continue;
		}

		FVector& Position = StreakPositions[Index];
		if (Position.Z < -50000.f)
		{
			// First use of this slot (or it was parked out of sight): drop it in.
			Position = FVector(
				Centre.X + Rng.FRandRange(-RainAreaCm, RainAreaCm),
				Centre.Y + Rng.FRandRange(-RainAreaCm, RainAreaCm),
				Centre.Z + Rng.FRandRange(-RainHeightCm * 0.25f, RainHeightCm * 0.5f));
		}

		Position += (Wind + FVector(0.f, 0.f, -FallSpeed)) * DeltaSeconds;

		// Recycle when a streak hits the floor...
		if (Position.Z < FloorZ)
		{
			Position.Z = CeilingZ;
		}
		// ...and wrap horizontally so the volume follows the player indefinitely.
		if (Position.X - Centre.X > RainAreaCm) { Position.X -= RainAreaCm * 2.f; }
		else if (Centre.X - Position.X > RainAreaCm) { Position.X += RainAreaCm * 2.f; }
		if (Position.Y - Centre.Y > RainAreaCm) { Position.Y -= RainAreaCm * 2.f; }
		else if (Centre.Y - Position.Y > RainAreaCm) { Position.Y += RainAreaCm * 2.f; }

		// Streaks are stretched along their fall direction so they read as motion.
		const FQuat Rotation = FRotationMatrix::MakeFromZ(
			-(Wind + FVector(0.f, 0.f, -FallSpeed)).GetSafeNormal()).ToQuat();
		StreakTransforms[Index] = FTransform(Rotation, Position,
			FVector(0.02f, 0.02f, StreakLengthCm / 100.f));
	}

	RainStreaks->BatchUpdateInstancesTransforms(0, StreakTransforms,
		/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false);
}

void ACitixWeatherSystem::UpdateState(float DeltaSeconds)
{
	switch (State)
	{
	case ECitixWeatherState::Clear:
		CurrentIntensity = FMath::FInterpConstantTo(CurrentIntensity, 0.f, DeltaSeconds, 1.f / RampSeconds);
		if (CurrentIntensity <= 0.001f)
		{
			CurrentIntensity = 0.f;
			StateTimer -= DeltaSeconds;
			if (StateTimer <= 0.f)
			{
				// Night showers are more likely, and heavier, than daytime drizzle.
				const float NightAlpha = TimeOfDay ? TimeOfDay->GetNightAlpha() : 0.f;
				const float Chance = FMath::Lerp(0.25f, 0.6f, NightAlpha);
				if (bRainEnabled && Rng.FRand() < Chance)
				{
					TargetIntensity = Rng.FRandRange(MinIntensity, MaxIntensity);
					State = ECitixWeatherState::Starting;
					StateTimer = RampSeconds;
					UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: rain starting (target %.2f)."), TargetIntensity);
				}
				else
				{
					StateTimer = Rng.FRandRange(MinClearSeconds, MaxClearSeconds);
				}
			}
		}
		break;

	case ECitixWeatherState::Starting:
		CurrentIntensity = FMath::FInterpConstantTo(CurrentIntensity, TargetIntensity,
			DeltaSeconds, FMath::Max(0.01f, TargetIntensity / RampSeconds));
		if (CurrentIntensity >= TargetIntensity - 0.01f)
		{
			CurrentIntensity = TargetIntensity;
			State = ECitixWeatherState::Raining;
			StateTimer = Rng.FRandRange(MinRainSeconds, MaxRainSeconds);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: raining at %.2f."), CurrentIntensity);
		}
		break;

	case ECitixWeatherState::Raining:
		CurrentIntensity = TargetIntensity;
		StateTimer -= DeltaSeconds;
		if (StateTimer <= 0.f)
		{
			State = ECitixWeatherState::Stopping;
			StateTimer = RampSeconds;
			UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: rain easing off."));
		}
		break;

	case ECitixWeatherState::Stopping:
		CurrentIntensity = FMath::FInterpConstantTo(CurrentIntensity, 0.f, DeltaSeconds, 1.f / RampSeconds);
		if (CurrentIntensity <= 0.001f)
		{
			CurrentIntensity = 0.f;
			State = ECitixWeatherState::Clear;
			StateTimer = Rng.FRandRange(MinClearSeconds, MaxClearSeconds);
		}
		break;
	}

	ApplyIntensityToWorld();
}

void ACitixWeatherSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The cycle runs on the server; clients render the replicated intensity
	// around their own pawn. NOTE: net-mode, not HasAuthority: a client-spawned
	// system reports local authority.
	UWorld* TickWorld = GetWorld();
	if (TickWorld && TickWorld->GetNetMode() != NM_Client)
	{
		UpdateState(DeltaSeconds);
	}
	else if (ACitixTimeOfDay* TodClock = ACitixTimeOfDay::Find(GetWorld()))
	{
		CurrentIntensity = TodClock->GetRainIntensity();
	}

	if (CurrentIntensity > 0.001f)
	{
		if (!RainStreaks->IsVisible())
		{
			RainStreaks->SetVisibility(true, true);
		}
		UpdateStreaks(DeltaSeconds);
	}
	else if (RainStreaks->IsVisible())
	{
		RainStreaks->SetVisibility(false, true);
	}
}

// ---------------------------------------------------------------------------
// Console commands
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GCitixRainCmd(
	TEXT("Citix.Rain"),
	TEXT("Weather control. Citix.Rain 1 = force rain, Citix.Rain 0 = force clear, "
		"Citix.Rain 0.5 = force medium rain, Citix.Rain auto = resume the weather cycle."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ACitixWeatherSystem* Weather = ACitixWeatherSystem::Find(World);
		if (!Weather)
		{
			UE_LOG(LogCitix, Warning, TEXT("Citix.Rain: no ACitixWeatherSystem in the world."));
			return;
		}
		if (Args.Num() == 0)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: intensity %.2f, state %d, auto=%s"),
				Weather->GetRainIntensity(), static_cast<int32>(Weather->IsRaining()),
				Weather->IsRainEnabled() ? TEXT("yes") : TEXT("no"));
			return;
		}
		if (Args[0].Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			Weather->SetRainEnabled(true);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: automatic cycle resumed."));
			return;
		}
		Weather->ForceRain(FCString::Atof(*Args[0]));
		UE_LOG(LogCitix, Log, TEXT("[Citix] Weather: forced to %.2f."), Weather->GetRainIntensity());
	}));
