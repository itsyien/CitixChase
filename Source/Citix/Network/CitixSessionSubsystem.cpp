#include "Network/CitixSessionSubsystem.h"
#include "Network/CitixEOSOnline.h"
#include "Network/CitixOnlineProbe.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "OnlineSubsystem.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/NetworkVersion.h"
#include "Misc/CommandLine.h"
#include "Chase/CitixChaseGameState.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectGlobals.h"
#include "Citix.h"

namespace {
 const FName GameKey(TEXT("CITIX_GAME")), NameKey(TEXT("CITIX_ROOM")), VersionKey(TEXT("CITIX_PROTOCOL")), NetworkKey(TEXT("CITIX_NETWORK")), PlayersKey(TEXT("CITIX_PLAYERS")), ProgressKey(TEXT("CITIX_PLAYING"));
 const TCHAR* LobbyMapPath=TEXT("/Engine/Maps/Templates/Template_Default");
}
uint32 UCitixSessionSubsystem::NetworkVersion() { return FNetworkVersion::GetLocalNetworkVersion(); }
FString UCitixSessionSubsystem::DefaultRoomName()
{
 return TEXT("CitixRoom1");
}
FString UCitixSessionSubsystem::NormalizeRoomName(const FString& Name)
{
 FString Clean;
 for(TCHAR C:Name.TrimStartAndEnd()) if(!FChar::IsControl(C) && C!=TEXT('?') && C!=TEXT('|') && C!=TEXT('/')) Clean.AppendChar(C);
 Clean=Clean.TrimStartAndEnd().Left(32); return Clean.IsEmpty() ? TEXT("CitixRoom1") : Clean;
}
bool UCitixSessionSubsystem::ValidateAddress(const FString& Address)
{
 FString Host,Port; if(!Address.Split(TEXT(":"),&Host,&Port)) Host=Address;
 if(Host.IsEmpty() || Host.Len()>253) return false;
 for(TCHAR C:Host) if(!FChar::IsAlnum(C) && C!=TEXT('.') && C!=TEXT('-')) return false;
 if(Address.Contains(TEXT(":"))) { if(Port.IsEmpty() || !Port.IsNumeric()) return false; const int32 P=FCString::Atoi(*Port); if(P<1 || P>65535) return false; }
 return true;
}
FCitixNearbyRoom UCitixSessionSubsystem::DescribeRoom(const FOnlineSessionSearchResult& Result)
{
 FCitixNearbyRoom Room; const auto& Settings=Result.Session.SessionSettings;
 Settings.Get(NameKey,Room.Name); Room.Name=NormalizeRoomName(Room.Name); Room.Id=Result.GetSessionIdStr();
 Room.Capacity=FMath::Clamp(Settings.NumPublicConnections,1,2);
 if(!Settings.Get(PlayersKey,Room.Players)) Room.Players=Room.Capacity-Result.Session.NumOpenPublicConnections;
 Room.Players=FMath::Clamp(Room.Players,0,Room.Capacity);
 int32 Protocol=0; FString Build; Settings.Get(VersionKey,Protocol); Settings.Get(NetworkKey,Build);
 Room.bCompatible=Protocol==ProtocolVersion && Build==FString::Printf(TEXT("%u"),NetworkVersion());
 Settings.Get(ProgressKey,Room.bInProgress); return Room;
}
FString UCitixSessionSubsystem::AdmissionError(const FString& Options,int32 Players,bool InProgress)
{
 if(Players>=2) return TEXT("Room full — two drivers are already connected.");
 if(InProgress) return TEXT("Match in progress — wait for the host to return to the lobby.");
 const FString Protocol=UGameplayStatics::ParseOption(Options,TEXT("CitixProtocol"));
 const FString Build=UGameplayStatics::ParseOption(Options,TEXT("CitixBuild"));
 // Legacy development startup URLs still work; normal Join/Advanced always sends both fields.
 if((!Protocol.IsEmpty() || !Build.IsEmpty()) && (Protocol!=FString::FromInt(ProtocolVersion) || Build!=FString::Printf(TEXT("%u"),NetworkVersion()))) return TEXT("Different game version — both players need the same build.");
 return FString();
}
FString UCitixSessionSubsystem::ExplainFailure(ENetworkFailure::Type Type,const FString& Error)
{
 if(Type==ENetworkFailure::OutdatedClient || Type==ENetworkFailure::OutdatedServer || Error.Contains(TEXT("version"),ESearchCase::IgnoreCase) || Error.Contains(TEXT("incompatible"),ESearchCase::IgnoreCase)) return TEXT("Different game version — both players need the same build.");
 if(Error.Contains(TEXT("full"),ESearchCase::IgnoreCase)) return TEXT("Room full — choose another room or ask your friend to free a slot.");
 if(Error.Contains(TEXT("progress"),ESearchCase::IgnoreCase)) return TEXT("Match in progress — try again when the host returns to the lobby.");
 if(Type==ENetworkFailure::ConnectionLost) return TEXT("Connection lost — the host left or the network disconnected. Try again.");
 if(Type==ENetworkFailure::ConnectionTimeout) return TEXT("The host did not respond. Check that both players are on the same network, then retry.");
 return TEXT("Could not connect. Check the host is still open and both players use the same network, then retry.");
}
void UCitixSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 if(auto* OSS=IOnlineSubsystem::Get(TEXT("NULL"))) Sessions=OSS->GetSessionInterface();
 if(GEngine) { NetworkHandle=GEngine->OnNetworkFailure().AddUObject(this,&UCitixSessionSubsystem::NetworkFailed); TravelHandle=GEngine->OnTravelFailure().AddUObject(this,&UCitixSessionSubsystem::TravelFailed); }
 TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UCitixSessionSubsystem::Tick));
}
void UCitixSessionSubsystem::Deinitialize()
{
 ++OnlineGeneration;
 if(Online) Online->Leave();
 Online.Reset();
 FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); ClearOperationDelegates();
 if(Sessions.IsValid()) { if(Search.IsValid() && Search->SearchState==EOnlineAsyncTaskState::InProgress) Sessions->CancelFindSessions(); if(Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession); }
 if(GEngine) { GEngine->OnNetworkFailure().Remove(NetworkHandle); GEngine->OnTravelFailure().Remove(TravelHandle); }
 Sessions.Reset(); Super::Deinitialize();
}
bool UCitixSessionSubsystem::IsBusy() const { return State==ECitixConnectionState::Creating || State==ECitixConnectionState::Searching || State==ECitixConnectionState::Joining || State==ECitixConnectionState::Connecting || State==ECitixConnectionState::Leaving; }
void UCitixSessionSubsystem::SetState(ECitixConnectionState NewState,const FString& Text,float Timeout)
{
 State=NewState; Message=Text; Deadline=Timeout>0 ? FPlatformTime::Seconds()+Timeout : 0;
 UE_LOG(LogCitix,Log,TEXT("[CitixLAN] state=%d %s"),static_cast<int32>(State),*Message);
}
void UCitixSessionSubsystem::ClearOperationDelegates()
{
 if(!Sessions.IsValid()) return;
 Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle); CreateHandle.Reset();
 Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle); FindHandle.Reset();
 Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle); JoinHandle.Reset();
 Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle); DestroyHandle.Reset();
}
void UCitixSessionSubsystem::HostRoom(const FString& Name)
{
 if(IsBusy() || IsInRoom()) return;
 bOnline=true;
 bManualHost=false; ManualHostPort=0; HostAddress.Empty();
 StartHost(Name);
}
void UCitixSessionSubsystem::HostLANRoom(const FString& Name)
{
 if(IsBusy() || IsInRoom()) return;
 bOnline=false; bManualHost=false; ManualHostPort=0; HostAddress.Empty(); StartHost(Name);
}
void UCitixSessionSubsystem::FindLANRooms()
{
 if(IsBusy() || IsInRoom()) return;
 bOnline=false; StartFind();
}
void UCitixSessionSubsystem::AuthenticateOnline(TFunction<void()> Next)
{
 if(!Online) Online=MakeShared<FCitixEOSOnline,ESPMode::ThreadSafe>();
 const uint32 Generation=++OnlineGeneration;
 SetState(ECitixConnectionState::Connecting,TEXT("Connecting online automatically…"),60);
 TWeakObjectPtr<UCitixSessionSubsystem> Weak(this);
 Online->Authenticate([Weak,Generation,Next=MoveTemp(Next)](bool Success,const FString& Error) mutable
 {
  auto* Self=Weak.Get(); if(!Self || Self->OnlineGeneration!=Generation) return;
  if(!Success) { Self->Fail(TEXT("Could not connect online. ")+Error+TEXT(" Retry, or use LAN in Advanced.")); return; }
  Next();
 });
}
FString UCitixSessionSubsystem::LocalAddress()
{
 TArray<TSharedPtr<FInternetAddr>> Addresses;
 if(auto* SocketSystem=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) SocketSystem->GetLocalAdapterAddresses(Addresses);
 for(const auto& Address:Addresses) if(Address.IsValid() && !Address->ToString(false).StartsWith(TEXT("127.")) && !Address->ToString(false).Contains(TEXT(":"))) return Address->ToString(false);
 return TEXT("127.0.0.1");
}
void UCitixSessionSubsystem::HostAtAddress(const FString& Name,const FString& Address)
{
 if(IsBusy() || IsInRoom()) return;
 bOnline=false;
 FString Entered=Address.TrimStartAndEnd(); if(Entered.IsEmpty()) Entered=LocalAddress()+TEXT(":7777");
 if(!ValidateAddress(Entered)) { Fail(TEXT("Enter this PC's local IPv4 address and optional port to host.")); return; }
 FString Host,Port; if(!Entered.Split(TEXT(":"),&Host,&Port)) Host=Entered;
 bool Local=Host==TEXT("127.0.0.1") || Host==TEXT("localhost") || Host==TEXT("0.0.0.0");
 TArray<TSharedPtr<FInternetAddr>> Addresses;
 if(auto* SocketSystem=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) SocketSystem->GetLocalAdapterAddresses(Addresses);
 for(const auto& Adapter:Addresses) if(Adapter.IsValid() && Adapter->ToString(false)==Host) Local=true;
 if(!Local) { Fail(TEXT("That address belongs to another computer. Use Join, or enter this PC's address to host.")); return; }
 bManualHost=true; ManualHostPort=Port.IsEmpty() ? 7777 : FCString::Atoi(*Port);
 HostAddress=(Host==TEXT("0.0.0.0") ? LocalAddress() : Host)+FString::Printf(TEXT(":%d"),ManualHostPort);
 StartHost(Name);
}
void UCitixSessionSubsystem::StartHost(const FString& Name)
{
 if(IsBusy() || IsInRoom()) return;
 bPreserveError=false;
 LastAction=EAction::Host; LastArgument=NormalizeRoomName(Name); RoomName=LastArgument;
 if(bOnline)
 {
  Rooms.Reset(); ++ResultsRevision; bHosting=true;
  TWeakObjectPtr<UCitixSessionSubsystem> Weak(this);
  AuthenticateOnline([Weak]()
  {
   auto* Self=Weak.Get(); if(!Self) return;
   const uint32 Generation=Self->OnlineGeneration;
   Self->SetState(ECitixConnectionState::Creating,TEXT("Creating your online room…"),30);
   Self->Online->Host(Self->RoomName,[Weak,Generation](bool Success,const FString& Error)
   {
    auto* Self=Weak.Get(); if(!Self || Self->OnlineGeneration!=Generation) return;
    if(!Success) { Self->Fail(TEXT("Could not host online. ")+Error); return; }
    Self->SetState(ECitixConnectionState::Connecting,TEXT("Opening your online room…"),30);
    if(!Self->GetWorld() || !Self->GetWorld()->ServerTravel(FString(LobbyMapPath)+TEXT("?listen?CitixOnline"),true)) Self->Fail(TEXT("Could not open the online room. Retry."));
   });
  });
  return;
 }
 if(!Sessions.IsValid()) { Fail(TEXT("LAN networking is unavailable. Restart the game and try again.")); return; }
 if(Sessions->GetNamedSession(NAME_GameSession)) { DestroyRoom(); return; }
 Rooms.Reset(); ++ResultsRevision; bHosting=true;
 FOnlineSessionSettings Settings; Settings.bIsLANMatch=true; Settings.bShouldAdvertise=!bManualHost; Settings.bAllowJoinInProgress=true; Settings.NumPublicConnections=2; Settings.NumPrivateConnections=0;
 Settings.Set(GameKey,FString(TEXT("CitixChase")),EOnlineDataAdvertisementType::ViaOnlineService);
 Settings.Set(NameKey,RoomName,EOnlineDataAdvertisementType::ViaOnlineService);
 int32 Version=ProtocolVersion; FParse::Value(FCommandLine::Get(),TEXT("CitixLANVersion="),Version);
 Settings.Set(VersionKey,Version,EOnlineDataAdvertisementType::ViaOnlineService);
 Settings.Set(NetworkKey,FString::Printf(TEXT("%u"),NetworkVersion()),EOnlineDataAdvertisementType::ViaOnlineService);
 Settings.Set(PlayersKey,1,EOnlineDataAdvertisementType::ViaOnlineService); Settings.Set(ProgressKey,false,EOnlineDataAdvertisementType::ViaOnlineService);
 SetState(ECitixConnectionState::Creating,TEXT("Creating your nearby room…"),10);
 CreateHandle=Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this,&UCitixSessionSubsystem::OnCreated));
 if(!Sessions->CreateSession(0,NAME_GameSession,Settings) && State==ECitixConnectionState::Creating) Fail(TEXT("Could not host a room. Check network access and retry."));
}
void UCitixSessionSubsystem::OnCreated(FName Name,bool Success)
{
 if(State!=ECitixConnectionState::Creating || Name!=NAME_GameSession) return;
 ClearOperationDelegates();
 if(!Success) { Fail(TEXT("Could not host a room. Check network access and retry.")); return; }
 SetState(ECitixConnectionState::Connecting,TEXT("Opening your room…"),30);
 if(GetWorld()) {
  // Relative map travel inherits the chosen local listen port without a host URL.
  if(ManualHostPort>0) { GetWorld()->URL.Port=ManualHostPort; if(GEngine) GEngine->GetWorldContextFromWorldChecked(GetWorld()).LastURL.Port=ManualHostPort; }
  if(!GetWorld()->ServerTravel(FString(LobbyMapPath)+TEXT("?listen?bIsLanMatch"))) Fail(TEXT("Could not open the room. Retry from the lobby."));
 }
 else Fail(TEXT("Could not open the room. Retry from the lobby."));
}
void UCitixSessionSubsystem::FindRooms()
{
 if(IsBusy() || IsInRoom()) return;
 bOnline=true; StartFind();
}
void UCitixSessionSubsystem::StartFind()
{
 if(IsBusy() || IsInRoom()) return;
 bPreserveError=false;
 LastAction=EAction::Find;
 if(bOnline)
 {
  bHosting=false; Rooms.Reset(); ++ResultsRevision;
  TWeakObjectPtr<UCitixSessionSubsystem> Weak(this);
  AuthenticateOnline([Weak]()
  {
   auto* Self=Weak.Get(); if(!Self) return;
   const uint32 Generation=Self->OnlineGeneration;
   Self->SetState(ECitixConnectionState::Searching,TEXT("Searching online games…"),30);
   Self->Online->Find([Weak,Generation](bool Success,const FString& Error)
   {
    auto* Self=Weak.Get(); if(!Self || Self->OnlineGeneration!=Generation) return;
    if(!Success) { Self->Fail(TEXT("Online search failed. ")+Error); return; }
    Self->Rooms=Self->Online->Rooms; ++Self->ResultsRevision;
    Self->SetState(ECitixConnectionState::Idle,Self->Rooms.IsEmpty() ? TEXT("No online games yet. Ask your friend to host, then refresh.") : TEXT("Choose your friend's online room to join."));
   });
  });
  return;
 }
 if(!Sessions.IsValid()) { Fail(TEXT("LAN networking is unavailable. Restart the game and try again.")); return; }
 if(Sessions->GetNamedSession(NAME_GameSession)) { DestroyRoom(); return; }
 bHosting=false; Rooms.Reset(); ++ResultsRevision;
 Search=MakeShared<FOnlineSessionSearch>(); Search->bIsLanQuery=true; Search->MaxSearchResults=50; Search->TimeoutInSeconds=5;
 SetState(ECitixConnectionState::Searching,TEXT("Searching your nearby network…"),12);
 FindHandle=Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this,&UCitixSessionSubsystem::OnFound));
 if(!Sessions->FindSessions(0,Search.ToSharedRef()) && State==ECitixConnectionState::Searching) Fail(TEXT("Could not search. Check Wi-Fi or wired network access and retry."));
}
void UCitixSessionSubsystem::OnFound(bool Success)
{
 if(State!=ECitixConnectionState::Searching) return;
 ClearOperationDelegates();
 if(!Success || !Search.IsValid()) { Fail(TEXT("Search failed. Check your local network and retry.")); return; }
 TArray<FOnlineSessionSearchResult> Filtered;
 for(const auto& Result:Search->SearchResults) { FString Game; if(Result.Session.SessionSettings.Get(GameKey,Game) && Game==TEXT("CitixChase")) Filtered.Add(Result); }
 Filtered.Sort([](const auto& A,const auto& B){ return DescribeRoom(A).Name<DescribeRoom(B).Name; });
 Search->SearchResults=MoveTemp(Filtered); Rooms.Reset();
 for(const auto& Result:Search->SearchResults) Rooms.Add(DescribeRoom(Result));
 ++ResultsRevision;
 SetState(ECitixConnectionState::Idle,Rooms.IsEmpty() ? TEXT("No nearby games yet. Ask your friend to click Host Game, then refresh. Both players need the same local network.") : TEXT("Choose your friend's room to join."));
 UE_LOG(LogCitix,Log,TEXT("[CitixLAN] found=%d"),Rooms.Num());
}
void UCitixSessionSubsystem::JoinRoom(int32 Index)
{
 if(IsBusy() || IsInRoom()) return;
 bPreserveError=false;
 if(bOnline)
 {
  if(!Rooms.IsValidIndex(Index) || !Online) { Fail(TEXT("That room is no longer available. Refresh online games.")); return; }
  const auto Room=Rooms[Index]; LastAction=EAction::Join; LastRoomId=Room.Id; RoomName=Room.Name; bHosting=false;
  if(!Room.CanJoin()) { Fail(!Room.bCompatible ? TEXT("Different game version — both players need the same build.") : Room.bInProgress ? TEXT("Match in progress — wait for the host to return to the lobby.") : TEXT("Room full — two drivers are already connected.")); return; }
  const uint32 Generation=++OnlineGeneration;
  SetState(ECitixConnectionState::Joining,FString::Printf(TEXT("Joining %s online…"),*Room.Name),30);
  TWeakObjectPtr<UCitixSessionSubsystem> Weak(this);
  Online->Join(Index,[Weak,Generation](bool Success,const FString& Address)
  {
   auto* Self=Weak.Get(); if(!Self || Self->OnlineGeneration!=Generation) return;
   if(!Success) { Self->Fail(Address); return; }
   Self->TravelTo(Address);
  });
  return;
 }
 if(!Rooms.IsValidIndex(Index) || !Search.IsValid() || !Search->SearchResults.IsValidIndex(Index)) { Fail(TEXT("That room is no longer available. Refresh nearby games.")); return; }
 const auto Room=Rooms[Index];
 LastAction=EAction::Join; LastRoomId=Room.Id; RoomName=Room.Name;
 if(!Room.bCompatible) { Fail(TEXT("Different game version — both players need the same build.")); return; }
 if(Room.Players>=Room.Capacity) { Fail(TEXT("Room full — two drivers are already connected.")); return; }
 if(Room.bInProgress) { Fail(TEXT("Match in progress — try again when your friend returns to the lobby.")); return; }
 if(!Sessions.IsValid()) { Fail(TEXT("LAN networking is unavailable. Restart the game.")); return; }
 bHosting=false;
 SetState(ECitixConnectionState::Joining,FString::Printf(TEXT("Joining %s…"),*Room.Name),12);
 JoinHandle=Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this,&UCitixSessionSubsystem::OnJoined));
 if(!Sessions->JoinSession(0,NAME_GameSession,Search->SearchResults[Index]) && State==ECitixConnectionState::Joining) Fail(TEXT("That room could not be joined. Refresh nearby games and retry."));
}
void UCitixSessionSubsystem::OnJoined(FName Name,EOnJoinSessionCompleteResult::Type Result)
{
 if(State!=ECitixConnectionState::Joining || Name!=NAME_GameSession) return;
 ClearOperationDelegates();
 if(Result!=EOnJoinSessionCompleteResult::Success) { Fail(Result==EOnJoinSessionCompleteResult::SessionIsFull ? TEXT("Room full — choose another room or ask your friend to free a slot.") : TEXT("That room is no longer available. Refresh nearby games and retry.")); return; }
 FString Address;
 if(!Sessions->GetResolvedConnectString(NAME_GameSession,Address) || Address.IsEmpty()) { Fail(TEXT("Could not resolve the room. Refresh nearby games and retry.")); return; }
 TravelTo(Address);
}
void UCitixSessionSubsystem::JoinAddress(const FString& Address)
{
 if(IsBusy() || IsInRoom()) return;
 bOnline=false;
 bPreserveError=false;
 LastAction=EAction::Address; LastArgument=Address.TrimStartAndEnd();
 if(!ValidateAddress(LastArgument)) { Fail(TEXT("Enter a host address such as 192.168.1.20 or 192.168.1.20:7777.")); return; }
 if(Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession)) { DestroyRoom(); return; }
 RoomName=TEXT("Friend's Room"); bHosting=false; TravelTo(LastArgument);
}
void UCitixSessionSubsystem::TravelTo(const FString& Address)
{
 SetState(ECitixConnectionState::Connecting,FString::Printf(TEXT("Connecting to %s…"),*RoomName),25);
 auto* PC=GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
 if(!PC) { Fail(TEXT("Could not connect from this screen. Return to the lobby and retry.")); return; }
 PC->ClientTravel(Address+FString::Printf(TEXT("?CitixProtocol=%d?CitixBuild=%u"),ProtocolVersion,NetworkVersion()),TRAVEL_Absolute);
}
void UCitixSessionSubsystem::Fail(const FString& Reason)
{
 ++OnlineGeneration;
 if(Online) Online->Leave();
 ClearOperationDelegates();
 if(Search.IsValid() && Search->SearchState==EOnlineAsyncTaskState::InProgress && Sessions.IsValid()) Sessions->CancelFindSessions();
 // Failure callbacks run inside the driver's Tick. Defer destroying that driver.
 bCancelPending=true;
 if(GetWorld()) GetWorld()->NextURL.Empty();
 Rooms.Reset(); ++ResultsRevision;
 bHosting=false; bPreserveError=true;
 if(GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone) bReturnToMenu=true;
 SetState(ECitixConnectionState::Error,Reason);
 if(Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
}
void UCitixSessionSubsystem::Cancel()
{
 if(IsInRoom()) { LeaveRoom(); return; }
 ++OnlineGeneration;
 if(Online) Online->Leave();
 ClearOperationDelegates();
 if(Search.IsValid() && Search->SearchState==EOnlineAsyncTaskState::InProgress && Sessions.IsValid()) Sessions->CancelFindSessions();
 Search.Reset(); Rooms.Reset(); ++ResultsRevision;
 if(GEngine && GetWorld()) { GEngine->CancelPending(GetWorld()); GetWorld()->NextURL.Empty(); }
 bHosting=false; bPreserveError=false;
 // Cancelling a travel or leave must also detach an already opened network world.
 if(GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone) bReturnToMenu=true;
 if(Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
 SetState(ECitixConnectionState::Idle,TEXT("Cancelled. Host or find an online game, or use LAN in Advanced."));
}
void UCitixSessionSubsystem::Retry()
{
 if(IsBusy() || IsInRoom()) return;
 bPreserveError=false;
 switch(LastAction) {
 case EAction::Host: if(bManualHost) HostAtAddress(LastArgument,HostAddress); else if(bOnline) HostRoom(LastArgument); else HostLANRoom(LastArgument); break;
 case EAction::Address: JoinAddress(LastArgument); break;
 default: if(bOnline) FindRooms(); else FindLANRooms(); break;
 }
}
void UCitixSessionSubsystem::DestroyRoom()
{
 SetState(ECitixConnectionState::Leaving,TEXT("Closing the previous room…"),8);
 DestroyHandle=Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this,&UCitixSessionSubsystem::OnDestroyed));
 if(!Sessions->DestroySession(NAME_GameSession)) { ClearOperationDelegates(); Fail(TEXT("Could not close the previous room. Restart the game before retrying.")); }
}
void UCitixSessionSubsystem::OnDestroyed(FName Name,bool Success)
{
 if(State!=ECitixConnectionState::Leaving || Name!=NAME_GameSession) return;
 ClearOperationDelegates();
 if(!Success) { Fail(TEXT("Could not leave the room. Retry or restart the game.")); return; }
 SetState(ECitixConnectionState::Idle,TEXT("Room closed."));
 if(bReturnToMenu) return;
 Retry();
}
void UCitixSessionSubsystem::LeaveRoom()
{
 if(!IsInRoom()) { Cancel(); return; }
 LastAction=EAction::None; bHosting=false; bReturnToMenu=true; bPreserveError=false; RoomName.Empty();
 if(bOnline && Online)
 {
  ++OnlineGeneration; SetState(ECitixConnectionState::Leaving,TEXT("Leaving online room…"),20);
  TWeakObjectPtr<UCitixSessionSubsystem> Weak(this);
  Online->Leave([Weak](bool Success,const FString& Error) { if(auto* Self=Weak.Get()) { if(!Success && !Self->Online->IsWorking()) Self->Fail(TEXT("Online cleanup failed. ")+Error); } });
  return;
 }
 if(Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession)) DestroyRoom();
 else SetState(ECitixConnectionState::Leaving,TEXT("Leaving room…"));
}
void UCitixSessionSubsystem::NotifyWorldReady()
{
 if(!GetWorld() || bPreserveError || bReturnToMenu) return;
 if(GetWorld()->GetNetMode()!=NM_Standalone && (State==ECitixConnectionState::Connecting || State==ECitixConnectionState::Idle)) {
  bHosting=GetWorld()->GetNetMode()==NM_ListenServer;
  bOnline=GetWorld()->URL.HasOption(TEXT("CitixOnline")) || (GetWorld()->GetNetDriver() && GetWorld()->GetNetDriver()->ServerConnection && GetWorld()->GetNetDriver()->ServerConnection->URL.Host.StartsWith(TEXT("EOS:")));
  if(RoomName.IsEmpty()) RoomName=bOnline ? TEXT("Online Room") : TEXT("Nearby Room");
  SetState(ECitixConnectionState::InRoom,TEXT("Connected. Both drivers press Ready to begin.")); AdvertisedPlayers=-1; UpdateOccupancy();
 }
}
void UCitixSessionSubsystem::UpdateOccupancy()
{
 if(bHosting && bOnline && Online && GetWorld())
 {
  auto* Current=GetWorld()->GetGameState<ACitixChaseGameState>(); if(!Current) return;
  Current->RoomDisplayName=RoomName;
  TArray<FString> Users;
  if(auto* Driver=GetWorld()->GetNetDriver()) for(UNetConnection* Connection:Driver->ClientConnections) if(Connection && Connection->PlayerController && Connection->GetRemoteAddr().IsValid())
  { const FString Address=Connection->GetRemoteAddr()->ToString(false); if(FCitixEOSOnline::ValidateTransportAddress(Address)) Users.Add(Address.Mid(4)); }
  Online->Update(RoomName,FMath::Clamp(Current->PlayerArray.Num(),1,2),Current->Phase!=ECitixChasePhase::Waiting && Current->Phase!=ECitixChasePhase::MatchResults,Users);
  return;
 }
 if(!bHosting || !Sessions.IsValid() || !GetWorld()) return;
 auto* StateNow=GetWorld()->GetGameState<ACitixChaseGameState>(); auto* Session=Sessions->GetNamedSession(NAME_GameSession);
 if(!StateNow || !Session) return;
 StateNow->RoomDisplayName=RoomName;
 const int32 Players=FMath::Clamp(StateNow->PlayerArray.Num(),1,2);
 const bool Playing=StateNow->Phase!=ECitixChasePhase::Waiting && StateNow->Phase!=ECitixChasePhase::MatchResults;
 if(Players==AdvertisedPlayers && Playing==bAdvertisedInProgress) return;
 AdvertisedPlayers=Players; bAdvertisedInProgress=Playing;
 auto Settings=Session->SessionSettings; Settings.Set(PlayersKey,Players,EOnlineDataAdvertisementType::ViaOnlineService); Settings.Set(ProgressKey,Playing,EOnlineDataAdvertisementType::ViaOnlineService);
 Session->NumOpenPublicConnections=FMath::Max(0,2-Players);
 Sessions->UpdateSession(NAME_GameSession,Settings,true);
 UE_LOG(LogCitix,Log,TEXT("[CitixLAN] room=%s players=%d/2 playing=%d"),*RoomName,Players,Playing);
}
void UCitixSessionSubsystem::NetworkFailed(UWorld* World,UNetDriver* Driver,ENetworkFailure::Type Type,const FString& Error)
{
 if(World && World!=GetWorld()) return;
 if(State==ECitixConnectionState::Idle && LastAction==EAction::None) return;
 const bool Specific=Type==ENetworkFailure::OutdatedClient || Type==ENetworkFailure::OutdatedServer || Error.Contains(TEXT("version"),ESearchCase::IgnoreCase) || Error.Contains(TEXT("incompatible"),ESearchCase::IgnoreCase) || Error.Contains(TEXT("full"),ESearchCase::IgnoreCase) || Error.Contains(TEXT("progress"),ESearchCase::IgnoreCase);
 Fail(bOnline && !Specific ? TEXT("Online connection lost or unavailable. Check internet access and that the host is still open, then retry.") : ExplainFailure(Type,Error)); bReturnToMenu=true;
}
void UCitixSessionSubsystem::TravelFailed(UWorld* World,ETravelFailure::Type Type,const FString& Error)
{
 if(World && World!=GetWorld()) return;
 Fail(bOnline ? TEXT("Could not open the online room. Refresh online games or retry.") : TEXT("Could not open the LAN room. Refresh nearby games or retry.")); bReturnToMenu=true;
}
bool UCitixSessionSubsystem::Tick(float Delta)
{
 CitixOnlineProbeTick(this);
 if(Online) Online->Maintain();
 if(bOnline && IsInRoom() && Online && !Online->IsReady()) { Fail(TEXT("Online guest connection expired. Retry to reconnect automatically.")); bReturnToMenu=true; }
 if(Deadline>0 && FPlatformTime::Seconds()>Deadline) { Fail(bOnline ? TEXT("Online connection took too long. Check internet access and retry, or use LAN in Advanced.") : TEXT("This took too long. Check that the host is open on the same network, then retry.")); }
 if(bCancelPending) { bCancelPending=false; if(GEngine && GetWorld()) GEngine->CancelPending(GetWorld()); }
 if(bReturnToMenu) {
  if(bOnline && Online && Online->HasRoom() && Online->IsWorking() && State==ECitixConnectionState::Leaving) return true;
  if(State==ECitixConnectionState::Leaving && Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession)) return true;
  bReturnToMenu=false;
  if(!bPreserveError) SetState(ECitixConnectionState::Idle,TEXT("You left the room. Host or find another game."));
  if(GetWorld()) UGameplayStatics::OpenLevel(GetWorld(),FName(LobbyMapPath));
  return true;
 }
 if(IsInRoom() && FPlatformTime::Seconds()>NextOccupancyCheck) { NextOccupancyCheck=FPlatformTime::Seconds()+1; UpdateOccupancy(); }
 return true;
}
