// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sandbox/CitixSandboxDirector.h"

#include "Algo/Reverse.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Core/CitixVisibility.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "Sandbox/CitixPoliceVehicle.h"
#include "Sandbox/CitixPoliceOfficer.h"
#include "Sandbox/CitixPulseBolt.h"
#include "Sandbox/CitixHitSpark.h"
#include "Sandbox/CitixRouteGuide.h"
#include "Sandbox/CitixShopkeeper.h"
#include "Sandbox/CitixWeaponProp.h"
#include "Traffic/CitixTrafficSystem.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "World/CitixTimeOfDay.h"
#include "World/CitixWeatherSystem.h"
#include "Camera/CameraComponent.h"
#include "Character/CitixOnFootPawn.h"
#include "City/CitixCityGenerator.h"
#include "Player/CitixPlayerState.h"
#include "Player/CitixGameState.h"
#include "Sandbox/CitixDestinationBeacon.h"
#include "Citix.h"

namespace
{
	const TCHAR* DistrictName(ECitixDistrict District)
	{
		switch (District)
		{
		case ECitixDistrict::Financial:   return TEXT("Financial District");
		case ECitixDistrict::Downtown:    return TEXT("Downtown");
		case ECitixDistrict::Commercial:  return TEXT("Commercial Quarter");
		case ECitixDistrict::OldTown:     return TEXT("Old Town");
		case ECitixDistrict::Residential: return TEXT("Residential Quarter");
		case ECitixDistrict::Industrial:  return TEXT("Industrial Port");
		case ECitixDistrict::Parkland:    return TEXT("Parkland");
		case ECitixDistrict::Waterfront:  return TEXT("Waterfront");
		case ECitixDistrict::University:  return TEXT("University");
		case ECitixDistrict::Market:      return TEXT("Market Quarter");
		default:                          return TEXT("Outer City");
		}
	}

	float SegmentDistance2D(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector2D P2(P.X, P.Y);
		const FVector2D A2(A.X, A.Y);
		const FVector2D B2(B.X, B.Y);
		const FVector2D AB = B2 - A2;
		const float LengthSq = static_cast<float>(AB.SizeSquared());
		if (LengthSq < 1.f)
		{
			return static_cast<float>(FVector2D::Distance(P2, A2));
		}
		const float T = FMath::Clamp(FVector2D::DotProduct(P2 - A2, AB) / LengthSq, 0.f, 1.f);
		return static_cast<float>(FVector2D::Distance(P2, A2 + AB * T));
	}
}

FString ACitixSandboxDirector::DistrictFlavor(ECitixDistrict District)
{
	switch (District)
	{
	case ECitixDistrict::Financial:   return TEXT("Towers of glass and capital — best skyline at night.");
	case ECitixDistrict::Downtown:    return TEXT("Dense blocks and bright commercial streets.");
	case ECitixDistrict::Commercial:  return TEXT("Markets and offices stacked tight.");
	case ECitixDistrict::OldTown:     return TEXT("Narrow lanes and old shikumen walls — walk it.");
	case ECitixDistrict::Residential: return TEXT("Quiet compounds, corner parks, local errands.");
	case ECitixDistrict::Industrial:  return TEXT("Warehouses and yards — long hauls out here.");
	case ECitixDistrict::Parkland:    return TEXT("Green lungs of the city.");
	case ECitixDistrict::Waterfront:  return TEXT("Promenades along the river.");
	case ECitixDistrict::University:  return TEXT("Campus courtyards and bike lanes.");
	case ECitixDistrict::Market:      return TEXT("Stalls, signs and crowds.");
	default:                          return TEXT("Ring roads and open lots.");
	}
}

FLinearColor ACitixSandboxDirector::GetTourStopColor(int32 StopIndex)
{
	switch (StopIndex % 3)
	{
	case 1:  return FLinearColor(1.00f, 0.35f, 0.60f, 1.f); // pink
	case 2:  return FLinearColor(1.00f, 0.85f, 0.30f, 1.f); // yellow
	default: return FLinearColor(0.35f, 0.78f, 1.00f, 1.f); // cyan
	}
}

FLinearColor ACitixSandboxDirector::GetObjectiveColor() const
{
	if (ActivityState == ECitixActivityState::Active)
	{
		if (ActivityType == ECitixActivityType::Tour)
		{
			return GetTourStopColor(ActivityStopsDone);
		}
		return FLinearColor(0.35f, 0.78f, 1.00f, 1.f); // drive route: cyan
	}
	if (JobState == ECitixJobState::Active)
	{
		return FLinearColor(1.00f, 0.55f, 0.20f, 1.f); // delivery: orange
	}
	return FLinearColor(0.35f, 0.95f, 0.55f, 1.f); // manual waypoint: green
}

/** Beacon/guide surface matching the current objective. */
ECitixSurface ACitixSandboxDirector::ObjectiveBeaconSurface(bool bJobActive, bool bTourActive, int32 TourStop)
{
	if (bJobActive)
	{
		return ECitixSurface::EmissiveWarm;
	}
	if (bTourActive)
	{
		const int32 Stop = TourStop % 3;
		return (Stop == 1) ? ECitixSurface::OrbPink
			: (Stop == 2 ? ECitixSurface::EmissiveWarm : ECitixSurface::EmissiveCool);
	}
	return ECitixSurface::EmissiveCool;
}

void ACitixSandboxDirector::RefreshBeaconMaterial()
{
	if (!JobBeacon)
	{
		return;
	}
	// The 3D pillar matches the map/HUD color of whatever it marks.
	const ECitixSurface Surface = ObjectiveBeaconSurface(
		JobState == ECitixJobState::Active,
		ActivityState == ECitixActivityState::Active
			&& ActivityType == ECitixActivityType::Tour,
		ActivityStopsDone);
	if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(Surface))
	{
		JobBeacon->SetMaterial(0, Material);
	}
}

ACitixSandboxDirector::ACitixSandboxDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	JobBeacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JobBeacon"));
	JobBeacon->SetupAttachment(Root);
	JobBeacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JobBeacon->SetCanEverAffectNavigation(false);
	JobBeacon->SetCastShadow(false);
	JobBeacon->SetVisibility(false, true);
	JobBeacon->SetRelativeScale3D(FVector(3.f, 3.f, 60.f));
}

void ACitixSandboxDirector::BeginPlay()
{
	Super::BeginPlay();

	if (JobBeacon)
	{
		if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
		{
			JobBeacon->SetStaticMesh(Mesh);
		}
		if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveCool))
		{
			JobBeacon->SetMaterial(0, Material);
		}
	}

	Rng.Initialize(20260926);

	// Tracer pool: tiny, spawned once, actors idle with tick disabled.
	UWorld* World = GetWorld();
	if (World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			if (ACitixPulseBolt* Bolt = World->SpawnActor<ACitixPulseBolt>(
				ACitixPulseBolt::StaticClass(), FTransform::Identity, Params))
			{
				TracerBolts.Add(Bolt);
			}
		}
		WeaponProp = World->SpawnActor<ACitixWeaponProp>(
			ACitixWeaponProp::StaticClass(), FTransform::Identity, Params);
		if (WeaponProp)
		{
			WeaponProp->FollowPawn(nullptr);
		}
		for (int32 Index = 0; Index < 24; ++Index)
		{
			if (ACitixHitSpark* Spark = World->SpawnActor<ACitixHitSpark>(
				ACitixHitSpark::StaticClass(), FTransform::Identity, Params))
			{
				SparkPool.Add(Spark);
			}
		}
		RouteGuide = World->SpawnActor<ACitixRouteGuide>(
			ACitixRouteGuide::StaticClass(), FTransform::Identity, Params);
	}
}

void ACitixSandboxDirector::FireSparks(const FVector& Location, ECitixSurface Surface, int32 Count)
{
	int32 Fired = 0;
	for (ACitixHitSpark* Spark : SparkPool)
	{
		if (Fired >= Count)
		{
			break;
		}
		if (Spark && Spark->IsIdle())
		{
			const FVector Velocity(
				Rng.FRandRange(-500.f, 500.f),
				Rng.FRandRange(-500.f, 500.f),
				Rng.FRandRange(200.f, 800.f));
			Spark->Fire(Location, Velocity, Surface);
			++Fired;
		}
	}
}

void ACitixSandboxDirector::Configure(const FCitixRoadNetwork& InNetwork, const FCitixCityPlan& InPlan,
	const TArray<FVector>& LandmarkLocations, float CitySize){
	Network = InNetwork;
	CityPlan = InPlan;
	Rng.Initialize(20260926 + static_cast<int32>(CitySize));
	BuildPointsOfInterest(LandmarkLocations, CitySize);
	BuildShops();
	bConfigured = Network.Nodes.Num() > 0;

	UE_LOG(LogCitix, Log, TEXT("[Citix] Sandbox ready: %d points of interest, %d weapon shops."),
		POIs.Num(), Shops.Num());
	PublishSharedState();
	PublishCitySeed();
}

FCitixPlayerGame* ACitixSandboxDirector::FindPlayerGame(AController* PC)
{
	if (!PC)
	{
		return nullptr;
	}
	for (FCitixPlayerGame& Game : PlayerGames)
	{
		if (Game.PC.Get() == PC)
		{
			return &Game;
		}
	}
	// New driver: start clean. Arrays are fixed-size; keep them valid.
	FCitixPlayerGame Game;
	Game.PC = PC;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Game.MagAmmo[Index] = 0;
		Game.ReserveAmmo[Index] = 0;
		Game.bOwnedWeapons[Index] = false;
	}
	PlayerGames.Add(Game);
	return &PlayerGames.Last();
}

FCitixPlayerGame* ACitixSandboxDirector::FindPlayerGame(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	AController* PC = Pawn->GetController();
	if (!PC)
	{
		// Unpossessed (parked car, corpse walker): no game to update.
		return nullptr;
	}
	return FindPlayerGame(PC);
}

void ACitixSandboxDirector::RemovePlayerGame(AController* PC)
{
	if (!PC)
	{
		return;
	}
	for (int32 Index = PlayerGames.Num() - 1; Index >= 0; --Index)
	{
		AController* Member = PlayerGames[Index].PC.Get();
		if (Member == nullptr || Member == PC)
		{
			PlayerGames.RemoveAt(Index);
		}
	}
}

void ACitixSandboxDirector::CopyGameToGlobals(const FCitixPlayerGame& Game)
{
	// Only the focus record drives legacy local readers: a guest acting must
	// never flicker the listen host's HUD.
	if (Game.PC.Get() != FocusPC())
	{
		return;
	}
	bHasWaypoint = Game.bHasWaypoint;
	WaypointLocation = Game.WaypointLocation;
	WaypointName = Game.WaypointName;
	WaypointCursor = Game.WaypointCursor;
	RoutePoints = Game.RoutePoints;
	JobState = Game.JobState;
	JobDestination = Game.JobDestination;
	JobTimeRemaining = Game.JobTimeRemaining;
	JobDistanceRemaining = Game.JobDistanceRemaining;
	JobRewardMult = Game.JobRewardMult;
	Score = Game.Score;
	JobsCompleted = Game.JobsCompleted;
	Money = Game.Money;
	LastHour = Game.LastHour;
	bQuestOffered = Game.bQuestOffered;
	bQuestAccepted = Game.bQuestAccepted;
	ActivityType = Game.ActivityType;
	ActivityState = Game.ActivityState;
	ActivityName = Game.ActivityName;
	ActivityStops = Game.ActivityStops;
	ActivityStopNames = Game.ActivityStopNames;
	ActivityStopsDone = Game.ActivityStopsDone;
	ActivityTimeRemaining = Game.ActivityTimeRemaining;
	ActivityDistanceRemaining = Game.ActivityDistanceRemaining;
	bActivityRushRoute = Game.bActivityRushRoute;
	ActivitiesCompleted = Game.ActivitiesCompleted;
	bOffRoute = Game.bOffRoute;
	OffRouteTimer = Game.OffRouteTimer;
	OffRouteToastCooldown = Game.OffRouteToastCooldown;
	Heat = Game.Heat;
	WantedStars = Game.WantedStars;
	TimeSinceIncident = Game.TimeSinceIncident;
	CrashCooldown = Game.CrashCooldown;
	LastHullFraction = Game.LastHullFraction;
	EscapeProgress = Game.EscapeProgress;
	PhotosTaken = Game.PhotosTaken;
	LastPhotoScore = Game.LastPhotoScore;
	LastPhotoBreakdown = Game.LastPhotoBreakdown;
	PhotoCooldown = Game.PhotoCooldown;
	bVariantBonusesUnlocked = Game.bVariantBonusesUnlocked;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		bOwnedWeapons[Index] = Game.bOwnedWeapons[Index];
		MagAmmo[Index] = Game.MagAmmo[Index];
		ReserveAmmo[Index] = Game.ReserveAmmo[Index];
	}
	CurrentWeapon = Game.CurrentWeapon;
	FireCooldownTimer = Game.FireCooldownTimer;
	ReloadTimer = Game.ReloadTimer;
	NoWeaponToastCooldown = Game.NoWeaponToastCooldown;
	bTriggerHeld = Game.bTriggerHeld;
	AdsAmount = Game.AdsAmount;
	DiscoveryCardTitle = Game.CardTitle;
	DiscoveryCardSub = Game.CardSub;
	DiscoveryCardRemaining = Game.CardTitle.IsEmpty() ? 0.f : DiscoveryCardDuration;
	CurrentDistrict = Game.CurrentDistrict;
	CurrentDistrictIndex = Game.CurrentDistrictIndex;
	DistrictCardCooldown = Game.DistrictCardCooldown;
	DistrictCardRemaining = 0.f;
	ToastText = Game.ToastText;
	ToastRemaining = Game.ToastText.IsEmpty() ? 0.f : 3.f;
}

const FCitixPlayerGame* ACitixSandboxDirector::FindPlayerGameConst(AController* PC) const
{
	if (!PC)
	{
		return nullptr;
	}
	for (const FCitixPlayerGame& Game : PlayerGames)
	{
		if (Game.PC.Get() == PC)
		{
			return &Game;
		}
	}
	return nullptr;
}

ACitixPlayerState* ACitixSandboxDirector::PlayerStateFor(FCitixPlayerGame& Game) const
{
	AController* PC = Game.PC.Get();
	if (PC && PC->PlayerState)
	{
		return Cast<ACitixPlayerState>(PC->PlayerState);
	}
	return nullptr;
}

AController* ACitixSandboxDirector::FocusPC() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			return PC;
		}
	}
	return nullptr;
}

void ACitixSandboxDirector::ShowToast(FCitixPlayerGame& Game, const FString& Text)
{
	Game.ToastText = Text;
	++Game.ToastSerial;
}

void ACitixSandboxDirector::ShowCard(FCitixPlayerGame& Game, const FString& Title, const FString& Sub)
{
	Game.CardTitle = Title;
	Game.CardSub = Sub;
	++Game.CardSerial;
}

void ACitixSandboxDirector::AddScore(FCitixPlayerGame& Game, int32 Points, const TCHAR* Reason)
{
	if (Points <= 0)
	{
		return;
	}
	Game.Score += Points;
	UE_LOG(LogCitix, Log, TEXT("[Citix] Score +%d (%s) = %d."), Points, Reason, Game.Score);
	CheckProgressUnlocks(Game);
}

void ACitixSandboxDirector::AddMoney(FCitixPlayerGame& Game, int32 Amount, const TCHAR* Reason)
{
	if (Amount == 0)
	{
		return;
	}
	Game.Money = FMath::Max(0, Game.Money + Amount);
	UE_LOG(LogCitix, Log, TEXT("[Citix] Funds %s$%d (%s) = $%d."),
		Amount > 0 ? TEXT("+") : TEXT(""), Amount, Reason, Game.Money);
}

void ACitixSandboxDirector::CheckProgressUnlocks(FCitixPlayerGame& Game)
{
	if (!Game.bVariantBonusesUnlocked && Game.ActivitiesCompleted >= VariantBonusActivities)
	{
		Game.bVariantBonusesUnlocked = true;
		ShowToast(Game, TEXT("Activity bonuses unlocked: rain / rush / night pay extra"));
		UE_LOG(LogCitix, Log, TEXT("[Citix] Unlock: activity variant bonuses."));
	}
}

void ACitixSandboxDirector::PublishPlayerGame(FCitixPlayerGame& Game)
{
	ACitixPlayerState* PS = PlayerStateFor(Game);
	if (!PS)
	{
		return;
	}
	PS->CitixScore = Game.Score;
	PS->Money = Game.Money;
	PS->JobsCompleted = Game.JobsCompleted;
	PS->ActivitiesCompleted = Game.ActivitiesCompleted;
	PS->PhotosTaken = Game.PhotosTaken;
	PS->Heat = Game.Heat;
	PS->WantedStars = Game.WantedStars;
	PS->EscapeProgress = Game.EscapeProgress;
	PS->WantedState = static_cast<uint8>(GetWantedStateFor(Game));
	PS->bHasWaypoint = Game.bHasWaypoint;
	PS->WaypointLocation = Game.WaypointLocation;
	PS->WaypointName = Game.WaypointName;
	PS->DiscoveredPOIs = Game.DiscoveredPOIs.Array();
	PS->VisitedDistrictsPersonal = Game.VisitedDistricts.Array();
	PS->JobState = Game.JobState;
	PS->JobDestination = Game.JobDestination;
	PS->JobTimeRemaining = Game.JobTimeRemaining;
	PS->JobDistanceRemaining = Game.JobDistanceRemaining;
	PS->bQuestOffered = Game.bQuestOffered;
	PS->ActivityType = Game.ActivityType;
	PS->ActivityState = Game.ActivityState;
	PS->ActivityName = Game.ActivityName;
	PS->ActivityStopsDone = Game.ActivityStopsDone;
	PS->ActivityStopsTotal = Game.ActivityStops.Num();
	PS->ActivityStops = Game.ActivityStops;
	PS->ActivityTimeRemaining = Game.ActivityTimeRemaining;
	PS->ActivityDistanceRemaining = Game.ActivityDistanceRemaining;
	PS->bOffRoute = Game.bOffRoute;
	PS->CardTitle = Game.CardTitle;
	PS->CardSub = Game.CardSub;
	PS->CardSerial = Game.CardSerial;
	PS->ToastText = Game.ToastText;
	PS->ToastSerial = Game.ToastSerial;
	PS->LastPhotoScore = Game.LastPhotoScore;
	PS->LastPhotoBreakdown = Game.LastPhotoBreakdown;
	PS->PhotoCooldown = Game.PhotoCooldown;
	int32 Mask = 0;
	PS->MagAmmo.SetNum(4);
	PS->ReserveAmmo.SetNum(4);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Game.bOwnedWeapons[Index])
		{
			Mask |= (1 << Index);
		}
		PS->MagAmmo[Index] = Game.MagAmmo[Index];
		PS->ReserveAmmo[Index] = Game.ReserveAmmo[Index];
	}
	PS->OwnedMask = Mask;
	PS->CurrentWeapon = Game.CurrentWeapon;
	PS->VisibleWeapon = Game.CurrentWeapon >= 0 ? static_cast<uint8>(Game.CurrentWeapon) : 255;
	PS->bReloading = Game.ReloadTimer > 0.f;
	PS->ReloadFraction = Game.ReloadTimer > 0.f && ReloadDuration > 0.f
		? FMath::Clamp(1.f - Game.ReloadTimer / ReloadDuration, 0.f, 1.f) : 0.f;
	PS->AdsAmount = Game.AdsAmount;
	PS->ForceNetUpdate();
}

void ACitixSandboxDirector::NoteViolence(AController* InInstigator)
{
	LastShooterPC = InInstigator;
	LastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void ACitixSandboxDirector::PublishSharedState()
{
	// Clients own a presentation-only director (route/beacon/guide/cards); only
	// the authority ever writes the replicated game state.
	if (GetNetMode() == NM_Client)
	{
		return;
	}
	// Change-gated: discovery events are rare, but a forced full-array publish
	// every mirror tick saturated the link and starved movement updates.
	int32 Discovered = 0;
	for (const FCitixPointOfInterest& POI : POIs)
	{
		Discovered += POI.bDiscovered ? 1 : 0;
	}
	if (PublishedPOICount == POIs.Num() && PublishedDiscovered == Discovered
		&& PublishedDistricts == VisitedDistricts.Num()
		&& PublishedShops == Shops.Num())
	{
		return;
	}
	PublishedPOICount = POIs.Num();
	PublishedDiscovered = Discovered;
	PublishedDistricts = VisitedDistricts.Num();
	PublishedShops = Shops.Num();

	ACitixGameState* GS = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ACitixGameState> It(World); It; ++It)
		{
			GS = *It;
			break;
		}
	}
	if (!GS)
	{
		return;
	}
	GS->SharedPOIs = POIs;
	GS->VisitedDistricts.Reset();
	for (int32 Index : VisitedDistricts)
	{
		GS->VisitedDistricts.Add(Index);
	}
	GS->ShopLocations.Reset();
	GS->ShopNames.Reset();
	for (const FCitixWeaponShop& Shop : Shops)
	{
		GS->ShopLocations.Add(Shop.Location);
		GS->ShopNames.Add(Shop.Name);
	}
	GS->ForceNetUpdate();
}

void ACitixSandboxDirector::PublishCitySeed()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	ACitixGameState* GS = nullptr;
	for (TActorIterator<ACitixGameState> It(World); It; ++It)
	{
		GS = *It;
		break;
	}
	ACitixCityGenerator* Generator = nullptr;
	for (TActorIterator<ACitixCityGenerator> It(World); It; ++It)
	{
		Generator = *It;
		break;
	}
	if (GS && Generator && Generator->IsGenerated())
	{
		GS->CitySeed = Generator->GetResolvedSeed();
	}
}

void ACitixSandboxDirector::MirrorToReplication()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	// Shared city knowledge for everyone.
	PublishSharedState();

	// Independent records: each driver publishes only their own snapshot, so
	// A earning, shooting or starting a job never touches B's wallet, ammo,
	// objective or stars. Join-in-progress clients snapshot instantly.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (FCitixPlayerGame* Game = FindPlayerGame(PC))
		{
			PublishPlayerGame(*Game);
		}
	}
}

