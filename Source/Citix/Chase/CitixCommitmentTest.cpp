#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Chase/CitixChaseRules.h"
#include "Sandbox/CitixDestinationBeacon.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixCommitmentTest,"CitixChase.Balance.ObjectiveCommitments",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixCommitmentTest::RunTest(const FString&) {
 TestEqual(TEXT("Relay has a 9.6 meter radius"),FCitixChaseRules::RelayInteractionRadius,960.f);
 TestEqual(TEXT("A low-speed chaser has 2.7 times base engine force"),FCitixChaseRules::EngineScale(false,0),2.7f);
 TestEqual(TEXT("Corner recovery retains low-speed force"),FCitixChaseRules::EngineScale(false,79.9f),2.7f);
 TestTrue(TEXT("Acceleration tapers continuously"),FCitixChaseRules::EngineScale(false,120)<FCitixChaseRules::EngineScale(false,80));
 TestTrue(TEXT("High-speed force stays 1.82"),FMath::IsNearlyEqual(FCitixChaseRules::EngineScale(false,200),1.82f,.0001f));
 TestEqual(TEXT("Top speed is unchanged"),FCitixChaseRules::SpeedLimit(false),292.5f/.036f);
 TestEqual(TEXT("Relay commitment lasts a full second"),FCitixChaseRules::RelaySyncDuration,1.f);
 TestEqual(TEXT("Escape requires four seconds"),FCitixChaseRules::EscapeCommitDuration,4.f);
 TestTrue(TEXT("Boundary at 9.6 meters and below sixty is valid"),FCitixChaseRules::RelayEligible(960,59.99f));
 TestFalse(TEXT("Exactly sixty is not below sixty"),FCitixChaseRules::RelayEligible(960,60));
 TestFalse(TEXT("Outside the zone cannot sync"),FCitixChaseRules::RelayEligible(960.01f,0));
 TestFalse(TEXT("Another road elevation cannot sync"),FCitixChaseRules::RelayEligible(0,0,351));
 TestTrue(TEXT("Relay cannot complete at 0.99 seconds"),FCitixChaseRules::CommitmentRemaining(10,10.99f,1)>0);
 TestEqual(TEXT("Full second completes relay"),FCitixChaseRules::CommitmentRemaining(10,11,1),0.f);
 TestTrue(TEXT("Escape cannot complete at 3.99 seconds"),FCitixChaseRules::CommitmentRemaining(10,13.99f,4)>0);
 TestEqual(TEXT("Escape completes at four seconds"),FCitixChaseRules::CommitmentRemaining(10,14,4),0.f);
 for(float FPS:{30.f,60.f,120.f}) TestTrue(TEXT("A frame before one second is still incomplete"),FCitixChaseRules::CommitmentRemaining(10,11-1/FPS,1)>0);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false); auto* Exit=World->SpawnActor<ACitixDestinationBeacon>(); Exit->SetExitProjection(FVector(0,0,100));
 auto* BeamProperty=FindFProperty<FObjectProperty>(Exit->GetClass(),TEXT("BeaconMesh")); auto* Beam=BeamProperty ? Cast<UStaticMeshComponent>(BeamProperty->GetObjectPropertyValue_InContainer(Exit)) : nullptr;
 TestTrue(TEXT("Escape commitment does not obscure the car with a solid beam"),Beam && !Beam->IsVisible());
 TArray<UInstancedStaticMeshComponent*> Parts; Exit->GetComponents(Parts); bool Ring=false;
 for(auto* Part:Parts) if(Part->GetInstanceCount()==32) {FTransform Pose;Part->GetInstanceTransform(0,Pose,false); Ring|=FMath::IsNearlyEqual(Pose.GetLocation().Size2D(),800.,1.);}
 TestTrue(TEXT("Ground boundary communicates the actual eight-meter zone"),Ring);
 Exit->Hide(); for(auto* Part:Parts) TestFalse(TEXT("Collected or hidden objective leaves no visible ring"),Part->IsVisible());
 auto* Relay=World->SpawnActor<ACitixDestinationBeacon>();Relay->SetRelayProjection(FVector(0,0,100));
 Parts.Reset();Relay->GetComponents(Parts);bool Expanded=false;
 for(auto* Part:Parts) if(Part->GetInstanceCount()==32) {FTransform Pose;Part->GetInstanceTransform(0,Pose,false);Expanded|=FMath::IsNearlyEqual(Pose.GetLocation().Size2D(),960.,1.);}
 TestTrue(TEXT("Relay boundary expands to 9.6m while escape stays 8m"),Expanded);
 World->DestroyWorld(false);
 return true;
}
#endif
