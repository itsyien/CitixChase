#include "Player/CitixDrivingPlayerController.h"
#include "Network/CitixSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Chase/CitixIceWave.h"
#include "Chase/CitixIceProbe.h"
#include "Core/CitixGraphicsSettings.h"
#include "Chase/CitixSettingsWidget.h"
#include "Camera/CameraActor.h"
#include "Chase/CitixSmokeCloud.h"
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/CitixDrivingPlayerController.h"
#include "UnrealClient.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Chase/CitixChaseRules.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Chase/CitixChaseLobbyWidget.h"
#include "Sandbox/CitixRouteGuide.h"
#include "Sandbox/CitixRouteHelper.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/CitixOnFootPawn.h"
#include "Player/CitixGameState.h"
#include "Player/CitixPlayerState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Misc/FileHelper.h"
#include "InputModifiers.h"
#include "Misc/Parse.h"
#include "Player/CitixDrivingHUD.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "Sandbox/CitixPoliceVehicle.h"
#include "Sandbox/CitixPoliceOfficer.h"
#include "City/CitixCityGenerator.h"
#include "City/CitixRoadNetwork.h"
#include "Core/CitixCitySettings.h"
#include "World/CitixTimeOfDay.h"
#include "World/CitixWeatherSystem.h"
#include "Traffic/CitixTrafficSystem.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Vehicle/CitixCarLibrary.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "World/CitixTimeOfDay.h"
#include "Citix.h"
#include "Engine/Engine.h"


ACitixDrivingPlayerController::ACitixDrivingPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* Action = CreateDefaultSubobject<UInputAction>(Name);
		Action->ValueType = Type;
		return Action;
	};

	// --- Time-of-day controls (work in a car and on foot) ---------------
	TimeContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("TimeContext"));
	TimeBackAction = MakeAction(TEXT("IA_TimeBack"), EInputActionValueType::Boolean);
	TimeForwardAction = MakeAction(TEXT("IA_TimeForward"), EInputActionValueType::Boolean);
	if (TimeContext)
	{
		TimeContext->MapKey(TimeBackAction, EKeys::LeftBracket);
		TimeContext->MapKey(TimeBackAction, EKeys::Comma);
		TimeContext->MapKey(TimeForwardAction, EKeys::RightBracket);
		TimeContext->MapKey(TimeForwardAction, EKeys::Period);
	}

	// --- Sandbox verbs --------------------------------------------------
	SandboxContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("SandboxContext"));
	MapAction = MakeAction(TEXT("IA_Map"), EInputActionValueType::Boolean);
	WaypointAction = MakeAction(TEXT("IA_Waypoint"), EInputActionValueType::Boolean);
	JobAction = MakeAction(TEXT("IA_Job"), EInputActionValueType::Boolean);
	CycleCarAction = MakeAction(TEXT("IA_CycleCar"), EInputActionValueType::Boolean);
	PhotoAction = MakeAction(TEXT("IA_Photo"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(TEXT("IA_Interact"), EInputActionValueType::Boolean);
	CancelAction = MakeAction(TEXT("IA_Cancel"), EInputActionValueType::Boolean);
	FireAction = MakeAction(TEXT("IA_Fire"), EInputActionValueType::Boolean);
	AdsAction = MakeAction(TEXT("IA_Ads"), EInputActionValueType::Boolean);
	ReloadAction = MakeAction(TEXT("IA_Reload"), EInputActionValueType::Boolean);
	RaceAction = MakeAction(TEXT("IA_Race"), EInputActionValueType::Boolean);
	ShopRow1Action = MakeAction(TEXT("IA_ShopRow1"), EInputActionValueType::Boolean);
	ShopRow2Action = MakeAction(TEXT("IA_ShopRow2"), EInputActionValueType::Boolean);
	ShopRow3Action = MakeAction(TEXT("IA_ShopRow3"), EInputActionValueType::Boolean);
	ShopRow4Action = MakeAction(TEXT("IA_ShopRow4"), EInputActionValueType::Boolean);
	ShopRow5Action = MakeAction(TEXT("IA_ShopRow5"), EInputActionValueType::Boolean);
	CapturePhotoAction = MakeAction(TEXT("IA_CapturePhoto"), EInputActionValueType::Boolean);
	PhotoMoveAction = MakeAction(TEXT("IA_PhotoMove"), EInputActionValueType::Axis2D);
	PhotoUpAction = MakeAction(TEXT("IA_PhotoUp"), EInputActionValueType::Axis1D);
	PhotoLookAction = MakeAction(TEXT("IA_PhotoLook"), EInputActionValueType::Axis2D);

	if (SandboxContext)
	{
		SandboxContext->MapKey(MapAction, EKeys::M);
		SandboxContext->MapKey(WaypointAction, EKeys::G);
		SandboxContext->MapKey(JobAction, EKeys::J);
		SandboxContext->MapKey(CycleCarAction, EKeys::V);
		SandboxContext->MapKey(PhotoAction, EKeys::P);
		SandboxContext->MapKey(InteractAction, EKeys::E);
		SandboxContext->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
		SandboxContext->MapKey(CancelAction, EKeys::X);
		SandboxContext->MapKey(CancelAction, EKeys::Gamepad_FaceButton_Top);
		SandboxContext->MapKey(FireAction, EKeys::LeftMouseButton);
		SandboxContext->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
		SandboxContext->MapKey(AdsAction, EKeys::RightMouseButton);
		SandboxContext->MapKey(AdsAction, EKeys::Gamepad_RightThumbstick);
		SandboxContext->MapKey(ReloadAction, EKeys::T);
		SandboxContext->MapKey(ReloadAction, EKeys::Gamepad_DPad_Up);
		SandboxContext->MapKey(RaceAction, EKeys::Y);
		SandboxContext->MapKey(ShopRow1Action, EKeys::One);
		SandboxContext->MapKey(ShopRow2Action, EKeys::Two);
		SandboxContext->MapKey(ShopRow3Action, EKeys::Three);
		SandboxContext->MapKey(ShopRow4Action, EKeys::Four);
		SandboxContext->MapKey(ShopRow5Action, EKeys::Five);
		SandboxContext->MapKey(CancelAction, EKeys::Escape);
		SandboxContext->MapKey(CapturePhotoAction, EKeys::Enter);
		SandboxContext->MapKey(CapturePhotoAction, EKeys::Gamepad_Special_Right);
		SandboxContext->MapKey(PhotoLookAction, EKeys::Mouse2D);

		// Modifiers must be default subobjects: NewObject is illegal in a constructor.
		int32 ModifierIndex = 0;
		auto MakeModifierMapping = [this, &ModifierIndex](UInputAction* Action, const FKey& Key,
			bool bToY, bool bNegateX, bool bNegateY) -> FEnhancedActionKeyMapping&
		{
			FEnhancedActionKeyMapping& Mapping = SandboxContext->MapKey(Action, Key);
			if (bToY)
			{
				const FName SwizzleName = *FString::Printf(TEXT("PhotoSwizzle_%d"), ModifierIndex++);
				if (UInputModifierSwizzleAxis* Swizzle = CreateDefaultSubobject<UInputModifierSwizzleAxis>(SwizzleName))
				{
					Swizzle->Order = EInputAxisSwizzle::YXZ;
					Mapping.Modifiers.Add(Swizzle);
				}
			}
			if (bNegateX || bNegateY)
			{
				const FName NegateName = *FString::Printf(TEXT("PhotoNegate_%d"), ModifierIndex++);
				if (UInputModifierNegate* Negate = CreateDefaultSubobject<UInputModifierNegate>(NegateName))
				{
					Negate->bX = bNegateX;
					Negate->bY = bNegateY;
					Negate->bZ = false;
					Mapping.Modifiers.Add(Negate);
				}
			}
			return Mapping;
		};

		// Free photo camera: WASD pans, Q/E rises and falls.
		MakeModifierMapping(PhotoMoveAction, EKeys::W, /*bToY*/ true, false, false);
		MakeModifierMapping(PhotoMoveAction, EKeys::S, /*bToY*/ true, false, true);
		MakeModifierMapping(PhotoMoveAction, EKeys::D, false, false, false);
		MakeModifierMapping(PhotoMoveAction, EKeys::A, false, true, false);
		MakeModifierMapping(PhotoUpAction, EKeys::E, false, false, false);
		MakeModifierMapping(PhotoUpAction, EKeys::Q, false, true, false);
	}
}

void ACitixDrivingPlayerController::BeginPlay()
{
	Super::BeginPlay();
 if (IsLocalController()) if(auto* Settings=UCitixGraphicsSettings::Get()) Settings->InitializeForPC();

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	FParse::Value(FCommandLine::Get(), TEXT("CitixNetTag="), NetTag);

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			// Low priority: the active pawn's context wins on any shared keys.
			Subsystem->AddMappingContext(TimeContext, -1);
			if (!IsChaseMode())
			{
				Subsystem->AddMappingContext(SandboxContext, -1);
			}
		}
	}
	RefreshModeInputContexts();

	// Clients build their own deterministic city copy (visuals + collision).
	// GameMode only exists on the server, so the controller bootstraps it here.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		EnsureLocalWorld();
	}
	if (IsChaseMode() && (!FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTest")) || FParse::Param(FCommandLine::Get(),TEXT("CitixLobbyButtons"))))
	{
		ShowChaseLobby();

 }

 if(IsLocalController() && GetGameInstance()) GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>()->NotifyWorldReady();

 // Laptop dual-window testing: two full cities + a server on one GPU stutters
	// regardless of networking. -CitixLowSpec halves render cost on that instance.
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixLowSpec")))
	{
		static const TCHAR* LowSpecCVars[] = {
			TEXT("sg.ResolutionQuality 50"),
			TEXT("sg.ViewDistanceQuality 1"),
			TEXT("sg.ShadowQuality 0"),
			TEXT("sg.PostProcessQuality 0"),
			TEXT("sg.EffectsQuality 1"),
			TEXT("r.MotionBlurQuality 0"),
		};
		if (UEngine* Engine = GEngine)
		{
			for (const TCHAR* CVar : LowSpecCVars)
			{
				Engine->Exec(GetWorld(), CVar);
			}
		}
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] low-spec rendering enabled."), *NetTag);
	}

	// Multiplayer test hooks (run on every instance, including remote clients).
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixNetLog")))
	{
		FTimerHandle NetLogHandle;
		GetWorldTimerManager().SetTimer(NetLogHandle, this,
			&ACitixDrivingPlayerController::LogNetState, 2.f, true);
	}
	// Test hook: walk driver for the multiplayer loop test. Retried: a late
	// joiner may have no pawn yet when the first attempt fires.
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixNetWalk")))
	{
		FTimerHandle NetExitHandle;
		GetWorldTimerManager().SetTimer(NetExitHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!IsOnFoot())
			{
				RequestExitVehicle();
				UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] walk driver exit attempt."), *NetTag);
			}
		}), 2.5f, true);
		FTimerHandle NetWalkHandle;
		GetWorldTimerManager().SetTimer(NetWalkHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
			// Sprint leg: hold Shift too (-CitixNetSprint). Verifies the server
			// runs sprint speed (700) so remotes stop seeing walking (340).
			if (FParse::Param(FCommandLine::Get(), TEXT("CitixNetSprint")))
			{
				InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Pressed, 1.0f));
				UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] walk driver pressed W+Shift (sprint)."), *NetTag);
			}
			else
			{
				UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] walk driver pressed W."), *NetTag);
			}
		}), 3.2f, false);
	}
	// Test hook: shared-verb driver (stage 2 loop test). Client 1 cycles a
	// waypoint, then starts a delivery; the server log shows each verb RPC and
	// every client renders the replicated route/beacon/guide.
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixVerbTest")))
	{
		FTimerHandle VerbWaypointHandle;
		GetWorldTimerManager().SetTimer(VerbWaypointHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			CycleWaypoint();
			const ACitixSandboxDirector* Sandbox = GetSandbox();
			UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] verb test: waypoint sent (local markers=%d)."),
				*NetTag, Sandbox ? Sandbox->GetGuideMarkers() : -1);
		}), 6.f, false);
		FTimerHandle VerbJobHandle;
		GetWorldTimerManager().SetTimer(VerbJobHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			StartDeliveryJob();
			const ACitixSandboxDirector* Sandbox = GetSandbox();
			UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] verb test: job sent (local markers=%d)."),
				*NetTag, Sandbox ? Sandbox->GetGuideMarkers() : -1);
		}), 12.f, false);
	}
	// Opt-in regression: reuse an owned car, then drive using actual local key input.
	if (!HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("CitixReentryProbe")))
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Step=0, Wait=0]() mutable
		{
			const auto* State=GetWorld()->GetGameState<ACitixChaseGameState>();
			if (!State || State->Phase!=ECitixChasePhase::Pursuit || Step>=5) return;
			if (++Wait<3) return;
			if (Step==0) { RequestExitVehicle(); Step=1; Wait=0; }
			else if (Step==1 && Cast<ACitixOnFootPawn>(GetPawn())) { RequestEnterVehicle(); Step=2; Wait=0; }
			else if (Step==2 && Cast<ACitixVehiclePawn>(GetPawn())) { InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f)); Step=3; Wait=0; }
			else if (Step==3) {
				const auto* Car=Cast<ACitixVehiclePawn>(GetPawn());
				const float Speed=Car ? Car->GetDisplaySpeedKmh() : 0.f;
				UE_LOG(LogCitix,Log,TEXT("[CitixReentryProbe] %s local throttle=%.2f speed=%.1f"),Speed>5.f ? TEXT("PASS") : TEXT("FAIL"),Car ? Car->GetVehicleMovement()->GetThrottleInput() : 0.f,Speed);
				const FString Receipt=FString::Printf(TEXT("{\"passed\":%s,\"speed_kmh\":%.2f,\"throttle\":%.2f}"),Speed>5.f ? TEXT("true") : TEXT("false"),Speed,Car ? Car->GetVehicleMovement()->GetThrottleInput() : 0.f);
				FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/TEXT("ChaseReentryProbe.json")));
				InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f)); Step=5;
			}
		}),1.f,true);
	}
	// Test hook: hold throttle in the car (drives the server-RPC input path).
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixNetDrive")))
	{
		FTimerHandle NetDriveHandle;
		GetWorldTimerManager().SetTimer(NetDriveHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
			UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] drive tester holding throttle."), *NetTag);
		}), 3.2f, false);
	}
	// Test hook: hold throttle + handbrake + boost so both pooled particle systems
	// are exercised on every instance (smoke and boost trail).
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixNetDrift")))
	{
		FTimerHandle DriftHandle;
		GetWorldTimerManager().SetTimer(DriftHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>();
			if (Chase && Chase->Phase != ECitixChasePhase::Pursuit) return;
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, IE_Released, 0.f));
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Released, 0.f));
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, IE_Pressed, 1.0f));
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Pressed, 1.0f));
			UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] drift/boost tester: W + Space + Shift held."), *NetTag);
		}), 3.2f, true);
	}
	// Test hook: auto-ready the chase (lobby + rematch). Drives the round state
	// machine without a human so the loop can be verified headlessly.
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixChaseReady")))
	{
		FTimerHandle ReadyHandle;
		GetWorldTimerManager().SetTimer(ReadyHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const ACitixChaseGameState* Chase = GetWorld()
				? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr;
			if (Chase && (Chase->Phase == ECitixChasePhase::Waiting || Chase->Phase == ECitixChasePhase::MatchResults))
			{
				ServerChaseInteract();
				UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] chase auto-ready (%s)."), *NetTag,
					Chase->Phase == ECitixChasePhase::Waiting ? TEXT("lobby") : TEXT("rematch"));
			}
		}), 5.f, true);
	}
}

void ACitixDrivingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
 if (SettingsMenu) { SettingsMenu->RemoveFromParent(); SettingsMenu=nullptr; }
	if (GEngine) { GEngine->OnNetworkFailure().RemoveAll(this); GEngine->OnTravelFailure().RemoveAll(this); }
 if (ChaseRoute) ChaseRoute->Destroy();
	LastShotSerial.Reset();
	AdsCameraBaseFov.Reset();
	if (PhotoCamera)
	{
		PhotoCamera->Destroy();
		PhotoCamera = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ACitixDrivingPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	EnhancedInput->BindAction(TimeBackAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleTimeBackStart);
	EnhancedInput->BindAction(TimeBackAction, ETriggerEvent::Completed, this,
		&ACitixDrivingPlayerController::HandleTimeBackStop);
	EnhancedInput->BindAction(TimeForwardAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleTimeForwardStart);
	EnhancedInput->BindAction(TimeForwardAction, ETriggerEvent::Completed, this,
		&ACitixDrivingPlayerController::HandleTimeForwardStop);

	EnhancedInput->BindAction(MapAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleMapToggle);
	EnhancedInput->BindAction(WaypointAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleWaypoint);
	EnhancedInput->BindAction(JobAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleJob);
	EnhancedInput->BindAction(CycleCarAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleCycleCar);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleInteract);
	EnhancedInput->BindAction(CancelAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleCancel);
	EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleFire);
	EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this,
		&ACitixDrivingPlayerController::HandleFireReleased);
	EnhancedInput->BindAction(AdsAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleAds);
	EnhancedInput->BindAction(AdsAction, ETriggerEvent::Completed, this,
		&ACitixDrivingPlayerController::HandleAdsReleased);
	EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleReload);
	EnhancedInput->BindAction(RaceAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleRace);
	EnhancedInput->BindAction(ShopRow1Action, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleShopRow1);
	EnhancedInput->BindAction(ShopRow2Action, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleShopRow2);
	EnhancedInput->BindAction(ShopRow3Action, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleShopRow3);
	EnhancedInput->BindAction(ShopRow4Action, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleShopRow4);
	EnhancedInput->BindAction(ShopRow5Action, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleShopRow5);
	EnhancedInput->BindAction(CapturePhotoAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandleCapturePhoto);
	EnhancedInput->BindAction(PhotoAction, ETriggerEvent::Started, this,
		&ACitixDrivingPlayerController::HandlePhotoToggle);
	EnhancedInput->BindAction(PhotoMoveAction, ETriggerEvent::Triggered, this,
		&ACitixDrivingPlayerController::HandlePhotoMove);
	EnhancedInput->BindAction(PhotoUpAction, ETriggerEvent::Triggered, this,
		&ACitixDrivingPlayerController::HandlePhotoUp);
	EnhancedInput->BindAction(PhotoLookAction, ETriggerEvent::Triggered, this,
		&ACitixDrivingPlayerController::HandlePhotoLook);
}

void ACitixDrivingPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
 if (IsLocalController() && FParse::Param(FCommandLine::Get(),TEXT("CitixGraphicsProbe"))) {
  int32& Step=GraphicsProbeStep; float& Wait=GraphicsProbeWait; bool& Passed=bGraphicsProbePassed;
  const auto* ProbeState=GetWorld()->GetGameState<ACitixChaseGameState>();
  const bool MultiplayerProbe=FParse::Param(FCommandLine::Get(),TEXT("CitixChaseReady"));
  const bool Stable=!MultiplayerProbe || (ProbeState && ProbeState->Phase==ECitixChasePhase::Pursuit && ProbeState->PhaseSecondsRemaining<298.f);
  Wait+=DeltaSeconds;
  if (Stable && Wait>3.f && Step<5) {
   Wait=0;
   if(Step==-1) { GraphicsProbeRoundSeconds=ProbeState ? ProbeState->PhaseSecondsRemaining : 0.f; InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape,IE_Pressed,1.f)); Passed &= IsSettingsMenuOpen() && IsMoveInputIgnored() && IsLookInputIgnored(); ++Step; }
   else if(Step<4) {
    Passed &= SettingsMenu && SettingsMenu->VerifyPresetClick(Step);
    if (auto* Car=Cast<ACitixVehiclePawn>(GetPawn())) Passed &= !Car->CanAcceptDriveInput() && Car->GetVehicleMovement()->GetThrottleInput()==0.f;
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/FString::Printf(TEXT("GraphicsPreset-%d-%s.png"),Step,*NetTag),true,false); ++Step;
   } else {
    Passed &= SettingsMenu && SettingsMenu->VerifyPresetClick(-1);
    Passed &= SettingsMenu && SettingsMenu->VerifyPerformanceControls();
    InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape,IE_Pressed,1.f));
    if(MultiplayerProbe) Passed &= ProbeState && ProbeState->Phase==ECitixChasePhase::Pursuit && ProbeState->PhaseSecondsRemaining<GraphicsProbeRoundSeconds-10.f;
    Passed &= !IsSettingsMenuOpen() && !IsMoveInputIgnored() && !IsLookInputIgnored() && !IsPaused();
    FFileHelper::SaveStringToFile(Passed ? TEXT("{\"passed\":true,\"preset_clicks_and_persistence\":true,\"esc_input_restored\":true,\"match_not_paused\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/(TEXT("ChaseGraphics-")+NetTag+TEXT(".json")))); ++Step;
   }
  }
 }

 if (IsLocalController() && FParse::Param(FCommandLine::Get(),TEXT("CitixCountdownProbe"))) {
  static bool SpawnWritten=false;
  const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>();
  const auto* PS=GetPlayerState<ACitixChasePlayerState>();
  if (!SpawnWritten && S && PS && GetPawn() && S->Phase==ECitixChasePhase::Countdown && S->PhaseSecondsRemaining<8.f) {
   SpawnWritten=true;
   const int32 Index=PS->ChaseRole==ECitixChaseRole::Runner ? 0 : 1;
   FTransform Dry;
   const bool Passed=S->LayoutSpawnLocations.IsValidIndex(Index) && FVector::Dist2D(GetPawn()->GetActorLocation(),S->LayoutSpawnLocations[Index])<5.f && ACitixCityGenerator::ValidateChaseSurface(GetWorld(),GetPawn()->GetActorLocation(),FVector(240,110,85),GetPawn()->GetActorRotation().Yaw,GetPawn(),Dry,false);
   FFileHelper::SaveStringToFile(Passed ? TEXT("{\"passed\":true,\"client_on_safe_round_start\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/(TEXT("ChaseSpawn-")+NetTag+TEXT(".json"))));
  }
 }

 if (IsLocalController() && FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeProbe"))) {
  auto* S=GetWorld()->GetGameState<ACitixChaseGameState>(); const auto* PS=GetPlayerState<ACitixChasePlayerState>();
  if (S && PS && S->Phase==ECitixChasePhase::Pursuit && PS->ChaseRole==ECitixChaseRole::Runner) {
   const bool Eligible=(SmokeProbeStep==0 && Cast<ACitixVehiclePawn>(GetPawn())) || (SmokeProbeStep==1 && IsOnFoot() && PS->SmokeCharges>0);
   if (Eligible) { SmokeProbeWait+=DeltaSeconds; if (SmokeProbeWait>1.f) { InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f)); ++SmokeProbeStep; SmokeProbeWait=0; } }
  }
 }
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixIceScreenshot"))) CitixIceVisualProbeTick(this);
 if (IsLocalController() && FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeScreenshot"))) {
  static bool BriefShot=false;
  const auto* BriefState=GetWorld()->GetGameState<ACitixChaseGameState>();
  if (!BriefShot && BriefState && BriefState->Phase==ECitixChasePhase::Countdown && BriefState->PhaseSecondsRemaining<8.f) { BriefShot=true; FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("ChaseBriefing-")+NetTag+TEXT(".png")),true,false); }
 }
 if (IsLocalController() && FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeScreenshot"))) {
  static float FirstCloud=-1.f; static bool Early=false,Full=false,Fade=false,Foot=false;
  const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>();
  for (TActorIterator<ACitixSmokeCloud> It(GetWorld()); It && S; ++It) {
   if (FirstCloud<0) FirstCloud=It->StartedAt;
   const float Age=S->GetServerWorldTimeSeconds()-It->StartedAt;
   static bool TrailChecked=false;
   if (!TrailChecked && Age>6.f && It->StartedAt==FirstCloud && FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeTrailProbe"))) {
    TrailChecked=true; const bool Passed=It->EmissionPositions.Num()==ACitixSmokeCloud::PuffCount && FVector::Dist2D(It->EmissionPositions[0],It->EmissionPositions.Last())>500.f;
    UE_LOG(LogCitix,Log,TEXT("[CitixSmokeTrailClient] %s received all 100 fixed birth positions from moving emitter"),Passed?TEXT("PASS"):TEXT("FAIL"));
    FFileHelper::SaveStringToFile(Passed ? TEXT("{\"passed\":true,\"replicated_trail\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseSmokeTrailClient.json")));
   }
   FString Shot;
   if (It->StartedAt>FirstCloud+10.f && Age>2.f && !Foot) { Foot=true; Shot=TEXT("SmokeFoot-"); }
   else if (!Early && Age>1.7f) { Early=true; Shot=TEXT("SmokeEarly-"); }
   else if (!Full && Age>5.1f) { Full=true; Shot=TEXT("SmokeFull-"); }
   else if (!Fade && Age>13.f) { Fade=true; Shot=TEXT("SmokeFade-"); }
   if (Shot==TEXT("SmokeFull-") && FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeProbe"))) {
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>();
    const FVector Target=(It->EmissionPositions.Num()>0 ? (FVector(It->EmissionPositions[0])+FVector(It->EmissionPositions.Last()))*.5f : It->GetActorLocation())+FVector(0,0,300);
    const FVector Offset=GetPawn() ? -GetPawn()->GetActorForwardVector()*2400.f+FVector(0,0,850) : FVector(-2400,0,850);
    Camera->SetActorLocation(Target+Offset); Camera->SetActorRotation((Target-Camera->GetActorLocation()).Rotation());
    SetViewTarget(Camera);
    FTimerHandle Snapshot,Restore;
    GetWorldTimerManager().SetTimer(Snapshot,FTimerDelegate::CreateWeakLambda(this,[this,Shot]() { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(Shot+NetTag+TEXT(".png")),true,false); }),1.2f,false);
    GetWorldTimerManager().SetTimer(Restore,FTimerDelegate::CreateWeakLambda(this,[this,Camera]() { SetViewTarget(GetPawn()); Camera->Destroy(); }),2.5f,false);
   } else if (!Shot.IsEmpty()) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(Shot+NetTag+TEXT(".png")),true,false);
  }
 }

	RefreshModeInputContexts();
	if (const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>(); Chase && Chase->Phase != ECitixChasePhase::Waiting && Chase->Phase != ECitixChasePhase::MatchResults && ChaseLobby) HideChaseLobby();
	UpdateChaseRoute(DeltaSeconds);
	if (!ChaseLobby && (!FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTest")) || FParse::Param(FCommandLine::Get(),TEXT("CitixLobbyButtons"))))
	{
		if (const ACitixChaseGameState* Chase = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr; Chase && (Chase->Phase == ECitixChasePhase::Waiting || Chase->Phase == ECitixChasePhase::MatchResults))
		{
			ShowChaseLobby();
		}
	}

	if (!FMath::IsNearlyZero(TimeScrubDirection))
	{
		if (ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(GetWorld()))
		{
			TimeOfDay->AddHours(TimeScrubDirection * TimeScrubHoursPerSecond * DeltaSeconds);
		}
	}

	if (bPhotoMode)
	{
		UpdatePhotoCamera(DeltaSeconds);
	}

	// Clients present the server snapshot locally (route/beacon/guide/cards).
	// The local director never simulates; this only copies replicated state.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		// Rank 9: authoritative city identity. A mismatched client simulates a
		// different city (routes/positions diverge silently), so warn loudly
		// once and disconnect instead of playing the wrong world.
		CitySeedCheckTimer += DeltaSeconds;
		if (CitySeedCheckTimer >= 5.f)
		{
			CitySeedCheckTimer = 0.f;
			if (const ACitixChaseGameState* ChaseSeed = GetWorld()->GetGameState<ACitixChaseGameState>())
			{
				ACitixCityGenerator* LocalGen = nullptr;
				for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) { LocalGen = *It; break; }
				if (LocalGen && LocalGen->IsGenerated() && ChaseSeed->CitySeed != 0)
				{
					const FCitixRoadNetwork& Roads = LocalGen->GetRoadNetwork();
					const int32 LocalHash = static_cast<int32>(HashCombine(GetTypeHash(Roads.CitySize), HashCombine(GetTypeHash(Roads.Nodes.Num()), GetTypeHash(Roads.Edges.Num()))));
					if (LocalGen->GetResolvedSeed() != ChaseSeed->CitySeed || LocalHash != ChaseSeed->CityConfigHash)
					{
						bCitySeedMismatch = true;
						UE_LOG(LogCitix, Error, TEXT("[CitixChase] CITY MISMATCH: local seed/hash %d/%d vs server %d/%d."), LocalGen->GetResolvedSeed(), LocalHash, ChaseSeed->CitySeed, ChaseSeed->CityConfigHash);
					}
					else if (!bReportedChaseCityIdentity)
					{
						bReportedChaseCityIdentity = true;
						ServerReportChaseCityIdentity(LocalGen->GetResolvedSeed(), LocalHash);
					}
				}
			}
			if (const ACitixGameState* SeedGS = GetWorld()->GetGameState<ACitixGameState>())
			{
				if (SeedGS->CitySeed != 0 && !bCitySeedMismatch)
				{
					ACitixCityGenerator* LocalGen = nullptr;
					for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It)
					{
						LocalGen = *It;
						break;
					}
					if (LocalGen && LocalGen->IsGenerated()
						&& LocalGen->GetResolvedSeed() != SeedGS->CitySeed)
					{
						bCitySeedMismatch = true;
						UE_LOG(LogCitix, Error, TEXT("[CitixNet] CITY MISMATCH: local seed %d vs server %d - disconnecting."),
							LocalGen->GetResolvedSeed(), SeedGS->CitySeed);
						FTimerHandle MismatchHandle;
						GetWorldTimerManager().SetTimer(MismatchHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
						{
							ConsoleCommand(TEXT("disconnect"));
						}), 5.f, false);
					}
				}
			}
		}
		// Local ADS zoom (the server drives host/dedicated cameras only).
		if (APawn* AdsPawn = GetPawn())
		{
			UCameraComponent* AdsCamera = Cast<UCameraComponent>(
				AdsPawn->GetComponentByClass(UCameraComponent::StaticClass()));
			if (AdsCamera)
			{
				if (!AdsCameraBaseFov.Contains(AdsPawn))
				{
					AdsCameraBaseFov.Add(AdsPawn, AdsCamera->FieldOfView);
				}
				const float BaseFov = AdsCameraBaseFov.FindRef(AdsPawn);
				const float TargetFov = bAdsHeldLocal ? BaseFov * 0.65f : BaseFov;
				AdsCamera->SetFieldOfView(FMath::FInterpTo(
					AdsCamera->FieldOfView, TargetFov, DeltaSeconds, 12.f));
			}
		}
		if (AdsCameraBaseFov.Num() > 8)
		{
			AdsCameraBaseFov.Reset();
		}
		SandboxMirrorTimer += DeltaSeconds;
		if (SandboxMirrorTimer >= 0.25f)
		{
			SandboxMirrorTimer = 0.f;
			if (ACitixSandboxDirector* Sandbox = GetSandbox())
			{
				const ACitixPlayerState* PS = Cast<ACitixPlayerState>(PlayerState);
				const ACitixGameState* GS = GetWorld()->GetGameState<ACitixGameState>();
				APawn* OwnedPawn = GetPawn();
				Sandbox->UpdateFromReplication(PS, GS,
					OwnedPawn ? OwnedPawn->GetActorLocation() : FVector::ZeroVector,
					0.25f);
			}
		}

		// Auto weapons: resend while held (server enforces cooldown + ammo).
		// Semis fire once per press: no resend, matching the server's edge.
		if (bTriggerHeldLocal && IsOnFoot() && !bPhotoMode && !bMapOpen)
		{
			bool bAuto = false;
			if (ACitixSandboxDirector* Sandbox = GetSandbox())
			{
				const int32 Weapon = Sandbox->GetCurrentWeapon();
				if (Weapon >= 0 && Weapon < Sandbox->GetWeaponCount())
				{
					bAuto = Sandbox->GetWeaponDef(Weapon).bFullAuto;
				}
			}
			if (bAuto)
			{
				TriggerResendTimer += DeltaSeconds;
				if (TriggerResendTimer >= 0.12f)
				{
					TriggerResendTimer = 0.f;
					SendFireShot();
				}
			}
		}

		// Remote shot feed: play every driver's tracers in the local pools,
		// including our own echo (rejected shots never render).
		if (const ACitixGameState* GS = GetWorld()->GetGameState<ACitixGameState>())
		{
			for (TObjectPtr<APlayerState> Member : GS->PlayerArray)
			{
				APlayerState* MemberPS = Member.Get();
				if (!MemberPS)
				{
					continue;
				}
				const ACitixPlayerState* ShooterPS = Cast<ACitixPlayerState>(MemberPS);
				if (!ShooterPS)
				{
					continue;
				}
				int32& LastSerial = LastShotSerial.FindOrAdd(TWeakObjectPtr<APlayerState>(MemberPS));
				if (ShooterPS->ShotSerial != LastSerial)
				{
					LastSerial = ShooterPS->ShotSerial;
					if (!ShooterPS->ShotMuzzle.IsZero() && !ShooterPS->ShotImpact.IsZero())
					{
						if (ACitixSandboxDirector* Sandbox = GetSandbox())
						{
							Sandbox->PlayShotVisual(ShooterPS->ShotMuzzle,
								ShooterPS->ShotImpact,
								static_cast<ECitixSurface>(ShooterPS->ShotSurface));
						}
					}
				}
			}
		}
	}
}

bool ACitixDrivingPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
 if (IsChaseMode() && Params.Key==EKeys::Escape && Params.Event==IE_Pressed) { ToggleSettingsMenu(); return true; }
 if (IsSettingsMenuOpen()) return true;
	if (Params.Event == IE_Pressed)
	{
		if (IsChaseMode() && Params.Key==EKeys::LeftMouseButton) {
   const auto* PS=GetPlayerState<ACitixChasePlayerState>();
   if (PS && PS->ChaseRole==ECitixChaseRole::Runner) ServerRunnerSmoke();
   else if (IsOnFoot()) SendFireShot();
   else ServerChaserIce();
   return true;
  }
		if (const ACitixChaseGameState* Chase = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr;
			Chase && (Chase->Phase == ECitixChasePhase::Waiting || Chase->Phase == ECitixChasePhase::MatchResults))
		{
			if (Params.Key == EKeys::H) { HostChaseMatch(); return true; }
			if (Params.Key == EKeys::J) { if(GetGameInstance()) GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>()->FindRooms(); return true; }
		}
	}
	return Super::InputKey(Params);
}