void ACitixSandboxDirector::UpdateFromReplication(const ACitixPlayerState* PS,
	const ACitixGameState* GS, const FVector& PlayerLocation, float DeltaSeconds)
{
	if (!PS)
	{
		return;
	}
	ToastRemaining = FMath::Max(0.f, ToastRemaining - DeltaSeconds);
	DiscoveryCardRemaining = FMath::Max(0.f, DiscoveryCardRemaining - DeltaSeconds);
	DistrictCardRemaining = FMath::Max(0.f, DistrictCardRemaining - DeltaSeconds);
	// Presentation-only: the client never simulates, it just renders the
	// server's snapshot through the same fields the HUD already reads.
	Score = PS->CitixScore;
	Money = PS->Money;
	JobsCompleted = PS->JobsCompleted;
	ActivitiesCompleted = PS->ActivitiesCompleted;
	PhotosTaken = PS->PhotosTaken;
	Heat = PS->Heat;
	WantedStars = PS->WantedStars;
	EscapeProgress = PS->EscapeProgress;
	bHasWaypoint = PS->bHasWaypoint;
	WaypointLocation = PS->WaypointLocation;
	WaypointName = PS->WaypointName;
	JobState = PS->JobState;
	JobDestination = PS->JobDestination;
	JobTimeRemaining = PS->JobTimeRemaining;
	JobDistanceRemaining = PS->JobDistanceRemaining;
	bQuestOffered = PS->bQuestOffered;
	ActivityType = PS->ActivityType;
	ActivityState = PS->ActivityState;
	ActivityName = PS->ActivityName;
	ActivityStopsDone = PS->ActivityStopsDone;
	ActivityStops = PS->ActivityStops;
	ActivityTimeRemaining = PS->ActivityTimeRemaining;
	ActivityDistanceRemaining = PS->ActivityDistanceRemaining;
	bOffRoute = PS->bOffRoute;
	LastPhotoScore = PS->LastPhotoScore;
	LastPhotoBreakdown = PS->LastPhotoBreakdown;
	PhotoCooldown = PS->PhotoCooldown;

	// Race checkpoint overrides the waypoint visuals while racing: the beacon
	// and guide follow the driver's own next checkpoint (rank 7).
	RaceCheckpointLocal = PS->RaceCheckpoint;
	RaceStateLocal = static_cast<ECitixRaceState>(PS->RaceState);

	// Loadout wiring (rank 2 fix): the HUD reads these director fields, so the
	// owner snapshot must land here — a replicated field alone never closed it.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		bOwnedWeapons[Index] = (PS->OwnedMask & (1 << Index)) != 0;
		MagAmmo[Index] = PS->MagAmmo.IsValidIndex(Index) ? PS->MagAmmo[Index] : 0;
		ReserveAmmo[Index] = PS->ReserveAmmo.IsValidIndex(Index) ? PS->ReserveAmmo[Index] : 0;
	}
	CurrentWeapon = PS->CurrentWeapon;
	ClientReloading = PS->bReloading;
	ClientReloadFraction = PS->ReloadFraction;
	AdsAmount = PS->AdsAmount;

	// Cards/toasts are event feeds: show each serial once, timed locally.
	if (PS->CardSerial != LastSeenCardSerial && !PS->CardTitle.IsEmpty())
	{
		LastSeenCardSerial = PS->CardSerial;
		DiscoveryCardTitle = PS->CardTitle;
		DiscoveryCardSub = PS->CardSub;
		DiscoveryCardRemaining = DiscoveryCardDuration;
		DistrictCardRemaining = 0.f;
	}
	if (PS->ToastSerial != LastSeenToastSerial && !PS->ToastText.IsEmpty())
	{
		LastSeenToastSerial = PS->ToastSerial;
		ToastText = PS->ToastText;
		ToastRemaining = 3.f;
	}

	if (GS && GS->SharedPOIs.Num() == POIs.Num())
	{
		for (int32 Index = 0; Index < POIs.Num(); ++Index)
		{
			POIs[Index].bDiscovered = GS->SharedPOIs[Index].bDiscovered;
		}
	}
	if (GS)
	{
		VisitedDistricts.Reset();
		for (int32 Index : GS->VisitedDistricts)
		{
			VisitedDistricts.Add(Index);
		}
	}

	// Beacon follows the same rule as the server: jobs and activities only.
	// Racing overrides everything with the driver's own checkpoint.
	const bool bRacing = RaceStateLocal == ECitixRaceState::Racing
		&& !RaceCheckpointLocal.IsZero();
	const bool bJobActive = !bRacing && JobState == ECitixJobState::Active;
	const bool bTourActive = !bRacing && ActivityState == ECitixActivityState::Active
		&& ActivityType == ECitixActivityType::Tour;
	if (JobBeacon)
	{
		if (bRacing)
		{
			JobBeacon->SetWorldLocation(RaceCheckpointLocal + FVector(0.f, 0.f, 3000.f));
			JobBeacon->SetVisibility(true, true);
		}
		else if (bJobActive)
		{
			JobBeacon->SetWorldLocation(JobDestination + FVector(0.f, 0.f, 3000.f));
			JobBeacon->SetVisibility(true, true);
		}
		else if (ActivityState == ECitixActivityState::Active
			&& ActivityStops.IsValidIndex(ActivityStopsDone))
		{
			JobBeacon->SetWorldLocation(
				ActivityStops[ActivityStopsDone] + FVector(0.f, 0.f, 3000.f));
			JobBeacon->SetVisibility(true, true);
		}
		else
		{
			JobBeacon->SetVisibility(false, true);
		}
	}
	RefreshBeaconMaterial();

	if (bRacing)
	{
		// Route to the checkpoint through the caller's waypoint fields. Only
		// the scalars are restored afterwards; RoutePoints keeps the checkpoint
		// route (and the laid guide) until the next snapshot.
		const bool bSavedHasWaypoint = bHasWaypoint;
		const FVector SavedWaypoint = WaypointLocation;
		bHasWaypoint = true;
		WaypointLocation = RaceCheckpointLocal;
		RebuildRoute(PlayerLocation);
		bHasWaypoint = bSavedHasWaypoint;
		WaypointLocation = SavedWaypoint;
	}
	else if (bHasWaypoint)
	{
		RebuildRoute(PlayerLocation);
	}
	else if (RouteGuide)
	{
		RouteGuide->Clear();
	}
}

void ACitixSandboxDirector::BuildPointsOfInterest(const TArray<FVector>& LandmarkLocations, float CitySize)
{
	POIs.Reset();

	// One POI per planned district, named after the district itself, so exploring the
	// map is exploring the actual city plan.
	for (const FCitixPlanDistrict& District : CityPlan.Districts)
	{
		FCitixPointOfInterest POI;
		POI.Location = FVector(District.Centroid.X, District.Centroid.Y, 0.f);
		POI.Name = District.Name;
		POI.District = District.Type;
		POI.Kind = ECitixPOIKind::District;
		POIs.Add(POI);
	}

	// Landmark towers become named POIs with increasing numbering.
	int32 LandmarkIndex = 1;
	for (const FVector& Landmark : LandmarkLocations)
	{
		FCitixPointOfInterest POI;
		POI.Location = FVector(Landmark.X, Landmark.Y, 0.f);
		POI.Name = FString::Printf(TEXT("Landmark Tower %d"), LandmarkIndex++);
		POI.District = CityPlan.GetDistrictTypeAt(FVector2D(Landmark.X, Landmark.Y));
		POI.Kind = ECitixPOIKind::Landmark;
		POIs.Add(POI);
	}

	// A few waterfront and junction viewpoints, so there is something to find everywhere.
	for (int32 Index = 0; Index < CityPlan.RiverPoints.Num(); Index += 24)
	{
		const FVector2D P = CityPlan.RiverPoints[Index];
		FCitixPointOfInterest POI;
		POI.Location = FVector(P.X, P.Y, 0.f);
		POI.Name = TEXT("Riverside View");
		POI.District = CityPlan.GetDistrictTypeAt(P);
		POI.Kind = ECitixPOIKind::Riverside;
		POIs.Add(POI);
	}

	const int32 NodeStride = FMath::Max(1, Network.Nodes.Num() / 8);
	for (int32 NodeIndex = 0; NodeIndex < Network.Nodes.Num(); NodeIndex += NodeStride)
	{
		const FVector2D Position = Network.Nodes[NodeIndex].Position;
		FCitixPointOfInterest POI;
		POI.Location = FVector(Position.X, Position.Y, 0.f);
		POI.District = CityPlan.GetDistrictTypeAt(Position);
		POI.Name = FString::Printf(TEXT("%s Crossing"), DistrictName(POI.District));
		POI.Kind = ECitixPOIKind::Crossing;
		POIs.Add(POI);
	}
}

void ACitixSandboxDirector::BuildShops()
{
	Shops.Reset();
	if (!bConfigured && Network.Nodes.Num() == 0)
	{
		return;
	}

	// Ten shops: one per district centroid, then landmark overflow. Sites snap to
	// the nearest road node at build time, so employees always stand on the street.
	TArray<FVector> Sites;
	for (const FCitixPlanDistrict& District : CityPlan.Districts)
	{
		Sites.Add(FVector(District.Centroid.X, District.Centroid.Y, 0.f));
		if (Sites.Num() >= 10)
		{
			break;
		}
	}
	for (const FCitixPointOfInterest& POI : POIs)
	{
		if (Sites.Num() >= 10)
		{
			break;
		}
		if (POI.Kind == ECitixPOIKind::Landmark)
		{
			Sites.Add(POI.Location);
		}
	}

	UWorld* World = GetWorld();
	int32 Seed = 7000;
	for (const FVector& Site : Sites)
	{
		const int32 Node = NearestNode(Site);
		if (Node == INDEX_NONE || !Network.IsValidNode(Node)
			|| Node >= Network.NodeEdgeIndices.Num()
			|| Network.NodeEdgeIndices[Node].Num() == 0)
		{
			continue;
		}
		const FVector2D& NodePos = Network.Nodes[Node].Position;

		// Sidewalk side: perpendicular to the first incident edge, on the side with
		// the most room to the next carriageway, so the shop never sits in the road.
		const int32 EdgeIndex = Network.NodeEdgeIndices[Node][0];
		FVector2D EdgeDir(1.f, 0.f);
		if (Network.IsValidEdge(EdgeIndex))
		{
			EdgeDir = Network.EdgeDirection(EdgeIndex);
		}
		const FVector2D Perp(-EdgeDir.Y, EdgeDir.X);
		const float Push = Network.IsValidEdge(EdgeIndex)
			? Network.Edges[EdgeIndex].CorridorWidth * 0.5f + 700.f
			: 1500.f;

		auto ClearanceAt = [this](const FVector2D& P)
		{
			float Best = TNumericLimits<float>::Max();
			for (const FCitixRoadEdge& Edge : Network.Edges)
			{
				if (!Network.IsValidNode(Edge.NodeA) || !Network.IsValidNode(Edge.NodeB))
				{
					continue;
				}
				const FVector A(Network.Nodes[Edge.NodeA].Position.X,
					Network.Nodes[Edge.NodeA].Position.Y, 0.f);
				const FVector B(Network.Nodes[Edge.NodeB].Position.X,
					Network.Nodes[Edge.NodeB].Position.Y, 0.f);
				Best = FMath::Min(Best, SegmentDistance2D(
					FVector(P.X, P.Y, 0.f), A, B));
			}
			return Best;
		};

		const FVector2D SideA = NodePos + Perp * Push;
		const FVector2D SideB = NodePos - Perp * Push;
		const FVector2D ShopXY = (ClearanceAt(SideA) >= ClearanceAt(SideB)) ? SideA : SideB;

		// Face the road: local +X toward the node.
		const FVector2D ToRoad = (NodePos - ShopXY).GetSafeNormal();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToRoad.Y, ToRoad.X));

		FVector Ground(ShopXY.X, ShopXY.Y, Site.Z + 1500.f);
		FHitResult GroundHit;
		FCollisionQueryParams Params;
		Params.bTraceComplex = false;
		if (World && World->LineTraceSingleByChannel(GroundHit, Ground,
			FVector(ShopXY.X, ShopXY.Y, Site.Z - 2000.f), ECC_WorldStatic, Params))
		{
			Ground.Z = GroundHit.Location.Z;
		}
		else
		{
			Ground.Z = Site.Z;
		}

		FCitixWeaponShop Shop;
		Shop.Location = Ground;
		Shop.Name = FString::Printf(TEXT("%s Arms"), *FString(DistrictName(
			CityPlan.GetDistrictTypeAt(ShopXY))));

		if (World)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (ACitixShopkeeper* Keeper = World->SpawnActor<ACitixShopkeeper>(
				ACitixShopkeeper::StaticClass(),
				FTransform(FRotator(0.f, Yaw, 0.f), Ground), SpawnParams))
			{
				Keeper->InitializeKeeper(Seed++);
				Shop.Keeper = Keeper;
			}

			UStaticMeshComponent* Marker = NewObject<UStaticMeshComponent>(this, FName(
				*FString::Printf(TEXT("ShopMarker%d"), Shops.Num())));
			Marker->SetupAttachment(GetRootComponent());
			Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Marker->SetCanEverAffectNavigation(false);
			Marker->SetCastShadow(false);
			if (UStaticMesh* Mesh = FCitixSurfaceLibrary::GetMesh(ECitixSurface::FacadeConcrete))
			{
				Marker->SetStaticMesh(Mesh);
			}
			if (UMaterialInterface* Material = FCitixSurfaceLibrary::GetMaterial(ECitixSurface::EmissiveWarm))
			{
				Marker->SetMaterial(0, Material);
			}
			Marker->SetRelativeScale3D(FVector(1.f, 1.f, 12.f));
			Marker->SetWorldLocation(Ground + FVector(250.f, 0.f, 600.f));
			Marker->RegisterComponent();
			Shop.Marker = Marker;
		}
		Shops.Add(Shop);
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] Built %d weapon shops."), Shops.Num());
}

int32 ACitixSandboxDirector::GetDiscoveredCount() const
{
	int32 Count = 0;
	for (const FCitixPointOfInterest& POI : POIs)
	{
		Count += POI.bDiscovered ? 1 : 0;
	}
	return Count;
}

int32 ACitixSandboxDirector::GetPoliceCount() const
{
	int32 Count = 0;
	for (const FCitixPoliceUnit& Unit : PoliceUnits)
	{
		Count += (Unit.Car != nullptr) ? 1 : 0;
	}
	return Count;
}

float ACitixSandboxDirector::GetNearestPoliceDistance() const
{
	// Clients measure against the replicated police cars (local unit structs
	// do not exist client-side).
	if (GetNetMode() == NM_Client)
	{
		if (UWorld* World = GetWorld())
		{
			FVector PlayerLocation = GetActorLocation();
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					PlayerLocation = Pawn->GetActorLocation();
				}
			}
			float Best = -1.f;
			for (TActorIterator<ACitixPoliceVehicle> It(World); It; ++It)
			{
				if (*It)
				{
					const float Distance = FVector::Dist2D((*It)->GetActorLocation(), PlayerLocation);
					if (Best < 0.f || Distance < Best)
					{
						Best = Distance;
					}
				}
			}
			return Best;
		}
		return -1.f;
	}
	const APawn* PlayerPawn = FocusPC() ? FocusPC()->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();
	float Best = -1.f;
	for (const FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.Car && !Unit.bDisabled)
		{
			const float Distance = FVector::Dist2D(Unit.Car->GetActorLocation(), PlayerLocation);
			if (Best < 0.f || Distance < Best)
			{
				Best = Distance;
			}
		}
	}
	return Best;
}

float ACitixSandboxDirector::GetNearestPursuitRemaining() const
{
	const APawn* PlayerPawn = FocusPC() ? FocusPC()->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();
	float Best = -1.f;
	for (const FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (!Unit.Car || Unit.bDisabled)
		{
			continue;
		}
		// Remaining drive: what is left of the committed path, else crow-flies.
		float Remaining = -1.f;
		if (Unit.PathCursor < Unit.Path.Num())
		{
			Remaining = 0.f;
			FVector2D Prev2D(Unit.Car->GetActorLocation().X, Unit.Car->GetActorLocation().Y);
			for (int32 Cursor = Unit.PathCursor; Cursor < Unit.Path.Num(); ++Cursor)
			{
				const int32 Node = Unit.Path[Cursor];
				if (!Network.IsValidNode(Node))
				{
					break;
				}
				const FVector2D& P = Network.Nodes[Node].Position;
				Remaining += FVector2D::Distance(Prev2D, P);
				Prev2D = P;
			}
		}
		else
		{
			Remaining = FVector::Dist2D(Unit.Car->GetActorLocation(), PlayerLocation);
		}
		if (Best < 0.f || Remaining < Best)
		{
			Best = Remaining;
		}
	}
	return Best;
}

ECitixWantedState ACitixSandboxDirector::GetWantedStateFor(const FCitixPlayerGame& Game) const
{
	if (Game.WantedStars <= 0)
	{
		return ECitixWantedState::Calm;
	}
	AController* SuspectPC = Game.PC.Get();
	APawn* SuspectPawn = SuspectPC ? SuspectPC->GetPawn() : nullptr;
	if (!SuspectPawn)
	{
		return ECitixWantedState::Searching;
	}
	for (const FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.Car && !Unit.bDisabled && Unit.SuspectPC.Get() == SuspectPC
			&& FVector::Dist2D(Unit.Car->GetActorLocation(), SuspectPawn->GetActorLocation()) < 12000.f)
		{
			return ECitixWantedState::Pursuit;
		}
	}
	return ECitixWantedState::Searching;
}

ECitixWantedState ACitixSandboxDirector::GetWantedState() const
{
	// Clients read their own replicated pursuit status (local unit structs do
	// not exist client-side; the cars themselves replicate separately).
	if (GetNetMode() == NM_Client)
	{
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (ACitixPlayerState* PS = Cast<ACitixPlayerState>(PC->PlayerState))
				{
					return static_cast<ECitixWantedState>(PS->WantedState);
				}
			}
		}
		return ECitixWantedState::Calm;
	}
	if (WantedStars <= 0 || GetPoliceCount() == 0)
	{
		return ECitixWantedState::Calm;
	}
	AController* Focus = FocusPC();
	const APawn* PlayerPawn = Focus ? Focus->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();
	for (const FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.Car && FVector::Dist2D(Unit.Car->GetActorLocation(), PlayerLocation) < 12000.f)
		{
			return ECitixWantedState::Pursuit;
		}
	}
	return ECitixWantedState::Searching;
}

void ACitixSandboxDirector::AddScore(int32 Points, const TCHAR* Reason)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		AddScore(*Game, Points, Reason);
	}
}

void ACitixSandboxDirector::CheckProgressUnlocks()
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		CheckProgressUnlocks(*Game);
	}
}

void ACitixSandboxDirector::CycleWaypoint()
{
	CycleWaypoint(FocusPC());
}

void ACitixSandboxDirector::CycleWaypoint(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game || POIs.Num() == 0)
	{
		return;
	}
	// Never hijack an active delivery route (it navigates by its own waypoint).
	if (Game->JobState == ECitixJobState::Active)
	{
		ShowToast(*Game, TEXT("Finish the delivery first (X cancels)"));
		CopyGameToGlobals(*Game);
		return;
	}

	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// Cycle through the nearest few POIs: reachable goals rather than arbitrary ones
	// on the far side of the map.
	TArray<int32> Order;
	Order.Reserve(POIs.Num());
	for (int32 Index = 0; Index < POIs.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.Sort([this, &PlayerLocation](int32 A, int32 B)
	{
		return FVector::DistSquared(POIs[A].Location, PlayerLocation)
			< FVector::DistSquared(POIs[B].Location, PlayerLocation);
	});

	const int32 CandidateCount = FMath::Min(6, Order.Num());
	Game->WaypointCursor = (Game->WaypointCursor + 1) % CandidateCount;
	const int32 PoiIndex = Order[Game->WaypointCursor];
	SetWaypoint(PC, POIs[PoiIndex].Location, POIs[PoiIndex].Name);
}

void ACitixSandboxDirector::SetWaypoint(const FVector& InLocation, const FString& InName)
{
	SetWaypoint(FocusPC(), InLocation, InName);
}

void ACitixSandboxDirector::SetWaypoint(AController* PC, const FVector& InLocation, const FString& InName)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game)
	{
		return;
	}
	Game->WaypointLocation = InLocation;
	Game->WaypointName = InName;
	Game->bHasWaypoint = true;

	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	RebuildRoute(*Game, PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector);
	ShowToast(*Game, FString::Printf(TEXT("Waypoint: %s"), *InName));
	CopyGameToGlobals(*Game);
}

void ACitixSandboxDirector::ClearWaypoint()
{
	ClearWaypoint(FocusPC());
}

void ACitixSandboxDirector::ClearWaypoint(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game)
	{
		return;
	}
	Game->bHasWaypoint = false;
	Game->RoutePoints.Reset();
	if (Game->PC.Get() == FocusPC() && RouteGuide)
	{
		RouteGuide->Clear();
	}
	CopyGameToGlobals(*Game);
}

int32 ACitixSandboxDirector::GetGuideMarkers() const
{
	return RouteGuide ? RouteGuide->GetMarkerCount() : 0;
}

/** Beacon/guide surface matching the current objective (shared mapping). */
static ECitixSurface GuideSurfaceFor(bool bJobActive, bool bTourActive, int32 TourStop)
{
	if (bJobActive)
	{
		return ECitixSurface::EmissiveWarm;
	}
	if (bTourActive)
	{
		const int32 Stop = TourStop % 3;
		return (Stop == 1) ? ECitixSurface::OrbPink
			: (Stop == 2 ? ECitixSurface::EmissiveWarm : ECitixSurface::EmissiveCool);
	}
	return ECitixSurface::EmissiveCool;
}

/** Road height under a point (ground trace, fallback keeps the old Z). */
static float GroundZAt(UWorld* World, const FVector& XY, float FallbackZ)
{
	if (!World)
	{
		return FallbackZ;
	}
	FHitResult GroundHit;
	FCollisionQueryParams Params;
	Params.bTraceComplex = false;
	if (World->LineTraceSingleByChannel(GroundHit, FVector(XY.X, XY.Y, XY.Z + 1500.f),
		FVector(XY.X, XY.Y, XY.Z - 2000.f), ECC_WorldStatic, Params))
	{
		return GroundHit.Location.Z + 60.f;
	}
	return FallbackZ;
}

int32 ACitixSandboxDirector::NearestNode(const FVector& Location) const
{
	int32 Best = INDEX_NONE;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Network.Nodes.Num(); ++Index)
	{
		const FVector2D& P = Network.Nodes[Index].Position;
		const float DistanceSq = FVector2D::DistSquared(P, FVector2D(Location.X, Location.Y));
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Index;
		}
	}
	return Best;
}

bool ACitixSandboxDirector::FindPathNodes(const FVector& From, const FVector& To, TArray<int32>& OutPath) const
{
	OutPath.Reset();
	if (Network.Nodes.Num() == 0 || Network.NodeEdgeIndices.Num() == 0)
	{
		return false;
	}

	const int32 StartNode = NearestNode(From);
	const int32 GoalNode = NearestNode(To);
	if (StartNode == INDEX_NONE || GoalNode == INDEX_NONE || StartNode == GoalNode)
	{
		return false;
	}

	// Dijkstra over the road graph. The graph is tiny (~100 nodes), so a linear scan is
	// far cheaper than a heap and runs at the low update rate anyway.
	const int32 NodeCount = Network.Nodes.Num();
	TArray<float> Cost;
	TArray<int32> Previous;
	Cost.Init(TNumericLimits<float>::Max(), NodeCount);
	Previous.Init(INDEX_NONE, NodeCount);
	TArray<bool> Visited;
	Visited.Init(false, NodeCount);
	Cost[StartNode] = 0.f;

	for (int32 Iteration = 0; Iteration < NodeCount; ++Iteration)
	{
		int32 Current = INDEX_NONE;
		float BestCost = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < NodeCount; ++Index)
		{
			if (!Visited[Index] && Cost[Index] < BestCost)
			{
				BestCost = Cost[Index];
				Current = Index;
			}
		}
		if (Current == INDEX_NONE || Current == GoalNode)
		{
			break;
		}
		Visited[Current] = true;

		for (int32 EdgeIndex : Network.NodeEdgeIndices[Current])
		{
			const FCitixRoadEdge& Edge = Network.Edges[EdgeIndex];
			const int32 Other = (Edge.NodeA == Current) ? Edge.NodeB : Edge.NodeA;
			const float Step = Network.EdgeLength(EdgeIndex);
			if (Cost[Current] + Step < Cost[Other])
			{
				Cost[Other] = Cost[Current] + Step;
				Previous[Other] = Current;
			}
		}
	}

	if (Previous[GoalNode] == INDEX_NONE)
	{
		return false;
	}

	for (int32 Node = GoalNode; Node != INDEX_NONE; Node = Previous[Node])
	{
		OutPath.Add(Node);
		if (Node == StartNode)
		{
			break;
		}
	}
	Algo::Reverse(OutPath);
	return true;
}

