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
 auto FullPackage=[](const FString& Path) {
  UPackage* Package=LoadPackage(nullptr,*Path,LOAD_None);
  if (!Package) Package=CreatePackage(*Path);
  Package->FullyLoad(); return Package;
 };
 const FString MatPath=TEXT("/Game/Citix/Materials/M_CitixBush");
 auto* BushPackage=FullPackage(MatPath); auto* Bush=FindObject<UMaterial>(BushPackage,TEXT("M_CitixBush"));
 if (!Bush) Bush=NewObject<UMaterial>(BushPackage,TEXT("M_CitixBush"),RF_Public|RF_Standalone);
 Bush->GetExpressionCollection().Empty(); Bush->TwoSided=true; Bush->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes,true);
 auto* Colour=Cast<UMaterialExpressionVertexColor>(UMaterialEditingLibrary::CreateMaterialExpression(Bush,UMaterialExpressionVertexColor::StaticClass()));
 UMaterialEditingLibrary::ConnectMaterialProperty(Colour,TEXT(""),MP_BaseColor);
 auto* Rough=Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Bush,UMaterialExpressionConstant::StaticClass())); Rough->R=.88f;
 UMaterialEditingLibrary::ConnectMaterialProperty(Rough,TEXT(""),MP_Roughness);
 UMaterialEditingLibrary::RecompileMaterial(Bush);
 if (!Save(Bush,MatPath)) return false;

 const FString MeshPath=TEXT("/Game/Citix/Meshes/SM_CitixBush");
 auto* MeshPackage=FullPackage(MeshPath); auto* Mesh=FindObject<UStaticMesh>(MeshPackage,TEXT("SM_CitixBush"));
 if (!Mesh) Mesh=NewObject<UStaticMesh>(MeshPackage,TEXT("SM_CitixBush"),RF_Public|RF_Standalone);
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
 // Overlapping icosahedral lobes form a broad, irregular low-poly bush.
 // Each lobe has twenty flat faces; no smooth single-sphere canopy.
 const float G=(1.f+FMath::Sqrt(5.f))*.5f;
 const FVector LobeVertices[]={ {-1,G,0},{1,G,0},{-1,-G,0},{1,-G,0},
  {0,-1,G},{0,1,G},{0,-1,-G},{0,1,-G},{G,0,-1},{G,0,1},{-G,0,-1},{-G,0,1} };
 const int32 Faces[][3]={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
  {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
  {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
  {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
 const FVector Centers[]={{-28,-12,-12},{-6,-19,-9},{20,-16,-12},
  {30,7,-10},{6,17,-6},{-24,14,-13},{-16,-1,10},{10,-1,14},{0,8,25}};
 for (int32 Lobe=0;Lobe<UE_ARRAY_COUNT(Centers);++Lobe) {
  const FVector Radius(Rng.FRandRange(18,25),Rng.FRandRange(17,23),Rng.FRandRange(18,25));
  const FRotator Rotation(Rng.FRandRange(-20,20),Lobe*47.f,Rng.FRandRange(-15,15));
  const float Shade=Rng.FRandRange(.85f,1.15f);
  for (const auto& Face:Faces) {
   const FLinearColor Green=FLinearColor(.10f,.30f,.055f)*Shade*Rng.FRandRange(.9f,1.08f);
   Tri(Centers[Lobe]+Rotation.RotateVector(LobeVertices[Face[0]].GetSafeNormal()*Radius),
       Centers[Lobe]+Rotation.RotateVector(LobeVertices[Face[1]].GetSafeNormal()*Radius),
       Centers[Lobe]+Rotation.RotateVector(LobeVertices[Face[2]].GetSafeNormal()*Radius),Green);
  }
 }
 // Short angular stems ground the canopy without adding fine foliage noise.
 for (int32 Stem=0;Stem<3;++Stem) {
  const FVector Start((Stem-1)*11,0,-43),End((Stem-1)*16,0,-8);
  for (int32 Side=0;Side<4;++Side) {
   const FVector A(FMath::Cos(Side*PI/2)*2,FMath::Sin(Side*PI/2)*2,0);
   const FVector B(FMath::Cos((Side+1)*PI/2)*2,FMath::Sin((Side+1)*PI/2)*2,0);
   Tri(Start+A,Start+B,End+B,FLinearColor(.12f,.075f,.035f));
   Tri(Start+A,End+B,End+A,FLinearColor(.12f,.075f,.035f));
  }
 }
 // Normalize the authored bush to the same centred 100 cm convention as city instances.
 FBox Bounds(ForceInit); for (const auto V:Description.Vertices().GetElementIDs()) Bounds+=FVector(Positions[V]);
 for (const auto V:Description.Vertices().GetElementIDs()) Positions[V]=FVector3f((FVector(Positions[V])-Bounds.GetCenter())/Bounds.GetSize()*100.f);
 Mesh->GetStaticMaterials().Reset(); Mesh->GetStaticMaterials().Add(FStaticMaterial(Bush,TEXT("Leaves")));
 UStaticMesh::FBuildMeshDescriptionsParams Build; Build.bBuildSimpleCollision=false; Build.bCommitMeshDescription=true;
 if (!Mesh->BuildFromMeshDescriptions({&Description},Build)) return false;
 Mesh->CreateBodySetup(); auto* Body=Mesh->GetBodySetup(); Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
 Body->AggGeom.EmptyElements(); FKBoxElem Box; Box.X=85; Box.Y=85; Box.Z=90; Body->AggGeom.BoxElems.Add(Box);
 Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes();
 if (!Save(Mesh,MeshPath)) return false;

 const FString AirPath=TEXT("/Game/Citix/Materials/M_CitixAirFlow");
 auto* AirPackage=FullPackage(AirPath); auto* Air=FindObject<UMaterial>(AirPackage,TEXT("M_CitixAirFlow"));
 if (!Air) Air=NewObject<UMaterial>(AirPackage,TEXT("M_CitixAirFlow"),RF_Public|RF_Standalone);
 Air->GetExpressionCollection().Empty(); Air->BlendMode=BLEND_Additive; Air->SetShadingModel(MSM_Unlit); Air->TwoSided=true; Air->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes,true);
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
 UE_LOG(LogCitix,Log,TEXT("[CitixRoadside] bush triangles=%d, faceted lobes=9; assets saved=%d"),Triangles,Saved);
 return Saved;
}
#endif

