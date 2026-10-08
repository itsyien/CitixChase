// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/CitixDrivingGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/CitixOnFootPawn.h"
#include "City/CitixCityGenerator.h"
#include "City/CitixCityPlan.h"
#include "City/CitixRoadNetwork.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Pedestrian/CitixPedestrian.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "Player/CitixDrivingHUD.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Player/CitixGameState.h"
#include "Player/CitixPlayerState.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "Sandbox/CitixPoliceVehicle.h"
#include "World/CitixWeatherSystem.h"
#include "TimerManager.h"
#include "Traffic/CitixTrafficSystem.h"
#include "UnrealClient.h"
#include "Vehicle/CitixDriftSmokeComponent.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "World/CitixTimeOfDay.h"
#include "Citix.h"

namespace
{
	/** Headless test helper: move the player pawn somewhere safe and settle it. */
	void CitixTestTeleportPlayer(UWorld* World, const FVector& Destination)
	{
		if (!World)
		{
			return;
		}
		APawn* PlayerPawn = World->GetFirstPlayerController()
			? World->GetFirstPlayerController()->GetPawn() : nullptr;
		if (!PlayerPawn)
		{
			return;
		}
		PlayerPawn->SetActorLocationAndRotation(Destination,
			PlayerPawn->GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(PlayerPawn))
		{
			Vehicle->ResetVehicle();
		}
	}
}

ACitixDrivingGameMode::ACitixDrivingGameMode()
{
	DefaultPawnClass = ACitixVehiclePawn::StaticClass();
	PlayerControllerClass = ACitixDrivingPlayerController::StaticClass();
	HUDClass = ACitixDrivingHUD::StaticClass();
	GameStateClass = ACitixGameState::StaticClass();
	PlayerStateClass = ACitixPlayerState::StaticClass();
}

void ACitixDrivingGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureWorldReady();

	// Headless PvP loop test: aimed server shots at the other driver every few
	// seconds (damage -> death -> respawn + beam), plus one staged ram. Run:
	//   server -CitixPvpTest, C1 -CitixNetWalk, C2 -CitixNetDrive
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixPvpTest")))
	{
		FTimerHandle PvpHandle;
		GetWorldTimerManager().SetTimer(PvpHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			PvpTestTick();
		}), 4.f, true);
	}

	// Isolation test (audit rank 1): A earns/buys/heats while B must not move.
	//   server -CitixIsoTest, C1 + C2 join (any hooks)
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixIsoTest")))
	{
		FTimerHandle IsoHandle;
		GetWorldTimerManager().SetTimer(IsoHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			IsoTestTick();
		}), 5.f, true);
	}

	// Race loop test (audit rank 7): join both drivers, teleport them through
	// checkpoints to prove countdown/progress/finish/rewards end to end.
	//   server -CitixRaceTest, C1 + C2 join (any hooks)
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixRaceTest")))
	{
		FTimerHandle RaceHandle;
		GetWorldTimerManager().SetTimer(RaceHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			RaceTestTick();
		}), 5.f, true);
	}

	// Population probe: live pedestrian count every 5 s (any mode).
	//   -CitixPedProbe
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixPedProbe")))
	{
		FTimerHandle PedProbeHandle;
		GetWorldTimerManager().SetTimer(PedProbeHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
			{
				UE_LOG(LogCitix, Log, TEXT("[Citix] PedProbe: active=%d."),
					(*It)->GetActivePedestrianCount());
				break;
			}
		}), 5.f, true);
	}
}

void ACitixDrivingGameMode::PvpTestTick()
{
	UWorld* World = GetWorld();
	if (!World || !Sandbox)
	{
		return;
	}
	TArray<APlayerController*> PCs;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PCs.Add(PC);
		}
	}
	if (PCs.Num() < 2)
	{
		return;
	}

	// Staged ram (once): park the walker ahead of the driver's nose.
	if (!bPvpRamStaged)
	{
		for (APlayerController* DriverPC : PCs)
		{
			ACitixVehiclePawn* Car = Cast<ACitixVehiclePawn>(DriverPC ? DriverPC->GetPawn() : nullptr);
			if (!Car || Car->GetVelocity().Size() < 500.f)
			{
				continue;
			}
			for (APlayerController* WalkerPC : PCs)
			{
				if (WalkerPC == DriverPC)
				{
					continue;
				}
				ACitixOnFootPawn* Walker = Cast<ACitixOnFootPawn>(WalkerPC ? WalkerPC->GetPawn() : nullptr);
				if (!Walker)
				{
					continue;
				}
				const FVector Nose = Car->GetActorLocation()
					+ Car->GetActorForwardVector() * 700.f;
				Walker->SetActorLocation(Nose + FVector(0.f, 0.f, 100.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				bPvpRamStaged = true;
				UE_LOG(LogCitix, Log, TEXT("[CitixPvp] staged ram: walker ahead of driver."));
				break;
			}
			if (bPvpRamStaged)
			{
				break;
			}
		}
	}

	// Aimed shot: first on-foot driver fires at the other driver's chest.
	// Re-staged every shot (800 cm ahead, facing): walkers drift apart and
	// city geometry blocks long shots, so only close range proves damage.
	for (APlayerController* ShooterPC : PCs)
	{
		APawn* ShooterPawn = ShooterPC ? ShooterPC->GetPawn() : nullptr;
		if (!Cast<ACitixOnFootPawn>(ShooterPawn))
		{
			continue;
		}
		for (APlayerController* TargetPC : PCs)
		{
			if (TargetPC == ShooterPC || !TargetPC->GetPawn())
			{
				continue;
			}
			if (!Cast<ACitixOnFootPawn>(TargetPC->GetPawn()))
			{
				continue;
			}
			const FVector Forward = ShooterPawn->GetActorForwardVector();
			TargetPC->GetPawn()->SetActorLocation(
				ShooterPawn->GetActorLocation() + Forward * 800.f + FVector(0.f, 0.f, 50.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			const FVector Muzzle = ShooterPawn->GetActorLocation() + FVector(0.f, 0.f, 140.f);
			const FVector Chest = TargetPC->GetPawn()->GetActorLocation() + FVector(0.f, 0.f, 100.f);
			const FVector Direction = (Chest - Muzzle).GetSafeNormal();
			if (Sandbox->ResolveShot(ShooterPC, Muzzle, Direction, false))
			{
				UE_LOG(LogCitix, Log, TEXT("[CitixPvp] aimed shot fired."));
			}
			return;
		}
	}
}

void ACitixDrivingGameMode::IsoTestTick()
{
	UWorld* World = GetWorld();
	if (!World || !Sandbox)
	{
		return;
	}
	TArray<APlayerController*> PCs;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PCs.Add(PC);
		}
	}
	if (PCs.Num() < 2)
	{
		return;
	}
	APlayerController* A = PCs[0];
	APlayerController* B = PCs[1];
	auto Snapshot = [](APlayerController* PC)
	{
		ACitixPlayerState* PS = PC && PC->PlayerState ? Cast<ACitixPlayerState>(PC->PlayerState) : nullptr;
		return FString::Printf(TEXT("money=%d owned=%d mag=%d heat=%.0f stars=%d job=%d wp=%d"),
			PS ? PS->Money : -1, PS ? PS->OwnedMask : -1,
			(PS && PS->MagAmmo.IsValidIndex(0)) ? PS->MagAmmo[0] : -1,
			PS ? PS->Heat : -1.f, PS ? PS->WantedStars : -1,
			PS ? (int32)PS->JobState : -1, PS && PS->bHasWaypoint ? 1 : 0);
	};

	++IsoPhase;
	if (IsoPhase == 4)
	{
		// A earns + buys a rifle; B must not move.
		if (FCitixPlayerGame* GameA = Sandbox->FindPlayerGame(A))
		{
			Sandbox->AddMoney(*GameA, 2000, TEXT("isotest"));
			Sandbox->BuyWeapon(A, 2);
			UE_LOG(LogCitix, Log, TEXT("[CitixIso] A earn+buy | A{%s} B{%s}"), *Snapshot(A), *Snapshot(B));
		}
	}
	else if (IsoPhase == 6)
	{
		// A heats up; police must hunt A only.
		if (FCitixPlayerGame* GameA = Sandbox->FindPlayerGame(A))
		{
			Sandbox->AddHeat(*GameA, 90.f, TEXT("isotest"));
			UE_LOG(LogCitix, Log, TEXT("[CitixIso] A heat | A{%s} B{%s}"), *Snapshot(A), *Snapshot(B));
		}
	}
	else if (IsoPhase >= 8 && IsoPhase % 2 == 0)
	{
		// Shop isolation (phase 8): remote buy denied first (too far), then
		// staged at the keeper for a real purchase; B must not move.
		if (IsoPhase == 8 && Sandbox->GetShops().Num() > 0)
		{
			const bool bRemoteDenied = !Sandbox->BuySelected(A, 0, 1);
			UE_LOG(LogCitix, Log, TEXT("[CitixIso] remote buy denied=%d (expect 1)."), bRemoteDenied ? 1 : 0);
			if (APawn* PawnA = A->GetPawn())
			{
				PawnA->SetActorLocation(Sandbox->GetShops()[0].Location + FVector(0.f, 0.f, 100.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (FCitixPlayerGame* GameA = Sandbox->FindPlayerGame(A))
			{
				Sandbox->AddMoney(*GameA, 5000, TEXT("isotest"));
				Sandbox->BuySelected(A, 0, 1);
				UE_LOG(LogCitix, Log, TEXT("[CitixIso] A shop buy | A{%s} B{%s}"), *Snapshot(A), *Snapshot(B));
			}
		}
		// Dual suspects (phase 10+): heat B as well; both must carry their own
		// stars and units while escaping independently.
		if (IsoPhase == 10)
		{
			if (FCitixPlayerGame* GameB = Sandbox->FindPlayerGame(B))
			{
				Sandbox->AddHeat(*GameB, 90.f, TEXT("isotest"));
				UE_LOG(LogCitix, Log, TEXT("[CitixIso] B heat | A{%s} B{%s}"), *Snapshot(A), *Snapshot(B));
			}
		}
		// Photo authority (phase 12): server-validated reward from A's own
		// viewpoint; cooldown enforced (second immediate call must fail).
		if (IsoPhase == 12)
		{
			APawn* PawnA = A ? A->GetPawn() : nullptr;
			if (PawnA)
			{
				const FVector ViewLoc = PawnA->GetActorLocation() + FVector(0.f, 0.f, 150.f);
				const FVector ViewDir = PawnA->GetActorForwardVector().GetSafeNormal();
				const bool bFirst = Sandbox->ResolvePhoto(A, ViewLoc, ViewDir);
				const bool bSecond = Sandbox->ResolvePhoto(A, ViewLoc, ViewDir);
				UE_LOG(LogCitix, Log, TEXT("[CitixIso] photo first=%d second=%d (expect 1, 0)."),
					bFirst ? 1 : 0, bSecond ? 1 : 0);
			}
		}
		// Report attribution: stars per player + suspect of each unit.
		FString Units;
		for (const FCitixPoliceUnit& Unit : Sandbox->GetPoliceUnits())
		{
			AController* Suspect = Unit.SuspectPC.Get();
			FString Name = Suspect && Suspect->PlayerState
				? Suspect->PlayerState->GetPlayerName() : TEXT("none");
			Units += FString::Printf(TEXT("[suspect=%s]"), *Name);
		}
		UE_LOG(LogCitix, Log, TEXT("[CitixIso] suspects A{%s} B{%s} units=%s"),
			*Snapshot(A), *Snapshot(B), *Units);
	}
}

void ACitixDrivingGameMode::RaceTestTick()
{
	UWorld* World = GetWorld();
	if (!World || !Sandbox)
	{
		return;
	}
	TArray<APlayerController*> PCs;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PCs.Add(PC);
		}
	}
	if (PCs.Num() < 2)
	{
		return;
	}
	// Join both (idempotent: JoinRace toggles, so only join the unjoined).
	for (APlayerController* PC : PCs)
	{
		if (!Sandbox->IsRacing(PC))
		{
			Sandbox->JoinRace(PC);
		}
	}
	// Walk every unfinished racer onto their next checkpoint.
	for (APlayerController* PC : PCs)
	{
		if (Sandbox->TestTeleportRacerToCheckpoint(PC))
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixRace] advanced a racer."));
		}
	}
}

