// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/CitixMaterialSetupCommandlet.h"
#include "Editor/CitixRoadsideAssets.h"

#include "Citix.h"

#if WITH_EDITOR
#include "Factories/MaterialFactoryNew.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

UCitixMaterialSetupCommandlet::UCitixMaterialSetupCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

#if WITH_EDITOR

namespace
{
	const TCHAR* const WindowMaterialPath = TEXT("/Game/Citix/Materials/M_CitixWindow");
	const TCHAR* const EmissiveMaterialPath = TEXT("/Game/Citix/Materials/M_CitixEmissive");
	const TCHAR* const WaterMaterialPath = TEXT("/Game/Citix/Materials/M_CitixWater");
	const TCHAR* const SkyMaterialPath = TEXT("/Game/Citix/Materials/M_CitixSky");
	/** Linear HDR night panorama imported from the Content folder. */
	const TCHAR* const SkyTexturePath = TEXT("/Game/Citix/Textures/T_CitixNight");

	/**
	 * Import the lossless 4K night panorama. It stays linear HDR: tone, exposure and the
	 * night transition are applied by the sky material rather than baked into an 8-bit PNG.
	 */
	void ImportSkyTexture()
	{
		const FString SourceFile = FPaths::ProjectContentDir() / TEXT("Citix/Textures/T_CitixNight.exr");
		if (!IFileManager::Get().FileExists(*SourceFile))
		{
			UE_LOG(LogCitix, Warning, TEXT("[Citix] Sky image missing at %s; the dome will fall back to the engine sky."),
				*SourceFile);
			return;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = SourceFile;
		Task->DestinationPath = TEXT("/Game/Citix/Textures");
		Task->DestinationName = TEXT("T_CitixNight");
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = true;

		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetTools.Get().ImportAssetTasks({ Task });

		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, SkyTexturePath))
		{
			Texture->CompressionSettings = TC_HDR;
			Texture->SRGB = false;
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Clamp;
			Texture->MipGenSettings = TMGS_FromTextureGroup;
			Texture->NeverStream = false;
			Texture->PostEditChange();
			Texture->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			const FString TextureFilename = FPackageName::LongPackageNameToFilename(
				SkyTexturePath, FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(Texture->GetOutermost(), Texture, *TextureFilename, SaveArgs);
			UE_LOG(LogCitix, Log, TEXT("[Citix] Imported linear HDR night sky from %s (4096x2048, sRGB off, HDR compression)."), *SourceFile);
		}
		else
		{
			UE_LOG(LogCitix, Error, TEXT("[Citix] Dusk sky import did not create %s."), SkyTexturePath);
		}
	}