void ACitixSandboxDirector::RebuildRoute(const FVector& PlayerLocation)
{
	// Legacy globals path: client presentation mirrors its snapshot into these
	// fields (no record exists client-side), and focus readers use them too.
	RoutePoints.Reset();
	auto ClearGuide = [this]()
	{
		if (RouteGuide)
		{
			RouteGuide->Clear();
		}
	};
	if (!bHasWaypoint || Network.Nodes.Num() == 0 || Network.NodeEdgeIndices.Num() == 0)
	{
		ClearGuide();
		return;
	}

	const int32 StartNode = NearestNode(PlayerLocation);
	const int32 GoalNode = NearestNode(WaypointLocation);
	if (StartNode == INDEX_NONE || GoalNode == INDEX_NONE)
	{
		ClearGuide();
		return;
	}
	if (StartNode == GoalNode)
	{
		RoutePoints.Add(WaypointLocation);
		ClearGuide();
		return;
	}

	TArray<int32> Path;
	if (!FindPathNodes(PlayerLocation, WaypointLocation, Path))
	{
		RoutePoints.Add(WaypointLocation);
		ClearGuide();
		return;
	}
	for (int32 Node : Path)
	{
		const FVector2D& P = Network.Nodes[Node].Position;
		RoutePoints.Add(FVector(P.X, P.Y, 60.f));
	}
	RoutePoints.Add(WaypointLocation);

	if (RouteGuide)
	{
		const bool bPlainWaypoint = ActivityState != ECitixActivityState::Active
			&& JobState != ECitixJobState::Active;
		const ECitixSurface Surface = bPlainWaypoint ? ECitixSurface::OrbCyan
			: GuideSurfaceFor(JobState == ECitixJobState::Active,
				ActivityType == ECitixActivityType::Tour, ActivityStopsDone);
		RouteGuide->UpdateRoute(RoutePoints, Surface);
	}
}

void ACitixSandboxDirector::RebuildRoute(FCitixPlayerGame& Game, const FVector& PlayerLocation)
{
	Game.RoutePoints.Reset();
	const bool bFocus = (Game.PC.Get() == FocusPC());
	auto ClearGuide = [this]()
	{
		if (RouteGuide)
		{
			RouteGuide->Clear();
		}
	};
	if (!Game.bHasWaypoint || Network.Nodes.Num() == 0 || Network.NodeEdgeIndices.Num() == 0)
	{
		if (bFocus)
		{
			ClearGuide();
		}
		return;
	}

	const int32 StartNode = NearestNode(PlayerLocation);
	const int32 GoalNode = NearestNode(Game.WaypointLocation);
	if (StartNode == INDEX_NONE || GoalNode == INDEX_NONE)
	{
		if (bFocus)
		{
			ClearGuide();
		}
		return;
	}
	if (StartNode == GoalNode)
	{
		Game.RoutePoints.Add(Game.WaypointLocation);
		if (bFocus)
		{
			ClearGuide();
		}
		return;
	}

	TArray<int32> Path;
	if (!FindPathNodes(PlayerLocation, Game.WaypointLocation, Path))
	{
		Game.RoutePoints.Add(Game.WaypointLocation);
		if (bFocus)
		{
			ClearGuide();
		}
		return;
	}
	for (int32 Node : Path)
	{
		const FVector2D& P = Network.Nodes[Node].Position;
		Game.RoutePoints.Add(FVector(P.X, P.Y, 60.f));
	}
	Game.RoutePoints.Add(Game.WaypointLocation);

	// Lay the in-scene guide: no map needed while driving.
	// Saved waypoints read teal; everything else matches the beacon.
	// Singleton visuals follow the focus record only; every client rebuilds
	// its own guide from its snapshot.
	if (bFocus && RouteGuide)
	{
		const bool bPlainWaypoint = Game.ActivityState != ECitixActivityState::Active
			&& Game.JobState != ECitixJobState::Active;
		const ECitixSurface Surface = bPlainWaypoint ? ECitixSurface::OrbCyan
			: GuideSurfaceFor(Game.JobState == ECitixJobState::Active,
				Game.ActivityType == ECitixActivityType::Tour, Game.ActivityStopsDone);
		RouteGuide->UpdateRoute(Game.RoutePoints, Surface);
	}
}

bool ACitixSandboxDirector::StartDeliveryJob()
{
	return StartDeliveryJob(FocusPC());
}

bool ACitixSandboxDirector::StartDeliveryJob(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game || Game->JobState == ECitixJobState::Active || Network.Nodes.Num() == 0)
	{
		if (Game && Game->JobState == ECitixJobState::Active)
		{
			ShowToast(*Game, TEXT("Delivery already active"));
			CopyGameToGlobals(*Game);
		}
		return false;
	}
	if (Game->ActivityState == ECitixActivityState::Active)
	{
		ShowToast(*Game, TEXT("Finish the activity first (X cancels)"));
		CopyGameToGlobals(*Game);
		return false;
	}

	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// Prefer a destination inside the requested band; relax if the city is small.
	int32 BestNode = INDEX_NONE;
	for (int32 Attempt = 0; Attempt < 48; ++Attempt)
	{
		const int32 Candidate = Rng.RandRange(0, Network.Nodes.Num() - 1);
		const FVector2D& P = Network.Nodes[Candidate].Position;
		const float Distance = FVector2D::Distance(P, FVector2D(PlayerLocation.X, PlayerLocation.Y));
		if (Distance >= JobMinDistance && Distance <= JobMaxDistance)
		{
			BestNode = Candidate;
			break;
		}
		BestNode = Candidate; // fallback: keep the last candidate
	}
	if (BestNode == INDEX_NONE)
	{
		return false;
	}

	const FVector2D& Destination = Network.Nodes[BestNode].Position;
	Game->JobDestination = FVector(Destination.X, Destination.Y, 0.f);
	Game->JobState = ECitixJobState::Active;

	// Deliveries navigate like everything else: silent waypoint, so the beacon,
	// the route line and the in-scene guide all follow without a map.
	Game->WaypointLocation = Game->JobDestination;
	Game->WaypointName = TEXT("Delivery");
	Game->bHasWaypoint = true;
	RebuildRoute(*Game, PlayerLocation);

	// A waiting priority contract turns this delivery into the daily quest.
	Game->JobRewardMult = 1.f;
	if (Game->bQuestOffered && !Game->bQuestAccepted)
	{
		Game->bQuestOffered = false;
		Game->bQuestAccepted = true;
		Game->JobRewardMult = 3.f;
	}

	const float Distance = FVector::Dist2D(PlayerLocation, Game->JobDestination);
	Game->JobDistanceRemaining = Distance;
	Game->JobTimeRemaining = FMath::Max(12.f, (Distance / FMath::Max(100.f, JobAssumedSpeed)) * 1.6f
		/ (Game->JobRewardMult > 1.f ? 1.25f : 1.f));

	if (Game->PC.Get() == FocusPC() && JobBeacon)
	{
		JobBeacon->SetWorldLocation(Game->JobDestination + FVector(0.f, 0.f, 3000.f));
		JobBeacon->SetVisibility(true, true);
	}
	RefreshBeaconMaterial();

	ShowToast(*Game, Game->JobRewardMult > 1.f
		? TEXT("PRIORITY CONTRACT - 3x pay, tight clock")
		: TEXT("Delivery accepted - reach the beacon"));
	UE_LOG(LogCitix, Log, TEXT("[Citix] Delivery job started: %.0f cm away, %.0f s (x%.0f pay)."),
		Distance, Game->JobTimeRemaining, Game->JobRewardMult);
	CopyGameToGlobals(*Game);
	return true;
}

void ACitixSandboxDirector::CancelDeliveryJob()
{
	CancelDeliveryJob(FocusPC());
}

void ACitixSandboxDirector::CancelDeliveryJob(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game || Game->JobState != ECitixJobState::Active)
	{
		return;
	}
	Game->JobState = ECitixJobState::Idle;
	Game->JobRewardMult = 1.f;
	ClearWaypoint(PC);
	if (Game->PC.Get() == FocusPC() && JobBeacon)
	{
		JobBeacon->SetVisibility(false, true);
	}
	ShowToast(*Game, TEXT("Delivery cancelled"));
	UE_LOG(LogCitix, Log, TEXT("[Citix] Delivery cancelled."));
	CopyGameToGlobals(*Game);
}

void ACitixSandboxDirector::UpdateJob(FCitixPlayerGame& Game, float DeltaSeconds, const FVector& PlayerLocation)
{
	if (Game.JobState != ECitixJobState::Active)
	{
		return;
	}

	Game.JobDistanceRemaining = FVector::Dist2D(PlayerLocation, Game.JobDestination);
	Game.JobTimeRemaining -= DeltaSeconds;

	// Delivered: within 18 m of the beacon. Arrival wins over the clock: the
	// timeout below only fires while the driver is still outside.
	if (Game.JobDistanceRemaining < 1800.f)
	{
		Game.JobState = ECitixJobState::Complete;
		++Game.JobsCompleted;
		const int32 Reward = 100 + FMath::RoundToInt(Game.JobTimeRemaining) * 5;
		AddScore(Game, Reward, TEXT("delivery"));
		const int32 Pay = FMath::RoundToInt(Reward * Game.JobRewardMult);
		AddMoney(Game, Pay, TEXT("delivery"));
		ShowToast(Game, FString::Printf(TEXT("Delivered!  +%d  +$%d  (score %d)"), Reward, Pay, Game.Score));
		if (Game.PC.Get() == FocusPC() && JobBeacon)
		{
			JobBeacon->SetVisibility(false, true);
		}
		Game.JobRewardMult = 1.f;
		Game.bHasWaypoint = false;
		Game.RoutePoints.Reset();
		UE_LOG(LogCitix, Log, TEXT("[Citix] Delivery complete. Score %d, funds $%d."), Game.Score, Game.Money);
		return;
	}

	if (Game.JobTimeRemaining <= 0.f)
	{
		Game.JobState = ECitixJobState::Failed;
		ShowToast(Game, TEXT("Delivery failed - out of time"));
		if (Game.PC.Get() == FocusPC() && JobBeacon)
		{
			JobBeacon->SetVisibility(false, true);
		}
		Game.JobRewardMult = 1.f;
		Game.bHasWaypoint = false;
		Game.RoutePoints.Reset();
	}
}

// ---------------------------------------------------------------------------
// Activities
// ---------------------------------------------------------------------------

bool ACitixSandboxDirector::CanPromptActivity(FString& OutPrompt) const
{
	return CanPromptActivity(FocusPC(), OutPrompt);
}

bool ACitixSandboxDirector::CanPromptActivity(AController* PC, FString& OutPrompt) const
{
	OutPrompt.Reset();
	const FCitixPlayerGame* Game = FindPlayerGameConst(PC);
	if (!bConfigured || !Game || Game->ActivityState == ECitixActivityState::Active
		|| Game->JobState == ECitixJobState::Active || POIs.Num() == 0)
	{
		return false;
	}

	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// A tour prompt wins when an undiscovered landmark is close by.
	for (const FCitixPointOfInterest& POI : POIs)
	{
		if (!POI.bDiscovered && POI.Kind == ECitixPOIKind::Landmark
			&& FVector::DistSquared2D(POI.Location, PlayerLocation) < FMath::Square(15000.f))
		{
			OutPrompt = FString::Printf(TEXT("START TOUR: %s"), *POI.Name);
			return true;
		}
	}

	int32 Best = INDEX_NONE;
	float BestDistanceSq = FMath::Square(30000.f);
	for (int32 Index = 0; Index < POIs.Num(); ++Index)
	{
		const float DistanceSq = FVector::DistSquared2D(POIs[Index].Location, PlayerLocation);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Index;
		}
	}
	if (Best != INDEX_NONE)
	{
		OutPrompt = FString::Printf(TEXT("START ROUTE: %s"), *POIs[Best].Name);
		return true;
	}
	return false;
}

bool ACitixSandboxDirector::StartActivity()
{
	return StartActivity(FocusPC());
}

bool ACitixSandboxDirector::StartActivity(AController* PC)
{
// Local aliases shadow the legacy focus globals by design (the record is the
// authority now); the shadowing warning is noise here, not a bug.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4458)
#endif
	FCitixPlayerGame* GamePtr = FindPlayerGame(PC);
	if (!GamePtr)
	{
		return false;
	}
	FCitixPlayerGame& Game = *GamePtr;
	ECitixActivityState& ActivityState = Game.ActivityState;
	ECitixJobState& JobState = Game.JobState;
	TArray<FVector>& ActivityStops = Game.ActivityStops;
	TArray<FString>& ActivityStopNames = Game.ActivityStopNames;
	int32& ActivityStopsDone = Game.ActivityStopsDone;
	bool& bActivityRushRoute = Game.bActivityRushRoute;
	ECitixActivityType& ActivityType = Game.ActivityType;
	FString& ActivityName = Game.ActivityName;
	float& ActivityTimeRemaining = Game.ActivityTimeRemaining;
	bool& bOffRoute = Game.bOffRoute;
	float& OffRouteTimer = Game.OffRouteTimer;
	if (ActivityState == ECitixActivityState::Active || POIs.Num() == 0)
	{
		if (ActivityState == ECitixActivityState::Active)
		{
			ShowToast(Game, TEXT("Activity already active"));
			CopyGameToGlobals(Game);
		}
		return false;
	}
	if (JobState == ECitixJobState::Active)
	{
		ShowToast(Game, TEXT("Finish the delivery first (X cancels)"));
		CopyGameToGlobals(Game);
		return false;
	}

	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// Rush-hour awareness: a busy city means a longer route for a bigger reward.
	const bool bRush = AppliedDensity > 1.2f;

	// Prefer a landmark tour when undiscovered landmarks are near.
	int32 TourAnchor = INDEX_NONE;
	float TourAnchorDistanceSq = FMath::Square(20000.f);
	for (int32 Index = 0; Index < POIs.Num(); ++Index)
	{
		if (!POIs[Index].bDiscovered && POIs[Index].Kind == ECitixPOIKind::Landmark)
		{
			const float DistanceSq = FVector::DistSquared2D(POIs[Index].Location, PlayerLocation);
			if (DistanceSq < TourAnchorDistanceSq)
			{
				TourAnchorDistanceSq = DistanceSq;
				TourAnchor = Index;
			}
		}
	}

	ActivityStops.Reset();
	ActivityStopNames.Reset();
	ActivityStopsDone = 0;
	bActivityRushRoute = bRush;

	if (TourAnchor != INDEX_NONE)
	{
		// Tour: the anchor plus the two nearest other unvisited stops.
		ActivityType = ECitixActivityType::Tour;
		ActivityStops.Add(POIs[TourAnchor].Location);
		ActivityStopNames.Add(POIs[TourAnchor].Name);

		TArray<int32> Order;
		for (int32 Index = 0; Index < POIs.Num(); ++Index)
		{
			if (Index != TourAnchor && !POIs[Index].bDiscovered
				&& POIs[Index].Kind != ECitixPOIKind::Crossing)
			{
				Order.Add(Index);
			}
		}
		Order.Sort([this, Anchor = POIs[TourAnchor].Location](int32 A, int32 B)
		{
			return FVector::DistSquared(POIs[A].Location, Anchor)
				< FVector::DistSquared(POIs[B].Location, Anchor);
		});
		for (int32 K = 0; K < FMath::Min(2, Order.Num()); ++K)
		{
			ActivityStops.Add(POIs[Order[K]].Location);
			ActivityStopNames.Add(POIs[Order[K]].Name);
		}
		ActivityName = FString::Printf(TEXT("Landmark tour (%d stops)"), ActivityStops.Num());
		ActivityTimeRemaining = 0.f;
	}
	else
	{
		// Drive: the nearest undiscovered POI in a driving band, else a road node.
		int32 Best = INDEX_NONE;
		float BestDistanceSq = 0.f;
		const float BandMax = (bRush ? 120000.f : 95000.f) * (bRush ? 1.3f : 1.f);
		for (int32 Index = 0; Index < POIs.Num(); ++Index)
		{
			if (POIs[Index].bDiscovered)
			{
				continue;
			}
			const float DistanceSq = FVector::DistSquared2D(POIs[Index].Location, PlayerLocation);
			if (DistanceSq >= FMath::Square(40000.f) && DistanceSq <= FMath::Square(BandMax)
				&& (Best == INDEX_NONE || DistanceSq < BestDistanceSq))
			{
				BestDistanceSq = DistanceSq;
				Best = Index;
			}
		}

		FVector Destination = FVector::ZeroVector;
		FString DestinationName = TEXT("Waypoint");
		bool bHasDestination = false;
		if (Best != INDEX_NONE)
		{
			Destination = POIs[Best].Location;
			DestinationName = POIs[Best].Name;
			bHasDestination = true;
		}
		else if (Network.Nodes.Num() > 0)
		{
			const int32 Node = Rng.RandRange(0, Network.Nodes.Num() - 1);
			const FVector2D& P = Network.Nodes[Node].Position;
			Destination = FVector(P.X, P.Y, 0.f);
			DestinationName = TEXT("Far side of town");
			bHasDestination = true;
		}
		if (!bHasDestination)
		{
			return false;
		}

		ActivityType = ECitixActivityType::Drive;
		ActivityStops.Add(Destination);
		ActivityStopNames.Add(DestinationName);
		ActivityName = FString::Printf(TEXT("Route to %s%s"),
			*DestinationName, bRush ? TEXT(" (rush hour!)") : TEXT(""));
		const float Distance = FVector::Dist2D(PlayerLocation, Destination);
		ActivityTimeRemaining = FMath::Max(25.f, (Distance / 1200.f) * 1.8f);
	}

	ActivityState = ECitixActivityState::Active;
	bOffRoute = false;
	OffRouteTimer = 0.f;

	// Route to the first stop without the waypoint toast (the activity toast covers it).
	Game.WaypointLocation = ActivityStops[0];
	Game.WaypointName = ActivityStopNames[0];
	Game.bHasWaypoint = true;
	RebuildRoute(Game, PlayerLocation);
	if (Game.PC.Get() == FocusPC() && JobBeacon)
	{
		JobBeacon->SetWorldLocation(ActivityStops[0] + FVector(0.f, 0.f, 3000.f));
		JobBeacon->SetVisibility(true, true);
	}
	RefreshBeaconMaterial();

	ShowToast(Game, FString::Printf(TEXT("Activity: %s  (X cancels)"), *ActivityName));
	UE_LOG(LogCitix, Log, TEXT("[Citix] Activity started: %s, %d stops."),
		*ActivityName, ActivityStops.Num());
	CopyGameToGlobals(Game);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	return true;
}

void ACitixSandboxDirector::CancelActivity()
{
	CancelActivity(FocusPC());
}

void ACitixSandboxDirector::CancelActivity(AController* PC)
{
	FCitixPlayerGame* GamePtr = FindPlayerGame(PC);
	if (!GamePtr || GamePtr->ActivityState != ECitixActivityState::Active)
	{
		return;
	}
	FCitixPlayerGame& Game = *GamePtr;
	Game.ActivityState = ECitixActivityState::Idle;
	Game.ActivityType = ECitixActivityType::None;
	Game.bOffRoute = false;
	ClearWaypoint(PC);
	if (Game.PC.Get() == FocusPC() && JobBeacon)
	{
		JobBeacon->SetVisibility(false, true);
	}
	ShowToast(Game, TEXT("Activity cancelled"));
	UE_LOG(LogCitix, Log, TEXT("[Citix] Activity cancelled."));
	CopyGameToGlobals(Game);
}

float ACitixSandboxDirector::RiverDistance(const FVector& Location) const
{
	float Best = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < CityPlan.RiverPoints.Num(); Index += 4)
	{
		const float Distance = FVector2D::Distance(
			FVector2D(Location.X, Location.Y), CityPlan.RiverPoints[Index]);
		Best = FMath::Min(Best, Distance);
	}
	return Best;
}

