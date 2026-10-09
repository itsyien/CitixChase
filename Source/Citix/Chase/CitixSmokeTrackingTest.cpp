#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixSmokeCloud.h"
#include "Engine/World.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixSmokeTrackingTest,"CitixChase.Smoke.TrackingConcealment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixSmokeTrackingTest::RunTest(const FString&) {
 TestFalse(TEXT("Smoke hides tracking just below 150m"),FCitixChaseRules::RunnerTrackingVisible(true,14999.f));
 TestTrue(TEXT("Exactly 150m restores tracking"),FCitixChaseRules::RunnerTrackingVisible(true,15000.f));
 TestTrue(TEXT("Distant runner remains tracked in smoke"),FCitixChaseRules::RunnerTrackingVisible(true,30000.f));
 TestTrue(TEXT("No smoke preserves continuous tracking nearby"),FCitixChaseRules::RunnerTrackingVisible(false,1.f));
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Smoke=World->SpawnActor<ACitixSmokeCloud>();
 Smoke->StartedAt=0;for(int32 I=0;I<ACitixSmokeCloud::PuffCount;++I) Smoke->EmissionPositions.Add(FVector(1000,2000,70));
 TestFalse(TEXT("Unborn puffs do not conceal"),Smoke->ContainsPoint(FVector(1000,2000,70),-1));
 TestTrue(TEXT("A live cloud conceals the runner"),Smoke->ContainsPoint(FVector(1000,2000,70),2));
 TestFalse(TEXT("Outside smoke is not concealed"),Smoke->ContainsPoint(FVector(10000,2000,70),2));
 TestFalse(TEXT("Smoke on another elevation does not conceal"),Smoke->ContainsPoint(FVector(1000,2000,2000),2));
 TestTrue(TEXT("Old emitted smoke still conceals after emission ends"),Smoke->ContainsPoint(FVector(1000,2000,70),7));
 TestFalse(TEXT("Expired puffs cannot conceal"),Smoke->ContainsPoint(FVector(1000,2000,70),16));
 Smoke->SetActorLocation(FVector(10000,0,0));
 TestTrue(TEXT("Coverage uses stationary world birth positions"),Smoke->ContainsPoint(FVector(1000,2000,70),2));
 World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixSmokeSightlineTest,"CitixChase.Smoke.SightlineConcealment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixSmokeSightlineTest::RunTest(const FString&) {
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* State=World->SpawnActor<ACitixChaseGameState>(); World->SetGameState(State);
 auto* Mode=World->SpawnActor<ACitixChaseGameMode>(); Mode->GameState=State;
 auto MakeDriver=[&](ECitixChaseRole Role,const FVector& Location) {
  auto* PC=World->SpawnActor<APlayerController>();
  auto* PS=World->SpawnActor<ACitixChasePlayerState>(); PS->SetOwner(PC); PC->PlayerState=PS; PS->ChaseRole=Role; State->AddPlayerState(PS);
  auto* Pawn=World->SpawnActor<APawn>(); auto* Root=NewObject<USceneComponent>(Pawn); Pawn->SetRootComponent(Root); Root->RegisterComponent(); Pawn->SetActorLocation(Location); PC->Possess(Pawn);
  return Pawn;
 };
 auto* Runner=MakeDriver(ECitixChaseRole::Runner,FVector(5000,0,70));
 auto* Chaser=MakeDriver(ECitixChaseRole::Chaser,FVector(-5000,0,70));
 State->Phase=ECitixChasePhase::Countdown; Mode->Tick(0.f); // Enter a real timed pursuit without generating a city.
 auto* Smoke=World->SpawnActor<ACitixSmokeCloud>(); Smoke->StartedAt=-3.f; Smoke->CloudSeed=1;
 for(int32 I=0;I<ACitixSmokeCloud::PuffCount;++I) Smoke->EmissionPositions.Add(FVector(0,0,70));
 TestFalse(TEXT("Runner is beyond the cloud, not standing inside it"),Smoke->ContainsPoint(Runner->GetActorLocation(),0.f));
 Mode->Tick(0.f);
 TestFalse(TEXT("A smoke trail between cars hides tracking below 150m"),State->bRunnerRevealed);
 Chaser->SetActorLocation(FVector(-10000,0,70)); Mode->Tick(0.f);
 TestTrue(TEXT("Exactly 150m restores ESP even across a smoke trail"),State->bRunnerRevealed);
 Chaser->SetActorLocation(FVector(-5000,6000,70)); Mode->Tick(0.f);
 TestTrue(TEXT("Smoke beside the sightline does not hide tracking"),State->bRunnerRevealed);
 Chaser->SetActorLocation(FVector(-5000,0,70));
 for(auto& Position:Smoke->EmissionPositions) Position=FVector(9000,0,70);
 Mode->Tick(0.f); TestTrue(TEXT("Smoke beyond the runner does not block the finite sightline"),State->bRunnerRevealed);
 for(auto& Position:Smoke->EmissionPositions) Position=FVector(0,0,70);
 Smoke->StartedAt=-16.f; Mode->Tick(0.f);
 TestTrue(TEXT("An expired trail restores tracking"),State->bRunnerRevealed);
 Smoke->StartedAt=-3.f;
 for(auto& Position:Smoke->EmissionPositions) Position=FVector(0,0,2070);
 Mode->Tick(0.f); TestTrue(TEXT("Smoke on another elevation cannot block the sightline"),State->bRunnerRevealed);
 World->DestroyWorld(false);return true;
}
#endif