void ACitixDrivingGameMode::WeaponTestTick()
{
	if (!WeaponTestArmed)
	{
		return;
	}
	UWorld* World = GetWorld();
	ACitixSandboxDirector* TestSandbox = nullptr;
	for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It) { TestSandbox = *It; break; }
	APawn* PlayerPawn = World ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!TestSandbox || !PlayerPawn)
	{
		return;
	}
	WeaponTestPhaseTime += 1.f;
	if (WeaponTestPhase == 0)
	{
		TestSandbox->TestGiveMoney(3000);
		const bool bBought = TestSandbox->TestBuyWeapon(0);
		const bool bFirst = TestSandbox->FireWeapon();
		UE_LOG(LogCitix, Log, TEXT("[Citix] Weapon test: bought=%d shot=%d mag=%d/%d funds=$%d"),
			bBought ? 1 : 0, bFirst ? 1 : 0,
			TestSandbox->GetMagAmmo(0), TestSandbox->GetReserveAmmo(0), TestSandbox->GetMoney());
		WeaponTestPhase = 1;
		WeaponTestPhaseTime = 0.f;
	}
	else if (WeaponTestPhase == 1 && WeaponTestPhaseTime >= 2.f)
	{
		const bool bSecond = TestSandbox->FireWeapon();
		TestSandbox->SetAdsHeld(true);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Weapon test: second shot=%d mag=%d spread=%.2f ads=%.2f"),
			bSecond ? 1 : 0, TestSandbox->GetMagAmmo(0),
			TestSandbox->GetCurrentSpreadDegrees(), TestSandbox->GetAdsAmount());
		WeaponTestPhase = 2;
		WeaponTestPhaseTime = 0.f;
	}
	else if (WeaponTestPhase == 2 && WeaponTestPhaseTime >= 4.f)
	{
		// Population has built up by now: reactions must be observable.
		TestSandbox->TestStartleAroundPlayer();
		TestSandbox->SetAdsHeld(false);
		const int32 LethalKills = TestSandbox->TestGunHit();
		const bool bPass = TestSandbox->IsWeaponOwned(0)
			&& TestSandbox->GetMagAmmo(0) < 12
			&& TestSandbox->GetMoney() == 3000 - 250
			&& LethalKills >= 1;
		UE_LOG(LogCitix, Log, TEXT("[Citix] Weapon test: %s (owned=%d mag=%d funds=$%d ads=%.2f lethality=%d)."),
			bPass ? TEXT("PASS") : TEXT("FAIL"),
			TestSandbox->IsWeaponOwned(0) ? 1 : 0,
			TestSandbox->GetMagAmmo(0), TestSandbox->GetMoney(), TestSandbox->GetAdsAmount(),
			LethalKills);
		WeaponTestArmed = false;
	}
}