void ACitixDrivingPlayerController::UpdateChaseRoute(float DeltaSeconds)
{
	if (!IsLocalController()) return;
	const ACitixChaseGameState* Chase = GetWorld()->GetGameState<ACitixChaseGameState>();
	const ACitixChasePlayerState* PS = GetPlayerState<ACitixChasePlayerState>();
	if (!Chase || !PS || !GetPawn() || PS->ChaseRole != ECitixChaseRole::Runner || Chase->Phase != ECitixChasePhase::Pursuit)
	{
		if (ChaseRoute && ChaseRoute->GetMarkerCount()>0) ChaseRoute->Clear();
		return;
	}
	ChaseRouteUpdateIn -= DeltaSeconds;
	if (ChaseRouteUpdateIn > 0.f) return;
	ChaseRouteUpdateIn = .5f;
	if (!ChaseRouteCity.IsValid())
		for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) { ChaseRouteCity = *It; break; }
	if (!ChaseRouteCity.IsValid()) return;
	const bool bReplacement = IsOnFoot() && PS->CharacterHealth<100.f && !PS->bReplacementUsed;
 TArray<FVector> Cars;
 if (bReplacement) if (const AActor* Car=FindChaseEntryCandidate(50000.f)) Cars.Add(Car->GetActorLocation());
 const TArray<FVector>& Targets = bReplacement ? Cars : Chase->bExitsUnlocked ? Chase->LayoutExitLocations : Chase->LayoutRelayLocations;
	int32 Nearest = INDEX_NONE;
	float NearestDistance = TNumericLimits<float>::Max();
	for (int32 Index=0; Index<Targets.Num(); ++Index)
	{
		if (!bReplacement && !Chase->bExitsUnlocked && Chase->ActivatedRelays.IsValidIndex(Index) && Chase->ActivatedRelays[Index]) continue;
		const float Distance = FVector::DistSquared2D(GetPawn()->GetActorLocation(),Targets[Index]);
		if (Distance<NearestDistance) { NearestDistance=Distance; Nearest=Index; }
	}
	if (!ChaseRoute)
	{
		ChaseRoute = GetWorld()->SpawnActor<ACitixRouteGuide>();
		if (!ChaseRoute) return;
		ChaseRoute->SetOwner(this);
		ChaseRoute->MarkerSpacing = 900.f;
		ChaseRoute->MarkerScale = FVector(2.8f, .32f, .035f);
	}
	if (Nearest == INDEX_NONE) { ChaseRoute->Clear(); return; }
	TArray<FVector> Points;
	FCitixRouteHelper::BuildRoutePoints(ChaseRouteCity->GetRoadNetwork(),GetPawn()->GetActorLocation(),Targets[Nearest],Points);
	Points.Insert(GetPawn()->GetActorLocation(),0);
 for (FVector& Point : Points) {
  FTransform Ground;
  if (ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Point,FVector(1,1,1),0,GetPawn(),Ground,false)) Point.Z=Ground.GetLocation().Z+12.f;
 }
 if (!Points.IsEmpty()) Points.Last()=Targets[Nearest];
	ChaseRoute->UpdateRoute(Points,ECitixSurface::EmissiveCool);
}

void ACitixDrivingPlayerController::RefreshModeInputContexts()
{
	const bool bChaseMode = IsChaseMode();
	if (bChaseMode == bChaseInputContextsSuppressed)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			if (bChaseMode)
			{
				Subsystem->RemoveMappingContext(SandboxContext);
				Subsystem->RemoveMappingContext(TimeContext);
				TimeScrubDirection = 0.f;
			}
			else
			{
				Subsystem->AddMappingContext(TimeContext, -1);
				Subsystem->AddMappingContext(SandboxContext, -1);
			}
		}
	}
	bChaseInputContextsSuppressed = bChaseMode;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void ACitixDrivingPlayerController::HandleTimeBackStart(const FInputActionValue& Value)
{
	// Time scrub is authority-only: clients never rewind the shared clock.
	if (GetWorld() && (GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() || (GetWorld()->GetNetMode() != NM_Standalone && !HasAuthority())))
	{
		return;
	}
	TimeScrubDirection = -1.f;
}

void ACitixDrivingPlayerController::HandleTimeBackStop(const FInputActionValue& Value)
{
	TimeScrubDirection = 0.f;
}

void ACitixDrivingPlayerController::HandleTimeForwardStart(const FInputActionValue& Value)
{
	// Time scrub is authority-only: clients never fast-forward the shared clock.
	if (GetWorld() && (GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() || (GetWorld()->GetNetMode() != NM_Standalone && !HasAuthority())))
	{
		return;
	}
	TimeScrubDirection = 1.f;
}

void ACitixDrivingPlayerController::HandleTimeForwardStop(const FInputActionValue& Value)
{
	TimeScrubDirection = 0.f;
}

// ---------------------------------------------------------------------------
// Sandbox verbs
// ---------------------------------------------------------------------------

void ACitixDrivingPlayerController::EnsureLocalWorld()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const bool bChaseMode = World->GetGameState<ACitixChaseGameState>() != nullptr;

	ACitixCityGenerator* Generator = nullptr;
	for (TActorIterator<ACitixCityGenerator> It(World); It; ++It)
	{
		Generator = *It;
		break;
	}
	if (!Generator)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Generator = World->SpawnActor<ACitixCityGenerator>(
			ACitixCityGenerator::StaticClass(), FTransform::Identity, Params);
	}
	if (bChaseMode && Generator)
	{
		Generator->bSpawnPedestrians = false;
	}
	if (Generator && !Generator->IsGenerated())
	{
		UE_LOG(LogCitix, Log, TEXT("[CitixNet] Generating the local city copy."));
		Generator->GenerateCity();
	}

	// The clock and the rain replicate from the server; the actors just need to
	// exist locally to render them. Traffic/pedestrian/sandbox systems come with
	// the generator but stay inert without authority (server replicates pools).
	if (!ACitixTimeOfDay::Find(World))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ACitixTimeOfDay>(ACitixTimeOfDay::StaticClass(), FTransform::Identity, Params);
	}
	if (!ACitixWeatherSystem::Find(World))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ACitixWeatherSystem>(ACitixWeatherSystem::StaticClass(), FTransform::Identity, Params);
	}
	if (bChaseMode)
	{
		return;
	}

	// Presentation-only sandbox: the client never simulates (Tick is gated), it
	// renders the server snapshot from replication (route/beacon/guide/cards).
	// The road graph + POIs come from the deterministic local city copy.
	ACitixSandboxDirector* Sandbox = nullptr;
	for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It)
	{
		Sandbox = *It;
		break;
	}
	if (!Sandbox)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// No replication: a client echo would collide with the server's sim.
		Params.bNoFail = true;
		Sandbox = World->SpawnActor<ACitixSandboxDirector>(
			ACitixSandboxDirector::StaticClass(), FTransform::Identity, Params);
		if (Sandbox)
		{
			Sandbox->SetReplicates(false);
			UE_LOG(LogCitix, Log, TEXT("[CitixNet] Spawned the local presentation sandbox."));
		}
	}
	if (Sandbox && Generator && Generator->IsGenerated())
	{
		const UCitixCitySettings& Settings = UCitixCitySettings::Get();
		Sandbox->Configure(Generator->GetRoadNetwork(), Generator->GetCityPlan(),
			Generator->GetLandmarkLocations(), Settings.CitySize);
	}
}

ACitixSandboxDirector* ACitixDrivingPlayerController::GetSandbox() const
{
	if (IsChaseMode())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACitixSandboxDirector> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ACitixDrivingPlayerController::HandleMapToggle(const FInputActionValue& Value)
{
	ToggleMap();
}

void ACitixDrivingPlayerController::ToggleMap()
{
	// The map and the shop never share the screen.
	if (!bMapOpen)
	{
		CloseShopUI();
	}
	bMapOpen = !bMapOpen;
}

void ACitixDrivingPlayerController::HandleWaypoint(const FInputActionValue& Value)
{
	CycleWaypoint();
}

void ACitixDrivingPlayerController::HandleJob(const FInputActionValue& Value)
{
	StartDeliveryJob();
}

void ACitixDrivingPlayerController::HandleCycleCar(const FInputActionValue& Value)
{
	CyclePlayerCar();
}

void ACitixDrivingPlayerController::CycleWaypoint()
{
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerCycleWaypoint();
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->CycleWaypoint(this);
	}
}

void ACitixDrivingPlayerController::StartDeliveryJob()
{
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerStartDeliveryJob();
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->StartDeliveryJob(this);
	}
}

void ACitixDrivingPlayerController::StartActivity()
{
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerStartActivity();
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->StartActivity(this);
	}
}

void ACitixDrivingPlayerController::ServerCycleWaypoint_Implementation()
{
	UE_LOG(LogCitix, Log, TEXT("[CitixNet] Server: CycleWaypoint verb from %s."), *GetName());
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->CycleWaypoint(this);
	}
}

void ACitixDrivingPlayerController::ServerStartDeliveryJob_Implementation()
{
	UE_LOG(LogCitix, Log, TEXT("[CitixNet] Server: StartDeliveryJob verb from %s."), *GetName());
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->StartDeliveryJob(this);
	}
}

void ACitixDrivingPlayerController::ServerStartActivity_Implementation()
{
	UE_LOG(LogCitix, Log, TEXT("[CitixNet] Server: StartActivity verb from %s."), *GetName());
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->StartActivity(this);
	}
}

void ACitixDrivingPlayerController::ServerCancelSandbox_Implementation()
{
	UE_LOG(LogCitix, Log, TEXT("[CitixNet] Server: CancelSandbox verb from %s."), *GetName());
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		if (FCitixPlayerGame* Game = Sandbox->FindPlayerGame(this))
		{
			if (Sandbox->IsRacing(this))
			{
				Sandbox->LeaveRace(this);
			}
			else if (Game->ActivityState == ECitixActivityState::Active)
			{
				Sandbox->CancelActivity(this);
			}
			else if (Game->JobState == ECitixJobState::Active)
			{
				Sandbox->CancelDeliveryJob(this);
			}
			else if (Game->bHasWaypoint)
			{
				Sandbox->ClearWaypoint(this);
			}
		}
	}
}

void ACitixDrivingPlayerController::ServerRequestExitVehicle_Implementation()
{
	RequestExitVehicle();
}

void ACitixDrivingPlayerController::ServerRequestEnterVehicle_Implementation()
{
	RequestEnterVehicle();
}

bool ACitixDrivingPlayerController::NetVerbAllowed() const
{
	// Shared-world verbs (waypoint/job/activity/cancel) are server-authoritative:
	// standalone + server execute directly, clients send Server RPCs.
	return true;
}

bool ACitixDrivingPlayerController::CombatVerbAllowed() const
{
	// Guns, shops and car swaps replicate from stage 3 on. Until then they
	// stay single-player only — plus the listen host, which IS the authority
	// and runs the same paths as a dedicated server (rank 13). Client-side
	// controllers never have authority, so the client gate is unaffected.
	if (!GetWorld())
	{
		return true;
	}
	const ENetMode Mode = GetWorld()->GetNetMode();
	return Mode == NM_Standalone || HasAuthority();
}

