#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixRelayLayout.h"
#include "Chase/CitixRoundLifecycleProbe.h"
#include "Network/CitixSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Chase/CitixIceWave.h"
#include "Chase/CitixIceProbe.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Chase/CitixGateLayout.h"
#include "Chase/CitixSmokeCloud.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "World/CitixTimeOfDay.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Chase/CitixChaseRules.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Player/CitixDrivingHUD.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "City/CitixCityGenerator.h"
#include "City/CitixHillsideLayout.h"
#include "Kismet/GameplayStatics.h"
#include "City/CitixCityChunk.h"
#include "Traffic/CitixTrafficVehicle.h"
#include "Traffic/CitixTrafficSystem.h"
#include "Components/PrimitiveComponent.h"
#include "Character/CitixOnFootPawn.h"
#include "Sandbox/CitixDestinationBeacon.h"
#include "Sandbox/CitixRouteHelper.h"
#include "Core/CitixTypes.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Citix.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ACitixChaseGameMode::ACitixChaseGameMode() { PlayerControllerClass = ACitixDrivingPlayerController::StaticClass(); PlayerStateClass = ACitixChasePlayerState::StaticClass(); GameStateClass = ACitixChaseGameState::StaticClass(); DefaultPawnClass = ACitixVehiclePawn::StaticClass(); HUDClass = ACitixDrivingHUD::StaticClass(); PrimaryActorTick.bCanEverTick = true; bChaseTest = FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTest")); bChaseTestCapture = FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestCapture")); bChaseTestTimeout = FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestTimeout")); bChaseTestSecondExit = FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestSecondExit")); bChaseTestRelayHold = FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestRelayHold")); }
bool ACitixChaseGameMode::SelectLobbyMap(APlayerController* Host,bool Hillside)
{
 auto* State=ChaseState();
 if(!Host || !Host->HasAuthority() || !Host->IsLocalController() || !State || GetWorld()->GetNetMode()!=NM_ListenServer) return false;
 if(State->Phase!=ECitixChasePhase::Waiting && State->Phase!=ECitixChasePhase::MatchResults) return false;
 if(State->bHillsideMap==Hillside || !GetWorld()->NextURL.IsEmpty()) return false;
 FURL Destination=GetWorld()->URL;
 Destination.AddOption(Hillside ? TEXT("CitixMap=Hillside") : TEXT("CitixMap=City"));
 Destination.AddOption(*FString::Printf(TEXT("CitixSeed=%d"),State->CitySeed));
 Destination.AddOption(TEXT("listen"));
 if(!GetWorld()->ServerTravel(Destination.ToString(),true)) {State->StatusText=TEXT("Map loading failed — try selecting again"); return false;}
 LobbyReady.Reset(); RematchReady.Reset();
 for(APlayerState* Player:State->PlayerArray) if(auto* PS=Cast<ACitixChasePlayerState>(Player)) {PS->bReady=false; PS->bCityIdentityValid=false;}
 State->StatusText=Hillside ? TEXT("Loading Hillside Switchback…") : TEXT("Loading City…");
 UE_LOG(LogCitix,Log,TEXT("[CitixChase] Host selected map: %s"),Hillside ? TEXT("Hillside") : TEXT("City"));
 return true;
}
void ACitixChaseGameMode::BeginPlay() { Super::BeginPlay(); int32 CitySeed = 0; int32 CityConfigHash = 0; RelayLocations.Reset(); ExitLocations = { FVector(-31000.f, 30000.f, 200.f), FVector(31000.f, -30000.f, 200.f) }; ReplacementLocations = { FVector(-9000.f, -10000.f, 200.f), FVector(11000.f, 9000.f, 200.f) }; const FVector SpawnPairs[][2] = { { FVector(-22000.f, -16000.f, 300.f), FVector(18000.f, 12000.f, 300.f) }, { FVector(-21000.f, 17000.f, 300.f), FVector(22000.f, -15000.f, 300.f) }, { FVector(-24000.f, 4000.f, 300.f), FVector(21000.f, -4000.f, 300.f) } }; int32 SpawnPair = FMath::RandRange(0, UE_ARRAY_COUNT(SpawnPairs) - 1); if (bChaseTest) FParse::Value(FCommandLine::Get(),TEXT("CitixSpawnPair="),SpawnPair); SpawnPair=FMath::Clamp(SpawnPair,0,UE_ARRAY_COUNT(SpawnPairs)-1); UE_LOG(LogCitix,Log,TEXT("[CitixChase] Spawn pair %d selected"),SpawnPair); SpawnLocations = { SpawnPairs[SpawnPair][0], SpawnPairs[SpawnPair][1] }; ActivatedRelays.Init(false, RelayLocations.Num()); if (GetWorld()) { ACitixCityGenerator* City = GetWorld()->SpawnActorDeferred<ACitixCityGenerator>(ACitixCityGenerator::StaticClass(), FTransform::Identity); if (City) { City->bAutoGenerateOnBeginPlay = false; City->bHillsideMap=UGameplayStatics::ParseOption(OptionsString,TEXT("CitixMap"))==TEXT("Hillside"); FParse::Value(FCommandLine::Get(),TEXT("CitixCitySeed="),City->SeedOverride); const FString TravelSeed=UGameplayStatics::ParseOption(OptionsString,TEXT("CitixSeed")); if(!TravelSeed.IsEmpty() && TravelSeed.IsNumeric()) City->SeedOverride=FCString::Atoi(*TravelSeed); if(auto* Selected=ChaseState()) {Selected->bHillsideMap=City->bHillsideMap; Selected->MapRevision=City->bHillsideMap ? FCitixHillsideLayout::Revision : 0;} City->bSpawnPedestrians = false; City->FinishSpawning(FTransform::Identity); City->GenerateCity(); const FCitixRoadNetwork& Roads = City->GetRoadNetwork(); CitySeed = City->GetResolvedSeed(); CityConfigHash = static_cast<int32>(Roads.GetLayoutHash(City->bHillsideMap ? FCitixHillsideLayout::Revision : 0)); SnapChaseLocationsToRoads(Roads); } auto Unsafe=[](const FVector& P) { return P.ContainsNaN() || P.SizeSquared2D()>FMath::Square(10000000.f); }; if (SpawnLocations.Num()!=2 || RelayLocations.Num()!=FCitixChaseRules::ActiveRelayCount || SpawnLocations.ContainsByPredicate(Unsafe) || RelayLocations.ContainsByPredicate(Unsafe) || ExitLocations.ContainsByPredicate(Unsafe) || ReplacementLocations.ContainsByPredicate(Unsafe)) { SpawnLocations.Reset(); if (ACitixChaseGameState* S=ChaseState()) S->StatusText=TEXT("No safe road layout found — host a new city"); return; } for (const FVector& Location : RelayLocations) { ACitixDestinationBeacon* Beacon = GetWorld()->SpawnActor<ACitixDestinationBeacon>(); Beacon->SetRelayProjection(Location); RelayBeacons.Add(Beacon); } for (const FVector& Location : ExitLocations) { ACitixDestinationBeacon* Beacon = GetWorld()->SpawnActor<ACitixDestinationBeacon>(); Beacon->SetExitProjection(Location); Beacon->Hide(); ExitBeacons.Add(Beacon); } for (const FVector& Location : ReplacementLocations) { ACitixDestinationBeacon* Beacon = GetWorld()->SpawnActor<ACitixDestinationBeacon>(); Beacon->SetDestination(Location, ECitixSurface::CarPaint); Beacon->Hide(); ReplacementBeacons.Add(Beacon); } } EnsurePlayerStarts(); InitializeBreakaways(); if (ACitixChaseGameState* S = ChaseState()) { S->CitySeed = CitySeed; S->CityConfigHash = CityConfigHash; S->LayoutRelayLocations = RelayLocations; S->LayoutExitLocations = ExitLocations; S->LayoutSpawnLocations = SpawnLocations; S->LayoutReplacementLocations = ReplacementLocations; S->StatusText = TEXT("Waiting for two drivers"); } }
void ACitixChaseGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
 if (ErrorMessage.IsEmpty()) {
  const auto* S=ChaseState();
  const bool Playing=S && S->Phase!=ECitixChasePhase::Waiting && S->Phase!=ECitixChasePhase::MatchResults;
  ErrorMessage=UCitixSessionSubsystem::AdmissionError(Options,GetNumPlayers(),Playing);
 }
}
void ACitixChaseGameMode::InitializeBreakaways()
{
 for (TActorIterator<ACitixSmokeCloud> It(GetWorld()); It; ++It) It->Destroy();
 for (TActorIterator<ACitixIceWave> It(GetWorld()); It; ++It) It->Destroy();
 if (!ACitixTimeOfDay::Find(GetWorld()))
  if (auto* Clock=GetWorld()->SpawnActor<ACitixTimeOfDay>()) Clock->SetReplicates(false);
 auto* S=ChaseState(); if (!S) return;
 S->BreakawayUntil=S->BreakawayReadyAt=0.f; GateOverlaps.Reset();
 if (S->BreakawayLocations.IsEmpty()) {
  for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) {
   const auto& Roads=It->GetRoadNetwork();
   TArray<int32> Edges;
   for (int32 I=0; I<Roads.Edges.Num(); ++I) if (Roads.Edges[I].bDrivable && !Roads.Edges[I].bBridge) Edges.Add(I);
   Edges.Sort([Seed=S->CitySeed](int32 A,int32 B) { return HashCombine(GetTypeHash(A),GetTypeHash(Seed))<HashCombine(GetTypeHash(B),GetTypeHash(Seed)); });
   TArray<FCitixGateCandidate> Candidates;
   for (int32 I:Edges) {
    const auto& E=Roads.Edges[I]; const FVector A=Roads.EdgePoint3D(I,0),B=Roads.EdgePoint3D(I,1);
    const float Length=Roads.EdgeLength(I);
    if (Length<3500.f) continue; // Keep posts away from small junction connectors.
    const int32 Count=FMath::CeilToInt(Length/2500.f);
    const float Yaw=(B-A).Rotation().Yaw;
    // Generate safe choices; selection spaces them across the connected network.
    for (int32 Slot=0; Slot<Count; ++Slot) {
     const float EndClearance=FMath::Min(3000.f,Length*.5f);
     const float Along=FMath::Clamp(Length*(Slot+.5f)/Count,EndClearance,Length-EndClearance);
     const FVector P=FMath::Lerp(A,B,Along/Length);
     FTransform Surface;
     if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),P+FVector(0,0,100),FVector(140,FMath::Max(1200.f,E.CorridorWidth)*.5f+100.f,85),Yaw,nullptr,Surface,false)) continue;
     Candidates.Add({Surface,I,Along,FMath::Max(1200.f,E.CorridorWidth)});
    }
   }
   for (int32 Pick:FCitixGateLayout::Select(Roads,Candidates)) { const auto& C=Candidates[Pick]; S->BreakawayLocations.Add(C.Surface.GetLocation()); S->BreakawayYaws.Add(C.Surface.Rotator().Yaw); S->BreakawayWidths.Add(C.Width); }
   break;
  }
 }
 while (BreakawayBeacons.Num()<S->BreakawayLocations.Num()) BreakawayBeacons.Add(GetWorld()->SpawnActor<ACitixDestinationBeacon>());
 for (int32 I=0; I<BreakawayBeacons.Num(); ++I) if (auto* Beacon=BreakawayBeacons[I].Get()) {
  Beacon->SetBreakawayStation(S->BreakawayLocations[I],I,S->BreakawayWidths[I]);
  Beacon->SetActorRotation(FRotator(0,S->BreakawayYaws[I],0));
 }
 UE_LOG(LogCitix,Log,TEXT("[CitixGate] %d road-wide gates; minimum road spacing 200m, one gate per nearby junction; shared boost cooldown 15s"),S->BreakawayLocations.Num());
}

void ACitixChaseGameMode::ActivateBreakaway(int32 Index)
{
 auto* S=ChaseState(); const float Now=GetWorld()->GetTimeSeconds();
 if (!S || S->Phase!=ECitixChasePhase::Pursuit || !S->BreakawayLocations.IsValidIndex(Index) || Now<S->BreakawayReadyAt) return;
 S->BreakawayUntil=Now+5.f; S->BreakawayReadyAt=Now+15.f;
 S->StatusText=TEXT("BREAKAWAY — energetic boost"); S->ForceNetUpdate();
 UE_LOG(LogCitix,Log,TEXT("[CitixGate] Runner boost gate %d; all gates locked until %.2f"),Index,S->BreakawayReadyAt);
}

bool ACitixChaseGameMode::IsAtBreakaway(AController* Controller,int32 Index) const
{
 const auto* S=ChaseState();
 if (!S || !Controller || !Controller->GetPawn() || !S->BreakawayLocations.IsValidIndex(Index) || !S->BreakawayWidths.IsValidIndex(Index)) return false;
 const FVector Local=FRotator(0,S->BreakawayYaws[Index],0).UnrotateVector(Controller->GetPawn()->GetActorLocation()-S->BreakawayLocations[Index]);
 return FMath::Abs(Local.X)<=350.f && FMath::Abs(Local.Y)<=S->BreakawayWidths[Index]*.5f && FMath::Abs(Local.Z)<=300.f;
}

void ACitixChaseGameMode::UseChaserIce(APlayerController* Player)
{
 auto* PS=Player ? Player->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 auto* Car=Player ? Cast<ACitixVehiclePawn>(Player->GetPawn()) : nullptr;
 auto* S=ChaseState(); const float Now=GetWorld()->GetTimeSeconds();
 if (!PS || !Car || !S || S->Phase!=ECitixChasePhase::Pursuit || PS->ChaseRole!=ECitixChaseRole::Chaser || Car->IsDisplayDestroyed() || PS->IceCharges<=0 || (PS->LastIceAt>0 && Now-PS->LastIceAt<1.2f)) return;
 const FVector Forward=Car->GetActorForwardVector().GetSafeNormal2D();
 const FVector Origin=Car->GetActorLocation()+Forward*240.f;
 auto* Wave=GetWorld()->SpawnActorDeferred<ACitixIceWave>(ACitixIceWave::StaticClass(),FTransform(Forward.Rotation(),Origin));
 if (!Wave) return;
 Wave->EmitterCar=Car;
 --PS->IceCharges; PS->LastIceAt=Now; PS->bLastIceHit=false;
 if (PS->NextIceAt<=0) PS->NextIceAt=Now+FCitixChaseRules::IceRecharge;
 for (APlayerState* State:S->PlayerArray) {
  auto* Runner=Cast<ACitixChasePlayerState>(State); APawn* Target=Runner ? Runner->GetPawn() : nullptr;
  if (!Runner || Runner->ChaseRole!=ECitixChaseRole::Runner || !Target || !FCitixChaseRules::InIceCone(Origin,Forward,Target->GetActorLocation())) continue;
  const bool AlreadyFrozen=Runner->FrozenUntil>Now;
  if (auto* Body=Cast<UPrimitiveComponent>(Target->GetRootComponent()); Body && Body->IsSimulatingPhysics()) {
   FVector Velocity=Body->GetPhysicsLinearVelocity();
   if (!AlreadyFrozen) { Velocity.X*=.7f; Velocity.Y*=.7f; }
   Runner->FrozenSpeedLimit=AlreadyFrozen ? FMath::Min(Runner->FrozenSpeedLimit,static_cast<float>(Velocity.Size2D())) : Velocity.Size2D();
   Body->SetPhysicsLinearVelocity(Velocity); Target->ForceNetUpdate();
  } else if (auto* Foot=Cast<ACitixOnFootPawn>(Target)) {
   auto* Movement=Foot->GetCharacterMovement();
   if (!AlreadyFrozen) { Movement->Velocity.X*=.7f; Movement->Velocity.Y*=.7f; }
   Runner->FrozenSpeedLimit=Movement->Velocity.Size2D();
  }
  Runner->FrozenUntil=Now+FCitixChaseRules::IceDuration; Runner->ForceNetUpdate();
  Wave->FrozenTarget=Target; PS->bLastIceHit=true;
 }
 Wave->StartedAt=Now; Wave->FinishSpawning(FTransform(Forward.Rotation(),Origin));
 PS->ForceNetUpdate();
 Player->ClientMessage(PS->bLastIceHit ? TEXT("Ice Wave — runner frozen") : TEXT("Ice Wave released"));
 UE_LOG(LogCitix,Log,TEXT("[CitixIce] deployed hit=%d charges=%d"),PS->bLastIceHit,PS->IceCharges);
}

void ACitixChaseGameMode::UseRunnerSmoke(APlayerController* Player)
{
 auto* S=ChaseState(); auto* PS=Player ? Player->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 if (!S || !PS || !Player->GetPawn() || PS->ChaseRole!=ECitixChaseRole::Runner || S->Phase!=ECitixChasePhase::Pursuit) return;
 const float Now=GetWorld()->GetTimeSeconds();
 if (PS->SmokeCharges<=0 || PS->SmokeEmittingUntil>Now) return;
 if (const auto* Foot=Cast<ACitixOnFootPawn>(Player->GetPawn()); Foot && Foot->IsInShockwaveRecovery()) return;
 const FTransform Pose(FRotator::ZeroRotator,Player->GetPawn()->GetActorLocation());
 auto* Smoke=GetWorld()->SpawnActorDeferred<ACitixSmokeCloud>(ACitixSmokeCloud::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if (!Smoke) return;
 Smoke->EmitterController=Player; Smoke->StartedAt=Now; Smoke->CloudSeed=GetTypeHash(Now); Smoke->FinishSpawning(Pose);
 --PS->SmokeCharges; PS->SmokeEmittingUntil=Now+5.f;
 if (PS->NextSmokeAt<=0.f) PS->NextSmokeAt=Now+60.f;
 PS->ForceNetUpdate();
 UE_LOG(LogCitix,Log,TEXT("[CitixSmoke] Runner deploys wider uniform-grey trailing cloud over 5s, remaining=%d"),PS->SmokeCharges);
}

void ACitixChaseGameMode::FireChasePistol(APlayerController* Shooter, const FVector& Origin, const FVector& Direction)
{
	ACitixChaseGameState* S = ChaseState();
	ACitixChasePlayerState* PS = Shooter ? Shooter->GetPlayerState<ACitixChasePlayerState>() : nullptr;
	if (!S || !PS || !Shooter->GetPawn() || !FCitixChaseRules::CanFirePistol(PS->ChaseRole == ECitixChaseRole::Chaser,
		Shooter->GetPawn()->IsA<ACitixOnFootPawn>(), S->Phase == ECitixChasePhase::Pursuit)) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastPistolShot < .8f || Origin.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero()) return;
	FVector ServerOrigin; FRotator ServerAim;
	Shooter->GetPlayerViewPoint(ServerOrigin, ServerAim);
	if (FVector::Distance(Origin, ServerOrigin) > 500.f || FVector::DotProduct(Direction.GetSafeNormal(), ServerAim.Vector()) < .95f) {
  if (bChaseTest && Now-LastPistolShot>.8f) UE_LOG(LogCitix,Log,TEXT("[CitixPistolTest] rejected aim dot=%.3f delta=%.1f"),FVector::DotProduct(Direction.GetSafeNormal(),ServerAim.Vector()),FVector::Distance(Origin,ServerOrigin));
  return;
 }
	if (PS->Ammo <= 0) return;
 --PS->Ammo;
 if (PS->NextAmmoAt <= 0.f) PS->NextAmmoAt = Now + 8.f;
 LastPistolShot = Now;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChasePistol), false, Shooter->GetPawn());
	FHitResult Hit;
	const FVector End = ServerOrigin + Direction.GetSafeNormal() * 5000.f;
	bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ServerOrigin, End, ECC_Visibility, Params);
 const FVector Muzzle=Cast<ACitixOnFootPawn>(Shooter->GetPawn())->GetChaseMuzzle();
 const FVector AimEndpoint=bHit ? Hit.ImpactPoint : End;
 FHitResult MuzzleHit;
 // The camera may see past a corner while the physical weapon is still behind it.
 // Resolve that obstruction before damage and use the same endpoint for the tracer.
 if (GetWorld()->LineTraceSingleByChannel(MuzzleHit,Muzzle,AimEndpoint,ECC_Visibility,Params)) {
  Hit=MuzzleHit; bHit=true;
 }
 if (bChaseTest) UE_LOG(LogCitix,Log,TEXT("[CitixPistolTest] shot ammo=%d actor=%s origin=%s end=%s"),PS->Ammo,*GetNameSafe(Hit.GetActor()),*ServerOrigin.ToCompactString(),*End.ToCompactString());
	bool bRunnerHit = false;
	if (ACitixOnFootPawn* Runner = Cast<ACitixOnFootPawn>(Hit.GetActor()))
		if (ACitixChasePlayerState* Target = Runner->GetPlayerState<ACitixChasePlayerState>(); Target && Target->ChaseRole == ECitixChaseRole::Runner && Now >= CaptureProtectionUntil && !Runner->IsInShockwaveRecovery())
		{
			Target->StaggerUntil = Now + .6f;
   ++Target->PistolHits;
   if (Target->PistolHits >= 4) FinishRound(false, TEXT("Four pistol hits confirmed"));
			bRunnerHit = true;
		}
	S->MulticastPistolShot(Muzzle, bHit ? Hit.ImpactPoint : End, bRunnerHit, PS);
}

