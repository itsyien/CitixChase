#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Chase/CitixChaseRules.h"
#include "Chase/CitixRelayLayout.h"
#include "Chase/CitixChasePlayerState.h"
#include "UObject/UnrealType.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixBalanceTest,"CitixChase.Balance.PursuitAndRelaySpread",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixBalanceTest::RunTest(const FString&) {
 TestEqual(TEXT("Five relays required"),FCitixChaseRules::RelaysRequired,5);
 TestEqual(TEXT("Ice reaches 56m"),FCitixChaseRules::IceRange,5600.f);
 TestEqual(TEXT("Ice recharges in 50s"),FCitixChaseRules::IceRecharge,50.f);
 TestEqual(TEXT("4.5 seconds of freeze control"),FCitixChaseRules::IceDuration,4.5f);
 TestTrue(TEXT("Chaser acceleration gains 40 percent over previous tuning"),FMath::IsNearlyEqual(FCitixChaseRules::EngineScale(false,200),1.82f,.0001f));
 TestEqual(TEXT("Chaser top speed is unchanged"),FCitixChaseRules::SpeedLimit(false),292.5f/.036f);
 TArray<FVector> Candidates; for(int32 I=0;I<36;++I) {float A=I*2*PI/36; Candidates.Add(FVector(40000*FMath::Cos(A),40000*FMath::Sin(A),100));}
 Candidates.Add(FVector(0,0,100));
 const auto A=FCitixRelayLayout::Select(Candidates,11),B=FCitixRelayLayout::Select(Candidates,19),Repeat=FCitixRelayLayout::Select(Candidates,11);
 TestEqual(TEXT("Six locations selected"),A.Num(),6);
 TestTrue(TEXT("Seed makes layout repeatable for diagnostics"),A==Repeat);
 TestTrue(TEXT("Different rooms can have different relay locations"),A!=B);
 for(int32 I=0;I<A.Num();++I) {TestTrue(TEXT("Relays avoid city centre cluster"),A[I].Size2D()>20000);for(int32 J=0;J<I;++J) TestTrue(TEXT("Relay locations remain at least 180m apart"),FVector::Dist2D(A[I],A[J])>=FCitixRelayLayout::MinimumSpacing);}
 TestEqual(TEXT("Empty road input fails safely"),FCitixRelayLayout::Select({},11).Num(),0);
 TestTrue(TEXT("Moving chaser may brake"),FCitixChaseRules::CanRapidBrake(true,true,true,1,8000));
 TestFalse(TEXT("Runner cannot brake"),FCitixChaseRules::CanRapidBrake(false,true,true,1,8000));
 TestFalse(TEXT("On-foot chaser cannot brake"),FCitixChaseRules::CanRapidBrake(true,false,true,1,8000));
 TestFalse(TEXT("No braking outside pursuit"),FCitixChaseRules::CanRapidBrake(true,true,false,1,8000));
 TestFalse(TEXT("Empty reserve rejects repeated use"),FCitixChaseRules::CanRapidBrake(true,true,true,0,8000));
 TestFalse(TEXT("Stopped car does not waste charge"),FCitixChaseRules::CanRapidBrake(true,true,true,1,0));
 for(float FPS:{30.f,60.f,120.f}) {
  FVector V(8000,0,-250); const FVector First=FCitixChaseRules::RapidBrakeVelocity(V,1.f/FPS);
  TestTrue(TEXT("Brake is fast but not instantaneous"),First.X>0 && First.X<V.X);
  for(int32 I=0;I<static_cast<int32>(FPS*.7f);++I) V=FCitixChaseRules::RapidBrakeVelocity(V,1.f/FPS);
  TestTrue(TEXT("Brake stops within 0.7s at different frame rates"),V.Size2D()<1.f);
  TestEqual(TEXT("Brake preserves vertical physics"),V.Z,-250.);
 }
 int32 Charge=0; float Next=0; FCitixChaseRules::RefillRapidBrake(10,Charge,Next);
 TestEqual(TEXT("Brake recharge takes 20 seconds"),Next,30.f);
 FCitixChaseRules::RefillRapidBrake(29.9f,Charge,Next); TestEqual(TEXT("No early brake charge"),Charge,0);
 FCitixChaseRules::RefillRapidBrake(30,Charge,Next); TestEqual(TEXT("Brake refills exactly on time"),Charge,1);
 FCitixChaseRules::RefillRapidBrake(100,Charge,Next); TestEqual(TEXT("Only one brake charge stored"),Charge,1);
 for(const TCHAR* Name:{TEXT("RapidBrakeCharges"),TEXT("NextRapidBrakeAt"),TEXT("RapidBrakeUntil")}) {
  const auto* Property=FindFProperty<FProperty>(ACitixChasePlayerState::StaticClass(),Name);
  TestTrue(TEXT("Brake state replicates to both players"),Property && Property->HasAnyPropertyFlags(CPF_Net));
 }
 return true;
}
#endif
