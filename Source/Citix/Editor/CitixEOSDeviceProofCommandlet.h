#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CitixEOSDeviceProofCommandlet.generated.h"

/** Opt-in headless Connect Device ID proof; no maps, assets or rooms changed. */
UCLASS()
class UCitixEOSDeviceProofCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCitixEOSDeviceProofCommandlet();
    virtual int32 Main(const FString& Params) override;
};
