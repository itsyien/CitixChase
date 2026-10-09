#include "Sandbox/CitixBulletTracer.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

ACitixBulletTracer::ACitixBulletTracer()
{
 PrimaryActorTick.bCanEverTick=true;
 SetCanBeDamaged(false);
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 Trail=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trail"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
 Trail->SetupAttachment(GetRootComponent()); Trail->SetStaticMesh(Cube.Object);
 Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision); Trail->SetCastShadow(false);
 Trail->SetReceivesDecals(false); Trail->SetCanEverAffectNavigation(false);
}

FVector ACitixBulletTracer::ResolveVisibleEndpoint(UWorld* World,const FVector& Muzzle,const FVector& ResolvedEndpoint,AActor* Shooter)
{
 if (!World || Muzzle.Equals(ResolvedEndpoint)) return ResolvedEndpoint;
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CitixTracerCover),false,Shooter);
 FHitResult Hit;
 // Client-local defensive clip also handles a replicated door/vehicle that has
 // moved in front of the muzzle since the authoritative shot was resolved.
 if (World->LineTraceSingleByChannel(Hit,Muzzle,ResolvedEndpoint,ECC_Visibility,Query))
  return Hit.ImpactPoint;
 return ResolvedEndpoint;
}

void ACitixBulletTracer::Spawn(UWorld* World,const FVector& Muzzle,const FVector& ResolvedEndpoint,AActor* Shooter)
{
 if (!World || World->GetNetMode()==NM_DedicatedServer || Muzzle.ContainsNaN() || ResolvedEndpoint.ContainsNaN()) return;
 const FVector Endpoint=ResolveVisibleEndpoint(World,Muzzle,ResolvedEndpoint,Shooter);
 if (FVector::DistSquared(Muzzle,Endpoint)<4.f) return;
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 if (auto* Tracer=World->SpawnActor<ACitixBulletTracer>(ACitixBulletTracer::StaticClass(),FTransform::Identity,Params))
  Tracer->Initialize(Muzzle,Endpoint);
}

void ACitixBulletTracer::Initialize(const FVector& Muzzle,const FVector& Endpoint)
{
 const FVector Direction=(Endpoint-Muzzle).GetSafeNormal();
 // Inset from cover slightly, so the bright face never pokes through the wall.
 const FVector End=Endpoint-Direction*.5f;
 Length=FVector::Distance(Muzzle,End)/100.f;
 SetActorLocation((Muzzle+End)*.5f); SetActorRotation(Direction.Rotation());
 UMaterialInterface* Base=FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(1.f,.85f,.55f));
 if (auto* Material=Base ? UMaterialInstanceDynamic::Create(Base->GetMaterial(),Trail) : nullptr)
 {
  // Give a short shot enough contrast at driving distance without brightening
  // the shared material used by windows and other world decoration.
  Material->SetVectorParameterValue(TEXT("Color"),FLinearColor(8.f,5.f,2.f));
  Trail->SetMaterial(0,Material);
 }
 Trail->SetRelativeScale3D(FVector(Length,.024f,.024f));
 SetLifeSpan(Duration);
}

void ACitixBulletTracer::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 Age+=DeltaSeconds;
 const float Fade=FMath::Square(FMath::Clamp(1.f-Age/Duration,0.f,1.f));
 const float Width=.024f*(.45f+.55f*Fade);
 Trail->SetRelativeScale3D(FVector(Length,Width,Width));
}