void ACitixDrivingGameMode::EnsureWorldReady()
{
	if (bWorldReady)
	{
		return;
	}
	bWorldReady = true;

	if (bAutoCreateLighting)
	{
		EnsureLighting();
	}
	if (bAutoCreateTimeOfDay)
	{
		EnsureTimeOfDay();
	}
	if (bAutoCreateCity)
	{
		EnsureCityGenerator();
	}
	EnsureSandbox();
	EnsureWeather();
	EnsurePlayerStart();

	// Headless smoke test: generate, report, and exit. Run with:
	//   Citix.exe <map> -game -nullrhi -log -CitixSelfTest
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixSelfTest")))
	{
		const int32 Instances = CityGenerator ? CityGenerator->GetTotalInstanceCount() : 0;
		UE_LOG(LogCitix, Display, TEXT("[Citix] SELF-TEST PASS: generated=%s instances=%d"),
			(CityGenerator && CityGenerator->IsGenerated()) ? TEXT("yes") : TEXT("no"), Instances);
		if (GLog)
		{
			GLog->Flush();
		}
		FGenericPlatformMisc::RequestExit(false);
		return;
	}

	// Automated screenshot: render the city, save a PNG, then exit.
	//   -CitixScreenshot                              overview camera (whole city)
	//   -CitixScreenshot -CitixScreenshotChaseOnly    player camera (street level)
	//   -CitixScreenshot -CitixDriveTest              hold throttle, then screenshot
	//   -CitixScreenshot -CitixDriftTest              throttle + handbrake drift
	//   -CitixScreenshot -CitixOnFootTest             step out of the car first
	//   -CitixScreenshot -CitixWalkTest               step out, then hold W and measure
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixScreenshot")))
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		const bool bChaseOnly = FParse::Param(FCommandLine::Get(), TEXT("CitixScreenshotChaseOnly"));
		const bool bDriftTest = FParse::Param(FCommandLine::Get(), TEXT("CitixDriftTest"));
		const bool bOnFootTest = FParse::Param(FCommandLine::Get(), TEXT("CitixOnFootTest"));
		const bool bWalkTest = FParse::Param(FCommandLine::Get(), TEXT("CitixWalkTest"));
		const bool bDriveTest = FParse::Param(FCommandLine::Get(), TEXT("CitixDriveTest"));
		const bool bCornerTest = FParse::Param(FCommandLine::Get(), TEXT("CitixCornerTest"));
		const bool bReenterTest = FParse::Param(FCommandLine::Get(), TEXT("CitixReenterTest"));
		const bool bRagdollTest = FParse::Param(FCommandLine::Get(), TEXT("CitixRagdollTest"));
		const bool bSandboxTest = FParse::Param(FCommandLine::Get(), TEXT("CitixSandboxTest"));
		const bool bActivityTest = FParse::Param(FCommandLine::Get(), TEXT("CitixActivityTest"));
		const bool bWeaponTest = FParse::Param(FCommandLine::Get(), TEXT("CitixWeaponTest"));
		const bool bWantedTest = FParse::Param(FCommandLine::Get(), TEXT("CitixWantedTest"));
		const bool bBlastTest = FParse::Param(FCommandLine::Get(), TEXT("CitixBlastTest"));
		const bool bSkylineView = FParse::Param(FCommandLine::Get(), TEXT("CitixSkylineView"));
		const bool bLeaveVehicle = bOnFootTest || bWalkTest;

		// Switch cameras only after possession, otherwise the pawn overrides it.
		const bool bCarInspect = FParse::Param(FCommandLine::Get(), TEXT("CitixCarInspect"));
		float WaterViewHeight = 3400.f;
		const bool bWaterView = FParse::Param(FCommandLine::Get(), TEXT("CitixWaterView"))
			|| FParse::Value(FCommandLine::Get(), TEXT("CitixWaterView="), WaterViewHeight);
		if (bSkylineView)
		{
			FTimerHandle SkylineHandle;
			World->GetTimerManager().SetTimer(SkylineHandle, this,
				&ACitixDrivingGameMode::SetupSkylineCamera, 2.5f, false);
		}
		else if (bWaterView)
		{
			// Look along the river, which is the view that shows the water off.
			FTimerHandle WaterHandle;
			World->GetTimerManager().SetTimer(WaterHandle, this,
				&ACitixDrivingGameMode::SetupWaterCamera, 2.5f, false);
		}
		else if (bCarInspect)
		{
			FTimerHandle InspectHandle;
			World->GetTimerManager().SetTimer(InspectHandle, this,
				&ACitixDrivingGameMode::SetupCarInspectCamera, 2.0f, false);
		}
		else if (!bChaseOnly && !bLeaveVehicle && !bDriftTest && !bRagdollTest)
		{
			FTimerHandle CameraHandle;
			World->GetTimerManager().SetTimer(CameraHandle, this,
				&ACitixDrivingGameMode::SetupOverviewCamera, 2.5f, false);
		}

		const float CaptureDelay = [&]() -> float
		{
			// Explicit override so the ragdoll sequence can be captured at any stage.
			float Override = 0.f;
			if (FParse::Value(FCommandLine::Get(), TEXT("CitixCaptureAt="), Override) && Override > 0.f)
			{
				return Override;
			}
			return bSandboxTest ? 7.2f
				: (bActivityTest ? 9.5f
				: (bWeaponTest ? 6.5f
				: (bWantedTest ? 12.5f
				: (bBlastTest ? 7.0f
				: (bDriftTest ? 4.9f
				: (bReenterTest ? 7.5f : (bRagdollTest ? 4.6f
					: ((bLeaveVehicle || bCornerTest) ? 6.5f : 9.0f))))))));
		}();
		FTimerHandle CaptureHandle;
		World->GetTimerManager().SetTimer(CaptureHandle, this,
			&ACitixDrivingGameMode::CaptureScreenshotAndExit, CaptureDelay, false);

		// Walk test: inject a real "W" key so the movement axis mapping is exercised.
		if (bWalkTest)
		{
			FTimerHandle PressHandle;
			World->GetTimerManager().SetTimer(PressHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
				{
					PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
					UE_LOG(LogCitix, Log, TEXT("[Citix] Walk test: W pressed."));
				}
			}), 3.2f, false);
		}

		// Optional: start the run in a specific car type, so each type's handling can be
		// measured with the same test. Example: -CitixCar=Bus
		FString CarName;
		if (FParse::Value(FCommandLine::Get(), TEXT("CitixCar="), CarName))
		{
			FTimerHandle CarHandle;
			World->GetTimerManager().SetTimer(CarHandle, FTimerDelegate::CreateWeakLambda(this, [this, CarName]()
			{
				ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
				if (!Vehicle)
				{
					return;
				}
				for (int32 Index = 0; Index < static_cast<int32>(ECitixCarType::Count); ++Index)
				{
					const ECitixCarType Type = static_cast<ECitixCarType>(Index);
					if (CarName.Equals(FCitixCarLibrary::GetTypeName(Type), ESearchCase::IgnoreCase))
					{
						Vehicle->SetCarAppearance(Type, Vehicle->GetPaintColor());
						UE_LOG(LogCitix, Log, TEXT("[Citix] Test car set to %s.")
							, FCitixCarLibrary::GetTypeName(Type));
						break;
					}
				}
			}), 1.4f, false);
		}

		// Drive test: let the car settle, then hold full throttle.
		if (bDriveTest || bDriftTest || bCornerTest)
		{
			FTimerHandle DriveHandle;
			World->GetTimerManager().SetTimer(DriveHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
				{
					if (UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
					{
						Movement->SetThrottleInput(1.f);
						Movement->SetBoostInput(true);
						UE_LOG(LogCitix, Log, TEXT("[Citix] Drive test: full throttle + boost applied."));
					}
				}
			}), 2.0f, false);
		}

		// Drift test: add steering then the handbrake to break traction.
		if (bDriftTest)
		{
			FTimerHandle DriftHandle;
			World->GetTimerManager().SetTimer(DriftHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
				{
					if (UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
					{
						// Moderate lock: a power drift should stay on the road. Full lock
						// puts the car into a building and hides the physics behind a crash.
						Movement->SetSteeringInput(0.45f);
						UE_LOG(LogCitix, Log, TEXT("[Citix] Drift test: steering 0.45 applied."));
					}
				}
			}), 3.0f, false);

			// Speed series across the drift. The hull fraction is logged alongside so a
			// crash (which also scrubs speed violently) cannot be mistaken for the tyres.
			for (int32 Sample = 0; Sample < 9; ++Sample)
			{
				const float SampleTime = 4.0f + Sample * 0.1f;
				FTimerHandle SampleHandle;
				World->GetTimerManager().SetTimer(SampleHandle, FTimerDelegate::CreateWeakLambda(this, [this, SampleTime]()
				{
					if (const ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
						GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
					{
						if (const UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
						{
							UE_LOG(LogCitix, Log, TEXT("[Citix] Drift series t=%.1f: %6.1f km/h, tyreSlip=%4.0f, hull=%3.0f%%"),
								SampleTime, Movement->GetSpeedKmh(), Movement->GetMaxLateralSlipSpeed(),
								Movement->GetHealthFraction() * 100.f);
						}
					}
				}), SampleTime, false);
			}

			FTimerHandle HandbrakeHandle;
			World->GetTimerManager().SetTimer(HandbrakeHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
				{
					if (UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
					{
						Movement->SetHandbrake(true);
						UE_LOG(LogCitix, Log, TEXT("[Citix] Drift test: handbrake at %.1f km/h."),
							Movement->GetSpeedKmh());
					}
				}
			}), 4.0f, false);
		}

		// Corner test: steady throttle plus a fixed steering input, then measure the
		// body slip angle (how much the car slides wide instead of tracking its nose).		if (bCornerTest)
		{
			FTimerHandle CornerHandle;
			World->GetTimerManager().SetTimer(CornerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
				{
					if (UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
					{
						Movement->SetSteeringInput(0.55f);
						UE_LOG(LogCitix, Log, TEXT("[Citix] Corner test: steering 0.55 applied."));
					}
				}
			}), 3.8f, false);
		}

		// Re-enter test: exit the car, get back in, then hold throttle. Catches the
		// bug where leaving the car parked it with the handbrake still applied.
		if (bReenterTest)
		{
			FTimerHandle ExitHandle;
			World->GetTimerManager().SetTimer(ExitHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixDrivingPlayerController* PC = Cast<ACitixDrivingPlayerController>(
					GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr))
				{
					PC->RequestExitVehicle();
					UE_LOG(LogCitix, Log, TEXT("[Citix] Re-enter test: exited vehicle."));
				}
			}), 2.0f, false);

			FTimerHandle EnterHandle;
			World->GetTimerManager().SetTimer(EnterHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixDrivingPlayerController* PC = Cast<ACitixDrivingPlayerController>(
					GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr))
				{
					PC->RequestEnterVehicle();
					UE_LOG(LogCitix, Log, TEXT("[Citix] Re-enter test: entered vehicle."));
				}
			}), 3.8f, false);

			FTimerHandle ThrottleHandle;
			World->GetTimerManager().SetTimer(ThrottleHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
				{
					if (UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
					{
						Movement->SetThrottleInput(1.f);
						UE_LOG(LogCitix, Log, TEXT("[Citix] Re-enter test: full throttle applied."));
					}
				}
			}), 5.0f, false);
		}

		// Knocked-down test: everyone near the player is hit. At a survivable impact speed
		// they should all ragdoll and then get back up; a lethal hit should fade them away.
		// Verify with the log at 2 s, 3 s and 7 s, and screenshot any stage with
		// -CitixCaptureAt=<seconds>.
		if (FParse::Param(FCommandLine::Get(), TEXT("CitixRagdollTest")))
		{
			const bool bLethal = FParse::Param(FCommandLine::Get(), TEXT("CitixKillTest"));
			const float ImpactSpeed = bLethal ? 3000.f : 700.f;

			FTimerHandle HitHandle;
			World->GetTimerManager().SetTimer(HitHandle, FTimerDelegate::CreateWeakLambda(this, [this, ImpactSpeed, bLethal]()
			{
				const APawn* PlayerPawn = GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
				if (!PlayerPawn)
				{
					return;
				}
				for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
				{
					// Wide enough to catch several pedestrians, but the framing camera picks
					// out the nearest body so the screenshot is still a close-up.
					const int32 Hit = It->HitPedestriansInRadius(PlayerPawn->GetActorLocation(),
						10000.f, FVector(600.f, 600.f, 900.f), ImpactSpeed);
					UE_LOG(LogCitix, Log, TEXT("[Citix] %s test: %d hit near the player, %d ragdolling, %d recovered."),
						bLethal ? TEXT("Lethal") : TEXT("Knock-down"), Hit, It->GetRagdollCount(),
						It->GetRecoveredCount());
					break;
				}
			}), 4.0f, false);

			FTimerHandle MidHandle;
			World->GetTimerManager().SetTimer(MidHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
				{
					UE_LOG(LogCitix, Log, TEXT("[Citix] Ragdoll mid: %d ragdolling, %d recovered."),
						It->GetRagdollCount(), It->GetRecoveredCount());
					break;
				}
			}), 5.2f, false);

			FTimerHandle SettledHandle;
			World->GetTimerManager().SetTimer(SettledHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It)
				{
					UE_LOG(LogCitix, Log, TEXT("[Citix] Ragdoll end: %d ragdolling, %d recovered, %d active walkers."),
						It->GetRagdollCount(), It->GetRecoveredCount(), It->GetActivePedestrianCount());
					break;
				}
			}), 9.0f, false);

			// Lock a camera onto a freshly knocked-down body shortly after the hit, so
			// every capture shows the same person through fall -> get-up -> walking.
			FTimerHandle RagdollCameraHandle;
			World->GetTimerManager().SetTimer(RagdollCameraHandle, this,
				&ACitixDrivingGameMode::SetupRagdollCamera, 4.3f, false);
		}

		// Sandbox test: exercise discovery, waypoint routing, delivery jobs, the car
		// swap and the full map without a human at the keyboard.
		if (bSandboxTest)
		{
			FTimerHandle SandboxHandle;
			World->GetTimerManager().SetTimer(SandboxHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					UE_LOG(LogCitix, Warning, TEXT("[Citix] Sandbox test: no director found."));
					return;
				}
				UE_LOG(LogCitix, Log, TEXT("[Citix] Sandbox test: POIs=%d discovered=%d"),
					Sandbox->GetPOICount(), Sandbox->GetDiscoveredCount());
				Sandbox->CycleWaypoint();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Sandbox test: waypoint '%s' at %s, route points=%d"),
					*Sandbox->GetWaypointName(), *Sandbox->GetWaypoint().ToCompactString(),
					Sandbox->GetRoutePoints().Num());
				Sandbox->StartDeliveryJob();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Sandbox test: job state=%d destination=%s time=%.1fs"),
					static_cast<int32>(Sandbox->GetJobState()),
					*Sandbox->GetJobDestination().ToCompactString(), Sandbox->GetJobTimeRemaining());
			}), 2.5f, false);

			FTimerHandle SandboxMapHandle;
			World->GetTimerManager().SetTimer(SandboxMapHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixDrivingPlayerController* PC = Cast<ACitixDrivingPlayerController>(
					GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr))
				{
					PC->CyclePlayerCar();
					PC->ToggleMap();
					UE_LOG(LogCitix, Log, TEXT("[Citix] Sandbox test: car is now %s, map open=%s"),
						*(PC->GetPawn() ? PC->GetPawn()->GetClass()->GetName() : FString(TEXT("none"))),
						PC->IsMapOpen() ? TEXT("yes") : TEXT("no"));
				}
			}), 3.6f, false);
		}

		// Activity slice test: discover -> start -> navigate -> finish -> score once.
		// Teleports the player onto each stop (within arrival radius) and verifies the
		// full vertical slice through the log: type, stops, completion, score.
		if (bActivityTest)
		{
			FTimerHandle ActivityStartHandle;
			World->GetTimerManager().SetTimer(ActivityStartHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					UE_LOG(LogCitix, Warning, TEXT("[Citix] Activity test: no director found."));
					return;
				}
				Sandbox->TestForceDiscovery(3);
				const bool bStarted = Sandbox->StartActivity();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Activity test: started=%d type=%d stops=%d score=%d"),
					bStarted ? 1 : 0, static_cast<int32>(Sandbox->GetActivityType()),
					Sandbox->GetActivityStopsTotal(), Sandbox->GetScore());
			}), 2.0f, false);

			// Walk the stops: teleport onto the live waypoint, which the activity
			// advances after each arrival (tour) or completes (drive).
			for (int32 Hop = 0; Hop < 4; ++Hop)
			{
				FTimerHandle HopHandle;
				World->GetTimerManager().SetTimer(HopHandle, FTimerDelegate::CreateWeakLambda(this, [this, Hop]()
				{
					ACitixSandboxDirector* Sandbox = nullptr;
					for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
					APawn* PlayerPawn = GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
					if (!Sandbox || !PlayerPawn || !Sandbox->HasWaypoint())
					{
						return;
					}
					CitixTestTeleportPlayer(GetWorld(), Sandbox->GetWaypoint());
					UE_LOG(LogCitix, Log, TEXT("[Citix] Activity test: hop %d to '%s' (stop %d/%d guide=%d)."),
						Hop, *Sandbox->GetWaypointName(),
						Sandbox->GetActivityStopsDone(), Sandbox->GetActivityStopsTotal(),
						Sandbox->GetGuideMarkers());
				}), 2.5f + Hop * 1.5f, false);
			}

			// Mid-flow cancel + clean restart, then photo scoring on the new leg.
			FTimerHandle ActivityRestartHandle;
			World->GetTimerManager().SetTimer(ActivityRestartHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				Sandbox->CancelActivity();
				const bool bRestarted = Sandbox->StartActivity();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Activity test: cancel/restart=%d type=%d stops=%d"),
					bRestarted ? 1 : 0, static_cast<int32>(Sandbox->GetActivityType()),
					Sandbox->GetActivityStopsTotal());
			}), 3.2f, false);

			FTimerHandle ActivityPhotoHandle;
			World->GetTimerManager().SetTimer(ActivityPhotoHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				const bool bPhoto = Sandbox->CapturePhoto();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Activity test: photo=%d score=%d"),
					bPhoto ? 1 : 0, Sandbox->GetLastPhotoScore());
			}), 7.0f, false);

			FTimerHandle ActivityFinalHandle;
			World->GetTimerManager().SetTimer(ActivityFinalHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				const bool bDone = Sandbox->GetActivityState() == ECitixActivityState::Complete;
				UE_LOG(LogCitix, Log, TEXT("[Citix] Activity test: %s (state=%d stops=%d/%d score=%d discovered=%d photos=%d guide=%d)."),
					bDone ? TEXT("PASS") : TEXT("FAIL"),
					static_cast<int32>(Sandbox->GetActivityState()),
					Sandbox->GetActivityStopsDone(), Sandbox->GetActivityStopsTotal(),
					Sandbox->GetScore(), Sandbox->GetDiscoveredCount(), Sandbox->GetPhotosTaken(),
					Sandbox->GetGuideMarkers());
			}), 8.0f, false);
		}

		// Weapon economy test: earn -> buy -> fire (ammo) -> reload -> ADS -> reactions.
		// Readiness-gated phases (not wall-clock): the world (city, player,
		// population) must exist before each step, or screenshot-mode timing
		// flakes the verdict.
		if (bWeaponTest)
		{
			WeaponTestArmed = true;
			WeaponTestPhase = 0;
			WeaponTestPhaseTime = 0.f;
			FTimerHandle WeaponPhaseHandle;
			World->GetTimerManager().SetTimer(WeaponPhaseHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				WeaponTestTick();
			}), 1.f, true);
		}

		// Wanted loop test: heat -> stars -> pursuit -> caught -> reset.
		if (bWantedTest)
		{
			FTimerHandle WantedHeatHandle;
			World->GetTimerManager().SetTimer(WantedHeatHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				Sandbox->AddHeat(90.f, TEXT("test"));
				UE_LOG(LogCitix, Log, TEXT("[Citix] Wanted test: heat=90 stars=%d."),
					Sandbox->GetWantedStars());
			}), 2.0f, false);

			FTimerHandle WantedEarlyHandle;
			World->GetTimerManager().SetTimer(WantedEarlyHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				WantedPursuitDistB = Sandbox->GetNearestPursuitRemaining();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Wanted test: early path remaining=%.0f."),
					WantedPursuitDistB);
			}), 4.0f, false);

			FTimerHandle WantedPursuitHandle;
			World->GetTimerManager().SetTimer(WantedPursuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				WantedPursuitDistA = Sandbox->GetNearestPursuitRemaining();
				APawn* TestPawn = GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
				const FVector TestPlayerLoc = TestPawn ? TestPawn->GetActorLocation() : FVector::ZeroVector;
				FString UnitInfo;
				for (const FCitixPoliceUnit& Unit : Sandbox->GetPoliceUnits())
				{
					if (Unit.Car)
					{
						UnitInfo += FString::Printf(TEXT("[d=%.0f path=%d/%d catch=%.1f dis=%d] "),
							FVector::Dist2D(Unit.Car->GetActorLocation(), TestPlayerLoc),
							Unit.PathCursor, Unit.Path.Num(), Unit.CatchTimer,
							Unit.bDisabled ? 1 : 0);
					}
				}
				UE_LOG(LogCitix, Log, TEXT("[Citix] Wanted test: stars=%d police=%d state=%d neardist=%.0f units=%s"),
					Sandbox->GetWantedStars(), Sandbox->GetPoliceCount(),
					static_cast<int32>(Sandbox->GetWantedState()), WantedPursuitDistA, *UnitInfo);
			}), 6.0f, false);

			FTimerHandle WantedCaughtHandle;
			World->GetTimerManager().SetTimer(WantedCaughtHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				Sandbox->TestForceCaught();
			}), 7.0f, false);

			FTimerHandle WantedVerifyHandle;
			World->GetTimerManager().SetTimer(WantedVerifyHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				// Pursuit must visibly close distance on its own (4.0s -> 6.0s),
				// before the forced catch at 7.0s exercises the reset path.
				// Officers spawn + die cleanly on the side.
				const bool bClosing = WantedPursuitDistB > 0.f && WantedPursuitDistA >= 0.f
					&& WantedPursuitDistA < WantedPursuitDistB;
				const bool bPass = Sandbox->GetWantedStars() == 0 && Sandbox->GetPoliceCount() == 0
					&& bClosing && Sandbox->GetOfficers().Num() == 0;
				UE_LOG(LogCitix, Log, TEXT("[Citix] Wanted test: %s (stars=%d police=%d score=%d closing=%.0f->%.0f officers=%d)."),
					bPass ? TEXT("PASS") : TEXT("FAIL"),
					Sandbox->GetWantedStars(), Sandbox->GetPoliceCount(), Sandbox->GetScore(),
					WantedPursuitDistB, WantedPursuitDistA, Sandbox->GetOfficers().Num());
			}), 10.5f, false);

			// Officer lifecycle: spawn one, put it down, watch it fade + release.
			FTimerHandle OfficerHandle;
			World->GetTimerManager().SetTimer(OfficerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				const int32 Spawned = Sandbox->TestSpawnOfficer();
				const bool bDown = Sandbox->TestDamageOfficer();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Wanted test: officer spawn=%d down=%d."),
					Spawned, bDown ? 1 : 0);
			}), 6.5f, false);
		}
		// Blast test: total the player's car, verify the bigger explosion ejects
		// the driver on foot beside the wreck.
		if (bBlastTest)
		{
			FTimerHandle BlastHandle;
			World->GetTimerManager().SetTimer(BlastHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(
					GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
				{
					Vehicle->ApplyVehicleDamage(5000.f);
					UE_LOG(LogCitix, Log, TEXT("[Citix] Blast test: car totalled."));
				}
			}), 2.5f, false);

			FTimerHandle BlastVerifyHandle;
			World->GetTimerManager().SetTimer(BlastVerifyHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixDrivingPlayerController* PC = Cast<ACitixDrivingPlayerController>(
					GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
				const bool bPass = PC && PC->IsOnFoot();
				UE_LOG(LogCitix, Log, TEXT("[Citix] Blast test: %s (driver on foot=%d)."),
					bPass ? TEXT("PASS") : TEXT("FAIL"), bPass ? 1 : 0);
			}), 5.0f, false);
		}

		// On-foot test: leave the vehicle and walk.
		if (bLeaveVehicle)
		{
			FTimerHandle ExitHandle;
			World->GetTimerManager().SetTimer(ExitHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
				{
					if (ACitixDrivingPlayerController* DrivingPC = Cast<ACitixDrivingPlayerController>(PC))
					{
						DrivingPC->RequestExitVehicle();
						UE_LOG(LogCitix, Log, TEXT("[Citix] On-foot test: exited the vehicle."));
					}
				}
			}), 2.5f, false);
		}

		// On-foot gun check: arm the walker so the screenshot proves the prop is
		// held at the chest, not floating, and the muzzle math has a live prop.
		if (bOnFootTest)
		{
			FTimerHandle ArmHandle;
			World->GetTimerManager().SetTimer(ArmHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				Sandbox->TestGiveMoney(3000);
				const bool bBought = Sandbox->TestBuyWeapon(2);
				UE_LOG(LogCitix, Log, TEXT("[Citix] On-foot test: bought rifle=%d current=%d prop=%s."),
					bBought ? 1 : 0, Sandbox->GetCurrentWeapon(), *Sandbox->TestWeaponPropState());
			}), 3.5f, false);

			FTimerHandle PropHandle;
			World->GetTimerManager().SetTimer(PropHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ACitixSandboxDirector* Sandbox = nullptr;
				for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { Sandbox = *It; break; }
				if (!Sandbox)
				{
					return;
				}
				if (APawn* Pawn = GetWorld() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr)
				{
					UE_LOG(LogCitix, Log, TEXT("[Citix] On-foot test: settled prop=%s pawnat=%s."),
						*Sandbox->TestWeaponPropState(), *Pawn->GetActorLocation().ToCompactString());
				}
			}), 6.0f, false);
		}
	}
}

