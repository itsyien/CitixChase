#include "Editor/CitixRoadsideAssets.h"
#if WITH_EDITOR
#include "Citix.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "MaterialEditingLibrary.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"

bool CitixCreateRoadsideAssets()
{
 auto Save=[](UObject* Asset,const FString& Path) {
  Asset->GetOutermost()->MarkPackageDirty();
  const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
  FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
  return UPackage::SavePackage(Asset->GetOutermost(),Asset,*Filename,Args);
 };
 const FString MatPath=TEXT("/Game/Citix/Materials/M_CitixBush");
 auto* Bush=NewObject<UMaterial>(CreatePackage(*MatPath),TEXT("M_CitixBush"),RF_Public|RF_Standalone);
 Bush->TwoSided=true; Bush->bUsedWithInstancedStaticMeshes=true;
 auto* Colour=Cast<UMaterialExpressionVertexColor>(UMaterialEditingLibrary::CreateMaterialExpression(Bush,UMaterialExpressionVertexColor::StaticClass()));
 UMaterialEditingLibrary::ConnectMaterialProperty(Colour,TEXT("RGB"),MP_BaseColor);
 auto* Rough=Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Bush,UMaterialExpressionConstant::StaticClass())); Rough->R=.88f;
 UMaterialEditingLibrary::ConnectMaterialProperty(Rough,TEXT(""),MP_Roughness);
 UMaterialEditingLibrary::RecompileMaterial(Bush);
 if (!Save(Bush,MatPath)) return false;

 const FString MeshPath=TEXT("/Game/Citix/Meshes/SM_CitixBush");
 auto* Mesh=NewObject<UStaticMesh>(CreatePackage(*MeshPath),TEXT("SM_CitixBush"),RF_Public|RF_Standalone);
 FMeshDescription Description; FStaticMeshAttributes Attr(Description); Attr.Register(); Attr.GetVertexInstanceUVs().SetNumChannels(1);
 auto Positions=Attr.GetVertexPositions(); auto Normals=Attr.GetVertexInstanceNormals(); auto Colors=Attr.GetVertexInstanceColors(); auto UVs=Attr.GetVertexInstanceUVs();
 const auto Group=Description.CreatePolygonGroup(); Attr.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Leaves");
 int32 Triangles=0;
 auto Tri=[&](FVector A,FVector B,FVector C,FLinearColor Tint) {
  const FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal(); TArray<FVertexInstanceID> Instances;
  const FVector Points[]={A,B,C};
  for (int32 I=0;I<3;++I) {
   const auto V=Description.CreateVertex(); Positions[V]=FVector3f(Points[I]);
   const auto VI=Description.CreateVertexInstance(V); Normals[VI]=FVector3f(N); Colors[VI]=FVector4f(Tint.R,Tint.G,Tint.B,1); UVs.Set(VI,0,FVector2f(I==1, I==2)); Instances.Add(VI);
  }
  Description.CreatePolygon(Group,Instances); ++Triangles;
 };
 FRandomStream Rng(1337);
 // Twelve irregular woody stems carry many pointed, folded leaves. There is no
 // spherical canopy: silhouette, open gaps and varied green faces come from leaves.
 for (int32 Stem=0;Stem<12;++Stem) {
  const float Angle=Stem*137.5f;
  const FVector Start(Rng.FRandRange(-12,12),Rng.FRandRange(-12,12),-48);
  const FVector End=FRotator(0,Angle,0).Vector()*Rng.FRandRange(22,39)+FVector(0,0,Rng.FRandRange(16,36));
  const FVector Axis=(End-Start).GetSafeNormal(); FVector Side,Up; Axis.FindBestAxisVectors(Side,Up);
  for (int32 SideIndex=0;SideIndex<5;++SideIndex) {
   const FVector A=Side*FMath::Cos(SideIndex*2*PI/5)+Up*FMath::Sin(SideIndex*2*PI/5);
   const FVector B=Side*FMath::Cos((SideIndex+1)*2*PI/5)+Up*FMath::Sin((SideIndex+1)*2*PI/5);
   Tri(Start+A*1.3f,Start+B*1.3f,End+B*.45f,FLinearColor(.12f,.075f,.035f));
   Tri(Start+A*1.3f,End+B*.45f,End+A*.45f,FLinearColor(.12f,.075f,.035f));
  }
  for (int32 Leaf=0;Leaf<22;++Leaf) {
   const float T=.22f+.78f*(Leaf/21.f);
   const FVector P=FMath::Lerp(Start,End,T);
   const FVector Out=FRotator(Rng.FRandRange(-45,30),Angle+Leaf*151.f,0).Vector();
   const FVector Across=FVector::CrossProduct(Out,FVector::UpVector).GetSafeNormal();
   const float Length=Rng.FRandRange(9,17),Width=Length*.34f;
   const FVector Tip=P+Out*Length,Left=P+Out*(Length*.42f)+Across*Width,Right=P+Out*(Length*.42f)-Across*Width,Ridge=P+Out*(Length*.52f)+FVector(0,0,2.4f);
   const float Shade=Rng.FRandRange(.75f,1.25f);
   const FLinearColor Green(.12f*Shade,.34f*Shade,.075f*Shade);
   Tri(P,Left,Ridge,Green); Tri(Left,Tip,Ridge,Green*1.07f); Tri(Tip,Right,Ridge,Green*.85f); Tri(Right,P,Ridge,Green*.9f);
  }
 }
 // Normalize the authored bush to the same centred 100 cm convention as city instances.
 FBox Bounds(ForceInit); for (const auto V:Description.Vertices().GetElementIDs()) Bounds+=FVector(Positions[V]);
 for (const auto V:Description.Vertices().GetElementIDs()) Positions[V]=FVector3f((FVector(Positions[V])-Bounds.GetCenter())/Bounds.GetSize()*100.f);
 Mesh->GetStaticMaterials().Add(FStaticMaterial(Bush,TEXT("Leaves")));
 UStaticMesh::FBuildMeshDescriptionsParams Build; Build.bBuildSimpleCollision=false; Build.bCommitMeshDescription=true;
 if (!Mesh->BuildFromMeshDescriptions({&Description},Build)) return false;
 Mesh->CreateBodySetup(); auto* Body=Mesh->GetBodySetup(); Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
 FKBoxElem Box; Box.X=85; Box.Y=85; Box.Z=90; Body->AggGeom.BoxElems.Add(Box);
 Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes();
 if (!Save(Mesh,MeshPath)) return false;

 const FString AirPath=TEXT("/Game/Citix/Materials/M_CitixAirFlow");
 auto* Air=NewObject<UMaterial>(CreatePackage(*AirPath),TEXT("M_CitixAirFlow"),RF_Public|RF_Standalone);
 Air->BlendMode=BLEND_Additive; Air->SetShadingModel(MSM_Unlit); Air->TwoSided=true; Air->bUsedWithInstancedStaticMeshes=true;
 auto* White=Cast<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(Air,UMaterialExpressionConstant3Vector::StaticClass())); White->Constant=FLinearColor(.75f,1.05f,1.3f);
 UMaterialEditingLibrary::ConnectMaterialProperty(White,TEXT(""),MP_EmissiveColor);
 auto* Opacity=Cast<UMaterialExpressionCustom>(UMaterialEditingLibrary::CreateMaterialExpression(Air,UMaterialExpressionCustom::StaticClass())); Opacity->OutputType=CMOT_Float1;
 auto Input=[&](const TCHAR* Name,UMaterialExpression* Expr) {FCustomInput I; I.InputName=Name; I.Input.Connect(0,Expr); Opacity->Inputs.Add(I);};
 auto* UV=UMaterialEditingLibrary::CreateMaterialExpression(Air,UMaterialExpressionTextureCoordinate::StaticClass()); Input(TEXT("UV"),UV);
 auto* Time=UMaterialEditingLibrary::CreateMaterialExpression(Air,UMaterialExpressionTime::StaticClass()); Input(TEXT("Time"),Time);
 auto* Fade=Cast<UMaterialExpressionPerInstanceCustomData>(UMaterialEditingLibrary::CreateMaterialExpression(Air,UMaterialExpressionPerInstanceCustomData::StaticClass())); Fade->DataIndex=0; Input(TEXT("Strength"),Fade);
 auto* Phase=Cast<UMaterialExpressionPerInstanceCustomData>(UMaterialEditingLibrary::CreateMaterialExpression(Air,UMaterialExpressionPerInstanceCustomData::StaticClass())); Phase->DataIndex=1; Input(TEXT("Phase"),Phase);
 Opacity->Code=TEXT("float edge=pow(saturate(1-abs(UV.y*2-1)),3); float pulse=.3+.7*pow(saturate(sin(Time*18-Phase*1.8)),3); return edge*pulse*Strength*.65;");
 UMaterialEditingLibrary::ConnectMaterialProperty(Opacity,TEXT(""),MP_Opacity); UMaterialEditingLibrary::RecompileMaterial(Air);
 const bool Saved=Save(Air,AirPath);
 UE_LOG(LogCitix,Log,TEXT("[CitixRoadside] bush triangles=%d, pointed leaves=264; assets saved=%d"),Triangles,Saved);
 return Saved;
}
#endif