void ACitixSandboxDirector::UpdateActivity(FCitixPlayerGame& Game, float DeltaSeconds, const FVector& PlayerLocation)
{
// See StartActivity: aliases shadow focus globals by design.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4458)
#endif
	ECitixActivityState& ActivityState = Game.ActivityState;
	TArray<FVector>& ActivityStops = Game.ActivityStops;
	TArray<FString>& ActivityStopNames = Game.ActivityStopNames;
	int32& ActivityStopsDone = Game.ActivityStopsDone;
	float& ActivityTimeRemaining = Game.ActivityTimeRemaining;
	float& ActivityDistanceRemaining = Game.ActivityDistanceRemaining;
	ECitixActivityType& ActivityType = Game.ActivityType;
	bool& bOffRoute = Game.bOffRoute;
	float& OffRouteTimer = Game.OffRouteTimer;
	float& OffRouteToastCooldown = Game.OffRouteToastCooldown;
	TArray<FVector>& RoutePoints = Game.RoutePoints;
	if (ActivityState != ECitixActivityState::Active || ActivityStops.Num() == 0)
	{
		return;
	}

	const FVector& Target = ActivityStops[ActivityStopsDone];
	ActivityDistanceRemaining = FVector::Dist2D(PlayerLocation, Target);
	if (ActivityType == ECitixActivityType::Drive)
	{
		ActivityTimeRemaining -= DeltaSeconds;
	}

	// Arrival at the current stop.
	if (ActivityDistanceRemaining < ActivityArrivalRadius)
	{
		if (ActivityType == ECitixActivityType::Tour)
		{
			AddScore(Game, 60, TEXT("tour stop"));
			++ActivityStopsDone;
			if (ActivityStopsDone >= ActivityStops.Num())
			{
				ActivityState = ECitixActivityState::Complete;
				++Game.ActivitiesCompleted;
				AddScore(Game, 120, TEXT("tour complete"));
				const int32 TourTotal = ActivityStopsDone * 60 + 120;
				ShowToast(Game, FString::Printf(TEXT("Tour complete!  +%d  (score %d)"), TourTotal, Game.Score));
				ClearWaypoint(Game.PC.Get());
				if (Game.PC.Get() == FocusPC() && JobBeacon)
				{
					JobBeacon->SetVisibility(false, true);
				}
				UE_LOG(LogCitix, Log, TEXT("[Citix] Tour complete. Score %d."), Game.Score);
				return;
			}
			ShowToast(Game, FString::Printf(TEXT("Stop %d/%d  +60"), ActivityStopsDone + 1, ActivityStops.Num()));
			Game.WaypointLocation = ActivityStops[ActivityStopsDone];
			Game.WaypointName = ActivityStopNames[ActivityStopsDone];
			RebuildRoute(Game, PlayerLocation);
			if (Game.PC.Get() == FocusPC() && JobBeacon)
			{
				JobBeacon->SetWorldLocation(ActivityStops[ActivityStopsDone] + FVector(0.f, 0.f, 3000.f));
			}
			RefreshBeaconMaterial();
			return;
		}

		// Drive complete: base reward plus variant bonuses (rain / rush / night).
		int32 Reward = 120 + FMath::Max(0, FMath::RoundToInt(ActivityTimeRemaining)) * 3;
		FString BonusText;
		if (Game.bVariantBonusesUnlocked)
		{
			if (ACitixWeatherSystem* Weather = ACitixWeatherSystem::Find(GetWorld()))
			{
				if (Weather->GetRainIntensity() > 0.4f)
				{
					Reward += 40;
					BonusText += TEXT(" rain +40");
				}
			}
			if (Game.bActivityRushRoute)
			{
				Reward = FMath::RoundToInt(Reward * 1.5f);
				BonusText += TEXT(" rush x1.5");
			}
			if (ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(GetWorld()))
			{
				if (TimeOfDay->IsNight() && RiverDistance(Target) < 8000.f)
				{
					Reward += 60;
					BonusText += TEXT(" night waterfront +60");
				}
			}
		}
		ActivityState = ECitixActivityState::Complete;
		++Game.ActivitiesCompleted;
		AddScore(Game, Reward, TEXT("route complete"));
		CheckProgressUnlocks(Game);
		ShowToast(Game, FString::Printf(TEXT("Arrived!  +%d%s  (score %d)"), Reward, *BonusText, Game.Score));
		ClearWaypoint(Game.PC.Get());
		if (Game.PC.Get() == FocusPC() && JobBeacon)
		{
			JobBeacon->SetVisibility(false, true);
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Drive activity complete. Score %d."), Game.Score);
		return;
	}

	if (ActivityType == ECitixActivityType::Drive && ActivityTimeRemaining <= 0.f)
	{
		ActivityState = ECitixActivityState::Failed;
		ShowToast(Game, TEXT("Too slow - route failed"));
		ClearWaypoint(Game.PC.Get());
		if (Game.PC.Get() == FocusPC() && JobBeacon)
		{
			JobBeacon->SetVisibility(false, true);
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Drive activity failed (timer)."));
		return;
	}

	// Lost recovery: far from the drawn route for a while means a nudge, not a fail.
	// The route itself rebuilds every tick, so following the green line always works.
	float NearestRouteDistance = TNumericLimits<float>::Max();
	for (int32 Index = 1; Index < RoutePoints.Num(); ++Index)
	{
		NearestRouteDistance = FMath::Min(NearestRouteDistance,
			SegmentDistance2D(PlayerLocation, RoutePoints[Index - 1], RoutePoints[Index]));
	}
	if (RoutePoints.Num() > 0 && NearestRouteDistance > OffRouteDistance)
	{
		bOffRoute = true;
		OffRouteTimer += DeltaSeconds;
		OffRouteToastCooldown = FMath::Max(0.f, OffRouteToastCooldown - DeltaSeconds);
		if (OffRouteTimer > 6.f && OffRouteToastCooldown <= 0.f)
		{
			OffRouteToastCooldown = 20.f;
			ShowToast(Game, TEXT("Off route - follow the green line (X cancels)"));
		}
	}
	else
	{
		bOffRoute = false;
		OffRouteTimer = 0.f;
	}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

// ---------------------------------------------------------------------------
// Photo mode scoring (no image analysis: geometry + time + weather only)
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::UpdatePhoto(FCitixPlayerGame& Game, float DeltaSeconds)
{
	Game.PhotoCooldown = FMath::Max(0.f, Game.PhotoCooldown - DeltaSeconds);
}

float ACitixSandboxDirector::GetPhotoCooldownFraction() const
{
	return (PhotoCooldown > 0.f) ? FMath::Clamp(PhotoCooldown / 8.f, 0.f, 1.f) : 0.f;
}

bool ACitixSandboxDirector::CapturePhoto()
{
	return CapturePhoto(FocusPC());
}

bool ACitixSandboxDirector::CapturePhoto(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game || Game->PhotoCooldown > 0.f || !bConfigured)
	{
		return false;
	}

	APlayerController* ViewPC = Cast<APlayerController>(PC);
	if (!ViewPC)
	{
		return false;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	ViewPC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	return ApplyPhotoScore(*Game, CameraLocation, CameraRotation.Vector(), true);
}

bool ACitixSandboxDirector::ResolvePhoto(AController* PC, const FVector& CameraLocation,
	const FVector& Direction)
{
	// Server-validated capture (rank 19): finite view, cooldown, and a bound
	// to the photographer's pawn. Rewards credit the record, so they survive
	// replication; the screenshot itself stays local to the client.
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game || Game->PhotoCooldown > 0.f || !bConfigured)
	{
		return false;
	}
	if (CameraLocation.ContainsNaN() || Direction.ContainsNaN()
		|| FMath::Abs(Direction.SizeSquared() - 1.f) > 0.05f)
	{
		return false;
	}
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FVector Anchor = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	if (!Pawn || FVector::DistSquared(CameraLocation, Anchor) > FMath::Square(30000.f))
	{
		return false;
	}
	return ApplyPhotoScore(*Game, CameraLocation, Direction.GetSafeNormal(), false);
}

bool ACitixSandboxDirector::ApplyPhotoScore(FCitixPlayerGame& Game, const FVector& CameraLocation,
	const FVector& Forward, bool bTakeScreenshot)
{
	int32 Total = 0;
	FString Breakdown;

	// Landmark in view: within a forward cone and inside 600 m.
	int32 BestLandmark = INDEX_NONE;
	float BestLandmarkDistance = 60000.f;
	for (int32 Index = 0; Index < POIs.Num(); ++Index)
	{
		if (POIs[Index].Kind != ECitixPOIKind::Landmark && POIs[Index].Kind != ECitixPOIKind::Riverside)
		{
			continue;
		}
		const FVector ToPOI = POIs[Index].Location - CameraLocation;
		const float Distance = ToPOI.Size2D();
		if (Distance > BestLandmarkDistance)
		{
			continue;
		}
		const FVector Direction = ToPOI.GetSafeNormal();
		const float CosAngle = FVector::DotProduct(Forward, Direction);
		if (CosAngle > 0.94f) // ~20 degree cone
		{
			BestLandmark = Index;
			BestLandmarkDistance = Distance;
		}
	}
	if (BestLandmark != INDEX_NONE)
	{
		const int32 Points = POIs[BestLandmark].bDiscovered ? 30 : 60;
		Total += Points;
		Breakdown += FString::Printf(TEXT("landmark +%d "), Points);
	}

	// Time of day: golden hour and night lights pay.
	if (ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(GetWorld()))
	{
		const float Hour = TimeOfDay->GetHours();
		const bool bGolden = (Hour >= 16.5f && Hour <= 19.5f) || (Hour >= 4.5f && Hour <= 7.f);
		if (bGolden)
		{
			Total += 30;
			Breakdown += TEXT("golden hour +30 ");
		}
		else if (TimeOfDay->IsNight())
		{
			Total += 25;
			Breakdown += TEXT("night lights +25 ");
		}
		else
		{
			Total += 10;
			Breakdown += TEXT("daylight +10 ");
		}
	}

	// Weather mood.
	float Rain = 0.f;
	if (ACitixWeatherSystem* Weather = ACitixWeatherSystem::Find(GetWorld()))
	{
		Rain = Weather->GetRainIntensity();
	}
	if (Rain > 0.4f)
	{
		Total += 25;
		Breakdown += TEXT("storm +25 ");
	}
	else if (Rain > 0.05f)
	{
		Total += 15;
		Breakdown += TEXT("light rain +15 ");
	}
	else
	{
		Total += 10;
		Breakdown += TEXT("clear +10 ");
	}

	// Waterfront / viewpoint bonus.
	if (RiverDistance(CameraLocation) < 12000.f)
	{
		Total += 25;
		Breakdown += TEXT("waterfront +25 ");
	}

	if (Total <= 0)
	{
		return false;
	}

	Game.PhotoCooldown = 8.f;
	++Game.PhotosTaken;
	Game.LastPhotoScore = Total;
	Game.LastPhotoBreakdown = Breakdown;
	AddScore(Game, Total, TEXT("photo"));
	ShowToast(Game, FString::Printf(TEXT("Photo  +%d  (%s)"), Total, *Breakdown.TrimEnd()));
	UE_LOG(LogCitix, Log, TEXT("[Citix] Photo scored %d: %s"), Total, *Breakdown);
	CopyGameToGlobals(Game);

	// Screenshots save where a screen exists: standalone server-side, online
	// on the capturing client (it saves at send time).
	if (bTakeScreenshot && GetWorld() && GetWorld()->GetNetMode() == NM_Standalone)
	{
		if (APlayerController* ViewPC = Cast<APlayerController>(Game.PC.Get()))
		{
			ViewPC->ConsoleCommand(TEXT("HighResShot 1"), /*bWriteToLog*/ false);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Weapons: hitscan guns with ammo, ADS and a sniper scope.
// Kills use the existing lethal-ragdoll path (fade-out, no gore) and escalate.
// ---------------------------------------------------------------------------

bool ACitixSandboxDirector::IsWeaponOwned(int32 Index) const
{
	return Index >= 0 && Index < GetWeaponCount() && bOwnedWeapons[Index];
}

bool ACitixSandboxDirector::IsWeaponOwned(AController* PC, int32 Index) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return Index >= 0 && Index < GetWeaponCount() && Game->bOwnedWeapons[Index];
	}
	return IsWeaponOwned(Index);
}

int32 ACitixSandboxDirector::GetMagAmmo(int32 Index) const
{
	return (Index >= 0 && Index < GetWeaponCount()) ? MagAmmo[Index] : 0;
}

int32 ACitixSandboxDirector::GetMagAmmo(AController* PC, int32 Index) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return (Index >= 0 && Index < GetWeaponCount()) ? Game->MagAmmo[Index] : 0;
	}
	return GetMagAmmo(Index);
}

int32 ACitixSandboxDirector::GetReserveAmmo(int32 Index) const
{
	return (Index >= 0 && Index < GetWeaponCount()) ? ReserveAmmo[Index] : 0;
}

int32 ACitixSandboxDirector::GetReserveAmmo(AController* PC, int32 Index) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return (Index >= 0 && Index < GetWeaponCount()) ? Game->ReserveAmmo[Index] : 0;
	}
	return GetReserveAmmo(Index);
}

int32 ACitixSandboxDirector::GetCurrentWeapon(AController* PC) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return Game->CurrentWeapon;
	}
	return GetCurrentWeapon();
}

bool ACitixSandboxDirector::IsReloading(AController* PC) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return Game->ReloadTimer > 0.f;
	}
	return IsReloading();
}

float ACitixSandboxDirector::GetReloadFraction(AController* PC) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return (Game->ReloadTimer > 0.f && ReloadDuration > 0.f)
			? FMath::Clamp(1.f - Game->ReloadTimer / ReloadDuration, 0.f, 1.f) : 1.f;
	}
	return GetReloadFraction();
}

float ACitixSandboxDirector::GetCurrentSpreadDegrees(AController* PC) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return GetCurrentSpreadDegrees(*Game);
	}
	return GetCurrentSpreadDegrees();
}

float ACitixSandboxDirector::GetAdsAmount(AController* PC) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return Game->AdsAmount;
	}
	return GetAdsAmount();
}

bool ACitixSandboxDirector::IsScopeActive(AController* PC) const
{
	if (const FCitixPlayerGame* Game = FindPlayerGameConst(PC))
	{
		return IsScopeActive(*Game);
	}
	return IsScopeActive();
}

float ACitixSandboxDirector::GetReloadFraction() const
{
	if (GetNetMode() == NM_Client)
	{
		return ClientReloadFraction;
	}
	return (ReloadTimer > 0.f && ReloadDuration > 0.f)
		? FMath::Clamp(1.f - ReloadTimer / ReloadDuration, 0.f, 1.f)
		: 1.f;
}

float ACitixSandboxDirector::GetCurrentSpreadDegrees(const FCitixPlayerGame& Game) const
{
	if (Game.CurrentWeapon < 0 || Game.CurrentWeapon >= GetWeaponCount())
	{
		return 0.f;
	}
	const float Base = CitixWeaponCatalogue()[Game.CurrentWeapon].SpreadDegrees;
	return Base * FMath::Lerp(1.f, 0.35f, Game.AdsAmount);
}

float ACitixSandboxDirector::GetCurrentSpreadDegrees() const
{
	if (CurrentWeapon < 0 || CurrentWeapon >= GetWeaponCount())
	{
		return 0.f;
	}
	const float Base = CitixWeaponCatalogue()[CurrentWeapon].SpreadDegrees;
	return Base * FMath::Lerp(1.f, 0.35f, AdsAmount);
}

bool ACitixSandboxDirector::IsScopeActive(const FCitixPlayerGame& Game) const
{
	return Game.CurrentWeapon >= 0 && Game.CurrentWeapon < GetWeaponCount()
		&& CitixWeaponCatalogue()[Game.CurrentWeapon].bScoped
		&& Game.AdsAmount > 0.7f;
}

bool ACitixSandboxDirector::IsScopeActive() const
{
	return CurrentWeapon >= 0 && CurrentWeapon < GetWeaponCount()
		&& CitixWeaponCatalogue()[CurrentWeapon].bScoped
		&& AdsAmount > 0.7f;
}

bool ACitixSandboxDirector::SetAdsHeld(bool bHeld)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		Game->bAdsHeld = bHeld;
	}
	bAdsHeld = bHeld;
	return true;
}

bool ACitixSandboxDirector::SetAdsHeld(AController* PC, bool bHeld)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(PC))
	{
		Game->bAdsHeld = bHeld;
		return true;
	}
	return false;
}

bool ACitixSandboxDirector::SetTriggerHeld(bool bHeld)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		if (bHeld && !Game->bTriggerHeld)
		{
			Game->bTriggerFresh = true;
		}
		Game->bTriggerHeld = bHeld;
	}
	bTriggerHeld = bHeld;
	return true;
}

bool ACitixSandboxDirector::SetTriggerHeld(AController* PC, bool bHeld)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(PC))
	{
		if (bHeld && !Game->bTriggerHeld)
		{
			Game->bTriggerFresh = true;
		}
		Game->bTriggerHeld = bHeld;
		return true;
	}
	return false;
}

bool ACitixSandboxDirector::StartReload()
{
	return StartReload(FocusPC());
}

bool ACitixSandboxDirector::StartReload(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!bConfigured || !Game || Game->CurrentWeapon < 0 || Game->ReloadTimer > 0.f)
	{
		return false;
	}
	const FCitixWeaponDef& Def = CitixWeaponCatalogue()[Game->CurrentWeapon];
	if (Game->MagAmmo[Game->CurrentWeapon] >= Def.MagSize
		|| Game->ReserveAmmo[Game->CurrentWeapon] <= 0)
	{
		return false;
	}
	Game->ReloadTimer = ReloadDuration;
	return true;
}

bool ACitixSandboxDirector::FireWeapon()
{
	// Legacy standalone path: the single local driver.
	UWorld* World = GetWorld();
	AController* Shooter = World ? World->GetFirstPlayerController() : nullptr;
	return FireWeapon(Shooter);
}

bool ACitixSandboxDirector::FireWeapon(AController* Shooter)
{
	FCitixPlayerGame* ShooterGame = FindPlayerGame(Shooter);
	if (!bConfigured || !ShooterGame || ShooterGame->CurrentWeapon < 0
		|| ShooterGame->CurrentWeapon >= GetWeaponCount())
	{
		NoWeaponToastCooldown = FMath::Max(0.f, NoWeaponToastCooldown - UpdateInterval);
		if (NoWeaponToastCooldown <= 0.f)
		{
			NoWeaponToastCooldown = 5.f;
			ShowToast(TEXT("No weapon - visit a gunsmith (orange marker)"), 3.f);
		}
		return false;
	}
	if (ShooterGame->ReloadTimer > 0.f || ShooterGame->FireCooldownTimer > 0.f)
	{
		return false;
	}

	APawn* PlayerPawn = Shooter ? Shooter->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return false;
	}
	// NOTE: no on-foot gate here (legacy callers, e.g. the SP weapon test,
	// fire from the car). The no-drive-by rule is enforced at the input and
	// RPC layers, which are on-foot only.

	// Aim from the shooter's own viewpoint (server control rotation for RPC
	// shooters; explicit client aim arrives via ResolveShot instead).
	FVector CameraLocation = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 150.f);
	FRotator CameraRotation = Shooter ? Shooter->GetControlRotation() : FRotator::ZeroRotator;
	if (APlayerController* PC = Cast<APlayerController>(Shooter))
	{
		PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	}
	return ResolveShot(Shooter, CameraLocation, CameraRotation.Vector(), ShooterGame->bAdsHeld);
}

bool ACitixSandboxDirector::ResolveShot(AController* Shooter, const FVector& CameraLocation,
	const FVector& Direction, bool bAiming)
{
	FCitixPlayerGame* ShooterGame = FindPlayerGame(Shooter);
	if (!bConfigured || !ShooterGame || ShooterGame->CurrentWeapon < 0
		|| ShooterGame->CurrentWeapon >= GetWeaponCount())
	{
		return false;
	}
	FCitixPlayerGame& SG = *ShooterGame;
	if (SG.ReloadTimer > 0.f || SG.FireCooldownTimer > 0.f)
	{
		return false;
	}

	APawn* PlayerPawn = Shooter ? Shooter->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return false;
	}

	// Shot validation (rank 3): finite aim, origin bound to the shooter's head,
	// living shooter. The trace itself is the line-of-sight check: walls block.
	if (CameraLocation.ContainsNaN() || Direction.ContainsNaN()
		|| FMath::Abs(Direction.SizeSquared() - 1.f) > 0.05f)
	{
		UE_LOG(LogCitix, Warning, TEXT("[Citix] Rejected shot: malformed aim."));
		return false;
	}
	const FVector HeadLocation = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 150.f);
	// 1000 cm covers chase-camera booms with lag; teleport exploits still fail
	// by orders of magnitude.
	const float OriginDistSq = FVector::DistSquared(CameraLocation, HeadLocation);
	if (OriginDistSq > FMath::Square(1000.f))
	{
		UE_LOG(LogCitix, Warning, TEXT("[Citix] Rejected shot: origin %.0f cm from shooter."),
			FMath::Sqrt(OriginDistSq));
		return false;
	}

	const FCitixWeaponDef& Def = CitixWeaponCatalogue()[SG.CurrentWeapon];
	if (SG.MagAmmo[SG.CurrentWeapon] <= 0)
	{
		// Dry-fire: roll straight into a reload when there is ammo left.
		if (SG.ReserveAmmo[SG.CurrentWeapon] > 0)
		{
			StartReload(Shooter);
		}
		else
		{
			ShowToast(TEXT("No ammo - visit a gunsmith"), 2.f);
		}
		return false;
	}

	// Aim arrives via parameters (shooter viewpoint or client RPC); the shooter
	// pawn was validated by the caller.

	// Face where we shoot: snap the on-foot body to the aim yaw immediately.
	// (Movement input is controller-relative, so this never breaks walking.)
	// Replicates, so remotes see the shooter turn.
	const FRotator AimRot = Direction.Rotation();
	if (ACitixOnFootPawn* OnFoot = Cast<ACitixOnFootPawn>(PlayerPawn))
	{
		const FRotator PawnRotation = OnFoot->GetActorRotation();
		OnFoot->SetActorRotation(FRotator(PawnRotation.Pitch, AimRot.Yaw, PawnRotation.Roll));
	}

	// Spread cone: tighter while aiming down the sights. The RPC aim flag
	// temporarily stands in for the shooter's ADS state (restored after).
	const bool bSavedAds = SG.bAdsHeld;
	SG.bAdsHeld = bAiming;
	const float Spread = GetCurrentSpreadDegrees(SG);
	SG.bAdsHeld = bSavedAds;
	FVector ShotDirection = Direction;
	if (Spread > 0.001f)
	{
		const float YawJitter = Rng.FRandRange(-Spread, Spread);
		const float PitchJitter = Rng.FRandRange(-Spread, Spread);
		ShotDirection = (AimRot + FRotator(PitchJitter, YawJitter, 0.f)).Vector();
	}

	// Hits are decided from the camera (crosshair-honest); the tracer starts at
	// the gun's muzzle so shots visibly leave the weapon, not the face.
	FVector Impact = CameraLocation + ShotDirection * WeaponRange;
	FCollisionQueryParams Params;
	Params.bTraceComplex = false;
	Params.AddIgnoredActor(PlayerPawn);
	if (WeaponProp)
	{
		Params.AddIgnoredActor(WeaponProp);
	}
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, Impact,
		ECC_Visibility, Params))
	{
		Impact = Hit.Location;
	}
	const FVector Muzzle = (WeaponProp && !WeaponProp->IsHidden())
		? WeaponProp->GetMuzzleLocation()
		: PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 140.f) + ShotDirection * 60.f;

	// Tracer from the pool (first idle actor), coloured per gun.
	ECitixSurface TracerSurface = ECitixSurface::EmissiveCool;
	if (Def.TracerColor.G > 0.8f && Def.TracerColor.R > 0.8f)
	{
		TracerSurface = ECitixSurface::EmissiveWarm;
	}
	PlayShotVisual(Muzzle, Impact, TracerSurface);

	--SG.MagAmmo[SG.CurrentWeapon];
	SG.FireCooldownTimer = Def.FireInterval;
	NoteViolence(Shooter);

	// Gunfire: one bullet, one victim, flat per-gun damage (pistol 3 / smg 4 /
	// rifle 2 / sniper 1 hits to 100 health). Witnesses run.
	int32 Kills = 0;
	if (!PedestrianSystem.IsValid())
	{
		for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
		{
			PedestrianSystem = *It;
			break;
		}
	}
	if (PedestrianSystem.IsValid())
	{
		PedestrianSystem->FleeFrom(Impact, 1500.f);
		int32 ShotKills = 0;
		PedestrianSystem->GunHit(CameraLocation, Impact, 90.f, Def.DamagePerHit, ShotKills);
		Kills = ShotKills;
	}

	int32 Startled = 0;
	if (!TrafficSystem.IsValid())
	{
		for (TActorIterator<ACitixTrafficSystem> It(GetWorld()); It; ++It)
		{
			TrafficSystem = *It;
			break;
		}
	}
	if (TrafficSystem.IsValid())
	{
		Startled = TrafficSystem->StartleVehicles(Impact, 1200.f, 2.f);
	}

	// Hitting a police car damages it (same hits-to-kill as pedestrians);
	// a disabled unit drops out of the chase with its lightbar dark.
	bool bHitPolice = false;
	for (FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.Car && !Unit.bDisabled
			&& FVector::Dist2D(Unit.Car->GetActorLocation(), Impact) < 500.f)
		{
			bHitPolice = true;
			Unit.Health -= Def.DamagePerHit;
			if (Unit.Health <= 0.f && !Unit.bDisabled)
			{
				Unit.bDisabled = true;
				Unit.Car->SetFlashEnabled(false);
				FireSparks(Unit.Car->GetActorLocation() + FVector(0.f, 0.f, 120.f),
					ECitixSurface::EmissiveWarm, 6);
				ShowToast(TEXT("Police unit disabled!"), 2.5f);
				UE_LOG(LogCitix, Log, TEXT("[Citix] Police unit disabled by gunfire."));
			}
			break;
		}
	}

	if (Kills > 0)
	{
		// Counted again by the kill watcher for the wanted level; the toast lands once.
		ShowToast(TEXT("Pedestrian down!"), 2.f);
		FireSparks(Impact, ECitixSurface::EmissiveWarm, 4);
	}

	// Dismounted officers die like pedestrians (same per-gun hits-to-kill).
	for (FCitixPoliceOfficerState& State : Officers)
	{
		if (State.Officer && !State.bDying
			&& FVector::Dist2D(State.Officer->GetActorLocation(), Impact) < 350.f)
		{
			State.Health -= Def.DamagePerHit;
			if (State.Health <= 0.f)
			{
				State.bDying = true;
				if (State.Officer)
				{
					State.Officer->bDead = true;
				}
				AddHeat(SG, 35.f, TEXT("officer down"));
				FireSparks(State.Officer->GetActorLocation() + FVector(0.f, 0.f, 120.f),
					ECitixSurface::EmissiveWarm, 4);
				ShowToast(TEXT("Officer down!"), 2.5f);
				UE_LOG(LogCitix, Log, TEXT("[Citix] Officer gunned down."));
			}
			break;
		}
	}
	if (bHitPolice)
	{
		AddHeat(SG, 20.f, TEXT("police targeted"));
		ShowToast(TEXT("Police targeted!"), 2.f);
	}
	else
	{
		const bool bPublic = Kills > 0 || Startled > 0
			|| (TrafficSystem.IsValid() && TrafficSystem->CountAgentsNear(Impact, 4000.f) > 0);
		AddHeat(SG, bPublic ? 6.f : 2.f, TEXT("gunfire"));
	}

	// Players hit players: same per-gun damage as pedestrians. The trace hit
	// actor decides (drivers are safe inside their cars: the trace stops at
	// the body, which is not an on-foot pawn).
	AController* DirectVictimPC = nullptr;
	if (Hit.GetActor() && Hit.GetActor() != PlayerPawn)
	{
		if (ACitixOnFootPawn* VictimPawn = Cast<ACitixOnFootPawn>(Hit.GetActor()))
		{
			if (AController* VictimPC = VictimPawn->GetController())
			{
				if (VictimPC != Shooter)
				{
					DirectVictimPC = VictimPC;
				}
			}
		}
	}

	// Proximity fallback (same model as pedestrian GunHit): capsules often do
	// not block the visibility trace, so the shot passes through and the
	// impact lands behind the victim. Measure against the RAY SEGMENT, not the
	// impact point: an unblocked shot ends at max range, metres past its victim.
	// One victim per shot: direct hit wins, otherwise the closest in radius.
	AController* ProximityVictimPC = nullptr;
	float BestDist = 150.f;
	const FVector RayEnd = Impact;
	const FVector RayDir = (RayEnd - CameraLocation).GetSafeNormal();
	const float RayLength = FVector::Dist(CameraLocation, RayEnd);
	for (TActorIterator<ACitixOnFootPawn> VictimIt(GetWorld()); VictimIt; ++VictimIt)
	{
		ACitixOnFootPawn* VictimPawn = *VictimIt;
		if (!VictimPawn || VictimPawn == PlayerPawn)
		{
			continue;
		}
		AController* VictimPC = VictimPawn->GetController();
		if (!VictimPC || VictimPC == Shooter)
		{
			continue;
		}
		ACitixPlayerState* VictimPS = VictimPC->PlayerState
			? Cast<ACitixPlayerState>(VictimPC->PlayerState) : nullptr;
		if (!VictimPS || VictimPS->Health <= 0.f)
		{
			continue;
		}
		const FVector Chest = VictimPawn->GetActorLocation() + FVector(0.f, 0.f, 100.f);
		const float Along = FMath::Clamp(FVector::DotProduct(Chest - CameraLocation, RayDir),
			0.f, RayLength);
		const float Dist = FVector::Dist(Chest, CameraLocation + RayDir * Along);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			ProximityVictimPC = VictimPC;
		}
	}

	if (AController* VictimPC = DirectVictimPC ? DirectVictimPC : ProximityVictimPC)
	{
		FString ShooterName = Shooter && Shooter->PlayerState
			? Shooter->PlayerState->GetPlayerName() : FString(TEXT("Someone"));
		DamagePlayer(VictimPC, Def.DamagePerHit, Shooter, ShooterName, TEXT("shot"));
	}

	// Shot event feed: remotes play their local pools from this serial.
	if (Shooter && Shooter->PlayerState)
	{
		if (ACitixPlayerState* ShooterPS = Cast<ACitixPlayerState>(Shooter->PlayerState))
		{
			ShooterPS->ShotMuzzle = Muzzle;
			ShooterPS->ShotImpact = Impact;
			ShooterPS->ShotSurface = static_cast<uint8>(TracerSurface);
			++ShooterPS->ShotSerial;
		}
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] Fired %s: kills=%d startled=%d mag=%d/%d."),
		*Def.Name, Kills, Startled, SG.MagAmmo[SG.CurrentWeapon], SG.ReserveAmmo[SG.CurrentWeapon]);
	return true;
}

