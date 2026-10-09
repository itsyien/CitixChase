#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixBulletTracer.generated.h"

class UStaticMeshComponent;

// Cosmetic hitscan trail. Damage is resolved before this is spawned, and this
// actor never collides, damages anything, or continues beyond the resolved hit.
UCLASS(NotBlueprintable)
class CITIX_API ACitixBulletTracer : public AActor
{
 GENERATED_BODY()
public:
 ACitixBulletTracer();
 virtual void Tick(float DeltaSeconds) override;
 static void Spawn(UWorld* World,const FVector& Muzzle,const FVector& ResolvedEndpoint,AActor* Shooter=nullptr);
 static FVector ResolveVisibleEndpoint(UWorld* World,const FVector& Muzzle,const FVector& ResolvedEndpoint,AActor* Shooter);
 static constexpr float Duration=.09f;
private:
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Trail;
 float Age=0.f;
 float Length=0.f;
 void Initialize(const FVector& Muzzle,const FVector& Endpoint);
};