void ACitixDrivingGameMode::SetupOverviewCamera()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CityExtent = CityGenerator ? CityGenerator->GetRoadNetwork().CitySize : 102400.f;
	// A deliberately plan-like view: near top-down, framing the whole river band.
	const float Distance = CityExtent * 0.55f;

	const FVector Location(Distance, -Distance, CityExtent * 1.30f);
	const FRotator Rotation = (FVector::ZeroVector - Location).Rotation();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform(Rotation, Location), Params))
	{
		if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
		{
			CameraComponent->SetFieldOfView(75.f);
		}
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->SetViewTarget(Camera);
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Overview camera at %s"), *Location.ToCompactString());
	}
}

void ACitixDrivingGameMode::BeginRagdollCamera()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	if (!World || !PC || !PlayerPawn)
	{
		return;
	}

	// Lock onto one knocked-down body so every capture shows the same person through the
	// whole fall -> get-up sequence, rather than whichever body happens to be ragdolling.
	ACitixPedestrian* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	for (TActorIterator<ACitixPedestrian> It(World); It; ++It)
	{
		ACitixPedestrian* Pedestrian = *It;
		if (!Pedestrian || Pedestrian->IsHidden() || !Pedestrian->IsRagdolling())
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(PlayerLocation, Pedestrian->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Pedestrian;
		}
	}
	if (!Best)
	{
		UE_LOG(LogCitix, Warning, TEXT("[Citix] Ragdoll camera: no knocked-down body to follow."));
		return;
	}
	RagdollTestTarget = Best;

	// Approach from the player's side: the player is on the road and the building is
	// behind the pavement, so this keeps buildings out of the frame.
	FVector ToPlayer = PlayerLocation - Best->GetActorLocation();
	ToPlayer.Z = 0.f;
	FVector Approach = ToPlayer.GetSafeNormal();
	if (ToPlayer.SizeSquared() < 100.f * 100.f)
	{
		Approach = FVector(-0.7f, -0.7f, 0.f).GetSafeNormal();
	}
	RagdollCameraOffset = Approach * 620.f + FVector(0.f, 0.f, 380.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RagdollTestCamera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform(FRotator::ZeroRotator, Best->GetActorLocation() + RagdollCameraOffset), Params);
	if (!RagdollTestCamera)
	{
		return;
	}
	if (UCameraComponent* CameraComponent = RagdollTestCamera->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(68.f);
	}
	PC->SetViewTarget(RagdollTestCamera);
	UpdateRagdollCamera();

	// Follow it while the sequence plays out.
	FTimerHandle FollowHandle;
	World->GetTimerManager().SetTimer(FollowHandle, this,
		&ACitixDrivingGameMode::UpdateRagdollCamera, 0.05f, true);

	UE_LOG(LogCitix, Log, TEXT("[Citix] Ragdoll camera: following body at %s offset %s."),
		*Best->GetActorLocation().ToCompactString(), *RagdollCameraOffset.ToCompactString());
}