void ACitixSandboxDirector::PlayShotVisual(const FVector& Muzzle, const FVector& Impact,
	ECitixSurface Surface)
{
	for (ACitixPulseBolt* Bolt : TracerBolts)
	{
		if (Bolt && Bolt->IsIdle())
		{
			Bolt->Fire(Muzzle, Impact, Surface);
			break;
		}
	}
	FireSparks(Impact, Surface, 3);
}

void ACitixSandboxDirector::GrantStarterPistol()
{
	GrantStarterPistol(FocusPC());
}

void ACitixSandboxDirector::GrantStarterPistol(AController* PC)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!bConfigured || GetWeaponCount() < 1 || !Game)
	{
		return;
	}
	if (Game->bOwnedWeapons[0])
	{
		if (Game->CurrentWeapon < 0)
		{
			Game->CurrentWeapon = 0;
		}
		return;
	}
	const FCitixWeaponDef& Def = CitixWeaponCatalogue()[0];
	Game->bOwnedWeapons[0] = true;
	Game->MagAmmo[0] = Def.MagSize;
	Game->ReserveAmmo[0] = Def.MagSize * 2;
	Game->CurrentWeapon = 0;
	UE_LOG(LogCitix, Log, TEXT("[Citix] Starter pistol granted."));
}

void ACitixSandboxDirector::DamagePlayer(AController* Victim, float Amount, AController* KillerPC,
	const FString& KillerName, const TCHAR* Cause)
{
	if (!Victim || !Victim->PlayerState || Amount <= 0.f)
	{
		return;
	}
	ACitixPlayerState* PS = Cast<ACitixPlayerState>(Victim->PlayerState);
	if (!PS || PS->Health <= 0.f)
	{
		return;
	}
	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (FCitixPlayerGame* VictimGame = FindPlayerGame(Victim))
	{
		if (Now < VictimGame->SpawnProtectedUntil)
		{
			return;
		}
	}
	PS->Health = FMath::Max(0.f, PS->Health - Amount);
	PS->LastDamageTime = Now;
	PS->ForceNetUpdate();

	UE_LOG(LogCitix, Log, TEXT("[Citix] %s took %.0f damage (%s), health %.0f."),
		*PS->GetPlayerName(), Amount, Cause, PS->Health);

	if (PS->Health > 0.f)
	{
		return;
	}

	// Death: killfeed, heat on the killer's record, respawn. No score/money
	// penalty (session-only).
	const FString VictimName = PS->GetPlayerName();
	ShowToast(FString::Printf(TEXT("%s killed %s"), *KillerName, *VictimName), 3.f);
	if (KillerPC)
	{
		if (FCitixPlayerGame* KillerGame = FindPlayerGame(KillerPC))
		{
			AddHeat(*KillerGame, 15.f, TEXT("player down"));
		}
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] %s killed %s (%s)."), *KillerName, *VictimName, Cause);

	FVector DeathLocation = GetActorLocation();
	if (Victim->GetPawn())
	{
		DeathLocation = Victim->GetPawn()->GetActorLocation();
	}
	// Death closes the victim's pursuit (other suspects keep theirs).
	ReleasePoliceUnits(Victim);
	// Death interrupts paid work and racing (rank 10: clean, no reward).
	if (FCitixPlayerGame* VictimGame = FindPlayerGame(Victim))
	{
		FailObjectivesOnDeath(*VictimGame);
	}
	LeaveRace(Victim, true);
	RespawnPlayer(Victim, DeathLocation);
}

void ACitixSandboxDirector::RespawnPlayer(AController* PC, const FVector& DeathLocation)
{
	UWorld* World = GetWorld();
	if (!World || !PC)
	{
		return;
	}
	if (APawn* OldPawn = PC->GetPawn())
	{
		OldPawn->Destroy();
	}

	// Random ring 30-100 m around the death spot, settled on the ground with
	// headroom. A few attempts: a failed pick falls back to the death spot
	// (AlwaysSpawn cannot strand the player).
	FVector Spot = DeathLocation + FVector(0.f, 0.f, 200.f);
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		const float Distance = Rng.FRandRange(3000.f, 10000.f);
		const FVector Candidate = DeathLocation + FVector(
			FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		FHitResult GroundHit;
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(CitixRespawnTrace), false);
		if (!World->LineTraceSingleByChannel(GroundHit, Candidate + FVector(0.f, 0.f, 1500.f),
			Candidate - FVector(0.f, 0.f, 3000.f), ECC_WorldStatic, GroundParams))
		{
			continue;
		}
		FHitResult HeadHit;
		if (World->LineTraceSingleByChannel(HeadHit,
			FVector(Candidate.X, Candidate.Y, GroundHit.Location.Z + 100.f),
			FVector(Candidate.X, Candidate.Y, GroundHit.Location.Z + 250.f),
			ECC_WorldStatic, GroundParams))
		{
			continue;
		}
		Spot = FVector(Candidate.X, Candidate.Y, GroundHit.Location.Z + 100.f);
		break;
	}

	ACitixOnFootPawn* NewPawn = World->SpawnActorDeferred<ACitixOnFootPawn>(
		ACitixOnFootPawn::StaticClass(), FTransform(FRotator::ZeroRotator, Spot),
		PC, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewPawn)
	{
		return;
	}
	const int32 JoinSeed = PC->PlayerState ? PC->PlayerState->GetPlayerId() + 1 : 1;
	NewPawn->Style = ECitixCharacterStyle::Casual;
	NewPawn->AppearanceSeed = JoinSeed * 100003;
	NewPawn->FinishSpawning(FTransform(FRotator::ZeroRotator, Spot));
	PC->Possess(NewPawn);

	// Full health on respawn, plus brief spawn protection.
	if (ACitixPlayerState* PS = Cast<ACitixPlayerState>(PC->PlayerState))
	{
		PS->Health = PS->MaxHealth;
		PS->LastDamageTime = World->GetTimeSeconds();
		PS->ForceNetUpdate();
	}
	if (FCitixPlayerGame* Game = FindPlayerGame(PC))
	{
		Game->SpawnProtectedUntil = World->GetTimeSeconds() + 2.f;
	}

	// Blue sky beam marks the spawn (replicated, auto-expires).
	FActorSpawnParameters BeamParams;
	BeamParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACitixDestinationBeacon* Beam = World->SpawnActor<ACitixDestinationBeacon>(
		ACitixDestinationBeacon::StaticClass(), FTransform(FRotator::ZeroRotator, Spot), BeamParams))
	{
		Beam->ShowRespawnBeam(Spot);
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] %s respawned."),
		PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("Someone"));
}

// ---------------------------------------------------------------------------
// Competitive race (rank 7): one server-validated loop, solo time-trial out
// of scope. Countdown needs 2+ drivers; checkpoints are ordered road nodes;
// resets keep progress (no skipping possible); death/disconnect/leave = DNF.
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::FailObjectivesOnDeath(FCitixPlayerGame& Game)
{
	// Death interrupts paid work: fail the job/activity cleanly, no reward.
	// (Rank 10 decision: the route is broken by respawn anyway.)
	if (Game.JobState == ECitixJobState::Active)
	{
		Game.JobState = ECitixJobState::Failed;
		Game.JobRewardMult = 1.f;
		Game.bHasWaypoint = false;
		Game.RoutePoints.Reset();
		ShowToast(Game, TEXT("Delivery failed - you died"));
	}
	if (Game.ActivityState == ECitixActivityState::Active)
	{
		Game.ActivityState = ECitixActivityState::Failed;
		Game.bHasWaypoint = false;
		Game.RoutePoints.Reset();
		ShowToast(Game, TEXT("Activity failed - you died"));
	}
}

bool ACitixSandboxDirector::IsRacing(AController* PC) const
{
	if (!PC)
	{
		return false;
	}
	for (const FRaceRacer& Racer : RaceRacers)
	{
		if (Racer.PC.Get() == PC && !Racer.bDNF && !Racer.bFinished)
		{
			return true;
		}
	}
	return false;
}

bool ACitixSandboxDirector::JoinRace(AController* PC)
{
	if (!bConfigured || !PC)
	{
		return false;
	}
	RaceRacers.RemoveAll([](const FRaceRacer& Racer) { return !Racer.PC.IsValid(); });
	for (const FRaceRacer& Racer : RaceRacers)
	{
		if (Racer.PC.Get() == PC)
		{
			// Toggle: joined drivers leave (DNF while racing).
			LeaveRace(PC);
			return true;
		}
	}
	if (RaceState == ECitixRaceState::Countdown || RaceState == ECitixRaceState::Racing)
	{
		if (FCitixPlayerGame* Game = FindPlayerGame(PC))
		{
			ShowToast(*Game, TEXT("Race already running - wait for the next one"));
		}
		return false;
	}
	if (RaceState == ECitixRaceState::Finished)
	{
		return false;
	}
	FRaceRacer Racer;
	Racer.PC = PC;
	RaceRacers.Add(Racer);
	if (FCitixPlayerGame* Game = FindPlayerGame(PC))
	{
		ShowToast(*Game, FString::Printf(TEXT("Race joined (%d) - T to leave"), RaceRacers.Num()));
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Race joined (%d racers)."), RaceRacers.Num());

	if (RaceRacers.Num() >= 2 && RaceState == ECitixRaceState::Idle)
	{
		// Build checkpoints from the joiner's surroundings: 4 ordered road
		// nodes in growing distance bands (0.6/1.2/1.8/2.4 km).
		APawn* AnchorPawn = PC->GetPawn();
		const FVector Anchor = AnchorPawn ? AnchorPawn->GetActorLocation() : GetActorLocation();
		RaceCheckpoints.Reset();
		for (int32 Band = 0; Band < 4; ++Band)
		{
			const float Want = 60000.f * static_cast<float>(Band + 1);
			int32 Best = INDEX_NONE;
			float BestScore = TNumericLimits<float>::Max();
			for (int32 Attempt = 0; Attempt < 40; ++Attempt)
			{
				const int32 Candidate = Rng.RandRange(0, Network.Nodes.Num() - 1);
				if (!Network.IsValidNode(Candidate))
				{
					continue;
				}
				const FVector2D& P = Network.Nodes[Candidate].Position;
				const float NodeScore = FMath::Abs(FVector2D::Distance(
					P, FVector2D(Anchor.X, Anchor.Y)) - Want);
				if (NodeScore < BestScore)
				{
					BestScore = NodeScore;
					Best = Candidate;
				}
			}
			if (Best != INDEX_NONE)
			{
				const FVector2D& P = Network.Nodes[Best].Position;
				RaceCheckpoints.Add(FVector(P.X, P.Y, 60.f));
			}
		}
		if (RaceCheckpoints.Num() < 2)
		{
			RaceRacers.Reset();
			return false;
		}
		RaceState = ECitixRaceState::Countdown;
		RaceCountdown = 3.2f;
		LastCountdownSecond = -1;
		ShowToast(FString::Printf(TEXT("RACE STARTING - %d drivers"), RaceRacers.Num()), 3.f);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Race countdown (%d checkpoints)."), RaceCheckpoints.Num());
	}
	return true;
}

void ACitixSandboxDirector::LeaveRace(AController* PC, bool bSilent)
{
	if (!PC)
	{
		return;
	}
	for (FRaceRacer& Racer : RaceRacers)
	{
		if (Racer.PC.Get() == PC && !Racer.bFinished && !Racer.bDNF)
		{
			Racer.bDNF = true;
			if (!bSilent)
			{
				if (FCitixPlayerGame* Game = FindPlayerGame(PC))
				{
					ShowToast(*Game, TEXT("Race DNF"));
				}
				UE_LOG(LogCitix, Log, TEXT("[Citix] Race DNF."));
			}
		}
	}
	RaceRacers.RemoveAll([](const FRaceRacer& Racer) { return !Racer.PC.IsValid() || Racer.bDNF; });
	if (RaceState == ECitixRaceState::Countdown && RaceRacers.Num() < 2)
	{
		RaceState = ECitixRaceState::Idle;
		RaceCheckpoints.Reset();
		ShowToast(TEXT("Race cancelled - need 2 drivers"), 3.f);
	}
}

void ACitixSandboxDirector::UpdateRace(float DeltaSeconds)
{
	RaceRacers.RemoveAll([](const FRaceRacer& Racer) { return !Racer.PC.IsValid(); });
	if (RaceState == ECitixRaceState::Idle || RaceState == ECitixRaceState::Finished)
	{
		if (RaceState == ECitixRaceState::Finished)
		{
			RaceEndTimer -= DeltaSeconds;
			if (RaceEndTimer <= 0.f)
			{
				RaceState = ECitixRaceState::Idle;
				RaceRacers.Reset();
				RaceCheckpoints.Reset();
				RaceFinishCount = 0;
			}
		}
		PublishRaceSnapshots();
		return;
	}

	if (RaceState == ECitixRaceState::Countdown)
	{
		RaceCountdown -= DeltaSeconds;
		const int32 Second = FMath::Max(0, FMath::CeilToInt(RaceCountdown));
		if (Second != LastCountdownSecond && Second <= 3)
		{
			LastCountdownSecond = Second;
			ShowToast(Second > 0 ? FString::Printf(TEXT("%d"), Second) : TEXT("GO!"), 1.2f);
		}
		if (RaceCountdown <= 0.f)
		{
			RaceState = ECitixRaceState::Racing;
			RaceElapsed = 0.f;
		}
		PublishRaceSnapshots();
		return;
	}

	// Racing: ordered checkpoint validation per racer (25 m radius, 2D).
	RaceElapsed += DeltaSeconds;
	for (FRaceRacer& Racer : RaceRacers)
	{
		AController* PC = Racer.PC.Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!PC || !Pawn || Racer.bFinished || Racer.bDNF)
		{
			continue;
		}
		if (!RaceCheckpoints.IsValidIndex(Racer.CheckpointsDone))
		{
			continue;
		}
		if (FVector::Dist2D(Pawn->GetActorLocation(), RaceCheckpoints[Racer.CheckpointsDone]) < 2500.f)
		{
			++Racer.CheckpointsDone;
			if (Racer.CheckpointsDone >= RaceCheckpoints.Num())
			{
				Racer.bFinished = true;
				Racer.FinishTime = RaceElapsed;
				Racer.FinishPlace = ++RaceFinishCount;
				// One-time placement rewards (score + funds, personal records).
				static const int32 ScoreReward[3] = { 300, 150, 100 };
				static const int32 MoneyReward[3] = { 200, 100, 50 };
				const int32 Slot = FMath::Clamp(Racer.FinishPlace - 1, 0, 2);
				if (FCitixPlayerGame* Game = FindPlayerGame(PC))
				{
					AddScore(*Game, ScoreReward[Slot], TEXT("race"));
					AddMoney(*Game, MoneyReward[Slot], TEXT("race"));
					ShowCard(*Game, FString::Printf(TEXT("P%d FINISH"), Racer.FinishPlace),
						FString::Printf(TEXT("%s  +%d  +$%d"),
							*FormatRaceTime(Racer.FinishTime), ScoreReward[Slot], MoneyReward[Slot]));
				}
				UE_LOG(LogCitix, Log, TEXT("[Citix] Race P%d finished in %s."),
					Racer.FinishPlace, *FormatRaceTime(Racer.FinishTime));
			}
		}
	}

	bool bAllDone = true;
	for (const FRaceRacer& Racer : RaceRacers)
	{
		if (!Racer.bFinished && !Racer.bDNF)
		{
			bAllDone = false;
			break;
		}
	}
	if (RaceElapsed > RaceTimeout)
	{
		for (FRaceRacer& Racer : RaceRacers)
		{
			if (!Racer.bFinished && !Racer.bDNF)
			{
				Racer.bDNF = true;
				if (FCitixPlayerGame* Game = FindPlayerGame(Racer.PC.Get()))
				{
					ShowToast(*Game, TEXT("Race DNF - out of time"));
				}
			}
		}
		bAllDone = true;
	}
	if (bAllDone && RaceRacers.Num() > 0)
	{
		RaceState = ECitixRaceState::Finished;
		RaceEndTimer = 5.f;
		ShowToast(TEXT("Race complete - results posted"), 4.f);
	}
	PublishRaceSnapshots();
}

FString ACitixSandboxDirector::FormatRaceTime(float Seconds)
{
	const int32 Minutes = FMath::FloorToInt(Seconds / 60.f);
	const int32 Secs = FMath::FloorToInt(Seconds - Minutes * 60.f);
	return FString::Printf(TEXT("%d:%02d"), Minutes, Secs);
}

bool ACitixSandboxDirector::TestTeleportRacerToCheckpoint(AController* PC)
{
	if (!PC)
	{
		return false;
	}
	for (FRaceRacer& Racer : RaceRacers)
	{
		if (Racer.PC.Get() == PC && !Racer.bFinished && !Racer.bDNF)
		{
			APawn* Pawn = PC->GetPawn();
			if (!Pawn || !RaceCheckpoints.IsValidIndex(Racer.CheckpointsDone))
			{
				return false;
			}
			Pawn->SetActorLocation(RaceCheckpoints[Racer.CheckpointsDone] + FVector(0.f, 0.f, 100.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			return true;
		}
	}
	return false;
}

void ACitixSandboxDirector::PublishRaceSnapshots()
{
	// Live positions: most checkpoints, then closest to next.
	TArray<FRaceRacer*> Ordered;
	for (FRaceRacer& Racer : RaceRacers)
	{
		Ordered.Add(&Racer);
	}
	Ordered.Sort([](const FRaceRacer& A, const FRaceRacer& B)
	{
		if (A.bFinished != B.bFinished)
		{
			return A.bFinished > B.bFinished;
		}
		if (A.CheckpointsDone != B.CheckpointsDone)
		{
			return A.CheckpointsDone > B.CheckpointsDone;
		}
		return A.FinishTime < B.FinishTime;
	});
	for (FRaceRacer& Racer : RaceRacers)
	{
		AController* PC = Racer.PC.Get();
		if (!PC || !PC->PlayerState)
		{
			continue;
		}
		ACitixPlayerState* PS = Cast<ACitixPlayerState>(PC->PlayerState);
		if (!PS)
		{
			continue;
		}
		PS->RaceState = static_cast<uint8>(RaceState);
		PS->RaceCountdown = FMath::Max(0.f, RaceCountdown);
		PS->RaceCheckpointsDone = Racer.CheckpointsDone;
		PS->RaceCheckpointsTotal = RaceCheckpoints.Num();
		PS->RacePosition = Racer.bFinished ? Racer.FinishPlace : Ordered.Find(&Racer) + 1;
		PS->RaceTime = Racer.bFinished ? Racer.FinishTime : RaceElapsed;
		PS->RaceCheckpoint = (RaceState == ECitixRaceState::Racing
			&& RaceCheckpoints.IsValidIndex(Racer.CheckpointsDone))
			? RaceCheckpoints[Racer.CheckpointsDone] : FVector::ZeroVector;
	}
}

void ACitixSandboxDirector::UpdatePlayerHealth(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->PlayerState)
		{
			continue;
		}
		if (ACitixPlayerState* PS = Cast<ACitixPlayerState>(PC->PlayerState))
		{
			if (PS->Health > 0.f && PS->Health < PS->MaxHealth
				&& Now - PS->LastDamageTime > 5.f)
			{
				PS->Health = FMath::Min(PS->MaxHealth, PS->Health + 6.f * DeltaSeconds);
			}
		}
	}
}

