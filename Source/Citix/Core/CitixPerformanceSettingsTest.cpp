#if WITH_DEV_AUTOMATION_TESTS
#include "Core/CitixGraphicsSettings.h"
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixPerformancePresetPersistenceTest,
 "Citix.Performance.PresetPreservesIndependentControls",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixPerformancePresetPersistenceTest::RunTest(const FString& Parameters)
{
 UCitixGraphicsSettings* Settings = NewObject<UCitixGraphicsSettings>();
 Settings->bHardwareDetected = true;
 Settings->RecommendedPreset = 1;
 Settings->SetFrameRateLimit(144.f);
 Settings->SetResolutionScaleValueEx(63.f);
 Settings->SelectPreset(3);
 TestEqual(TEXT("Changing visual detail preserves the selected FPS cap"), Settings->GetFrameRateLimit(), 144.f);
 TestEqual(TEXT("Changing visual detail preserves a manually selected scene scale"), Settings->ScalabilityQuality.ResolutionQuality, 63.f);
 Settings->SelectPreset(-1);
 TestEqual(TEXT("Automatic restores the recommended scene scale"), Settings->ScalabilityQuality.ResolutionQuality, 85.f);
 TestEqual(TEXT("Automatic preserves the selected FPS cap"), Settings->GetFrameRateLimit(), 144.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixPerformanceRangeTest,
 "Citix.Performance.ScaleRangeAndRuntime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixPerformanceRangeTest::RunTest(const FString& Parameters)
{
 auto* Settings=NewObject<UCitixGraphicsSettings>();
 Settings->bHardwareDetected=true;
 for(float Percentage:{30.f,100.f,110.f}) {
  Settings->SetResolutionScaleValueEx(Percentage);
  auto* Runtime=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
  TestNotNull(TEXT("Runtime render percentage exists"),Runtime);
  if(Runtime) TestEqual(TEXT("Actual render percentage supports full range"),Runtime->GetFloat(),Percentage);
  Settings->ApplyNonResolutionSettings();
  if(Runtime) TestEqual(TEXT("Normal engine apply preserves scale beyond scalability clamp"),Runtime->GetFloat(),Percentage);
 }
 Settings->SetResolutionScaleValueEx(29.f); TestEqual(TEXT("Lower bound"),Settings->EffectiveResolutionScale(),30.f);
 Settings->SetResolutionScaleValueEx(111.f); TestEqual(TEXT("Upper bound"),Settings->EffectiveResolutionScale(),110.f);
 Settings->SetResolutionScaleValueEx(63.6f); TestEqual(TEXT("One percent steps"),Settings->EffectiveResolutionScale(),64.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixPerformanceFPSDetentsTest,
 "Citix.Performance.FPSDetents",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixPerformanceFPSDetentsTest::RunTest(const FString& Parameters)
{
 auto* Settings=NewObject<UCitixGraphicsSettings>();
 const float Expected[]={30,60,120,144,240,0};
 for(int32 I=0;I<6;++I) {
  Settings->SetFPSDetent(I);
  TestEqual(TEXT("Slider detent sets the selected cap"),Settings->GetFrameRateLimit(),Expected[I]);
  TestEqual(TEXT("Current cap returns its detent"),Settings->FPSDetent(),I);
  auto* Runtime=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
  if(Runtime) TestEqual(TEXT("Cap applies immediately"),Runtime->GetFloat(),Expected[I]);
 }
 Settings->SetFPSDetent(-1); TestEqual(TEXT("Lower detent clamp"),Settings->GetFrameRateLimit(),30.f);
 Settings->SetFPSDetent(6); TestEqual(TEXT("Upper detent clamp"),Settings->GetFrameRateLimit(),0.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixPerformanceSavedControlsTest,
 "Citix.Performance.SavedControlsRestore",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixPerformanceSavedControlsTest::RunTest(const FString& Parameters)
{
 auto* Settings=NewObject<UCitixGraphicsSettings>();
 Settings->bHardwareDetected=true; Settings->RecommendedPreset=1;
 Settings->SetResolutionScaleValueEx(110); Settings->SetFPSDetent(4); Settings->SelectPreset(0);
 auto* Reloaded=NewObject<UCitixGraphicsSettings>(); Reloaded->LoadSettings(true); Reloaded->ApplyNonResolutionSettings();
 TestEqual(TEXT("Manual scale survives fresh settings object"),Reloaded->EffectiveResolutionScale(),110.f);
 TestEqual(TEXT("FPS survives fresh settings object"),Reloaded->GetFrameRateLimit(),240.f);
 Reloaded->SelectPreset(-1);
 TestEqual(TEXT("Automatic restores benchmark recommendation"),Reloaded->EffectiveResolutionScale(),85.f);
 TestEqual(TEXT("Automatic leaves FPS independent"),Reloaded->GetFrameRateLimit(),240.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixLowTierTest,"Citix.Performance.ExtremelyLowRestoresHigherTier",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixLowTierTest::RunTest(const FString& Parameters)
{
 auto* Settings=NewObject<UCitixGraphicsSettings>(); Settings->bHardwareDetected=true; Settings->ManualResolutionScale=-1;
 Settings->SelectPreset(0);
 TestEqual(TEXT("Low automatic scene scale is 50 percent"),Settings->EffectiveResolutionScale(),50.f);
 auto* Distance=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"));
 auto* Foliage=IConsoleManager::Get().FindConsoleVariable(TEXT("foliage.CullDistanceScale"));
 auto* Bloom=IConsoleManager::Get().FindConsoleVariable(TEXT("r.BloomQuality"));
 TestNotNull(TEXT("Runtime distance control exists"),Distance); TestNotNull(TEXT("Runtime foliage distance exists"),Foliage);
 if(Distance) TestTrue(TEXT("Low shortens distant visibility"),FMath::IsNearlyEqual(Distance->GetFloat(),.15f));
 if(Foliage) TestTrue(TEXT("Low preserves the explicit detail distance floor"),FMath::IsNearlyEqual(Foliage->GetFloat(),1.f));
 if(Bloom) TestEqual(TEXT("Low removes bloom cost"),Bloom->GetInt(),0);
 Settings->SelectPreset(3);
 if(Distance) TestTrue(TEXT("Max restores full view distance"),FMath::IsNearlyEqual(Distance->GetFloat(),1.f));
 if(Foliage) TestTrue(TEXT("Max restores foliage distance"),FMath::IsNearlyEqual(Foliage->GetFloat(),1.f));
 return true;
}
#endif