void ACitixDrivingGameMode::UpdateRagdollCamera()
{
	ACitixPedestrian* Target = RagdollTestTarget.Get();
	if (!Target || !RagdollTestCamera)
	{
		return;
	}
	const FVector Body = Target->GetBodyLocation();
	const FVector CameraLocation = Body + RagdollCameraOffset;
	RagdollTestCamera->SetActorLocation(CameraLocation);
	RagdollTestCamera->SetActorRotation((Body + FVector(0.f, 0.f, 55.f) - CameraLocation).Rotation());
}

void ACitixDrivingGameMode::SetupRagdollCamera()
{
	BeginRagdollCamera();
}

void ACitixDrivingGameMode::SetupWaterCamera()
{
	UWorld* World = GetWorld();
	if (!World || !CityGenerator)
	{
		return;
	}
	const FCitixCityPlan& Plan = CityGenerator->GetCityPlan();
	const int32 Count = Plan.RiverPoints.Num();
	if (Count < 4)
	{
		return;
	}

	// Stand on the bank, a little way along the river, and look downstream: that grazing
	// angle is where water reads best. The position avoids the bridge crossings so the
	// view is clear water.
	const int32 Index = FMath::Clamp(FMath::RoundToInt(Count * 0.30f), 1, Count - 2);
	const FVector2D Point = Plan.RiverPoints[Index];
	const FVector2D Tangent = (Plan.RiverPoints[Index + 1] - Plan.RiverPoints[Index - 1]).GetSafeNormal();
	const FVector2D Normal(-Tangent.Y, Tangent.X);
	const float HalfWidth = Plan.GetHalfWidth(static_cast<float>(Index) / FMath::Max(1, Count - 1));

	// Hover over the channel itself and look downstream: a clear view of the water, with
	// the banks and the skyline either side for the reflections to pick up.
	// -CitixWaterView=<cm> overrides the camera height, so the same shot can be taken from
	// a driver's eye line as well as from the air.
	float WaterViewHeight = 3400.f;
	FParse::Value(FCommandLine::Get(), TEXT("CitixWaterView="), WaterViewHeight);
	float CameraHeight = FMath::Clamp(WaterViewHeight, 120.f, 40000.f);
	const float LookAhead = FMath::Lerp(20000.f, 85000.f, FMath::Clamp(WaterViewHeight / 3400.f, 0.f, 1.f));

	const FVector2D CameraXY = Point + Normal * (HalfWidth * 0.15f);
	const FVector2D LookXY = Point + Tangent * LookAhead - Normal * (HalfWidth * 0.35f);
	const FVector Location(CameraXY.X, CameraXY.Y, CameraHeight);
	const FVector Target(LookXY.X, LookXY.Y, 150.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform((Target - Location).Rotation(), Location), Params))
	{
		if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
		{
			CameraComponent->SetFieldOfView(62.f);
		}
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->SetViewTarget(Camera);
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Water camera at %s looking down the river.")
			, *Location.ToCompactString());
	}
}

