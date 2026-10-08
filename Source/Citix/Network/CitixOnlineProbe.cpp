#include "Network/CitixOnlineProbe.h"
#include "Network/CitixSessionSubsystem.h"
#include "Network/CitixEOSOnline.h"
#include "Network/CitixEOSNetDriver.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "HAL/PlatformMisc.h"
#include "Citix.h"

void CitixOnlineProbeTick(UCitixSessionSubsystem* Manager)
{
#if !UE_BUILD_SHIPPING
 static FString Action;
 static const bool Enabled=FParse::Value(FCommandLine::Get(),TEXT("CitixOnlineProbe="),Action);
 if(!Enabled || !Manager || !Manager->GetWorld()) return;
 static const double Started=FPlatformTime::Seconds();
 static int32 Step=0;
 static bool Bound=false;
 static bool ReadyRequested=false;
 static double PursuitAt=0;
 static FString Session;
 const double Age=FPlatformTime::Seconds()-Started;
 const bool MatchProof=FParse::Param(FCommandLine::Get(),TEXT("CitixOnlineMatchProof"));
 float Duration=35; FParse::Value(FCommandLine::Get(),TEXT("CitixProbeSeconds="),Duration);
 FString Tag=TEXT("Probe"); FParse::Value(FCommandLine::Get(),TEXT("CitixNetTag="),Tag);
 const FString Directory=FPaths::ProjectSavedDir()/TEXT("EOSProof");
 auto Report=[&](bool Success,const FString& Reason)
 {
  auto Service=Manager->GetOnlineService();
  FString Peer,Type;
  if(Service) for(const auto& Connection:Service->PeerConnections) { Peer=Connection.Key; Type=Connection.Value; break; }
  FString Text=FString::Printf(TEXT("{\"success\":%s,\"action\":\"%s\",\"puid\":\"%s\",\"session_id\":\"%s\",\"eos_listener\":%s,\"rooms\":%d,\"cleanup_confirmed\":%s,\"reason\":\"%s\"}"),Success ? TEXT("true") : TEXT("false"),*Action,Service ? *Service->UserString() : TEXT(""),*Session,Bound ? TEXT("true") : TEXT("false"),Manager->Rooms.Num(),Service && !Service->HasRoom() && !Service->IsWorking() ? TEXT("true") : TEXT("false"),*Reason.Replace(TEXT("\""),TEXT("'")));
  Text.RemoveFromEnd(TEXT("}"));
  auto* State=Manager->GetWorld()->GetGameState<ACitixChaseGameState>();
  Text+=FString::Printf(TEXT(",\"remote_puid\":\"%s\",\"connection_type\":\"%s\",\"players\":%d,\"city_seed\":%d,\"city_hash\":%d,\"replicated_match\":%s}"),*Peer,*Type,State ? State->PlayerArray.Num() : 0,State ? State->CitySeed : 0,State ? State->CityConfigHash : 0,MatchProof && Success ? TEXT("true") : TEXT("false"));
  FString ReportPath=Directory/(TEXT("Game-")+Tag+TEXT(".json"));
  FParse::Value(FCommandLine::Get(),TEXT("CitixProbeReport="),ReportPath);
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath),true);
  FFileHelper::SaveStringToFile(Text,*ReportPath);
  UE_LOG(LogCitix,Log,TEXT("[CitixOnlineProbe] %s %s"),Success ? TEXT("PASS") : TEXT("FAIL"),*Reason);
  Step=99; if(!MatchProof || !Success) FPlatformMisc::RequestExit(false);
 };
 if(Step==99) return;
 if(Manager->State==ECitixConnectionState::Error) { Report(false,Manager->Message); return; }
 if(Age>100+Duration) { Manager->Cancel(); Report(false,TEXT("Probe timed out")); return; }
 if(Step==0 && Age>3)
 {
  Step=1;
  if(Action==TEXT("Search") || Action==TEXT("Join")) Manager->FindRooms();
  else Manager->HostRoom(TEXT("CitixOnlineTest"));
 }
 else if(Step==1)
 {
  if(Action==TEXT("Search") && !Manager->IsBusy()) { Report(true,TEXT("Online search completed")); return; }
  if(Action==TEXT("Join") && !Manager->IsBusy())
  {
   for(int32 I=0;I<Manager->Rooms.Num();++I) if(Manager->Rooms[I].Name==TEXT("CitixOnlineTest") && Manager->Rooms[I].CanJoin()) { Manager->JoinRoom(I); Step=4; return; }
   static double NextSearch=0; if(Age>NextSearch) { NextSearch=Age+3; Manager->FindRooms(); }
   return;
  }
  if(Manager->IsInRoom())
  {
   auto* Driver=Cast<UCitixEOSNetDriver>(Manager->GetWorld()->GetNetDriver());
   Bound=Driver && !Driver->bIsPassthrough && Manager->GetWorld()->GetNetMode()==NM_ListenServer;
   if(auto Service=Manager->GetOnlineService()) Session=Service->SessionId;
   if(!Bound) { Manager->LeaveRoom(); Report(false,TEXT("Listen world did not use EOS transport")); return; }
   UE_LOG(LogCitix,Log,TEXT("[CitixOnlineProbe] EOS listener ready session=%s"),*Session);
   Step=2;
  }
 }
 else if(Step==4 && Manager->IsInRoom())
 {
  auto* Driver=Cast<UCitixEOSNetDriver>(Manager->GetWorld()->GetNetDriver());
  Bound=Driver && !Driver->bIsPassthrough && Manager->GetWorld()->GetNetMode()==NM_Client;
  if(auto Service=Manager->GetOnlineService()) Session=Service->SessionId;
  if(!Bound) { Manager->LeaveRoom(); Report(false,TEXT("Joining world did not use EOS transport")); return; }
  Step=2;
 }
 else if(Step==2)
 {
  if(MatchProof)
  {
   auto* World=Manager->GetWorld(); auto* State=World->GetGameState<ACitixChaseGameState>();
   auto* PC=Cast<ACitixDrivingPlayerController>(World->GetFirstPlayerController()); auto* Player=PC ? PC->GetPlayerState<ACitixChasePlayerState>() : nullptr;
   if(State && Player && Player->bCityIdentityValid && State->PlayerArray.Num()==2 && State->LayoutSpawnLocations.Num()==2)
   {
    if(!ReadyRequested && State->Phase==ECitixChasePhase::Waiting) { ReadyRequested=true; PC->ServerChaseInteract(); }
    if(State->Phase==ECitixChasePhase::Pursuit)
    {
     if(PursuitAt==0) PursuitAt=Age;
     int32 Cars=0; for(TActorIterator<ACitixVehiclePawn> It(World);It;++It) ++Cars;
     auto Service=Manager->GetOnlineService(); bool Relay=true;
     if(Service) for(const auto& Connection:Service->PeerConnections) Relay&=Connection.Value==TEXT("Relayed");
     if(Age-PursuitAt>=5 && Cars>=2 && Service && Service->PeerConnections.Num()>0 && (Action==TEXT("Join") || Service->RegisteredPlayerCount()==2) && (!FParse::Param(FCommandLine::Get(),TEXT("CitixForceRelay")) || Relay))
     { Report(true,TEXT("Two EOS peers verified the city, readied and entered replicated pursuit")); return; }
    }
   }
  }
  if(Age>Duration) { Manager->LeaveRoom(); Step=3; }
 }
 else if(Step==3 && !Manager->IsBusy())
 {
  auto Service=Manager->GetOnlineService();
  if(Service && !Service->HasRoom() && !Service->IsWorking() && Manager->GetWorld()->GetNetMode()==NM_Standalone) Report(!MatchProof,MatchProof ? TEXT("No verified online match before timeout") : TEXT("EOS listener hosted and room cleanup completed"));
 }
#endif
}
