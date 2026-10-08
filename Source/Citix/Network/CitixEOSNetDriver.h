#pragma once
#include "CoreMinimal.h"
#include "NetDriverEOS.h"
#include "CitixEOSNetDriver.generated.h"
class FCitixEOSOnline;

/** Routes to this game instance's guest platform; unrelated listen URLs stay LAN/IP. */
UCLASS(Transient,Config=Engine)
class CITIX_API UCitixEOSNetDriver : public UNetDriverEOS
{
 GENERATED_BODY()
public:
 virtual bool IsAvailable() const override { return true; }
 virtual bool InitConnect(FNetworkNotify* Notify,const FURL& URL,FString& Error) override;
 virtual bool InitListen(FNetworkNotify* Notify,FURL& URL,bool Reuse,FString& Error) override;
 virtual ISocketSubsystem* GetSocketSubsystem() override;
private:
 TSharedPtr<FCitixEOSOnline,ESPMode::ThreadSafe> Guest;
 bool GetGuest(FString& Error);
};
