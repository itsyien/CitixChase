#include "Core/CitixGraphicsSettings.h"
#include "Engine/Engine.h"
#include "Misc/App.h"
#include "HAL/IConsoleManager.h"
#include "City/CitixCityChunk.h"
#include "UObject/UObjectIterator.h"
#include "AudioDevice.h"
#include "Engine/World.h"

UCitixGraphicsSettings* UCitixGraphicsSettings::Get()
{
 return GEngine ? Cast<UCitixGraphicsSettings>(GEngine->GetGameUserSettings()) : nullptr;
}
const TCHAR* UCitixGraphicsSettings::PresetName(int32 Level)
{
 const TCHAR* Names[]={TEXT("LOW"),TEXT("MEDIUM"),TEXT("HIGH"),TEXT("MAX")};
 return Names[FMath::Clamp(Level,0,3)];
}
float UCitixGraphicsSettings::ResolutionForPreset(int32 Level)
{
 const float Scales[]={50.f,85.f,100.f,100.f};
 return Scales[FMath::Clamp(Level,0,3)];
}
int32 UCitixGraphicsSettings::Recommend(int32 GPUQuality,int32 CPUQuality)
{
 return FMath::Clamp(FMath::Min(GPUQuality,CPUQuality),0,3);
}
void UCitixGraphicsSettings::InitializeForPC()
{
 if (!FApp::CanEverRender()) return;
 if (!bHardwareDetected) {
  RunHardwareBenchmark(5);
  RecommendedPreset=Recommend(GetShadowQuality(),GetViewDistanceQuality());
  ManualPreset=-1;
  bHardwareDetected=true;
  UE_LOG(LogTemp,Log,TEXT("[CitixGraphics] CPU %.1f GPU %.1f -> %s"),GetLastCPUBenchmarkResult(),GetLastGPUBenchmarkResult(),PresetName(RecommendedPreset));
 }
 RecommendedPreset=FMath::Clamp(RecommendedPreset,0,3);
 ManualPreset=FMath::Clamp(ManualPreset,-1,3);
 ApplyPreset();
}
void UCitixGraphicsSettings::SelectPreset(int32 Level)
{
 ManualPreset=FMath::Clamp(Level,-1,3);
 if (ManualPreset < 0) ManualResolutionScale=-1.f;
 ApplyPreset();
}
float UCitixGraphicsSettings::EffectiveResolutionScale() const
{
 return ManualResolutionScale < 0.f ? ResolutionForPreset(EffectivePreset()) : FMath::Clamp(FMath::RoundToFloat(ManualResolutionScale),30.f,110.f);
}
void UCitixGraphicsSettings::SetResolutionScaleValueEx(float Percentage)
{
 ManualResolutionScale=FMath::Clamp(FMath::RoundToFloat(Percentage),30.f,110.f);
 ApplySceneScale();
}
float UCitixGraphicsSettings::FPSForDetent(int32 Detent)
{
 const float Caps[]={30.f,60.f,120.f,144.f,240.f,0.f};
 return Caps[FMath::Clamp(Detent,0,5)];
}
int32 UCitixGraphicsSettings::FPSDetent() const
{
 const float Limit=GetFrameRateLimit();
 if (Limit<=0.f) return 5;
 int32 Closest=0;
 for (int32 I=1;I<5;++I) if (FMath::Abs(FPSForDetent(I)-Limit)<FMath::Abs(FPSForDetent(Closest)-Limit)) Closest=I;
 return Closest;
}
void UCitixGraphicsSettings::SetFPSDetent(int32 Detent)
{
 SetFrameRateLimit(FPSForDetent(Detent));
 SetFrameRateLimitCVar(GetFrameRateLimit());
}
float UCitixGraphicsSettings::GetMasterVolumePercent() const
{
 return FMath::IsFinite(MasterVolumePercent) ? FMath::Clamp(FMath::RoundToFloat(MasterVolumePercent),0.f,100.f) : 100.f;
}
void UCitixGraphicsSettings::SetMasterVolumePercent(float Percentage,const UObject* WorldContext)
{
 MasterVolumePercent=Percentage;
 MasterVolumePercent=GetMasterVolumePercent();
 ApplyAudioSettings(WorldContext);
}
void UCitixGraphicsSettings::ApplyAudioSettings(const UObject* WorldContext) const
{
 // The device primary gain covers every source, including sounds already playing
 // and future spawns, without rewriting their individual volume or sound class.
 // Resolve this world's device so a client never changes a different PIE device.
 const UWorld* World=WorldContext ? WorldContext->GetWorld() : nullptr;
 if(World) if(FAudioDevice* Device=World->GetAudioDeviceRaw())
  Device->SetTransientPrimaryVolume(GetMasterVolumePercent()/100.f);
}
void UCitixGraphicsSettings::ApplyNonResolutionSettings()
{
 ScalabilityQuality.ResolutionQuality=EffectiveResolutionScale();
 SetDynamicResolutionEnabled(false);
 Super::ApplyNonResolutionSettings();
 // Scalability clamps its resolution CVar. Apply after it with game-setting
 // priority so preset changes and normal engine settings application retain 110%.
 ApplySceneScale();
 if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("foliage.MinimumScreenSize"))) C->Set(0.f,ECVF_SetByGameSetting);
 for(TObjectIterator<ACitixCityChunk> It;It;++It) if(!It->IsTemplate() && It->GetWorld()) It->RefreshDetailDrawDistance(EffectivePreset());
}
void UCitixGraphicsSettings::ApplySceneScale()
{
 ScalabilityQuality.ResolutionQuality=EffectiveResolutionScale();
 if (IConsoleVariable* Percentage=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
  Percentage->Set(EffectiveResolutionScale(),ECVF_SetByGameSetting);
}
void UCitixGraphicsSettings::ApplyPreset()
{
 const int32 Level=EffectivePreset();
 SetOverallScalabilityLevel(Level);
 SetAntiAliasingQuality(Level==0 ? 0 : FMath::Max(Level,1));
 // Keep all UI at native resolution; only the 3D scene is scaled.
 SetVSyncEnabled(false);
 ApplyNonResolutionSettings();
 SaveSettings();
}
