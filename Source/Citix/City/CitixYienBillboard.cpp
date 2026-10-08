#include "City/CitixYienBillboard.h"
#include "City/CitixCityGenerator.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Citix.h"
#include "UObject/ConstructorHelpers.h"

ACitixYienBillboard::ACitixYienBillboard()
{
 PrimaryActorTick.bCanEverTick=false;
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> TextAsset(TEXT("/Game/Citix/Materials/M_YienSignText.M_YienSignText"));
 SignTextMaterial=TextAsset.Object;
 auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("BillboardRoot")); SetRootComponent(Root);
 Frame=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Structure"));
 Screen=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Display"));
 Trim=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CyanTrim"));
 for (auto* C:{Frame.Get(),Screen.Get(),Trim.Get()}) {
  C->SetupAttachment(Root); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCanEverAffectNavigation(false);
  C->SetStaticMesh(FCitixSurfaceLibrary::GetMesh(ECitixSurface::PropDark));
 }
 Frame->SetMaterial(0,FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface::PropDark,FLinearColor(.045f,.055f,.07f)));
 Frame->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 Frame->SetCollisionObjectType(ECC_WorldStatic); Frame->SetCollisionResponseToAllChannels(ECR_Block);
 Screen->SetMaterial(0,FCitixSurfaceLibrary::GetTintedMaterial(ECitixSurface::PropDark,FLinearColor(.02f,.03f,.07f)));
 Trim->SetMaterial(0,FCitixSurfaceLibrary::GetTintedEmissiveMaterial(FLinearColor(.025f,.65f,.85f)));
 Trim->SetCastShadow(false); Screen->SetCastShadow(false);
}
void ACitixYienBillboard::AddText(const FString& Text,const FVector& Position,float Size,const FLinearColor& Colour)
{
 auto* Label=NewObject<UTextRenderComponent>(this); Label->SetupAttachment(GetRootComponent());
 Label->SetRelativeLocation(Position); Label->SetHorizontalAlignment(EHTA_Center); Label->SetVerticalAlignment(EVRTA_TextCenter);
 Label->SetWorldSize(Size); Label->SetText(FText::FromString(Text)); Label->SetTextRenderColor(Colour.ToFColor(true));
 if (SignTextMaterial) Label->SetTextMaterial(SignTextMaterial);
 Label->SetCollisionEnabled(ECollisionEnabled::NoCollision); Label->SetCastShadow(false); Label->RegisterComponent();
}
void ACitixYienBillboard::Configure(int32 Campaign,bool BuildingScreen)
{
 bBuildingScreen=BuildingScreen;
 const TCHAR* Headlines[]={TEXT("IMAGINATION."),TEXT("YIEN ROYALE 2"),TEXT("LAVA PARKOUR"),TEXT("UNDEAD RUSH"),TEXT("GAMES & APPS")};
 const TCHAR* Subtitles[]={TEXT("MADE INTERACTIVE."),TEXT("BATTLE ROYALE / PC"),TEXT("PLATFORMER / PARKOUR / PC"),TEXT("SCAVENGE. FORTIFY. SURVIVE."),TEXT("IMAGINATION. MADE INTERACTIVE.")};
 Campaign=FMath::Clamp(Campaign,0,4); CampaignText=Headlines[Campaign];
 auto Box=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size) { C->AddInstance(FTransform(FRotator::ZeroRotator,P,Size*.01f)); };
 Box(Frame,FVector(0,0,0),FVector(45,1550,830));
 Box(Screen,FVector(25,0,0),FVector(8,1490,770));
 Box(Trim,FVector(32,0,365),FVector(10,1410,8));
 Box(Trim,FVector(32,-685,-315),FVector(10,50,50));
 if (!BuildingScreen) {
  for (float Side:{-1.f,1.f}) {
   Box(Frame,FVector(-50,Side*490,-670),FVector(65,65,540));
   Box(Frame,FVector(-50,Side*490,-938),FVector(150,160,16));
  }
 } else {
  for (float Side:{-1.f,1.f}) Box(Frame,FVector(-80,Side*640,0),FVector(150,45,590));
 }
 const FLinearColor White(.9f,.95f,1), Cyan(.24f,.85f,1), Muted(.56f,.68f,.78f);
 AddText(TEXT("Yien Studio"),FVector(34,0,240),125,White);
 AddText(Headlines[Campaign],FVector(34,0,40),Campaign==0 ? 119 : 105,Cyan);
 AddText(Subtitles[Campaign],FVector(34,0,-115),Campaign==4 ? 49 : 60,White);
 AddText(TEXT("yienstudio.net"),FVector(34,0,-285),50,Muted);
}
int32 ACitixYienBillboard::Populate(ACitixCityGenerator* City)
{
 if (!City || !City->GetWorld()) return 0;
 auto* World=City->GetWorld(); const auto& Roads=City->GetRoadNetwork();
 TArray<FVector> Sites; int32 Count=0;
 TArray<int32> Candidates; for (int32 I=0;I<Roads.Edges.Num();++I) if (Roads.IsValidNode(Roads.Edges[I].NodeA) && Roads.IsValidNode(Roads.Edges[I].NodeB)) Candidates.Add(I);
 Candidates.Sort([&](int32 A,int32 B) {
  const auto& EA=Roads.Edges[A];const auto& EB=Roads.Edges[B];
  const float DA=((Roads.Nodes[EA.NodeA].Position+Roads.Nodes[EA.NodeB].Position)*.5f).SizeSquared();
  const float DB=((Roads.Nodes[EB.NodeA].Position+Roads.Nodes[EB.NodeB].Position)*.5f).SizeSquared();
  return DA==DB ? A<B : DA<DB;
 });
 FCollisionQueryParams Params(SCENE_QUERY_STAT(YienBillboards),false); Params.AddIgnoredActor(City);
 // Building-mounted faces first. A three-point trace verifies the entire screen fits on one facade instance.
 for (int32 Kind=0;Kind<2;++Kind) {
  const int32 Wanted=Kind==0 ? 3 : Count+2;
  for (int32 Pass=0;Pass<2 && Count<Wanted;++Pass) for (int32 K=0;K<Candidates.Num() && Count<Wanted;++K) {
   const int32 Index=Candidates[K]; const auto& E=Roads.Edges[Index];
   if (!E.bDrivable || E.bBridge || !Roads.IsValidNode(E.NodeA) || !Roads.IsValidNode(E.NodeB) || Roads.EdgeLength(Index)<2100) continue;
   const FVector2D Mid=(Roads.Nodes[E.NodeA].Position+Roads.Nodes[E.NodeB].Position)*.5f;
   const FVector2D D=Roads.EdgeDirection(Index); const FVector2D Side(-D.Y,D.X);
   for (float Sign:{-1.f,1.f}) {
    const FVector Normal=FVector(Side.X,Side.Y,0)*Sign;
    FVector Site; FRotator Facing;
    if (Kind==0) {
     const FVector Start(Mid.X,Mid.Y,1200.f); FHitResult Hit;
     if (!World->LineTraceSingleByObjectType(Hit,Start,Start+Normal*6000,FCollisionObjectQueryParams(ECC_WorldStatic),Params) || FMath::Abs(Hit.ImpactNormal.Z)>.1f || !Hit.GetComponent() || !(Hit.GetComponent()->GetMaterial(0)==FCitixSurfaceLibrary::GetMaterial(ECitixSurface::FacadeConcrete) || Hit.GetComponent()->GetMaterial(0)==FCitixSurfaceLibrary::GetMaterial(ECitixSurface::FacadeBeige) || Hit.GetComponent()->GetMaterial(0)==FCitixSurfaceLibrary::GetMaterial(ECitixSurface::FacadeWhite) || Hit.GetComponent()->GetMaterial(0)==FCitixSurfaceLibrary::GetMaterial(ECitixSurface::FacadeBrick) || Hit.GetComponent()->GetMaterial(0)==FCitixSurfaceLibrary::GetMaterial(ECitixSurface::FacadeMetal))) continue;
     const FVector Tangent=FVector::CrossProduct(FVector::UpVector,Hit.ImpactNormal);
     bool Fits=true;
     for (float Offset:{-780.f,780.f}) { FHitResult Edge; const FVector P=Hit.ImpactPoint+Tangent*Offset+Hit.ImpactNormal*250;
      Fits &= World->LineTraceSingleByObjectType(Edge,P,P-Hit.ImpactNormal*400,FCollisionObjectQueryParams(ECC_WorldStatic),Params) && Edge.GetComponent()==Hit.GetComponent() && Edge.Item==Hit.Item;
     }
     if (!Fits) continue;
     Site=Hit.ImpactPoint+Hit.ImpactNormal*300; Facing=Hit.ImpactNormal.Rotation();
    } else {
     Site=FVector(Mid.X,Mid.Y,955)+Normal*(E.CorridorWidth*.5f+550);
     Facing=(-Normal).Rotation();
     if (!City->IsDryFootprint(Site,FVector2D(200,830),Facing.Yaw)) continue;
     if (World->OverlapAnyTestByObjectType(Site,Facing.Quaternion(),FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeBox(FVector(180,820,850)),Params)) continue;
    }
    const float Separation=Pass==0 ? 13000.f : 6500.f;
    if (Sites.ContainsByPredicate([&](const FVector& P){return FVector::DistSquared2D(P,Site)<FMath::Square(Separation);})) continue;
    auto* SignActor=World->SpawnActor<ACitixYienBillboard>(Site,Facing);
    if (!SignActor) continue;
    SignActor->Configure(Count,Kind==0); SignActor->SetOwner(City); Sites.Add(Site); ++Count;
    UE_LOG(LogCitix,Log,TEXT("[YienBillboard] %d %s at %s campaign=%s"),Count,Kind==0?TEXT("building screen"):TEXT("ground billboard"),*Site.ToString(),*SignActor->CampaignText);
    if (Count>=Wanted) break;
   }
  }
 }
 UE_LOG(LogCitix,Log,TEXT("[YienBillboard] generated %d / 5 deterministic roadside advertisements"),Count);
 return Count;
}
