#pragma once
#include "CoreMinimal.h"
#include "IEOSSDKManager.h"
#include "SocketSubsystemEOSUtils.h"
#include "Network/CitixSessionSubsystem.h"
#include "eos_sdk.h"
#include "eos_sessions.h"

class ISocketSubsystem;

/** Connect identity and Sessions without an Epic account. Unreal gameplay identity stays NULL. */
class FCitixEOSOnline : public TSharedFromThis<FCitixEOSOnline, ESPMode::ThreadSafe>
{
public:
 using FCompletion = TFunction<void(bool,const FString&)>;
 ~FCitixEOSOnline();
 void Authenticate(FCompletion Completion);
 void Host(const FString& Name,FCompletion Completion);
 void Find(FCompletion Completion);
 void Join(int32 Index,FCompletion Completion);
 void Leave(FCompletion Completion = {});
 void Update(const FString& Name,int32 Players,bool Playing,const TArray<FString>& TransportUsers);
 void Maintain();
 bool IsReady() const;
 bool HasRoom() const { return bHasRoom; }
 bool IsWorking() const { return bWorking || bUpdating; }
 FString UserString() const;
 FString SessionId;
 TMap<FString,FString> PeerConnections;
 int32 RegisteredPlayerCount() const { return Registered.Num(); }
 TArray<FCitixNearbyRoom> Rooms;
 ISocketSubsystem* GetSockets() const { return Sockets.Get(); }
 static bool ValidateTransportAddress(const FString& Address);
 static FString TravelAddress(const FString& Address);
private:
 struct FRequest;
 struct FUtils;
 IEOSPlatformHandlePtr Platform;
 TUniquePtr<ISocketSubsystem> Sockets;
 EOS_HConnect Connect = nullptr;
 EOS_HSessions Sessions = nullptr;
 EOS_ProductUserId User = nullptr;
 EOS_NotificationId ExpiryNotify = EOS_INVALID_NOTIFICATIONID;
 EOS_NotificationId StatusNotify = EOS_INVALID_NOTIFICATIONID;
 EOS_NotificationId PeerNotify = EOS_INVALID_NOTIFICATIONID;
 EOS_HSessionSearch Search = nullptr;
 TArray<EOS_HSessionDetails> Details;
 TArray<FString> Addresses;
 TSet<FString> Registered;
 FString PublishedName;
 int32 PublishedPlayers=-1;
 bool bPublishedPlaying=false;
 FCompletion Pending;
 bool bWorking = false, bUpdating = false, bHasRoom = false, bHost = false, bLeaveRequested = false;
 bool bRefreshRequested=false;
 void Login();
 void AcceptLogin(EOS_ProductUserId Id);
 void Complete(EOS_EResult Result,const FString& Stage);
 void ClearResults();
 void RegisterHost();
 EOS_EResult Attributes(EOS_HSessionModification Modification,const FString& Name,int32 Players,bool Playing);
 FRequest* Request(TFunction<void(FCitixEOSOnline&,EOS_EResult,const FString&)> Function);
 template<class T> static void EOS_CALL Callback(const T* Data);
 static void EOS_CALL Published(const EOS_Sessions_UpdateSessionCallbackInfo* Data);
 static void EOS_CALL LoggedIn(const EOS_Connect_LoginCallbackInfo* Data);
 static void EOS_CALL UserCreated(const EOS_Connect_CreateUserCallbackInfo* Data);
 static void EOS_CALL DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo* Data);
};
