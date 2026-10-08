#include "Network/CitixEOSNetDriver.h"
#include "Network/CitixEOSOnline.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SocketSubsystem.h"

bool UCitixEOSNetDriver::GetGuest(FString& Error)
{
 UWorld* W=FindWorld();
 auto* Manager=W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UCitixSessionSubsystem>() : nullptr;
 Guest=Manager ? Manager->GetOnlineService() : nullptr;
 if(!Guest || !Guest->IsReady() || !Guest->GetSockets()) { Error=TEXT("Online guest connection is not ready. Retry from the lobby."); return false; }
 return true;
}
bool UCitixEOSNetDriver::InitConnect(FNetworkNotify* InNotify,const FURL& URL,FString& Error)
{
 if(URL.Host.StartsWith(TEXT("EOS:")) && !GetGuest(Error)) return false;
 return Super::InitConnect(InNotify,URL,Error);
}
bool UCitixEOSNetDriver::InitListen(FNetworkNotify* InNotify,FURL& URL,bool Reuse,FString& Error)
{
 if(URL.HasOption(TEXT("CitixOnline"))) { if(!GetGuest(Error)) return false; }
 else URL.AddOption(TEXT("bUseIPSockets"));
 return Super::InitListen(InNotify,URL,Reuse,Error);
}
ISocketSubsystem* UCitixEOSNetDriver::GetSocketSubsystem()
{
 return bIsPassthrough ? ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM) : Guest ? Guest->GetSockets() : nullptr;
}