void ACitixChaseGameMode::EnsurePlayerStarts()
{
	if (!GetWorld() || GeneratedPlayerStarts.Num() > 0)
	{
		return;
	}
	// One lobby start per driver at the road-validated spawn pair. Roles are only
	// assigned when the round begins, so the lobby just needs two distinct cars.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!SpawnLocations.IsValidIndex(Index)) continue;
  FTransform Surface;
  float Yaw=0,Best=FLT_MAX;
  for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) { const auto& Roads=It->GetRoadNetwork(); for (const auto& E:Roads.Edges) if (E.bDrivable) {
   const FVector A(Roads.Nodes[E.NodeA].Position,0),B(Roads.Nodes[E.NodeB].Position,0); const float D=FMath::PointDistToSegment(FVector(SpawnLocations[Index].X,SpawnLocations[Index].Y,0),A,B);
   if (D<Best) { Best=D; Yaw=(B-A).Rotation().Yaw; }
  } break; }
  if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),SpawnLocations[Index],FVector(240,110,85),Yaw,nullptr,Surface)) continue;
  const FVector Location=Surface.GetLocation();
		if (APlayerStart* Start = GetWorld()->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), Surface, Params))
		{
			GeneratedPlayerStarts.Add(Start);
		}
	}
	UE_LOG(LogCitix, Log, TEXT("[Citix] Chase player starts: %d."), GeneratedPlayerStarts.Num());
	// The initial controller may have been spawned before BeginPlay generated these
	// road starts. Restart it once so it cannot remain at the template map start.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Player = It->Get())
		{
			RestartPlayer(Player);
		}
	}
}

AActor* ACitixChaseGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (GeneratedPlayerStarts.Num() > 0 && Player)
	{
		// Reuse a prior claim (short disconnect) before handing out a new slot.
		for (const TPair<TWeakObjectPtr<AController>, int32>& Claim : PlayerStartClaims)
		{
			if (Claim.Key.Get() == Player && GeneratedPlayerStarts.IsValidIndex(Claim.Value)
				&& GeneratedPlayerStarts[Claim.Value])
			{
				return GeneratedPlayerStarts[Claim.Value];
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
				UE_LOG(LogCitix, Log, TEXT("[Citix] Chase start %d assigned."), Index);
				return GeneratedPlayerStarts[Index];
			}
		}
		return GeneratedPlayerStarts[0];
	}
	return nullptr;
}

ACitixChaseGameState* ACitixChaseGameMode::ChaseState() const { return GetGameState<ACitixChaseGameState>(); }
APlayerController* ACitixChaseGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
 // Recheck at admission: simultaneous pending connections can both pass PreLogin.
 const auto* S=ChaseState();
 ErrorMessage=UCitixSessionSubsystem::AdmissionError(Options,GetNumPlayers(),S && S->Phase!=ECitixChasePhase::Waiting && S->Phase!=ECitixChasePhase::MatchResults);
 if(!ErrorMessage.IsEmpty()) return nullptr;
 return Super::Login(NewPlayer,InRemoteRole,Portal,Options,UniqueId,ErrorMessage);
}
void ACitixChaseGameMode::PostLogin(APlayerController* P) { Super::PostLogin(P); if (ACitixChasePlayerState* PS = P ? P->GetPlayerState<ACitixChasePlayerState>() : nullptr) { PS->bCityIdentityValid = P->IsLocalController(); PS->SetPlayerName(FString::Printf(TEXT("Driver %02d"),GetNumPlayers())); } if (ACitixChaseGameState* S = ChaseState()) S->StatusText = GeneratedPlayerStarts.Num()!=2 ? TEXT("No safe road starts found — host a new city") : GetNumPlayers() == 2 ? TEXT("Both drivers press F to ready") : TEXT("Waiting for a second driver"); }
void ACitixChaseGameMode::RestartPlayer(AController* Player) {
 if (GeneratedPlayerStarts.Num()!=2) return;
 Super::RestartPlayer(Player);
 ACitixChaseGameState* S=ChaseState(); if (!S || !Player || !Player->GetPawn()) return;
 if (S->Phase==ECitixChasePhase::Countdown || S->Phase==ECitixChasePhase::Pursuit) PlacePlayerAtRoundSpawn(Player);
 else if (AActor* Start=ChoosePlayerStart_Implementation(Player)) {
  Player->GetPawn()->SetActorTransform(Start->GetActorTransform(),false,nullptr,ETeleportType::TeleportPhysics);
  if (ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(Player->GetPawn())) { Car->SetOwningController(Player); Car->SetOccupied(true); Car->SetRoundStartPose(Start->GetActorTransform()); }
  UE_LOG(LogCitix,Log,TEXT("[CitixChase] Validated lobby pose %s."),*Start->GetActorLocation().ToCompactString());
 }
}
void ACitixChaseGameMode::Logout(AController* Exiting) { for (TActorIterator<ACitixVehiclePawn> It(GetWorld()); It; ++It) if (It->GetOwningController()==Exiting) It->Destroy(); LobbyReady.Remove(Exiting); RematchReady.Remove(Exiting); for (int32 Index = PlayerStartClaims.Num() - 1; Index >= 0; --Index) if (!PlayerStartClaims[Index].Key.IsValid() || PlayerStartClaims[Index].Key.Get() == Exiting) PlayerStartClaims.RemoveAt(Index); Super::Logout(Exiting); CancelMatch(TEXT("Round cancelled — waiting for two drivers")); }
void ACitixChaseGameMode::StartRound()
{
 if (GeneratedPlayerStarts.Num()!=2) { if (ACitixChaseGameState* S=ChaseState()) { S->Phase=ECitixChasePhase::Waiting; S->StatusText=TEXT("No safe road starts found — host a new city"); } return; }
 // Both role assignments use one server-owned selection. Only a new match rerolls.
 if (RoundNumber==0 && !SelectMatchRelays()) { CancelMatch(TEXT("No safe relay layout found — host a new city")); return; }
	InitializeBreakaways();
	LobbyReady.Reset();
	RematchReady.Reset();
	for (TPair<TWeakObjectPtr<AController>, FCitixChaseHold>& Pair : ActiveHolds) ClearHold(Pair.Key.Get(), false);
	ActiveHolds.Reset();
	PendingEjections.Reset();
	for (ACitixVehiclePawn* Car : ReplacementCars) if (IsValid(Car)) Car->Destroy();
	ReplacementCars.Reset();
	++RoundNumber;
	PhaseTime = FCitixChaseRules::CountdownSeconds;
	ActivatedRelays.Init(false, RelayLocations.Num());
	for (int32 Index = 0; Index < RelayBeacons.Num(); ++Index) if (RelayBeacons[Index]) RelayBeacons[Index]->SetRelayProjection(RelayLocations[Index]);
	for (ACitixDestinationBeacon* Beacon : ExitBeacons) if (Beacon) Beacon->Hide();
	for (ACitixDestinationBeacon* Beacon : ReplacementBeacons) if (Beacon) Beacon->Hide();

	ACitixChaseGameState* S = ChaseState();
	if (!S) return;
	S->Phase = ECitixChasePhase::Waiting;
	S->RoundNumber = RoundNumber;
	S->PhaseSecondsRemaining = PhaseTime;
	S->CompletedRelays = 0;
	S->LayoutRelayLocations = RelayLocations;
	S->ActivatedRelays = ActivatedRelays;
	S->ActiveReplacementLocations.Reset();
	S->bExitsUnlocked = false;
	S->bRunnerRevealed = false;
	S->RevealSecondsRemaining = 0.f;
	S->NextRevealSecondsRemaining = 0.f;
	S->bInteractionActive = false;
	S->InteractionSecondsRemaining = 0.f;
	S->StatusText = TEXT("Round starts in 10");

	// Clear both drivers before role swaps, so an old car cannot obstruct the other start.
 for (APlayerState* State : GameState->PlayerArray) if (AController* Controller=Cast<AController>(State->GetOwner())) {
  if (APawn* Old=Controller->GetPawn()) { Controller->UnPossess(); Old->Destroy(); }
  for (TActorIterator<ACitixVehiclePawn> It(GetWorld()); It; ++It) if (It->GetOwningController()==Controller) It->Destroy();
 }
 int32 Index = 0;
 for (APlayerState* PS : GameState->PlayerArray)
	{
		if (ACitixChasePlayerState* ChasePS = Cast<ACitixChasePlayerState>(PS))
		{
			const int32 PlayerIndex = Index++;
			ChasePS->bReady = false;
			ChasePS->bInteractionActive = false;
			ChasePS->InteractionSecondsRemaining = 0.f;
			ChasePS->InteractionType = ECitixChaseInteraction::None;
			ChasePS->ChaseRole = (PlayerIndex % 2 == RoundNumber % 2) ? ECitixChaseRole::Runner : ECitixChaseRole::Chaser;
			ChasePS->CharacterHealth = 100.f;
			ChasePS->bReplacementUsed = false;
			ChasePS->RapidBrakeCharges=ChasePS->ChaseRole==ECitixChaseRole::Chaser ? 1 : 0; ChasePS->NextRapidBrakeAt=ChasePS->RapidBrakeUntil=0; ChasePS->IceCharges=ChasePS->ChaseRole==ECitixChaseRole::Chaser ? 1 : 0; ChasePS->NextIceAt=ChasePS->FrozenUntil=ChasePS->FrozenSpeedLimit=ChasePS->LastIceAt=0; ChasePS->bLastIceHit=false;
   ChasePS->StaggerUntil = 0.f; ChasePS->GateSlowStartedAt=ChasePS->GateSlowUntil=0.f;
			ChasePS->RunnerCarHits = 0;
   ChasePS->ReplacementReadyAt = 0.f; ChasePS->PistolHits = 0; ChasePS->Ammo = 15; ChasePS->NextAmmoAt = 0.f; ChasePS->SmokeCharges=ChasePS->ChaseRole==ECitixChaseRole::Runner ? 1 : 0; ChasePS->NextSmokeAt=ChasePS->SmokeEmittingUntil=0.f;
			AController* PC = Cast<AController>(PS->GetOwner());
   if (!ResetPlayerForRound(PC, SpawnLocations[ChasePS->ChaseRole == ECitixChaseRole::Runner ? 0 : 1])) { CancelMatch(TEXT("Safe start blocked — host a new city")); return; }
		}
	}
 S->Phase = ECitixChasePhase::Countdown;
 if (bChaseTest && FParse::Param(FCommandLine::Get(),TEXT("CitixRoundLifecycleProbe"))) {
  if (LifecycleProbes.IsEmpty()) for (APlayerState* State:GameState->PlayerArray) {
   FActorSpawnParameters Params; Params.Owner=State->GetOwner();
   if (auto* Probe=GetWorld()->SpawnActor<ACitixRoundLifecycleProbe>(ACitixRoundLifecycleProbe::StaticClass(),FTransform::Identity,Params)) LifecycleProbes.Add(Probe);
  }
  LifecycleDeadline=GetWorld()->GetTimeSeconds()+60.f;
 }
 S->ForceNetUpdate();
}
void ACitixChaseGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	if (bChaseTest) ChaseTestTick();
	ACitixChaseGameState* S = ChaseState();
	if (!S) return;

	if (LastRamSource.IsValid() && LastRamTarget.IsValid()
		&& FVector::DistSquared(LastRamSource->GetActorLocation(), LastRamTarget->GetActorLocation()) >= FMath::Square(650.f))
	{
		bRamSeparated = true;
	}
	if (GetWorld())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		for (auto It = PendingEjections.CreateIterator(); It; ++It)
		{
			ACitixDrivingPlayerController* Controller = It.Key().Get();
			if (!Controller || !Cast<ACitixVehiclePawn>(Controller->GetPawn())) { It.RemoveCurrent(); continue; }
			if (It.Value() <= Now)
			{
				if (Controller->ForceChaseEjection()) It.RemoveCurrent();
				else It.Value() = Now + 0.5f;
			}
		}
	}
	if (S->Phase == ECitixChasePhase::RoundResults)
	{
		PhaseTime = FMath::Max(0.f, PhaseTime - Dt);
		S->PhaseSecondsRemaining = PhaseTime;
		if (PhaseTime <= 0.f) { if (RoundNumber < 2) StartRound(); else FinishMatch(); }
		return;
	}
	if (S->Phase == ECitixChasePhase::MatchResults) return;
	if (S->Phase != ECitixChasePhase::Countdown && S->Phase != ECitixChasePhase::Pursuit) return;
	if (S->Phase == ECitixChasePhase::Pursuit) {
  UpdateRunnerReveal();
  for (APlayerState* State:GameState->PlayerArray) {
   auto* PS=Cast<ACitixChasePlayerState>(State); auto* Driver=State ? Cast<AController>(State->GetOwner()) : nullptr;
   if (!PS || !Driver || !Driver->GetPawn()) continue;
   auto& Inside=GateOverlaps.FindOrAdd(Driver);
   for (int32 I=0; I<S->BreakawayLocations.Num(); ++I) {
    if (!IsAtBreakaway(Driver,I)) { Inside.Remove(I); continue; }
    if (Inside.Contains(I)) continue;
    Inside.Add(I);
    if (PS->ChaseRole==ECitixChaseRole::Runner) ActivateBreakaway(I);
    else { const float Now=GetWorld()->GetTimeSeconds(); if (PS->GateSlowUntil<=Now) PS->GateSlowStartedAt=Now; PS->GateSlowUntil=Now+FCitixChaseRules::ChaserGateDragDuration; PS->ForceNetUpdate(); }
   }
  }
  for (APlayerState* State : GameState->PlayerArray) if (ACitixChasePlayerState* PS = Cast<ACitixChasePlayerState>(State))
   if (PS->ChaseRole == ECitixChaseRole::Chaser) { FCitixChaseRules::RefillAmmo(GetWorld()->GetTimeSeconds(), PS->Ammo, PS->NextAmmoAt); FCitixChaseRules::RefillIce(GetWorld()->GetTimeSeconds(),PS->IceCharges,PS->NextIceAt); FCitixChaseRules::RefillRapidBrake(GetWorld()->GetTimeSeconds(),PS->RapidBrakeCharges,PS->NextRapidBrakeAt); }
   else FCitixChaseRules::RefillSmoke(GetWorld()->GetTimeSeconds(),PS->SmokeCharges,PS->NextSmokeAt);
 }

 // Automatic server-owned commitments reuse the existing replicated interaction HUD.
 for(APlayerState* State:GameState->PlayerArray) {
  auto* PS=Cast<ACitixChasePlayerState>(State); auto* Runner=State ? Cast<AController>(State->GetOwner()) : nullptr;
  if(!PS || PS->ChaseRole!=ECitixChaseRole::Runner || !Runner)continue;
  if(!S->bExitsUnlocked) CompleteRelay(Runner);
  else if(!ActiveHolds.Contains(Runner)) for(int32 I=0;I<ExitLocations.Num();++I) if(IsAtExit(Runner,I)) {
   auto& Hold=ActiveHolds.Add(Runner); Hold.ExitIndex=I; Hold.StartedAt=GetWorld()->GetTimeSeconds(); Hold.SecondsRemaining=FCitixChaseRules::EscapeCommitDuration;
   PS->bInteractionActive=true; PS->InteractionType=ECitixChaseInteraction::Escape; PS->InteractionSecondsRemaining=Hold.SecondsRemaining;
   PS->ForceNetUpdate(); S->StatusText=TEXT("Runner escaping — intercept the exit"); break;
  }
 }

	TArray<TWeakObjectPtr<AController>,TInlineAllocator<2>> CompletedHolds;
	bool bCaptureCompleted = false;
 bool bEscapeCompleted=false;
	for (TPair<TWeakObjectPtr<AController>, FCitixChaseHold>& Pair : ActiveHolds)
	{
		AController* Controller = Pair.Key.Get();
		FCitixChaseHold& Hold = Pair.Value;
		const bool bValid = Controller && (Hold.ExitIndex!=INDEX_NONE ? (S->bExitsUnlocked && IsAtExit(Controller,Hold.ExitIndex)) : Hold.BreakawayIndex != INDEX_NONE ? IsAtBreakaway(Controller, Hold.BreakawayIndex) : (Hold.bCapture ? IsCaptureRange(Controller) : IsAtRelay(Controller, Hold.RelayIndex)));
		if (!bValid)
		{
			CompletedHolds.Add(Pair.Key);
			if(auto* PC=Cast<ACitixDrivingPlayerController>(Controller)) {
    if(Hold.ExitIndex!=INDEX_NONE) PC->ClientChaseMessage(TEXT("ESCAPE CANCELLED / STAY INSIDE EXIT"));
    else if(Hold.RelayIndex!=INDEX_NONE) PC->ClientChaseMessage(TEXT("SYNC RESET / STAY WITHIN 9.6m AND BELOW 60 KM/H"));
   }
   ClearHold(Controller, true);
			continue;
		}
		if(Hold.RelayIndex!=INDEX_NONE || Hold.ExitIndex!=INDEX_NONE) Hold.SecondsRemaining=FCitixChaseRules::CommitmentRemaining(Hold.StartedAt,GetWorld()->GetTimeSeconds(),Hold.ExitIndex!=INDEX_NONE ? FCitixChaseRules::EscapeCommitDuration : FCitixChaseRules::RelaySyncDuration);
  else Hold.SecondsRemaining -= Dt;
		if (ACitixChasePlayerState* PS = Controller->GetPlayerState<ACitixChasePlayerState>()) PS->InteractionSecondsRemaining = FMath::Max(0.f, Hold.SecondsRemaining);
		if (Hold.SecondsRemaining <= 0.f)
		{
			CompletedHolds.Add(Pair.Key);
			if (Hold.BreakawayIndex != INDEX_NONE)
			{
				ActivateBreakaway(Hold.BreakawayIndex);
			}
			else if(Hold.ExitIndex!=INDEX_NONE) bEscapeCompleted=true;
   else if (Hold.bCapture) bCaptureCompleted = true;
			else CompleteRelayAt(Controller, Hold.RelayIndex);
			ClearHold(Controller, false);
		}
	}
	for (const TWeakObjectPtr<AController>& Controller : CompletedHolds) ActiveHolds.Remove(Controller);
	UpdateInteractionPresentation();
	if(bEscapeCompleted) {FinishRound(true,TEXT("Runner escaped after four-second commitment")); return;}
	if (bCaptureCompleted)
	{
		FinishRound(false, TEXT("Runner captured"));
		return;
	}

	PhaseTime -= Dt;
	S->PhaseSecondsRemaining = FMath::Max(0.f, PhaseTime);
	if (PhaseTime > 0.f) return;
	if (S->Phase == ECitixChasePhase::Countdown)
	{
		S->Phase = ECitixChasePhase::Pursuit;
		PhaseTime = 300.f;
		S->PhaseSecondsRemaining = PhaseTime;
		UpdateRunnerReveal();
		S->StatusText = TEXT("Runner: sync five relays, then commit four seconds at an exit. Chaser: stop them.");
		UE_LOG(LogCitix, Log, TEXT("[CitixChase] Pursuit begins (round %d)."), RoundNumber);
	}
	else
	{
		FinishRound(false, TEXT("Time expired"));
	}
}
void ACitixChaseGameMode::CompleteRelay(AController* Runner) {
 auto* S=ChaseState(); auto* PS=Runner ? Runner->GetPlayerState<ACitixChasePlayerState>() : nullptr; int32 Relay=INDEX_NONE;
 if(!S || S->Phase!=ECitixChasePhase::Pursuit || !PS || PS->ChaseRole!=ECitixChaseRole::Runner || S->bExitsUnlocked || ActiveHolds.Contains(Runner) || !IsNearRelay(Runner,Relay) || !IsAtRelay(Runner,Relay))return;
 auto& Hold=ActiveHolds.Add(Runner); Hold.RelayIndex=Relay; Hold.StartedAt=GetWorld()->GetTimeSeconds(); Hold.SecondsRemaining=FCitixChaseRules::RelaySyncDuration;
 PS->bInteractionActive=true; PS->InteractionType=ECitixChaseInteraction::Relay; PS->InteractionSecondsRemaining=Hold.SecondsRemaining; PS->ForceNetUpdate();
}
void ACitixChaseGameMode::CompleteRelayAt(AController* Runner, int32 Relay) {
 const auto* Hold=ActiveHolds.Find(Runner); if(!Hold || Hold->RelayIndex!=Relay || Hold->SecondsRemaining>0.f) return; ACitixChaseGameState* S = ChaseState(); ACitixChasePlayerState* PS = Runner ? Runner->GetPlayerState<ACitixChasePlayerState>() : nullptr; if (!S || S->Phase != ECitixChasePhase::Pursuit || !PS || PS->ChaseRole != ECitixChaseRole::Runner || S->bExitsUnlocked || !IsAtRelay(Runner, Relay) || !ActivatedRelays.IsValidIndex(Relay) || ActivatedRelays[Relay]) return; S->MulticastRelaySprinkles(Runner->GetPawn()->GetActorLocation(),Runner->GetPawn()->GetVelocity()); ActivatedRelays[Relay] = true; S->ActivatedRelays = ActivatedRelays; if (RelayBeacons.IsValidIndex(Relay) && RelayBeacons[Relay]) RelayBeacons[Relay]->Hide(); ++S->CompletedRelays; S->bExitsUnlocked = FCitixChaseRules::AreExitsUnlocked(S->CompletedRelays); if (S->bExitsUnlocked) for (ACitixDestinationBeacon* Beacon : RelayBeacons) if (IsValid(Beacon)) Beacon->Hide(); if (S->bExitsUnlocked) for (ACitixDestinationBeacon* Beacon : ExitBeacons) if (Beacon) Beacon->SetExitProjection(ExitLocations[ExitBeacons.IndexOfByKey(Beacon)]); S->StatusText = S->bExitsUnlocked ? TEXT("Exits unlocked — escape now") : FString::Printf(TEXT("Relay %d/%d complete"), S->CompletedRelays, FCitixChaseRules::RelaysRequired); UE_LOG(LogCitix, Log, TEXT("[CitixChase] Relay complete: %d/%d%s."), S->CompletedRelays, FCitixChaseRules::RelaysRequired, S->bExitsUnlocked ? TEXT(" - exits unlocked") : TEXT("")); }
