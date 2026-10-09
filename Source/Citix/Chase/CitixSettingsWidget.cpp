#include "Chase/CitixSettingsWidget.h"
#include "Core/CitixGraphicsSettings.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Font.h"
#include "HAL/IConsoleManager.h"

TSharedRef<SWidget> UCitixSettingsWidget::RebuildWidget()
{
 if (!WidgetTree || Status) return Super::RebuildWidget();
 SetIsFocusable(true);
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
 auto* Shade=WidgetTree->ConstructWidget<UBorder>(); Shade->SetBrushColor(FLinearColor(0,0,0,.65f));
 auto* Back=Root->AddChildToCanvas(Shade); Back->SetAnchors(FAnchors(0,0,1,1)); Back->SetOffsets(FMargin(0));
 // Fit the entire card inside the viewport, including small windows and UI DPI scaling.
 auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(); Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::DownOnly);
 auto* FitSlot=Root->AddChildToCanvas(Fit); FitSlot->SetAnchors(FAnchors(0,0,1,1)); FitSlot->SetOffsets(FMargin(24));
 auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(); Bounds->SetWidthOverride(620); Bounds->SetHeightOverride(790); Fit->AddChild(Bounds);
 auto* Card=WidgetTree->ConstructWidget<UBorder>(); Card->SetBrushColor(FLinearColor(.018f,.035f,.065f,.98f)); Card->SetPadding(FMargin(28,24)); Bounds->AddChild(Card);
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed); Card->SetContent(Scroll);
 auto* Panel=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Panel);
 const FLinearColor Blue(.12f,.72f,1.f),Dim(.65f,.75f,.85f);
 auto* Font=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
 auto Text=[&](const FString& Value,int Size,FLinearColor Color) {
  auto* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Value));
  T->SetFont(FSlateFontInfo(Font,Size,TEXT("Regular"))); T->SetColorAndOpacity(Color); return T;
 };
 auto Label=[&](const FString& Value,int Size,FLinearColor Color,float Bottom=12.f) {
  auto* T=Text(Value,Size,Color); T->SetAutoWrapText(true); Panel->AddChildToVerticalBox(T)->SetPadding(FMargin(0,0,0,Bottom)); return T;
 };
 Label(TEXT("CITIXCHASE  /  SETTINGS"),12,Blue,8);
 Label(TEXT("AUDIO & PERFORMANCE"),28,FLinearColor::White,10);
 Status=Label(TEXT("Detecting hardware..."),14,Blue,16);
 auto Button=[&](const FString& Value) {
  auto* B=WidgetTree->ConstructWidget<UButton>();
  FButtonStyle Style; Style.SetNormal(FSlateColorBrush(FLinearColor(.05f,.14f,.22f))).SetHovered(FSlateColorBrush(FLinearColor(.08f,.32f,.5f))).SetPressed(FSlateColorBrush(Blue)).SetNormalPadding(FMargin(10,10)).SetPressedPadding(FMargin(10,11,10,9)); B->SetStyle(Style);
  auto* T=Text(Value,14,FLinearColor::White); T->SetJustification(ETextJustify::Center); B->AddChild(T); return B;
 };
 Label(TEXT("GRAPHICS DETAIL"),12,Dim,8);
 auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(); Panel->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,10));
 for (int I=0;I<4;++I) { auto* B=Button(UCitixGraphicsSettings::PresetName(I)); Presets.Add(B); auto* S=Row->AddChildToHorizontalBox(B); S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetPadding(FMargin(I ? 6 : 0,0,0,0)); }
 Presets[0]->OnClicked.AddDynamic(this,&UCitixSettingsWidget::Low); Presets[1]->OnClicked.AddDynamic(this,&UCitixSettingsWidget::Medium); Presets[2]->OnClicked.AddDynamic(this,&UCitixSettingsWidget::High); Presets[3]->OnClicked.AddDynamic(this,&UCitixSettingsWidget::Max);
 Description=Label(TEXT(""),12,Dim,20);
 auto Control=[&](const FString& Name,UTextBlock*& Value) {
  auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>(); Panel->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,4));
  Header->AddChildToHorizontalBox(Text(Name,16,FLinearColor::White))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  Value=Text(TEXT(""),16,Blue); Header->AddChildToHorizontalBox(Value);
  auto* Slider=WidgetTree->ConstructWidget<USlider>(); Slider->SetSliderBarColor(FLinearColor(.12f,.25f,.34f)); Slider->SetSliderHandleColor(Blue);
  Slider->RequiresControllerLock=false; Slider->MouseUsesStep=true; Slider->IsFocusable=true; Slider->bPreventThrottling=true;
  auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(32); Height->AddChild(Slider); Panel->AddChildToVerticalBox(Height);
  Slider->OnMouseCaptureBegin.AddDynamic(this,&UCitixSettingsWidget::BeginInteraction); Slider->OnControllerCaptureBegin.AddDynamic(this,&UCitixSettingsWidget::BeginInteraction);
  Slider->OnMouseCaptureEnd.AddDynamic(this,&UCitixSettingsWidget::EndInteraction); Slider->OnControllerCaptureEnd.AddDynamic(this,&UCitixSettingsWidget::EndInteraction);
  return Slider;
 };
 UTextBlock* ScaleText=nullptr; ResolutionSlider=Control(TEXT("RESOLUTION SCALE"),ScaleText); ResolutionValue=ScaleText;
 ResolutionSlider->SetMinValue(30); ResolutionSlider->SetMaxValue(110); ResolutionSlider->SetStepSize(1); ResolutionSlider->OnValueChanged.AddDynamic(this,&UCitixSettingsWidget::ResolutionChanged);
 ResolutionSlider->SetToolTipText(FText::FromString(TEXT("Lower values improve performance. 100% is native clarity; 110% renders extra detail. Arrow keys adjust by 1%.")));
 // Native sits seven eighths of the way from 30 to 110, directly beneath the track.
 auto* Marks=WidgetTree->ConstructWidget<UCanvasPanel>(); auto* MarksHeight=WidgetTree->ConstructWidget<USizeBox>(); MarksHeight->SetHeightOverride(22); MarksHeight->AddChild(Marks); Panel->AddChildToVerticalBox(MarksHeight)->SetPadding(FMargin(0,0,0,3));
 auto Mark=[&](const FString& Value,float X,float Align,FLinearColor Color) { auto* T=Text(Value,11,Color); auto* TrackSlot=Marks->AddChildToCanvas(T); TrackSlot->SetAnchors(FAnchors(X,0)); TrackSlot->SetAlignment(FVector2D(Align,0)); TrackSlot->SetAutoSize(true); };
 Mark(TEXT("30%"),0,0,Dim); Mark(TEXT("100% NATIVE "),.875f,1,Blue); Mark(TEXT("|"),.875f,.5f,Blue); Mark(TEXT("110%"),1,1,Dim);
 Label(TEXT("Sharper image or smoother driving. Only the 3D scene is scaled."),12,Dim,16);
 UTextBlock* CapText=nullptr; FPSSlider=Control(TEXT("MAXIMUM FPS"),CapText); FPSValue=CapText;
 FPSSlider->SetMinValue(0); FPSSlider->SetMaxValue(5); FPSSlider->SetStepSize(1); FPSSlider->OnValueChanged.AddDynamic(this,&UCitixSettingsWidget::FPSChanged);
 FPSSlider->SetToolTipText(FText::FromString(TEXT("Choose 30, 60, 120, 144, 240 or Unlimited. Arrow keys move one option at a time.")));
 auto* FPSMarks=WidgetTree->ConstructWidget<UCanvasPanel>(); auto* FPSMarksHeight=WidgetTree->ConstructWidget<USizeBox>(); FPSMarksHeight->SetHeightOverride(20); FPSMarksHeight->AddChild(FPSMarks); Panel->AddChildToVerticalBox(FPSMarksHeight)->SetPadding(FMargin(0,0,0,16));
 const TCHAR* Caps[]={TEXT("30"),TEXT("60"),TEXT("120"),TEXT("144"),TEXT("240"),TEXT("Unlimited")};
 for(int32 I=0;I<6;++I) { auto* T=Text(Caps[I],11,Dim); auto* TrackSlot=FPSMarks->AddChildToCanvas(T); TrackSlot->SetAnchors(FAnchors(I/5.f,0)); TrackSlot->SetAlignment(FVector2D(I==0 ? 0.f : I==5 ? 1.f : .5f,0)); TrackSlot->SetAutoSize(true); }
 AutoButton=Button(TEXT("USE AUTOMATIC")); Panel->AddChildToVerticalBox(AutoButton)->SetPadding(FMargin(0,0,0,8)); AutoButton->OnClicked.AddDynamic(this,&UCitixSettingsWidget::Auto);
 Label(TEXT("Restores recommended detail and resolution. Keeps your FPS limit."),11,Dim,14);
 Label(TEXT("AUDIO"),12,Dim,6);
 UTextBlock* AudioText=nullptr; VolumeSlider=Control(TEXT("MASTER VOLUME"),AudioText); VolumeValue=AudioText;
 VolumeSlider->SetMinValue(0); VolumeSlider->SetMaxValue(100); VolumeSlider->SetStepSize(1); VolumeSlider->OnValueChanged.AddDynamic(this,&UCitixSettingsWidget::VolumeChanged);
 VolumeSlider->SetToolTipText(FText::FromString(TEXT("Changes all game audio immediately. 0% mutes. Arrow keys adjust by 1%. Saved on this PC.")));
 Label(TEXT("0% mutes all game audio. Applies immediately."),11,Dim,16);
 auto* BackButton=Button(TEXT("RESUME  /  ESC")); Panel->AddChildToVerticalBox(BackButton)->SetPadding(FMargin(0,0,0,12)); BackButton->OnClicked.AddDynamic(this,&UCitixSettingsWidget::Resume);
 Label(TEXT("Saved on this PC. Multiplayer continues while settings are open."),11,Dim,0);
 Refresh(); return Super::RebuildWidget();
}
void UCitixSettingsWidget::Refresh()
{
 const auto* S=UCitixGraphicsSettings::Get(); if (!S || !Status) return;
 // UE 5.8's programmatic SetValue broadcasts OnValueChanged. Synchronizing
 // labels/thumbs must not create a manual override or save a new interaction.
 TGuardValue<bool> RefreshGuard(bRefreshing,true);
 Status->SetText(FText::FromString(FString::Printf(TEXT("%s DETAIL  /  %s    ·    Recommended: %s"),S->IsAutomatic()?TEXT("AUTO"):TEXT("CUSTOM"),S->PresetName(S->EffectivePreset()),S->PresetName(S->RecommendedPreset))));
 const TCHAR* Help[]={TEXT("Minimum detail, shorter distant visibility, minimal effects."),TEXT("Balanced lighting, detail and responsiveness."),TEXT("Rich lighting and detailed shadows."),TEXT("Full lighting, reflections and effects.")};
 Description->SetText(FText::FromString(Help[S->EffectivePreset()]));
 ResolutionSlider->SetValue(S->EffectiveResolutionScale()); FPSSlider->SetValue(S->FPSDetent());
 ResolutionValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%%s"),S->EffectiveResolutionScale(),S->ManualResolutionScale<0.f ? TEXT("  · AUTO") : TEXT(""))));
 FPSValue->SetText(FText::FromString(S->GetFrameRateLimit()<=0.f ? TEXT("Unlimited") : FString::Printf(TEXT("%.0f FPS"),S->GetFrameRateLimit())));
 VolumeSlider->SetValue(S->GetMasterVolumePercent());
 VolumeValue->SetText(FText::FromString(S->GetMasterVolumePercent()==0.f ? TEXT("0%  · MUTED") : FString::Printf(TEXT("%.0f%%"),S->GetMasterVolumePercent())));
 auto Selected=[](UButton* Button,bool Active) { FButtonStyle Style=Button->GetStyle(); Style.SetNormal(FSlateColorBrush(Active ? FLinearColor(.06f,.35f,.5f) : FLinearColor(.05f,.14f,.22f))); Button->SetStyle(Style); };
 for(int I=0;I<Presets.Num();++I) Selected(Presets[I],S->EffectivePreset()==I);
 Selected(AutoButton,S->IsAutomatic() && S->ManualResolutionScale<0.f);
}
void UCitixSettingsWidget::BeginInteraction(){bCapturing=true;}
void UCitixSettingsWidget::EndInteraction(){bCapturing=false; bKeyboardInteraction=false; if(bDirty) {if(auto* S=UCitixGraphicsSettings::Get()) S->SaveSettings(); bDirty=false;}}
void UCitixSettingsWidget::ResolutionChanged(float Value){if(bRefreshing) return; if(auto* S=UCitixGraphicsSettings::Get()) S->SetResolutionScaleValueEx(Value); bDirty=true; Refresh(); if(!bCapturing) EndInteraction();}
void UCitixSettingsWidget::FPSChanged(float Value){if(bRefreshing) return; if(auto* S=UCitixGraphicsSettings::Get()) S->SetFPSDetent(FMath::RoundToInt(Value)); bDirty=true; Refresh(); if(!bCapturing) EndInteraction();}
void UCitixSettingsWidget::VolumeChanged(float Value){if(bRefreshing) return; if(auto* S=UCitixGraphicsSettings::Get()) S->SetMasterVolumePercent(Value,GetWorld()); bDirty=true; Refresh(); if(!bCapturing) EndInteraction();}
void UCitixSettingsWidget::Low(){if(auto* S=UCitixGraphicsSettings::Get()) S->SelectPreset(0); Refresh();}
bool UCitixSettingsWidget::VerifyPresetClick(int32 Level)
{
 if(Level<0) AutoButton->OnClicked.Broadcast();
 else if(Presets.IsValidIndex(Level)) Presets[Level]->OnClicked.Broadcast();
 auto* S=UCitixGraphicsSettings::Get(); if(!S) return false;
 S->LoadSettings(true);
 const IConsoleVariable* Runtime=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
 const bool Passed=S->ManualPreset==Level && S->GetShadowQuality()==S->EffectivePreset() && FMath::IsNearlyEqual(S->ScalabilityQuality.ResolutionQuality,S->EffectiveResolutionScale()) && Runtime && FMath::IsNearlyEqual(Runtime->GetFloat(),S->EffectiveResolutionScale());
 UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] preset=%d storedPreset=%d shadow=%d effective=%d metadataScale=%.0f desiredScale=%.0f runtimeScale=%.0f manualScale=%.0f fps=%.0f result=%s"),Level,S->ManualPreset,S->GetShadowQuality(),S->EffectivePreset(),S->ScalabilityQuality.ResolutionQuality,S->EffectiveResolutionScale(),Runtime ? Runtime->GetFloat() : -1.f,S->ManualResolutionScale,S->GetFrameRateLimit(),Passed ? TEXT("PASS") : TEXT("FAIL"));
 return Passed;
}
bool UCitixSettingsWidget::VerifyPerformanceControls()
{
 auto* S=UCitixGraphicsSettings::Get(); if(!S || !ResolutionSlider || !FPSSlider) return false;
 const int32 PreviousPreset=S->ManualPreset;
 const float PreviousScale=S->ManualResolutionScale,PreviousFPS=S->GetFrameRateLimit();
 IConsoleVariable* Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
 IConsoleVariable* Cap=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
 bool Passed=Scale && Cap;
 for (float Value:{30.f,100.f,110.f}) {
  ResolutionSlider->OnMouseCaptureBegin.Broadcast(); ResolutionSlider->OnValueChanged.Broadcast(Value); ResolutionSlider->OnMouseCaptureEnd.Broadcast();
  Passed &= Scale && FMath::IsNearlyEqual(Scale->GetFloat(),Value) && FMath::IsNearlyEqual(ResolutionSlider->GetValue(),Value);
  UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] requested=%.0f runtime=%.0f widget=%.0f cumulative=%s"),Value,Scale ? Scale->GetFloat() : -1.f,ResolutionSlider->GetValue(),Passed ? TEXT("PASS") : TEXT("FAIL"));
 }
 for (int32 I=0;I<6;++I) {
  FPSSlider->OnMouseCaptureBegin.Broadcast(); FPSSlider->OnValueChanged.Broadcast(static_cast<float>(I)); FPSSlider->OnMouseCaptureEnd.Broadcast();
  Passed &= Cap && FMath::IsNearlyEqual(Cap->GetFloat(),S->FPSForDetent(I)) && FPSSlider->GetValue()==I;
  UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] detent=%d runtimeFPS=%.0f widgetDetent=%.1f cumulative=%s"),I,Cap ? Cap->GetFloat() : -1.f,FPSSlider->GetValue(),Passed ? TEXT("PASS") : TEXT("FAIL"));
 }
 FPSSlider->OnValueChanged.Broadcast(3.f);
 const bool PresetPassed=VerifyPresetClick(0) && S->ManualResolutionScale==110.f && S->GetFrameRateLimit()==144.f;
 Passed &= PresetPassed;
 UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] saved_manual_preset result=%s scale=%.0f fps=%.0f"),PresetPassed ? TEXT("PASS") : TEXT("FAIL"),S->ManualResolutionScale,S->GetFrameRateLimit());
 S->InitializeForPC();
 const bool StartupPassed=Scale && Scale->GetFloat()==110.f && Cap && Cap->GetFloat()==144.f;
 Passed &= StartupPassed;
 UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] reinitialize result=%s runtimeScale=%.0f runtimeFPS=%.0f"),StartupPassed ? TEXT("PASS") : TEXT("FAIL"),Scale ? Scale->GetFloat() : -1.f,Cap ? Cap->GetFloat() : -1.f);
 const bool AutoPassed=VerifyPresetClick(-1) && S->ManualResolutionScale<0.f && S->GetFrameRateLimit()==144.f;
 Passed &= AutoPassed;
 UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] automatic result=%s manualScale=%.0f fps=%.0f"),AutoPassed ? TEXT("PASS") : TEXT("FAIL"),S->ManualResolutionScale,S->GetFrameRateLimit());
 S->ManualResolutionScale=PreviousScale; S->ManualPreset=PreviousPreset; S->SetFrameRateLimit(PreviousFPS); S->ApplyNonResolutionSettings(); S->SaveSettings(); Refresh();
 UE_LOG(LogTemp,Log,TEXT("[CitixPerformanceProbe] sliders_runtime_reload_automatic=%s"),Passed ? TEXT("PASS") : TEXT("FAIL"));
 return Passed;
}
void UCitixSettingsWidget::Medium(){if(auto* S=UCitixGraphicsSettings::Get()) S->SelectPreset(1); Refresh();}
void UCitixSettingsWidget::High(){if(auto* S=UCitixGraphicsSettings::Get()) S->SelectPreset(2); Refresh();}
void UCitixSettingsWidget::Max(){if(auto* S=UCitixGraphicsSettings::Get()) S->SelectPreset(3); Refresh();}
void UCitixSettingsWidget::Auto(){if(auto* S=UCitixGraphicsSettings::Get()) S->SelectPreset(-1); Refresh();}
void UCitixSettingsWidget::Resume(){EndInteraction(); if(auto* PC=Cast<ACitixDrivingPlayerController>(GetOwningPlayer())) PC->ToggleSettingsMenu();}
void UCitixSettingsWidget::NativeDestruct(){EndInteraction(); Super::NativeDestruct();}
FReply UCitixSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
 if(E.GetKey()==EKeys::Escape){Resume(); return FReply::Handled();}
 const FKey Key=E.GetKey();
 const bool Adjusting=Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right;
 if(Adjusting && ((ResolutionSlider && ResolutionSlider->HasKeyboardFocus()) || (FPSSlider && FPSSlider->HasKeyboardFocus()) || (VolumeSlider && VolumeSlider->HasKeyboardFocus()))) {bKeyboardInteraction=true; bCapturing=true;}
 else if(bKeyboardInteraction) EndInteraction();
 return Super::NativeOnPreviewKeyDown(G,E);
}
FReply UCitixSettingsWidget::NativeOnKeyUp(const FGeometry& G,const FKeyEvent& E)
{
 if(bKeyboardInteraction) EndInteraction();
 return Super::NativeOnKeyUp(G,E);
}
FReply UCitixSettingsWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
 if(E.GetKey()==EKeys::Escape){Resume(); return FReply::Handled();}
 return Super::NativeOnKeyDown(G,E);
}