void ACitixDrivingGameMode::SetupSkylineCamera()
{
	UWorld* World = GetWorld();
	if (!World || !CityGenerator)
	{
		return;
	}
	const FCitixCityPlan& Plan = CityGenerator->GetCityPlan();
	for (const FCitixPlanDistrict& District : Plan.Districts)
	{
		if (District.Type != ECitixDistrict::Financial || District.Zone != ECitixPlanZone::PrimaryCluster)
		{
			continue;
		}
		const float U = (District.UMin + District.UMax) * 0.5f;
		FVector2D RiverPoint, Tangent;
		Plan.GetRiverFrame(U, RiverPoint, Tangent);
		const FVector2D Normal(-Tangent.Y, Tangent.X);
		const float HalfWidth = Plan.GetHalfWidth(U);
		FVector2D HeroCentre = District.Centroid;
		const TArray<FVector>& Heroes = CityGenerator->GetLandmarkLocations();
		if (Heroes.Num() > 0)
		{
			HeroCentre = FVector2D::ZeroVector;
			for (const FVector& Hero : Heroes)
			{
				HeroCentre += FVector2D(Hero.X, Hero.Y);
			}
			HeroCentre /= Heroes.Num();
		}
		const FVector2D CameraXY = RiverPoint - Normal * static_cast<float>(District.Side) * (HalfWidth + 90000.f);
		const FVector Location(CameraXY.X, CameraXY.Y, 32000.f);
		const FVector Target(HeroCentre.X, HeroCentre.Y, 13500.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),
			FTransform((Target - Location).Rotation(), Location), Params))
		{
			if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
			{
				CameraComponent->SetFieldOfView(42.f);
			}
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->SetViewTarget(Camera);
			}
			UE_LOG(LogCitix, Log, TEXT("[Citix] Skyline camera at %s"), *Location.ToCompactString());
		}
		return;
	}
}

void ACitixDrivingGameMode::SetupCarInspectCamera()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	const FVector CarLocation = Pawn->GetActorLocation();
	// Behind-left and above: shows both left wheels and the rear of the car.
	const FVector CameraLocation = CarLocation + FVector(-520.f, -620.f, 250.f);
	const FRotator CameraRotation = (CarLocation - CameraLocation).Rotation();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform(CameraRotation, CameraLocation), Params))
	{
		if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
		{
			CameraComponent->SetFieldOfView(42.f);
		}
		if (PC)
		{
			PC->SetViewTarget(Camera);
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Car inspect camera at %s"), *CameraLocation.ToCompactString());
	}
}