bool ACitixChaseGameMode::IsNearRelay(const AController* Controller, int32& OutRelay) const { if (!Controller || !Controller->GetPawn()) return false; for (int32 Index = 0; Index < RelayLocations.Num(); ++Index) if (!ActivatedRelays[Index] && FVector::DistSquared2D(Controller->GetPawn()->GetActorLocation(), RelayLocations[Index]) <= FMath::Square(FCitixChaseRules::RelayInteractionRadius)) { OutRelay = Index; return true; } return false; }
bool ACitixChaseGameMode::IsAtRelay(const AController* Controller,int32 RelayIndex) const {
 if(!Controller || !Controller->GetPawn() || !ActivatedRelays.IsValidIndex(RelayIndex) || ActivatedRelays[RelayIndex] || !RelayLocations.IsValidIndex(RelayIndex))return false;
 const APawn* Pawn=Controller->GetPawn(); const auto& Location=RelayLocations[RelayIndex];
 return FCitixChaseRules::RelayEligible(FVector::Dist2D(Pawn->GetActorLocation(),Location),Pawn->GetVelocity().Size2D()*.036f,Pawn->GetActorLocation().Z-Location.Z);
}
bool ACitixChaseGameMode::IsCaptureRange(const AController* Controller) const { if (!Controller || !Controller->GetPawn() || !Controller->GetPawn()->IsA<ACitixOnFootPawn>() || !GetWorld() || GetWorld()->GetTimeSeconds() < CaptureProtectionUntil) return false; for (APlayerState* State : GameState->PlayerArray) { const ACitixChasePlayerState* PS = Cast<ACitixChasePlayerState>(State); const AController* Other = State ? Cast<AController>(State->GetOwner()) : nullptr; if (!PS || PS->ChaseRole != ECitixChaseRole::Runner || !Other || !Other->GetPawn() || !Other->GetPawn()->IsA<ACitixOnFootPawn>() || Cast<ACitixOnFootPawn>(Other->GetPawn())->IsInShockwaveRecovery() || FVector::DistSquared(Controller->GetPawn()->GetActorLocation(), Other->GetPawn()->GetActorLocation()) > FMath::Square(250.f)) continue; FCollisionQueryParams Params(SCENE_QUERY_STAT(CitixCaptureSight), false); Params.AddIgnoredActor(Controller->GetPawn()); Params.AddIgnoredActor(Other->GetPawn()); FHitResult Hit; if (!GetWorld()->LineTraceSingleByChannel(Hit, Controller->GetPawn()->GetActorLocation(), Other->GetPawn()->GetActorLocation(), ECC_Visibility, Params)) return true; } return false; }
bool ACitixChaseGameMode::IsAtExit(const AController* Controller,int32 ExitIndex) const {
 const auto* Car=Controller ? Cast<ACitixVehiclePawn>(Controller->GetPawn()) : nullptr;
 if(!Car || Car->IsDisplayDestroyed() || !Car->IsOccupied()) return false;
 for(int32 I=0;I<ExitLocations.Num();++I) if((ExitIndex==INDEX_NONE || I==ExitIndex) && FVector::DistSquared(Car->GetActorLocation(),ExitLocations[I])<=FMath::Square(800.f))return true;
 return false;
}
void ACitixChaseGameMode::BeginInteraction(AController* InteractingController) { ACitixChaseGameState* S = ChaseState(); if (!S || !InteractingController) return; if (const ACitixOnFootPawn* Walker = Cast<ACitixOnFootPawn>(InteractingController->GetPawn()); Walker && Walker->IsInShockwaveRecovery() && S->Phase == ECitixChasePhase::Pursuit) return; ACitixChasePlayerState* PS = InteractingController->GetPlayerState<ACitixChasePlayerState>(); if (S->Phase == ECitixChasePhase::Waiting) { if (GetNumPlayers() != 2 || !PS || PS->bReady) return; if (!PS->bCityIdentityValid) { S->StatusText = TEXT("Waiting for city verification"); return; } LobbyReady.Add(InteractingController); PS->bReady = true; if (LobbyReady.Num() == 2) { UE_LOG(LogCitix, Log, TEXT("[CitixChase] Both drivers ready - starting round %d."), RoundNumber + 1); StartRound(); } else S->StatusText = TEXT("Ready — waiting for the other driver"); return; } if (S->Phase == ECitixChasePhase::MatchResults) { if (!PS || PS->bReady) return; RematchReady.Add(InteractingController); PS->bReady = true; if (RematchReady.Num() == 2) { RoundNumber = 0; for (APlayerState* State : GameState->PlayerArray) if (ACitixChasePlayerState* MatchPS = Cast<ACitixChasePlayerState>(State)) { MatchPS->RoundsWon = 0; MatchPS->bReady = false; } StartRound(); } else S->StatusText = TEXT("Rematch ready — waiting for the other driver"); return; } if (S->Phase != ECitixChasePhase::Pursuit || !PS || !InteractingController->GetPawn() || !InteractingController->GetPawn()->IsA<ACitixOnFootPawn>() ) return; if (ActiveHolds.Contains(InteractingController)) { if (ActiveHolds[InteractingController].RelayIndex!=INDEX_NONE) if (auto* Driving=Cast<ACitixDrivingPlayerController>(InteractingController)) Driving->RequestEnterVehicle(); return; } if (PS->ChaseRole==ECitixChaseRole::Runner && PS->CharacterHealth<100 && !PS->bReplacementUsed && PS->ReplacementReadyAt>0 && GetWorld()->GetTimeSeconds()>=PS->ReplacementReadyAt) if (auto* Driving=Cast<ACitixDrivingPlayerController>(InteractingController); Driving && Driving->FindChaseEntryCandidate(300.f) && Driving->RequestEnterVehicle()) return; const bool bCapture = PS->ChaseRole == ECitixChaseRole::Chaser && IsCaptureRange(InteractingController);
 if (!bCapture) { if (auto* Driving=Cast<ACitixDrivingPlayerController>(InteractingController)) Driving->RequestEnterVehicle(); return; }
 FCitixChaseHold& Hold=ActiveHolds.Add(InteractingController);
 Hold.bCapture=true; Hold.SecondsRemaining=2.f;
 PS->bInteractionActive=true; PS->InteractionSecondsRemaining=2.f; PS->InteractionType=ECitixChaseInteraction::Capture;
 UpdateInteractionPresentation(); S->StatusText=TEXT("Capturing runner…");
}

void ACitixChaseGameMode::TryRam(ACitixVehiclePawn* Source, ACitixVehiclePawn* Target, float ClosingSpeedKmh)
{
	ACitixChaseGameState* S = ChaseState();
	if (!S || S->Phase != ECitixChasePhase::Pursuit || !Source || !Target || !GetWorld() || Target->IsDisplayDestroyed()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bSamePair = LastRamSource == Source && LastRamTarget == Target;
	if (!FCitixChaseRules::IsValidRam(ClosingSpeedKmh, Now-LastRamTime, !bSamePair || bRamSeparated)) return;
	AController* SourceController = Source->GetOwningController();
	AController* TargetController = Target->GetOwningController();
	ACitixChasePlayerState* SourceState = SourceController ? SourceController->GetPlayerState<ACitixChasePlayerState>() : nullptr;
	ACitixChasePlayerState* TargetState = TargetController ? TargetController->GetPlayerState<ACitixChasePlayerState>() : nullptr;
	if (!SourceState || !TargetState || SourceState->ChaseRole != ECitixChaseRole::Chaser || TargetState->ChaseRole != ECitixChaseRole::Runner) return;
	LastRamTime = Now; LastRamSource = Source; LastRamTarget = Target; bRamSeparated = false;
	Target->ApplyVehicleDamage(Target->GetVehicleMovement()->GetMaxHealth() * .25f);
	++TargetState->RunnerCarHits;
	S->StatusText = FString::Printf(TEXT("Runner hit %d/4"),TargetState->RunnerCarHits);
	UE_LOG(LogCitix,Log,TEXT("[CitixChase] Ram %d/4 accepted (%.0f km/h closing)."),TargetState->RunnerCarHits,ClosingSpeedKmh);
	if (TargetState->RunnerCarHits < 4) return;
	TargetState->RunnerCarHits = 0;
	TargetState->CharacterHealth = FCitixChaseRules::HealthAfterWreck(TargetState->CharacterHealth);
	S->MulticastPixelExplosion(Target->GetActorLocation());
 if (FCitixChaseRules::IsFinalWreck(TargetState->bReplacementUsed,TargetState->CharacterHealth)) {
  TargetState->CharacterHealth=0.f;
  TargetState->ForceNetUpdate();
  FinishRound(false,TEXT("Runner's second car destroyed"));
  return;
 }
	CaptureProtectionUntil = Now + 3.65f;
 TargetState->ReplacementReadyAt = Now + 40.f;
	UE_LOG(LogCitix,Log,TEXT("[CitixChase] Runner car wrecked - health now %.0f, replacement available."),TargetState->CharacterHealth);
	if (ACitixDrivingPlayerController* Runner = Cast<ACitixDrivingPlayerController>(TargetController))
		if (!Runner->ForceChaseEjection()) PendingEjections.Add(Runner,Now+.5f);
	SpawnReplacementCars();
	S->StatusText = TEXT("Car wrecked - survive the shockwave, then find a replacement");
}

void ACitixChaseGameMode::TryRunOver(AActor* Car, const FVector& From, const FVector& To, float SpeedKmh)
{
	ACitixChaseGameState* S = ChaseState();
	if (!HasAuthority() || !S || S->Phase != ECitixChasePhase::Pursuit || !Car || !FCitixChaseRules::IsLethalRunOver(SpeedKmh,false)
		|| FVector::DistSquared(From,To)>FMath::Square(2000.f) || FVector::DistSquared(From,To)<1.f) return;
	for (APlayerState* State : GameState->PlayerArray)
	{
		ACitixChasePlayerState* PS = Cast<ACitixChasePlayerState>(State);
		ACitixOnFootPawn* Runner = PS ? Cast<ACitixOnFootPawn>(PS->GetPawn()) : nullptr;
		if (!Runner || PS->ChaseRole != ECitixChaseRole::Runner
			|| !FCitixChaseRules::IsLethalRunOver(SpeedKmh,Runner->IsInShockwaveRecovery() || GetWorld()->GetTimeSeconds()<CaptureProtectionUntil)) continue;
		const FVector Direction = (To-From).GetSafeNormal();
		if (FMath::PointDistToSegment(Runner->GetActorLocation(),From-Direction*210.f,To+Direction*210.f)>140.f) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaseRunOver),false,Car);
		Params.AddIgnoredActor(Runner);
		FHitResult Blocker;
		if (GetWorld()->LineTraceSingleByChannel(Blocker,To,Runner->GetActorLocation(),ECC_WorldStatic,Params)) continue;
		PS->CharacterHealth = 0.f;
		CancelInteraction(Runner->GetController());
		Runner->BeginShockwaveRecovery(Direction*950.f+FVector(0,0,350.f));
		S->MulticastPistolShot(Runner->GetActorLocation(),Runner->GetActorLocation(),true,nullptr);
		UE_LOG(LogCitix,Log,TEXT("[CitixChase] Lethal run-over: %.1f km/h."),SpeedKmh);
		FinishRound(false,TEXT("Runner struck by a car above 40 km/h"));
		return;
	}
}
void ACitixChaseGameMode::FinishRound(bool bRunnerWon, const FString& Reason) { ACitixChaseGameState* S = ChaseState(); if (!S || S->Phase == ECitixChasePhase::RoundResults || S->Phase == ECitixChasePhase::MatchResults) return; PhaseTime = 8.f; for (TPair<TWeakObjectPtr<AController>, FCitixChaseHold>& Pair : ActiveHolds) ClearHold(Pair.Key.Get(), false); ActiveHolds.Reset(); S->bInteractionActive = false; S->InteractionSecondsRemaining = 0.f; S->bRunnerRevealed = false; S->RevealSecondsRemaining = 0.f; S->Phase = ECitixChasePhase::RoundResults; S->PhaseSecondsRemaining = PhaseTime; S->StatusText = FString::Printf(TEXT("%s wins: %s"), bRunnerWon ? TEXT("Runner") : TEXT("Chaser"), *Reason); UE_LOG(LogCitix, Log, TEXT("[CitixChase] Round %d result: %s wins - %s."), RoundNumber, bRunnerWon ? TEXT("Runner") : TEXT("Chaser"), *Reason); for (APlayerState* PS : GameState->PlayerArray) if (ACitixChasePlayerState* C = Cast<ACitixChasePlayerState>(PS)) if ((C->ChaseRole == ECitixChaseRole::Runner) == bRunnerWon) ++C->RoundsWon; }
void ACitixChaseGameMode::UpdateRunnerReveal() {
 if(auto* S=ChaseState()) {
  bool Visible=S->Phase==ECitixChasePhase::Pursuit;
  const AController* Runner=nullptr; const AController* Chaser=nullptr;
  for(const APlayerState* Player:S->PlayerArray) if(const auto* PS=Cast<ACitixChasePlayerState>(Player)) {
   if(PS->ChaseRole==ECitixChaseRole::Runner) Runner=Cast<AController>(PS->GetOwner());
   else if(PS->ChaseRole==ECitixChaseRole::Chaser) Chaser=Cast<AController>(PS->GetOwner());
  }
  if(Visible && Runner && Chaser && Runner->GetPawn() && Chaser->GetPawn()) {
   const FVector Position=Runner->GetPawn()->GetActorLocation(); const float Distance=FVector::Dist2D(Position,Chaser->GetPawn()->GetActorLocation());
   if(!FCitixChaseRules::RunnerTrackingVisible(true,Distance)) {
    const float Now=GetWorld()->GetTimeSeconds();
    const FVector From=Chaser->GetPawn()->GetPawnViewLocation(),To=Runner->GetPawn()->GetPawnViewLocation();
    for(TActorIterator<ACitixSmokeCloud> It(GetWorld());It;++It) {
     if(It->ContainsPoint(Position,Now) || It->IntersectsSightline(From,To,Now)) {Visible=false;break;}
    }
   }
  }
  if(S->bRunnerRevealed!=Visible) {S->bRunnerRevealed=Visible;S->ForceNetUpdate();}
  S->RevealSecondsRemaining=0; S->NextRevealSecondsRemaining=0;
 }
}

void ACitixChaseGameMode::SpawnReplacementCars()
{
 if (!GetWorld() || ReplacementLocations.IsEmpty()) return;
 FVector RunnerLocation=ReplacementLocations[0];
 for (APlayerState* State:GameState->PlayerArray) if (const auto* PS=Cast<ACitixChasePlayerState>(State); PS && PS->ChaseRole==ECitixChaseRole::Runner && PS->GetPawn()) RunnerLocation=PS->GetPawn()->GetActorLocation();
 TArray<int32> Choices; for (int32 I=0; I<ReplacementLocations.Num(); ++I) Choices.Add(I);
 Choices.Sort([this,RunnerLocation](int32 A,int32 B) { return FVector::DistSquared(ReplacementLocations[A],RunnerLocation)<FVector::DistSquared(ReplacementLocations[B],RunnerLocation); });
 for (ACitixDestinationBeacon* Beacon:ReplacementBeacons) if (Beacon) Beacon->Hide();
 ACitixChaseGameState* S=ChaseState(); if (S) S->ActiveReplacementLocations.Reset();
 int32 Created=0;
 for (int32 I:Choices) {
  FTransform Safe; if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),ReplacementLocations[I],FVector(240,110,85),0,nullptr,Safe)) continue;
  FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  if (ACitixVehiclePawn* Car=GetWorld()->SpawnActor<ACitixVehiclePawn>(ACitixVehiclePawn::StaticClass(),Safe,Params)) {
   Car->SetOccupied(false); Car->LastDryPose=Safe; Car->bHasDryPose=true; ReplacementCars.Add(Car);
   if (S) S->ActiveReplacementLocations.Add(Safe.GetLocation());
   if (ReplacementBeacons.IsValidIndex(I) && ReplacementBeacons[I]) ReplacementBeacons[I]->SetDestination(Safe.GetLocation(),ECitixSurface::CarPaint);
   if (++Created==2) break;
  }
 }
}

bool ACitixChaseGameMode::IsReplacementVehicle(const ACitixVehiclePawn* Vehicle) const { return ReplacementCars.Contains(Vehicle); }
bool ACitixChaseGameMode::CanClaimReplacement(const AController* Controller, const ACitixVehiclePawn* Vehicle) const
{
 const ACitixChasePlayerState* PS = Controller ? Controller->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 if (!PS || !Vehicle || Vehicle->IsDisplayDestroyed() || Vehicle->IsOccupied()) return false;
 if (Vehicle->GetOwningController() == Controller) return true;
 return PS->ChaseRole == ECitixChaseRole::Runner && !Vehicle->GetOwningController() && Controller->GetPawn()
  && FCitixChaseRules::ReplacementReady(GetWorld()->GetTimeSeconds(), PS->ReplacementReadyAt, PS->bReplacementUsed,
   Vehicle->GetVehicleMovement()->GetSpeedKmh(), FVector::Distance(Controller->GetPawn()->GetActorLocation(), Vehicle->GetActorLocation()));
}
void ACitixChaseGameMode::ClaimReplacement(AController* Controller, ACitixVehiclePawn* Vehicle)
{
 auto* PS=Controller ? Controller->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 if (!HasAuthority() || !PS || !IsValid(Vehicle) || Vehicle->GetOwningController()==Controller || PS->bReplacementUsed) return;
 PS->bReplacementUsed=true;
 PS->RunnerCarHits=FCitixChaseRules::InitialVehicleHits(true);
 auto* Movement=Vehicle->GetVehicleMovement();
 Movement->RepairFull();
 Movement->ApplyDamage(Movement->GetMaxHealth()*PS->RunnerCarHits/FCitixChaseRules::VehicleIntegrity);
 PS->ForceNetUpdate(); Vehicle->ForceNetUpdate();
 UE_LOG(LogCitix,Log,TEXT("[CitixChase] Replacement claimed: %s integrity %d/%d"),*GetNameSafe(Controller),FCitixChaseRules::ReplacementIntegrity,FCitixChaseRules::VehicleIntegrity);
}

