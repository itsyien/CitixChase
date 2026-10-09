#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Chase/CitixChasePlayerState.h"
#include "UObject/UnrealType.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixIceWave.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixIceSpaceTest, "CitixChase.IceWave.LocalScanWorldFrost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixIceSpaceTest::RunTest(const FString& Parameters)
{
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
 auto* World=GI->GetWorld();
 auto* Car=World->SpawnActor<AActor>();
 auto* Root=NewObject<USceneComponent>(Car); Car->SetRootComponent(Root); Root->RegisterComponent();
 Car->SetActorLocation(FVector(100,200,80));
 auto* Wave=World->SpawnActor<ACitixIceWave>(FVector(340,200,80),FRotator::ZeroRotator);
 auto* EmitterProperty=FindFProperty<FObjectProperty>(Wave->GetClass(),TEXT("EmitterCar"));
 TestNotNull(TEXT("Visual emitter exists independently of frozen target"),EmitterProperty);
 if (EmitterProperty) {
  TestTrue(TEXT("Emitter reference is replicated for remote viewers"),EmitterProperty->HasAnyPropertyFlags(CPF_Net));
  EmitterProperty->SetObjectPropertyValue_InContainer(Wave,Car);
 }
 Wave->StartedAt=.01f; Wave->DispatchBeginPlay();
 World->Tick(LEVELTICK_All,.25f); Wave->Tick(.25f);
 TestTrue(TEXT("Scan starts at the car bumper"),Wave->Scan->GetComponentLocation().Equals(FVector(340,200,80),1.f));
 TArray<UDecalComponent*> Stamps; Wave->GetComponents(Stamps);
 TestTrue(TEXT("Swept frost is deposited"),Stamps.Num()>0);
 const FTransform FirstStamp=Stamps.Num() ? Stamps[0]->GetComponentTransform() : FTransform::Identity;
 auto* FrostMID=Stamps.Num() ? Cast<UMaterialInstanceDynamic>(Stamps[0]->GetDecalMaterial()) : nullptr;
 const float FirstRadius=FrostMID ? FrostMID->K2_GetScalarParameterValue(TEXT("Radius")) : 0.f;
 float InnerRadius=0.f;
 if (FrostMID) TestTrue(TEXT("Authored frost shader supports a bounded world-space band"),FrostMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("InnerRadius")),InnerRadius));
 Car->SetActorLocationAndRotation(FVector(9100,2200,80),FRotator(0,75,0));
 World->Tick(LEVELTICK_All,.2f); Wave->Tick(.2f);
 const FVector NewBumper=Car->GetActorLocation()+Car->GetActorForwardVector()*240.f;
 TestTrue(TEXT("Scan follows a fast car translation and turn"),Wave->Scan->GetComponentLocation().Equals(NewBumper,1.f));
 TestTrue(TEXT("Scan forward follows the current heading"),Wave->Scan->GetForwardVector().Equals(Car->GetActorForwardVector(),.001f));
 FTransform Arc; Wave->Scan->GetInstanceTransform(32,Arc,true);
 TestTrue(TEXT("Front stays ahead even after the car outruns world wave speed"),Wave->Scan->GetComponentTransform().InverseTransformPosition(Arc.GetLocation()).X>1000.f);
 if (Stamps.Num()) TestTrue(TEXT("Previously deposited frost remains fixed in world space"),Stamps[0]->GetComponentTransform().Equals(FirstStamp,.01f));
 if (FrostMID) TestEqual(TEXT("A frozen stamp's radius stops advancing"),FrostMID->K2_GetScalarParameterValue(TEXT("Radius")),FirstRadius);
 Car->Destroy(); World->Tick(LEVELTICK_All,.1f); Wave->Tick(.1f);
 TestTrue(TEXT("Emitter removal leaves the last valid scan pose"),Wave->Scan->GetComponentLocation().Equals(NewBumper,1.f));
 Wave->GetComponents(Stamps);
 TestTrue(TEXT("World frost has a bounded decal budget"),Stamps.Num()<=24);
 // UWorld clamps unusually large frame deltas; advance real-sized frames.
 for (int32 Frame=0;Frame<55;++Frame) { World->Tick(LEVELTICK_All,.1f); Wave->Tick(.1f); }
 if (FrostMID) TestEqual(TEXT("World-space frost thaws completely"),FrostMID->K2_GetScalarParameterValue(TEXT("Fade")),0.f);
 GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixIceStateTest, "CitixChase.IceWave.ReplicatedState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixIceStateTest::RunTest(const FString& Parameters)
{
 for (const FName Name : {FName("IceCharges"), FName("NextIceAt"), FName("FrozenUntil"), FName("FrozenSpeedLimit")}) {
  const FProperty* Property=ACitixChasePlayerState::StaticClass()->FindPropertyByName(Name);
  TestNotNull(*FString::Printf(TEXT("Ice ability state %s exists"),*Name.ToString()),Property);
  if (Property) TestTrue(TEXT("Ability state is replicated to its driver"),Property->HasAnyPropertyFlags(CPF_Net));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixIceRulesTest, "CitixChase.IceWave.TargetingAndRecharge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixIceRulesTest::RunTest(const FString& Parameters)
{
 const FVector Origin(150,200,50), Forward(1,0,0);
 TestTrue(TEXT("Runner at fifty-six metres is hit"),FCitixChaseRules::InIceCone(Origin,Forward,Origin+FVector(5600,0,0)));
 TestFalse(TEXT("Runner beyond fifty-six metres is safe"),FCitixChaseRules::InIceCone(Origin,Forward,Origin+FVector(5601,0,0)));
 TestFalse(TEXT("Runner behind chaser is safe"),FCitixChaseRules::InIceCone(Origin,Forward,Origin+FVector(-500,0,0)));
 TestTrue(TEXT("Wide cone includes sixty degree edge"),FCitixChaseRules::InIceCone(Origin,Forward,Origin+FRotator(0,60,0).Vector()*2000));
 TestFalse(TEXT("Runner outside cone is safe"),FCitixChaseRules::InIceCone(Origin,Forward,Origin+FRotator(0,61,0).Vector()*2000));
 TestFalse(TEXT("Different elevated road is safe"),FCitixChaseRules::InIceCone(Origin,Forward,Origin+FVector(500,0,601)));
 int32 Charges=1; float Next=0;
 FCitixChaseRules::RefillIce(10,Charges,Next);
 TestEqual(TEXT("First recharge is fifty seconds away"),Next,60.f);
 FCitixChaseRules::RefillIce(59.9f,Charges,Next); TestEqual(TEXT("Cannot recharge early"),Charges,1);
 FCitixChaseRules::RefillIce(60,Charges,Next); TestEqual(TEXT("One charge arrives at fifty seconds"),Charges,2);
 TestEqual(TEXT("Full reserve stops recharge"),Next,0.f);
 FCitixChaseRules::RefillIce(1000,Charges,Next); TestEqual(TEXT("Never stores a third charge"),Charges,2);
 Charges=0; Next=50; FCitixChaseRules::RefillIce(100,Charges,Next);
 TestEqual(TEXT("Long frame catches up without exceeding capacity"),Charges,2);
 TestEqual(TEXT("Freeze lasts 4.5 seconds"),FCitixChaseRules::IceDuration,4.5f);
 return true;
}
#endif
