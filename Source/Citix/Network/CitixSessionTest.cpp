#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/CitixDrivingPlayerController.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Network/CitixSessionSubsystem.h"
#include "Network/CitixEOSOnline.h"
#include "Chase/CitixChaseGameMode.h"
#include "Chase/CitixChasePlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixOnlineDefaultTest,"Citix.Online.PrimaryFlow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixOnlineDefaultTest::RunTest(const FString& Parameters)
{
 AddExpectedError(TEXT("DeviceId access credentials already exist"),EAutomationExpectedErrorFlags::Contains,0);
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
 auto* World=GI->GetWorld(); auto* Manager=GI->GetSubsystem<UCitixSessionSubsystem>();
 Manager->FindRooms();
 TestTrue(TEXT("Primary search uses online feedback instead of nearby LAN"),Manager->Message.Contains(TEXT("online"),ESearchCase::IgnoreCase));
 Manager->Cancel(); GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixHostAdvertisesTest,"Citix.LAN.HostAdvertisesRoom",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixHostAdvertisesTest::RunTest(const FString& Parameters)
{
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
 auto* World=GI->GetWorld();
 auto* PC=World->SpawnActor<ACitixDrivingPlayerController>();
 auto* OSS=IOnlineSubsystem::Get(TEXT("NULL"));
 auto Sessions=OSS ? OSS->GetSessionInterface() : IOnlineSessionPtr();
 TestTrue(TEXT("LAN session provider available"),Sessions.IsValid());
 GI->GetSubsystem<UCitixSessionSubsystem>()->HostLANRoom(UCitixSessionSubsystem::DefaultRoomName());
 FNamedOnlineSession* Room=Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
 TestNotNull(TEXT("Host Game advertises a discoverable room before travel"),Room);
 if(Room) { TestTrue(TEXT("Room is LAN"),Room->SessionSettings.bIsLANMatch); TestEqual(TEXT("Two-driver capacity"),Room->SessionSettings.NumPublicConnections,2); }
 if(Sessions.IsValid() && Room) Sessions->DestroySession(NAME_GameSession);
 GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixRoomMetadataTest,"Citix.LAN.RoomValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixRoomMetadataTest::RunTest(const FString& Parameters)
{
 TestEqual(TEXT("Blank name receives a friendly fallback"),UCitixSessionSubsystem::NormalizeRoomName(TEXT(" \n ")),FString(TEXT("CitixRoom1")));
 TestEqual(TEXT("Name trims outside whitespace"),UCitixSessionSubsystem::NormalizeRoomName(TEXT(" Yien's Room ")),FString(TEXT("Yien's Room")));
 TestFalse(TEXT("Manual address cannot inject travel options"),UCitixSessionSubsystem::ValidateAddress(TEXT("127.0.0.1?listen")));
 TestFalse(TEXT("Manual address rejects an invalid port"),UCitixSessionSubsystem::ValidateAddress(TEXT("192.168.1.2:65536")));
 TestTrue(TEXT("Manual address accepts explicit port"),UCitixSessionSubsystem::ValidateAddress(TEXT("192.168.1.2:7797")));
 FOnlineSessionSearchResult Result;
 Result.Session.SessionSettings.NumPublicConnections=2;
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_ROOM")),FString(TEXT("Yien's Room")),EOnlineDataAdvertisementType::ViaOnlineService);
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_PLAYERS")),1,EOnlineDataAdvertisementType::ViaOnlineService);
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_PROTOCOL")),UCitixSessionSubsystem::ProtocolVersion,EOnlineDataAdvertisementType::ViaOnlineService);
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_NETWORK")),FString::Printf(TEXT("%u"),UCitixSessionSubsystem::NetworkVersion()),EOnlineDataAdvertisementType::ViaOnlineService);
 auto Room=UCitixSessionSubsystem::DescribeRoom(Result); TestEqual(TEXT("Room metadata carries host name"),Room.Name,FString(TEXT("Yien's Room"))); TestTrue(TEXT("One-driver compatible room is joinable"),Room.CanJoin());
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_PLAYERS")),2,EOnlineDataAdvertisementType::ViaOnlineService); Room=UCitixSessionSubsystem::DescribeRoom(Result); TestFalse(TEXT("Full room cannot be joined"),Room.CanJoin());
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_PLAYERS")),1,EOnlineDataAdvertisementType::ViaOnlineService); Result.Session.SessionSettings.Set(FName(TEXT("CITIX_PROTOCOL")),99,EOnlineDataAdvertisementType::ViaOnlineService); Room=UCitixSessionSubsystem::DescribeRoom(Result); TestFalse(TEXT("Different room protocol cannot be joined"),Room.CanJoin());
 Result.Session.SessionSettings.Set(FName(TEXT("CITIX_PROTOCOL")),UCitixSessionSubsystem::ProtocolVersion-1,EOnlineDataAdvertisementType::ViaOnlineService); Room=UCitixSessionSubsystem::DescribeRoom(Result); TestFalse(TEXT("Previous gameplay protocol cannot be joined"),Room.CanJoin());
 TestTrue(TEXT("Concurrent third join rejected at admission"),UCitixSessionSubsystem::AdmissionError(TEXT(""),2,false).Contains(TEXT("full")));
 TestTrue(TEXT("Incompatible client rejected at admission"),UCitixSessionSubsystem::AdmissionError(TEXT("?CitixProtocol=99?CitixBuild=1"),1,false).Contains(TEXT("version")));
 TestTrue(TEXT("Match already started rejected at admission"),UCitixSessionSubsystem::AdmissionError(TEXT(""),1,true).Contains(TEXT("progress")));
 TestTrue(TEXT("Version failure has actionable feedback"),UCitixSessionSubsystem::ExplainFailure(ENetworkFailure::OutdatedClient,TEXT("")).Contains(TEXT("same build")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixSearchCancelTest,"Citix.LAN.CancelThenSearchAgain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixSearchCancelTest::RunTest(const FString& Parameters)
{
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone(); auto* World=GI->GetWorld(); auto* M=GI->GetSubsystem<UCitixSessionSubsystem>();
 M->FindLANRooms(); TestTrue(TEXT("Real LAN search starts"),M->State==ECitixConnectionState::Searching);
 M->Cancel(); TestTrue(TEXT("Cancelled search returns to idle"),M->State==ECitixConnectionState::Idle); TestEqual(TEXT("Cancelled search clears stale rows"),M->Rooms.Num(),0);
 M->FindLANRooms(); TestTrue(TEXT("Retry can immediately start a new provider search"),M->State==ECitixConnectionState::Searching); M->Cancel();
 GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixLateLoginCapacityTest,"Citix.LAN.LoginRechecksCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixLateLoginCapacityTest::RunTest(const FString& Parameters)
{
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
 auto* World=GI->GetWorld();
 auto* Mode=World->SpawnActor<ACitixChaseGameMode>();
 auto AddDriver=[World]() {
  auto* Driver=World->SpawnActor<ACitixDrivingPlayerController>();
  Driver->SetPlayerState(World->SpawnActor<ACitixChasePlayerState>());
  // InitializeStandalone creates a world that has not initialized actors for
  // play. Controller::PostInitializeComponents therefore has not registered
  // the fixture in the world's controller iterator yet. Avoid beginning play
  // (and generating a city) just to test admission; register explicitly.
  World->AddController(Driver);
  return Driver;
 };
 AddDriver();
 TestEqual(TEXT("Only the host exists during the early admission checks"),Mode->GetNumPlayers(),1);
 // Both handshakes can be admitted before either guest creates a controller.
 TestTrue(TEXT("First pending guest passes early admission"),UCitixSessionSubsystem::AdmissionError(TEXT(""),Mode->GetNumPlayers(),false).IsEmpty());
 TestTrue(TEXT("Second pending guest also passes early admission"),UCitixSessionSubsystem::AdmissionError(TEXT(""),Mode->GetNumPlayers(),false).IsEmpty());
 AddDriver();
 if(TestEqual(TEXT("First guest now occupies the final driver slot"),Mode->GetNumPlayers(),2)) {
  FString Error;
  APlayerController* LateGuest=Mode->Login(nullptr,ROLE_AutonomousProxy,TEXT(""),TEXT(""),FUniqueNetIdRepl(),Error);
  TestNull(TEXT("Late guest is rejected at actual Login"),LateGuest);
  TestTrue(TEXT("Login reports the two-driver capacity reason"),Error.Contains(TEXT("Room full")));
  TestEqual(TEXT("Rejected login cannot add a third driver"),Mode->GetNumPlayers(),2);
 }
 GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixManualHostTest,"Citix.LAN.ManualHostPort",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixManualHostTest::RunTest(const FString& Parameters)
{
 TestEqual(TEXT("Default name is independent of Windows account"),UCitixSessionSubsystem::DefaultRoomName(),FString(TEXT("CitixRoom1")));
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone(); auto* World=GI->GetWorld(); auto* Manager=GI->GetSubsystem<UCitixSessionSubsystem>();
 Manager->HostAtAddress(TEXT("CitixRoom1"),TEXT("203.0.113.42:7802"));
 TestTrue(TEXT("Cannot host on another PC's address"),Manager->State==ECitixConnectionState::Error);
 Manager->HostAtAddress(TEXT("CitixRoom1"),TEXT("127.0.0.1:7802"));
 auto Sessions=IOnlineSubsystem::Get(TEXT("NULL"))->GetSessionInterface(); auto* Session=Sessions->GetNamedSession(NAME_GameSession);
 TestNotNull(TEXT("Manual host still owns a two-player session"),Session);
 if(Session) TestFalse(TEXT("Direct hosting works independently of discovery beacon"),Session->SessionSettings.bShouldAdvertise);
 const FURL ListenURL(&World->URL,*World->NextURL,TRAVEL_Relative);
 TestEqual(TEXT("Manual host travels with selected listen port"),ListenURL.Port,7802);
 TestTrue(TEXT("Host travel opens a local map rather than joining remotely"),ListenURL.IsLocalInternal());
 Manager->Cancel(); GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixOnlineURLTest,"Citix.Online.TransportURL",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCitixOnlineURLTest::RunTest(const FString& Parameters)
{
 const FString Address=TEXT("EOS:00026d2988b442018995aab726cb0d7e");
 TestEqual(TEXT("Balance update rejects old replication layouts"),UCitixSessionSubsystem::ProtocolVersion,4);
 const FString Travel=FCitixEOSOnline::TravelAddress(Address);
 const FURL URL(nullptr,*(Travel+TEXT("?CitixProtocol=1")),TRAVEL_Absolute);
 TestTrue(TEXT("EOS travel URL is valid"),bool(URL.Valid));
 TestEqual(TEXT("FURL preserves the entire EOS host"),URL.Host,Address);
 TestTrue(TEXT("Game admission options survive EOS URL parsing"),URL.HasOption(TEXT("CitixProtocol=1")));
 TestFalse(TEXT("EOS addresses reject travel option injection"),FCitixEOSOnline::ValidateTransportAddress(Address+TEXT("?listen")));
 TestFalse(TEXT("EOS addresses reject legacy SDK metadata URLs"),FCitixEOSOnline::ValidateTransportAddress(Address+TEXT(":CitixProof:0")));
 TestTrue(TEXT("Invalid transport addresses produce no travel target"),FCitixEOSOnline::TravelAddress(TEXT("EOS:invalid")).IsEmpty());
 return true;
}
#endif