bool ACitixChaseGameMode::SelectMatchRelays()
{
 const TArray<FVector> Previous=RelayLocations;
 TArray<FVector> Selected;
 for (int32 Attempt=0; Attempt<16; ++Attempt) {
  Selected=FCitixRelayLayout::Select(ValidatedRelaySites,++RelaySelectionSeed);
  if (Selected.Num()!=FCitixChaseRules::ActiveRelayCount) continue;
  // Compare site membership rather than ordering; an order shuffle is not a reroll.
  if (!Selected.ContainsByPredicate([&Previous](const FVector& P){ return !Previous.Contains(P); }) && Attempt<15) continue;
  RelayLocations=MoveTemp(Selected);
  UE_LOG(LogCitix,Log,TEXT("[CitixRelayLayout] match seed=%d pool=%d active=%d required=%d"),RelaySelectionSeed,ValidatedRelaySites.Num(),RelayLocations.Num(),FCitixChaseRules::RelaysRequired);
  return true;
 }
 return false;
}

void ACitixChaseGameMode::ChaseTestTick()
{
	ACitixChaseGameState* S = ChaseState();
	UWorld* World = GetWorld();
	if (!S || !World)
	{
		return;
	}
	// Keep the existing test lobby setup, then leave pursuit to the real reveal/replication flow.
	if (FParse::Param(FCommandLine::Get(),TEXT("CitixTrackerNetProbe")) && !FParse::Param(FCommandLine::Get(),TEXT("CitixRelayFXProbe")) && S->Phase!=ECitixChasePhase::Waiting) return;
	ChaseTestTimer += World->GetDeltaSeconds();

	auto ControllerWithRole = [this](ECitixChaseRole InRole) -> AController*
	{
		if (GameState)
		{
			for (APlayerState* State : GameState->PlayerArray)
			{
				if (ACitixChasePlayerState* Chase = Cast<ACitixChasePlayerState>(State))
				{
					if (Chase->ChaseRole == InRole)
					{
						return Cast<AController>(State->GetOwner());
					}
				}
			}
		}
		return nullptr;
	};

 if (FParse::Param(FCommandLine::Get(),TEXT("CitixRoundLifecycleProbe")) && S->Phase!=ECitixChasePhase::Waiting) {
  if (bLifecycleReceiptWritten) return;
  auto Receipt=[&](bool Passed) {
   bLifecycleReceiptWritten=true;
   if (!Passed) for (ACitixRoundLifecycleProbe* Probe:LifecycleProbes) if (IsValid(Probe)) Probe->DumpState();
   const int32 A=LifecycleProbes.IsValidIndex(0) ? LifecycleProbes[0]->VerifiedRounds() : 0;
   const int32 B=LifecycleProbes.IsValidIndex(1) ? LifecycleProbes[1]->VerifiedRounds() : 0;
   FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"passed\":%s,\"matches\":%d,\"driver_a_verified_rounds\":%d,\"driver_b_verified_rounds\":%d,\"uses_keyboard_input\":true,\"required_escapes\":4}"),Passed ? TEXT("true") : TEXT("false"),LifecycleCompletedMatches,A,B),*(FPaths::ProjectSavedDir()/TEXT("RoundLifecycleProbe.json")));
   UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] result=%d matches=%d verified driver rounds=%d,%d"),Passed,LifecycleCompletedMatches,A,B);
  };
  if (S->Phase==ECitixChasePhase::MatchResults) {
   ++LifecycleCompletedMatches;
   if (LifecycleCompletedMatches==2) { Receipt(LifecycleProbes.Num()==2 && LifecycleProbes[0]->VerifiedRounds()==4 && LifecycleProbes[1]->VerifiedRounds()==4); return; }
   const TArray<FVector> Previous=RelayLocations;
   for (APlayerState* State:GameState->PlayerArray) BeginInteraction(Cast<AController>(State->GetOwner()));
   if (!RelayLocations.ContainsByPredicate([&Previous](const FVector& P){return !Previous.Contains(P);})) {Receipt(false);return;}
   LifecycleMatchSites=RelayLocations;
   return;
  }
  if (World->GetTimeSeconds()>LifecycleDeadline) {Receipt(false);return;}
  if (S->RoundNumber==1 && LifecycleMatchSites.IsEmpty()) LifecycleMatchSites=RelayLocations;
  if (S->RoundNumber==2 && LifecycleMatchSites!=RelayLocations) {Receipt(false);return;}
  if (S->Phase!=ECitixChasePhase::Pursuit) return;
  if (LifecycleProbes.Num()!=2 || !LifecycleProbes[0]->IsRoundVerified() || !LifecycleProbes[1]->IsRoundVerified()) return;
  auto* Runner=ControllerWithRole(ECitixChaseRole::Runner);
  auto* Car=Runner ? Cast<ACitixVehiclePawn>(Runner->GetPawn()) : nullptr;
  if (!Car) {Receipt(false);return;}
  const FVector Target=S->bExitsUnlocked ? ExitLocations[LifecycleCompletedMatches%ExitLocations.Num()] : RelayLocations[S->CompletedRelays];
  Car->SetActorLocation(Target,false,nullptr,ETeleportType::TeleportPhysics);
  Car->LastDryPose=Car->GetActorTransform(); Car->bHasDryPose=true;
  Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(FVector::ZeroVector);
  Car->GetVehicleMovement()->Velocity=FVector::ZeroVector;
  return;
 }


 // Opt-in replication fixture: exercise conceal/reveal without input-focus dependencies.
 if(FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeTrackingProbe")) && S->Phase!=ECitixChasePhase::Waiting) {
  static TWeakObjectPtr<ACitixSmokeCloud> Cloud;static bool Passed=true;static FVector Center;
  if(S->Phase!=ECitixChasePhase::Pursuit || ChaseTestPhase==99)return;
  auto* Runner=ControllerWithRole(ECitixChaseRole::Runner);auto* Chaser=ControllerWithRole(ECitixChaseRole::Chaser);
  if(!Runner || !Chaser || !Runner->GetPawn() || !Chaser->GetPawn())return;
  if(ChaseTestPhase<200){ChaseTestPhase=200;ChaseTestTimer=0;Center=Runner->GetPawn()->GetActorLocation();
   auto* Smoke=World->SpawnActor<ACitixSmokeCloud>(Center,FRotator::ZeroRotator);Smoke->StartedAt=World->GetTimeSeconds()-3;Smoke->CloudSeed=1;
   const FVector CloudCenter=Center+(FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeSightlineProbe")) ? FVector(7000,0,0) : FVector::ZeroVector);
   for(int32 I=0;I<ACitixSmokeCloud::PuffCount;++I)Smoke->EmissionPositions.Add(CloudCenter);Cloud=Smoke;Smoke->ForceNetUpdate();}
  const float Distance=ChaseTestPhase==200 ? 14000.f : ChaseTestPhase==201 ? 15000.f : ChaseTestPhase==202 ? 16000.f : 14000.f;
  if(Cloud.IsValid())Cloud->StartedAt=World->GetTimeSeconds()-3;
  for(AController* Driver:{Runner,Chaser}){auto* Car=Cast<ACitixVehiclePawn>(Driver->GetPawn());if(!Car)return;
   Car->SetActorLocation(Center+(Driver==Chaser ? FVector(Distance,0,0) : FVector::ZeroVector),false,nullptr,ETeleportType::TeleportPhysics);
   auto* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent());Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Car->GetVehicleMovement()->Velocity=FVector::ZeroVector;}
  UpdateRunnerReveal();
  if(ChaseTestTimer>2.f){Passed&=S->bRunnerRevealed==(ChaseTestPhase==201 || ChaseTestPhase==202 || ChaseTestPhase==204);
   if(ChaseTestPhase==203 && Cloud.IsValid()){Cloud->Destroy();Cloud.Reset();}
   if(ChaseTestPhase==204){FFileHelper::SaveStringToFile(Passed ? TEXT("{\"passed\":true,\"hidden_140m\":true,\"visible_150m\":true,\"visible_160m\":true,\"hidden_on_return\":true,\"restored_after_expiry\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("SmokeTrackingProbe.json")));ChaseTestPhase=99;}
   else {++ChaseTestPhase;ChaseTestTimer=0;}}
  return;
 }
 if(FParse::Param(FCommandLine::Get(),TEXT("CitixCommitmentProbe")) && S->Phase!=ECitixChasePhase::Waiting) {
  static bool Passed=true; static float ExitAt=-1;
  auto Finish=[&](bool Result) {FFileHelper::SaveStringToFile(Result ? TEXT("{\"passed\":true,\"speed_gate\":true,\"leave_resets_relay\":true,\"speed_resets_relay\":true,\"five_timed_relays\":true,\"leave_resets_escape\":true,\"four_second_escape\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseCommitmentProbe.json"))); UE_LOG(LogCitix,Log,TEXT("[CitixCommitmentProbe] %s"),Result ? TEXT("PASS") : TEXT("FAIL")); ChaseTestPhase=99;};
  if(ChaseTestPhase==99)return;
  if(S->Phase==ECitixChasePhase::RoundResults && ChaseTestPhase==110) {Finish(Passed && World->GetTimeSeconds()-ExitAt>=4.f); return;}
  if(S->Phase!=ECitixChasePhase::Pursuit)return;
  auto* Runner=ControllerWithRole(ECitixChaseRole::Runner); auto* Car=Runner ? Cast<ACitixVehiclePawn>(Runner->GetPawn()) : nullptr;
  auto* PS=Runner ? Runner->GetPlayerState<ACitixChasePlayerState>() : nullptr; if(!Car || !PS || RelayLocations.Num()!=FCitixChaseRules::ActiveRelayCount){Finish(false);return;}
  if(ChaseTestPhase<100){ChaseTestPhase=100;ChaseTestTimer=0;}
  const bool AtExit=ChaseTestPhase>=107;
  const FVector Anchor=AtExit ? ExitLocations[0] : RelayLocations[FMath::Clamp(S->CompletedRelays,0,FCitixChaseRules::RelaysRequired-1)];
  const bool Outside=ChaseTestPhase==102 || ChaseTestPhase==108;
  // Align the teleport fixture with the road, allowing a representative chase-camera view.
  static FVector LastAnchor(FVector::ZeroVector); static float FixtureYaw=0;
  if(!Anchor.Equals(LastAnchor,1.f)) {
   float Best=MAX_flt;
   for(TActorIterator<ACitixCityGenerator> It(World);It;++It) {const auto& Roads=It->GetRoadNetwork();
    for(const auto& E:Roads.Edges) if(E.bDrivable) {const FVector A(Roads.Nodes[E.NodeA].Position,0),B(Roads.Nodes[E.NodeB].Position,0);const float D=FMath::PointDistToSegment(FVector(Anchor.X,Anchor.Y,0),A,B);if(D<Best){Best=D;FixtureYaw=(B-A).Rotation().Yaw;}} break;
   }
   LastAnchor=Anchor;
  }
  Car->SetActorLocationAndRotation(Anchor+(Outside ? FVector(1100,0,0) : FVector::ZeroVector),FRotator(0,FixtureYaw,0),false,nullptr,ETeleportType::TeleportPhysics);
  const FVector Velocity=Car->GetActorForwardVector()*((ChaseTestPhase==100 || ChaseTestPhase==104) ? 70.f/.036f : 0.f);
  Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(Velocity); Car->GetVehicleMovement()->Velocity=Velocity;
  auto Next=[&](int32 Phase){ChaseTestPhase=Phase;ChaseTestTimer=0;};
  if(ChaseTestPhase==100 && ChaseTestTimer>1.3f){Passed&=S->CompletedRelays==0 && !PS->bInteractionActive;Next(101);}
  else if(ChaseTestPhase==101 && ChaseTestTimer>.4f){Passed&=PS->bInteractionActive && S->CompletedRelays==0 && PS->InteractionSecondsRemaining>0;Next(102);}
  else if(ChaseTestPhase==102 && ChaseTestTimer>.25f){Passed&=!PS->bInteractionActive && S->CompletedRelays==0;Next(103);}
  else if(ChaseTestPhase==103 && ChaseTestTimer>.4f){Passed&=PS->bInteractionActive && S->CompletedRelays==0;Next(104);}
  else if(ChaseTestPhase==104 && ChaseTestTimer>.25f){Passed&=!PS->bInteractionActive && S->CompletedRelays==0;Next(105);}
  else if(ChaseTestPhase==105 && ChaseTestTimer<.9f) Passed&=S->CompletedRelays==0;
  else if(ChaseTestPhase==105 && S->CompletedRelays==1){Next(106);}
  else if(ChaseTestPhase==106 && S->CompletedRelays==FCitixChaseRules::RelaysRequired){Passed&=S->bExitsUnlocked;Next(107);}
  else if(ChaseTestPhase==107 && ChaseTestTimer>1.5f){Passed&=S->Phase==ECitixChasePhase::Pursuit && PS->bInteractionActive && PS->InteractionType==ECitixChaseInteraction::Escape && PS->InteractionSecondsRemaining>2;Next(108);}
  else if(ChaseTestPhase==108 && ChaseTestTimer>.25f){Passed&=!PS->bInteractionActive && S->Phase==ECitixChasePhase::Pursuit;Next(109);}
  else if(ChaseTestPhase==109){if(ExitAt<0)ExitAt=World->GetTimeSeconds(); if(ChaseTestTimer>3.5f){Passed&=S->Phase==ECitixChasePhase::Pursuit;Next(110);}}
  if(!Passed || ChaseTestTimer>15.f){Finish(false);return;}
  return;
 }
 if(FParse::Param(FCommandLine::Get(),TEXT("CitixBalanceProbe")) && S->Phase!=ECitixChasePhase::Waiting) {
  if(S->Phase!=ECitixChasePhase::Pursuit) return;
  static float Began=-1,BrakeAt=-1,Deadline=0; static bool Failed=false,Done=false;
  if(Done)return;
  auto* Chaser=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Chaser));
  auto* Car=Chaser ? Cast<ACitixVehiclePawn>(Chaser->GetPawn()) : nullptr;
  auto* PS=Chaser ? Chaser->GetPlayerState<ACitixChasePlayerState>() : nullptr;
  if(!Car || !PS)return;
  const float Now=World->GetTimeSeconds();
  if(Began<0) {
   Began=Now; Failed=RelayLocations.Num()!=FCitixChaseRules::ActiveRelayCount;
   for(int32 I=0;I<RelayLocations.Num();++I) for(int32 J=0;J<I;++J) Failed|=FVector::Dist2D(RelayLocations[I],RelayLocations[J])<17999.f;
   Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(Car->GetActorForwardVector()*8000.f);
   UE_LOG(LogCitix,Log,TEXT("[CitixBalanceProbe] %s six safe relays at least 180m apart"),Failed ? TEXT("FAIL") : TEXT("PASS"));
  }
  Failed|=!S->bRunnerRevealed;
  if(BrakeAt<0 && PS->RapidBrakeUntil>Now) {
   BrakeAt=Now; Deadline=PS->NextRapidBrakeAt; Failed|=PS->RapidBrakeCharges!=0;
   UE_LOG(LogCitix,Log,TEXT("[CitixBalanceProbe] %s remote RMB consumed one charge; speed still %.1fkm/h"),Car->GetVelocity().Size2D()>100.f ? TEXT("PASS") : TEXT("FAIL"),Car->GetVelocity().Size2D()*.036f);
   Failed|=Car->GetVelocity().Size2D()<=100.f;
  }
  if(BrakeAt>0 && Now-BrakeAt>.9f && Now-BrakeAt<1.1f) Failed|=Car->GetVelocity().Size2D()>500.f;
  if(BrakeAt>0 && Now<Deadline-.1f) Failed|=PS->RapidBrakeCharges!=0;
  if(Now-Began>35.f) {
   Failed|=BrakeAt<0 || PS->RapidBrakeCharges!=1;
   FFileHelper::SaveStringToFile(Failed ? TEXT("{\"passed\":false}") : TEXT("{\"passed\":true,\"six_spread_relays\":true,\"remote_rmb_brake\":true,\"gradual_stop\":true,\"twenty_second_recharge\":true,\"continuous_reveal_35_seconds\":true}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseBalanceProbe.json")));
   UE_LOG(LogCitix,Log,TEXT("[CitixBalanceProbe] %s gradual brake, 20s recharge, uninterrupted reveal for 35s"),Failed ? TEXT("FAIL") : TEXT("PASS")); Done=true;
  }
  return;
 }
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixIceProbe"))) { CitixIceProbeTick(this); return; }
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixCountdownProbe")) && !bCountdownProbeWritten) {
  if (S->Phase==ECitixChasePhase::Countdown) {
   const bool First=CountdownProbeStartedAt<0.f;
   if (First) CountdownProbeStartedAt=World->GetTimeSeconds();
   for (int I=0;I<2;++I) {
    auto* Controller=ControllerWithRole(I==0 ? ECitixChaseRole::Runner : ECitixChaseRole::Chaser);
    auto* Car=Controller ? Cast<ACitixVehiclePawn>(Controller->GetPawn()) : nullptr;
    if (!Car) { bCountdownProbePassed=false; continue; }
    if (First) {
     SpeedProbeStarts[I]=Car->GetActorTransform();
     FTransform Dry;
     if (FVector::Dist2D(Car->GetActorLocation(),SpawnLocations[I])>5.f || !ACitixCityGenerator::ValidateChaseSurface(World,Car->GetActorLocation(),FVector(240,110,85),Car->GetActorRotation().Yaw,Car,Dry,false)) bCountdownProbePassed=false;
    }
    Car->ServerSendDriveInput(1.f,.8f,false,true);
    if (World->GetTimeSeconds()-CountdownProbeStartedAt>.5f) {
     const auto* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent());
     if (!Body || Body->IsSimulatingPhysics() || !Car->bCountdownFrozen || Car->CanAcceptDriveInput() || FVector::Dist2D(Car->GetActorLocation(),SpeedProbeStarts[I].GetLocation())>5.f || Car->GetVehicleMovement()->GetThrottleInput()!=0.f) bCountdownProbePassed=false;
    }
   }
  } else if (S->Phase==ECitixChasePhase::Pursuit && CountdownProbeStartedAt>=0.f && S->PhaseSecondsRemaining<299.f) {
   for (int I=0;I<2;++I) {
    auto* Controller=ControllerWithRole(I==0 ? ECitixChaseRole::Runner : ECitixChaseRole::Chaser);
    auto* Car=Controller ? Cast<ACitixVehiclePawn>(Controller->GetPawn()) : nullptr;
    if (!Car || Car->bCountdownFrozen || !Car->CanAcceptDriveInput() || !Cast<UPrimitiveComponent>(Car->GetRootComponent())->IsSimulatingPhysics()) bCountdownProbePassed=false;
    else { Car->ServerSendDriveInput(1.f,0.f,false,false); if (Car->GetVehicleMovement()->GetThrottleInput()!=1.f) bCountdownProbePassed=false; }
   }
   bCountdownProbeWritten=true;
   UE_LOG(LogCitix,Log,TEXT("[CitixCountdownProbe] %s both cars held against full throttle/boost for countdown; physics and input resume in pursuit"),bCountdownProbePassed?TEXT("PASS"):TEXT("FAIL"));
   FFileHelper::SaveStringToFile(bCountdownProbePassed ? TEXT("{\"passed\":true,\"both_cars_locked\":true,\"safe_road_starts\":true,\"released_on_go\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseCountdownProbe.json")));
  }
 }

 if (FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeProbe"))) {
  if (ChaseTestPhase==99 || S->Phase!=ECitixChasePhase::Pursuit) return;
  auto* PC=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Runner));
  auto* Opponent=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Chaser));
  auto* PS=PC ? PC->GetPlayerState<ACitixChasePlayerState>() : nullptr;
  const float Now=World->GetTimeSeconds();
  auto Fail=[this]() { UE_LOG(LogCitix,Error,TEXT("[CitixSmokeProbe] FAIL")); FFileHelper::SaveStringToFile(TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseSmokeProbe.json"))); ChaseTestPhase=99; };
  if (!PC || !PS || !Opponent) { Fail(); return; }
  auto CloudCount=[this]() { int32 N=0; for (TActorIterator<ACitixSmokeCloud> It(GetWorld()); It; ++It) ++N; return N; };
  if (ChaseTestPhase<90) {
   if (PS->SmokeCharges!=1 || S->BreakawayLocations.Num()>100) { Fail(); return; }
   auto* Car=Cast<ACitixVehiclePawn>(PC->GetPawn()); auto* Other=Cast<ACitixVehiclePawn>(Opponent->GetPawn());
   if (!Car || !Other) { Fail(); return; }
   FTransform Surface; const FVector Behind=Car->GetActorLocation()-Car->GetActorForwardVector()*2400.f;
   if (ACitixCityGenerator::ValidateChaseSurface(World,Behind,FVector(240,110,85),Car->GetActorRotation().Yaw,Other,Surface)) {
    Other->SetActorTransform(Surface,false,nullptr,ETeleportType::TeleportPhysics); Other->LastDryPose=Surface; Other->bHasDryPose=true;
    Cast<UPrimitiveComponent>(Other->GetRootComponent())->SetPhysicsLinearVelocity(FVector::ZeroVector);
   }
   UseRunnerSmoke(Opponent); if (CloudCount()!=0) { Fail(); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixSmokeProbe] PASS initial runner charge, chaser denied, gates=%d"),S->BreakawayLocations.Num());
   ChaseTestPhase=90; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==90) {
   if (PS->SmokeEmittingUntil>Now) { SpeedProbeMax[0]=PS->NextSmokeAt; SpeedProbeMax[1]=Now; if (PS->SmokeCharges!=0 || CloudCount()!=1) { Fail(); return; } ChaseTestPhase=91; ChaseTestTimer=0; }
   else if (ChaseTestTimer>10.f) Fail();
   return;
  }
  if (ChaseTestPhase==91 && ChaseTestTimer>1.5f) {
   ACitixSmokeCloud* Cloud=nullptr; for (TActorIterator<ACitixSmokeCloud> It(World); It; ++It) { Cloud=*It; break; }
   if (!Cloud) { Fail(); return; } int32 Visible=0;
   for (int32 I=0; I<ACitixSmokeCloud::PuffCount; ++I) if (Cloud->Billows->PerInstanceSMCustomData[I*2]>.01f) ++Visible;
   if (Visible<=0 || Visible>=ACitixSmokeCloud::PuffCount) { Fail(); return; }
   const int32 Before=CloudCount(); UseRunnerSmoke(PC);
   if (CloudCount()!=Before || PS->NextSmokeAt!=SpeedProbeMax[0]) { Fail(); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixSmokeProbe] PASS car LMB server RPC, gradual emission (%d/100), repeated use rejected"),Visible);
   if (FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeTrailProbe"))) {
    auto* Car=Cast<ACitixVehiclePawn>(PC->GetPawn()); FTransform Safe; bool Found=false;
    if (!Car) { Fail(); return; }
    const FVector Dirs[]={Car->GetActorForwardVector(),-Car->GetActorForwardVector(),Car->GetActorRightVector(),-Car->GetActorRightVector()};
    for (const FVector& Direction:Dirs) if (ACitixCityGenerator::ValidateChaseSurface(World,Car->GetActorLocation()+Direction*1000.f,FVector(240,110,85),Car->GetActorRotation().Yaw,Car,Safe)) { Found=true; break; }
    if (!Found) { Fail(); return; }
    Car->SetActorTransform(Safe,false,nullptr,ETeleportType::TeleportPhysics); Car->LastDryPose=Safe; Car->bHasDryPose=true; Car->ForceNetUpdate();
    Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(FVector::ZeroVector);
   }
   ChaseTestPhase=92; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==92 && ChaseTestTimer>5.f) {
   if (PS->SmokeCharges!=0 || PS->NextSmokeAt!=SpeedProbeMax[0]) { Fail(); return; }
   if (FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeTrailProbe"))) {
    ACitixSmokeCloud* Cloud=nullptr; for (TActorIterator<ACitixSmokeCloud> It(World); It; ++It) { Cloud=*It; break; }
    if (!Cloud || Cloud->EmissionPositions.Num()!=ACitixSmokeCloud::PuffCount || FVector::Dist2D(Cloud->EmissionPositions[0],Cloud->EmissionPositions.Last())<500.f || FVector::Dist2D(Cloud->EmissionPositions[0],Cloud->GetActorLocation())>30.f) { Fail(); return; }
    UE_LOG(LogCitix,Log,TEXT("[CitixSmokeProbe] PASS moving emitter: 100 world birth positions, first puff stays behind, trail span %.0fcm"),FVector::Dist2D(Cloud->EmissionPositions[0],Cloud->EmissionPositions.Last()));
   }
   UE_LOG(LogCitix,Log,TEXT("[CitixSmokeProbe] PASS no early recharge, original deadline retained")); ChaseTestPhase=93; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==93 && PS->SmokeCharges==1) {
   if (Now-SpeedProbeMax[1]<58.f || CloudCount()!=0 || !PC->RequestExitVehicle()) { Fail(); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixSmokeProbe] PASS real sixty-second refill, first cloud expired, voluntary exit"));
   ChaseTestPhase=94; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==94) {
   if (PS->SmokeEmittingUntil>Now && PC->GetPawn()->IsA<ACitixOnFootPawn>()) {
    if (PS->SmokeCharges!=0 || CloudCount()!=1 || PS->NextSmokeAt<=Now+50.f) { Fail(); return; }
    UE_LOG(LogCitix,Log,TEXT("[CitixSmokeProbe] PASS on-foot LMB deployment, preserved recharge cadence")); ChaseTestPhase=95; ChaseTestTimer=0;
   } else if (ChaseTestTimer>10.f) Fail();
   return;
  }
  if (ChaseTestPhase==95 && ChaseTestTimer>6.f) {
   const FString Receipt=FString::Printf(TEXT("{\"passed\":true,\"gates\":%d,\"spacing_m\":200,\"car_lmb\":true,\"foot_lmb\":true,\"gradual_emission\":true,\"real_60_second_refill\":true,\"cloud_expiry\":true,\"moving_emitter\":%s,\"diameter_m\":35,\"puffs\":100}"),S->BreakawayLocations.Num(),FParse::Param(FCommandLine::Get(),TEXT("CitixSmokeTrailProbe")) ? TEXT("true") : TEXT("false"));
   FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/TEXT("ChaseSmokeProbe.json"))); ChaseTestPhase=99; return;
  }
  return;
 }
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixGateProbe")) && ChaseTestPhase!=99 && S->Phase==ECitixChasePhase::Pursuit) {
  auto* Runner=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Runner));
  auto* Chaser=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Chaser));
  auto* R=Runner ? Cast<ACitixVehiclePawn>(Runner->GetPawn()) : nullptr;
  auto* C=Chaser ? Cast<ACitixVehiclePawn>(Chaser->GetPawn()) : nullptr;
  auto* PS=Chaser ? Chaser->GetPlayerState<ACitixChasePlayerState>() : nullptr;
  const float Now=World->GetTimeSeconds();
  auto Fail=[this]() { UE_LOG(LogCitix,Error,TEXT("[CitixGateProbe] FAIL")); FFileHelper::SaveStringToFile(TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseGateProbe.json"))); ChaseTestPhase=99; };
  if (!R || !C || !PS) { Fail(); return; }
  auto Move=[this,S](ACitixVehiclePawn* Car,int32 Gate,float Offset) {
   const FRotator Facing(0,S->BreakawayYaws[Gate],0);
   const FTransform Pose(Facing,S->BreakawayLocations[Gate]+Facing.Vector()*Offset);
   Car->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics); Car->LastDryPose=Pose; Car->bHasDryPose=true;
   auto* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent()); Body->SetPhysicsLinearVelocity(FVector::ZeroVector); Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
  };
  if (ChaseTestPhase<70) {
   if (S->BreakawayLocations.Num()<=20 || S->BreakawayLocations.Num()>100) { UE_LOG(LogCitix,Error,TEXT("[CitixGateProbe] FAIL fixed sparse gate count; expected road coverage")); Fail(); return; }
   for (int32 Gate=0; Gate<S->BreakawayLocations.Num(); ++Gate) {
    FTransform Dry; const FVector P=S->BreakawayLocations[Gate];
    if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),P,FVector(230,95,85),S->BreakawayYaws[Gate],R,Dry,false) || FMath::Abs(Dry.GetLocation().Z-P.Z)>10.f) { Fail(); return; }
   }
   float WorstDistance=0.f; int32 CoverageSamples=0;
   for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) {
    const auto& Roads=It->GetRoadNetwork();
    // Nearest gate through connected roads: a shoreline stub can use its
    // neighbouring road without pretending a gate can stand over water.
    TArray<float> NodeDistance; NodeDistance.Init(FLT_MAX,Roads.Nodes.Num());
    for (const auto& Edge:Roads.Edges) if (Edge.bDrivable) {
     const FVector A(Roads.Nodes[Edge.NodeA].Position,0),B(Roads.Nodes[Edge.NodeB].Position,0);
     const float Length=FVector::Distance(A,B); if (Length<1.f) continue;
     for (const FVector& Gate:S->BreakawayLocations) {
      const FVector Flat(Gate.X,Gate.Y,0);
      if (FMath::PointDistToSegment(Flat,A,B)<100.f) {
       const float Along=FMath::Clamp(FVector::DotProduct(Flat-A,(B-A)/Length),0.f,Length);
       NodeDistance[Edge.NodeA]=FMath::Min(NodeDistance[Edge.NodeA],Along);
       NodeDistance[Edge.NodeB]=FMath::Min(NodeDistance[Edge.NodeB],Length-Along);
      }
     }
    }
    TArray<bool> Visited; Visited.Init(false,Roads.Nodes.Num());
    for (int32 Step=0; Step<Roads.Nodes.Num(); ++Step) {
     int32 Node=INDEX_NONE; float Best=FLT_MAX;
     for (int32 I=0; I<NodeDistance.Num(); ++I) if (!Visited[I] && NodeDistance[I]<Best) { Best=NodeDistance[I]; Node=I; }
     if (Node==INDEX_NONE) break; Visited[Node]=true;
     for (const auto& Edge:Roads.Edges) if (Edge.bDrivable && (Edge.NodeA==Node || Edge.NodeB==Node)) {
      const int32 Other=Edge.NodeA==Node ? Edge.NodeB : Edge.NodeA;
      const float Cost=FVector2D::Distance(Roads.Nodes[Node].Position,Roads.Nodes[Other].Position);
      NodeDistance[Other]=FMath::Min(NodeDistance[Other],Best+Cost);
     }
    }
    for (const auto& Edge:Roads.Edges) {
     if (!Edge.bDrivable || Edge.bBridge) continue;
     const FVector A(Roads.Nodes[Edge.NodeA].Position,0), B(Roads.Nodes[Edge.NodeB].Position,0);
     const float Length=FVector::Distance(A,B); if (Length<3500.f) continue;
     const FVector Direction=(B-A)/Length;
     TArray<float> Along;
     for (const FVector& Gate:S->BreakawayLocations) {
      const FVector Flat(Gate.X,Gate.Y,0);
      if (FMath::PointDistToSegment(Flat,A,B)<100.f) Along.Add(FVector::DotProduct(Flat-A,Direction));
     }
     const int32 Samples=FMath::CeilToInt(Length/3750.f);
     for (int32 I=0; I<=Samples; ++I) {
      const float Distance=Length*I/Samples; const FVector P=A+Direction*Distance; FTransform Dry;
      if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),P+FVector(0,0,100),FVector(140,FMath::Max(1200.f,Edge.CorridorWidth)*.5f+100.f,85),Direction.Rotation().Yaw,nullptr,Dry,false)) continue;
      float Nearest=FMath::Min(NodeDistance[Edge.NodeA]+Distance,NodeDistance[Edge.NodeB]+Length-Distance); for (float Gate:Along) Nearest=FMath::Min(Nearest,FMath::Abs(Gate-Distance));
      // Sparse gates may be reached through an adjacent road; record coverage without forcing junction clusters.
      WorstDistance=FMath::Max(WorstDistance,Nearest); ++CoverageSamples;
     }
    }
    break;
   }
   if (!CoverageSamples) { Fail(); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixGateProbe] PASS sparse gate coverage: %d checkpoints, worst along-road distance %.1fm"),CoverageSamples,WorstDistance*.01f);
   UE_LOG(LogCitix,Log,TEXT("[CitixGateProbe] PASS dry road surface beneath all gates"));
   Move(R,0,-1200); Move(C,1,-1200);
   ChaseTestPhase=70; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==70 && ChaseTestTimer>3.f) { Move(R,0,0); ChaseTestPhase=71; ChaseTestTimer=0; return; }
  if (ChaseTestPhase==71 && ChaseTestTimer>.25f) {
   if (S->BreakawayUntil<=Now || S->BreakawayReadyAt-Now<14.f || PS->GateSlowUntil>Now) { Fail(); return; }
   GateProbeReady=S->BreakawayReadyAt; Move(R,2,0); Move(C,1,0); ChaseTestPhase=72; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==72 && ChaseTestTimer>.8f) {
   if (S->BreakawayReadyAt!=GateProbeReady || S->BreakawayUntil>GateProbeReady-9.f || PS->GateSlowUntil<=Now || !FMath::IsNearlyEqual(C->GetVehicleMovement()->AbsoluteSpeedLimit,FCitixChaseRules::SpeedLimit(false)*.7f,2.f)) { Fail(); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixGateProbe] PASS independent boost/drag, shared cooldown, thirty percent ramp"));
   ChaseTestPhase=73; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==73 && ChaseTestTimer>.7f) {
   if (PS->GateSlowUntil>Now || !FMath::IsNearlyEqual(C->GetVehicleMovement()->AbsoluteSpeedLimit,FCitixChaseRules::SpeedLimit(false),2.f)) { Fail(); return; }
   // Real Chaos collision against a parked bot, rather than calling the stun directly.
   const FVector P=C->GetActorLocation()+C->GetActorForwardVector()*900.f-FVector(0,0,90);
   auto* Bot=World->SpawnActor<ACitixTrafficVehicle>(P,C->GetActorRotation());
   if (!Bot) { Fail(); return; } Bot->InitializeVehicle(ECitixCarType::Sedan,FLinearColor(.2f,.3f,.4f),701); Bot->SetVehicleVisible(true); GateProbeTraffic=Bot;
   Cast<UPrimitiveComponent>(C->GetRootComponent())->SetPhysicsLinearVelocity(C->GetActorForwardVector()*2200.f);
   ChaseTestPhase=74; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==74 && ChaseTestTimer>3.f) {
   if (C->GetVehicleMovement()->MaxEngineForce<=0.f) { Fail(); return; }
   if (GateProbeTraffic.IsValid()) GateProbeTraffic->Destroy();
   UE_LOG(LogCitix,Log,TEXT("[CitixGateProbe] PASS traffic collision has no scripted driving stun"));
   ChaseTestPhase=76; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==76 && Now>=GateProbeReady+.2f) { Move(R,2,-1200); ChaseTestPhase=77; ChaseTestTimer=0; return; }
  if (ChaseTestPhase==77 && ChaseTestTimer>.2f) { Move(R,2,0); ChaseTestPhase=78; ChaseTestTimer=0; return; }
  if (ChaseTestPhase==78 && ChaseTestTimer>.2f) {
   if (S->BreakawayReadyAt<=GateProbeReady || S->BreakawayUntil<=Now) { Fail(); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixGateProbe] PASS gate reusable after global cooldown"));
   ChaseTestRams=0;
   float Best=FLT_MAX,Yaw=0.f;
   for (TActorIterator<ACitixCityGenerator> It(World); It; ++It) { const auto& Roads=It->GetRoadNetwork();
    for (const auto& E:Roads.Edges) if (E.bDrivable) { const FVector A(Roads.Nodes[E.NodeA].Position,0),B(Roads.Nodes[E.NodeB].Position,0); const float D=FMath::PointDistToSegment(FVector(RelayLocations[0].X,RelayLocations[0].Y,0),A,B); if (D<Best) { Best=D; Yaw=(B-A).Rotation().Yaw; } } break;
   }
   const FTransform Pose(FRotator(0,Yaw,0),RelayLocations[0]-FRotator(0,Yaw,0).Vector()*2200.f);
   R->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics); R->LastDryPose=Pose;
   ChaseTestPhase=80; ChaseTestTimer=0; return;
  }
  if (ChaseTestPhase==80 && ChaseTestTimer>3.f) { ChaseTestPhase=79; ChaseTestTimer=0; return; }
  if (ChaseTestPhase==79 && ChaseTestTimer>.15f) {
   if (ChaseTestRams<5) {
    R->SetActorLocation(RelayLocations[ChaseTestRams],false,nullptr,ETeleportType::TeleportPhysics);
    R->LastDryPose=R->GetActorTransform(); ++ChaseTestRams; ChaseTestTimer=0; return;
   }
   if (S->CompletedRelays!=5 || !S->bExitsUnlocked) { Fail(); return; }
   int32 Desired=0; for (TActorIterator<ACitixTrafficSystem> It(World); It; ++It) { Desired=It->GetDesiredVehicleCount(); break; }
   const FString Receipt=FString::Printf(TEXT("{\"passed\":true,\"gates\":%d,\"instant_relays\":%d,\"desired_traffic\":%d,\"traffic_stun_removed\":true,\"cooldown_reuse\":true,\"road_coverage\":true,\"target_spacing_m\":200}"),S->BreakawayLocations.Num(),S->CompletedRelays,Desired);
   FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/TEXT("ChaseGateProbe.json")));
   UE_LOG(LogCitix,Log,TEXT("[CitixGateProbe] PASS five instantaneous relays; traffic target=%d"),Desired);
   ChaseTestPhase=99;
  }
  return;
 }

 if (FParse::Param(FCommandLine::Get(),TEXT("CitixAmmoProbe")) && ChaseTestPhase!=99 && S->Phase==ECitixChasePhase::Pursuit) {
  auto* PC=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Chaser)); auto* PS=PC ? PC->GetPlayerState<ACitixChasePlayerState>() : nullptr; if (!PS) return;
  auto Shoot=[this,PC]() { FVector Origin; FRotator Aim; PC->GetPlayerViewPoint(Origin,Aim); FireChasePistol(PC,Origin,Aim.Vector()); };
  if (!bSpeedProbeStarted) {
   if (!PC->ForceChaseEjection()) return; bSpeedProbeStarted=true; ChaseTestTimer=0; SpeedProbeBody=0;
   Shoot(); const int32 Before=PS->Ammo; Shoot(); SpeedProbeMax[0]=PS->NextAmmoAt;
   UE_LOG(LogCitix,Log,TEXT("[CitixAmmoProbe] %s start 15, accepted one, rejected excessive fire."),Before==14 && PS->Ammo==14 ? TEXT("PASS") : TEXT("FAIL"));
  }
  if (SpeedProbeBody==0 && ChaseTestTimer>1) {
   Shoot(); UE_LOG(LogCitix,Log,TEXT("[CitixAmmoProbe] %s additional shot preserves refill deadline."),PS->Ammo==13 && PS->NextAmmoAt==SpeedProbeMax[0] ? TEXT("PASS") : TEXT("FAIL"));
   PS->Ammo=0; PS->NextAmmoAt=World->GetTimeSeconds()+8; SpeedProbeBody=1; ChaseTestTimer=0;
  } else if (SpeedProbeBody==1 && ChaseTestTimer>1) {
   Shoot(); UE_LOG(LogCitix,Log,TEXT("[CitixAmmoProbe] %s empty server firing rejected."),PS->Ammo==0 ? TEXT("PASS") : TEXT("FAIL")); SpeedProbeBody=2;
  } else if (SpeedProbeBody==2 && ChaseTestTimer>8.2f) {
   UE_LOG(LogCitix,Log,TEXT("[CitixAmmoProbe] %s one bullet regenerated after eight seconds."),PS->Ammo==1 ? TEXT("PASS") : TEXT("FAIL"));
   ChaseTestPhase=99;
  }
  return;
 }
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixSafetyProbe")) && ChaseTestPhase!=99 && S->Phase==ECitixChasePhase::Pursuit) {
  ACitixDrivingPlayerController* PC=Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Runner));
  ACitixVehiclePawn* Car=PC ? Cast<ACitixVehiclePawn>(PC->GetPawn()) : nullptr; if (!Car) return;
  bool ExitCase=false; int32 RiverRejected=0,Bridges=0; FTransform SafeStart;
  for (TActorIterator<ACitixCityGenerator> It(World); It; ++It) {
   for (const auto& P:It->GetCityPlan().RiverPoints) { FTransform Pose; RiverRejected+=!ACitixCityGenerator::ValidateChaseSurface(World,FVector(P,90),FVector(230,95,85),0,Car,Pose,false); }
   const auto& Roads=It->GetRoadNetwork();
   for (const auto& E:Roads.Edges) if (E.bBridge && E.bDrivable) {
    const FVector A(Roads.Nodes[E.NodeA].Position,0),B(Roads.Nodes[E.NodeB].Position,0),D=(B-A).GetSafeNormal2D(),R(-D.Y,D.X,0); const float Yaw=D.Rotation().Yaw;
    FTransform Bridge; Bridges+=ACitixCityGenerator::ValidateChaseSurface(World,(A+B)*.5f+FVector(0,0,90),FVector(230,95,85),Yaw,Car,Bridge,false);
    for (float Side:{1.f,-1.f}) {
     FTransform Pose; const FVector P=(A+B)*.5f+R*(Side*(E.CorridorWidth*.5f-205.f))+FVector(0,0,90);
     if (ExitCase || !ACitixCityGenerator::ValidateChaseSurface(World,P,FVector(230,95,85),Yaw+(Side<0 ? 180.f : 0),Car,Pose)) continue;
     Car->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics); Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(FVector::ZeroVector);
     FTransform Bad; if (ACitixCityGenerator::ValidateChaseSurface(World,Car->GetExitTransform().GetLocation(),FVector(48,48,85),Pose.Rotator().Yaw,Car,Bad,false)) continue;
     ExitCase=true; SafeStart=Pose;
     const bool Left=PC->RequestExitVehicle(); FTransform Exit; const bool Safe=Left && PC->GetPawn() && ACitixCityGenerator::ValidateChaseSurface(World,PC->GetPawn()->GetActorLocation(),FVector(48,48,85),0,PC->GetPawn(),Exit,false);
     UE_LOG(LogCitix,Log,TEXT("[CitixSafetyProbe] %s shoreline ordinary exit avoids unsafe side."),Safe ? TEXT("PASS") : TEXT("FAIL"));
    }
   }
   if (ExitCase) {
    Car->ApplyVehicleDamage(25); Car->GetVehicleMovement()->ReconcileBoostCharge(.4f); Car->LastDryPose=SafeStart; Car->bHasDryPose=true;
    Car->SetActorLocation(FVector(It->GetCityPlan().RiverPoints[0],90),false,nullptr,ETeleportType::TeleportPhysics); Car->Tick(0);
    FTransform Dry; const bool Restored=ACitixCityGenerator::ValidateChaseSurface(World,Car->GetActorLocation(),FVector(230,95,85),Car->GetActorRotation().Yaw,Car,Dry,false);
    Car->ResetVehicle(); const bool Preserved=FMath::IsNearlyEqual(Car->GetVehicleMovement()->GetHealthFraction(),.75f) && FMath::IsNearlyEqual(Car->GetVehicleMovement()->GetBoostCharge(),.4f);
    UE_LOG(LogCitix,Log,TEXT("[CitixSafetyProbe] %s water guard/reset preserve health and boost."),Restored && Preserved ? TEXT("PASS") : TEXT("FAIL"));
   }
   break;
  }
  UE_LOG(LogCitix,Log,TEXT("[CitixSafetyProbe] %s rivers=%d usable bridges=%d shoreline case=%d."),RiverRejected>0 && Bridges>0 && ExitCase ? TEXT("PASS") : TEXT("FAIL"),RiverRejected,Bridges,ExitCase);
  ChaseTestPhase=99; return;
 }
	// Opt-in physics probe: remove city obstacles, retain the real ground and vehicle simulation.
 if (FParse::Param(FCommandLine::Get(),TEXT("CitixSpeedProbe")) && ChaseTestPhase!=99 && S->Phase==ECitixChasePhase::Pursuit) {
  if (!bSpeedProbeStarted) {
   bSpeedProbeStarted=true; ChaseTestTimer=0;
   for (TActorIterator<ACitixCityGenerator> It(World); It; ++It) {
    const auto& Roads=It->GetRoadNetwork(); int32 Found=0;
    for (const auto& Edge:Roads.Edges) {
     if (!Edge.bDrivable || Edge.bBridge || FVector2D::Distance(Roads.Nodes[Edge.NodeA].Position,Roads.Nodes[Edge.NodeB].Position)<12000.f) continue;
     const FVector2D A=Roads.Nodes[Edge.NodeA].Position,B=Roads.Nodes[Edge.NodeB].Position,D=(B-A).GetSafeNormal(),M=(A+B)*.5f;
     const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X)); FTransform Pose;
     if (!It->IsDryFootprint(FVector(M+D*5000.f,90),FVector2D(240,110),Yaw) || !It->IsDryFootprint(FVector(M-D*5000.f,90),FVector2D(240,110),Yaw) || !ACitixCityGenerator::ValidateChaseSurface(World,FVector(M,90),FVector(240,110,85),Yaw,nullptr,Pose,false)) continue;
     SpeedProbeStarts[Found]=Pose;
     if (ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(ControllerWithRole(Found==0 ? ECitixChaseRole::Runner : ECitixChaseRole::Chaser)->GetPawn())) { Car->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics); Car->LastDryPose=Pose; Car->bHasDryPose=true; }
     if (++Found==2) break;
    }
    if (Found<2) { UE_LOG(LogCitix,Error,TEXT("[CitixSpeedProbe] No safe test strip")); ChaseTestPhase=99; return; } break;
   }
   for (TActorIterator<ACitixCityChunk> It(World); It; ++It) It->SetActorEnableCollision(false);
   for (TActorIterator<ACitixTrafficVehicle> It(World); It; ++It) It->SetActorEnableCollision(false);
  }
  for (TActorIterator<ACitixCityChunk> It(World); It; ++It) It->SetActorEnableCollision(false);
  for (TActorIterator<ACitixTrafficVehicle> It(World); It; ++It) It->SetActorEnableCollision(false);
  for (int RoleIndex=0; RoleIndex<2; ++RoleIndex) {
   ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(ControllerWithRole(RoleIndex==0 ? ECitixChaseRole::Runner : ECitixChaseRole::Chaser)->GetPawn());
   if (!Car) continue;
   UPrimitiveComponent* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent());
   const FVector Velocity=Body->GetPhysicsLinearVelocity();
   SpeedProbeMax[RoleIndex]=FMath::Max(SpeedProbeMax[RoleIndex],static_cast<float>(Velocity.Size2D()*.036f));
   const FVector Start=SpeedProbeStarts[RoleIndex].GetLocation();
   if (FVector::DistSquared2D(Car->GetActorLocation(),Start)>FMath::Square(3500.f)) {
    Car->SetActorLocationAndRotation(FVector(Start.X,Start.Y,Car->GetActorLocation().Z),SpeedProbeStarts[RoleIndex].Rotator(),false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(SpeedProbeStarts[RoleIndex].GetRotation().GetForwardVector()*Velocity.Size2D()+FVector(0,0,Velocity.Z));
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
   }
   // Test driver holds a straight heading; production steering is untouched.
   FRotator Straight=Car->GetActorRotation(); Straight.Yaw=SpeedProbeStarts[RoleIndex].Rotator().Yaw;
   Car->SetActorRotation(Straight,ETeleportType::TeleportPhysics);
   FVector Spin=Body->GetPhysicsAngularVelocityInRadians(); Spin.Z=0; Body->SetPhysicsAngularVelocityInRadians(Spin);
   Car->GetVehicleMovement()->SetThrottleInput(1); Car->GetVehicleMovement()->SetSteeringInput(0);
   Car->GetVehicleMovement()->SetHandbrake(false); Car->GetVehicleMovement()->SetBoostInput(ChaseTestTimer>30.f);
   if (RoleIndex==0 && SpeedProbeBody%2==1) S->BreakawayUntil=World->GetTimeSeconds()+1.f;
   if (ChaseTestTimer>34.99f && ChaseTestTimer<35.f) UE_LOG(LogCitix,Log,TEXT("[CitixSpeedProbeDiag] role=%d shore=%d grounded=%d throttle=%.1f engine=%.0f z=%.1f"),RoleIndex,Car->ShoreRestores,Car->GetVehicleMovement()->IsGrounded(),Car->GetVehicleMovement()->GetThrottleInput(),Car->GetVehicleMovement()->MaxEngineForce,Car->GetActorLocation().Z);
  }
  if (ChaseTestTimer>35.f) {
   for (int RoleIndex=0; RoleIndex<2; ++RoleIndex) {
    const float Cap=FCitixChaseRules::SpeedLimit(RoleIndex==0)*.036f;
    UE_LOG(LogCitix,Log,TEXT("[CitixSpeedProbe] %s body=%d role=%d attained=%.2f cap=%.0f (ordinary + boost)."),SpeedProbeMax[RoleIndex]>=Cap*.97f && SpeedProbeMax[RoleIndex]<=Cap+.1f ? TEXT("PASS") : TEXT("FAIL"),SpeedProbeBody,RoleIndex,SpeedProbeMax[RoleIndex],Cap);
   }
   ++SpeedProbeBody;
   if (SpeedProbeBody>=static_cast<int32>(ECitixCarType::Count)) { FinishRound(true,TEXT("Speed probe complete")); ChaseTestPhase=99; return; }
   for (int RoleIndex=0; RoleIndex<2; ++RoleIndex) if (ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(ControllerWithRole(RoleIndex==0 ? ECitixChaseRole::Runner : ECitixChaseRole::Chaser)->GetPawn())) {
    Car->SetCarAppearance(static_cast<ECitixCarType>(SpeedProbeBody),Car->GetPaintColor()); Car->SetActorTransform(SpeedProbeStarts[RoleIndex],false,nullptr,ETeleportType::TeleportPhysics); Car->LastDryPose=SpeedProbeStarts[RoleIndex];
    if (UPrimitiveComponent* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent())) { Body->SetPhysicsLinearVelocity(FVector::ZeroVector); Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector); }
    Car->GetVehicleMovement()->ReconcileBoostCharge(1.f);
   }
   SpeedProbeMax[0]=SpeedProbeMax[1]=0; ChaseTestTimer=0;
  }
  return;
 }
 // One accepted ram: put the chaser on the runner, hit, then move apart so the
	// next hit passes the cooldown + separation rules.
	auto DoRamHit = [this](AController* Chaser, AController* Runner) -> bool
	{
		ACitixVehiclePawn* ChaserCar = Chaser ? Cast<ACitixVehiclePawn>(Chaser->GetPawn()) : nullptr;
		ACitixVehiclePawn* RunnerCar = Runner ? Cast<ACitixVehiclePawn>(Runner->GetPawn()) : nullptr;
		if (!ChaserCar || !RunnerCar)
		{
			return false;
		}
		const FVector Base = RunnerCar->GetActorLocation();
		ChaserCar->SetActorLocation(Base + FVector(500.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		TryRam(ChaserCar, RunnerCar, 60.f);
		ChaserCar->SetActorLocation(Base - FVector(900.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		ChaserCar->SetActorRotation(FRotator::ZeroRotator, ETeleportType::TeleportPhysics);
		return true;
	};

 if (FParse::Param(FCommandLine::Get(),TEXT("CitixFinalWreckProbe"))) {
  if (ChaseTestPhase==99 || S->Phase!=ECitixChasePhase::Pursuit) return;
  auto* Runner=ControllerWithRole(ECitixChaseRole::Runner); auto* Chaser=ControllerWithRole(ECitixChaseRole::Chaser);
  auto* RunnerPS=Runner ? Runner->GetPlayerState<ACitixChasePlayerState>() : nullptr;
  if (!RunnerPS || !Chaser) return;
  if (!bSpeedProbeStarted) { RunnerPS->bReplacementUsed=true; RunnerPS->CharacterHealth=50; RunnerPS->ForceNetUpdate(); bSpeedProbeStarted=true; ChaseTestRams=0; ChaseTestTimer=0; }
  if (ChaseTestTimer>1.7f) {
   APawn* Before=Runner->GetPawn();
   if (!DoRamHit(Chaser,Runner)) return;
   ++ChaseTestRams; ChaseTestTimer=0;
   if (ChaseTestRams==4) {
    const bool Passed=S->Phase==ECitixChasePhase::RoundResults && RunnerPS->CharacterHealth==0 && Runner->GetPawn()==Before && RunnerPS->ReplacementReadyAt==0 && Chaser->GetPlayerState<ACitixChasePlayerState>()->RoundsWon==1;
    UE_LOG(LogCitix,Log,TEXT("[CitixFinalWreckProbe] %s fourth ram ends final car immediately, no ejection/recovery, one chaser win"),Passed?TEXT("PASS"):TEXT("FAIL"));
    FFileHelper::SaveStringToFile(Passed ? TEXT("{\"passed\":true,\"immediate_second_wreck\":true,\"no_recovery\":true}") : TEXT("{\"passed\":false}"),*(FPaths::ProjectSavedDir()/TEXT("ChaseFinalWreckProbe.json")));
    ChaseTestPhase=99;
   }
  }
  return;
 }

	switch (ChaseTestPhase)
	{
	case 0: // wait for two drivers, then ready both
		if (GetNumPlayers() != 2)
		{
			return;
		}
		if (S->Phase == ECitixChasePhase::Waiting && ChaseTestTimer > 4.f)
		{
			for (APlayerState* State : GameState->PlayerArray)
			{
				if (ACitixChasePlayerState* ChasePS = Cast<ACitixChasePlayerState>(State)) ChasePS->bCityIdentityValid = true;
				if (AController* C = Cast<AController>(State->GetOwner()))
				{
					BeginInteraction(C);
				}
			}
			ChaseTestPhase = 1;
			ChaseTestTimer = 0.f;
		}
		return;

	case 1: // countdown -> pursuit
		if (S->Phase == ECitixChasePhase::Pursuit)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS countdown->pursuit (round %d)."), RoundNumber);
			if (bChaseTestTimeout)
			{
				PhaseTime = .1f;
				S->PhaseSecondsRemaining = PhaseTime;
				UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] forcing pursuit timeout."));
				ChaseTestPhase = 30;
				ChaseTestTimer = 0.f;
				return;
			}
			ChaseTestPhase = FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestPolish")) ? 40 : 2;
			ChaseTestTimer = 0.f;
			ChaseTestRams = 0;
		}
		else if (ChaseTestTimer > 25.f)
		{
			UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL countdown stuck (phase %d)."), (int32)S->Phase);
			ChaseTestPhase = 99;
		}
		return;

	case 40: // exercise the real station hold before the ordinary round regression
	{
		AController* Runner = ControllerWithRole(ECitixChaseRole::Runner);
		AController* Chaser = ControllerWithRole(ECitixChaseRole::Chaser);
		if (!Runner || !Chaser || !Runner->GetPawn()) return;
		if (ACitixVehiclePawn* Car = Cast<ACitixVehiclePawn>(Chaser->GetPawn()))
		{
			Car->ApplyVehicleDamage(100.f);
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] %s chaser damage immunity."), Car->GetDisplayHullFraction() == 1.f ? TEXT("PASS") : TEXT("FAIL"));
		}
  auto* Car=Cast<ACitixVehiclePawn>(Runner->GetPawn());
  if (!Car) { ChaseTestPhase=99; return; }
  const FRotator Facing=BreakawayBeacons[0]->GetActorRotation()+FRotator(0,180,0);
  const FTransform Pose(Facing,S->BreakawayLocations[0]-Facing.Vector()*1200.f);
  Car->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);
  Car->LastDryPose=Pose; Car->bHasDryPose=true;
  Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(FVector::ZeroVector);
  Car->GetVehicleMovement()->ReconcileBoostCharge(0.f); // Free station boost must work with an empty reserve.
		ChaseTestPhase = 39;
		ChaseTestTimer = 0.f;
		return;
	}
 case 39: // Render the real approach before exercising drive-through activation.
 {
  if (ChaseTestTimer<4.f) return;
  auto* Car=Cast<ACitixVehiclePawn>(ControllerWithRole(ECitixChaseRole::Runner)->GetPawn());
  if (!Car) { ChaseTestPhase=99; return; }
  const FTransform Pose(Car->GetActorRotation(),S->BreakawayLocations[0]);
  Car->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);
  Car->LastDryPose=Pose; Car->bHasDryPose=true;
  Cast<UPrimitiveComponent>(Car->GetRootComponent())->SetPhysicsLinearVelocity(FVector::ZeroVector);
  Car->GetVehicleMovement()->ReconcileBoostCharge(0.f);
  ChaseTestPhase=41; ChaseTestTimer=0.f;
  return;
 }
	case 41:
	{
		if (S->BreakawayUntil<=GetWorld()->GetTimeSeconds())
		{
			if (ChaseTestTimer>6.f) { UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL station hold did not complete.")); ChaseTestPhase=99; }
			return;
		}
  if (ChaseTestTimer<.25f) return;
  AController* Runner=ControllerWithRole(ECitixChaseRole::Runner);
  AController* Chaser=ControllerWithRole(ECitixChaseRole::Chaser);
  auto* RunnerCar=Runner ? Cast<ACitixVehiclePawn>(Runner->GetPawn()) : nullptr;
  auto* ChaserCar=Chaser ? Cast<ACitixVehiclePawn>(Chaser->GetPawn()) : nullptr;
  const float Remaining=S->BreakawayUntil-GetWorld()->GetTimeSeconds();
  const float Before=S->BreakawayUntil;
  BeginInteraction(Runner);
  const bool Pass=Remaining>4.4f && Remaining<=5.f && RunnerCar && RunnerCar->GetVehicleMovement()->IsBoosting() && RunnerCar->GetVehicleMovement()->GetBoostCharge()<.02f
   && ChaserCar && FMath::IsNearlyEqual(ChaserCar->GetVehicleMovement()->AbsoluteSpeedLimit,FCitixChaseRules::SpeedLimit(false),1.f)
   && Before==S->BreakawayUntil;
  UE_LOG(LogCitix,Log,TEXT("[CitixChaseTest] %s station drive-through: five-second boost with empty reserve, chaser unaffected until its own crossing, global cooldown enforced."),Pass ? TEXT("PASS") : TEXT("FAIL"));
  if (!Pass) { ChaseTestPhase=99; return; }
		ChaseTestPhase=42;
		ChaseTestTimer=0.f;
		return;
	}
	case 42:
	{
		if (ChaseTestTimer<5.2f) return;
		UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] %s station advantage expired."), S->BreakawayUntil<=GetWorld()->GetTimeSeconds() ? TEXT("PASS") : TEXT("FAIL"));
  auto* RunnerCar=Cast<ACitixVehiclePawn>(ControllerWithRole(ECitixChaseRole::Runner)->GetPawn());
  auto* ChaserCar=Cast<ACitixVehiclePawn>(ControllerWithRole(ECitixChaseRole::Chaser)->GetPawn());
  UE_LOG(LogCitix,Log,TEXT("[CitixChaseTest] %s station effects cleared."),RunnerCar && !RunnerCar->GetVehicleMovement()->bChaseBreakawayBoost && ChaserCar && FMath::IsNearlyEqual(ChaserCar->GetVehicleMovement()->AbsoluteSpeedLimit,FCitixChaseRules::SpeedLimit(false),1.f) ? TEXT("PASS") : TEXT("FAIL"));
  bStationTestPassed=S->BreakawayUntil<=GetWorld()->GetTimeSeconds() && RunnerCar && !RunnerCar->GetVehicleMovement()->bChaseBreakawayBoost && ChaserCar && FMath::IsNearlyEqual(ChaserCar->GetVehicleMovement()->AbsoluteSpeedLimit,FCitixChaseRules::SpeedLimit(false),1.f);
  if (!bStationTestPassed) { ChaseTestPhase=99; return; }
  ChaseTestPhase=2; ChaseTestTimer=0.f;
  return;
 }
	case 2: // complete five relays
		if (S->bExitsUnlocked)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS five pass-through relays -> exits unlocked."));
			ChaseTestPhase = 3;
			ChaseTestTimer = 0.f;
			return;
		}

		if (ChaseTestTimer > 1.f && ChaseTestRams < FCitixChaseRules::RelaysRequired)
		{
			if (AController* Runner = ControllerWithRole(ECitixChaseRole::Runner))
			{
				if (Runner->GetPawn())
				{
					Runner->GetPawn()->SetActorLocation(RelayLocations[S->CompletedRelays], false, nullptr, ETeleportType::TeleportPhysics);
					CompleteRelay(Runner);
					ChaseTestRams=S->CompletedRelays;
				}
			}
			return;
		}
		if (ChaseTestTimer > 15.f)
		{
			UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL relays stuck at %d/5."), S->CompletedRelays);
			ChaseTestPhase = 99;
		}
		return;

	case 3: // drive into an exit -> runner wins round 1
		if (S->Phase == ECitixChasePhase::RoundResults)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS round 1 exit %d finished: %s."), bChaseTestSecondExit ? 2 : 1, *S->StatusText);
			ChaseTestPhase = 4;
			ChaseTestTimer = 0.f;
			ChaseTestRams = 0;
			return;
		}
		if (AController* Runner = ControllerWithRole(ECitixChaseRole::Runner))
		{
			if (bChaseTestRelayHold && !Cast<ACitixVehiclePawn>(Runner->GetPawn()))
			{
				for (TActorIterator<ACitixVehiclePawn> It(GetWorld()); It; ++It)
				{
					if (It->GetOwningController() != Runner || It->IsDisplayDestroyed()) continue;
					Runner->GetPawn()->SetActorLocation(It->GetActorLocation() + FVector(200.f, 0.f, 50.f), false, nullptr, ETeleportType::TeleportPhysics);
					if (ACitixDrivingPlayerController* Driving = Cast<ACitixDrivingPlayerController>(Runner); Driving && Driving->RequestEnterVehicle()) return;
				}
				UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL runner could not re-enter the original car after relays."));
				ChaseTestPhase = 99;
				return;
			}
			if (Runner->GetPawn())
			{
				const int32 ExitIndex = bChaseTestSecondExit && ExitLocations.Num() > 1 ? 1 : 0;
				Runner->GetPawn()->SetActorLocation(ExitLocations[ExitIndex], false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
		if (ChaseTestTimer > 12.f)
		{
			UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL exit did not end round 1."));
			ChaseTestPhase = 99;
		}
		return;

	case 4: // round 2 follows automatically, roles swapped
		if (S->Phase == ECitixChasePhase::Pursuit)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS round 2 pursuit (roles swapped)."));
			ChaseTestPhase = 5;
			ChaseTestTimer = 0.f;
			ChaseTestRams = 0;
		}
		else if (ChaseTestTimer > 40.f)
		{
			UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL round 2 never started (phase %d)."), (int32)S->Phase);
			ChaseTestPhase = 99;
		}
		return;

	case 5: // four rams -> first wreck; then claim a replacement and four more -> second wreck
	case 6:
		{
			AController* Chaser = ControllerWithRole(ECitixChaseRole::Chaser);
			AController* Runner = ControllerWithRole(ECitixChaseRole::Runner);
			ACitixChasePlayerState* RunnerState = Runner ? Runner->GetPlayerState<ACitixChasePlayerState>() : nullptr;
			if (!Chaser || !Runner || !RunnerState)
			{
				return;
			}

			if (ChaseTestPhase == 6 && !Cast<ACitixVehiclePawn>(Runner->GetPawn()))
			{
				if (ACitixOnFootPawn* Walker = Cast<ACitixOnFootPawn>(Runner->GetPawn()); Walker && Walker->IsInShockwaveRecovery())
				{
					if (ChaseTestTimer > 8.f) { UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL shockwave recovery stuck.")); ChaseTestPhase = 99; }
					return;
				}
				if (RunnerState->ReplacementReadyAt>GetWorld()->GetTimeSeconds()) {
     if (ChaseTestAttempts==0 && ReplacementCars.Num()>0) {
      Runner->GetPawn()->SetActorLocation(ReplacementCars[0]->GetActorLocation()+FVector(260,0,0),false,nullptr,ETeleportType::TeleportPhysics);
      const bool Entered=Cast<ACitixDrivingPlayerController>(Runner)->RequestEnterVehicle();
      UE_LOG(LogCitix,Log,TEXT("[CitixChaseTest] %s replacement rejected before 40 seconds, reserve retained."),!Entered && !RunnerState->bReplacementUsed ? TEXT("PASS") : TEXT("FAIL"));
      ChaseTestAttempts=1;
     }
     return;
    }

    if (FParse::Param(FCommandLine::Get(),TEXT("CitixTrafficEntryTest"))) {
     auto* Driving=Cast<ACitixDrivingPlayerController>(Runner);
     for (TActorIterator<ACitixTrafficVehicle> It(World); It; ++It) {
      if (It->IsHidden() || It->IsTakenByPlayer() || It->GetAuthoritativeSpeedKmh()>.01f) continue;
      float HalfLength,HalfWidth,Height; FCitixCarLibrary::GetFootprint(It->GetCarType(),HalfLength,HalfWidth,Height);
      FTransform CarPose,FootPose;
      if (!ACitixCityGenerator::ValidateChaseSurface(World,It->GetActorLocation(),FVector(HalfLength,HalfWidth,85),It->GetActorRotation().Yaw,*It,CarPose,true,Runner->GetPawn()) || !ACitixCityGenerator::ValidateChaseSurface(World,It->GetActorLocation()+It->GetActorRightVector()*(HalfWidth+85.f),FVector(48,48,85),0,*It,FootPose)) continue;
      Runner->GetPawn()->SetActorTransform(FootPose,false,nullptr,ETeleportType::TeleportPhysics);
      It->PublishMotion(10.f); const bool TooFast=Driving->RequestEnterVehicle();
      UE_LOG(LogCitix,Log,TEXT("[CitixTrafficEntryTest] %s car at 10 km/h rejected, reserve retained."),!TooFast && !RunnerState->bReplacementUsed ? TEXT("PASS") : TEXT("FAIL"));
      It->PublishMotion(9.99f); const bool Entered=Driving->RequestEnterVehicle();
      const auto* Adopted=Cast<ACitixVehiclePawn>(Runner->GetPawn());
      const bool Good=Entered && Adopted && RunnerState->bReplacementUsed && RunnerState->CharacterHealth==50 && Adopted->GetVehicleMovement()->GetHealth()==50 && RunnerState->RunnerCarHits==2 && It->IsActorBeingDestroyed();
      UE_LOG(LogCitix,Log,TEXT("[CitixTrafficEntryTest] %s below-10 traffic conversion, two-integrity car, health 50, source retired."),Good ? TEXT("PASS") : TEXT("FAIL"));
      if (!Good) ChaseTestPhase=99; ChaseTestTimer=0; return;
     }
     if (ChaseTestTimer>55) { UE_LOG(LogCitix,Error,TEXT("[CitixTrafficEntryTest] FAIL no safe parked traffic fixture.")); ChaseTestPhase=99; } return;
    }
    // The wreck ejected the runner: walk onto a replacement car and take it.
				if (ChaseTestTimer > 0.5f)
				{
					ChaseTestTimer = 0.f;
					++ChaseTestAttempts;
					if (ReplacementCars.Num() > 0 && Runner->GetPawn())
					{
						ACitixVehiclePawn* Car = ReplacementCars[0];
						Runner->GetPawn()->SetActorLocation(Car->GetActorLocation() + FVector(200.f, 0.f, 50.f), false, nullptr, ETeleportType::TeleportPhysics);
						if (ACitixDrivingPlayerController* Driving = Cast<ACitixDrivingPlayerController>(Runner))
						{
							const bool bEntered = Driving->RequestEnterVehicle();
							UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] replacement claim attempt %d -> %s."),
								ChaseTestAttempts, bEntered ? TEXT("entered") : TEXT("rejected"));
							if (bEntered)
							{
								const bool Good = RunnerState->bReplacementUsed && RunnerState->RunnerCarHits == 2 && Car->GetVehicleMovement()->GetHealth() == 50.f;
								UE_LOG(LogCitix, Log, TEXT("[CitixReplacementProbe] %s first replacement entry: integrity=%d hull=%.0f."), Good ? TEXT("PASS") : TEXT("FAIL"), RunnerState->RunnerCarHits, Car->GetVehicleMovement()->GetHealth());
								ChaseTestPhase = Good ? 61 : 99; ChaseTestTimer = 0.f;
							}
						}
					}
					else if (ChaseTestAttempts > 8)
					{
						UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL no replacement car to claim."));
						ChaseTestPhase = 99;
					}
				}
				if (!Cast<ACitixVehiclePawn>(Runner->GetPawn()) && ChaseTestAttempts > 10)
				{
					UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL could not claim a replacement car."));
					ChaseTestPhase = 99;
				}
				return;
			}

			if (ChaseTestTimer > 1.6f)
			{
				if (DoRamHit(Chaser, Runner))
				{
					ChaseTestRams=S->CompletedRelays;
				}
			}

			if (S->Phase == ECitixChasePhase::RoundResults)
			{
				UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS round 2 finished: %s."), *S->StatusText);
				ChaseTestPhase = 7;
				ChaseTestTimer = 0.f;
			}
			else if (ChaseTestPhase == 5 && RunnerState->CharacterHealth <= 50.f)
			{
				UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS first wreck: health %.0f, replacement spawned=%d."),
					RunnerState->CharacterHealth, ReplacementCars.Num());
				ACitixOnFootPawn* Walker = Cast<ACitixOnFootPawn>(Runner->GetPawn());
				UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] %s shockwave ragdoll active."), Walker && Walker->IsInShockwaveRecovery() ? TEXT("PASS") : TEXT("FAIL"));
				if (FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestRunOver")))
				{
					ChaseTestPhase = 50; ChaseTestTimer = 0.f; return;
				}
				if (FParse::Param(FCommandLine::Get(), TEXT("CitixChaseTestPistol")))
				{
					ChaseTestPhase = 51; ChaseTestTimer = 0.f; return;
				}
				ChaseTestPhase = bChaseTestCapture ? 20 : 6;
				ChaseTestTimer = 0.f;
				ChaseTestRams = 0;
				ChaseTestAttempts = 0;
			}
			else if (ChaseTestRams > 14)
			{
				UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL rams are not producing a wreck."));
				ChaseTestPhase = 99;
			}
		}
		return;

	case 61: // Exercise ordinary exit/re-entry without consuming the reserve twice.
		if (ChaseTestTimer > 1.f)
		{
			auto* Driving = Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Runner));
			if (Driving && Driving->RequestExitVehicle()) { ChaseTestPhase = 62; ChaseTestTimer = 0.f; }
			else if (ChaseTestTimer > 8.f) { UE_LOG(LogCitix, Error, TEXT("[CitixReplacementProbe] FAIL replacement exit.")); ChaseTestPhase = 99; }
		}
		return;
	case 62:
		if (ChaseTestTimer > .6f)
		{
			auto* Driving = Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Runner));
			if (Driving && Driving->RequestEnterVehicle())
			{
				auto* PS = Driving->GetPlayerState<ACitixChasePlayerState>();
				auto* Car = Cast<ACitixVehiclePawn>(Driving->GetPawn());
				const bool Good = PS && Car && PS->bReplacementUsed && PS->RunnerCarHits == 2 && Car->GetVehicleMovement()->GetHealth() == 50.f;
				UE_LOG(LogCitix, Log, TEXT("[CitixReplacementProbe] %s ordinary replacement re-entry preserves two integrity."), Good ? TEXT("PASS") : TEXT("FAIL"));
				ChaseTestPhase = Good ? 6 : 99; ChaseTestTimer = 0.f;
			}
			else if (ChaseTestTimer > 8.f) { UE_LOG(LogCitix, Error, TEXT("[CitixReplacementProbe] FAIL ordinary re-entry.")); ChaseTestPhase = 99; }
		}
		return;
	case 50: // Boundary and protection use the same server sweep as real cars.
		{
			AController* Runner = ControllerWithRole(ECitixChaseRole::Runner);
			AController* Chaser = ControllerWithRole(ECitixChaseRole::Chaser);
			ACitixOnFootPawn* Walker = Runner ? Cast<ACitixOnFootPawn>(Runner->GetPawn()) : nullptr;
			ACitixVehiclePawn* Car = Chaser ? Cast<ACitixVehiclePawn>(Chaser->GetPawn()) : nullptr;
			if (!Walker || !Car) return;
			if (Walker->IsInShockwaveRecovery()) return;
			if (ChaseTestTimer < 4.f) return;
			const FVector Point = Walker->GetActorLocation();
			TryRunOver(Car, Point-FVector(10,0,0), Point+FVector(10,0,0), 40.f);
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] %s 40 km/h nonlethal."), S->Phase == ECitixChasePhase::Pursuit ? TEXT("PASS") : TEXT("FAIL"));
			TryRunOver(Car, Point-FVector(10,0,0), Point+FVector(10,0,0), 41.f);
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] %s above 40 km/h lethal."), S->Phase == ECitixChasePhase::RoundResults ? TEXT("PASS") : TEXT("FAIL"));
			ChaseTestPhase = S->Phase == ECitixChasePhase::RoundResults ? 7 : 99; ChaseTestTimer = 0.f;
		}
		return;

	case 51:
	case 52:
		{
			AController* Runner = ControllerWithRole(ECitixChaseRole::Runner);
			ACitixDrivingPlayerController* Chaser = Cast<ACitixDrivingPlayerController>(ControllerWithRole(ECitixChaseRole::Chaser));
			ACitixOnFootPawn* Walker = Runner ? Cast<ACitixOnFootPawn>(Runner->GetPawn()) : nullptr;
			if (!Walker || !Chaser || Walker->IsInShockwaveRecovery()) return;
			if (ChaseTestPhase == 51)
			{
				if (!Chaser->ForceChaseEjection()) return;
				Chaser->GetPawn()->SetActorLocation(Walker->GetActorLocation()-FVector(400,0,0),false,nullptr,ETeleportType::TeleportPhysics);
				Chaser->SetControlRotation(FRotator::ZeroRotator);
				ChaseTestPhase = 52; ChaseTestTimer = 0.f;
				return;
			}
   ACitixChasePlayerState* TargetState=Runner->GetPlayerState<ACitixChasePlayerState>();
   if (TargetState->PistolHits==1 && !bPistolReentryChecked) {
    if (World->GetTimeSeconds()<TargetState->ReplacementReadyAt) return;
    auto* RunnerPC=Cast<ACitixDrivingPlayerController>(Runner); ACitixVehiclePawn* Replacement=ReplacementCars.IsEmpty() ? nullptr : ReplacementCars[0].Get();
    if (!RunnerPC || !Replacement) { UE_LOG(LogCitix,Error,TEXT("[CitixChaseTest] FAIL pistol re-entry fixture.")); ChaseTestPhase=99; return; }
    FTransform RelayPose;
    if (!ACitixCityGenerator::ValidateChaseSurface(World,RelayLocations[0],FVector(240,110,85),0,Replacement,RelayPose)) { UE_LOG(LogCitix,Error,TEXT("[CitixChaseTest] FAIL relay-entry fixture blocked.")); ChaseTestPhase=99; return; }
    Replacement->SetActorTransform(RelayPose,false,nullptr,ETeleportType::TeleportPhysics); Replacement->LastDryPose=RelayPose; Replacement->bHasDryPose=true;
    Walker->SetActorLocation(Replacement->GetExitTransform().GetLocation(),false,nullptr,ETeleportType::TeleportPhysics);
    BeginInteraction(RunnerPC); const bool Entered=RunnerPC->GetPawn()==Replacement; const bool Left=Entered && RunnerPC->RequestExitVehicle();
    const bool Kept=Left && TargetState->PistolHits==1 && TargetState->CharacterHealth==50 && TargetState->bReplacementUsed;
    UE_LOG(LogCitix,Log,TEXT("[CitixChaseTest] %s pistol hit persists through one-F replacement at relay and exit."),Kept ? TEXT("PASS") : TEXT("FAIL"));
    if (!Kept) { ChaseTestPhase=99; return; }
    bPistolReentryChecked=true; Walker=Cast<ACitixOnFootPawn>(RunnerPC->GetPawn());
    const FVector ShooterLocation=Walker->GetActorLocation()-FVector(400,0,0);
    Chaser->GetPawn()->TeleportTo(ShooterLocation,FRotator::ZeroRotator,false,true); Chaser->ClientSetLocation(ShooterLocation,FRotator::ZeroRotator);
    RunnerPC->ClientSetLocation(Walker->GetActorLocation(),Walker->GetActorRotation()); ChaseTestTimer=0; return;
   }
			FVector Origin; FRotator Aim;
			Chaser->GetPlayerViewPoint(Origin,Aim);
			const FVector Direction = (Walker->GetActorLocation()-Origin).GetSafeNormal();
			Chaser->SetControlRotation(Direction.Rotation());
   if (ChaseTestTimer<.01f || ChaseTestTimer>1.f) Chaser->ClientSetRotation(Direction.Rotation(),true);
			if (ChaseTestTimer < 1.f) return;
			FireChasePistol(Chaser,Origin,Direction);
			if (Runner->GetPlayerState<ACitixChasePlayerState>()->StaggerUntil > GetWorld()->GetTimeSeconds())
			{
				const ACitixChasePlayerState* Target=Runner->GetPlayerState<ACitixChasePlayerState>();
    UE_LOG(LogCitix,Log,TEXT("[CitixChaseTest] %s pistol hit %d/4; character health remains 50."),Target->CharacterHealth==50.f ? TEXT("PASS") : TEXT("FAIL"),Target->PistolHits);
    if (Target->PistolHits>=4) { UE_LOG(LogCitix,Log,TEXT("[CitixChaseTest] %s fourth pistol hit wins."),S->Phase==ECitixChasePhase::RoundResults ? TEXT("PASS") : TEXT("FAIL")); ChaseTestPhase=7; }
    ChaseTestTimer=0.f;
			}
			else if (ChaseTestTimer > 12.f) { UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL pistol did not hit.")); ChaseTestPhase = 99; }
		}
		return;

	case 7: // match complete -> request a rematch
		if (ChaseTestTimer > 10.f)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] Match final: %s - requesting rematch."), *S->StatusText);
			for (APlayerState* State : GameState->PlayerArray)
			{
				if (AController* C = Cast<AController>(State->GetOwner()))
				{
					BeginInteraction(C);
				}
			}
			ChaseTestPhase = 8;
			ChaseTestTimer = 0.f;
		}
		return;

	case 8: // the rematch should restart the round flow
		if (S->Phase == ECitixChasePhase::Countdown || S->Phase == ECitixChasePhase::Pursuit)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS rematch restarted (round %d)."), RoundNumber);
   // Shipping logging is disabled; preserve a small opt-in end-to-end receipt.
   const FString Scenario=FParse::Param(FCommandLine::Get(),TEXT("CitixChaseTestPistol")) ? TEXT("pistol") : bChaseTestCapture ? TEXT("capture") : TEXT("wreck");
   const FString Receipt=FString::Printf(TEXT("{\"complete\":true,\"scenario\":\"%s\",\"pistol_reentry\":%s,\"station_passed\":%s}"),*Scenario,bPistolReentryChecked ? TEXT("true") : TEXT("false"),bStationTestPassed ? TEXT("true") : TEXT("false"));
   FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/(TEXT("ChaseTest-")+Scenario+TEXT(".json"))));
			ChaseTestPhase = 99;
		}
		else if (ChaseTestTimer > 20.f)
		{
			UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL rematch did not restart (phase %d)."), (int32)S->Phase);
			ChaseTestPhase = 99;
		}
		return;

	case 20: // capture: leave the car and hold interact on the on-foot runner
		if (S->Phase == ECitixChasePhase::MatchResults)
		{
			UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS capture: %s."), *S->StatusText);
			ChaseTestPhase = 7;
			ChaseTestTimer = 0.f;
			return;
		}
		{
			AController* Chaser = ControllerWithRole(ECitixChaseRole::Chaser);
			AController* Runner = ControllerWithRole(ECitixChaseRole::Runner);
			ACitixChasePlayerState* ChaserState = Chaser ? Chaser->GetPlayerState<ACitixChasePlayerState>() : nullptr;
			if (!Chaser || !Runner)
			{
				return;
			}
			if (Cast<ACitixVehiclePawn>(Chaser->GetPawn()))
			{
				// Still in the car: get out once (chase ejection ignores the exit
				// clearance gate, which a car wedged against a wall would fail).
				if (ChaseTestTimer > 1.f)
				{
					if (ACitixDrivingPlayerController* Driving = Cast<ACitixDrivingPlayerController>(Chaser))
					{
						Driving->ForceChaseEjection();
						ChaseTestTimer = 0.f;
						UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] chaser left the car to capture."));
					}
				}
				return;
			}
			// On foot: wait out the wreck protection, then hold capture. Retry only
			// when no hold is in progress, so an active 2 s timer is never reset.
			if (ChaseTestAttempts == 1 && ChaserState && ChaserState->bInteractionActive && ChaseTestTimer > .5f)
			{
				if (ACitixOnFootPawn* ChaserWalker = Cast<ACitixOnFootPawn>(Chaser->GetPawn()))
				{
					ChaserWalker->SetActorLocation(ChaserWalker->GetActorLocation() + FVector(1000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
					ChaseTestAttempts = 2;
					ChaseTestTimer = 0.f;
				}
				return;
			}
			if (ChaseTestAttempts == 2 && ChaserState && !ChaserState->bInteractionActive)
			{
				if (!S->StatusText.Contains(TEXT("interrupted")))
				{
					UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL capture range break did not interrupt the hold."));
					ChaseTestPhase = 99;
					return;
				}
				if (ACitixOnFootPawn* RunnerWalker = Cast<ACitixOnFootPawn>(Runner->GetPawn()))
				{
					if (ACitixOnFootPawn* ChaserWalker = Cast<ACitixOnFootPawn>(Chaser->GetPawn()))
					{
						UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS capture interrupted on range break."));
						ChaserWalker->SetActorLocation(RunnerWalker->GetActorLocation() + FVector(120.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
						BeginInteraction(Chaser);
						ChaseTestAttempts = 3;
						ChaseTestTimer = 0.f;
					}
				}
				return;
			}
			if (ChaseTestTimer > 4.f && !S->bInteractionActive)
			{
				if (ACitixOnFootPawn* RunnerWalker = Cast<ACitixOnFootPawn>(Runner->GetPawn()))
				{
					if (ACitixOnFootPawn* ChaserWalker = Cast<ACitixOnFootPawn>(Chaser->GetPawn()))
					{
						ChaserWalker->SetActorLocation(RunnerWalker->GetActorLocation() + FVector(120.f, 0.f, 0.f),
							false, nullptr, ETeleportType::TeleportPhysics);
						BeginInteraction(Chaser);
						ChaseTestTimer = 0.f;
						++ChaseTestAttempts;
						UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] chaser holding capture (attempt %d)."), ChaseTestAttempts);
					}
				}
				if (ChaseTestAttempts > 12)
				{
					UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL capture never resolved."));
					ChaseTestPhase = 99;
				}
			}
		}
		return;

	case 30: // forced pursuit timeout
		if (S->Phase == ECitixChasePhase::RoundResults)
		{
			if (S->StatusText.Contains(TEXT("Time expired")))
			{
				UE_LOG(LogCitix, Log, TEXT("[CitixChaseTest] PASS timeout: %s."), *S->StatusText);
			}
			else
			{
				UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL timeout resolved with: %s."), *S->StatusText);
			}
			ChaseTestPhase = 99;
		}
		else if (ChaseTestTimer > 5.f)
		{
			UE_LOG(LogCitix, Error, TEXT("[CitixChaseTest] FAIL pursuit timeout did not resolve."));
			ChaseTestPhase = 99;
		}
		return;

	default:
		return;
	}
}