void ACitixDrivingGameMode::CaptureScreenshotAndExit()
{
	const bool bChaseOnly = FParse::Param(FCommandLine::Get(), TEXT("CitixScreenshotChaseOnly"));
	const bool bDriftTest = FParse::Param(FCommandLine::Get(), TEXT("CitixDriftTest"));
	const bool bOnFootTest = FParse::Param(FCommandLine::Get(), TEXT("CitixOnFootTest"));
	const bool bWalkTest = FParse::Param(FCommandLine::Get(), TEXT("CitixWalkTest"));
	const bool bDriveTest = FParse::Param(FCommandLine::Get(), TEXT("CitixDriveTest"));
	const bool bSkylineView = FParse::Param(FCommandLine::Get(), TEXT("CitixSkylineView"));
	const bool bCornerTest = FParse::Param(FCommandLine::Get(), TEXT("CitixCornerTest"));
	const bool bReenterTest = FParse::Param(FCommandLine::Get(), TEXT("CitixReenterTest"));

	FString ScreenshotName = TEXT("CitixOverview");
	if (bDriftTest)       { ScreenshotName = TEXT("CitixDrift"); }
	else if (bCornerTest) { ScreenshotName = TEXT("CitixCorner"); }
	else if (bReenterTest) { ScreenshotName = TEXT("CitixReenter"); }
	else if (FParse::Param(FCommandLine::Get(), TEXT("CitixCarInspect"))) { ScreenshotName = TEXT("CitixCarInspect"); }
	else if (bWalkTest)   { ScreenshotName = TEXT("CitixWalk"); }
	else if (bOnFootTest) { ScreenshotName = TEXT("CitixOnFoot"); }
	else if (bChaseOnly || bDriveTest) { ScreenshotName = TEXT("CitixChase"); }
	else if (bSkylineView) { ScreenshotName = TEXT("CitixSkyline"); }

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	// --- Report player state so behaviour is verifiable from logs alone ---
	if (const ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(Pawn))
	{
		if (const UCitixVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement())
		{
			const UCitixDriftSmokeComponent* Smoke = Vehicle->FindComponentByClass<UCitixDriftSmokeComponent>();
			// Body slip angle: the angle between where the car points and where it is
			// actually travelling. Small = planted, large = sliding wide.
			float SlipAngleDegrees = 0.f;
			const FVector Velocity = Vehicle->GetVelocity();
			const FVector PlanarVelocity(Velocity.X, Velocity.Y, 0.f);
			if (PlanarVelocity.SizeSquared() > 100.f)
			{
				const FVector VelocityDirection = PlanarVelocity.GetSafeNormal();
				const float SinSlip = FMath::Clamp(
					FVector::DotProduct(VelocityDirection, Vehicle->GetActorRightVector()), -1.f, 1.f);
				SlipAngleDegrees = FMath::RadiansToDegrees(FMath::Asin(SinSlip));
			}

			UE_LOG(LogCitix, Log,
				TEXT("[Citix] Vehicle: %.1f km/h, grounded=%s, bodySlip=%.1f deg, tyreSlip=%.0f cm/s, smokePuffs=%d, pos=%s"),
				Movement->GetSpeedKmh(),
				Movement->IsGrounded() ? TEXT("yes") : TEXT("no"),
				SlipAngleDegrees,
				Movement->GetMaxLateralSlipSpeed(),
				Smoke ? Smoke->GetActivePuffCount() : 0,
				*Vehicle->GetActorLocation().ToCompactString());
		}
	}
	else if (const ACitixOnFootPawn* OnFoot = Cast<ACitixOnFootPawn>(Pawn))
	{
		// Movement alignment: 1.0 means the character walks straight where the camera looks.
		float Alignment = 0.f;
		if (PC)
		{
			const FVector CameraForward = FRotationMatrix(
				FRotator(0.f, PC->GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::X);
			const FVector Velocity2D = FVector(OnFoot->GetVelocity().X, OnFoot->GetVelocity().Y, 0.f);
			if (Velocity2D.SizeSquared() > 1.f)
			{
				Alignment = FVector::DotProduct(CameraForward, Velocity2D.GetSafeNormal());
			}
		}

		UE_LOG(LogCitix, Log,
			TEXT("[Citix] On foot: speed=%.1f km/h, camera-forward alignment=%.2f (1.0 = straight ahead), pos=%s"),
			OnFoot->GetVelocity().Size2D() * 0.036f,
			Alignment,
			*OnFoot->GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogCitix, Warning, TEXT("[Citix] No player pawn at capture time."));
	}

	FScreenshotRequest::RequestScreenshot(ScreenshotName, false, false);
	UE_LOG(LogCitix, Log, TEXT("[Citix] Screenshot requested: %s"), *ScreenshotName);

	// --- Ambient population diagnostics ---
	if (World)
	{
		const FVector PawnLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		const FVector2D PlayerXY(PawnLocation.X, PawnLocation.Y);

		for (TActorIterator<ACitixTrafficSystem> It(World); It; ++It)
		{
			It->LogDiagnostics(PlayerXY);
			break;
		}
		for (TActorIterator<ACitixPedestrianSystem> It(World); It; ++It)
		{
			It->LogDiagnostics(PlayerXY);
			break;
		}
	}

	// Give the screenshot a moment, then exit.
	if (World)
	{
		FTimerHandle ExitHandle;
		World->GetTimerManager().SetTimer(ExitHandle, FTimerDelegate::CreateWeakLambda(this, []()
		{
			if (GLog)
			{
				GLog->Flush();
			}
			FGenericPlatformMisc::RequestExit(false);
		}), 4.0f, false);
	}
}

ACitixCityGenerator* ACitixDrivingGameMode::EnsureCityGenerator()
{
	if (CityGenerator)
	{
		return CityGenerator;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ACitixCityGenerator> It(World); It; ++It)
	{
		CityGenerator = *It;
		break;
	}

	if (!CityGenerator)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CityGenerator = World->SpawnActor<ACitixCityGenerator>(
			ACitixCityGenerator::StaticClass(), FTransform::Identity, Params);
		if (CityGenerator)
		{
			// We generate explicitly below so ordering is deterministic.
			CityGenerator->bAutoGenerateOnBeginPlay = false;
			UE_LOG(LogCitix, Log, TEXT("[Citix] Spawned city generator at origin."));
		}
	}

	if (CityGenerator && !CityGenerator->IsGenerated())
	{
		CityGenerator->GenerateCity();
	}

	return CityGenerator;
}

void ACitixDrivingGameMode::EnsureLighting()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bHasDirectionalLight = false;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		bHasDirectionalLight = true;
		break;
	}

	if (bHasDirectionalLight)
	{
		return;
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] No lighting found: creating a bootstrap sky/sun rig."));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), FTransform(FRotator(-48.f, 35.f, 0.f)), Params))
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			LightComponent->SetIntensity(5.0f);
			LightComponent->SetLightColor(FLinearColor(1.0f, 0.96f, 0.9f));
		}
	}

	if (ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, Params))
	{
		if (USkyLightComponent* SkyComponent = SkyLight->GetLightComponent())
		{
			SkyComponent->SetRealTimeCapture(true);
			SkyComponent->SetIntensity(1.0f);
		}
	}

	World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity, Params);

	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(
		AExponentialHeightFog::StaticClass(), FTransform::Identity, Params))
	{
		if (UExponentialHeightFogComponent* FogComponent = Fog->GetComponent())
		{
			FogComponent->SetFogDensity(0.012f);
		}
	}
}

void ACitixDrivingGameMode::EnsureTimeOfDay()
{
	if (TimeOfDay)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACitixTimeOfDay> It(World); It; ++It)
	{
		TimeOfDay = *It;
		break;
	}

	if (!TimeOfDay)
	{
		UClass* Class = TimeOfDayClass ? TimeOfDayClass.Get() : ACitixTimeOfDay::StaticClass();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TimeOfDay = World->SpawnActor<ACitixTimeOfDay>(Class, FTransform::Identity, Params);
		if (TimeOfDay)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Spawned time of day system."));
		}
	}
}

void ACitixDrivingGameMode::EnsureSandbox()
{
	if (Sandbox)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It)
	{
		Sandbox = *It;
		break;
	}

	if (!Sandbox)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Sandbox = World->SpawnActor<ACitixSandboxDirector>(
			ACitixSandboxDirector::StaticClass(), FTransform::Identity, Params);
	}

	// Give it the road graph, landmark positions and city size so it can build POIs,
	// route through the road network and place job destinations.
	if (Sandbox && CityGenerator && CityGenerator->IsGenerated())
	{
		const UCitixCitySettings& Settings = UCitixCitySettings::Get();
		Sandbox->Configure(CityGenerator->GetRoadNetwork(), CityGenerator->GetCityPlan(),
			CityGenerator->GetLandmarkLocations(), Settings.CitySize);
	}
}

