#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/CitixTrackerMath.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Engine/World.h"
#include "Player/CitixGroundTracker.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "City/CitixCityChunk.h"
#include "Sandbox/CitixHitSpark.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "EngineUtils.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixTrackerTest,"CitixChase.Tracker.CameraBearingAndReveal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixTrackerTest::RunTest(const FString&) {
 TestEqual(TEXT("Ahead is up"),FCitixTrackerMath::BearingDegrees(FVector(100,0,0),0),0.f);
 TestEqual(TEXT("Right follows the camera"),FCitixTrackerMath::BearingDegrees(FVector(0,100,0),0),90.f);
 TestEqual(TEXT("Camera rotation changes bearing"),FCitixTrackerMath::BearingDegrees(FVector(100,0,0),90),-90.f);
 TestTrue(TEXT("Behind remains behind"),FMath::Abs(FCitixTrackerMath::BearingDegrees(FVector(-100,0,0),0))>179.f);
 TestTrue(TEXT("Wrap follows the short arc"),FMath::Abs(FCitixTrackerMath::SmoothBearing(179,-179,1.f/60))>179.f);
 for(int32 Hz:{30,60,120}) {float Angle=0; for(int32 I=0;I<Hz/10;++I) Angle=FCitixTrackerMath::SmoothBearing(Angle,90,1.f/Hz); TestTrue(TEXT("Tracks a camera turn within a tenth of a second"),Angle>81 && Angle<90);}
 TestEqual(TEXT("Above cue"),FCitixTrackerMath::Elevation(FVector(0,0,800)),1);
 TestEqual(TEXT("Below cue"),FCitixTrackerMath::Elevation(FVector(0,0,-800)),-1);
 TestEqual(TEXT("Suspension motion does not flicker elevation"),FCitixTrackerMath::Elevation(FVector(0,0,100)),0);
 TestFalse(TEXT("Unrevealed runner remains hidden"),FCitixTrackerMath::CanTrack(true,false,true,true));
 TestFalse(TEXT("Runner cannot track themselves"),FCitixTrackerMath::CanTrack(false,true,true,true));
 TestFalse(TEXT("Missing target hides tracker"),FCitixTrackerMath::CanTrack(true,true,false,true));
 TestFalse(TEXT("Lobby does not show tracker"),FCitixTrackerMath::CanTrack(true,true,true,false));
 TestTrue(TEXT("Revealed live target remains trackable"),FCitixTrackerMath::CanTrack(true,true,true,true));
 auto* World=UWorld::CreateWorld(EWorldType::Game,false); auto* Car=World->SpawnActor<ACitixVehiclePawn>(); TInlineComponentArray<UActorComponent*> Components(Car);
 int32 OldMeshes=0; for(auto* Component:Components) if(Component->GetName().StartsWith(TEXT("ChaseTracker"))) ++OldMeshes;
 TestEqual(TEXT("Ground tracker is local-only and leaves no components on replicated cars"),OldMeshes,0); World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixGroundTrackerTest,"CitixChase.Tracker.GroundProjectionAndRelay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixGroundTrackerTest::RunTest(const FString&) {
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* State=World->SpawnActor<ACitixChaseGameState>(); World->SetGameState(State); State->Phase=ECitixChasePhase::Pursuit; State->bRunnerRevealed=true;
 auto* PC=World->SpawnActor<APlayerController>(); auto* PS=World->SpawnActor<ACitixChasePlayerState>(); PC->SetAsLocalPlayerController(); PC->PlayerState=PS; PS->ChaseRole=ECitixChaseRole::Chaser;
 auto* Car=World->SpawnActor<ACitixVehiclePawn>(FVector(0,0,70),FRotator::ZeroRotator); PC->Possess(Car);
 auto* Target=World->SpawnActor<ACitixVehiclePawn>(FVector(1000,0,500),FRotator::ZeroRotator);
 auto* Chunk=World->SpawnActor<ACitixCityChunk>(); Chunk->Init(FIntPoint(0,0),10000); Chunk->QueueBox(ECitixSurface::Asphalt,FVector(0,0,-10),FVector(2000,2000,20)); Chunk->Finish();
 auto* Scene=World->GetPhysicsScene(); const FVector Gravity(0,0,-980); Scene->SetUpForFrame(&Gravity,1.f/60,0,.1f,1.f/120,16,true); Scene->StartFrame(); Scene->WaitPhysScenes(); Scene->EndFrame();
 FActorSpawnParameters Params; Params.Owner=PC; auto* Tracker=World->SpawnActor<ACitixGroundTracker>(Params); Tracker->DispatchBeginPlay(); Tracker->Track(Target); Tracker->Tick(1.f/60);
 TestFalse(TEXT("Projection is visible over production road collision"),Tracker->IsHidden());
 TestTrue(TEXT("Projection sits on road instead of vehicle height"),FMath::Abs(Tracker->GetActorLocation().Z-2.f)<1.f);
 TestTrue(TEXT("Ring exceeds car footprint without giant floating geometry"),ACitixGroundTracker::Radius>=270.f && ACitixGroundTracker::Radius<=330.f);
 TInlineComponentArray<UInstancedStaticMeshComponent*> Meshes(Tracker); TestEqual(TEXT("Only two instanced batches"),Meshes.Num(),2);
 State->bRunnerRevealed=false; Tracker->Tick(1.f/60); TestTrue(TEXT("Reveal loss immediately hides ground ring"),Tracker->IsHidden());
 ACitixHitSpark::SpawnRelaySprinkles(World,FVector(0,0,70),FVector(500,0,0)); int32 Count=0;
 for(TActorIterator<ACitixHitSpark> It(World);It;++It) {++Count; TestTrue(TEXT("Relay sprinkles have bounded lifetime"),It->GetLifeSpan()<=1.1f); It->Tick(2.f); TestTrue(TEXT("Sprinkles finish animation"),It->IsIdle());}
 TestEqual(TEXT("Relay pickup emits one small bounded burst"),Count,24);
 World->DestroyWorld(false); return true;
}
#endif