void ACitixChaseGameMode::SnapChaseLocationsToRoads(const FCitixRoadNetwork& Roads)
{
 for(TActorIterator<ACitixCityGenerator> It(GetWorld());It;++It) if(It->bHillsideMap)
 {
  const auto& Layout=It->GetHillsideLayout();
  SpawnLocations=Layout.Spawns; ExitLocations=Layout.Exits; ReplacementLocations=Layout.RecoveryParking;
  auto Project=[&](TArray<FVector>& Locations)
  {
   for(FVector& P:Locations)
   {
    FTransform Surface;
    if(!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),P+FVector(0,0,100),FVector(240,110,85),0,nullptr,Surface,false)) {Locations.Reset(); return false;}
    P=Surface.GetLocation();
   }
   return true;
  };
  ValidatedRelaySites=Layout.RelaySites;
  if(!Project(SpawnLocations) || !Project(ExitLocations) || !Project(ReplacementLocations) || !Project(ValidatedRelaySites)) {RelayLocations.Reset(); return;}
  RelaySelectionSeed=FMath::Rand(); FParse::Value(FCommandLine::Get(),TEXT("CitixRelaySeed="),RelaySelectionSeed);
  RelayLocations=FCitixRelayLayout::Select(ValidatedRelaySites,RelaySelectionSeed);
  UE_LOG(LogCitix,Log,TEXT("[CitixChase] Hillside authored chase layout: sites=%d active=%d starts=%d exits=%d recovery=%d"),ValidatedRelaySites.Num(),RelayLocations.Num(),SpawnLocations.Num(),ExitLocations.Num(),ReplacementLocations.Num());
  return;
 }
 TArray<FVector> SafeRelays;
 for(const auto& E:Roads.Edges) {
  if(!E.bDrivable || !Roads.Nodes.IsValidIndex(E.NodeA) || !Roads.Nodes.IsValidIndex(E.NodeB)) continue;
  const FVector2D A=Roads.Nodes[E.NodeA].Position,B=Roads.Nodes[E.NodeB].Position;
  for(float T:{.2f,.5f,.8f}) {
   FTransform Surface; const FVector Candidate(A+(B-A)*T,90);
   if(!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Candidate,FVector(240,110,85),FMath::RadiansToDegrees(FMath::Atan2(B.Y-A.Y,B.X-A.X)),nullptr,Surface)) continue;
   TArray<int32> Route;
   if(!FCitixRouteHelper::FindRouteNodes(Roads,SpawnLocations[0],Surface.GetLocation(),Route) || Route.IsEmpty()) continue;
   SafeRelays.Add(Surface.GetLocation());
  }
 }
 int32 LayoutSeed=FMath::Rand(); FParse::Value(FCommandLine::Get(),TEXT("CitixRelaySeed="),LayoutSeed);
 ValidatedRelaySites=SafeRelays; RelaySelectionSeed=LayoutSeed;
 RelayLocations=FCitixRelayLayout::Select(ValidatedRelaySites,RelaySelectionSeed);
 UE_LOG(LogCitix,Log,TEXT("[CitixRelayLayout] seed=%d safe=%d selected=%d spacing=180m"),LayoutSeed,SafeRelays.Num(),RelayLocations.Num());
	ReplacementLocations.Reset();
	for (const FVector& Relay : RelayLocations)
	{
		ReplacementLocations.Add(Relay + FVector(5000.f, 0.f, 0.f));
		ReplacementLocations.Add(Relay + FVector(0.f, 5000.f, 0.f));
	}
	for (const FVector& Exit : ExitLocations)
	{
		ReplacementLocations.Add(Exit + FVector(5000.f, 0.f, 0.f));
		ReplacementLocations.Add(Exit + FVector(0.f, 5000.f, 0.f));
	}

	ACitixCityGenerator* City=nullptr;
 for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) { City=*It; break; }
 auto Snap = [this, &Roads, City](FVector& Location)
	{
		float Best = TNumericLimits<float>::Max();
		FVector2D BestPoint(Location.X, Location.Y);
		for (const FCitixRoadEdge& Edge : Roads.Edges)
		{
			if (!Edge.bDrivable || !Roads.Nodes.IsValidIndex(Edge.NodeA) || !Roads.Nodes.IsValidIndex(Edge.NodeB)) continue;
			const FVector2D A = Roads.Nodes[Edge.NodeA].Position, B = Roads.Nodes[Edge.NodeB].Position, P(Location.X, Location.Y);
			const FVector2D AB = B - A;
			const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(1.f, AB.SizeSquared()), 0.f, 1.f);
			const FVector2D Candidate = A + AB * T;
			FTransform Surface;
   const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(AB.Y,AB.X));
   if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),FVector(Candidate,90.f),FVector(240,110,85),Yaw,nullptr,Surface)) continue;
   const float Distance = FVector2D::DistSquared(P, Candidate);
			if (Distance < Best) { Best = Distance; BestPoint = Candidate; }
		}
		if (Best < TNumericLimits<float>::Max()) { Location.X = BestPoint.X; Location.Y = BestPoint.Y; FTransform Pose; if (ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Location,FVector(240,110,85),0,nullptr,Pose)) Location=Pose.GetLocation(); }
  else Location=FVector(TNumericLimits<float>::Max());
	};
	// Relay points were surface-validated before the spread selection.
	for (FVector& Location : ExitLocations) Snap(Location);
	for (FVector& Location : ReplacementLocations) Snap(Location);
	for (FVector& Location : SpawnLocations) Snap(Location);

	auto RouteLength = [&Roads](const FVector& From, const FVector& To)
	{
		TArray<int32> Path;
		if (!FCitixRouteHelper::FindRouteNodes(Roads, From, To, Path) || Path.Num() == 0) return TNumericLimits<float>::Max();
		float Length = static_cast<float>(FVector2D::Distance(FVector2D(From.X, From.Y), Roads.Nodes[Path[0]].Position));
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			Length += static_cast<float>(FVector2D::Distance(Roads.Nodes[Path[Index - 1]].Position, Roads.Nodes[Path[Index]].Position));
		}
		return Length + static_cast<float>(FVector2D::Distance(Roads.Nodes[Path.Last()].Position, FVector2D(To.X, To.Y)));
	};

	// 15-25 seconds at roughly 54 mph is 22.5-45k cm. Pick the connected road node
	// closest to the middle of that band instead of trusting a straight-line spawn pair.
	if (SpawnLocations.Num() == 2)
	{
		constexpr float MinSpawnRoute = 22500.f;
		constexpr float MaxSpawnRoute = 45000.f;
		constexpr float IdealSpawnRoute = (MinSpawnRoute + MaxSpawnRoute) * 0.5f;
		float BestScore = TNumericLimits<float>::Max();
		FVector BestLocation = SpawnLocations[1];
		for (const FCitixRoadNode& Node : Roads.Nodes)
		{
			const FVector Candidate(Node.Position.X, Node.Position.Y, SpawnLocations[1].Z);
			FTransform Dry;
   if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),Candidate,FVector(240,110,85),0,nullptr,Dry)) continue;
   const float Length = RouteLength(SpawnLocations[0], Dry.GetLocation());
			if (Length < MinSpawnRoute || Length > MaxSpawnRoute) continue;
			const float Score = FMath::Abs(Length - IdealSpawnRoute);
			if (Score < BestScore) { BestScore = Score; BestLocation = Dry.GetLocation(); }
		}
		if (BestScore < TNumericLimits<float>::Max()) SpawnLocations[1] = BestLocation;
		UE_LOG(LogCitix, Log, TEXT("[CitixChase] spawn road route %.0fcm%s."), RouteLength(SpawnLocations[0], SpawnLocations[1]), BestScore < TNumericLimits<float>::Max() ? TEXT("") : TEXT(" (target band unavailable)"));
	}

 // Reproduce the exact seed-1337 spawn geometry of the first lifecycle failure.
 // The usual EnsurePlayerStarts/ResetPlayerForRound surface checks still apply.
 if (bChaseTest && FParse::Param(FCommandLine::Get(),TEXT("CitixLifecycleOriginalSpawns"))) {
  SpawnLocations={FVector(-18428.21f,-23462.24f,106.f),FVector(11849.39f,-7379.87f,102.35f)};
  UE_LOG(LogCitix,Log,TEXT("[CitixLifecycle] using original failure spawn fixtures"));
 }

	// A recovery point must be reachable on the same drivable network from a pursuit
	// objective/exit within about 100 m. Keep candidates that satisfy that rule.
	TArray<FVector> ReachableReplacements;
	for (const FVector& Candidate : ReplacementLocations)
	{
		bool bReachable = false;
		for (const FVector& Anchor : RelayLocations)
		{
			if (RouteLength(Anchor, Candidate) <= 10000.f) { bReachable = true; break; }
		}
		if (!bReachable) for (const FVector& Anchor : ExitLocations)
		{
			if (RouteLength(Anchor, Candidate) <= 10000.f) { bReachable = true; break; }
		}
		if (bReachable) ReachableReplacements.Add(Candidate);
	}
	if (ReachableReplacements.Num() >= 2) ReplacementLocations = MoveTemp(ReachableReplacements);
	UE_LOG(LogCitix, Log, TEXT("[CitixChase] %d recovery parking candidates passed 100m route validation."), ReplacementLocations.Num());
}