void ACitixDrivingGameMode::EnsureWeather()
{
	if (Weather)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACitixWeatherSystem> It(World); It; ++It)
	{
		Weather = *It;
		break;
	}

	if (!Weather)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Weather = World->SpawnActor<ACitixWeatherSystem>(
			ACitixWeatherSystem::StaticClass(), FTransform::Identity, Params);
	}
}

void ACitixDrivingGameMode::EnsurePlayerStart()
{
	if (GeneratedPlayerStart)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Prefer a real PlayerStart if the map already has one away from the origin.
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		APlayerStart* Existing = *It;
		if (Existing && Existing->GetActorLocation().SizeSquared2D() > FMath::Square(5000.f))
		{
			return;
		}
	}

	FVector SpawnLocation(0.f, 0.f, SpawnHeight);
	FRotator SpawnRotation = FRotator::ZeroRotator;

	// Start the player mid-street on a major road, clear of the river.
	if (CityGenerator && CityGenerator->IsGenerated())
	{
		const FTransform Spawn = CityGenerator->GetPlayerSpawnTransform();
		SpawnLocation = Spawn.GetLocation();
		SpawnRotation = FRotator(0.f, Spawn.Rotator().Yaw, 0.f);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GeneratedPlayerStart = World->SpawnActor<APlayerStart>(
		APlayerStart::StaticClass(), FTransform(SpawnRotation, SpawnLocation), Params);

	if (GeneratedPlayerStart)
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] Player start: %s"), *SpawnLocation.ToCompactString());
	}

	// Overflow starts in a lateral line so extra joiners never spawn inside the
	// first player (or each other). Same street, same heading, 9 m apart.
	GeneratedPlayerStarts.Reset();
	GeneratedPlayerStarts.Add(GeneratedPlayerStart);
	const FVector Side = FRotator(0.f, SpawnRotation.Yaw + 90.f, 0.f).Vector();
	for (int32 Index = 1; Index < 8; ++Index)
	{
		const FVector Offset = Side * (900.f * static_cast<float>(Index));
		if (APlayerStart* Extra = World->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), FTransform(SpawnRotation, SpawnLocation + Offset), Params))
		{
			GeneratedPlayerStarts.Add(Extra);
		}
	}
}

AActor* ACitixDrivingGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureWorldReady();

	// Hand each joining controller the first free start. Sticky per controller so
	// rejoining after a short drop does not reshuffle everyone else.
	if (GeneratedPlayerStarts.Num() > 0 && Player)
	{
		for (const TPair<TWeakObjectPtr<AController>, int32>& Claim : PlayerStartClaims)
		{
			if (Claim.Key.Get() == Player)
			{
				const int32 Claimed = Claim.Value;
				if (GeneratedPlayerStarts.IsValidIndex(Claimed) && GeneratedPlayerStarts[Claimed])
				{
					return GeneratedPlayerStarts[Claimed];
				}
			}
		}
		for (int32 Index = 0; Index < GeneratedPlayerStarts.Num(); ++Index)
		{
			bool bTaken = false;
			for (const TPair<TWeakObjectPtr<AController>, int32>& Claim : PlayerStartClaims)
			{
				if (Claim.Value == Index && Claim.Key.IsValid())
				{
					bTaken = true;
					break;
				}
			}
			if (!bTaken && GeneratedPlayerStarts[Index])
			{
				PlayerStartClaims.Add(TPair<TWeakObjectPtr<AController>, int32>(Player, Index));
				UE_LOG(LogCitix, Log, TEXT("[Citix] Player start %d assigned."), Index);
				return GeneratedPlayerStarts[Index];
			}
		}
		PlayerStartClaims.Add(TPair<TWeakObjectPtr<AController>, int32>(Player, 0));
		return GeneratedPlayerStarts[0];
	}

	if (GeneratedPlayerStart)
	{
		return GeneratedPlayerStart;
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ACitixDrivingGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);

	// Rank 21-lite: readable names without a menu (`-CitixName=Ada`).
	// Falls back to the engine default when absent.
	if (NewPlayer && NewPlayer->PlayerState)
	{
		FString JoinName;
		if (FParse::Value(FCommandLine::Get(), TEXT("CitixName="), JoinName) && !JoinName.IsEmpty())
		{
			NewPlayer->PlayerState->SetPlayerName(JoinName.Left(16));
		}
	}

	// Rank 22: session-only progress is an explicit rule, surfaced in-game.
	if (FCitixPlayerGame* JoinGame = Sandbox ? Sandbox->FindPlayerGame(NewPlayer) : nullptr)
	{
		Sandbox->ShowToast(*JoinGame, TEXT("Session-only progress - earn it again next run"));
	}

	// New cars belong to the driver who spawned in them (no joyriding strangers).
	if (APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr)
	{
		if (ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(Pawn))
		{
			Vehicle->SetOwningController(NewPlayer);
		}
	}

	// Multiplayer joins start armed (shared arsenal); single-player stays
	// unarmed until the first gunsmith visit.
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone && Sandbox)
	{
		Sandbox->GrantStarterPistol(NewPlayer);
	}
}

void ACitixDrivingGameMode::Logout(AController* Exiting)
{
	for (int32 Index = PlayerStartClaims.Num() - 1; Index >= 0; --Index)
	{
		if (!PlayerStartClaims[Index].Key.IsValid() || PlayerStartClaims[Index].Key.Get() == Exiting)
		{
			PlayerStartClaims.RemoveAt(Index);
		}
	}
	// Rank 15-lite: a departing driver's pursuit ends, their record clears.
	// Reconnect starts fresh (session-only by design).
	if (Sandbox && Exiting)
	{
		ReleasePoliceUnitsForLeaver(Exiting);
		Sandbox->LeaveRace(Exiting, true);
	}
	// Abandoned cars become free rides (ownership released, parked state kept).
	if (Exiting)
	{
		for (TActorIterator<ACitixVehiclePawn> It(GetWorld()); It; ++It)
		{
			if (ACitixVehiclePawn* Car = *It)
			{
				if (Car->GetOwningController() == Exiting)
				{
					Car->SetOwningController(nullptr);
					Car->SetOccupied(false);
				}
			}
		}
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Player logged out; start claim freed."));
	Super::Logout(Exiting);
}

void ACitixDrivingGameMode::ReleasePoliceUnitsForLeaver(AController* Exiting)
{
	if (Sandbox)
	{
		Sandbox->ReleasePoliceUnits(Exiting);
		Sandbox->RemovePlayerGame(Exiting);
	}
}

void ACitixDrivingGameMode::LogNetState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const ENetMode NetMode = World->GetNetMode();
	FString NetRole = TEXT("standalone");
	if (NetMode == NM_DedicatedServer) { NetRole = TEXT("server"); }
	else if (NetMode == NM_Client) { NetRole = TEXT("client"); }
	else if (NetMode == NM_ListenServer) { NetRole = TEXT("listen"); }

	int32 PlayerCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		++PlayerCount;
	}

	int32 PawnCount = 0;
	FVector OwnPos = FVector::ZeroVector;
	FVector OtherPos = FVector::ZeroVector;
	bool bHasOwn = false;
	bool bHasOther = false;
	APlayerController* FirstPC = World->GetFirstPlayerController();
	APawn* OwnPawn = FirstPC ? FirstPC->GetPawn() : nullptr;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		++PawnCount;
		APawn* Pawn = *It;
		if (!Pawn)
		{
			continue;
		}
		if (!bHasOwn && Pawn == OwnPawn)
		{
			OwnPos = Pawn->GetActorLocation();
			bHasOwn = true;
		}
		else if (!bHasOther)
		{
			OtherPos = Pawn->GetActorLocation();
			bHasOther = true;
		}
	}

	UE_LOG(LogCitix, Log, TEXT("[CitixNet] role=%s pcs=%d pawns=%d own=(%.0f,%.0f,%.0f)%s other=(%.0f,%.0f,%.0f)%s"),
		*NetRole, PlayerCount, PawnCount,
		OwnPos.X, OwnPos.Y, OwnPos.Z, bHasOwn ? TEXT("") : TEXT("?"),
		OtherPos.X, OtherPos.Y, OtherPos.Z, bHasOther ? TEXT("") : TEXT("?"));
}
