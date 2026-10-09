#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Containers/Ticker.h"
#include "CitixSessionSubsystem.generated.h"
class FCitixEOSOnline;

UENUM()
enum class ECitixConnectionState : uint8 { Idle, Creating, Searching, Joining, Connecting, InRoom, Leaving, Error };

USTRUCT()
struct FCitixNearbyRoom
{
 GENERATED_BODY()
 FString Name;
 FString Id;
 int32 Players=1;
 int32 Capacity=2;
 bool bCompatible=false;
 bool bInProgress=false;
 bool CanJoin() const { return bCompatible && !bInProgress && Players<Capacity; }
};

/** Persistent LAN session lifecycle; widgets only issue actions and read state. */
UCLASS()
class CITIX_API UCitixSessionSubsystem : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 void HostRoom(const FString& Name);
 void HostLANRoom(const FString& Name);
 void FindLANRooms();
 bool IsOnline() const { return bOnline; }
 TSharedPtr<FCitixEOSOnline,ESPMode::ThreadSafe> GetOnlineService() const { return Online; }
 void HostAtAddress(const FString& Name,const FString& Address);
 static FString LocalAddress();
 bool bManualHost=false;
 FString HostAddress;
 void FindRooms();
 void JoinRoom(int32 Index);
 void JoinAddress(const FString& Address);
 void Cancel();
 void Retry();
 void LeaveRoom();
 void NotifyWorldReady();
 void UpdateOccupancy();
 bool IsBusy() const;
 bool IsInRoom() const { return State==ECitixConnectionState::InRoom; }
 ECitixConnectionState State=ECitixConnectionState::Idle;
 FString Message=TEXT("Host an online game or find your friend's room. LAN is available in Advanced.");
 FString RoomName;
 TArray<FCitixNearbyRoom> Rooms;
 int32 ResultsRevision=0;
 static constexpr int32 ProtocolVersion=4;
 static FString DefaultRoomName();
 static FString NormalizeRoomName(const FString& Name);
 static bool ValidateAddress(const FString& Address);
 static FCitixNearbyRoom DescribeRoom(const FOnlineSessionSearchResult& Result);
 static FString AdmissionError(const FString& Options,int32 Players,bool InProgress);
 static FString ExplainFailure(ENetworkFailure::Type Type,const FString& Error);
 static uint32 NetworkVersion();
private:
 TSharedPtr<FCitixEOSOnline,ESPMode::ThreadSafe> Online;
 bool bOnline=true;
 uint32 OnlineGeneration=0;
 void StartFind();
 void AuthenticateOnline(TFunction<void()> Next);
 enum class EAction : uint8 { None, Host, Find, Join, Address };
 EAction LastAction=EAction::None;
 FString LastArgument;
 FString LastRoomId;
 IOnlineSessionPtr Sessions;
 TSharedPtr<FOnlineSessionSearch> Search;
 FDelegateHandle CreateHandle, FindHandle, JoinHandle, DestroyHandle, NetworkHandle, TravelHandle;
 FTSTicker::FDelegateHandle TickHandle;
 double Deadline=0;
 double NextOccupancyCheck=0;
 int32 AdvertisedPlayers=-1;
 bool bAdvertisedInProgress=false;
 bool bHosting=false;
 bool bReturnToMenu=false;
 bool bPreserveError=false;
 bool bCancelPending=false;
 void SetState(ECitixConnectionState NewState,const FString& Text,float Timeout=0);
 void ClearOperationDelegates();
 void Fail(const FString& Reason);
 void DestroyRoom();
 void StartHost(const FString& Name);
 int32 ManualHostPort=0;
 void OnCreated(FName Name,bool Success);
 void OnFound(bool Success);
 void OnJoined(FName Name,EOnJoinSessionCompleteResult::Type Result);
 void OnDestroyed(FName Name,bool Success);
 void NetworkFailed(UWorld* World,UNetDriver* Driver,ENetworkFailure::Type Type,const FString& Error);
 void TravelFailed(UWorld* World,ETravelFailure::Type Type,const FString& Error);
 bool Tick(float Delta);
 void TravelTo(const FString& Address);
};