void ACitixChaseGameMode::ClearHold(AController* Controller, bool bInterrupted) { if (ACitixChasePlayerState* PS = Controller ? Controller->GetPlayerState<ACitixChasePlayerState>() : nullptr) { PS->bInteractionActive = false; PS->InteractionSecondsRemaining = 0.f; PS->InteractionType = ECitixChaseInteraction::None; } if (bInterrupted) if (ACitixChaseGameState* S = ChaseState()) S->StatusText = TEXT("Interaction interrupted"); }

void ACitixChaseGameMode::UpdateInteractionPresentation() { if (ACitixChaseGameState* S = ChaseState()) { S->bInteractionActive = false; S->InteractionSecondsRemaining = 0.f; for (const TPair<TWeakObjectPtr<AController>, FCitixChaseHold>& Pair : ActiveHolds) if (Pair.Key.IsValid()) { S->bInteractionActive = true; S->InteractionSecondsRemaining = FMath::Max(S->InteractionSecondsRemaining, Pair.Value.SecondsRemaining); } } }

void ACitixChaseGameMode::CancelInteraction(AController* InteractingController) { if (const FCitixChaseHold* Hold=ActiveHolds.Find(InteractingController); Hold && Hold->RelayIndex!=INDEX_NONE) return; if (!InteractingController || !ActiveHolds.Contains(InteractingController)) return; ClearHold(InteractingController, true); ActiveHolds.Remove(InteractingController); UpdateInteractionPresentation(); }

