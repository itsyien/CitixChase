#if WITH_DEV_AUTOMATION_TESTS
#include "Core/CitixGraphicsSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixMasterVolumeRangeTest,
 "Citix.Audio.MasterVolumeRange",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixMasterVolumeRangeTest::RunTest(const FString& Parameters)
{
 auto* Settings=NewObject<UCitixGraphicsSettings>();
 Settings->SetMasterVolumePercent(-4.f,nullptr);
 TestEqual(TEXT("Zero mutes; negative input cannot invert audio"),Settings->GetMasterVolumePercent(),0.f);
 Settings->SetMasterVolumePercent(125.f,nullptr);
 TestEqual(TEXT("Master volume cannot amplify beyond full volume"),Settings->GetMasterVolumePercent(),100.f);
 Settings->SetMasterVolumePercent(42.6f,nullptr);
 TestEqual(TEXT("Slider uses whole percent steps"),Settings->GetMasterVolumePercent(),43.f);
 Settings->MasterVolumePercent=std::numeric_limits<float>::quiet_NaN();
 TestEqual(TEXT("Malformed saved volume safely defaults to full volume"),Settings->GetMasterVolumePercent(),100.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixMasterVolumePersistenceTest,
 "Citix.Audio.MasterVolumePersistence",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixMasterVolumePersistenceTest::RunTest(const FString& Parameters)
{
 // Use an isolated config: automation must never overwrite the player's preferences.
 const FString Filename=FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(),TEXT("CitixAudioTest"),TEXT(".ini"));
 auto* Settings=NewObject<UCitixGraphicsSettings>();
 for(float Value : {0.f,37.f,100.f}) {
  Settings->SetMasterVolumePercent(Value,nullptr);
  Settings->SaveConfig(CPF_Config,*Filename,GConfig,false);
  GConfig->Flush(false,Filename);
  auto* Reloaded=NewObject<UCitixGraphicsSettings>();
  Reloaded->LoadConfig(nullptr,*Filename);
  TestEqual(TEXT("Volume including mute survives a fresh settings object"),Reloaded->GetMasterVolumePercent(),Value);
 }
 GConfig->UnloadFile(Filename);
 IFileManager::Get().Delete(*Filename);
 return true;
}
#endif