void ACitixDrivingPlayerController::LogNetState()
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

	// Chase state as this instance sees it (server = sim, client = replicated).
	if (const ACitixChaseGameState* Chase = World->GetGameState<ACitixChaseGameState>())
	{
		const ACitixChasePlayerState* CPS = GetPlayerState<ACitixChasePlayerState>();
		static bool bTookChaseScreenshot = false;
  static float PursuitSeenAt=-1.f;
  const float ScreenshotNow=Chase->GetServerWorldTimeSeconds();
  if (Chase->Phase==ECitixChasePhase::Pursuit && PursuitSeenAt<0) PursuitSeenAt=ScreenshotNow;
		if (!bTookChaseScreenshot && Chase->Phase == ECitixChasePhase::Pursuit && ScreenshotNow-PursuitSeenAt>1.f && FParse::Param(FCommandLine::Get(), TEXT("CitixChaseScreenshot")))
		{
			bTookChaseScreenshot = true;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (TEXT("ChasePolish-") + NetTag + TEXT(".png")), true, false);
		}

  static bool RelayModelShot=false;
  if (!RelayModelShot && FParse::Param(FCommandLine::Get(),TEXT("CitixGateProbe")) && FParse::Param(FCommandLine::Get(),TEXT("CitixChaseScreenshot")) && GetPawn()) {
   for (const FVector& P:Chase->LayoutRelayLocations) if (FVector::DistSquared2D(P,GetPawn()->GetActorLocation())>FMath::Square(1600.f) && FVector::DistSquared2D(P,GetPawn()->GetActorLocation())<FMath::Square(2300.f)) {
    RelayModelShot=true; FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("ChaseRelayModel-")+NetTag+TEXT(".png")),true,false); break;
   }
  }
  static bool StationShot=false;
  const float StationRemaining=Chase->BreakawayUntil-ScreenshotNow;
  if (!StationShot && StationRemaining>1.f && StationRemaining<4.f && FParse::Param(FCommandLine::Get(),TEXT("CitixChaseScreenshot"))) {
   StationShot=true;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("ChaseBreakaway-")+NetTag+TEXT(".png")),true,false);
  }
  static bool RecoveryShot=false,AimShot=false;
  if (FParse::Param(FCommandLine::Get(),TEXT("CitixChaseScreenshot")) && CPS && Chase->Phase==ECitixChasePhase::Pursuit) {
   const ACitixOnFootPawn* Foot=Cast<ACitixOnFootPawn>(GetPawn());
   const bool Recover=Foot && CPS->ChaseRole==ECitixChaseRole::Runner && CPS->ReplacementReadyAt-Chase->GetServerWorldTimeSeconds()>5 && !Foot->IsInShockwaveRecovery();
   const bool Aiming=Foot && CPS->ChaseRole==ECitixChaseRole::Chaser;
   if ((Recover && !RecoveryShot) || (Aiming && !AimShot)) {
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/((Recover ? TEXT("ChaseRecovery-") : TEXT("ChaseAiming-"))+NetTag+TEXT(".png")),true,false);
    if (Recover) RecoveryShot=true; else AimShot=true;
   }
  }
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] clock server=%.2f hours=%.2f impact=%d breakaway=%.1f"), *NetTag, Chase->GetServerWorldTimeSeconds(), FCitixChaseRules::SceneHour(Chase->GetServerWorldTimeSeconds()), Chase->ImpactSerial, FMath::Max(0.f, Chase->BreakawayUntil-Chase->GetServerWorldTimeSeconds()));
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] chase phase=%d t=%.0f relays=%d exits=%d reveal=%.0f hp=%.0f hits=%d role=%d rounds=%d"),
			*NetTag, (int32)Chase->Phase, Chase->PhaseSecondsRemaining, Chase->CompletedRelays,
			Chase->bExitsUnlocked ? 1 : 0, Chase->RevealSecondsRemaining,
			CPS ? CPS->CharacterHealth : -1.f, CPS ? CPS->RunnerCarHits : -1,
			CPS ? (int32)CPS->ChaseRole : -1, CPS ? CPS->RoundsWon : -1);
	}

	// Own-car particle read-out: proves the smoke/boost effects run on both the
	// authority and (via replicated state) a client.
	if (const ACitixVehiclePawn* Car = Cast<ACitixVehiclePawn>(GetPawn()))
	{
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] car smoke=%d flames=%d boosting=%d handbrake=%d slip=%.0f speed=%.0f"),
			*NetTag, Car->GetActiveSmokePuffs(), Car->GetActiveBoostFlames(),
			Car->IsDisplayBoosting() ? 1 : 0, Car->NetState.bHandbrake ? 1 : 0,
			Car->NetState.LateralSlip, Car->GetDisplaySpeedKmh());
	}

	int32 PlayerCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		++PlayerCount;
	}

	int32 PawnCount = 0;
	int32 TrafficCount = 0;
	FVector OwnPos = FVector::ZeroVector;
	FVector OtherPos = FVector::ZeroVector;
	bool bHasOwn = false;
	bool bHasOther = false;
	APawn* OwnPawn = GetPawn();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		++PawnCount;
		APawn* SeenPawn = *It;
		if (!SeenPawn)
		{
			continue;
		}
		// Per-pawn replication truth: authority/role, physics state, velocity,
		// display speed (from replicated NetState) vs actual movement.
		const bool bIsOwn = (SeenPawn == OwnPawn);
		const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(
			SeenPawn->GetRootComponent());
		const ACitixVehiclePawn* CarPawn = Cast<ACitixVehiclePawn>(SeenPawn);
		const ACitixOnFootPawn* FootPawn = Cast<ACitixOnFootPawn>(SeenPawn);
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] pawn %s loc=(%.0f,%.0f,%.0f) vel=%.0f dspeed=%.0f seed=%d repmov=%d auth=%d role=%d remote=%d local=%d physics=%d"),
			*NetTag, bIsOwn ? TEXT("own") : TEXT("other"),
			SeenPawn->GetActorLocation().X, SeenPawn->GetActorLocation().Y,
			SeenPawn->GetActorLocation().Z, SeenPawn->GetVelocity().Size(),
			CarPawn ? CarPawn->GetDisplaySpeedKmh() : -1.f,
			FootPawn ? FootPawn->AppearanceSeed : -1,
			SeenPawn->IsReplicatingMovement() ? 1 : 0,
			SeenPawn->HasAuthority() ? 1 : 0,
			(int32)SeenPawn->GetLocalRole(), (int32)SeenPawn->GetRemoteRole(),
			SeenPawn->IsLocallyControlled() ? 1 : 0,
			RootPrim && RootPrim->IsSimulatingPhysics() ? 1 : 0);
		if (!bHasOwn && bIsOwn)
		{
			OwnPos = SeenPawn->GetActorLocation();
			bHasOwn = true;
		}
		else if (!bHasOther && !bIsOwn)
		{
			OtherPos = SeenPawn->GetActorLocation();
			bHasOther = true;
		}
	}
	for (TActorIterator<ACitixTrafficVehicle> TraffIt(World); TraffIt; ++TraffIt)
	{
		++TrafficCount;
	}

	UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] role=%s pcs=%d pawns=%d traffic=%d own=(%.0f,%.0f,%.0f)%s other=(%.0f,%.0f,%.0f)%s"),
		*NetTag, *NetRole, PlayerCount, PawnCount, TrafficCount,
		OwnPos.X, OwnPos.Y, OwnPos.Z, bHasOwn ? TEXT("") : TEXT("?"),
		OtherPos.X, OtherPos.Y, OtherPos.Z, bHasOther ? TEXT("") : TEXT("?"));
	// One-sided-freeze diagnosis: frame rate (background-window starvation),
	// own<->other distance (cull check), and the remote pawn's flags.
	const float FrameMs = FApp::GetDeltaTime() * 1000.f;
	APawn* OtherPawn = nullptr;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* SeenPawn = *It;
		if (SeenPawn && SeenPawn != OwnPawn)
		{
			OtherPawn = SeenPawn;
			break;
		}
	}
	const float OtherDist = (bHasOwn && bHasOther) ? FVector::Dist(OwnPos, OtherPos) : -1.f;
	UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] diag frame=%.1fms owndist=%.0f otherpawn=%s role=%d remote=%d dormant=%d hidden=%d"),
		*NetTag, FrameMs, OtherDist, OtherPawn ? TEXT("yes") : TEXT("no"),
		OtherPawn ? (int32)OtherPawn->GetLocalRole() : -1,
		OtherPawn ? (int32)OtherPawn->GetRemoteRole() : -1,
		OtherPawn ? (int32)OtherPawn->NetDormancy : -1,
		OtherPawn && OtherPawn->IsHidden() ? 1 : 0);
	if (OwnPawn)
	{
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] ownflags role=%d remote=%d cull=%.0f dormant=%d hidden=%d"),
			*NetTag, (int32)OwnPawn->GetLocalRole(), (int32)OwnPawn->GetRemoteRole(),
			OwnPawn->GetNetCullDistanceSquared(), (int32)OwnPawn->NetDormancy,
			OwnPawn->IsHidden() ? 1 : 0);
	}
	// Shared-world snapshot as this instance sees it (server = sim, client =
	// replicated mirror): waypoint, job, route markers, score, discovery.
	if (const ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] sandbox wp=%s job=%d markers=%d score=%d discovered=%d/%d"),
			*NetTag,
			Sandbox->HasWaypoint() ? *Sandbox->GetWaypointName() : TEXT("none"),
			(int32)Sandbox->GetJobState(),
			Sandbox->GetGuideMarkers(),
			Sandbox->GetScore(),
			Sandbox->GetDiscoveredCount(), Sandbox->GetPOICount());
		int32 PoliceCars = 0;
		for (TActorIterator<ACitixPoliceVehicle> PoliceIt(World); PoliceIt; ++PoliceIt)
		{
			++PoliceCars;
		}
		int32 FootOfficers = 0;
		for (TActorIterator<ACitixPoliceOfficer> OfficerIt(World); OfficerIt; ++OfficerIt)
		{
			++FootOfficers;
		}
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] police cars=%d officers=%d stars=%d"),
			*NetTag, PoliceCars, FootOfficers, Sandbox->GetWantedStars());
	}
	else
	{
		UE_LOG(LogCitix, Log, TEXT("[CitixNet:%s] sandbox missing"), *NetTag);
	}
}

void ACitixDrivingPlayerController::CancelActivity()
{
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerCancelSandbox();
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		if (Sandbox->IsRacing(this))
		{
			Sandbox->LeaveRace(this);
			return;
		}
		if (FCitixPlayerGame* Game = Sandbox->FindPlayerGame(this))
		{
			if (Game->ActivityState == ECitixActivityState::Active)
			{
				Sandbox->CancelActivity(this);
			}
			else if (Game->JobState == ECitixJobState::Active)
			{
				Sandbox->CancelDeliveryJob(this);
			}
			else if (bMapOpen && Game->bHasWaypoint)
			{
				Sandbox->ClearWaypoint(this);
			}
		}
	}
}

void ACitixDrivingPlayerController::FireWeapon()
{
	if (!IsOnFoot() || bPhotoMode || bMapOpen)
	{
		return;
	}
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		SendFireShot();
		return;
	}
	if (!CombatVerbAllowed())
	{
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->FireWeapon(this);
	}
}

void ACitixDrivingPlayerController::SendFireShot()
{
	// Crosshair-honest aim from the shooter's own camera. The server resolves
	// (ammo, spread, damage) and every instance — including this one — plays
	// the tracer from the shot-event echo, so rejected shots never look live.
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector Direction = CameraRotation.Vector().GetSafeNormal();
	if (const ACitixChaseGameState* GS=GetWorld()->GetGameState<ACitixChaseGameState>()) {
  const ACitixChasePlayerState* PS=GetPlayerState<ACitixChasePlayerState>();
  const float Now=GetWorld()->GetTimeSeconds();
  if (!PS || !FCitixChaseRules::CanFirePistol(PS->ChaseRole==ECitixChaseRole::Chaser,IsOnFoot(),GS->Phase==ECitixChasePhase::Pursuit) || Now-LastLocalChaseShot<.8f) return;
  LastLocalChaseShot=Now;
  const bool Dry=PS->Ammo<=0;
  ACitixChaseGameState::PlayShotFeedback(GetWorld(),Cast<ACitixOnFootPawn>(GetPawn())->GetChaseMuzzle(),Dry);
  if (Dry) return;
  ServerFireWeapon(CameraLocation,Direction,false);
  AddPitchInput(-.65f); AddYawInput(FMath::FRandRange(-.12f,.12f)); return;
 }
 ServerFireWeapon(CameraLocation, Direction, bAdsHeldLocal);
}