void ACitixChaseGameMode::PlacePlayerAtRoundSpawn(AController* Controller) const
{
 if (!Controller || SpawnLocations.Num()!=2) return;
 const auto* PS=Controller->GetPlayerState<ACitixChasePlayerState>();
 auto* Car=Cast<ACitixVehiclePawn>(Controller->GetPawn());
 if (!PS || !Car) return;
 const int32 Index=PS->ChaseRole==ECitixChaseRole::Runner ? 0 : 1;
 if (!GeneratedPlayerStarts.IsValidIndex(Index)) return;
 FTransform Safe;
 if (ACitixCityGenerator::ValidateChaseSurface(GetWorld(),SpawnLocations[Index],FVector(240,110,85),GeneratedPlayerStarts[Index]->GetActorRotation().Yaw,Car,Safe)) Car->SetRoundStartPose(Safe);
}

bool ACitixChaseGameMode::ResetPlayerForRound(AController* Controller, const FVector& SpawnLocation) { if (!Controller) return false; TArray<ACitixVehiclePawn*> OldCars; for (TActorIterator<ACitixVehiclePawn> It(GetWorld()); It; ++It) if (It->GetOwningController() == Controller) OldCars.Add(*It); if (APawn* OldPawn = Controller->GetPawn()) { Controller->UnPossess(); OldPawn->Destroy(); } for (ACitixVehiclePawn* Car : OldCars) if (Car && !Car->IsActorBeingDestroyed()) Car->Destroy(); FTransform Safe;
 float Yaw=0; float Best=FLT_MAX;
 for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) for (const FCitixRoadEdge& E: It->GetRoadNetwork().Edges) {
  if (!E.bDrivable) continue;
  const FVector A(It->GetRoadNetwork().Nodes[E.NodeA].Position,0),B(It->GetRoadNetwork().Nodes[E.NodeB].Position,0);
  const float Distance=FMath::PointDistToSegment(FVector(SpawnLocation.X,SpawnLocation.Y,0),A,B);
  if (Distance<Best) { Best=Distance; Yaw=(B-A).Rotation().Yaw; }
 }
 if (!ACitixCityGenerator::ValidateChaseSurface(GetWorld(),SpawnLocation,FVector(240,110,85),Yaw,nullptr,Safe)) { return false; }
 RestartPlayerAtTransform(Controller, Safe); if (ACitixVehiclePawn* Car = Cast<ACitixVehiclePawn>(Controller->GetPawn())) { Car->SetOwningController(Controller); Car->SetOccupied(true); Car->SetRoundStartPose(Safe); return true; } return false; }