void ACitixSandboxDirector::UpdateWeapon(FCitixPlayerGame& Game, AController* PC,
	float DeltaSeconds, const AActor* PlayerPawn)
{
	Game.FireCooldownTimer = FMath::Max(0.f, Game.FireCooldownTimer - DeltaSeconds);
	Game.NoWeaponToastCooldown = FMath::Max(0.f, Game.NoWeaponToastCooldown - DeltaSeconds);

	// Reload progress: move reserve into the mag once.
	if (Game.ReloadTimer > 0.f)
	{
		Game.ReloadTimer = FMath::Max(0.f, Game.ReloadTimer - DeltaSeconds);
		if (Game.ReloadTimer <= 0.f && Game.CurrentWeapon >= 0)
		{
			const FCitixWeaponDef& Def = CitixWeaponCatalogue()[Game.CurrentWeapon];
			const int32 Need = Def.MagSize - Game.MagAmmo[Game.CurrentWeapon];
			const int32 Take = FMath::Min(Need, Game.ReserveAmmo[Game.CurrentWeapon]);
			Game.MagAmmo[Game.CurrentWeapon] += Take;
			Game.ReserveAmmo[Game.CurrentWeapon] -= Take;
		}
	}

	// Held trigger: autos keep firing, semis need a fresh press. Remote
	// clients resend while held (fresh aim each RPC); this path covers
	// standalone and the listen host's local trigger only.
	if (Game.bTriggerHeld && Game.CurrentWeapon >= 0 && Game.ReloadTimer <= 0.f)
	{
		const FCitixWeaponDef& Def = CitixWeaponCatalogue()[Game.CurrentWeapon];
		if (Def.bFullAuto || Game.bTriggerFresh)
		{
			Game.bTriggerFresh = false;
			FireWeapon(PC);
		}
	}

	// ADS blend (fast in, slightly slower out).
	const float AdsRate = Game.bAdsHeld ? 8.f : 5.f;
	Game.AdsAmount = FMath::Clamp(Game.AdsAmount
		+ (Game.bAdsHeld ? DeltaSeconds : -DeltaSeconds) * AdsRate, 0.f, 1.f);

	UpdateWeaponCamera(Game, DeltaSeconds, PlayerPawn);

	// While aiming, the on-foot body stays glued to the camera yaw.
	if (Game.AdsAmount > 0.5f && Cast<ACitixOnFootPawn>(PlayerPawn))
	{
		if (APlayerController* ViewPC = Cast<APlayerController>(PC))
		{
			FVector CameraLocation;
			FRotator CameraRotation;
			ViewPC->GetPlayerViewPoint(CameraLocation, CameraRotation);
			APawn* Pawn = const_cast<APawn*>(Cast<APawn>(PlayerPawn));
			const FRotator PawnRotation = Pawn->GetActorRotation();
			Pawn->SetActorRotation(FRotator(PawnRotation.Pitch, CameraRotation.Yaw, PawnRotation.Roll));
		}
	}

	// Visible gun follows the on-foot player; hidden in cars and when unarmed.
	// It takes the camera pitch so the barrel tracks the crosshair.
	const bool bOnFoot = Cast<ACitixOnFootPawn>(PlayerPawn) != nullptr;
	float AimPitch = 0.f;
	if (bOnFoot)
	{
		if (APlayerController* ViewPC = Cast<APlayerController>(PC))
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			ViewPC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			AimPitch = ViewRotation.Pitch;
		}
	}
	if (WeaponProp)
	{
		if (bOnFoot && Game.CurrentWeapon >= 0)
		{
			WeaponProp->SetWeaponKind(Game.CurrentWeapon);
			WeaponProp->SetAdsAmount(Game.AdsAmount);
			WeaponProp->SetAimPitch(AimPitch);
			WeaponProp->FollowPawn(const_cast<APawn*>(Cast<APawn>(PlayerPawn)));
		}
		else
		{
			WeaponProp->FollowPawn(nullptr);
		}
	}
}

void ACitixSandboxDirector::UpdateWeaponCamera(FCitixPlayerGame& Game,
	float DeltaSeconds, const AActor* PlayerPawn)
{
	APawn* Pawn = const_cast<APawn*>(Cast<APawn>(PlayerPawn));
	if (Pawn != AdsPawn.Get())
	{
		// Pawn changed (enter/exit vehicle): re-cache every camera, restore old FOVs.
		for (const FCachedCamera& Cached : AdsCameras)
		{
			if (Cached.Component.IsValid())
			{
				Cached.Component->SetFieldOfView(Cached.BaseFov);
			}
		}
		AdsCameras.Reset();
		AdsPawn = Pawn;
		if (Pawn)
		{
			for (UActorComponent* Component : Pawn->GetComponents())
			{
				if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
				{
					FCachedCamera Cached;
					Cached.Component = Camera;
					Cached.BaseFov = Camera->FieldOfView;
					AdsCameras.Add(Cached);
				}
			}
		}
	}

	if (AdsCameras.Num() == 0 || Game.CurrentWeapon < 0)
	{
		return;
	}
	const float Zoom = CitixWeaponCatalogue()[Game.CurrentWeapon].AdsZoom;
	for (const FCachedCamera& Cached : AdsCameras)
	{
		if (Cached.Component.IsValid())
		{
			const float Target = Cached.BaseFov * FMath::Lerp(1.f, Zoom, Game.AdsAmount);
			Cached.Component->SetFieldOfView(FMath::FInterpTo(
				Cached.Component->FieldOfView, Target, DeltaSeconds, 12.f));
		}
	}
}

// ---------------------------------------------------------------------------
// Shops, money, daily contract
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::AddMoney(int32 Amount, const TCHAR* Reason)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		AddMoney(*Game, Amount, Reason);
	}
}

bool ACitixSandboxDirector::CanPromptShop(FString& OutPrompt) const
{
	OutPrompt.Reset();
	if (!bConfigured || Shops.Num() == 0)
	{
		return false;
	}
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	return CanPromptShop(PlayerLocation, OutPrompt);
}

bool ACitixSandboxDirector::CanPromptShop(const FVector& PlayerLocation, FString& OutPrompt) const
{
	OutPrompt.Reset();
	if (!bConfigured || Shops.Num() == 0)
	{
		return false;
	}
	for (const FCitixWeaponShop& Shop : Shops)
	{
		if (FVector::DistSquared(Shop.Location, PlayerLocation) < FMath::Square(ShopInteractRadius * 2.f))
		{
			OutPrompt = FString::Printf(TEXT("TALK: %s"), *Shop.Name);
			return true;
		}
	}
	return false;
}

bool ACitixSandboxDirector::BuyWeapon(AController* PC, int32 Index)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	if (!Game || Index < 0 || Index >= GetWeaponCount())
	{
		return false;
	}
	const FCitixWeaponDef& Def = CitixWeaponCatalogue()[Index];
	if (Game->bOwnedWeapons[Index])
	{
		Game->CurrentWeapon = Index;
		return true;
	}
	if (Game->Money < Def.Price)
	{
		return false;
	}
	Game->Money -= Def.Price;
	Game->bOwnedWeapons[Index] = true;
	Game->MagAmmo[Index] = Def.MagSize;
	Game->ReserveAmmo[Index] = Def.MaxReserve;
	Game->CurrentWeapon = Index;
	UE_LOG(LogCitix, Log, TEXT("[Citix] Bought %s for $%d (funds $%d)."), *Def.Name, Def.Price, Game->Money);
	return true;
}

bool ACitixSandboxDirector::BuySelected(AController* PC, int32 ShopIdx, int32 Row)
{
	FCitixPlayerGame* Game = FindPlayerGame(PC);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Game || !Pawn || !Shops.IsValidIndex(ShopIdx))
	{
		return false;
	}
	// Server-side validation: buyer must be alive, near the employee, and the
	// row must be a real catalogue entry. Denials toast the buyer only.
	if (FVector::DistSquared(Shops[ShopIdx].Location, Pawn->GetActorLocation())
		> FMath::Square(ShopInteractRadius * 3.f))
	{
		ShowToast(*Game, TEXT("Too far from the counter"));
		return false;
	}
	// Rows 0-3: guns. Row 4: reserve refill for the current gun.
	if (Row >= 0 && Row <= 3)
	{
		const FCitixWeaponDef& Def = CitixWeaponCatalogue()[Row];
		const bool bWasOwned = Game->bOwnedWeapons[Row];
		if (BuyWeapon(PC, Row))
		{
			ShowToast(*Game, bWasOwned
				? FString::Printf(TEXT("Equipped %s"), *Def.Name)
				: FString::Printf(TEXT("Bought %s  (-$%d)"), *Def.Name, Def.Price));
			return true;
		}
		ShowToast(*Game, FString::Printf(TEXT("Need $%d for %s"), Def.Price, *Def.Name));
		return false;
	}

	if (Game->CurrentWeapon < 0)
	{
		ShowToast(*Game, TEXT("Buy a weapon first"));
		return false;
	}
	const FCitixWeaponDef& Def = CitixWeaponCatalogue()[Game->CurrentWeapon];
	if (Game->ReserveAmmo[Game->CurrentWeapon] >= Def.MaxReserve)
	{
		ShowToast(*Game, TEXT("Ammo already full"));
		return true;
	}
	if (Game->Money < Def.AmmoPrice)
	{
		ShowToast(*Game, FString::Printf(TEXT("Need $%d for ammo"), Def.AmmoPrice));
		return false;
	}
	Game->Money -= Def.AmmoPrice;
	Game->ReserveAmmo[Game->CurrentWeapon] = Def.MaxReserve;
	ShowToast(*Game, FString::Printf(TEXT("Ammo refilled  (-$%d)"), Def.AmmoPrice));
	UE_LOG(LogCitix, Log, TEXT("[Citix] Ammo refilled for %s (funds $%d)."), *Def.Name, Game->Money);
	return true;
}

void ACitixSandboxDirector::PoliceFireAtPlayer(FCitixPoliceUnit& Unit, APawn* TargetPawn, AController* TargetPC)
{
	if (!Unit.Car || !TargetPawn || Unit.bDisabled)
	{
		return;
	}

	const FVector Muzzle = Unit.Car->GetActorLocation() + FVector(0.f, 0.f, 150.f);
	const FVector Target = TargetPawn->GetActorLocation() + FVector(0.f, 0.f, 100.f);

	// Star-paced aim from the suspect's record: 1 star sprays wide, 3 groups.
	int32 SuspectStars = 1;
	if (FCitixPlayerGame* TargetGame = FindPlayerGame(TargetPC))
	{
		SuspectStars = FMath::Max(1, TargetGame->WantedStars);
	}
	const float SpreadDeg = 7.f - 1.5f * static_cast<float>(FMath::Clamp(SuspectStars, 1, 3));
	FRotator Aim = (Target - Muzzle).Rotation();
	Aim.Yaw += Rng.FRandRange(-SpreadDeg, SpreadDeg);
	Aim.Pitch += Rng.FRandRange(-SpreadDeg, SpreadDeg);
	const FVector Direction = Aim.Vector();
	const FVector End = Muzzle + Direction * 5000.f;

	for (ACitixPulseBolt* Bolt : TracerBolts)
	{
		if (Bolt && Bolt->IsIdle())
		{
			Bolt->Fire(Muzzle, End, ECitixSurface::EmissiveWarm);
			break;
		}
	}

	const bool bOnFoot = Cast<ACitixOnFootPawn>(TargetPawn) != nullptr;
	const FVector ToTarget = Target - Muzzle;
	const float Along = FMath::Clamp(FVector::DotProduct(ToTarget, Direction), 0.f, 5000.f);
	const float Miss = (Muzzle + Direction * Along - Target).Size();
	if (Miss < (bOnFoot ? 120.f : 220.f))
	{
		if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(TargetPawn))
		{
			Vehicle->ApplyVehicleDamage(10.f);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Police hit the player's car."));
		}
		else
		{
			// Caught out in the open under fire: the net tightens and it bleeds.
			Unit.CatchTimer = FMath::Min(Unit.CatchTimer + 0.5f, 2.9f);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Police clipped the player on foot."));
			if (TargetPC)
			{
				FString UnitName = TEXT("Police");
				DamagePlayer(TargetPC, 10.f, nullptr, UnitName, TEXT("shot"));
			}
		}
	}
}

void ACitixSandboxDirector::UpdateQuestDay(FCitixPlayerGame& Game)
{
	const ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(GetWorld());
	if (!TimeOfDay)
	{
		return;
	}
	const float Hour = TimeOfDay->GetHours();
	if (Game.LastHour >= 0.f && Hour < Game.LastHour - 12.f)
	{
		// The clock wrapped past midnight: a new in-game day, a new contract.
		Game.bQuestOffered = true;
		Game.bQuestAccepted = false;
		ShowToast(Game, TEXT("PRIORITY CONTRACT available - press J (3x pay)"));
		UE_LOG(LogCitix, Log, TEXT("[Citix] New day: priority contract offered."));
	}
	Game.LastHour = Hour;
}

void ACitixSandboxDirector::UpdateKillWatch()
{
	if (!PedestrianSystem.IsValid())
	{
		for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
		{
			PedestrianSystem = *It;
			break;
		}
	}
	if (!PedestrianSystem.IsValid())
	{
		return;
	}
	const int32 Deaths = PedestrianSystem->GetDeathCount();
	const int32 NewDeaths = Deaths - LastDeathCount;
	LastDeathCount = Deaths;
	if (NewDeaths > 0)
	{
		// Attribute to the recent shooter when the shots just landed (gunfire,
		// ram or blast within the window); otherwise the heat still lands but
		// on the focus record (traffic, falls, stale kills).
		AController* Killer = nullptr;
		UWorld* World = GetWorld();
		if (LastShooterPC.IsValid() && World
			&& World->GetTimeSeconds() - LastShotTime < 3.f)
		{
			Killer = LastShooterPC.Get();
		}
		FCitixPlayerGame* KillerGame = FindPlayerGame(Killer ? Killer : FocusPC());
		if (KillerGame)
		{
			AddHeat(*KillerGame, 35.f * static_cast<float>(NewDeaths), TEXT("pedestrian killed"));
		}
		ShowToast(TEXT("Killing spree reported - police inbound"), 3.f);
		UE_LOG(LogCitix, Log, TEXT("[Citix] %d new kills attributed to %s."), NewDeaths,
			Killer && Killer->PlayerState ? *Killer->PlayerState->GetPlayerName() : TEXT("no one"));
	}
}

// ---------------------------------------------------------------------------
// Wanted level + police pursuit
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::AddHeat(float Amount, const TCHAR* Reason)
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		AddHeat(*Game, Amount, Reason);
	}
}

void ACitixSandboxDirector::AddHeat(FCitixPlayerGame& Game, float Amount, const TCHAR* Reason)
{
	if (Amount <= 0.f || !bConfigured)
	{
		return;
	}
	Game.Heat = FMath::Clamp(Game.Heat + Amount, 0.f, 100.f);
	Game.TimeSinceIncident = 0.f;
	Game.EscapeProgress = 0.f;
	UE_LOG(LogCitix, Log, TEXT("[Citix] Heat +%.0f (%s) = %.0f."), Amount, Reason, Game.Heat);
}

bool ACitixSandboxDirector::IsOutOfCameraView(const FVector& Point) const
{
	const FCitixPlayerView View = FCitixVisibility::GetPlayerView(GetWorld());
	if (!View.bValid)
	{
		return true;
	}
	return !FCitixVisibility::IsInViewCone(View, Point);
}

bool ACitixSandboxDirector::IsOutOfAllCameras(const FVector& Point) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	bool bAnyDriver = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		bAnyDriver = true;
		const FVector Location = Pawn->GetActorLocation();
		const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal();
		// Widened cone like the single-player test: only clearly-behind counts.
		const float CosHalf = FMath::Cos(FMath::DegreesToRadians(58.5f));
		const FVector ToPoint = Point - Location;
		if (ToPoint.SizeSquared() > 1.f
			&& FVector::DotProduct(ToPoint.GetSafeNormal(), Forward) >= CosHalf)
		{
			return false;
		}
	}
	return bAnyDriver;
}

void ACitixSandboxDirector::SpawnPoliceUnit(const FVector& AnchorLocation, const FVector& AnchorVelocity,
	AController* SuspectPC)
{
	if (GetPoliceCount() >= MaxPoliceUnits || Network.Nodes.Num() == 0)
	{
		return;
	}

	const FVector PlayerLocation = AnchorLocation;

	// Intercept, not trail: when the anchor is moving, prefer nodes ahead of
	// their motion (a fast car outruns a chase, but not a roadblock). Still out
	// of every camera view, never inside geometry: nodes are on roads by
	// construction, and Z comes from a ground trace.
	const FVector2D Motion2D(AnchorVelocity.X, AnchorVelocity.Y);
	const bool bMoving = Motion2D.SizeSquared() > 900.f * 900.f; // faster than 32 km/h
	const FVector2D MotionDir = bMoving ? Motion2D.GetSafeNormal() : FVector2D(1.f, 0.f);

	int32 Chosen = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 Attempt = 0; Attempt < 24; ++Attempt)
	{
		const int32 Candidate = Rng.RandRange(0, Network.Nodes.Num() - 1);
		const FVector2D& P = Network.Nodes[Candidate].Position;
		const FVector2D ToNode = P - FVector2D(PlayerLocation.X, PlayerLocation.Y);
		const float Distance = ToNode.Size();
		if (Distance < 10000.f || Distance > 20000.f)
		{
			continue;
		}
		if (!IsOutOfAllCameras(FVector(P.X, P.Y, PlayerLocation.Z)))
		{
			continue;
		}
		// Ahead of motion scores up to +1, behind scores down to -1.
		float SpawnScore = bMoving ? FVector2D::DotProduct(ToNode / FMath::Max(1.f, Distance), MotionDir) : 0.f;
		SpawnScore += Rng.FRandRange(0.f, 0.5f);
		if (SpawnScore > BestScore)
		{
			BestScore = SpawnScore;
			Chosen = Candidate;
		}
	}
	if (Chosen == INDEX_NONE)
	{
		// Fallback: the roomiest in-band node, capped so a unit never commutes
		// across the map. Anything past 40 m is a fresh spawn, not a chase.
		float BestDistance = 0.f;
		for (int32 Attempt = 0; Attempt < 12; ++Attempt)
		{
			const int32 Candidate = Rng.RandRange(0, Network.Nodes.Num() - 1);
			const FVector2D& P = Network.Nodes[Candidate].Position;
			const float Distance = FVector2D::Distance(P, FVector2D(PlayerLocation.X, PlayerLocation.Y));
			if (Distance > BestDistance && Distance <= 40000.f
				&& IsOutOfCameraView(FVector(P.X, P.Y, PlayerLocation.Z)))
			{
				BestDistance = Distance;
				Chosen = Candidate;
			}
		}
	}
	if (Chosen == INDEX_NONE)
	{
		// Off-graph suspect (fled the city into the void): plant the unit at
		// the nearest out-of-view node instead of abandoning the pursuit.
		// Without this, driving off the map is a free escape.
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 Attempt = 0; Attempt < 24; ++Attempt)
		{
			const int32 Candidate = Rng.RandRange(0, Network.Nodes.Num() - 1);
			const FVector2D& P = Network.Nodes[Candidate].Position;
			const float Distance = FVector2D::Distance(P, FVector2D(PlayerLocation.X, PlayerLocation.Y));
			if (Distance < BestDistance
				&& IsOutOfCameraView(FVector(P.X, P.Y, PlayerLocation.Z)))
			{
				BestDistance = Distance;
				Chosen = Candidate;
			}
		}
	}
	if (Chosen == INDEX_NONE)
	{
		return;
	}

	const FVector2D& P = Network.Nodes[Chosen].Position;
	FVector SpawnLocation(P.X, P.Y, PlayerLocation.Z + 1500.f);
	FHitResult GroundHit;
	FCollisionQueryParams Params;
	Params.bTraceComplex = false;
	if (GetWorld()->LineTraceSingleByChannel(GroundHit, SpawnLocation,
		FVector(P.X, P.Y, PlayerLocation.Z - 2000.f), ECC_WorldStatic, Params))
	{
		SpawnLocation.Z = GroundHit.Location.Z + 60.f;
	}
	else
	{
		SpawnLocation.Z = PlayerLocation.Z;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACitixPoliceVehicle* Car = GetWorld()->SpawnActor<ACitixPoliceVehicle>(
		ACitixPoliceVehicle::StaticClass(),
		FTransform(FRotator::ZeroRotator, SpawnLocation), SpawnParams);
	if (!Car)
	{
		return;
	}
	Car->InitializeVehicle(ECitixCarType::Sedan, FLinearColor(0.03f, 0.03f, 0.04f), Rng.RandRange(0, 1000000));
	Car->InitializePolice();

	FCitixPoliceUnit Unit;
	Unit.Car = Car;
	Unit.SuspectPC = SuspectPC;
	Unit.RepathTimer = PoliceUnits.Num() * 0.33f; // stagger the 1 Hz repaths
	Unit.Speed = 0.f;
	Unit.GroundZ = SpawnLocation.Z;
	const FVector ToSuspect = PlayerLocation - SpawnLocation;
	Unit.PursuitYaw = (ToSuspect.SizeSquared2D() > 1.f)
		? FMath::RadiansToDegrees(FMath::Atan2(ToSuspect.Y, ToSuspect.X)) : 0.f;
	PoliceUnits.Add(Unit);

	int32 SuspectStars = 0;
	if (FCitixPlayerGame* SuspectGame = FindPlayerGame(SuspectPC))
	{
		SuspectStars = SuspectGame->WantedStars;
		ShowToast(*SuspectGame, FString::Printf(TEXT("Police responding (%d star%s)"),
			SuspectStars, SuspectStars > 1 ? TEXT("s") : TEXT("")));
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Police unit spawned (%d active) %.0f cm from suspect."),
		GetPoliceCount(), FVector::Dist2D(SpawnLocation, PlayerLocation));
}

void ACitixSandboxDirector::ReleasePoliceUnits()
{
	for (FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.Car)
		{
			Unit.Car->Destroy();
			Unit.Car = nullptr;
		}
	}
	PoliceUnits.Reset();
	for (FCitixPoliceOfficerState& Officer : Officers)
	{
		if (Officer.Officer)
		{
			Officer.Officer->Destroy();
			Officer.Officer = nullptr;
		}
	}
	Officers.Reset();
}