	/**
	 * The night sky dome: a translucent unlit sphere sampling the HDR panorama and
	 * fading over SkyAtmosphere. It is two-sided because the camera is inside the sphere.
	 */
	UMaterial* CreateSkyMaterial(const TCHAR* PackagePath, const TCHAR* AssetName, const TCHAR* TexturePath)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		if (IFileManager::Get().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename, false, true, true);
		}

		UPackage* Package = CreatePackage(PackagePath);
		if (!Package)
		{
			return nullptr;
		}

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(AssetName),
			RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!Material)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->bUsedWithInstancedStaticMeshes = true;

		UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -700, 60));
		if (Sample)
		{
			Sample->ParameterName = TEXT("SkyTexture");
			Sample->SamplerType = SAMPLERTYPE_LinearColor;
			if (UTexture* SkyTexture = LoadObject<UTexture>(nullptr, TexturePath))
			{
				Sample->Texture = SkyTexture;
			}
		}

		UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1180, 60));

		// Horizontal UV rotation keeps the warmest part of the panorama aligned with the
		// westward sunset. AddressX wraps, so the seam remains continuous.
		UMaterialExpressionScalarParameter* Rotation = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionScalarParameter::StaticClass(), -1180, 180));
		if (Rotation)
		{
			Rotation->ParameterName = TEXT("SkyRotation");
			Rotation->DefaultValue = 0.75f;
		}
		UMaterialExpressionConstant* Zero = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionConstant::StaticClass(), -1180, 260));
		UMaterialExpressionAppendVector* RotationOffset = Cast<UMaterialExpressionAppendVector>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionAppendVector::StaticClass(), -980, 180));
		UMaterialExpressionAdd* RotatedUV = Cast<UMaterialExpressionAdd>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionAdd::StaticClass(), -800, 80));

		UMaterialExpressionScalarParameter* Exposure = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionScalarParameter::StaticClass(), -600, 250));
		if (Exposure)
		{
			Exposure->ParameterName = TEXT("SkyExposure");
			Exposure->DefaultValue = 1.f;
		}
		UMaterialExpressionMultiply* Exposed = Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionMultiply::StaticClass(), -380, 80));

		UMaterialExpressionVectorParameter* Tint = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionVectorParameter::StaticClass(), -380, 250));
		if (Tint)
		{
			Tint->ParameterName = TEXT("SkyTint");
			Tint->DefaultValue = FLinearColor::White;
		}
		UMaterialExpressionMultiply* Tinted = Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionMultiply::StaticClass(), -160, 100));


		// Horizon band: saturate((1 - V - start) * sharpness), so the sky fades into the
		// city's light-pollution glow instead of meeting the skyline abruptly.
		UMaterialExpressionComponentMask* Vertical = Cast<UMaterialExpressionComponentMask>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionComponentMask::StaticClass(), -800, 420));
		if (Vertical)
		{
			Vertical->R = false;
			Vertical->G = true;
			Vertical->B = false;
			Vertical->A = false;
		}
		UMaterialExpressionOneMinus* OneMinusV = Cast<UMaterialExpressionOneMinus>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionOneMinus::StaticClass(), -820, 420));
		UMaterialExpressionConstant* HorizonStart = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionConstant::StaticClass(), -820, 560));
		if (HorizonStart)
		{
			HorizonStart->R = 0.10f;
		}
		UMaterialExpressionSubtract* AboveHorizon = Cast<UMaterialExpressionSubtract>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionSubtract::StaticClass(), -640, 440));
		UMaterialExpressionConstant* HorizonSharpness = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionConstant::StaticClass(), -640, 600));
		if (HorizonSharpness)
		{
			HorizonSharpness->R = 2.6f;
		}
		UMaterialExpressionMultiply* Band = Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionMultiply::StaticClass(), -460, 480));
		UMaterialExpressionSaturate* Mask = Cast<UMaterialExpressionSaturate>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionSaturate::StaticClass(), -280, 480));

		UMaterialExpressionVectorParameter* HorizonColour = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionVectorParameter::StaticClass(), -460, 740));
		if (HorizonColour)
		{
			HorizonColour->ParameterName = TEXT("HorizonColor");
			HorizonColour->DefaultValue = FLinearColor(0.055f, 0.022f, 0.030f);
		}

		UMaterialExpressionLinearInterpolate* Blend = Cast<UMaterialExpressionLinearInterpolate>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionLinearInterpolate::StaticClass(), 280, 180));

		UMaterialExpressionScalarParameter* Opacity = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionScalarParameter::StaticClass(), 280, 420));
		if (Opacity)
		{
			Opacity->ParameterName = TEXT("SkyOpacity");
			Opacity->DefaultValue = 0.f;
		}

		if (Rotation && Zero && RotationOffset && UV && RotatedUV && Sample)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Rotation, TEXT(""), RotationOffset, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Zero, TEXT(""), RotationOffset, TEXT("B"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), RotatedUV, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(RotationOffset, TEXT(""), RotatedUV, TEXT("B"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(RotatedUV, TEXT(""), Sample, TEXT("Coordinates"));
		}
		if (Sample && Exposure && Exposed && Tint && Tinted)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Sample, TEXT("RGB"), Exposed, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Exposure, TEXT(""), Exposed, TEXT("B"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Exposed, TEXT(""), Tinted, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Tint, TEXT(""), Tinted, TEXT("B"));
		}
		if (RotatedUV && Vertical && OneMinusV && HorizonStart && AboveHorizon && HorizonSharpness && Band && Mask)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(RotatedUV, TEXT(""), Vertical, TEXT(""));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Vertical, TEXT(""), OneMinusV, TEXT(""));
			UMaterialEditingLibrary::ConnectMaterialExpressions(OneMinusV, TEXT(""), AboveHorizon, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(HorizonStart, TEXT(""), AboveHorizon, TEXT("B"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(AboveHorizon, TEXT(""), Band, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(HorizonSharpness, TEXT(""), Band, TEXT("B"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Band, TEXT(""), Mask, TEXT(""));
		}
		if (Tinted && HorizonColour && Mask && Blend)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Tinted, TEXT(""), Blend, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(HorizonColour, TEXT(""), Blend, TEXT("B"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(Mask, TEXT(""), Blend, TEXT("Alpha"));
			UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT(""), MP_EmissiveColor);
		}
		else if (Tinted)
		{
			UMaterialEditingLibrary::ConnectMaterialProperty(Tinted, TEXT(""), MP_EmissiveColor);
		}
		if (Opacity)
		{
			UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		}

		UMaterialEditingLibrary::RecompileMaterial(Material);
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Material, *Filename, SaveArgs);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Authored material %s (%s)"),
			*Material->GetPathName(), bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
		return Material;
	}

	/** Builds (or replaces) one unlit emissive material and saves it. */
	UMaterial* CreateEmissiveMaterial(const TCHAR* PackagePath, const TCHAR* AssetName, bool bPerInstanceRandom)
	{
		// Replace any previous version by deleting the file, so re-running the
		// commandlet is idempotent.
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		if (IFileManager::Get().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename, false, true, true);
		}

		UPackage* Package = CreatePackage(PackagePath);
		if (!Package)
		{
			UE_LOG(LogCitix, Error, TEXT("[Citix] Could not create package %s"), PackagePath);
			return nullptr;
		}

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(AssetName),
			RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!Material)
		{
			UE_LOG(LogCitix, Error, TEXT("[Citix] Failed to create material %s"), AssetName);
			return nullptr;
		}

		// Opaque + unlit + emissive: the surface glows on its own, and because it is
		// OPAQUE Lumen includes it in the surface cache, so lit windows contribute real
		// global illumination. (The engine's additive EmissiveMeshMaterial is
		// translucent, which Lumen ignores entirely - that was the original bug.)
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		Material->bUsedWithInstancedStaticMeshes = true;
		Material->bUsedWithStaticLighting = false;

		UMaterialExpressionVectorParameter* Colour = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionVectorParameter::StaticClass(), -520, 0));
		if (Colour)
		{
			Colour->ParameterName = TEXT("Color");
			Colour->DefaultValue = FLinearColor::White;
		}

		if (bPerInstanceRandom)
		{
			// Per-instance random scales each window band's brightness, so lit windows
			// vary naturally (some bright, some dim, a few effectively off) without any
			// extra draw calls or material instances.
			UMaterialExpressionPerInstanceRandom* Random = Cast<UMaterialExpressionPerInstanceRandom>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionPerInstanceRandom::StaticClass(), -520, 220));

			UMaterialExpressionMultiply* TintTimesRandom = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), -220, 60));

			if (Colour && Random && TintTimesRandom)
			{
				UMaterialEditingLibrary::ConnectMaterialExpressions(Colour, TEXT(""), TintTimesRandom, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(Random, TEXT(""), TintTimesRandom, TEXT("B"));
			}

			// Pane mask: split each glazing band into individual windows along the
			// facade. frac(U * PaneColumns) ramps across each pane; Step cuts a dark
			// mullion between them, so a band reads as a row of windows rather than a
			// continuous glowing strip. Purely procedural - no texture needed.
			UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionTextureCoordinate::StaticClass(), -980, 320));
			UMaterialExpressionComponentMask* U = Cast<UMaterialExpressionComponentMask>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionComponentMask::StaticClass(), -900, 320));
			UMaterialExpressionScalarParameter* Panes = Cast<UMaterialExpressionScalarParameter>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionScalarParameter::StaticClass(), -980, 520));
			UMaterialExpressionMultiply* PaneScale = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), -740, 380));
			UMaterialExpressionFrac* PaneFrac = Cast<UMaterialExpressionFrac>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionFrac::StaticClass(), -540, 380));
			UMaterialExpressionConstant* Gap = Cast<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionConstant::StaticClass(), -540, 560));
			UMaterialExpressionStep* PaneStep = Cast<UMaterialExpressionStep>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionStep::StaticClass(), -320, 400));

			if (Panes)
			{
				Panes->ParameterName = TEXT("PaneColumns");
				Panes->DefaultValue = 16.f;
			}
			if (Gap)
			{
				Gap->R = 0.20f;
			}

			if (U)
			{
				// ComponentMask defaults to all four channels on, which makes it a float2+
				// even after R is set. The pane hash must stay a pure scalar: mask only R,
				// or the final tint (float3) multiply fails with "float3 and float2 are
				// undefined" and the whole material silently falls back to Default Material.
				U->R = true;
				U->G = false;
				U->B = false;
				U->A = false;
			}

			if (UV && U && Panes && PaneScale && PaneFrac && Gap && PaneStep)
			{
				UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), U, TEXT(""));
				UMaterialEditingLibrary::ConnectMaterialExpressions(U, TEXT(""), PaneScale, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(Panes, TEXT(""), PaneScale, TEXT("B"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneScale, TEXT(""), PaneFrac, TEXT(""));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneFrac, TEXT(""), PaneStep, TEXT("X"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(Gap, TEXT(""), PaneStep, TEXT("Y"));
			}

			// Per-PANE variation. Without this every pane in a band shares one brightness
			// (only the whole band is randomised), which is why a facade read as a few
			// uniform glowing strips. PaneIndex = floor(U * PaneColumns) is hashed together
			// with the band's own random into 0..1, then mapped so that roughly
			// PaneOffFraction of panes are dark and the rest carry graded brightness. Still
			// one draw call and one material instance - it is all arithmetic.
			UMaterialExpressionFloor* PaneIndex = Cast<UMaterialExpressionFloor>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionFloor::StaticClass(), -740, 660));
			UMaterialExpressionMultiply* IndexScale = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), -560, 660));
			UMaterialExpressionMultiply* BandScale = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), -560, 800));
			UMaterialExpressionAdd* HashSum = Cast<UMaterialExpressionAdd>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionAdd::StaticClass(), -380, 720));
			UMaterialExpressionSine* HashSin = Cast<UMaterialExpressionSine>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionSine::StaticClass(), -230, 720));
			UMaterialExpressionMultiply* HashBig = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), -80, 720));
			UMaterialExpressionFrac* PaneHash = Cast<UMaterialExpressionFrac>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionFrac::StaticClass(), 70, 720));
			UMaterialExpressionScalarParameter* PaneOff = Cast<UMaterialExpressionScalarParameter>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionScalarParameter::StaticClass(), 70, 900));
			UMaterialExpressionSubtract* PaneLit = Cast<UMaterialExpressionSubtract>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionSubtract::StaticClass(), 230, 780));
			UMaterialExpressionConstant* PaneLitRange = Cast<UMaterialExpressionConstant>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionConstant::StaticClass(), 230, 940));
			UMaterialExpressionDivide* PaneLevel = Cast<UMaterialExpressionDivide>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionDivide::StaticClass(), 400, 820));
			UMaterialExpressionSaturate* PaneSat = Cast<UMaterialExpressionSaturate>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionSaturate::StaticClass(), 560, 820));
			UMaterialExpressionMultiply* Masked = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), 720, 460));

			const float PaneOffFraction = 0.30f;
			if (PaneOff)
			{
				PaneOff->ParameterName = TEXT("PaneOffFraction");
				PaneOff->DefaultValue = PaneOffFraction;
			}
			if (PaneLitRange)
			{
				PaneLitRange->R = 1.f - PaneOffFraction;
			}

			if (PaneIndex && PaneFrac && IndexScale && BandScale && Random && HashSum && HashSin
				&& HashBig && PaneHash && PaneOff && PaneLit && PaneLitRange && PaneLevel
				&& PaneSat && PaneStep && Masked)
			{
				// PaneIndex = floor(U * PaneColumns)
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneScale, TEXT(""), PaneIndex, TEXT(""));
				// hash = frac(sin(PaneIndex * 12.9898 + BandRandom * 78.233) * 43758.5453)
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneIndex, TEXT(""), IndexScale, TEXT("A"));
				IndexScale->ConstB = 12.9898f;
				UMaterialEditingLibrary::ConnectMaterialExpressions(Random, TEXT(""), BandScale, TEXT("A"));
				BandScale->ConstB = 78.233f;
				UMaterialEditingLibrary::ConnectMaterialExpressions(IndexScale, TEXT(""), HashSum, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(BandScale, TEXT(""), HashSum, TEXT("B"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(HashSum, TEXT(""), HashSin, TEXT(""));
				UMaterialEditingLibrary::ConnectMaterialExpressions(HashSin, TEXT(""), HashBig, TEXT("A"));
				HashBig->ConstB = 43758.5453f;
				UMaterialEditingLibrary::ConnectMaterialExpressions(HashBig, TEXT(""), PaneHash, TEXT(""));
				// level = saturate((hash - PaneOffFraction) / (1 - PaneOffFraction))
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneHash, TEXT(""), PaneLit, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneOff, TEXT(""), PaneLit, TEXT("B"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneLit, TEXT(""), PaneLevel, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneLitRange, TEXT(""), PaneLevel, TEXT("B"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneLevel, TEXT(""), PaneSat, TEXT(""));
				// mask = mullion step * per-pane level
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneStep, TEXT(""), Masked, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(PaneSat, TEXT(""), Masked, TEXT("B"));
			}

			// Final: tint * per-instance random * pane mask
			UMaterialExpressionMultiply* Final = Cast<UMaterialExpressionMultiply>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material, UMaterialExpressionMultiply::StaticClass(), 880, 120));
			if (Final)
			{
				if (TintTimesRandom)
				{
					UMaterialEditingLibrary::ConnectMaterialExpressions(TintTimesRandom, TEXT(""), Final, TEXT("A"));
				}
				// The pane mask is the horizontal break-up: mullion gaps plus per-pane
				// on/off, so one lit floor reads as a row of individual windows instead of
				// one continuous glowing stripe. It has to terminate in the emissive
				// output; wiring the raw per-instance random here instead (as the previous
				// revision did) dropped the mask entirely, which is why every lit floor
				// rendered as one solid slab.
				UMaterialEditingLibrary::ConnectMaterialExpressions(Masked, TEXT(""), Final, TEXT("B"));
				UMaterialEditingLibrary::ConnectMaterialProperty(Final, TEXT(""), MP_EmissiveColor);
			}
		}
		else if (Colour)
		{
			UMaterialEditingLibrary::ConnectMaterialProperty(Colour, TEXT(""), MP_EmissiveColor);
		}

		UMaterialEditingLibrary::RecompileMaterial(Material);
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Material, *Filename, SaveArgs);

		UE_LOG(LogCitix, Log, TEXT("[Citix] Authored material %s (%s)"),
			*Material->GetPathName(), bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
		return Material;
	}

	/**
	 * Water: an opaque lit material whose surface is the sum of ten directional waves in
	 * three layers, modulated by a slow large-scale macro field.
	 *
	 * WHY THIS WAS REBUILT
	 * The previous version used three sine waves at fixed frequencies, which is a periodic
	 * field. Periodic normals give periodic specular highlights, and that is exactly what
	 * read as "obviously tiled" and "repetitive bright dots". Nothing varied across the
	 * surface, roughness was constant, and the motion was three constant scrolls.
	 *
	 * WHAT IT DOES NOW
	 *   * TEN waves in three layers (swell / wind chop / fine ripple). Wavelengths are
	 *     deliberately incommensurate (79 cm to 15 m) so the combined pattern has no short
	 *     repeat; directions are spread around the compass; and each wave's phase speed
	 *     follows the real deep-water dispersion relation c = sqrt(g*lambda/2pi), so long
	 *     waves travel faster than ripples and the motion never looks synchronised.
	 *   * A MACRO field from four very long waves (70-240 m) drives three uncorrelated
	 *     channels: how strong the ripples are, how glossy the water is, and how deep it
	 *     looks. That is the uneven breakup - some patches are choppier, some glossier,
	 *     some darker, so the surface never behaves uniformly.
	 *   * Everything is evaluated from WORLD position, so there is no UV tiling at all:
	 *     the field is unique across the whole map by construction.
	 *   * Wave axes come from a per-instance flow direction, so motion runs along the river
	 *     instead of along a fixed world axis.
	 *   * Fresnel drives roughness: mirror-sharp at grazing angles, softer and more broken
	 *     head-on, which is what fixes the toy look when looking down at the water.
	 *   * Roughness also rises with distance, which kills ripple shimmer on distant water
	 *     instead of letting it crawl.
	 *
	 * COST
	 * No textures at all: ten cosines, four sines and roughly 250 ALU per pixel. That is
	 * cheaper than sampling three normal maps, and it cannot tile.
	 */
	UMaterial* CreateWaterMaterial(const TCHAR* PackagePath, const TCHAR* AssetName)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		if (IFileManager::Get().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename, false, true, true);
		}

		UPackage* Package = CreatePackage(PackagePath);
		if (!Package)
		{
			UE_LOG(LogCitix, Error, TEXT("[Citix] Could not create package %s"), PackagePath);
			return nullptr;
		}

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(AssetName),
			RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!Material)
		{
			UE_LOG(LogCitix, Error, TEXT("[Citix] Failed to create material %s"), AssetName);
			return nullptr;
		}

		// Opaque, default lit: opaque so Lumen keeps it in the surface cache and the
		// already-enabled Lumen reflections carry the skyline at no extra cost.
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->TwoSided = false;
		Material->bUsedWithInstancedStaticMeshes = true;
		Material->bUsedWithStaticLighting = false;

		int32 Connections = 0;
		int32 FailedConnections = 0;

		// ---- Builder helpers ---------------------------------------------
		auto New = [Material](UClass* Class, float X, float Y) -> UMaterialExpression*
		{
			return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
		};
		auto Connect = [&Connections, &FailedConnections](UMaterialExpression* From,
			UMaterialExpression* To, const TCHAR* ToInput)
		{
			if (!From || !To)
			{
				++FailedConnections;
				UE_LOG(LogCitix, Warning, TEXT("[Citix]   water: null expression for input '%s'"), ToInput);
				return;
			}
			if (UMaterialEditingLibrary::ConnectMaterialExpressions(From, TEXT(""), To, ToInput))
			{
				++Connections;
			}
			else
			{
				++FailedConnections;
				UE_LOG(LogCitix, Warning, TEXT("[Citix]   water: could not connect %s -> %s.%s"),
					*From->GetClass()->GetName(), *To->GetClass()->GetName(), ToInput);
			}
		};
		auto Scalar = [&New](const TCHAR* Name, float Value, float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpressionScalarParameter* E = Cast<UMaterialExpressionScalarParameter>(
				New(UMaterialExpressionScalarParameter::StaticClass(), X, Y));
			if (E)
			{
				E->ParameterName = Name;
				E->DefaultValue = Value;
			}
			return E;
		};
		auto Vector = [&New](const TCHAR* Name, const FLinearColor& Value, float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpressionVectorParameter* E = Cast<UMaterialExpressionVectorParameter>(
				New(UMaterialExpressionVectorParameter::StaticClass(), X, Y));
			if (E)
			{
				E->ParameterName = Name;
				E->DefaultValue = Value;
			}
			return E;
		};
		auto Constant = [&New](float Value, float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpressionConstant* E = Cast<UMaterialExpressionConstant>(
				New(UMaterialExpressionConstant::StaticClass(), X, Y));
			if (E)
			{
				E->R = Value;
			}
			return E;
		};
		auto Mask = [&New, &Connect](UMaterialExpression* Source, bool bR, bool bG, float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpressionComponentMask* E = Cast<UMaterialExpressionComponentMask>(
				New(UMaterialExpressionComponentMask::StaticClass(), X, Y));
			if (E)
			{
				E->R = bR ? 1 : 0;
				E->G = bG ? 1 : 0;
				E->B = 0;
				E->A = 0;
			}
			Connect(Source, E, TEXT(""));
			return E;
		};
		auto Binary = [&New, &Connect](UClass* Class, UMaterialExpression* A, UMaterialExpression* B,
			float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpression* E = New(Class, X, Y);
			Connect(A, E, TEXT("A"));
			Connect(B, E, TEXT("B"));
			return E;
		};
		auto Multiply = [&Binary](UMaterialExpression* A, UMaterialExpression* B, float X, float Y)
		{
			return Binary(UMaterialExpressionMultiply::StaticClass(), A, B, X, Y);
		};
		auto Add = [&Binary](UMaterialExpression* A, UMaterialExpression* B, float X, float Y)
		{
			return Binary(UMaterialExpressionAdd::StaticClass(), A, B, X, Y);
		};
		auto Divide = [&Binary](UMaterialExpression* A, UMaterialExpression* B, float X, float Y)
		{
			return Binary(UMaterialExpressionDivide::StaticClass(), A, B, X, Y);
		};
		auto MulConst = [&Multiply, &Constant](UMaterialExpression* Source, float Value, float X, float Y)
		{
			return Multiply(Source, Constant(Value, X, Y + 60.f), X, Y);
		};
		auto Lerp = [&New, &Connect](UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha,
			float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpressionLinearInterpolate* E = Cast<UMaterialExpressionLinearInterpolate>(
				New(UMaterialExpressionLinearInterpolate::StaticClass(), X, Y));
			Connect(A, E, TEXT("A"));
			Connect(B, E, TEXT("B"));
			Connect(Alpha, E, TEXT("Alpha"));
			return E;
		};
		auto Append = [&New, &Connect](UMaterialExpression* A, UMaterialExpression* B, float X, float Y)
		{
			UMaterialExpressionAppendVector* E = Cast<UMaterialExpressionAppendVector>(
				New(UMaterialExpressionAppendVector::StaticClass(), X, Y));
			Connect(A, E, TEXT("A"));
			Connect(B, E, TEXT("B"));
			return static_cast<UMaterialExpression*>(E);
		};
		auto Cosine = [&New, &Connect](UMaterialExpression* Source, float X, float Y)
		{
			UMaterialExpressionCosine* E = Cast<UMaterialExpressionCosine>(
				New(UMaterialExpressionCosine::StaticClass(), X, Y));
			Connect(Source, E, TEXT(""));
			return static_cast<UMaterialExpression*>(E);
		};
		auto Sine = [&New, &Connect](UMaterialExpression* Source, float X, float Y)
		{
			UMaterialExpressionSine* E = Cast<UMaterialExpressionSine>(
				New(UMaterialExpressionSine::StaticClass(), X, Y));
			Connect(Source, E, TEXT(""));
			return static_cast<UMaterialExpression*>(E);
		};

		// ---- Tunable parameters ------------------------------------------
		// Everything an artist needs is exposed; the defaults are the tuned values.
		UMaterialExpression* WaterColour = Vector(TEXT("WaterColor"),
			FLinearColor(0.0035f, 0.0100f, 0.0185f), -4600, -900);
		UMaterialExpression* WaterShallowColour = Vector(TEXT("WaterShallowColor"),
			FLinearColor(0.0125f, 0.0285f, 0.0400f), -4600, -760);
		UMaterialExpression* WaveScale = Scalar(TEXT("WaveScale"), 1.0f, -4600, -620);
		UMaterialExpression* WaveSpeed = Scalar(TEXT("WaveSpeed"), 1.0f, -4600, -480);
		UMaterialExpression* WaveStrength = Scalar(TEXT("WaveStrength"), 1.0f, -4600, -340);
		UMaterialExpression* SwellSpeed = Scalar(TEXT("SwellSpeed"), 1.0f, -4600, -200);
		UMaterialExpression* ChopSpeed = Scalar(TEXT("ChopSpeed"), 1.15f, -4600, -60);
		UMaterialExpression* RippleSpeed = Scalar(TEXT("RippleSpeed"), 1.30f, -4600, 80);
		UMaterialExpression* MacroScale = Scalar(TEXT("MacroScale"), 1.0f, -4600, 220);
		UMaterialExpression* MacroSpeed = Scalar(TEXT("MacroSpeed"), 1.0f, -4600, 360);
		UMaterialExpression* MacroStrengthMin = Scalar(TEXT("MacroStrengthMin"), 0.55f, -4600, 500);
		UMaterialExpression* MacroStrengthMax = Scalar(TEXT("MacroStrengthMax"), 1.45f, -4600, 640);
		UMaterialExpression* RoughnessMin = Scalar(TEXT("RoughnessMin"), 0.050f, -4600, 780);
		UMaterialExpression* RoughnessMax = Scalar(TEXT("RoughnessMax"), 0.165f, -4600, 920);
		UMaterialExpression* RoughnessAtGrazing = Scalar(TEXT("RoughnessAtGrazing"), 0.032f, -4600, 1060);
		UMaterialExpression* FresnelPower = Scalar(TEXT("FresnelPower"), 4.0f, -4600, 1200);
		UMaterialExpression* FresnelBase = Scalar(TEXT("FresnelBase"), 0.02f, -4600, 1340);
		UMaterialExpression* ReflectionStrength = Scalar(TEXT("ReflectionStrength"), 1.0f, -4600, 1480);
		UMaterialExpression* DistanceRoughness = Scalar(TEXT("DistanceRoughness"), 0.22f, -4600, 1620);
		UMaterialExpression* DistanceRange = Scalar(TEXT("DistanceRange"), 30000.f, -4600, 1760);

		// ---- Shared inputs -------------------------------------------------
		UMaterialExpression* WorldPos = New(UMaterialExpressionWorldPosition::StaticClass(), -4300, -600);
		UMaterialExpression* WorldXY = Mask(WorldPos, true, true, -4150, -600);

		UMaterialExpression* Time = New(UMaterialExpressionTime::StaticClass(), -4300, -400);

		// Per-instance flow direction: the river's local direction for this piece of water.
		UMaterialExpression* Flow = New(UMaterialExpressionPerInstanceCustomData3Vector::StaticClass(), -4300, -200);
		if (Cast<UMaterialExpressionPerInstanceCustomData3Vector>(Flow))
		{
			Cast<UMaterialExpressionPerInstanceCustomData3Vector>(Flow)->DataIndex = 0;
			Cast<UMaterialExpressionPerInstanceCustomData3Vector>(Flow)->ConstDefaultValue =
				FLinearColor(1.f, 0.f, 0.f, 0.f);
		}
		UMaterialExpression* FlowX = Mask(Flow, true, false, -4150, -200);
		UMaterialExpression* FlowY = Mask(Flow, false, true, -4150, -60);
		// Cross direction is the flow turned 90 degrees: (-flowY, flowX).
		UMaterialExpression* CrossX = MulConst(FlowY, -1.f, -4000, 60);
		UMaterialExpression* CrossY = FlowX;

		auto DirectionComponent = [&](float A, float B, bool bX, float X, float Y) -> UMaterialExpression*
		{
			UMaterialExpression* Along = bX ? FlowX : FlowY;
			UMaterialExpression* Across = bX ? CrossX : CrossY;
			if (FMath::IsNearlyZero(B))
			{
				return FMath::IsNearlyEqual(A, 1.f) ? Along : MulConst(Along, A, X, Y);
			}
			if (FMath::IsNearlyZero(A))
			{
				return FMath::IsNearlyEqual(B, 1.f) ? Across : MulConst(Across, B, X, Y);
			}
			return Add(MulConst(Along, A, X, Y), MulConst(Across, B, X, Y - 40.f), X, Y);
		};

		// Per-layer time, so each layer has its own speed on top of the global one.
		UMaterialExpression* SwellTime = Multiply(Time, Multiply(WaveSpeed, SwellSpeed, -4150, -400), -3950, -400);
		UMaterialExpression* ChopTime = Multiply(Time, Multiply(WaveSpeed, ChopSpeed, -4150, -260), -3950, -260);
		UMaterialExpression* RippleTime = Multiply(Time, Multiply(WaveSpeed, RippleSpeed, -4150, -120), -3950, -120);
		UMaterialExpression* MacroTime = Multiply(Time, Multiply(WaveSpeed, MacroSpeed, -4150, 20), -3950, 20);

		// ---- Wave table ----------------------------------------------------
		// DirA/DirB are the direction in the (downstream, cross-stream) basis. Wavelength is
		// in cm. Slope is the peak surface slope this wave contributes, so the sum of the
		// slopes is roughly the steepest the surface can get.
		struct FWaveDesc
		{
			float DirA;
			float DirB;
			float Wavelength;
			float Slope;
			int32 Layer;   // 0 swell, 1 wind chop, 2 ripple
		};
		static const FWaveDesc Waves[] =
		{
			// Swell: long and slow, carrying most of the surface tilt.
			{  1.00f,  0.00f, 1100.f, 0.055f, 0 },
			{  0.94f,  0.34f,  745.f, 0.045f, 0 },
			{  0.87f, -0.50f, 1490.f, 0.042f, 0 },
			// Wind chop: shorter and faster, crossing the swell.
			{  0.50f,  0.87f,  362.f, 0.050f, 1 },
			{  0.17f,  0.98f,  259.f, 0.042f, 1 },
			{  0.77f,  0.64f,  441.f, 0.036f, 1 },
			// Ripple: fine detail, spread right around the compass.
			{  0.00f,  1.00f,  151.f, 0.028f, 2 },
			{ -0.42f,  0.91f,  104.f, 0.022f, 2 },
			{  0.34f, -0.94f,  178.f, 0.020f, 2 },
			{ -0.77f,  0.64f,   79.f, 0.016f, 2 }
		};

		constexpr float Gravity = 980.f;
		UMaterialExpression* GradientX = nullptr;
		UMaterialExpression* GradientY = nullptr;

		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Waves); ++Index)
		{
			const FWaveDesc& Wave = Waves[Index];
			const float RowY = -1000.f + Index * 270.f;
			const float WaveNumber = 2.f * PI / Wave.Wavelength;
			// Deep-water dispersion: long waves travel faster, so the layers never lock
			// into step with each other.
			const float PhaseSpeed = FMath::Sqrt(Gravity * Wave.Wavelength / (2.f * PI));
			const float AngularSpeed = PhaseSpeed * WaveNumber;

			UMaterialExpression* DirX = DirectionComponent(Wave.DirA, Wave.DirB, true, -3600, RowY);
			UMaterialExpression* DirY = DirectionComponent(Wave.DirA, Wave.DirB, false, -3600, RowY + 120.f);

			// phase = dot(worldXY, direction) * (k * WaveScale) + layerTime * (c * k)
			UMaterialExpression* Dot = Binary(UMaterialExpressionDotProduct::StaticClass(),
				Append(DirX, DirY, -3440, RowY + 60.f), WorldXY, -3280, RowY + 60.f);
			UMaterialExpression* Frequency = Multiply(WaveScale, Constant(WaveNumber, -3280, RowY - 40.f),
				-3120, RowY - 40.f);
			UMaterialExpression* Scaled = Multiply(Dot, Frequency, -2960, RowY + 20.f);
			UMaterialExpression* SpeedTerm = Multiply(
				Wave.Layer == 0 ? SwellTime : (Wave.Layer == 1 ? ChopTime : RippleTime),
				Constant(AngularSpeed, -3280, RowY + 160.f), -3120, RowY + 160.f);
			UMaterialExpression* Phase = Add(Scaled, SpeedTerm, -2800, RowY + 80.f);

			// Only the cosine is needed: the gradient of a sine is a cosine.
			UMaterialExpression* Cos = Cosine(Phase, -2630, RowY + 80.f);

			// Gradient contribution: slope * direction * cos(phase)
			UMaterialExpression* Slope = Constant(Wave.Slope, -2460, RowY + 200.f);
			UMaterialExpression* TermX = Multiply(Multiply(DirX, Slope, -2300, RowY + 40.f), Cos, -2140, RowY + 40.f);
			UMaterialExpression* TermY = Multiply(Multiply(DirY, Slope, -2300, RowY + 160.f), Cos, -2140, RowY + 160.f);

			GradientX = GradientX ? Add(GradientX, TermX, -1980, RowY) : TermX;
			GradientY = GradientY ? Add(GradientY, TermY, -1980, RowY + 140.f) : TermY;
		}

		// ---- Macro field: the uneven, large-scale breakup --------------------
		// Four very long waves (70-240 m) give three uncorrelated 0..1 channels that drive
		// ripple strength, glossiness and apparent depth.
		struct FMacroDesc { float DirA; float DirB; float Wavelength; };
		static const FMacroDesc MacroWaves[] =
		{
			{  1.00f,  0.00f,  70000.f },
			{  0.62f,  0.78f, 113000.f },
			{  0.90f, -0.44f, 167000.f },
			{  0.30f,  0.95f, 241000.f }
		};

		UMaterialExpression* MacroSines[4] = { nullptr, nullptr, nullptr, nullptr };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(MacroWaves); ++Index)
		{
			const FMacroDesc& Macro = MacroWaves[Index];
			const float RowY = 2300.f + Index * 210.f;
			const float WaveNumber = 2.f * PI / Macro.Wavelength;
			// Macro features drift very slowly: about 1.4 m/s of phase speed.
			const float AngularSpeed = 140.f * WaveNumber;

			UMaterialExpression* DirX = DirectionComponent(Macro.DirA, Macro.DirB, true, -3600, RowY);
			UMaterialExpression* DirY = DirectionComponent(Macro.DirA, Macro.DirB, false, -3600, RowY + 100.f);
			UMaterialExpression* Dot = Binary(UMaterialExpressionDotProduct::StaticClass(),
				Append(DirX, DirY, -3440, RowY + 50.f), WorldXY, -3280, RowY + 50.f);
			UMaterialExpression* Scaled = Multiply(Dot,
				Multiply(MacroScale, Constant(WaveNumber, -3280, RowY - 50.f), -3120, RowY - 50.f),
				-2960, RowY + 20.f);
			UMaterialExpression* Phase = Add(Scaled,
				Multiply(MacroTime, Constant(AngularSpeed, -3280, RowY + 140.f), -3120, RowY + 140.f),
				-2800, RowY + 80.f);
			MacroSines[Index] = Sine(Phase, -2630, RowY + 80.f);
		}

		auto MacroChannel = [&Add, &Multiply, &MulConst](UMaterialExpression* A, float Wa,
			UMaterialExpression* B, float Wb, UMaterialExpression* C, float Wc, float X, float Y)
		{
			return Add(Add(MulConst(A, Wa, X, Y), MulConst(B, Wb, X, Y), X, Y + 60.f),
				MulConst(C, Wc, X, Y + 120.f), X, Y + 140.f);
		};

		// Three different weightings of the same four waves, so the channels do not line up.
		UMaterialExpression* StrengthNoise = MacroChannel(MacroSines[0], 0.50f, MacroSines[1], 0.30f,
			MacroSines[2], 0.20f, -2300, 2300);
		UMaterialExpression* RoughNoise = MacroChannel(MacroSines[2], 0.45f, MacroSines[3], 0.35f,
			MacroSines[1], 0.20f, -2300, 2560);
		UMaterialExpression* ColourNoise = MacroChannel(MacroSines[3], 0.45f, MacroSines[0], 0.35f,
			MacroSines[2], 0.20f, -2300, 2820);

		auto ToUnit = [&Multiply, &Add, &Constant] (UMaterialExpression* Signed, float X, float Y)
		{
			// 0.5 + 0.5 * signed -> a smooth 0..1 macro channel.
			return Add(Multiply(Signed, Constant(0.5f, X, Y + 60.f), X, Y), Constant(0.5f, X, Y + 120.f), X + 150, Y);
		};
		UMaterialExpression* StrengthUnit = ToUnit(StrengthNoise, -1900, 2300);
		UMaterialExpression* RoughUnit = ToUnit(RoughNoise, -1900, 2560);
		UMaterialExpression* ColourUnit = ToUnit(ColourNoise, -1900, 2820);

		// ---- Normal --------------------------------------------------------
		{
			UMaterialExpression* Strength = Multiply(WaveStrength,
				Lerp(MacroStrengthMin, MacroStrengthMax, StrengthUnit, -1600, 2300), -1400, 2300);

			UMaterialExpression* Nx = Multiply(GradientX, Strength, -1200, -1000);
			UMaterialExpression* Ny = Multiply(GradientY, Strength, -1200, -800);
			UMaterialExpression* Nz = Constant(1.f, -1200, -600);

			// World-space perturbed normal, then rotated into the surface's tangent frame,
			// because the Normal input is tangent space. Using a world-space gradient keeps
			// the ripples continuous across every piece of the river.
			UMaterialExpression* XY = Append(Nx, Ny, -1000, -900);
			UMaterialExpression* XYZ = Append(XY, Nz, -850, -850);
			UMaterialExpressionNormalize* Normalised = Cast<UMaterialExpressionNormalize>(
				New(UMaterialExpressionNormalize::StaticClass(), -700, -850));
			Connect(XYZ, Normalised, TEXT(""));
			UMaterialExpressionTransform* ToTangent = Cast<UMaterialExpressionTransform>(
				New(UMaterialExpressionTransform::StaticClass(), -550, -850));
			if (ToTangent)
			{
				ToTangent->TransformSourceType = TRANSFORMSOURCE_World;
				ToTangent->TransformType = TRANSFORM_Tangent;
			}
			Connect(Normalised, ToTangent, TEXT(""));
			UMaterialEditingLibrary::ConnectMaterialProperty(ToTangent, TEXT(""), MP_Normal);
			++Connections;
		}

		// ---- Roughness: varied, sharper at grazing, softer with distance ----
		{
			// Fresnel from the view angle: 0 looking straight down, 1 at grazing.
			UMaterialExpressionFresnel* Fresnel = Cast<UMaterialExpressionFresnel>(
				New(UMaterialExpressionFresnel::StaticClass(), -1600, 2650));
			Connect(FresnelPower, Fresnel, TEXT("ExponentIn"));
			Connect(FresnelBase, Fresnel, TEXT("BaseReflectFractionIn"));

			UMaterialExpression* Base = Lerp(RoughnessMin, RoughnessMax, RoughUnit, -1400, 2600);
			// Grazing angles are the mirror: sharper there, softer and more broken head-on.
			UMaterialExpression* Angled = Lerp(Base, RoughnessAtGrazing, Fresnel, -1200, 2620);

			// Distance softening: sub-pixel ripples would otherwise crawl and shimmer.
			UMaterialExpression* Depth = New(UMaterialExpressionPixelDepth::StaticClass(), -1600, 2850);
			UMaterialExpression* Fade = Divide(Depth, Add(Depth, DistanceRange, -1400, 2900), -1250, 2850);
			UMaterialExpression* Final = Add(Angled, Multiply(Fade, DistanceRoughness, -1000, 2850), -850, 2700);

			UMaterialEditingLibrary::ConnectMaterialProperty(Final, TEXT(""), MP_Roughness);
			++Connections;
		}

		// ---- Base colour: deeper reading patches are darker -----------------
		{
			UMaterialExpression* Colour = Lerp(WaterColour, WaterShallowColour, ColourUnit, -1600, 2450);
			UMaterialEditingLibrary::ConnectMaterialProperty(Colour, TEXT(""), MP_BaseColor);
			++Connections;
		}

		// Water is a dielectric: full specular, no metallic. ReflectionStrength lets the
		// artist push the reflection a little past physical if the scene calls for it.
		UMaterialEditingLibrary::ConnectMaterialProperty(ReflectionStrength, TEXT(""), MP_Specular);
		UMaterialEditingLibrary::ConnectMaterialProperty(Constant(0.f, -850, 2400), TEXT(""), MP_Metallic);
		Connections += 2;

		UMaterialEditingLibrary::RecompileMaterial(Material);
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Material, *Filename, SaveArgs);

		UE_LOG(LogCitix, Log, TEXT("[Citix] Authored material %s (%s), %d connections, %d failed"),
			*Material->GetPathName(), bSaved ? TEXT("saved") : TEXT("SAVE FAILED"), Connections, FailedConnections);
		return Material;
	}
 UMaterial* CreateChaseSmokeMaterial()
 {
  const FString Path=TEXT("/Game/Citix/Materials/M_CitixChaseSmoke");
  UPackage* Package=CreatePackage(*Path);
  auto* Material=FindObject<UMaterial>(Package,TEXT("M_CitixChaseSmoke"));
  if (!Material) Material=NewObject<UMaterial>(Package,TEXT("M_CitixChaseSmoke"),RF_Public|RF_Standalone);
  Material->GetExpressionCollection().Empty();
  Material->BlendMode=BLEND_Translucent; Material->SetShadingModel(MSM_Unlit); Material->TwoSided=false; Material->bUsedWithInstancedStaticMeshes=true;
  auto* Colour=Cast<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionConstant3Vector::StaticClass())); Colour->Constant=FLinearColor(.24f,.24f,.24f);
  auto* Fade=Cast<UMaterialExpressionPerInstanceCustomData>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionPerInstanceCustomData::StaticClass())); Fade->DataIndex=0;
  UMaterialEditingLibrary::ConnectMaterialProperty(Colour,TEXT(""),MP_EmissiveColor);
  UMaterialEditingLibrary::ConnectMaterialProperty(Fade,TEXT(""),MP_Opacity);
  UMaterialEditingLibrary::RecompileMaterial(Material); Package->MarkPackageDirty();
  FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone;
  const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
  if (!UPackage::SavePackage(Package,Material,*Filename,Save)) return nullptr;

  const FString MeshPath=TEXT("/Game/Citix/Meshes/SM_CitixSmokeBillow");
  UPackage* MeshPackage=CreatePackage(*MeshPath);
  auto* Mesh=FindObject<UStaticMesh>(MeshPackage,TEXT("SM_CitixSmokeBillow"));
  if (!Mesh) Mesh=NewObject<UStaticMesh>(MeshPackage,TEXT("SM_CitixSmokeBillow"),RF_Public|RF_Standalone);
  FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
  Attributes.GetVertexInstanceUVs().SetNumChannels(1);
  auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals(); auto Colours=Attributes.GetVertexInstanceColors(); auto UVs=Attributes.GetVertexInstanceUVs();
  const auto Group=Description.CreatePolygonGroup(); Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Smoke");
  const float T=(1.f+FMath::Sqrt(5.f))*.5f;
  const FVector3f V[]={ {-1,T,0},{1,T,0},{-1,-T,0},{1,-T,0},{0,-1,T},{0,1,T},{0,-1,-T},{0,1,-T},{T,0,-1},{T,0,1},{-T,0,-1},{-T,0,1} };
  const int Faces[][3]={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
  for (const auto& Face:Faces) {
   FVector3f P[3]={V[Face[0]].GetSafeNormal()*50.f,V[Face[1]].GetSafeNormal()*50.f,V[Face[2]].GetSafeNormal()*50.f};
   FVector3f N=FVector3f::CrossProduct(P[1]-P[0],P[2]-P[0]).GetSafeNormal();
   if (FVector3f::DotProduct(N,P[0])<0) { Swap(P[1],P[2]); N=-N; }
   const float Shade=.18f+.11f*FMath::Clamp(FVector3f::DotProduct(N,FVector3f(-.3f,-.4f,1.f).GetSafeNormal())*.5f+.5f,0.f,1.f);
   TArray<FVertexInstanceID> Triangle;
   for (int I=0;I<3;++I) { const auto Vertex=Description.CreateVertex(); Positions[Vertex]=P[I]; const auto Instance=Description.CreateVertexInstance(Vertex); Normals[Instance]=N; Colours[Instance]=FVector4f(Shade,Shade*1.015f,Shade*1.03f,1.f); UVs.Set(Instance,0,FVector2f(I==1?1.f:0.f,I==2?1.f:0.f)); Triangle.Add(Instance); }
   Description.CreatePolygon(Group,Triangle);
  }
  Mesh->GetStaticMaterials().Reset(); Mesh->GetStaticMaterials().Add(FStaticMaterial(Material,TEXT("Smoke")));
  UStaticMesh::FBuildMeshDescriptionsParams Build; Build.bBuildSimpleCollision=false; Build.bCommitMeshDescription=true;
  if (!Mesh->BuildFromMeshDescriptions({&Description},Build)) return nullptr;
  MeshPackage->MarkPackageDirty();
  const FString MeshFilename=FPackageName::LongPackageNameToFilename(MeshPath,FPackageName::GetAssetPackageExtension()); IFileManager::Get().MakeDirectory(*FPaths::GetPath(MeshFilename),true);
  const bool Saved=UPackage::SavePackage(MeshPackage,Mesh,*MeshFilename,Save);
  UE_LOG(LogCitix,Log,TEXT("[CitixSmoke] Authored 20-triangle faceted billow and material: %s"),Saved ? TEXT("saved") : TEXT("FAILED"));
  return Saved ? Material : nullptr;
 }

 bool CreateIceAndSignMaterials()
 {
  bool Saved=true;
  for (int32 Kind=0;Kind<4;++Kind) {
   const TCHAR* Names[]={TEXT("M_CitixIceScan"),TEXT("M_CitixIceFrost"),TEXT("M_CitixIceShell"),TEXT("M_YienSignText")};
   const FString Path=FString(TEXT("/Game/Citix/Materials/"))+Names[Kind];
   auto* Package=CreatePackage(*Path);
   auto* M=FindObject<UMaterial>(Package,Names[Kind]);
   if (Kind==3) {
    // Preserve Unreal's distance-field smoothing and font-page sampling graph.
    if (M) M->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    M=DuplicateObject<UMaterial>(LoadObject<UMaterial>(nullptr,TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque")),Package,Names[Kind]);
    M->SetFlags(RF_Public|RF_Standalone);
    M->SetShadingModel(MSM_Unlit);
    auto* Vertex=Cast<UMaterialExpressionVertexColor>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionVertexColor::StaticClass()));
    if (!UMaterialEditingLibrary::ConnectMaterialProperty(Vertex,TEXT(""),MP_EmissiveColor)) return false;
    UMaterialEditingLibrary::RecompileMaterial(M);
    Package->MarkPackageDirty();
    FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone;
    const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
    Saved &= UPackage::SavePackage(Package,M,*Filename,Save);
    continue;
   }
   if (!M) M=NewObject<UMaterial>(Package,Names[Kind],RF_Public|RF_Standalone);
   M->GetExpressionCollection().Empty(); M->TwoSided=true;
   M->BlendMode=Kind==0 ? BLEND_Additive : Kind==3 ? BLEND_Masked : BLEND_Translucent;
   M->SetShadingModel(MSM_Unlit); M->bUsedWithInstancedStaticMeshes=Kind==0;
   if (Kind==1) { M->MaterialDomain=MD_DeferredDecal; M->SetShadingModel(MSM_DefaultLit); }
   auto* Colour=Cast<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionConstant3Vector::StaticClass()));
   Colour->Constant=Kind==0 ? FLinearColor(.12f,1.3f,1.9f) : Kind==1 ? FLinearColor(.05f,.12f,.16f) : FLinearColor(.22f,.65f,.85f);
   UMaterialEditingLibrary::ConnectMaterialProperty(Colour,TEXT(""),MP_EmissiveColor);
   {
    auto* Fade=Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionScalarParameter::StaticClass())); Fade->ParameterName=TEXT("Fade"); Fade->DefaultValue=1.f;
    auto* Opacity=Cast<UMaterialExpressionCustom>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionCustom::StaticClass())); Opacity->OutputType=CMOT_Float1;
    auto Input=[&](const TCHAR* Name,UMaterialExpression* Expr) { FCustomInput I; I.InputName=Name; I.Input.Connect(0,Expr); Opacity->Inputs.Add(I); };
    Opacity->Inputs.Reset();
    if (Kind==0) {
     auto* UV=Cast<UMaterialExpressionTextureCoordinate>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionTextureCoordinate::StaticClass()));
     auto* Alpha=Cast<UMaterialExpressionPerInstanceCustomData>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionPerInstanceCustomData::StaticClass())); Alpha->DataIndex=0;
     auto* Mode=Cast<UMaterialExpressionPerInstanceCustomData>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionPerInstanceCustomData::StaticClass())); Mode->DataIndex=1;
     Input(TEXT("UV"),UV); Input(TEXT("Fade"),Alpha); Input(TEXT("Mode"),Mode);
     Opacity->Code=TEXT("float2 p=UV*2-1; if(Mode>1.5) return Fade*.8; if(Mode>.5) { float plume=pow(saturate(1-abs(p.y+sin(p.x*7)*.08)*1.6),2); return plume*pow(saturate(1-abs(p.x)),1.7)*Fade; } return pow(saturate(1-abs(p.x)),5)*saturate((1-abs(p.y))*8)*Fade;");
    } else {
     auto* Position=Cast<UMaterialExpressionWorldPosition>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionWorldPosition::StaticClass()));
     Input(TEXT("Position"),Position); Input(TEXT("Fade"),Fade);
     FString Code=TEXT("float2 p=Position.xy*.016; float2 tile=floor(p), f=frac(p); float first=10, second=10; for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) { float2 g=float2(x,y); float2 h=frac(sin(float2(dot(tile+g,float2(127.1,311.7)),dot(tile+g,float2(269.5,183.3))))*43758.5453); float d=length(g+h-f); if(d<first) {second=first; first=d;} else second=min(second,d); } float vein=pow(saturate(1-(second-first)*24),2); float grain=frac(sin(dot(floor(p*13),float2(12.9898,78.233)))*43758.5453); float frost=(.03+vein*.34+pow(grain,8)*.06)*Fade;");
     if (Kind==1) {
      auto* Origin=Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionVectorParameter::StaticClass())); Origin->ParameterName=TEXT("Origin");
      auto* Radius=Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionScalarParameter::StaticClass())); Radius->ParameterName=TEXT("Radius");
      Input(TEXT("Origin"),Origin); Input(TEXT("Radius"),Radius);
      auto* InnerRadius=Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionScalarParameter::StaticClass())); InnerRadius->ParameterName=TEXT("InnerRadius"); InnerRadius->DefaultValue=0.f; Input(TEXT("InnerRadius"),InnerRadius);
      auto* Forward=Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionVectorParameter::StaticClass())); Forward->ParameterName=TEXT("Forward"); Forward->DefaultValue=FLinearColor(1,0,0,0); Input(TEXT("Forward"),Forward);
      Code=TEXT("float2 delta=Position.xy-Origin.xy; float distance=length(delta); float angle=dot(delta/max(distance,.01),Forward.xy); if(distance>=Radius || distance<=InnerRadius || angle<=.49) return 0; ")+Code;
      Code+=TEXT("frost*=saturate((Radius-distance)/140)*smoothstep(InnerRadius,InnerRadius+100,distance)*smoothstep(.49,.54,angle)*smoothstep(180,380,distance);");
      auto* Base=Cast<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionConstant3Vector::StaticClass())); Base->Constant=FLinearColor(.4f,.72f,.85f);
      UMaterialEditingLibrary::ConnectMaterialProperty(Base,TEXT(""),MP_BaseColor);
      auto* Rough=Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionConstant::StaticClass())); Rough->R=.65f;
      UMaterialEditingLibrary::ConnectMaterialProperty(Rough,TEXT(""),MP_Roughness);
     }
     Opacity->Code=Code+TEXT("return frost;");
    }
    UMaterialEditingLibrary::ConnectMaterialProperty(Opacity,TEXT(""),MP_Opacity);
   }
   UMaterialEditingLibrary::RecompileMaterial(M); Package->MarkPackageDirty();
   FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone;
   const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
   Saved &= UPackage::SavePackage(Package,M,*Filename,Save);
   UE_LOG(LogCitix,Log,TEXT("[CitixIce] authored material %s"),Names[Kind]);
  }
  return Saved;
 }

}

