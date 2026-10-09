#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CitixSettingsWidget.generated.h"
class UButton;
class UTextBlock;
class USlider;
UCLASS()
class CITIX_API UCitixSettingsWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
 virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
 virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
 virtual void NativeDestruct() override;
 void Refresh();
 bool VerifyPresetClick(int32 Level);
 bool VerifyPerformanceControls();
private:
 UFUNCTION() void Low();
 UFUNCTION() void Medium();
 UFUNCTION() void High();
 UFUNCTION() void Max();
 UFUNCTION() void Auto();
 UFUNCTION() void Resume();
 UFUNCTION() void ResolutionChanged(float Value);
 UFUNCTION() void FPSChanged(float Value);
 UFUNCTION() void VolumeChanged(float Value);
 UFUNCTION() void BeginInteraction();
 UFUNCTION() void EndInteraction();
 bool bCapturing=false;
 bool bRefreshing=false;
 bool bKeyboardInteraction=false;
 bool bDirty=false;
 UPROPERTY() TArray<TObjectPtr<UButton>> Presets;
 UPROPERTY() TObjectPtr<UButton> AutoButton;
 UPROPERTY() TObjectPtr<UTextBlock> Status;
 UPROPERTY() TObjectPtr<UTextBlock> Description;
 UPROPERTY() TObjectPtr<USlider> ResolutionSlider;
 UPROPERTY() TObjectPtr<USlider> FPSSlider;
 UPROPERTY() TObjectPtr<UTextBlock> ResolutionValue;
 UPROPERTY() TObjectPtr<UTextBlock> FPSValue;
 UPROPERTY() TObjectPtr<USlider> VolumeSlider;
 UPROPERTY() TObjectPtr<UTextBlock> VolumeValue;
};