void ACitixDrivingPlayerController::ServerFireWeapon_Implementation(
	FVector_NetQuantize CameraLocation, FVector_NetQuantizeNormal Direction, bool bAiming)
{
	if (ACitixChaseGameMode* Chase = GetWorld()->GetAuthGameMode<ACitixChaseGameMode>())
	{
		Chase->FireChasePistol(this, CameraLocation, Direction);
		return;
	}
	// Guns fire on foot only: no drive-bys, in any mode.
	if (!IsOnFoot())
	{
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->ResolveShot(this, CameraLocation, Direction, bAiming);
	}
}

void ACitixDrivingPlayerController::ServerReloadWeapon_Implementation()
{
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->StartReload(this);
	}
}

void ACitixDrivingPlayerController::ServerBuyShopRow_Implementation(int32 ShopIdx, int32 Row)
{
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->BuySelected(this, ShopIdx, Row);
	}
}

void ACitixDrivingPlayerController::SetAdsHeld(bool bHeld)
{
	// ADS feel stays local in every mode (fed to the server per shot).
	bAdsHeldLocal = bHeld;
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	if (!CombatVerbAllowed())
	{
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->SetAdsHeld(this, bHeld);
	}
}

void ACitixDrivingPlayerController::ReloadWeapon()
{
	if (!IsOnFoot() || bPhotoMode || bMapOpen)
	{
		return;
	}
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerReloadWeapon();
		return;
	}
	if (!CombatVerbAllowed())
	{
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->StartReload();
	}
}

void ACitixDrivingPlayerController::InteractShop()
{
	ACitixSandboxDirector* Sandbox = GetSandbox();
	if (!Sandbox)
	{
		return;
	}
	// Inside the shop E buys; outside it opens (keeper) or starts an activity.
	// Browsing is local per driver; the server validates every purchase.
	if (bShopOpenLocal)
	{
		BuyShopSelection();
		return;
	}
	const APawn* PlayerPawn = GetPawn();
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	for (int32 Index = 0; Index < Sandbox->GetShops().Num(); ++Index)
	{
		if (FVector::DistSquared(Sandbox->GetShops()[Index].Location, PlayerLocation)
			< FMath::Square(Sandbox->GetShopInteractRadius() * 2.f))
		{
			OpenShopUI(Index);
			return;
		}
	}
	StartActivity();
}

void ACitixDrivingPlayerController::OpenShopUI(int32 ShopIndex)
{
	ACitixSandboxDirector* Sandbox = GetSandbox();
	if (!Sandbox || !Sandbox->GetShops().IsValidIndex(ShopIndex))
	{
		return;
	}
	// No shopping mid-objective (matches the old director gate).
	if (FCitixPlayerGame* Game = Sandbox->FindPlayerGame(this))
	{
		if (Game->ActivityState == ECitixActivityState::Active
			|| Game->JobState == ECitixJobState::Active)
		{
			return;
		}
	}
	bShopOpenLocal = true;
	ShopIndexLocal = ShopIndex;
	ShopSelectedLocal = 0;
	LockShopInput();
}

void ACitixDrivingPlayerController::BuyShopSelection()
{
	if (!bShopOpenLocal)
	{
		return;
	}
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerBuyShopRow(ShopIndexLocal, ShopSelectedLocal);
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->BuySelected(this, ShopIndexLocal, ShopSelectedLocal);
	}
}

void ACitixDrivingPlayerController::LockShopInput()
{
	// Freeze the pawn (no walking, no camera swing) and hand over the mouse.
	if (APawn* PlayerPawn = GetPawn())
	{
		PlayerPawn->DisableInput(this);
	}
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
}

void ACitixDrivingPlayerController::UnlockShopInput()
{
	if (APawn* PlayerPawn = GetPawn())
	{
		PlayerPawn->EnableInput(this);
	}
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void ACitixDrivingPlayerController::CloseShopUI()
{
	bShopOpenLocal = false;
	ShopIndexLocal = INDEX_NONE;
	UnlockShopInput();
}

void ACitixDrivingPlayerController::ShopClickSelect()
{
	ACitixDrivingHUD* HUD = Cast<ACitixDrivingHUD>(GetHUD());
	if (!HUD || !bShopOpenLocal)
	{
		return;
	}
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	int32 Row = INDEX_NONE;
	int32 Button = 0;
	if (!HUD->ShopClickHitTest(MouseX, MouseY, Row, Button))
	{
		return;
	}
	if (Button == 1)
	{
		BuyShopSelection();
	}
	else if (Button == 2)
	{
		CloseShopUI();
	}
	else if (Row != INDEX_NONE)
	{
		ShopSelectedLocal = FMath::Clamp(Row, 0, 4);
	}
}

void ACitixDrivingPlayerController::CapturePhoto()
{
	if (!bPhotoMode)
	{
		return;
	}
	ACitixSandboxDirector* Sandbox = GetSandbox();
	if (!Sandbox)
	{
		return;
	}
	// Online: screenshot saves locally now, the server validates the reward
	// asynchronously (rank 19). Local cooldown mirror gates spam.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		if (Sandbox->GetPhotoCooldownFraction() > 0.f || !PhotoCamera)
		{
			return;
		}
		const FVector ViewLocation = PhotoCamera->GetActorLocation();
		const FVector ViewDirection = PhotoCamera->GetActorForwardVector().GetSafeNormal();
		ConsoleCommand(TEXT("HighResShot 1"), /*bWriteToLog*/ false);
		ServerCapturePhoto(ViewLocation, ViewDirection);
		return;
	}
	Sandbox->CapturePhoto(this);
}

void ACitixDrivingPlayerController::ServerCapturePhoto_Implementation(
	FVector_NetQuantize ViewLocation, FVector_NetQuantizeNormal ViewDirection)
{
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->ResolvePhoto(this, ViewLocation, ViewDirection);
	}
}

void ACitixDrivingPlayerController::HandleInteract(const FInputActionValue& Value)
{
	// The free camera owns E/Q while photo mode is up; never steal it.
	// Talking and activities happen on foot: the car is for driving.
	if (bPhotoMode || bMapOpen || !IsOnFoot())
	{
		return;
	}
	if (IsChaseMode())
	{
		ServerChaseInteract();
		return;
	}
	InteractShop();
}

void ACitixDrivingPlayerController::ServerChaseInteract_Implementation()
{
	if (ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr)
	{
		Chase->BeginInteraction(this);
	}
}


void ACitixDrivingPlayerController::ServerCancelChaseInteract_Implementation()
{
	if (ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr) Chase->CancelInteraction(this);
}

void ACitixDrivingPlayerController::ServerReportChaseCityIdentity_Implementation(int32 Seed, int32 LayoutHash)
{
	if (ACitixChasePlayerState* PS = GetPlayerState<ACitixChasePlayerState>())
	{
		if (const ACitixChaseGameState* State = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr)
		{
			PS->bCityIdentityValid = Seed == State->CitySeed && LayoutHash == State->CityConfigHash;
		}
	}
}

void ACitixDrivingPlayerController::HandleCancel(const FInputActionValue& Value)
{
	if (bPhotoMode)
	{
		return;
	}
	// The shop gets first claim on X/Escape.
	if (bShopOpenLocal)
	{
		CloseShopUI();
		return;
	}
	CancelActivity();
}

void ACitixDrivingPlayerController::HandleFire(const FInputActionValue& Value)
{
	// A click inside the shop selects a row; anywhere else firing needs feet
	// on the ground (no drive-bys, no photo/map fire).
	if (bPhotoMode || bMapOpen || !GetPawn())
	{
		return;
	}
	if (bShopOpenLocal)
	{
		ShopClickSelect();
		return;
	}
	if (!IsOnFoot())
	{
		return;
	}
	// Clients latch locally (auto resend in Tick) and fire via Server RPC;
	// standalone/server drive the shared sim directly.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		bTriggerHeldLocal = true;
		TriggerResendTimer = 0.f;
		FireWeapon();
		return;
	}
	if (!CombatVerbAllowed())
	{
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->SetTriggerHeld(true);
		Sandbox->FireWeapon();
	}
}

void ACitixDrivingPlayerController::HandleFireReleased(const FInputActionValue& Value)
{
	bTriggerHeldLocal = false;
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	if (!CombatVerbAllowed())
	{
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->SetTriggerHeld(false);
	}
}

void ACitixDrivingPlayerController::HandleAds(const FInputActionValue& Value)
{
	if (bPhotoMode || bMapOpen || !IsOnFoot())
	{
		return;
	}
	SetAdsHeld(true);
}

void ACitixDrivingPlayerController::HandleAdsReleased(const FInputActionValue& Value)
{
	SetAdsHeld(false);
}

void ACitixDrivingPlayerController::HandleReload(const FInputActionValue& Value)
{
	if (bPhotoMode || bMapOpen || !IsOnFoot())
	{
		return;
	}
	ReloadWeapon();
}

void ACitixDrivingPlayerController::HandleRace(const FInputActionValue& Value)
{
	JoinRace();
}

void ACitixDrivingPlayerController::JoinRace()
{
	if (bPhotoMode || bMapOpen)
	{
		return;
	}
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		ServerJoinRace();
		return;
	}
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->JoinRace(this);
	}
}

void ACitixDrivingPlayerController::ServerJoinRace_Implementation()
{
	if (ACitixSandboxDirector* Sandbox = GetSandbox())
	{
		Sandbox->JoinRace(this);
	}
}

void ACitixDrivingPlayerController::HandleShopRow(int32 Row)
{
	ShopSelectedLocal = FMath::Clamp(Row, 0, 4);
}

void ACitixDrivingPlayerController::HandleShopRow1(const FInputActionValue& Value) { HandleShopRow(0); }
void ACitixDrivingPlayerController::HandleShopRow2(const FInputActionValue& Value) { HandleShopRow(1); }
void ACitixDrivingPlayerController::HandleShopRow3(const FInputActionValue& Value) { HandleShopRow(2); }
void ACitixDrivingPlayerController::HandleShopRow4(const FInputActionValue& Value) { HandleShopRow(3); }
void ACitixDrivingPlayerController::HandleShopRow5(const FInputActionValue& Value) { HandleShopRow(4); }

void ACitixDrivingPlayerController::HandleCapturePhoto(const FInputActionValue& Value)
{
	CapturePhoto();
}

void ACitixDrivingPlayerController::CyclePlayerCar()
{
	if (!CombatVerbAllowed())
	{
		return;
	}
	ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(GetPawn());
	if (!Vehicle)
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] Car swap only works while driving."));
		return;
	}

	const int32 Count = static_cast<int32>(ECitixCarType::Count);
	const int32 Next = (static_cast<int32>(Vehicle->GetCarType()) + 1) % Count;
	Vehicle->SetCarAppearance(static_cast<ECitixCarType>(Next), Vehicle->GetPaintColor());

	UE_LOG(LogCitix, Log, TEXT("[Citix] Car swapped to %s."),
		FCitixCarLibrary::GetTypeName(static_cast<ECitixCarType>(Next)));
}

// ---------------------------------------------------------------------------
// Photo mode
// ---------------------------------------------------------------------------