int32 UCitixMaterialSetupCommandlet::Main(const FString& Params)
{
	if (Params.Contains(TEXT("RoadsideOnly"))) return CitixCreateRoadsideAssets() ? 0 : 1;
	if (Params.Contains(TEXT("IceOnly"))) return CreateIceAndSignMaterials() ? 0 : 1;
	if (Params.Contains(TEXT("SmokeOnly"))) return CreateChaseSmokeMaterial() ? 0 : 1;
	UE_LOG(LogCitix, Log, TEXT("[Citix] Authoring night lighting materials..."));

	CreateEmissiveMaterial(WindowMaterialPath, TEXT("M_CitixWindow"), /*bPerInstanceRandom*/ true);
	CreateEmissiveMaterial(EmissiveMaterialPath, TEXT("M_CitixEmissive"), /*bPerInstanceRandom*/ false);
	CreateWaterMaterial(WaterMaterialPath, TEXT("M_CitixWater"));
	// The night sky: import the linear HDR panorama, then author the fading dome material.
	ImportSkyTexture();
	CreateSkyMaterial(SkyMaterialPath, TEXT("M_CitixSky"), SkyTexturePath);

	UE_LOG(LogCitix, Log, TEXT("[Citix] Material setup complete."));
	return 0;
}

#else

int32 UCitixMaterialSetupCommandlet::Main(const FString& Params)
{
	UE_LOG(LogCitix, Error, TEXT("CitixMaterialSetup is editor-only."));
	return 1;
}

#endif
