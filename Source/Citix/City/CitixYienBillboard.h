#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CitixYienBillboard.generated.h"
class ACitixCityGenerator;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
/** Five deterministic roadside advertisements; generated independently on both peers. */
UCLASS()
class CITIX_API ACitixYienBillboard : public AActor
{
 GENERATED_BODY()
public:
 ACitixYienBillboard();
 void Configure(int32 Campaign,bool BuildingScreen);
 static int32 Populate(ACitixCityGenerator* City);
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Frame;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Screen;
 UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Trim;
 bool bBuildingScreen=false;
 FString CampaignText;
private:
 UPROPERTY() TObjectPtr<UMaterialInterface> SignTextMaterial;
 void AddText(const FString& Text,const FVector& Position,float Size,const FLinearColor& Colour);
};
