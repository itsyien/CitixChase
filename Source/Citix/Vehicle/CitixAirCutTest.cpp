#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Vehicle/CitixAirFlowComponent.h"
#include "Chase/CitixSmokeCloud.h"
#include "Sandbox/CitixBulletTracer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixAirCutThresholdTest,"CitixChase.FX.AirCutThreshold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixAirCutThresholdTest::RunTest(const FString&)
{
 TestEqual(TEXT("Exactly eighty percent remains off"),UCitixAirFlowComponent::StrengthForSpeed(160.f,200.f),0.f);
 TestTrue(TEXT("Just above eighty percent activates"),UCitixAirFlowComponent::StrengthForSpeed(160.1f,200.f)>0.f);
 TestEqual(TEXT("A different car uses its own cap"),UCitixAirFlowComponent::StrengthForSpeed(160.1f,250.f),0.f);
 TestTrue(TEXT("Intensity grows continuously"),UCitixAirFlowComponent::StrengthForSpeed(190.f,200.f)>UCitixAirFlowComponent::StrengthForSpeed(170.f,200.f));
 TestEqual(TEXT("Boost remains bounded"),UCitixAirFlowComponent::StrengthForSpeed(300.f,200.f),1.f);
 TestEqual(TEXT("No invalid cap"),UCitixAirFlowComponent::StrengthForSpeed(200.f,0.f),0.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixSmokeViewerTest,"CitixChase.FX.SmokeViewerOpacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixSmokeViewerTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* Cloud=World->SpawnActor<ACitixSmokeCloud>();
 auto* Runner=World->SpawnActor<APlayerState>();
 auto* Chaser=World->SpawnActor<APlayerState>();
 Cloud->EmitterPlayerState=Runner;
 TestEqual(TEXT("Deployer sees one third alpha"),Cloud->OpacityForViewer(Runner),1.f/3.f);
 TestEqual(TEXT("Other players see normal alpha"),Cloud->OpacityForViewer(Chaser),1.f);
 TestEqual(TEXT("Spectators see normal alpha"),Cloud->OpacityForViewer(nullptr),1.f);
 float Transmittance=1.f;
 for (int32 I=0;I<ACitixSmokeCloud::PuffCount;++I) Transmittance*=1.f-Cloud->PuffOpacityForViewer(1.f,Runner);
 TestTrue(TEXT("Even all overlapping owner puffs composite to at most one third opacity"),1.f-Transmittance<=1.f/3.f+.00001f);
 TestTrue(TEXT("Owner puff retains a visible veil"),Cloud->PuffOpacityForViewer(1.f,Runner)>0.f);
 TestEqual(TEXT("Other viewers keep each full opacity puff"),Cloud->PuffOpacityForViewer(.6f,Chaser),.6f);
 TestEqual(TEXT("Unborn owner puffs remain invisible"),Cloud->PuffOpacityForViewer(0.f,Runner),0.f);
 Cloud->EmitterPlayerState=nullptr;
 TestEqual(TEXT("Unresolved replication never clears smoke"),Cloud->OpacityForViewer(nullptr),1.f);
 World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixTracerEndpointTest,"CitixChase.FX.TracerStopsAtCover",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixTracerEndpointTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Wall=World->SpawnActor<AActor>();
 auto* Box=NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(25,100,100)); Box->SetWorldLocation(FVector(200,0,0));
 Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
 const FVector Start(0,0,0), End(1000,0,0);
 const FVector Clipped=ACitixBulletTracer::ResolveVisibleEndpoint(World,Start,End,nullptr);
 TestTrue(TEXT("Muzzle cover clips the tracer before its far endpoint"),Clipped.X>=170.f && Clipped.X<=176.f);
 TestTrue(TEXT("Unobstructed endpoint is unchanged"),ACitixBulletTracer::ResolveVisibleEndpoint(World,FVector(0,300,0),FVector(1000,300,0),nullptr).Equals(FVector(1000,300,0)));
 TestTrue(TEXT("Zero length is safe"),ACitixBulletTracer::ResolveVisibleEndpoint(World,Start,Start,nullptr).Equals(Start));
 World->DestroyWorld(false);
 return true;
}
#endif
