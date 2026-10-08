#include "Chase/CitixChaseLobbyWidget.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/Font.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Brushes/SlateColorBrush.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "Citix.h"

namespace {
 const FLinearColor Blue(.12f,.72f,1), Dim(.61f,.71f,.82f), Dark(.012f,.023f,.045f,.97f);
 UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int Size,FLinearColor Color=FLinearColor::White) {
  auto* T=Tree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Value)); T->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size,TEXT("Regular"))); T->SetColorAndOpacity(Color); T->SetAutoWrapText(true); return T;
 }
 UTextBlock* Label(UWidgetTree* Tree,UVerticalBox* Box,const FString& Value,int Size,FLinearColor Color=FLinearColor::White) {
  auto* T=Text(Tree,Value,Size,Color); Box->AddChildToVerticalBox(T)->SetPadding(FMargin(0,0,0,12)); return T;
 }
 UButton* Button(UWidgetTree* Tree,const FString& Value,UTextBlock** Out=nullptr,bool Primary=false) {
  auto* B=Tree->ConstructWidget<UButton>(); FButtonStyle Style;
  Style.SetNormal(FSlateColorBrush(Primary ? FLinearColor(.025f,.3f,.49f) : FLinearColor(.035f,.09f,.15f))).SetHovered(FSlateColorBrush(FLinearColor(.06f,.35f,.55f))).SetPressed(FSlateColorBrush(FLinearColor(.08f,.5f,.7f))).SetDisabled(FSlateColorBrush(FLinearColor(.028f,.04f,.06f))).SetNormalPadding(FMargin(14,12)).SetPressedPadding(FMargin(14,13,14,11)); B->SetStyle(Style);
  auto* T=Text(Tree,Value,16); T->SetAutoWrapText(false); T->SetJustification(ETextJustify::Center); B->AddChild(T); if(Out)*Out=T; return B;
 }
 UEditableTextBox* Entry(UWidgetTree* Tree,const FString& Initial,const FString& Hint) {
  auto* E=Tree->ConstructWidget<UEditableTextBox>(); E->SetText(FText::FromString(Initial)); E->SetHintText(FText::FromString(Hint));
  auto Style=E->GetWidgetStyle(); Style.SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),17)).SetBackgroundImageNormal(FSlateColorBrush(FLinearColor(.025f,.05f,.085f))).SetBackgroundImageHovered(FSlateColorBrush(FLinearColor(.04f,.09f,.15f))).SetBackgroundImageFocused(FSlateColorBrush(FLinearColor(.055f,.16f,.24f))).SetPadding(FMargin(12,10)); E->SetWidgetStyle(Style); E->SetForegroundColor(FLinearColor(.86f,.94f,1)); return E;
 }
 void ClickByKeyboard(UButton* Target,UWorld* World) {
  if(!Target || !Target->GetIsEnabled() || !FSlateApplication::IsInitialized() || Target->TakeWidget()->GetPaintSpaceGeometry().GetLocalSize().X<10) return;
  const TWeakObjectPtr<UButton> Weak(Target);
  World->GetTimerManager().SetTimerForNextTick([Weak]() { if(!Weak.IsValid() || !FSlateApplication::IsInitialized())return; FSlateApplication::Get().SetKeyboardFocus(Weak->TakeWidget(),EFocusCause::Navigation); FKeyEvent Key(EKeys::Enter,FModifierKeysState(),0,false,0,0); FSlateApplication::Get().ProcessKeyDownEvent(Key); FSlateApplication::Get().ProcessKeyUpEvent(Key); });
 }
}
TSharedRef<SWidget> UCitixNearbyRoomWidget::RebuildWidget()
{
 auto* Box=WidgetTree->ConstructWidget<UBorder>(); WidgetTree->RootWidget=Box; Box->SetBrushColor(FLinearColor(.026f,.057f,.095f)); Box->SetPadding(FMargin(14,12));
 auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(); Box->AddChild(Row);
 auto* Info=WidgetTree->ConstructWidget<UVerticalBox>(); Row->AddChildToHorizontalBox(Info)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 Label(WidgetTree,Info,Room.Name,20);
 const FString Extra=!Room.bCompatible ? TEXT("Different version") : Room.bInProgress ? TEXT("Match in progress") : Room.Players>=Room.Capacity ? TEXT("Room full") : TEXT("Ready to join");
 Label(WidgetTree,Info,FString::Printf(TEXT("%d/%d players  ·  %s"),Room.Players,Room.Capacity,*Extra),13,Dim);
 JoinButton=Button(WidgetTree,TEXT("JOIN"),nullptr,true); JoinButton->SetIsEnabled(Room.CanJoin()); JoinButton->OnClicked.AddDynamic(this,&UCitixNearbyRoomWidget::JoinClicked); Row->AddChildToHorizontalBox(JoinButton)->SetVerticalAlignment(VAlign_Center);
 return Super::RebuildWidget();
}
void UCitixNearbyRoomWidget::JoinClicked() { if(GetGameInstance()) GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>()->JoinRoom(RoomIndex); }
UCitixSessionSubsystem* UCitixChaseLobbyWidget::Manager() const { return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>() : nullptr; }
TSharedRef<SWidget> UCitixChaseLobbyWidget::RebuildWidget()
{
 if(!WidgetTree || HostButton)return Super::RebuildWidget(); SetIsFocusable(true);
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
 auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit); Scale->SetStretchDirection(EStretchDirection::DownOnly);
 auto* ScaleSlot=Root->AddChildToCanvas(Scale); ScaleSlot->SetAnchors(FAnchors(.045f,.075f,.51f,.925f)); ScaleSlot->SetOffsets(FMargin(0));
 auto* Size=WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(620); Size->SetHeightOverride(780); Scale->AddChild(Size);
 auto* Card=WidgetTree->ConstructWidget<UBorder>(); Card->SetBrushColor(Dark); Card->SetPadding(FMargin(30,26)); Size->AddChild(Card);
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); MainScroll=Scroll; Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed); Card->AddChild(Scroll);
 auto* Panel=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Panel);
 Label(WidgetTree,Panel,TEXT("URBAN PURSUIT  /  TWO DRIVERS  /  v1.0"),12,Blue);
 Label(WidgetTree,Panel,TEXT("CITIXCHASE"),44,FLinearColor::White);
 Label(WidgetTree,Panel,TEXT("Outrun. Break away. Escape."),17,Dim);
 BrowserPanel=WidgetTree->ConstructWidget<UVerticalBox>(); Panel->AddChildToVerticalBox(BrowserPanel)->SetPadding(FMargin(0,12,0,0));
 Label(WidgetTree,BrowserPanel,TEXT("PLAY TOGETHER ONLINE"),14,Blue);
 Label(WidgetTree,BrowserPanel,TEXT("Find your friend across the internet. No sign-in needed."),14,Dim);
 Label(WidgetTree,BrowserPanel,TEXT("YOUR ROOM NAME"),11,Dim);
 RoomEntry=Entry(WidgetTree,UCitixSessionSubsystem::DefaultRoomName(),TEXT("Give your room a name")); BrowserPanel->AddChildToVerticalBox(RoomEntry)->SetPadding(FMargin(0,0,0,12));
 auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>(); BrowserPanel->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,0,0,14));
 HostButton=Button(WidgetTree,TEXT("HOST ONLINE"),nullptr,true); FindButton=Button(WidgetTree,TEXT("FIND ONLINE GAMES"));
 Actions->AddChildToHorizontalBox(HostButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); auto* FS=Actions->AddChildToHorizontalBox(FindButton); FS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); FS->SetPadding(FMargin(10,0,0,0));
 HostButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::HostClicked); FindButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::FindClicked);
 ResultsLabel=Label(WidgetTree,BrowserPanel,TEXT("ONLINE GAMES"),11,Dim);
 auto* ListScroll=WidgetTree->ConstructWidget<UScrollBox>(); auto* ListSize=WidgetTree->ConstructWidget<USizeBox>(); ListSize->SetMaxDesiredHeight(235); ListSize->AddChild(ListScroll); BrowserPanel->AddChildToVerticalBox(ListSize);
 RoomsBox=WidgetTree->ConstructWidget<UVerticalBox>(); ListScroll->AddChild(RoomsBox);
 ConnectedPanel=WidgetTree->ConstructWidget<UVerticalBox>(); Panel->AddChildToVerticalBox(ConnectedPanel)->SetPadding(FMargin(0,16,0,8));
 RoomType=Label(WidgetTree,ConnectedPanel,TEXT("ONLINE ROOM"),12,Blue); RoomTitle=Label(WidgetTree,ConnectedPanel,TEXT(""),26);
 auto* Drivers=WidgetTree->ConstructWidget<UHorizontalBox>(); ConnectedPanel->AddChildToVerticalBox(Drivers)->SetPadding(FMargin(0,12));
 DriverOne=Text(WidgetTree,TEXT("01 / DRIVER"),15,Blue); DriverTwo=Text(WidgetTree,TEXT("02 / OPEN SLOT"),15,Dim);
 Drivers->AddChildToHorizontalBox(DriverOne)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Drivers->AddChildToHorizontalBox(DriverTwo)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 UTextBlock* Ready=nullptr; EnterLobbyButton=Button(WidgetTree,TEXT("READY"),&Ready,true); ReadyLabel=Ready;
 ConnectedPanel->AddChildToVerticalBox(EnterLobbyButton)->SetPadding(FMargin(0,8)); EnterLobbyButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::EnterLobbyClicked);
 Label(WidgetTree,ConnectedPanel,TEXT("Both drivers ready up to start. Your roles switch next round."),14,Dim);
 LeaveButton=Button(WidgetTree,TEXT("LEAVE ROOM")); ConnectedPanel->AddChildToVerticalBox(LeaveButton)->SetPadding(FMargin(0,0,0,8)); LeaveButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::LeaveClicked);
 StatusText=Label(WidgetTree,Panel,TEXT(""),15,Dim);
 auto* Recovery=WidgetTree->ConstructWidget<UHorizontalBox>(); Panel->AddChildToVerticalBox(Recovery)->SetPadding(FMargin(0,0,0,12));
 RetryButton=Button(WidgetTree,TEXT("RETRY")); CancelButton=Button(WidgetTree,TEXT("CANCEL")); Recovery->AddChildToHorizontalBox(RetryButton); Recovery->AddChildToHorizontalBox(CancelButton)->SetPadding(FMargin(8,0,0,0)); RetryButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::RetryClicked); CancelButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::CancelClicked);
 UTextBlock* AdvancedText=nullptr; auto* Advanced=Button(WidgetTree,TEXT("ADVANCED  +"),&AdvancedText); AdvancedToggle=Advanced; AdvancedLabel=AdvancedText; AdvancedText->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),12)); AdvancedText->SetColorAndOpacity(Dim); auto AdvancedStyle=Advanced->GetStyle(); AdvancedStyle.SetNormal(FSlateColorBrush(FLinearColor(.016f,.032f,.055f))).SetNormalPadding(FMargin(12,8)); Advanced->SetStyle(AdvancedStyle); Panel->AddChildToVerticalBox(Advanced)->SetPadding(FMargin(0,10,0,8)); Advanced->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::AdvancedClicked);
 AdvancedPanel=WidgetTree->ConstructWidget<UVerticalBox>(); Panel->AddChildToVerticalBox(AdvancedPanel);
 Label(WidgetTree,AdvancedPanel,TEXT("LOCAL NETWORK / LAN"),12,Blue);
 Label(WidgetTree,AdvancedPanel,TEXT("Same Wi-Fi or wired network. Works without internet."),12,Dim);
 auto* LANActions=WidgetTree->ConstructWidget<UHorizontalBox>(); AdvancedPanel->AddChildToVerticalBox(LANActions)->SetPadding(FMargin(0,8,0,14));
 HostLANButton=Button(WidgetTree,TEXT("HOST LAN")); FindLANButton=Button(WidgetTree,TEXT("FIND NEARBY"));
 LANActions->AddChildToHorizontalBox(HostLANButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); auto* LANFindSlot=LANActions->AddChildToHorizontalBox(FindLANButton); LANFindSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LANFindSlot->SetPadding(FMargin(8,0,0,0));
 HostLANButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::HostLANClicked); FindLANButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::FindLANClicked);
 Label(WidgetTree,AdvancedPanel,TEXT("DIRECT CONNECTION"),12,Dim); AddressEntry=Entry(WidgetTree,TEXT(""),UCitixSessionSubsystem::LocalAddress()+TEXT(":7777")); AdvancedPanel->AddChildToVerticalBox(AddressEntry)->SetPadding(FMargin(0,0,0,8));
 Label(WidgetTree,AdvancedPanel,TEXT("Your friend's address to join; this PC's address to host."),12,Dim);
 auto* DirectActions=WidgetTree->ConstructWidget<UHorizontalBox>(); AdvancedPanel->AddChildToVerticalBox(DirectActions)->SetPadding(FMargin(0,0,0,12));
 JoinButton=Button(WidgetTree,TEXT("JOIN IP")); HostAddressButton=Button(WidgetTree,TEXT("HOST IP"));
 DirectActions->AddChildToHorizontalBox(JoinButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); auto* DirectHostSlot=DirectActions->AddChildToHorizontalBox(HostAddressButton); DirectHostSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DirectHostSlot->SetPadding(FMargin(8,0,0,0));
 HostAddressButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::HostAddressClicked); JoinButton->OnClicked.AddDynamic(this,&UCitixChaseLobbyWidget::JoinClicked); AdvancedPanel->SetVisibility(ESlateVisibility::Collapsed);
 Label(WidgetTree,Panel,TEXT("ESC  SETTINGS"),11,Dim);
 RefreshRooms(); UE_LOG(LogCitix,Log,TEXT("[CitixLobby] online lobby displayed; LAN available in Advanced.")); return Super::RebuildWidget();
}
void UCitixChaseLobbyWidget::RefreshRooms()
{
 auto* M=Manager(); if(!M || !RoomsBox)return; SeenRevision=M->ResultsRevision; RoomsBox->ClearChildren();
 for(int32 I=0;I<M->Rooms.Num();++I) { auto* Row=CreateWidget<UCitixNearbyRoomWidget>(GetOwningPlayer(),UCitixNearbyRoomWidget::StaticClass()); Row->Room=M->Rooms[I]; Row->RoomIndex=I; RoomsBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,8)); }
}
void UCitixChaseLobbyWidget::NativeTick(const FGeometry& G,float Dt)
{
 Super::NativeTick(G,Dt); auto* M=Manager(); const auto* S=GetWorld()->GetGameState<ACitixChaseGameState>();
 const auto* Local=GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 if(!M || !S || !HostButton)return;
 if(SeenRevision!=M->ResultsRevision)RefreshRooms();
 const bool Busy=M->IsBusy(), Connected=M->IsInRoom();
 AdvancedToggle->SetVisibility(Connected ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
 AdvancedPanel->SetVisibility(!Connected && bAdvanced ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 MainScroll->SetScrollBarVisibility(!Connected && bAdvanced ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 HostLANButton->SetIsEnabled(!Busy && !Connected); FindLANButton->SetIsEnabled(!Busy && !Connected);
 RoomType->SetText(FText::FromString(M->IsOnline() ? TEXT("ONLINE ROOM") : TEXT("LAN ROOM")));
 ResultsLabel->SetVisibility(M->Rooms.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
 ResultsLabel->SetText(FText::FromString(M->IsOnline() ? TEXT("ONLINE GAMES") : TEXT("NEARBY LAN GAMES")));
 for(auto* Child:RoomsBox->GetAllChildren()) if(auto* Row=Cast<UCitixNearbyRoomWidget>(Child)) if(Row->JoinButton) Row->JoinButton->SetIsEnabled(!Busy && Row->Room.CanJoin());
 BrowserPanel->SetVisibility(Connected ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); ConnectedPanel->SetVisibility(Connected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 HostButton->SetIsEnabled(!Busy && !Connected); FindButton->SetIsEnabled(!Busy && !Connected); RoomEntry->SetIsReadOnly(Busy); JoinButton->SetIsEnabled(!Busy && !Connected); HostAddressButton->SetIsEnabled(!Busy && !Connected); AddressEntry->SetIsReadOnly(Busy);
 RetryButton->SetVisibility(M->State==ECitixConnectionState::Error ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
 CancelButton->SetVisibility(Busy || M->State==ECitixConnectionState::Error ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); LeaveButton->SetIsEnabled(!Busy);
 StatusText->SetColorAndOpacity(M->State==ECitixConnectionState::Error ? FLinearColor(1,.48f,.4f) : Busy ? Blue : Dim);
 const FString Status=Connected ? (S->PlayerArray.Num()==1 ? (M->IsOnline() ? FString(TEXT("Waiting for your friend. They can find this room in Online Games.")) : M->bManualHost ? FString::Printf(TEXT("Waiting for your friend. Open Advanced, enter %s, then Join IP."),*M->HostAddress) : FString(TEXT("Waiting for your friend. Open Advanced, then Find Nearby Games."))) : S->StatusText) : M->Message;
 StatusText->SetVisibility(ESlateVisibility::Visible); StatusText->SetText(FText::FromString(Status)); RoomTitle->SetText(FText::FromString(S->RoomDisplayName.IsEmpty() ? M->RoomName : S->RoomDisplayName));
 EnterLobbyButton->SetIsEnabled(Connected && S->PlayerArray.Num()==2 && Local && !Local->bReady && Local->bCityIdentityValid && S->LayoutSpawnLocations.Num()==2);
 ReadyLabel->SetText(FText::FromString(Local && Local->bReady ? TEXT("READY — WAITING") : S->Phase==ECitixChasePhase::MatchResults ? TEXT("READY FOR REMATCH") : TEXT("READY")));
 UTextBlock* Slots[]={DriverOne,DriverTwo}; for(int I=0;I<2;++I) { const auto* PS=S->PlayerArray.IsValidIndex(I) ? Cast<ACitixChasePlayerState>(S->PlayerArray[I]) : nullptr; Slots[I]->SetText(FText::FromString(FString::Printf(TEXT("0%d / %s"),I+1,PS ? PS->bReady ? TEXT("READY") : PS->bCityIdentityValid ? TEXT("CONNECTED") : TEXT("VERIFYING CITY") : TEXT("OPEN SLOT")))); Slots[I]->SetColorAndOpacity(PS && PS->bReady ? Blue : Dim); }
 TickUIProbe();
}
void UCitixChaseLobbyWidget::HostClicked() { if(auto* M=Manager()){bTestClicked=true; M->HostRoom(RoomEntry->GetText().ToString());} }
void UCitixChaseLobbyWidget::HostAddressClicked() { if(auto* M=Manager()) {bTestClicked=true; M->HostAtAddress(RoomEntry->GetText().ToString(),AddressEntry->GetText().ToString());} }
void UCitixChaseLobbyWidget::FindClicked() { if(auto* M=Manager()){bTestClicked=true; M->FindRooms();} }
void UCitixChaseLobbyWidget::HostLANClicked() { if(auto* M=Manager()){bTestClicked=true; M->HostLANRoom(RoomEntry->GetText().ToString());} }
void UCitixChaseLobbyWidget::FindLANClicked() { if(auto* M=Manager()){bTestClicked=true; M->FindLANRooms();} }
void UCitixChaseLobbyWidget::JoinClicked() { if(auto* M=Manager()){bTestClicked=true; M->JoinAddress(AddressEntry->GetText().ToString());} }
void UCitixChaseLobbyWidget::EnterLobbyClicked() { if(auto* PC=Cast<ACitixDrivingPlayerController>(GetOwningPlayer())){bTestClicked=true; PC->ServerChaseInteract();} }
void UCitixChaseLobbyWidget::AdvancedClicked() { bAdvanced=!bAdvanced; AdvancedPanel->SetVisibility(bAdvanced ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); AdvancedLabel->SetText(FText::FromString(bAdvanced ? TEXT("ADVANCED  −") : TEXT("ADVANCED  +"))); }
void UCitixChaseLobbyWidget::RetryClicked(){ if(auto* M=Manager())M->Retry(); }
void UCitixChaseLobbyWidget::CancelClicked(){ if(auto* M=Manager())M->Cancel(); }
void UCitixChaseLobbyWidget::LeaveClicked(){ if(auto* M=Manager())M->LeaveRoom(); }
void UCitixChaseLobbyWidget::TickUIProbe()
{
 const float Now=GetWorld()->GetTimeSeconds(); auto* M=Manager(); if(!M)return;
 FString Tag=TEXT("Host"); FParse::Value(FCommandLine::Get(),TEXT("CitixNetTag="),Tag);
 auto Shot=[&](const TCHAR* Name){ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(FString(Name)+TEXT("-")+Tag+TEXT(".png")),true,false); };
 if(!bTestScreenshot && Now>3 && FParse::Param(FCommandLine::Get(),TEXT("CitixLobbyScreenshot"))){bTestScreenshot=true; if(FParse::Param(FCommandLine::Get(),TEXT("CitixAdvancedScreenshot")))AdvancedClicked(); Shot(TEXT("OnlineLobby"));}
 if(!bTestFailureShot && M->State==ECitixConnectionState::Error && FParse::Param(FCommandLine::Get(),TEXT("CitixLobbyScreenshot"))){bTestFailureShot=true; Shot(TEXT("LANConnectionFailure"));}
 if(!FParse::Param(FCommandLine::Get(),TEXT("CitixLobbyButtons")) || Now<NextTestClick)return;
 NextTestClick=Now+2;
 if(M->IsInRoom()) {
  if(FParse::Param(FCommandLine::Get(),TEXT("CitixLANProbe")) && !bProbeConnectedShot) { bProbeConnectedShot=true; Shot(TEXT("LANRoom")); const FString Receipt=FString::Printf(TEXT("{\"connected\":true,\"room\":\"%s\",\"players\":%d}"),*M->RoomName.Replace(TEXT("\""),TEXT("")),GetWorld()->GetGameState<ACitixChaseGameState>()->PlayerArray.Num()); FFileHelper::SaveStringToFile(Receipt,*(FPaths::ProjectSavedDir()/(TEXT("LANConnected-")+Tag+TEXT(".json")))); }
  ClickByKeyboard(EnterLobbyButton,GetWorld()); return;
 }
 if(M->IsBusy() || M->State==ECitixConnectionState::Error)return;
 FString Address; const bool Guest=FParse::Value(FCommandLine::Get(),TEXT("CitixLobbyJoin="),Address);
 if(!Guest){
  FString ManualAddress;
  if(FParse::Value(FCommandLine::Get(),TEXT("CitixManualHost="),ManualAddress)) {
   if(!bAdvanced) { AdvancedClicked(); AddressEntry->SetText(FText::FromString(ManualAddress)); Shot(TEXT("AdvancedIP")); NextTestClick=Now+2; return; }
   if(!bTestClicked)ClickByKeyboard(HostAddressButton,GetWorld());
  } else if(!bTestClicked) { if(FParse::Param(FCommandLine::Get(),TEXT("CitixLANProbe"))) { if(!bAdvanced)AdvancedClicked(); ClickByKeyboard(HostLANButton,GetWorld()); } else ClickByKeyboard(HostButton,GetWorld()); }
  return;
 }
 if(FParse::Param(FCommandLine::Get(),TEXT("CitixLANProbe"))) {
  if(!bAdvanced)AdvancedClicked();
  if(!bTestClicked){ClickByKeyboard(FindLANButton,GetWorld());return;}
  if(!bProbeListShot && !M->Rooms.IsEmpty()){bProbeListShot=true; Shot(TEXT("LANNearbyGames")); NextTestClick=Now+2;return;}
  for(int32 I=0;I<M->Rooms.Num();++I) if(M->Rooms[I].CanJoin()){ if(auto* Row=Cast<UCitixNearbyRoomWidget>(RoomsBox->GetChildAt(I))) ClickByKeyboard(Row->JoinButton,GetWorld()); return; }
  if(M->Rooms.IsEmpty() && Now>15)ClickByKeyboard(FindLANButton,GetWorld());
 } else {
  if(!bAdvanced)AdvancedClicked(); AddressEntry->SetText(FText::FromString(Address)); if(!bTestClicked)ClickByKeyboard(JoinButton,GetWorld());
 }
}