void ACitixDrivingPlayerController::TogglePhotoMode()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	CloseShopUI();

	if (!bPhotoMode)
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		GetPlayerViewPoint(ViewLocation, ViewRotation);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PhotoCamera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), FTransform(ViewRotation, ViewLocation), Params);
		if (!PhotoCamera)
		{
			return;
		}
		if (UCameraComponent* Camera = PhotoCamera->GetCameraComponent())
		{
			Camera->SetFieldOfView(58.f);
		}

		// Unpossessing stops the pawn's own input component firing, so the car
		// coasts to a stop instead of driving on while the camera is free.
		SavedPawn = GetPawn();
		UnPossess();

		bPhotoMode = true;
		SetViewTarget(PhotoCamera);

		// Pausing the city clock is a single-player luxury: in a network game the
		// shared clock keeps running and only the camera goes free.
		if (ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(World))
		{
			if (World->GetNetMode() == NM_Standalone)
			{
				TimeOfDay->SetTimePaused(true);
			}
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Photo mode on."));
	}
	else
	{
		bPhotoMode = false;
		if (PhotoCamera)
		{
			PhotoCamera->Destroy();
			PhotoCamera = nullptr;
		}
		if (SavedPawn)
		{
			Possess(SavedPawn);
			SetViewTarget(SavedPawn);
			SavedPawn = nullptr;
		}
		if (ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(World))
		{
			if (World->GetNetMode() == NM_Standalone)
			{
				TimeOfDay->SetTimePaused(false);
			}
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Photo mode off."));
	}
}

void ACitixDrivingPlayerController::HandlePhotoToggle(const FInputActionValue& Value)
{
	TogglePhotoMode();
}

void ACitixDrivingPlayerController::HandlePhotoMove(const FInputActionValue& Value)
{
	if (bPhotoMode)
	{
		PhotoMoveInput = Value.Get<FVector2D>();
	}
}

void ACitixDrivingPlayerController::HandlePhotoUp(const FInputActionValue& Value)
{
	if (bPhotoMode)
	{
		PhotoUpInput = Value.Get<float>();
	}
}

void ACitixDrivingPlayerController::HandlePhotoLook(const FInputActionValue& Value)
{
	if (bPhotoMode)
	{
		PhotoLookInput += Value.Get<FVector2D>();
	}
}

void ACitixDrivingPlayerController::UpdatePhotoCamera(float DeltaSeconds)
{
	if (!PhotoCamera)
	{
		return;
	}

	// Move: forward/right from the camera basis, plus world up for Q/E.
	FVector Move = PhotoCamera->GetActorForwardVector() * PhotoMoveInput.Y
		+ PhotoCamera->GetActorRightVector() * PhotoMoveInput.X
		+ FVector::UpVector * PhotoUpInput;
	if (!Move.IsNearlyZero())
	{
		PhotoCamera->AddActorWorldOffset(Move.GetClampedToMaxSize(1.f) * PhotoMoveSpeed * DeltaSeconds);
	}

	// Look: mouse delta accumulated since the last frame.
	if (!PhotoLookInput.IsNearlyZero())
	{
		FRotator Rotation = PhotoCamera->GetActorRotation();
		Rotation.Yaw += PhotoLookInput.X * PhotoLookSpeed;
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch - PhotoLookInput.Y * PhotoLookSpeed, -88.f, 88.f);
		PhotoCamera->SetActorRotation(Rotation);
	}

	PhotoLookInput = FVector2D::ZeroVector;
}

// ---------------------------------------------------------------------------
// Enter / exit
// ---------------------------------------------------------------------------

bool ACitixDrivingPlayerController::IsOnFoot() const
{
	return Cast<ACitixOnFootPawn>(GetPawn()) != nullptr;
}

bool ACitixDrivingPlayerController::IsChaseMode() const
{
	return GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>() != nullptr;
}

void ACitixDrivingPlayerController::ToggleSettingsMenu()
{
 if (!IsLocalController()) return;
 if (SettingsMenu) {
  SettingsMenu->RemoveFromParent(); SettingsMenu=nullptr;
  SetIgnoreMoveInput(false); SetIgnoreLookInput(false);
  if (ChaseLobby) { SetInputMode(FInputModeGameAndUI()); bShowMouseCursor=true; }
  else { SetInputMode(FInputModeGameOnly()); bShowMouseCursor=false; }
  return;
 }
 SettingsMenu=CreateWidget<UCitixSettingsWidget>(this,UCitixSettingsWidget::StaticClass());
 if (!SettingsMenu) return;
 if (PlayerInput) PlayerInput->FlushPressedKeys();
 if (auto* Car=Cast<ACitixVehiclePawn>(GetPawn())) Car->ServerSendDriveInput(0,0,false,false);
 SetIgnoreMoveInput(true); SetIgnoreLookInput(true);
 SettingsMenu->AddToViewport(100);
 FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(SettingsMenu->TakeWidget()); Mode.SetHideCursorDuringCapture(false);
 SetInputMode(Mode); bShowMouseCursor=true;
}

void ACitixDrivingPlayerController::ShowChaseLobby()
{
	if (!IsLocalController() || ChaseLobby) return;
	ChaseLobby = CreateWidget<UCitixChaseLobbyWidget>(this, UCitixChaseLobbyWidget::StaticClass());
	if (!ChaseLobby) return;
	ChaseLobby->AddToViewport(50);
	if (!SettingsMenu) { SetInputMode(FInputModeGameAndUI()); bShowMouseCursor=true; }
}

void ACitixDrivingPlayerController::HideChaseLobby()
{
	if (ChaseLobby)
	{
		ChaseLobby->RemoveFromParent();
		ChaseLobby = nullptr;
	}
	if (!SettingsMenu) { SetInputMode(FInputModeGameOnly()); bShowMouseCursor=false; }
	bChaseLobbyDismissed = true;
}

void ACitixDrivingPlayerController::HostChaseMatch()
{
 if(GetGameInstance()) GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>()->HostRoom(UCitixSessionSubsystem::DefaultRoomName());
}
void ACitixDrivingPlayerController::JoinChaseMatch(const FString& Address)
{
 if(GetGameInstance()) GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>()->JoinAddress(Address);
}

bool ACitixDrivingPlayerController::RequestExitVehicle()
{
	if (!HasAuthority())
	{
		ServerRequestExitVehicle();
		return true;
	}
	CloseShopUI();
	ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(GetPawn());
	if (!Vehicle || !Vehicle->IsOccupied())
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] Exit denied: no occupied car (vehicle=%s occupied=%d)."),
			Vehicle ? TEXT("yes") : TEXT("no"), Vehicle ? (Vehicle->IsOccupied() ? 1 : 0) : 0);
		// Rank 17: silent gates explain themselves instead of swallowing input.
		if (ACitixSandboxDirector* DenySandbox = GetSandbox())
		{
			if (FCitixPlayerGame* DenyGame = DenySandbox->FindPlayerGame(this))
			{
				DenySandbox->ShowToast(*DenyGame, TEXT("No car to exit"));
			}
		}
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Never leave the driver in mid-air: only allow exit when roughly settled.
	// Speed gate (rank 11): stepping out at speed strands the walker in
	// traffic; the driver is told why instead of failing silently.
	const FVector VehicleLocation = Vehicle->GetActorLocation();
	if (Vehicle->GetVelocity().Size() > 1500.f)
	{
		if (World->GetGameState<ACitixChaseGameState>()) ClientChaseMessage(TEXT("Slow down to exit"));
		UE_LOG(LogCitix, Log, TEXT("[Citix] Exit denied: too fast (%.0f cm/s)."), Vehicle->GetVelocity().Size());
		if (ACitixSandboxDirector* DenySandbox = GetSandbox())
		{
			if (FCitixPlayerGame* DenyGame = DenySandbox->FindPlayerGame(this))
			{
				DenySandbox->ShowToast(*DenyGame, TEXT("Too fast to exit - slow down"));
			}
		}
		return false;
	}

	// Clearance: the exit transform must fit a standing body (capsule sweep).
	// Otherwise the walker pops inside geometry.
	FTransform ExitTransform = Vehicle->GetExitTransform();
 if (World->GetGameState<ACitixChaseGameState>() && !FindSafeChaseExit(Vehicle,ExitTransform)) { ClientChaseMessage(TEXT("No clear dry ground — move the car")); return false; }
	{
		FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(CitixExitClearance), false, Vehicle);
		FHitResult SweepHit;
		if (World->SweepSingleByChannel(SweepHit, ExitTransform.GetLocation(),
			ExitTransform.GetLocation(), FQuat::Identity, ECC_WorldStatic,
			FCollisionShape::MakeCapsule(48.f, 85.f), SweepParams))
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] Exit denied: no room (%s)."), *SweepHit.GetActor()->GetName());
			if (ACitixSandboxDirector* DenySandbox = GetSandbox())
			{
				if (FCitixPlayerGame* DenyGame = DenySandbox->FindPlayerGame(this))
				{
					DenySandbox->ShowToast(*DenyGame, TEXT("No room to exit here"));
				}
			}
			return false;
		}
	}

	// Deferred so appearance is assigned before BeginPlay builds the rig.

	ACitixOnFootPawn* OnFootCharacter = World->SpawnActorDeferred<ACitixOnFootPawn>(
		ACitixOnFootPawn::StaticClass(), ExitTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!OnFootCharacter)
	{
		UE_LOG(LogCitix, Warning, TEXT("[Citix] Failed to spawn the on-foot character."));
		return false;
	}

	// Deterministic looks from the join order: every instance builds the same
	// body. Assigned BEFORE BeginPlay (deferred spawn): BeginPlay builds the
	// rig from the seed, so assigning after spawn re-rolled FMath::Rand() on
	// every instance (different skin per screen, new skin every exit).
	const int32 JoinSeed = PlayerState ? PlayerState->GetPlayerId() + 1 : 1;
	OnFootCharacter->Style = ECitixCharacterStyle::Casual;
	OnFootCharacter->AppearanceSeed = JoinSeed * 100003;
	OnFootCharacter->FinishSpawning(ExitTransform);

	Vehicle->SetOccupied(false);
	Possess(OnFootCharacter);
	OnFootPawn = OnFootCharacter;

	UE_LOG(LogCitix, Log, TEXT("[Citix] Exited vehicle at %s."), *VehicleLocation.ToCompactString());
	return true;
}

ACitixVehiclePawn* ACitixDrivingPlayerController::FindOrSpawnPlayerVehicle()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// The player may have parked their earlier car somewhere: reuse it.
	// Never hand over another driver's ride.
	for (TActorIterator<ACitixVehiclePawn> It(World); It; ++It)
	{
		if (ACitixVehiclePawn* Existing = *It)
		{
			if (!Existing->IsOccupied()
				&& (!Existing->GetOwningController() || Existing->GetOwningController() == this))
			{
				return Existing;
			}
		}
	}

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	if (const APawn* CurrentPawn = GetPawn())
	{
		Location = CurrentPawn->GetActorLocation();
		Rotation = CurrentPawn->GetActorRotation();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	return World->SpawnActor<ACitixVehiclePawn>(
		ACitixVehiclePawn::StaticClass(), FTransform(Rotation, Location), Params);
}

