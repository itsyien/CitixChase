#include "Network/CitixEOSOnline.h"
#include "SocketSubsystemEOS.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "eos_p2p.h"
#include "Citix.h"

namespace
{
 const char* RoomSession="CitixGuestRoom";
 const char* Bucket="CitixChase-v1";
 FString IdString(EOS_ProductUserId Id)
 {
  char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH+1]={}; int32_t Size=sizeof(Buffer);
  return EOS_ProductUserId_ToString(Id,Buffer,&Size)==EOS_EResult::EOS_Success ? UTF8_TO_TCHAR(Buffer) : FString();
 }
}
struct FCitixEOSOnline::FRequest
{
 TWeakPtr<FCitixEOSOnline,ESPMode::ThreadSafe> Owner;
 TFunction<void(FCitixEOSOnline&,EOS_EResult,const FString&)> Function;
 void Finish(EOS_EResult Code,const FString& Id={})
 {
  if(auto Self=Owner.Pin()) Function(*Self,Code,Id);
 }
};
struct FCitixEOSOnline::FUtils : ISocketSubsystemEOSUtils
{
 TWeakPtr<FCitixEOSOnline,ESPMode::ThreadSafe> Owner;
 virtual EOS_ProductUserId GetLocalUserId() override { auto Self=Owner.Pin(); return Self ? Self->User : nullptr; }
 virtual FString GetSessionId() override { auto Self=Owner.Pin(); return Self ? Self->SessionId : FString(); }
 virtual FName GetSubsystemInstanceName() override { return NAME_None; }
 virtual bool IsLoggedIn() override { auto Self=Owner.Pin(); return Self && Self->IsReady(); }
};
FCitixEOSOnline::FRequest* FCitixEOSOnline::Request(TFunction<void(FCitixEOSOnline&,EOS_EResult,const FString&)> Function)
{
 return new FRequest{AsShared(),MoveTemp(Function)};
}
template<class T> void EOS_CALL FCitixEOSOnline::Callback(const T* Data)
{
 TUniquePtr<FRequest> Context(static_cast<FRequest*>(Data->ClientData)); Context->Finish(Data->ResultCode);
}
void EOS_CALL FCitixEOSOnline::Published(const EOS_Sessions_UpdateSessionCallbackInfo* Data)
{
 TUniquePtr<FRequest> Context(static_cast<FRequest*>(Data->ClientData));
 Context->Finish(Data->ResultCode,Data->SessionId ? UTF8_TO_TCHAR(Data->SessionId) : FString());
}
FCitixEOSOnline::~FCitixEOSOnline()
{
 ClearResults();
 if(Connect && ExpiryNotify!=EOS_INVALID_NOTIFICATIONID) EOS_Connect_RemoveNotifyAuthExpiration(Connect,ExpiryNotify);
 if(Connect && StatusNotify!=EOS_INVALID_NOTIFICATIONID) EOS_Connect_RemoveNotifyLoginStatusChanged(Connect,StatusNotify);
 if(Platform && PeerNotify!=EOS_INVALID_NOTIFICATIONID) EOS_P2P_RemoveNotifyPeerConnectionEstablished(EOS_Platform_GetP2PInterface(*Platform),PeerNotify);
 if(bHasRoom && Sessions) { EOS_Sessions_DestroySessionOptions Options={EOS_SESSIONS_DESTROYSESSION_API_LATEST,RoomSession}; EOS_Sessions_DestroySession(Sessions,&Options,nullptr,[](const EOS_Sessions_DestroySessionCallbackInfo*){}); }
 if(Sockets) Sockets->Shutdown();
 Sockets.Reset(); Platform.Reset();
}
bool FCitixEOSOnline::IsReady() const { return Connect && User && EOS_Connect_GetLoginStatus(Connect,User)==EOS_ELoginStatus::EOS_LS_LoggedIn; }
FString FCitixEOSOnline::UserString() const { return User ? IdString(User) : FString(); }
bool FCitixEOSOnline::ValidateTransportAddress(const FString& Address)
{
 if(!Address.StartsWith(TEXT("EOS:")) || Address.Len()!=36) return false;
 for(TCHAR C:Address.Mid(4)) if(!FChar::IsHexDigit(C)) return false;
 return true;
}
FString FCitixEOSOnline::TravelAddress(const FString& Address)
{
 // FURL would treat an unbracketed EOS: as a protocol. Brackets preserve the host.
 return ValidateTransportAddress(Address) ? TEXT("[")+Address+TEXT("]") : FString();
}
void FCitixEOSOnline::Complete(EOS_EResult Result,const FString& Stage)
{
 bWorking=false;
 UE_LOG(LogCitix,Log,TEXT("[CitixOnline] %s: %s"),*Stage,UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
 auto Completion=MoveTemp(Pending); Pending={};
 if(Completion) Completion(Result==EOS_EResult::EOS_Success,Result==EOS_EResult::EOS_Success ? FString() : Stage+TEXT(": ")+UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
 if(bLeaveRequested && !bWorking) { bLeaveRequested=false; Leave(); }
}
void FCitixEOSOnline::Authenticate(FCompletion Completion)
{
 if(IsReady()) { Completion(true,FString()); return; }
 if(bWorking) { Completion(false,TEXT("Online connection is still busy. Retry in a moment.")); return; }
 Pending=MoveTemp(Completion); bWorking=true;
 if(IsRunningCommandlet()) { Complete(EOS_EResult::EOS_NotConfigured,TEXT("Online is unavailable in commandlets")); return; }
 if(!Platform)
 {
  auto* Manager=IEOSSDKManager::Get();
  if(!Manager || !Manager->IsInitialized()) { Complete(EOS_EResult::EOS_NotConfigured,TEXT("Online SDK unavailable")); return; }
  TMap<FString,FString> Values;
  for(const TCHAR* Key:{TEXT("ProductId"),TEXT("SandboxId"),TEXT("DeploymentId"),TEXT("ClientId"),TEXT("ClientSecret")})
  {
   FString Value; GConfig->GetString(TEXT("CitixEOS"),Key,Value,GGameIni);
   if(Value.IsEmpty()) { Complete(EOS_EResult::EOS_NotConfigured,FString(TEXT("Online configuration missing "))+Key); return; }
   Values.Add(Key,Value);
  }
  FTCHARToUTF8 Product(*Values[TEXT("ProductId")]),Sandbox(*Values[TEXT("SandboxId")]),Deployment(*Values[TEXT("DeploymentId")]),Client(*Values[TEXT("ClientId")]),Secret(*Values[TEXT("ClientSecret")]);
  EOS_Platform_Options Options={}; Options.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
  Options.ProductId=Product.Get(); Options.SandboxId=Sandbox.Get(); Options.DeploymentId=Deployment.Get();
  Options.ClientCredentials.ClientId=Client.Get(); Options.ClientCredentials.ClientSecret=Secret.Get();
  Options.Flags=EOS_PF_DISABLE_OVERLAY|EOS_PF_DISABLE_SOCIAL_OVERLAY;
  Platform=Manager->CreatePlatform(Options);
  if(!Platform) { Complete(EOS_EResult::EOS_NotConfigured,TEXT("Online platform unavailable")); return; }
  Connect=EOS_Platform_GetConnectInterface(*Platform); Sessions=EOS_Platform_GetSessionsInterface(*Platform);
  EOS_Connect_AddNotifyAuthExpirationOptions Expiry={EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST};
  ExpiryNotify=EOS_Connect_AddNotifyAuthExpiration(Connect,&Expiry,this,[](const EOS_Connect_AuthExpirationCallbackInfo* Info)
  {
   auto* Self=static_cast<FCitixEOSOnline*>(Info->ClientData);
   Self->bRefreshRequested=true;
  });
  EOS_Connect_AddNotifyLoginStatusChangedOptions Status={EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST};
  StatusNotify=EOS_Connect_AddNotifyLoginStatusChanged(Connect,&Status,this,[](const EOS_Connect_LoginStatusChangedCallbackInfo* Info)
  {
   UE_LOG(LogCitix,Log,TEXT("[CitixOnline] guest login status=%d"),int32(Info->CurrentStatus));
  });
 }
 EOS_Connect_CreateDeviceIdOptions Device={EOS_CONNECT_CREATEDEVICEID_API_LATEST,"PC Windows"};
 EOS_Connect_CreateDeviceId(Connect,&Device,Request([](FCitixEOSOnline& Self,EOS_EResult Code,const FString&)
 {
  if(Code==EOS_EResult::EOS_Success || Code==EOS_EResult::EOS_DuplicateNotAllowed) Self.Login();
  else Self.Complete(Code,TEXT("Guest device identity"));
 }),DeviceCreated);
}
void EOS_CALL FCitixEOSOnline::DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo* Data) { Callback(Data); }
void FCitixEOSOnline::Login()
{
 EOS_Connect_Credentials Credentials={EOS_CONNECT_CREDENTIALS_API_LATEST,nullptr,EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN};
 EOS_Connect_UserLoginInfo Info={}; Info.ApiVersion=EOS_CONNECT_USERLOGININFO_API_LATEST; Info.DisplayName="CitixDriver";
 EOS_Connect_LoginOptions Options={EOS_CONNECT_LOGIN_API_LATEST,&Credentials,&Info};
 EOS_Connect_Login(Connect,&Options,Request([](FCitixEOSOnline& Self,EOS_EResult Code,const FString& Id)
 {
  if(Code==EOS_EResult::EOS_Success) Self.AcceptLogin(EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Id)));
  else Self.Complete(Code,TEXT("Guest login"));
 }),LoggedIn);
}
void FCitixEOSOnline::Maintain()
{
 if(bRefreshRequested && !bWorking && !bUpdating && !bLeaveRequested)
 { bRefreshRequested=false; bWorking=true; Login(); }
}
void EOS_CALL FCitixEOSOnline::LoggedIn(const EOS_Connect_LoginCallbackInfo* Data)
{
 TUniquePtr<FRequest> Context(static_cast<FRequest*>(Data->ClientData));
 if(Data->ResultCode==EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken)
 {
  if(auto Self=Context->Owner.Pin())
  {
   EOS_Connect_CreateUserOptions Options={EOS_CONNECT_CREATEUSER_API_LATEST,Data->ContinuanceToken};
   EOS_Connect_CreateUser(Self->Connect,&Options,Context.Release(),UserCreated);
  }
 }
 else Context->Finish(Data->ResultCode,Data->ResultCode==EOS_EResult::EOS_Success ? IdString(Data->LocalUserId) : FString());
}
void EOS_CALL FCitixEOSOnline::UserCreated(const EOS_Connect_CreateUserCallbackInfo* Data)
{
 TUniquePtr<FRequest> Context(static_cast<FRequest*>(Data->ClientData)); Context->Finish(Data->ResultCode,IdString(Data->LocalUserId));
}
void FCitixEOSOnline::AcceptLogin(EOS_ProductUserId Id)
{
 User=Id;
 if(!IsReady()) { Complete(EOS_EResult::EOS_InvalidUser,TEXT("Verify guest login")); return; }
 if(!Sockets)
 {
  auto Utils=MakeShared<FUtils,ESPMode::ThreadSafe>(); Utils->Owner=AsShared();
  Sockets=MakeUnique<FSocketSubsystemEOS>(Platform,Utils);
  FString Error;
  if(!Sockets->Init(Error)) { Sockets->Shutdown(); Sockets.Reset(); Complete(EOS_EResult::EOS_NoConnection,TEXT("Guest transport ")+Error); return; }
  const auto P2P=EOS_Platform_GetP2PInterface(*Platform);
  if(FParse::Param(FCommandLine::Get(),TEXT("CitixForceRelay")))
  {
   EOS_P2P_SetRelayControlOptions Relay={EOS_P2P_SETRELAYCONTROL_API_LATEST,EOS_ERelayControl::EOS_RC_ForceRelays};
   const auto Code=EOS_P2P_SetRelayControl(P2P,&Relay);
   if(Code!=EOS_EResult::EOS_Success) { Complete(Code,TEXT("Force relay test")); return; }
  }
  EOS_P2P_AddNotifyPeerConnectionEstablishedOptions Established={EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST,User,nullptr};
  PeerNotify=EOS_P2P_AddNotifyPeerConnectionEstablished(P2P,&Established,this,[](const EOS_P2P_OnPeerConnectionEstablishedInfo* Info)
  {
   auto* Self=static_cast<FCitixEOSOnline*>(Info->ClientData);
   const FString Remote=IdString(Info->RemoteUserId);
   const FString Type=Info->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection ? TEXT("Relayed") : Info->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_DirectConnection ? TEXT("Direct") : TEXT("None");
   Self->PeerConnections.Add(Remote,Type);
   UE_LOG(LogCitix,Log,TEXT("[CitixOnline] peer=%s connection=%s"),*Remote,*Type);
  });
 }
 UE_LOG(LogCitix,Log,TEXT("[CitixOnline] guest=%s transport ready"),*UserString());
 Complete(EOS_EResult::EOS_Success,TEXT("Guest login"));
}
EOS_EResult FCitixEOSOnline::Attributes(EOS_HSessionModification Modification,const FString& Name,int32 Players,bool Playing)
{
 const FString Values[]={Name,FString::FromInt(UCitixSessionSubsystem::ProtocolVersion),FString::Printf(TEXT("%u"),UCitixSessionSubsystem::NetworkVersion()),FString::FromInt(Players),Playing ? TEXT("1") : TEXT("0")};
 const char* Keys[]={"CITIX_ROOM","CITIX_PROTOCOL","CITIX_NETWORK","CITIX_PLAYERS","CITIX_PLAYING"};
 for(int32 I=0;I<5;++I)
 {
  FTCHARToUTF8 Value(*Values[I]); EOS_Sessions_AttributeData Data={}; Data.ApiVersion=EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
  Data.Key=Keys[I]; Data.ValueType=EOS_ESessionAttributeType::EOS_SAT_String; Data.Value.AsUtf8=Value.Get();
  EOS_SessionModification_AddAttributeOptions Add={EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,&Data,EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise};
  auto Code=EOS_SessionModification_AddAttribute(Modification,&Add); if(Code!=EOS_EResult::EOS_Success) return Code;
 }
 return EOS_EResult::EOS_Success;
}
void FCitixEOSOnline::Host(const FString& Name,FCompletion Completion)
{
 if(bWorking || bHasRoom || !IsReady()) { Completion(false,TEXT("Online connection is not ready.")); return; }
 PublishedPlayers=-1;
 Pending=MoveTemp(Completion); bWorking=true; bHost=true; ClearResults();
 EOS_HSessionModification Modification=nullptr;
 EOS_Sessions_CreateSessionModificationOptions Create={}; Create.ApiVersion=EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
 Create.SessionName=RoomSession; Create.BucketId=Bucket; Create.MaxPlayers=2; Create.LocalUserId=User;
 auto Code=EOS_Sessions_CreateSessionModification(Sessions,&Create,&Modification);
 if(Code==EOS_EResult::EOS_Success)
 {
  FTCHARToUTF8 Address(*(TEXT("EOS:")+UserString()));
  EOS_SessionModification_SetHostAddressOptions HostAddress={EOS_SESSIONMODIFICATION_SETHOSTADDRESS_API_LATEST,Address.Get()};
  Code=EOS_SessionModification_SetHostAddress(Modification,&HostAddress);
  EOS_SessionModification_SetPermissionLevelOptions Public={EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST,EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised};
  if(Code==EOS_EResult::EOS_Success) Code=EOS_SessionModification_SetPermissionLevel(Modification,&Public);
  if(Code==EOS_EResult::EOS_Success) Code=Attributes(Modification,Name,1,false);
 }
 if(Code==EOS_EResult::EOS_Success)
 {
  EOS_Sessions_UpdateSessionOptions Options={EOS_SESSIONS_UPDATESESSION_API_LATEST,Modification};
  EOS_Sessions_UpdateSession(Sessions,&Options,Request([](FCitixEOSOnline& Self,EOS_EResult Result,const FString& Id)
  {
   if(Result==EOS_EResult::EOS_Success) { Self.bHasRoom=true; Self.SessionId=Id; Self.RegisterHost(); }
   else Self.Complete(Result,TEXT("Create online room"));
  }),Published);
 }
 if(Modification) EOS_SessionModification_Release(Modification);
 if(Code!=EOS_EResult::EOS_Success) Complete(Code,TEXT("Prepare online room"));
}
void FCitixEOSOnline::RegisterHost()
{
 EOS_Sessions_RegisterPlayersOptions Options={EOS_SESSIONS_REGISTERPLAYERS_API_LATEST,RoomSession,&User,1};
 EOS_Sessions_RegisterPlayers(Sessions,&Options,Request([](FCitixEOSOnline& Self,EOS_EResult Code,const FString&)
 {
  if(Code==EOS_EResult::EOS_Success) Self.Registered.Add(Self.UserString());
  else Self.bLeaveRequested=true;
  Self.Complete(Code,TEXT("Register room host"));
 }),Callback<EOS_Sessions_RegisterPlayersCallbackInfo>);
}
void FCitixEOSOnline::ClearResults()
{
 for(auto Handle:Details) EOS_SessionDetails_Release(Handle);
 Details.Reset(); Addresses.Reset(); Rooms.Reset();
 if(Search) { EOS_SessionSearch_Release(Search); Search=nullptr; }
}
void FCitixEOSOnline::Find(FCompletion Completion)
{
 if(bWorking || !IsReady()) { Completion(false,TEXT("Online connection is not ready.")); return; }
 Pending=MoveTemp(Completion); bWorking=true; ClearResults();
 EOS_Sessions_CreateSessionSearchOptions Create={EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST,50};
 auto Code=EOS_Sessions_CreateSessionSearch(Sessions,&Create,&Search);
 if(Code==EOS_EResult::EOS_Success)
 {
  EOS_Sessions_AttributeData Data={}; Data.ApiVersion=EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST; Data.Key=EOS_SESSIONS_SEARCH_BUCKET_ID; Data.ValueType=EOS_ESessionAttributeType::EOS_SAT_String; Data.Value.AsUtf8=Bucket;
  EOS_SessionSearch_SetParameterOptions Filter={EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST,&Data,EOS_EOnlineComparisonOp::EOS_CO_EQUAL};
  Code=EOS_SessionSearch_SetParameter(Search,&Filter);
 }
 if(Code!=EOS_EResult::EOS_Success) { Complete(Code,TEXT("Search online rooms")); return; }
 EOS_SessionSearch_FindOptions Find={EOS_SESSIONSEARCH_FIND_API_LATEST,User};
 EOS_SessionSearch_Find(Search,&Find,Request([](FCitixEOSOnline& Self,EOS_EResult Result,const FString&)
 {
  if(Result==EOS_EResult::EOS_Success)
  {
   EOS_SessionSearch_GetSearchResultCountOptions Count={EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST};
   const uint32 Num=EOS_SessionSearch_GetSearchResultCount(Self.Search,&Count);
   for(uint32 I=0;I<Num;++I)
   {
    EOS_HSessionDetails Detail=nullptr; EOS_SessionSearch_CopySearchResultByIndexOptions Copy={EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST,I};
    if(EOS_SessionSearch_CopySearchResultByIndex(Self.Search,&Copy,&Detail)!=EOS_EResult::EOS_Success) continue;
    EOS_SessionDetails_Info* Info=nullptr; EOS_SessionDetails_CopyInfoOptions Get={EOS_SESSIONDETAILS_COPYINFO_API_LATEST};
    if(EOS_SessionDetails_CopyInfo(Detail,&Get,&Info)!=EOS_EResult::EOS_Success) { EOS_SessionDetails_Release(Detail); continue; }
    auto Attribute=[Detail](const char* Key)
    {
     EOS_SessionDetails_Attribute* Value=nullptr; EOS_SessionDetails_CopySessionAttributeByKeyOptions Read={EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYKEY_API_LATEST,Key};
     FString Text;
     if(EOS_SessionDetails_CopySessionAttributeByKey(Detail,&Read,&Value)==EOS_EResult::EOS_Success)
     { if(Value->Data && Value->Data->ValueType==EOS_ESessionAttributeType::EOS_SAT_String && Value->Data->Value.AsUtf8) Text=UTF8_TO_TCHAR(Value->Data->Value.AsUtf8); EOS_SessionDetails_Attribute_Release(Value); }
     return Text;
    };
    const FString Address=Info->HostAddress ? UTF8_TO_TCHAR(Info->HostAddress) : FString();
    FCitixNearbyRoom Room; Room.Id=Info->SessionId ? UTF8_TO_TCHAR(Info->SessionId) : FString();
    Room.Name=UCitixSessionSubsystem::NormalizeRoomName(Attribute("CITIX_ROOM")); Room.Capacity=Info->Settings ? FMath::Clamp(int32(Info->Settings->NumPublicConnections),1,2) : 2;
    Room.Players=FMath::Clamp(Room.Capacity-int32(Info->NumOpenPublicConnections),0,Room.Capacity);
    Room.bCompatible=Attribute("CITIX_PROTOCOL")==FString::FromInt(UCitixSessionSubsystem::ProtocolVersion) && Attribute("CITIX_NETWORK")==FString::Printf(TEXT("%u"),UCitixSessionSubsystem::NetworkVersion()) && ValidateTransportAddress(Address);
    Room.bInProgress=Attribute("CITIX_PLAYING")==TEXT("1");
    EOS_SessionDetails_Info_Release(Info);
    // A single guest cannot play against itself; keep backend results for separate processes observable in logs.
    UE_LOG(LogCitix,Log,TEXT("[CitixOnline] discovered room=%s id=%s players=%d compatible=%d"),*Room.Name,*Room.Id,Room.Players,Room.bCompatible);
    if(Address==TEXT("EOS:")+Self.UserString()) { EOS_SessionDetails_Release(Detail); continue; }
    Self.Rooms.Add(Room); Self.Details.Add(Detail); Self.Addresses.Add(Address);
   }
  }
  Self.Complete(Result,TEXT("Find online rooms"));
 }),Callback<EOS_SessionSearch_FindCallbackInfo>);
}
void FCitixEOSOnline::Join(int32 Index,FCompletion Completion)
{
 if(bWorking || bHasRoom || !IsReady() || !Rooms.IsValidIndex(Index) || !Rooms[Index].CanJoin()) { Completion(false,TEXT("This room is unavailable. Refresh online games.")); return; }
 Pending=MoveTemp(Completion); bWorking=true; bHost=false;
 const FString Address=Addresses[Index],Id=Rooms[Index].Id;
 EOS_Sessions_JoinSessionOptions Join={}; Join.ApiVersion=EOS_SESSIONS_JOINSESSION_API_LATEST; Join.SessionName=RoomSession; Join.SessionHandle=Details[Index]; Join.LocalUserId=User;
 EOS_Sessions_JoinSession(Sessions,&Join,Request([Address,Id](FCitixEOSOnline& Self,EOS_EResult Result,const FString&)
 {
  if(Result==EOS_EResult::EOS_Success) { Self.bHasRoom=true; Self.SessionId=Id; }
  Self.bWorking=false; auto Completion=MoveTemp(Self.Pending); Self.Pending={};
  if(Completion) Completion(Result==EOS_EResult::EOS_Success,Result==EOS_EResult::EOS_Success ? TravelAddress(Address) : FString(TEXT("Join online room: "))+UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
  if(Self.bLeaveRequested && !Self.bWorking) { Self.bLeaveRequested=false; Self.Leave(); }
 }),Callback<EOS_Sessions_JoinSessionCallbackInfo>);
}
void FCitixEOSOnline::Leave(FCompletion Completion)
{
 if(bWorking || bUpdating) { bLeaveRequested=true; if(Completion) Completion(false,TEXT("Closing the pending online operation…")); return; }
 if(!bHasRoom) { if(Completion) Completion(true,FString()); return; }
 bWorking=true; Pending=MoveTemp(Completion);
 EOS_Sessions_DestroySessionOptions Destroy={EOS_SESSIONS_DESTROYSESSION_API_LATEST,RoomSession};
 EOS_Sessions_DestroySession(Sessions,&Destroy,Request([](FCitixEOSOnline& Self,EOS_EResult Result,const FString&)
 {
  if(Result==EOS_EResult::EOS_Success || Result==EOS_EResult::EOS_NotFound) { Self.bHasRoom=false; Self.SessionId.Empty(); Self.Registered.Reset(); Self.PeerConnections.Reset(); Result=EOS_EResult::EOS_Success; }
  Self.Complete(Result,TEXT("Leave online room"));
 }),Callback<EOS_Sessions_DestroySessionCallbackInfo>);
}
void FCitixEOSOnline::Update(const FString& Name,int32 Players,bool Playing,const TArray<FString>& TransportUsers)
{
 if(!bHost || !bHasRoom || bWorking || bUpdating || !IsReady()) return;
 TSet<FString> Desired; Desired.Add(UserString()); for(const auto& Id:TransportUsers) Desired.Add(Id);
 for(const FString& Id:Desired)
 {
  if(Registered.Contains(Id)) continue;
  EOS_ProductUserId Remote=EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Id)); if(!EOS_ProductUserId_IsValid(Remote)) continue;
  bUpdating=true; EOS_Sessions_RegisterPlayersOptions Options={EOS_SESSIONS_REGISTERPLAYERS_API_LATEST,RoomSession,&Remote,1};
  EOS_Sessions_RegisterPlayers(Sessions,&Options,Request([Id](FCitixEOSOnline& Self,EOS_EResult Code,const FString&)
  { Self.bUpdating=false; if(Code==EOS_EResult::EOS_Success) Self.Registered.Add(Id); UE_LOG(LogCitix,Log,TEXT("[CitixOnline] register transport player: %s"),UTF8_TO_TCHAR(EOS_EResult_ToString(Code))); if(Self.bLeaveRequested) { Self.bLeaveRequested=false; Self.Leave(); } }),Callback<EOS_Sessions_RegisterPlayersCallbackInfo>); return;
 }
 for(const FString& Id:Registered)
 {
  if(Desired.Contains(Id)) continue;
  EOS_ProductUserId Remote=EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Id)); bUpdating=true;
  EOS_Sessions_UnregisterPlayersOptions Options={EOS_SESSIONS_UNREGISTERPLAYERS_API_LATEST,RoomSession,&Remote,1};
  EOS_Sessions_UnregisterPlayers(Sessions,&Options,Request([Id](FCitixEOSOnline& Self,EOS_EResult Code,const FString&)
  { Self.bUpdating=false; if(Code==EOS_EResult::EOS_Success) Self.Registered.Remove(Id); UE_LOG(LogCitix,Log,TEXT("[CitixOnline] unregister transport player: %s"),UTF8_TO_TCHAR(EOS_EResult_ToString(Code))); if(Self.bLeaveRequested) { Self.bLeaveRequested=false; Self.Leave(); } }),Callback<EOS_Sessions_UnregisterPlayersCallbackInfo>); return;
 }
 if(PublishedName==Name && PublishedPlayers==Players && bPublishedPlaying==Playing) return;
 EOS_HSessionModification Modification=nullptr; EOS_Sessions_UpdateSessionModificationOptions Options={EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST,RoomSession};
 auto Code=EOS_Sessions_UpdateSessionModification(Sessions,&Options,&Modification);
 if(Code==EOS_EResult::EOS_Success) Code=Attributes(Modification,Name,Players,Playing);
 if(Code==EOS_EResult::EOS_Success)
 {
  bUpdating=true; EOS_Sessions_UpdateSessionOptions Update={EOS_SESSIONS_UPDATESESSION_API_LATEST,Modification};
  EOS_Sessions_UpdateSession(Sessions,&Update,Request([Name,Players,Playing](FCitixEOSOnline& Self,EOS_EResult Code,const FString&)
  { Self.bUpdating=false; if(Code==EOS_EResult::EOS_Success) { Self.PublishedName=Name; Self.PublishedPlayers=Players; Self.bPublishedPlaying=Playing; } UE_LOG(LogCitix,Log,TEXT("[CitixOnline] occupancy update: %s"),UTF8_TO_TCHAR(EOS_EResult_ToString(Code))); if(Self.bLeaveRequested) { Self.bLeaveRequested=false; Self.Leave(); } }),Published);
 }
 if(Modification) EOS_SessionModification_Release(Modification);
}