void ACitixSandboxDirector::ReleasePoliceUnits(AController* SuspectPC)
{
	if (!SuspectPC)
	{
		return;
	}
	TArray<ACitixPoliceVehicle*> SuspectCars;
	for (FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.SuspectPC.Get() == SuspectPC && Unit.Car)
		{
			SuspectCars.Add(Unit.Car);
			Unit.Car->Destroy();
			Unit.Car = nullptr;
		}
	}
	PoliceUnits.RemoveAll([SuspectPC](const FCitixPoliceUnit& Unit)
	{
		return !Unit.Car && Unit.SuspectPC.Get() == SuspectPC;
	});
	// Officers walk for their unit's suspect; the sweep below drops orphans.
	for (FCitixPoliceOfficerState& Officer : Officers)
	{
		if (Officer.Officer && SuspectCars.Contains(Officer.UnitCar.Get()))
		{
			Officer.Officer->Destroy();
			Officer.Officer = nullptr;
		}
	}
}

int32 ACitixSandboxDirector::PoliceUnitsFor(AController* SuspectPC) const
{
	int32 Count = 0;
	for (const FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (Unit.Car && !Unit.bDisabled && Unit.SuspectPC.Get() == SuspectPC)
		{
			++Count;
		}
	}
	return Count;
}

int32 ACitixSandboxDirector::LivingOfficerCount() const
{
	int32 Count = 0;
	for (const FCitixPoliceOfficerState& Officer : Officers)
	{
		Count += (Officer.Officer && !Officer.bDying) ? 1 : 0;
	}
	return Count;
}

FVector ACitixSandboxDirector::FindSafeRoadLocation(const FVector& PlayerLocation) const
{
	// Nearby on the road graph, but away from every police car.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Network.Nodes.Num(); ++Index)
	{
		const FVector2D& P = Network.Nodes[Index].Position;
		const float PlayerDistance = FVector2D::Distance(P, FVector2D(PlayerLocation.X, PlayerLocation.Y));
		if (PlayerDistance < 8000.f || PlayerDistance > 25000.f)
		{
			continue;
		}
		bool bNearPolice = false;
		for (const FCitixPoliceUnit& Unit : PoliceUnits)
		{
			if (Unit.Car && FVector::Dist2D(Unit.Car->GetActorLocation(),
				FVector(P.X, P.Y, PlayerLocation.Z)) < 10000.f)
			{
				bNearPolice = true;
				break;
			}
		}
		if (!bNearPolice && PlayerDistance < BestDistance)
		{
			BestDistance = PlayerDistance;
			Best = Index;
		}
	}
	if (Best == INDEX_NONE)
	{
		Best = NearestNode(PlayerLocation);
	}
	if (Best == INDEX_NONE)
	{
		return PlayerLocation;
	}
	const FVector2D& P = Network.Nodes[Best].Position;
	return FVector(P.X, P.Y, PlayerLocation.Z);
}

void ACitixSandboxDirector::PoliceCaughtPlayer(APawn* CaughtPawn)
{
	APawn* PlayerPawn = CaughtPawn ? CaughtPawn
		: (FocusPC() && FocusPC()->GetPawn() ? FocusPC()->GetPawn().Get() : nullptr);
	const FVector SafeSpot = FindSafeRoadLocation(
		PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation());

	if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(PlayerPawn))
	{
		Vehicle->SetActorLocationAndRotation(SafeSpot, Vehicle->GetActorRotation(),
			false, nullptr, ETeleportType::TeleportPhysics);
		Vehicle->ResetVehicle();
	}
	else if (PlayerPawn)
	{
		PlayerPawn->SetActorLocation(SafeSpot, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Personal bust: only the caught driver's record resets, only their units
	// stand down, only their score pays. Bystanders keep their own pursuit.
	AController* SuspectPC = PlayerPawn ? PlayerPawn->GetController() : nullptr;
	if (FCitixPlayerGame* Game = FindPlayerGame(SuspectPC ? SuspectPC : FocusPC()))
	{
		Game->Heat = 0.f;
		Game->WantedStars = 0;
		Game->EscapeProgress = 0.f;
		Game->TimeSinceIncident = 1000.f;
		Game->Score = FMath::Max(0, Game->Score - 100);
		ShowToast(*Game, TEXT("Busted! Cooled off across town  (score -100)"));
		UE_LOG(LogCitix, Log, TEXT("[Citix] Caught by police: reset, score %d."), Game->Score);
	}
	ReleasePoliceUnits(SuspectPC);
	if (FCitixPlayerGame* FocusGame = FindPlayerGame(SuspectPC ? SuspectPC : FocusPC()))
	{
		CopyGameToGlobals(*FocusGame);
	}
}

void ACitixSandboxDirector::UpdateOfficersSlow(float DeltaSeconds, const TArray<APawn*>& Pawns)
{
	// Sweep released entries first.
	for (int32 Index = Officers.Num() - 1; Index >= 0; --Index)
	{
		if (!Officers[Index].Officer)
		{
			Officers.RemoveAt(Index);
		}
	}

	for (FCitixPoliceOfficerState& State : Officers)
	{
		if (!State.Officer || State.bDying)
		{
			continue;
		}
		// Each officer works its own unit's suspect on foot; drivers in cars
		// are someone else's problem (the ramming kind).
		FCitixPoliceUnit* Unit = nullptr;
		for (FCitixPoliceUnit& Candidate : PoliceUnits)
		{
			if (Candidate.Car && Candidate.Car == State.UnitCar.Get())
			{
				Unit = &Candidate;
				break;
			}
		}
		AController* SuspectPC = Unit ? Unit->SuspectPC.Get() : nullptr;
		APawn* TargetPawn = nullptr;
		if (SuspectPC)
		{
			if (APawn* SuspectPawn = SuspectPC->GetPawn())
			{
				if (Cast<ACitixOnFootPawn>(SuspectPawn))
				{
					TargetPawn = SuspectPawn;
				}
			}
		}
		const FVector OfficerLocation = State.Officer->GetActorLocation();
		const FVector PlayerLocation = TargetPawn ? TargetPawn->GetActorLocation() : OfficerLocation;
		const float Distance = TargetPawn
			? FVector::Dist2D(OfficerLocation, PlayerLocation) : TNumericLimits<float>::Max();
		float PlayerSpeed = 0.f;
		if (TargetPawn)
		{
			PlayerSpeed = TargetPawn->GetVelocity().Size();
		}

		// Stand down when there is nothing to enforce: suspect clean or gone,
		// no on-foot target in range, a trashed unit, or a target far away.
		int32 SuspectStars = 0;
		if (SuspectPC)
		{
			if (FCitixPlayerGame* SuspectGame = FindPlayerGame(SuspectPC))
			{
				SuspectStars = SuspectGame->WantedStars;
			}
		}
		bool bStandDown = !Unit || SuspectStars <= 0 || !TargetPawn || Distance > 8000.f;
		if (bStandDown)
		{
			State.bDying = true;
			if (State.Officer)
			{
				State.Officer->bDead = true;
			}
			continue;
		}

		// Close in: an officer on top of a slow player tightens the net.
		if (Distance < 1200.f && PlayerSpeed < 400.f)
		{
			Unit->CatchTimer = FMath::Min(Unit->CatchTimer + DeltaSeconds, 2.9f);
		}

		// Sidearm: aimed shots inside 30 m, pistol-slow.
		State.FireTimer = FMath::Max(0.f, State.FireTimer - DeltaSeconds);
		if (Distance < 3000.f && State.FireTimer <= 0.f && TargetPawn)
		{
			State.FireTimer = 1.1f;
			const FVector Muzzle = OfficerLocation + FVector(0.f, 0.f, 130.f);
			const FVector Target = PlayerLocation + FVector(0.f, 0.f, 100.f);
			FRotator Aim = (Target - Muzzle).Rotation();
			Aim.Yaw += Rng.FRandRange(-4.f, 4.f);
			Aim.Pitch += Rng.FRandRange(-4.f, 4.f);
			const FVector Direction = Aim.Vector();
			const FVector End = Muzzle + Direction * 4000.f;
			for (ACitixPulseBolt* Bolt : TracerBolts)
			{
				if (Bolt && Bolt->IsIdle())
				{
					Bolt->Fire(Muzzle, End, ECitixSurface::EmissiveWarm);
					break;
				}
			}
			const FVector ToTarget = Target - Muzzle;
			const float Along = FMath::Clamp(FVector::DotProduct(ToTarget, Direction), 0.f, 4000.f);
			if ((Muzzle + Direction * Along - Target).Size() < 120.f && Unit)
			{
				Unit->CatchTimer = FMath::Min(Unit->CatchTimer + 0.5f, 2.9f);
				UE_LOG(LogCitix, Log, TEXT("[Citix] Officer clipped the player on foot."));
				// Sidearm hits draw blood (rank 10: officer gunfire is lethal now).
				if (TargetPawn && TargetPawn->GetController())
				{
					FString UnitName = TEXT("Police");
					DamagePlayer(TargetPawn->GetController(), 10.f, nullptr, UnitName, TEXT("shot"));
				}
			}
		}
	}
}

bool ACitixSandboxDirector::HasOfficerForUnit(int32 UnitIndex) const
{
	if (!PoliceUnits.IsValidIndex(UnitIndex) || !PoliceUnits[UnitIndex].Car)
	{
		return false;
	}
	ACitixPoliceVehicle* Car = PoliceUnits[UnitIndex].Car;
	for (const FCitixPoliceOfficerState& State : Officers)
	{
		if (State.Officer && !State.bDying && State.UnitCar.Get() == Car)
		{
			return true;
		}
	}
	return false;
}

void ACitixSandboxDirector::SpawnOfficer(int32 UnitIndex, const FVector& CarLocation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Step out of the driver's side onto the road, never inside the car mesh.
	FVector SideOffset(220.f, 0.f, 0.f);
	if (PoliceUnits.IsValidIndex(UnitIndex) && PoliceUnits[UnitIndex].Car)
	{
		const float CarYaw = PoliceUnits[UnitIndex].Car->GetActorRotation().Yaw;
		const FVector CarRight = FRotator(0.f, CarYaw + 90.f, 0.f).Vector();
		SideOffset = CarRight * 220.f;
	}
	const FVector StepOut = CarLocation + SideOffset;
	const float StepZ = GroundZAt(World, StepOut, CarLocation.Z);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACitixPoliceOfficer* Officer = World->SpawnActor<ACitixPoliceOfficer>(
		ACitixPoliceOfficer::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(StepOut.X, StepOut.Y, StepZ)), Params))
	{
		// One fixed uniform for every officer.
		Officer->InitializeOfficer(4242);
		FCitixPoliceOfficerState State;
		State.Officer = Officer;
		State.UnitCar = PoliceUnits.IsValidIndex(UnitIndex) ? PoliceUnits[UnitIndex].Car : nullptr;
		State.FireTimer = 0.5f;
		Officers.Add(State);
		if (PoliceUnits.IsValidIndex(UnitIndex))
		{
			if (FCitixPlayerGame* SuspectGame = FindPlayerGame(PoliceUnits[UnitIndex].SuspectPC.Get()))
			{
				ShowToast(*SuspectGame, TEXT("Officers on foot - keep moving!"));
			}
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Officer dismounted (%d on foot)."), LivingOfficerCount());
	}
}

int32 ACitixSandboxDirector::TestSpawnOfficer()
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	UWorld* World = GetWorld();
	if (!World || !PlayerPawn)
	{
		return 0;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Spot = PlayerPawn->GetActorLocation() + FVector(500.f, 0.f, 0.f);
	if (ACitixPoliceOfficer* Officer = World->SpawnActor<ACitixPoliceOfficer>(
		ACitixPoliceOfficer::StaticClass(), FTransform(FRotator::ZeroRotator, Spot), Params))
	{
		Officer->InitializeOfficer(1234);
		FCitixPoliceOfficerState State;
		State.Officer = Officer;
		State.UnitCar = nullptr;
		Officers.Add(State);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Test: officer spawned."));
		return 1;
	}
	return 0;
}

bool ACitixSandboxDirector::TestDamageOfficer()
{
	for (FCitixPoliceOfficerState& State : Officers)
	{
		if (State.Officer && !State.bDying)
		{
			State.Health = 0.f;
			State.bDying = true;
			State.Officer->bDead = true;
			UE_LOG(LogCitix, Log, TEXT("[Citix] Test: officer put down."));
			return true;
		}
	}
	return false;
}

void ACitixSandboxDirector::UpdateCrashWatch(FCitixPlayerGame& Game, const APawn* PlayerPawn)
{
	Game.CrashCooldown = FMath::Max(0.f, Game.CrashCooldown - UpdateInterval);

	const ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(PlayerPawn);
	const UCitixVehicleMovementComponent* Movement = Vehicle ? Vehicle->GetVehicleMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	const float Hull = Movement->GetHealthFraction();
	// One capped crash costs ~11% of a sedan hull: a real impact, not a scrape.
	// Hull is tracked per driver so one player's pile-up never double-counts.
	if (Game.LastHullFraction - Hull >= 0.08f && Game.CrashCooldown <= 0.f)
	{
		Game.CrashCooldown = 3.f;
		AddHeat(Game, 12.f, TEXT("collision"));
	}
	Game.LastHullFraction = Hull;
}

void ACitixSandboxDirector::UpdateWantedForDrivers(float DeltaSeconds, const TArray<APawn*>& Pawns)
{
	for (APawn* Pawn : Pawns)
	{
		if (!Pawn)
		{
			continue;
		}
		AController* PC = Pawn->GetController();
		FCitixPlayerGame* Game = FindPlayerGame(PC);
		if (!Game)
		{
			continue;
		}
		Game->TimeSinceIncident += DeltaSeconds;

		// Heat decay: quiet time plus distance from YOUR hunters cools things.
		float NearestHunter = TNumericLimits<float>::Max();
		for (const FCitixPoliceUnit& Unit : PoliceUnits)
		{
			if (!Unit.Car || Unit.SuspectPC.Get() != PC)
			{
				continue;
			}
			NearestHunter = FMath::Min(NearestHunter,
				FVector::Dist2D(Unit.Car->GetActorLocation(), Pawn->GetActorLocation()));
		}
		const bool bHuntersFar = NearestHunter > 15000.f;
		if (Game->TimeSinceIncident > 8.f && Game->Heat > 0.f)
		{
			Game->Heat = FMath::Max(0.f, Game->Heat - (bHuntersFar ? 10.f : 3.f) * DeltaSeconds);
		}

		// Escape progress fills while your hunters are far and quiet.
		if (Game->WantedStars > 0 && PoliceUnitsFor(PC) > 0 && bHuntersFar && Game->TimeSinceIncident > 4.f)
		{
			Game->EscapeProgress = FMath::Clamp(Game->EscapeProgress + DeltaSeconds / 20.f, 0.f, 1.f);
		}
		else if (Game->WantedStars == 0)
		{
			Game->EscapeProgress = 0.f;
		}

		// Stars with hysteresis so the meter does not flicker at a threshold.
		if (Game->Heat >= 85.f) { Game->WantedStars = 3; }
		else if (Game->Heat >= 55.f) { Game->WantedStars = 2; }
		else if (Game->Heat >= 25.f) { Game->WantedStars = 1; }
		else if (Game->Heat < 15.f) { Game->WantedStars = 0; }
		else if (Game->WantedStars == 3 && Game->Heat < 75.f) { Game->WantedStars = 2; }
		else if (Game->WantedStars == 2 && Game->Heat < 45.f) { Game->WantedStars = 1; }

		// Match this suspect's pursuit fleet to their stars, staggered.
		// Disabled units do not count: pressure stays up until the heat drops.
		const int32 Desired = FMath::Min(Game->WantedStars, MaxPoliceUnits);
		PoliceSpawnTimer = FMath::Max(0.f, PoliceSpawnTimer - DeltaSeconds);
		if (PoliceUnitsFor(PC) < Desired && PoliceSpawnTimer <= 0.f && GetPoliceCount() < MaxPoliceUnits)
		{
			PoliceSpawnTimer = 3.f;
			SpawnPoliceUnit(Pawn->GetActorLocation(), Pawn->GetVelocity(), PC);
		}
		if (Game->WantedStars == 0 && PoliceUnitsFor(PC) > 0)
		{
			ReleasePoliceUnits(PC);
			ShowToast(*Game, TEXT("All clear - police stood down"));
			UE_LOG(LogCitix, Log, TEXT("[Citix] Wanted cleared."));
		}
	}
}

void ACitixSandboxDirector::UpdatePoliceMovementForDrivers(float DeltaSeconds, const TArray<APawn*>& Pawns)
{
	if (DeltaSeconds <= 0.f || Pawns.Num() == 0)
	{
		return;
	}

	// Pursuit cars drive every frame with a real speed state: accelerate up to
	// the chase, brake on a curve into the hold point, steer inside a car-like
	// yaw budget, and drive where the nose points. No steps, no hard stops.
	// Each unit works ITS suspect, never the nearest innocent.
	for (FCitixPoliceUnit& Unit : PoliceUnits)
	{
		if (!Unit.Car || Unit.bDisabled)
		{
			continue;
		}
		AController* SuspectPC = Unit.SuspectPC.Get();
		APawn* SuspectPawn = SuspectPC ? SuspectPC->GetPawn() : nullptr;
		if (!SuspectPawn)
		{
			continue;
		}
		const FVector CarLocation = Unit.Car->GetActorLocation();

		const FVector DriverLocation = SuspectPawn->GetActorLocation();
		const bool bDriverOnFoot = Cast<ACitixOnFootPawn>(SuspectPawn) != nullptr;

		FVector Target = DriverLocation;
		if (Unit.PathCursor < Unit.Path.Num())
		{
			const int32 Node = Unit.Path[Unit.PathCursor];
			if (Network.IsValidNode(Node))
			{
				const FVector2D& P = Network.Nodes[Node].Position;
				Target = FVector(P.X, P.Y, Unit.GroundZ);
			}
		}

		const float Distance = FVector::Dist2D(CarLocation, Target);
		// Hold short instead of clipping into the player: officers take it from
		// here on foot, bumpers kiss at 420 in a car chase.
		const float HoldDistance = bDriverOnFoot ? 1800.f : 420.f;
		const float DistToHold = FMath::Max(0.f, Distance - HoldDistance);

		// Desired speed: full 130 km/h chase far away, braking curve to rest
		// exactly at the hold point.
		const float Desired = FMath::Min(3611.f, FMath::Sqrt(2.f * 900.f * DistToHold));
		const float Rate = (Desired > Unit.Speed) ? 1800.f : 900.f;
		Unit.Speed = FMath::FInterpConstantTo(Unit.Speed, Desired, DeltaSeconds, Rate);

		const FVector ToTarget = Target - CarLocation;
		float TargetYaw = Unit.PursuitYaw;
		if (ToTarget.SizeSquared2D() > 1.f)
		{
			TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
		}
		Unit.PursuitYaw = FMath::FixedTurn(Unit.PursuitYaw, TargetYaw, 140.f * DeltaSeconds);

		if (Unit.Speed > 20.f)
		{
			const FVector Heading = FRotator(0.f, Unit.PursuitYaw, 0.f).Vector();
			const FVector NewLocation(CarLocation.X + Heading.X * Unit.Speed * DeltaSeconds,
				CarLocation.Y + Heading.Y * Unit.Speed * DeltaSeconds, Unit.GroundZ);
			Unit.Car->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else if (bDriverOnFoot)
		{
			// Stopped: keep facing the player.
			const FVector ToPlayer = DriverLocation - CarLocation;
			if (ToPlayer.SizeSquared2D() > 1.f)
			{
				Unit.PursuitYaw = FMath::FixedTurn(Unit.PursuitYaw,
					FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Y, ToPlayer.X)),
					360.f * DeltaSeconds);
			}
		}
		Unit.Car->SetActorRotation(FRotator(0.f, Unit.PursuitYaw, 0.f));
	}

	// Officers walk every frame toward their unit's suspect on foot; the dying
	// fade plays out here too.
	for (FCitixPoliceOfficerState& State : Officers)
	{
		if (!State.Officer)
		{
			continue;
		}
		if (State.bDying)
		{
			if (State.Officer->UpdateDying(DeltaSeconds))
			{
				State.Officer->Destroy();
				State.Officer = nullptr;
			}
			continue;
		}
		const FVector OfficerLocation = State.Officer->GetActorLocation();
		FVector WalkTarget = OfficerLocation;
		float WalkDistance = TNumericLimits<float>::Max();
		for (const FCitixPoliceUnit& Candidate : PoliceUnits)
		{
			if (Candidate.Car && Candidate.Car == State.UnitCar.Get())
			{
				if (AController* SuspectPC = Candidate.SuspectPC.Get())
				{
					if (APawn* SuspectPawn = SuspectPC->GetPawn())
					{
						if (Cast<ACitixOnFootPawn>(SuspectPawn))
						{
							WalkDistance = FVector::Dist2D(OfficerLocation,
								SuspectPawn->GetActorLocation());
							WalkTarget = SuspectPawn->GetActorLocation();
						}
					}
				}
				break;
			}
		}
		const float Distance = WalkDistance;
		float Speed = 0.f;
		if (Distance > 1200.f && Distance < TNumericLimits<float>::Max() / 2.f)
		{
			const FVector ToPlayer = WalkTarget - OfficerLocation;
			const FVector Direction = ToPlayer.GetSafeNormal2D();
			Speed = 280.f;
			// Feet stay planted: XY walks, Z never touches the player's height.
			const FVector NewLocation(OfficerLocation.X + Direction.X * Speed * DeltaSeconds,
				OfficerLocation.Y + Direction.Y * Speed * DeltaSeconds, OfficerLocation.Z);
			State.Officer->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
			State.Officer->SetActorRotation(FRotator(0.f,
				FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)), 0.f));
		}
		else
		{
			const FVector ToPlayer = WalkTarget - OfficerLocation;
			if (ToPlayer.SizeSquared2D() > 1.f)
			{
				State.Officer->SetActorRotation(FRotator(0.f,
					FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Y, ToPlayer.X)), 0.f));
			}
		}
		State.Officer->UpdateStride(Speed, DeltaSeconds);
	}
}