void ACitixDrivingPlayerController::ClientChaseMessage_Implementation(const FString& Message) { ChaseMessage=Message; ChaseMessageUntil=GetWorld()->GetTimeSeconds()+2.5f; }
AActor* ACitixDrivingPlayerController::FindChaseEntryCandidate(float Range) const
{
 const ACitixChasePlayerState* PS=GetPlayerState<ACitixChasePlayerState>();
 if (!PS || !GetPawn()) return nullptr;
 AActor* Best=nullptr; float Distance=Range*Range;
 auto Consider=[&](AActor* Actor, float Speed, bool Own, bool Available) {
  if (!Available || Speed>=10.f || Actor->IsHidden() || (!Own && (PS->ChaseRole!=ECitixChaseRole::Runner || PS->CharacterHealth>=100.f || PS->bReplacementUsed))) return;
  const float D=FVector::DistSquared(GetPawn()->GetActorLocation(),Actor->GetActorLocation()); if (D>Distance) return;
  float Length=240,Width=110,Height=100;
  if (const ACitixTrafficVehicle* Traffic=Cast<ACitixTrafficVehicle>(Actor)) FCitixCarLibrary::GetFootprint(Traffic->GetCarType(),Length,Width,Height);
  else if (const ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(Actor)) FCitixCarLibrary::GetFootprint(Car->GetCarType(),Length,Width,Height);
  FTransform Surface;
  if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Actor->GetActorLocation(),FVector(Length,Width,85),Actor->GetActorRotation().Yaw,Actor,Surface,true,GetPawn())) return;
  Best=Actor; Distance=D;
 };
 for (TActorIterator<ACitixVehiclePawn> It(GetWorld()); It; ++It) Consider(*It,It->GetDisplaySpeedKmh(),It->GetOwningController()==this,
  !It->IsOccupied() && !It->IsDisplayDestroyed() && (!It->GetOwningController() || It->GetOwningController()==this));
 for (TActorIterator<ACitixTrafficVehicle> It(GetWorld()); It; ++It) Consider(*It,It->GetAuthoritativeSpeedKmh(),false,!It->IsTakenByPlayer());
 return Best;
}
bool ACitixDrivingPlayerController::RequestEnterVehicle()
{
	if (!HasAuthority())
	{
		ServerRequestEnterVehicle();
		return true;
	}
	CloseShopUI();
	ACitixOnFootPawn* OnFootCharacter = Cast<ACitixOnFootPawn>(GetPawn());
	if (!OnFootCharacter)
	{
		return false;
	}
	if (OnFootCharacter->IsInShockwaveRecovery()) return false;
	ACitixChaseGameMode* Chase = GetWorld() ? GetWorld()->GetAuthGameMode<ACitixChaseGameMode>() : nullptr;
	ACitixChasePlayerState* ChasePlayer = GetPlayerState<ACitixChasePlayerState>();
	if (Chase)
	{
		Chase->CancelInteraction(this);
  const ACitixChaseGameState* State=GetWorld()->GetGameState<ACitixChaseGameState>();
  if (!State || State->Phase!=ECitixChasePhase::Pursuit) return false;
  AActor* Candidate=FindChaseEntryCandidate(300.f);
  ACitixVehiclePawn* Owned=Cast<ACitixVehiclePawn>(Candidate);
  const bool Own=Owned && Owned->GetOwningController()==this;
  if (!Own && (!ChasePlayer || !FCitixChaseRules::ReplacementReady(GetWorld()->GetTimeSeconds(),ChasePlayer->ReplacementReadyAt,ChasePlayer->bReplacementUsed,0,0))) {
   ClientChaseMessage(ChasePlayer && ChasePlayer->bReplacementUsed ? TEXT("Reserve used — return to your car") : TEXT("Replacement locked — wait for recovery timer")); return false;
  }
  if (!Candidate) { ClientChaseMessage(TEXT("Find a clear car within 3m, moving below 10 km/h")); return false; }
  FCollisionQueryParams Sight(SCENE_QUERY_STAT(ChaseCarEntry),false,OnFootCharacter); Sight.AddIgnoredActor(Candidate);
  FHitResult Blocker;
  if (GetWorld()->LineTraceSingleByChannel(Blocker,OnFootCharacter->GetActorLocation()+FVector(0,0,40),Candidate->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Sight)) { ClientChaseMessage(TEXT("Car entry obstructed")); return false; }
  if (Owned) {
   if (!Chase->CanClaimReplacement(this,Owned)) return false;
   Owned->SetOccupied(true); Possess(Owned);
   if (GetPawn()!=Owned) { Owned->SetOccupied(false); return false; }
   if (!Own) { ChasePlayer->bReplacementUsed=true; Owned->GetVehicleMovement()->RepairFull(); }
   Owned->SetOwningController(this); Owned->ApplyChasePerformance(ChasePlayer->ChaseRole==ECitixChaseRole::Runner);
  } else if (ACitixTrafficVehicle* Traffic=Cast<ACitixTrafficVehicle>(Candidate)) {
   ACitixTrafficSystem* System=nullptr; for (TActorIterator<ACitixTrafficSystem> It(GetWorld()); It; ++It) { System=*It; break; }
   if (!System || Traffic->IsTakenByPlayer() || Traffic->GetAuthoritativeSpeedKmh()>=10.f) return false;
   FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
   ACitixVehiclePawn* Car=GetWorld()->SpawnActor<ACitixVehiclePawn>(ACitixVehiclePawn::StaticClass(),Traffic->GetActorLocation()+FVector(0,0,85),Traffic->GetActorRotation(),Spawn);
   if (!Car) return false;
   Car->SetCarAppearance(Traffic->GetCarType(),Traffic->GetPaintColor()); Car->SetOccupied(true); Possess(Car);
   if (GetPawn()!=Car || !System->TakeVehicle(Traffic)) { Possess(OnFootCharacter); Car->Destroy(); return false; }
   Car->SetOwningController(this); Car->ApplyChasePerformance(true); Car->GetVehicleMovement()->RepairFull();
   ChasePlayer->bReplacementUsed=true;
  } else return false;
  OnFootPawn=nullptr; OnFootCharacter->Destroy();
  UE_LOG(LogCitix,Log,TEXT("[CitixChase] Road car claimed; reserve used=%d, hits=%d."),ChasePlayer->bReplacementUsed,ChasePlayer->PistolHits);
  return true;
 }

 // 1) The player's own car, if it is parked nearby (never someone else's).
	if (ACitixVehiclePawn* Vehicle = OnFootCharacter->FindNearestVehicle(EnterVehicleRange))
	{
		if (Chase && (Chase->IsReplacementVehicle(Vehicle) || (ChasePlayer && ChasePlayer->ChaseRole == ECitixChaseRole::Runner && ChasePlayer->CharacterHealth < 100.f)) && !Chase->CanClaimReplacement(this, Vehicle)) return false;
		if (Vehicle->GetOwningController() && Vehicle->GetOwningController() != this)
		{
			UE_LOG(LogCitix, Log, TEXT("[Citix] That car belongs to another driver."));
		}
		else
		{
			if (Chase) Chase->ClaimReplacement(this, Vehicle);
			Vehicle->SetOwningController(this);
			Vehicle->SetOccupied(true);
			Possess(Vehicle);

			OnFootPawn = nullptr;
			OnFootCharacter->Destroy();

			UE_LOG(LogCitix, Log, TEXT("[Citix] Entered vehicle."));
			return true;
		}
	}
	if (Chase) return false;

	// 2) Otherwise take over any parked or moving car the player is standing next to.
	ACitixTrafficVehicle* Target = OnFootCharacter->FindNearestTrafficVehicle(EnterVehicleRange);
	if (!Target)
	{
		UE_LOG(LogCitix, Log, TEXT("[Citix] No vehicle within %.0f cm."), EnterVehicleRange);
		return false;
	}

	ACitixVehiclePawn* PlayerVehicle = FindOrSpawnPlayerVehicle();
	if (!PlayerVehicle)
	{
		return false;
	}

	// Adopt the target's exact pose, body type and paint, then hide the original.
	PlayerVehicle->SetActorLocationAndRotation(Target->GetActorLocation(), Target->GetActorRotation(),
		false, nullptr, ETeleportType::TeleportPhysics);
	PlayerVehicle->SetCarAppearance(Target->GetCarType(), Target->GetPaintColor());
	PlayerVehicle->SetOwningController(this);
	PlayerVehicle->SetOccupied(true);

	// Release the source car from the traffic system (or hide it if it was parked).
	for (TActorIterator<ACitixTrafficSystem> It(GetWorld()); It; ++It)
	{
		It->TakeVehicle(Target);
		break;
	}

	Possess(PlayerVehicle);
	OnFootPawn = nullptr;
	OnFootCharacter->Destroy();

	UE_LOG(LogCitix, Log, TEXT("[Citix] Took over a %s."),
		FCitixCarLibrary::GetTypeName(Target->GetCarType()));
	return true;
}

bool ACitixDrivingPlayerController::ForceChaseEjection()
{
	if (!HasAuthority()) return false;
	ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(GetPawn());
	if (!Vehicle || !GetWorld()) return false;
	FTransform Exit;
	if (!FindSafeChaseExit(Vehicle, Exit))
	{
		UE_LOG(LogCitix, Warning, TEXT("[CitixChase] Wreck ejection delayed: no clear ground beside the car."));
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	ACitixOnFootPawn* Walker = GetWorld()->SpawnActorDeferred<ACitixOnFootPawn>(ACitixOnFootPawn::StaticClass(), Exit, this, nullptr, Params.SpawnCollisionHandlingOverride);
	if (!Walker) return false;
	Walker->AppearanceSeed = (PlayerState ? PlayerState->GetPlayerId()+1 : 1)*100003;
	Walker->FinishSpawning(Exit);
	Vehicle->SetOccupied(false);
	Possess(Walker);
	OnFootPawn = Walker;
	if (Vehicle->IsDisplayDestroyed())
	{
		const FVector Outward = (Exit.GetLocation()-Vehicle->GetActorLocation()).GetSafeNormal2D();
		Walker->BeginShockwaveRecovery(Outward*750.f+FVector(0,0,650.f));
		Vehicle->Destroy();
	}
	return true;
}

bool ACitixDrivingPlayerController::FindSafeChaseExit(const ACitixVehiclePawn* Vehicle, FTransform& OutExit) const
{
	if (!Vehicle || !GetWorld()) return false;
	const FVector Base = Vehicle->GetActorLocation();
	const FVector Forward = Vehicle->GetActorForwardVector();
	const FVector Right = Vehicle->GetActorRightVector();
	const TArray<FVector> Directions = { Right, -Right, -Forward, Forward, (Right + Forward).GetSafeNormal(), (Right - Forward).GetSafeNormal(), (-Right + Forward).GetSafeNormal(), (-Right - Forward).GetSafeNormal() };
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CitixChaseEject), false, Vehicle);
	for (float Distance : { 260.f, 420.f, 600.f, 900.f, 1200.f, 1600.f, 2400.f })
	{
		for (const FVector& Direction : Directions)
		{
			const FVector Probe = Base + Direction * Distance;
   FTransform Dry;
   if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Probe,FVector(48,48,85),Vehicle->GetActorRotation().Yaw,Vehicle,Dry)) continue;
			FHitResult Ground;
			if (!GetWorld()->LineTraceSingleByChannel(Ground, Probe + FVector(0.f, 0.f, 600.f), Probe - FVector(0.f, 0.f, 1200.f), ECC_WorldStatic, Params)) continue;
			const FVector Candidate = Ground.ImpactPoint + FVector(0.f, 0.f, 90.f);
			FHitResult Blocker;
			if (GetWorld()->SweepSingleByChannel(Blocker, Candidate, Candidate, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeCapsule(48.f, 85.f), Params)) continue;
			OutExit = FTransform(FRotator(0.f, Vehicle->GetActorRotation().Yaw, 0.f), Candidate);
			return true;
		}
	}
	return false;
}

void ACitixDrivingPlayerController::ServerChaserIce_Implementation()
{
 if (auto* Mode=GetWorld()->GetAuthGameMode<ACitixChaseGameMode>()) Mode->UseChaserIce(this);
}

void ACitixDrivingPlayerController::ServerRunnerSmoke_Implementation()
{
 if (auto* Mode=GetWorld()->GetAuthGameMode<ACitixChaseGameMode>()) Mode->UseRunnerSmoke(this);
}