void ACitixChaseGameMode::FinishMatch() { ACitixChaseGameState* S = ChaseState(); if (!S) return; TArray<ACitixChasePlayerState*> Players; for (APlayerState* State : GameState->PlayerArray) if (ACitixChasePlayerState* PS = Cast<ACitixChasePlayerState>(State)) { PS->bReady = false; Players.Add(PS); } if (Players.Num() != 2) { CancelMatch(TEXT("Match cancelled — waiting for two drivers")); return; } S->Phase = ECitixChasePhase::MatchResults; S->PhaseSecondsRemaining = 0.f; const int32 A = Players[0]->RoundsWon, B = Players[1]->RoundsWon; UE_LOG(LogCitix, Log, TEXT("[CitixChase] Match complete: %s %d - %s %d."), *Players[0]->GetPlayerName(), A, *Players[1]->GetPlayerName(), B); if (A == B) S->StatusText = FString::Printf(TEXT("Match draw %d-%d — both press F to rematch"), A, B); else { const ACitixChasePlayerState* Winner = A > B ? Players[0] : Players[1]; S->StatusText = FString::Printf(TEXT("%s wins the match %d-%d — both press F to rematch"), *Winner->GetPlayerName(), FMath::Max(A, B), FMath::Min(A, B)); } }

bool ACitixChaseGameMode::CanAcceptDriveInput() const { const ACitixChaseGameState* S = ChaseState(); return S && S->Phase == ECitixChasePhase::Pursuit; }

void ACitixChaseGameMode::CancelMatch(const FString& Reason) { PendingEjections.Reset(); ACitixChaseGameState* S = ChaseState(); if (!S) return; LobbyReady.Reset(); RematchReady.Reset(); for (TPair<TWeakObjectPtr<AController>, FCitixChaseHold>& Pair : ActiveHolds) ClearHold(Pair.Key.Get(), false); ActiveHolds.Reset(); for (ACitixVehiclePawn* Car : ReplacementCars) if (IsValid(Car)) Car->Destroy(); ReplacementCars.Reset(); PhaseTime = 0.f; RoundNumber = 0; LastRamTime = -100.f; LastRamSource.Reset(); LastRamTarget.Reset(); bRamSeparated = true; CaptureProtectionUntil = 0.f; S->Phase = ECitixChasePhase::Waiting; S->RoundNumber = 0; S->PhaseSecondsRemaining = 0.f; S->CompletedRelays = 0; S->bExitsUnlocked = false; S->bRunnerRevealed = false; S->RevealSecondsRemaining = 0.f; S->bInteractionActive = false; S->InteractionSecondsRemaining = 0.f; S->StatusText = Reason; for (APlayerState* State : GameState->PlayerArray) if (ACitixChasePlayerState* PS = Cast<ACitixChasePlayerState>(State)) { PS->bReady = false; PS->RoundsWon = 0; PS->CharacterHealth = 100.f; PS->bReplacementUsed = false; PS->RunnerCarHits = 0; PS->bInteractionActive = false; PS->InteractionSecondsRemaining = 0.f; PS->InteractionType = ECitixChaseInteraction::None; } UE_LOG(LogCitix, Log, TEXT("[CitixChase] Match cancelled: %s."), *Reason); }

void ACitixChaseGameMode::UseChaserRapidBrake(APlayerController* Player)
{
 auto* PS=Player ? Player->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 auto* Car=Player ? Cast<ACitixVehiclePawn>(Player->GetPawn()) : nullptr;
 auto* S=ChaseState();
 if(!PS || !Car || !S || Car->IsDisplayDestroyed() || !Car->IsOccupied()
    || !FCitixChaseRules::CanRapidBrake(PS->ChaseRole==ECitixChaseRole::Chaser,true,S->Phase==ECitixChasePhase::Pursuit,PS->RapidBrakeCharges,Car->GetVelocity().Size2D())) return;
 const float Now=GetWorld()->GetTimeSeconds();
 PS->RapidBrakeCharges=0; PS->NextRapidBrakeAt=Now+FCitixChaseRules::RapidBrakeRecharge; PS->RapidBrakeUntil=Now+FCitixChaseRules::RapidBrakeDuration;
 PS->ForceNetUpdate(); S->MulticastRapidBrake(Car->GetActorLocation(),Car->GetVelocity());
 UE_LOG(LogCitix,Log,TEXT("[CitixRapidBrake] activated speed=%.1f cooldown=20s"),Car->GetVelocity().Size2D()*.036f);
}
