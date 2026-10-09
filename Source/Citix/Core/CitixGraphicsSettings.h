#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "CitixGraphicsSettings.generated.h"

UCLASS(Config=GameUserSettings)
class CITIX_API UCitixGraphicsSettings : public UGameUserSettings
{
 GENERATED_BODY()
public:
 static UCitixGraphicsSettings* Get();
 void InitializeForPC();
 void SelectPreset(int32 Level); // -1 restores automatic recommendation.
 // Citix owns the wider range; Unreal's standard setter clamps at 100%.
 void SetResolutionScaleValueEx(float Percentage);
 void SetFPSDetent(int32 Detent);
 float GetMasterVolumePercent() const;
 void SetMasterVolumePercent(float Percentage,const UObject* WorldContext);
 void ApplyAudioSettings(const UObject* WorldContext) const;
 float EffectiveResolutionScale() const;
 int32 FPSDetent() const;
 static float FPSForDetent(int32 Detent);
 virtual void ApplyNonResolutionSettings() override;
 int32 EffectivePreset() const { return ManualPreset < 0 ? RecommendedPreset : ManualPreset; }
 bool IsAutomatic() const { return ManualPreset < 0; }
 static const TCHAR* PresetName(int32 Level);
 static float ResolutionForPreset(int32 Level);
 static int32 Recommend(int32 GPUQuality, int32 CPUQuality);
 UPROPERTY(Config) bool bHardwareDetected=false;
 UPROPERTY(Config) int32 RecommendedPreset=2;
 UPROPERTY(Config) int32 ManualPreset=-1;
 UPROPERTY(Config) float ManualResolutionScale=-1.f;
 // Local GameUserSettings only: never replicated or included in match state.
 UPROPERTY(Config) float MasterVolumePercent=100.f;
private:
 void ApplyPreset();
 void ApplySceneScale();
};