void ACitixSandboxDirector::UpdatePoliceForDrivers(float DeltaSeconds, const TArray<APawn*>& Pawns)
{
	if ((PoliceUnits.Num() == 0 && Officers.Num() == 0) || Pawns.Num() == 0)
	{
		return;
	}

	for (int32 UnitIndex = PoliceUnits.Num() - 1; UnitIndex >= 0; --UnitIndex)
	{
		FCitixPoliceUnit& Unit = PoliceUnits[UnitIndex];
		if (!Unit.Car)
		{
			PoliceUnits.RemoveAt(UnitIndex);
			continue;
		}
		if (Unit.bDisabled)
		{
			continue;
		}

		const FVector CarLocation = Unit.Car->GetActorLocation();

		// Each unit hunts ITS suspect: speed, footing and node follow them, even
		// when an innocent driver is closer. A released suspect drops the unit
		// (cleanup sweep below instead of retargeting innocents).
		AController* SuspectPC = Unit.SuspectPC.Get();
		APawn* TargetPawn = SuspectPC ? SuspectPC->GetPawn() : nullptr;
		if (!TargetPawn)
		{
			if (!SuspectPC)
			{
				Unit.Car->Destroy();
				Unit.Car = nullptr;
				PoliceUnits.RemoveAt(UnitIndex);
			}
			continue;
		}
		const FVector PlayerLocation = TargetPawn->GetActorLocation();
		const bool bPlayerOnFoot = Cast<ACitixOnFootPawn>(TargetPawn) != nullptr;
		float PlayerSpeed = 0.f;
		if (const ACitixVehiclePawn* TargetVehicle = Cast<ACitixVehiclePawn>(TargetPawn))
		{
			if (const UCitixVehicleMovementComponent* Movement = TargetVehicle->GetVehicleMovement())
			{
				PlayerSpeed = Movement->GetSpeedKmh() * 100.f / 3.6f; // cm/s
			}
		}
		else
		{
			PlayerSpeed = TargetPawn->GetVelocity().Size();
		}
		const int32 PlayerNode = NearestNode(PlayerLocation);

		// Repath at 1 Hz (staggered per unit): node path to the player's node.
		// Sticky: repath FROM the node currently being driven at, not from the
		// car. Repathing from the car re-anchors to the node just left behind
		// and the unit ping-pongs instead of chasing.
		Unit.RepathTimer -= DeltaSeconds;
		if (Unit.RepathTimer <= 0.f)
		{
			Unit.RepathTimer = 1.f;
			FVector RepathFrom = CarLocation;
			if (Unit.PathCursor < Unit.Path.Num())
			{
				const int32 HoldNode = Unit.Path[Unit.PathCursor];
				if (Network.IsValidNode(HoldNode))
				{
					const FVector2D& Hold = Network.Nodes[HoldNode].Position;
					RepathFrom = FVector(Hold.X, Hold.Y, CarLocation.Z);
				}
			}
			else
			{
				Unit.PathCursor = 0;
			}
			TArray<int32> Path;
			if (PlayerNode != INDEX_NONE && FindPathNodes(RepathFrom, PlayerLocation, Path))
			{
				Unit.Path = MoveTemp(Path);
				// The fresh path starts at the held node ahead: keep driving at it.
				Unit.PathCursor = 0;
			}
			else
			{
				Unit.Path.Reset();
				Unit.PathCursor = 0;
			}
			// Replant on the road: traces kill any float or sink once a second.
			Unit.GroundZ = GroundZAt(GetWorld(), CarLocation, Unit.GroundZ);
		}

		// Steer along the path, else drive straight at the player.
		// (Translation runs per-frame in UpdatePoliceMovement; here the route
		// only advances along committed corners.)
		if (Unit.PathCursor < Unit.Path.Num())
		{
			const int32 Node = Unit.Path[Unit.PathCursor];
			if (Network.IsValidNode(Node))
			{
				const FVector2D& P = Network.Nodes[Node].Position;
				if (FVector::Dist2D(CarLocation, FVector(P.X, P.Y, PlayerLocation.Z)) < 800.f)
				{
					++Unit.PathCursor;
				}
			}
			else
			{
				++Unit.PathCursor;
			}
		}
		const float DistToPlayer = FVector::Dist2D(CarLocation, PlayerLocation);

		// Ram contact: bumper to bumper with the player's car hurts both.
		Unit.RamCooldown = FMath::Max(0.f, Unit.RamCooldown - DeltaSeconds);
		if (!bPlayerOnFoot && DistToPlayer < 600.f && Unit.RamCooldown <= 0.f)
		{
			Unit.RamCooldown = 1.f;
			Unit.Health -= 4.f;
			if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(TargetPawn))
			{
				Vehicle->ApplyVehicleDamage(12.f);
			}
			for (ACitixPulseBolt* Bolt : TracerBolts)
			{
				if (Bolt && Bolt->IsIdle())
				{
					Bolt->Fire(CarLocation + FVector(0.f, 0.f, 100.f),
						PlayerLocation + FVector(0.f, 0.f, 100.f), ECitixSurface::EmissiveWarm);
					break;
				}
			}
			FireSparks((CarLocation + PlayerLocation) * 0.5f + FVector(0.f, 0.f, 100.f),
				ECitixSurface::EmissiveWarm, 4);
			if (Unit.Health <= 0.f && !Unit.bDisabled)
			{
				Unit.bDisabled = true;
				Unit.Car->SetFlashEnabled(false);
				if (FCitixPlayerGame* RamGame = FindPlayerGame(SuspectPC))
				{
					ShowToast(*RamGame, TEXT("Police unit wrecked itself ramming you!"));
				}
			}
			UE_LOG(LogCitix, Log, TEXT("[Citix] Police ram: player hit, unit health %.0f."), Unit.Health);
		}		if (DistToPlayer < 1200.f && PlayerSpeed < 400.f)
		{
			Unit.CatchTimer += DeltaSeconds;
			if (Unit.CatchTimer >= 3.f)
			{
				PoliceCaughtPlayer(TargetPawn);
				return;
			}
		}
		else
		{
			Unit.CatchTimer = FMath::Max(0.f, Unit.CatchTimer - DeltaSeconds);
		}

		// Return fire: aimed tracers inside 40 m, on the suspect's star-paced
		// cooldown. Accuracy sharpens with heat: 1 star sprays, 3 stars groups.
		Unit.FireTimer = FMath::Max(0.f, Unit.FireTimer - DeltaSeconds);
		if (DistToPlayer < 4000.f && Unit.FireTimer <= 0.f)
		{
			int32 SuspectStars = 1;
			if (FCitixPlayerGame* FireGame = FindPlayerGame(SuspectPC))
			{
				SuspectStars = FMath::Max(1, FireGame->WantedStars);
			}
			Unit.FireTimer = 1.5f - 0.3f * static_cast<float>(FMath::Clamp(SuspectStars, 1, 3));
			PoliceFireAtPlayer(Unit, TargetPawn, SuspectPC);
		}

		// Dismount: close to an on-foot player, send an officer on foot.
		if (bPlayerOnFoot && DistToPlayer < OfficerDismountRadius
			&& LivingOfficerCount() < MaxOfficers && !HasOfficerForUnit(UnitIndex))
		{
			SpawnOfficer(UnitIndex, CarLocation);
		}
	}

	UpdateOfficersSlow(DeltaSeconds, Pawns);
}

// ---------------------------------------------------------------------------
// Discovery + districts
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::UpdateDiscovery(FCitixPlayerGame& Game, AController* PC,
	const FVector& PlayerLocation)
{
	for (int32 PoiIndex = 0; PoiIndex < POIs.Num(); ++PoiIndex)
	{
		FCitixPointOfInterest& POI = POIs[PoiIndex];
		if (FVector::DistSquared2D(POI.Location, PlayerLocation) >= FMath::Square(DiscoveryRadius))
		{
			continue;
		}
		// Shared flag (map display for everyone), personal reward (rank 16):
		// a later visitor no longer consumes anyone's progression.
		POI.bDiscovered = true;
		if (!Game.DiscoveredPOIs.Contains(PoiIndex))
		{
			Game.DiscoveredPOIs.Add(PoiIndex);
			AddScore(Game, DiscoveryScore, TEXT("discovery"));

			// One card per landmark: name, district flavour, running progress.
			ShowCard(Game, POI.Name, FString::Printf(TEXT("%s  -  discovered %d/%d"),
				*DistrictFlavor(POI.District), Game.DiscoveredPOIs.Num(), POIs.Num()));

			// Crossings keep the old toast; everything else is covered by the card.
			if (POI.Kind == ECitixPOIKind::Crossing)
			{
				ShowToast(Game, FString::Printf(TEXT("Discovered: %s  (%d/%d)"),
					*POI.Name, Game.DiscoveredPOIs.Num(), POIs.Num()));
			}
			UE_LOG(LogCitix, Log, TEXT("[Citix] Discovered '%s' (%d/%d)."),
				*POI.Name, Game.DiscoveredPOIs.Num(), POIs.Num());
		}
	}
	CopyGameToGlobals(Game);
}

void ACitixSandboxDirector::UpdateDistricts(FCitixPlayerGame& Game, const FVector& PlayerLocation)
{
	Game.DistrictCardCooldown = FMath::Max(0.f, Game.DistrictCardCooldown - UpdateInterval);

	const int32 DistrictIndex = CityPlan.GetDistrictAt(FVector2D(PlayerLocation.X, PlayerLocation.Y));
	if (DistrictIndex == Game.CurrentDistrictIndex)
	{
		return;
	}
	Game.CurrentDistrictIndex = DistrictIndex;
	if (DistrictIndex == INDEX_NONE || !CityPlan.Districts.IsValidIndex(DistrictIndex))
	{
		Game.CurrentDistrict = ECitixDistrict::OuterCity;
		return;
	}

	const FCitixPlanDistrict& District = CityPlan.Districts[DistrictIndex];
	Game.CurrentDistrict = District.Type;

	if (Game.DistrictCardCooldown > 0.f)
	{
		return;
	}
	Game.DistrictCardCooldown = 4.f;

	const bool bFirstVisit = !Game.VisitedDistricts.Contains(DistrictIndex);
	Game.VisitedDistricts.Add(DistrictIndex);
	VisitedDistricts.Add(DistrictIndex);
	ShowCard(Game, District.Name.Len() > 0
		? District.Name
		: FString(DistrictName(District.Type)),
		FString::Printf(TEXT("%s%s  -  %d/%d districts"),
			bFirstVisit ? TEXT("NEW DISTRICT  -  ") : TEXT(""),
			*DistrictFlavor(District.Type), Game.VisitedDistricts.Num(), CityPlan.Districts.Num()));

	UE_LOG(LogCitix, Log, TEXT("[Citix] Entered district '%s' (%d/%d visited)."),
		*Game.CardTitle, Game.VisitedDistricts.Num(), CityPlan.Districts.Num());
	CopyGameToGlobals(Game);
}

// ---------------------------------------------------------------------------
// Test hooks
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::TestForceDiscovery(int32 Count)
{
	int32 Done = 0;
	for (FCitixPointOfInterest& POI : POIs)
	{
		if (Done >= Count)
		{
			break;
		}
		if (!POI.bDiscovered)
		{
			POI.bDiscovered = true;
			AddScore(DiscoveryScore, TEXT("discovery (test)"));
			++Done;
		}
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Test: forced %d discoveries, score=%d."), Done, Score);
}

void ACitixSandboxDirector::TestForceCaught()
{
	AController* Suspect = FocusPC();
	APawn* PlayerPawn = (Suspect && Suspect->GetPawn()) ? Suspect->GetPawn().Get()
		: UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();
	if (FCitixPlayerGame* Game = FindPlayerGame(Suspect))
	{
		AddHeat(*Game, 100.f, TEXT("test"));
	}
	TArray<APawn*> Pawns;
	if (PlayerPawn)
	{
		Pawns.Add(PlayerPawn);
	}
	UpdateWantedForDrivers(0.01f, Pawns);
	// Guarded: a failed spawn must never spin here (empty graph, no world).
	for (int32 Attempt = 0; Attempt < 4
		&& GetPoliceCount() < FMath::Min(GetWantedStars(), MaxPoliceUnits); ++Attempt)
	{
		SpawnPoliceUnit(PlayerLocation, FVector::ZeroVector, Suspect);
	}
	if (PoliceUnits.Num() > 0 && PoliceUnits[0].Car)
	{
		if (PlayerPawn)
		{
			PoliceUnits[0].Car->SetActorLocation(PlayerPawn->GetActorLocation() + FVector(300.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		PoliceUnits[0].CatchTimer = 2.9f;
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Test: forced catch setup, stars=%d police=%d."),
		GetWantedStars(), GetPoliceCount());
}

void ACitixSandboxDirector::TestClearWanted()
{
	if (FCitixPlayerGame* Game = FindPlayerGame(FocusPC()))
	{
		Game->Heat = 0.f;
		Game->WantedStars = 0;
		Game->EscapeProgress = 0.f;
		Game->TimeSinceIncident = 1000.f;
	}
	ReleasePoliceUnits();
	UE_LOG(LogCitix, Log, TEXT("[Citix] Test: wanted cleared."));
}

void ACitixSandboxDirector::TestGiveMoney(int32 Amount)
{
	AddMoney(Amount, TEXT("test"));
}

bool ACitixSandboxDirector::TestBuyWeapon(int32 Index)
{
	return BuyWeapon(FocusPC(), Index);
}

FString ACitixSandboxDirector::TestWeaponPropState() const
{
	if (!WeaponProp)
	{
		return TEXT("prop=null");
	}
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	return FString::Printf(TEXT("prop at %s hidden=%d tick=%d pawn=%s current=%d"),
		*WeaponProp->GetActorLocation().ToCompactString(),
		WeaponProp->IsHidden() ? 1 : 0,
		WeaponProp->IsActorTickEnabled() ? 1 : 0,
		PlayerPawn ? *PlayerPawn->GetClass()->GetName() : TEXT("none"),
		CurrentWeapon);
}

int32 ACitixSandboxDirector::TestGunHit()
{	if (!PedestrianSystem.IsValid())
	{
		for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
		{
			PedestrianSystem = *It;
			break;
		}
	}
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PedestrianSystem.IsValid() || !PlayerPawn)
	{
		return 0;
	}
	FVector Walker;
	if (!PedestrianSystem->GetNearestWalker(PlayerPawn->GetActorLocation(), 12000.f, Walker))
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] Weapon test: no walker in range for lethality check."));
		return 0;
	}
	// Sniper math, one landed hit: 101 > 100 health. (Pistol needs 3 landed hits on
	// the SAME walker, but knockdowns leave the targeting pool, so multi-shot kills
	// are proven by accumulation instead of scripting.)
	const FVector From = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 150.f);
	int32 Kills = 0;
	int32 ShotKills = 0;
	const int32 Hits = PedestrianSystem->GunHit(From, Walker, 300.f, 101.f, ShotKills);
	Kills += ShotKills;
	UE_LOG(LogCitix, Log, TEXT("[Citix] Weapon test: lethality check hits=%d kills=%d."), Hits, Kills);
	return Kills;
}

void ACitixSandboxDirector::TestStartleAroundPlayer()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();

	int32 Fled = 0;
	if (!PedestrianSystem.IsValid())
	{
		for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
		{
			PedestrianSystem = *It;
			break;
		}
	}
	if (PedestrianSystem.IsValid())
	{
		Fled = PedestrianSystem->FleeFrom(PlayerLocation, 8000.f);
	}

	int32 Startled = 0;
	if (!TrafficSystem.IsValid())
	{
		for (TActorIterator<ACitixTrafficSystem> It(GetWorld()); It; ++It)
		{
			TrafficSystem = *It;
			break;
		}
	}
	if (TrafficSystem.IsValid())
	{
		Startled = TrafficSystem->StartleVehicles(PlayerLocation, 6000.f, 3.f);
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] Weapon test: startle check fled=%d startled=%d."), Fled, Startled);
}

// ---------------------------------------------------------------------------
// Stunts / density / toast / tick (retained behaviour)
// ---------------------------------------------------------------------------

void ACitixSandboxDirector::UpdateStunts(FCitixPlayerGame& Game, float DeltaSeconds, const AActor* PlayerPawn)
{
	const ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(PlayerPawn);
	const UCitixVehicleMovementComponent* Movement = Vehicle ? Vehicle->GetVehicleMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	const bool bGrounded = Movement->IsGrounded();
	const float Speed = Movement->GetSpeedKmh();
	// Only count air time when the car is actually moving: otherwise a car that
	// simply settles onto the ground at spawn counts as a stunt.
	const bool bFastEnough = Speed > 20.f;

	if (bGrounded)
	{
		if (!Game.bStuntWasGrounded && Game.StuntAirTime >= StuntMinAirTime)
		{
			// Landing after real air time: score it by air time and distance covered.
			const float Distance = FVector::Dist2D(PlayerPawn->GetActorLocation(), Game.StuntAirStart);
			LastStuntScore = Game.StuntAirTime * 120.f + Distance * 0.25f;
			const int32 Points = FMath::RoundToInt(LastStuntScore);
			TotalAirTime += Game.StuntAirTime;
			AddScore(Game, Points, TEXT("stunt"));
			ShowToast(Game, FString::Printf(TEXT("Stunt!  +%d  (air %.1fs)"),
				Points, Game.StuntAirTime));
		}
		Game.StuntAirTime = 0.f;
	}
	else
	{
		if (Game.bStuntWasGrounded)
		{
			Game.StuntAirStart = PlayerPawn->GetActorLocation();
		}
		Game.StuntAirTime = bFastEnough ? Game.StuntAirTime + DeltaSeconds : 0.f;
	}
	Game.bStuntWasGrounded = bGrounded;
}

void ACitixSandboxDirector::ShowToast(const FString& Text, float Duration)
{
	// Legacy fan-out: focus readers use the globals, every record feeds its own
	// player state (today's shared behavior, preserved exactly).
	ToastText = Text;
	ToastRemaining = Duration;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (FCitixPlayerGame* Game = FindPlayerGame(PC))
			{
				ShowToast(*Game, Text);
			}
		}
	}
}

void ACitixSandboxDirector::UpdateDensity()
{
	const ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(GetWorld());
	if (!TimeOfDay)
	{
		return;
	}

	// A day in the life of the city: quiet small hours, commuter peaks at 8 and 18.
	static const float KeyHours[]  = { 0.f,  5.f,  7.f,  8.5f, 10.f, 16.f, 17.5f, 19.5f, 21.f, 24.f };
	static const float KeyScales[] = { 0.45f, 0.40f, 1.10f, 1.30f, 1.00f, 1.00f, 1.30f, 1.15f, 0.90f, 0.45f };
	constexpr int32 KeyCount = UE_ARRAY_COUNT(KeyHours);

	const float Hour = TimeOfDay->GetHours();
	float Scale = 1.f;
	for (int32 Index = 0; Index < KeyCount - 1; ++Index)
	{
		if (Hour >= KeyHours[Index] && Hour <= KeyHours[Index + 1])
		{
			const float Span = FMath::Max(0.01f, KeyHours[Index + 1] - KeyHours[Index]);
			Scale = FMath::Lerp(KeyScales[Index], KeyScales[Index + 1], (Hour - KeyHours[Index]) / Span);
			break;
		}
	}

	// Smooth changes anyway: density must never step while the player is watching.
	AppliedDensity = FMath::FInterpTo(AppliedDensity, Scale, UpdateInterval, 0.6f);

	if (!TrafficSystem.IsValid())
	{
		for (TActorIterator<ACitixTrafficSystem> It(GetWorld()); It; ++It) { TrafficSystem = *It; break; }
	}
	if (!PedestrianSystem.IsValid())
	{
		for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It) { PedestrianSystem = *It; break; }
	}
	if (TrafficSystem.IsValid())
	{
		TrafficSystem->SetDensityScale(AppliedDensity);
	}
	if (PedestrianSystem.IsValid())
	{
		PedestrianSystem->SetDensityScale(AppliedDensity);
	}
}

void ACitixSandboxDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bConfigured)
	{
		return;
	}

	// Clients never simulate: they render the server snapshot through
	// UpdateFromReplication (driven by the owning controller). Running the sim
	// here would fork score, jobs and police state.
	if (GetNetMode() == NM_Client)
	{
		return;
	}

	if (ToastRemaining > 0.f)
	{
		ToastRemaining = FMath::Max(0.f, ToastRemaining - DeltaSeconds);
	}
	DiscoveryCardRemaining = FMath::Max(0.f, DiscoveryCardRemaining - DeltaSeconds);
	DistrictCardRemaining = FMath::Max(0.f, DistrictCardRemaining - DeltaSeconds);

	// Stunts need per-frame resolution for air time; weapons need it for
	// auto-fire rate, reload and the ADS blend; pursuit glides per-frame too.
	// Every connected driver feeds their own record (single-player: one).
	struct FDriverEntry
	{
		APlayerController* PC = nullptr;
		APawn* Pawn = nullptr;
		FCitixPlayerGame* Game = nullptr;
	};
	TArray<FDriverEntry> Drivers;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				FDriverEntry Entry;
				Entry.PC = PC;
				Entry.Pawn = Pawn;
				Entry.Game = FindPlayerGame(PC);
				if (Entry.Game)
				{
					Drivers.Add(Entry);
				}
			}
		}
	}
	TArray<APawn*> DriverPawns;
	for (const FDriverEntry& Entry : Drivers)
	{
		DriverPawns.Add(Entry.Pawn);
	}

	for (const FDriverEntry& Entry : Drivers)
	{
		UpdateStunts(*Entry.Game, DeltaSeconds, Entry.Pawn);
		UpdateWeapon(*Entry.Game, Entry.PC, DeltaSeconds, Entry.Pawn);
	}
	UpdatePoliceMovementForDrivers(DeltaSeconds, DriverPawns);

	// Discovery, jobs and route only need a few updates per second.
	UpdateTimer += DeltaSeconds;
	if (UpdateTimer < UpdateInterval)
	{
		return;
	}
	UpdateTimer = 0.f;

	// Independent records: each driver advances only their own objectives,
	// wallet, heat and loadout. Solo contracts belong to their initiator.
	for (const FDriverEntry& Entry : Drivers)
	{
		const FVector DriverLocation = Entry.Pawn->GetActorLocation();
		UpdateDiscovery(*Entry.Game, Entry.PC, DriverLocation);
		UpdateDistricts(*Entry.Game, DriverLocation);
		UpdateCrashWatch(*Entry.Game, Entry.Pawn);
		UpdateJob(*Entry.Game, UpdateInterval, DriverLocation);
		UpdateActivity(*Entry.Game, UpdateInterval, DriverLocation);
		UpdatePhoto(*Entry.Game, UpdateInterval);
		UpdateQuestDay(*Entry.Game);
	}

	UpdateKillWatch();
	UpdateWantedForDrivers(UpdateInterval, DriverPawns);
	UpdatePoliceForDrivers(UpdateInterval, DriverPawns);
	UpdateRace(UpdateInterval);
	UpdateDensity();
	UpdatePlayerHealth(UpdateInterval);

	// Focus mirror + visuals for legacy local readers (host view / tests).
	// The rebuild runs unconditionally: a cleared waypoint must clear a stale
	// guide, not leave it behind.
	if (FCitixPlayerGame* FocusGame = FindPlayerGame(FocusPC()))
	{
		CopyGameToGlobals(*FocusGame);
		APawn* FocusPawn = FocusPC() ? FocusPC()->GetPawn() : nullptr;
		RebuildRoute(*FocusGame, FocusPawn ? FocusPawn->GetActorLocation() : FVector::ZeroVector);

		// Host racing: the singleton beacon/guide follow the host's own next
		// checkpoint (dedicated servers render nothing; guests use snapshots).
		if (RaceState == ECitixRaceState::Racing && FocusPC() && JobBeacon)
		{
			for (const FRaceRacer& Racer : RaceRacers)
			{
				if (Racer.PC.Get() == FocusPC() && !Racer.bFinished && !Racer.bDNF
					&& RaceCheckpoints.IsValidIndex(Racer.CheckpointsDone))
				{
					JobBeacon->SetWorldLocation(
						RaceCheckpoints[Racer.CheckpointsDone] + FVector(0.f, 0.f, 3000.f));
					JobBeacon->SetVisibility(true, true);
					RefreshBeaconMaterial();
					break;
				}
			}
		}
	}

	// Replicate each driver's own snapshot so clients render objectives,
	// score, heat and discovery through their player/game states.
	MirrorToReplication();
}
