// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/CitixTimeOfDay.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChaseRules.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CitixSurfaceLibrary.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Citix.h"

ACitixTimeOfDay::ACitixTimeOfDay()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	// A root is required for replication (relevancy needs a location).
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Multiplayer stage 1: the server owns the clock; clients render it.
	// Always relevant: the whole city keys off this one actor.
	bReplicates = true;
	bAlwaysRelevant = true;
}

void ACitixTimeOfDay::BeginPlay()
{
	Super::BeginPlay();

	// Allow a fixed time from the command line, e.g. -CitixHour=21.5 for night shots.
	float CommandHour = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("CitixHour="), CommandHour))
	{
		StartHours = CommandHour;
	}

	Hours = FMath::Fmod(FMath::Max(0.f, StartHours), 24.f);
	ResolveSceneActors();
	UpdateSky();
}

void ACitixTimeOfDay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const ACitixChaseGameState* Chase = GetWorld() ? GetWorld()->GetGameState<ACitixChaseGameState>() : nullptr)
	{
		// A locally generated clock must render the same server epoch as the host.
		if (!bResolved) ResolveSceneActors();
		Hours = FCitixChaseRules::SceneHour(Chase->GetServerWorldTimeSeconds());
		UpdateSky();
		return;
	}

	if (!bResolved)
	{
		ResolveSceneActors();
	}

	// Only the authority advances the shared clock. NOTE: net-mode check, not
	// HasAuthority(): a client-spawned clock reports local authority.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		// Rank 9: retire the bootstrap spare. The client spawns a local clock
		// so early frames have light; once the server copy arrives (simulated
		// role here, versus Authority for the locally spawned one), the spare
		// destroys itself so Find() can never bind weather/HUD to a dead clock.
		if (GetLocalRole() == ROLE_Authority)
		{
			for (TActorIterator<ACitixTimeOfDay> It(GetWorld()); It; ++It)
			{
				if (*It && *It != this && (*It)->GetLocalRole() != ROLE_Authority)
				{
					UE_LOG(LogCitix, Log, TEXT("[Citix] Retiring the local clock spare."));
					Destroy();
					return;
				}
			}
		}
		return;
	}

	if (bPaused || !bAutoAdvance || DayLengthMinutes <= 0.f)
	{
		return;
	}

	const float HoursPerSecond = 24.f / (DayLengthMinutes * 60.f);
	// Pacing by band: long golden hours, normal night, short day.
	const bool bDawn = (Hours >= 4.5f && Hours < 7.5f);
	const bool bDusk = (Hours >= 16.5f && Hours < 19.5f);
	const bool bNight = (Hours >= 19.5f || Hours < 4.5f);
	float RateScale = 0.5f; // day: twice as fast
	if (bDawn || bDusk)
	{
		RateScale = 3.f; // dawn/dusk: three times as long
	}
	else if (bNight)
	{
		RateScale = FMath::Max(1.f, NightDurationScale);
	}
	Hours = FMath::Fmod(Hours + (HoursPerSecond / RateScale) * DeltaSeconds, 24.f);
	UpdateSky();
}

void ACitixTimeOfDay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixTimeOfDay, Hours);
	DOREPLIFETIME(ACitixTimeOfDay, RainIntensity);
}

void ACitixTimeOfDay::SetHours(float InHours)
{
	// The replicated server clock is the only clock in multiplayer. A client can
	// still render the replicated value through OnRep_TimeState, but may not
	// locally scrub its copy out of sync.
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	Hours = FMath::Fmod(FMath::Max(0.f, InHours), 24.f);
	UpdateSky();
}

void ACitixTimeOfDay::SetRainIntensity(float InIntensity){
	const float Clamped = FMath::Clamp(InIntensity, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Clamped, RainIntensity))
	{
		return;
	}
	RainIntensity = Clamped;
	UpdateSky();
}

FString ACitixTimeOfDay::GetTimeString() const
{
	const int32 WholeHours = FMath::FloorToInt(Hours) % 24;
	const int32 Minutes = FMath::FloorToInt((Hours - FMath::FloorToFloat(Hours)) * 60.f) % 60;
	return FString::Printf(TEXT("%02d:%02d"), WholeHours, Minutes);
}

void ACitixTimeOfDay::ResolveSceneActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Reuse the map's directional light as the sun when there is one.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		SunLight = *It;
		break;
	}
	if (!SunLight)
	{
		SunLight = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, Params);
	}
	if (SunLight)
	{
		SunLight->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			Sun->SetMobility(EComponentMobility::Movable);
			// The sky atmosphere uses this light as its sun.
			Sun->SetAtmosphereSunLight(true);
			Sun->SetAtmosphereSunLightIndex(0);
			Sun->SetDynamicShadowDistanceMovableLight(18000.f);
			// Only one directional light may drive forward shading / translucency.
			Sun->SetForwardShadingPriority(10);
		}
	}

	// A second directional light stands in for the moon at night.
	MoonLight = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, Params);
	if (MoonLight)
	{
		MoonLight->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* Moon = Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			Moon->SetMobility(EComponentMobility::Movable);
			// Secondary atmosphere sun so the night sky scatters a little moonlight
			// instead of going pure black.
			Moon->SetAtmosphereSunLight(true);
			Moon->SetAtmosphereSunLightIndex(1);
			Moon->SetDynamicShadowDistanceMovableLight(12000.f);
			Moon->SetForwardShadingPriority(0);
		}
	}

	// Sky light must capture in real time so ambient follows the sky.
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		SkyLight = *It;
		break;
	}
	if (SkyLight)
	{
		if (USkyLightComponent* Sky = SkyLight->GetLightComponent())
		{
			Sky->SetMobility(EComponentMobility::Movable);
			Sky->SetRealTimeCapture(true);
		}
	}

	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		HeightFog = *It;
		break;
	}

	// An unbound post process volume keeps auto exposure from lifting the night
	// scene up to daylight brightness.
	PostProcessVolume = World->SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), FTransform::Identity, Params);
	if (PostProcessVolume)
	{
		PostProcessVolume->bUnbound = true;
		PostProcessVolume->Priority = 1.f;
		PostProcessVolume->BlendWeight = 1.f;
		PostProcessVolume->Settings.bOverride_AutoExposureBias = true;
		// Bloom is what makes lit windows bleed and read as a dazzling skyline from
		// a distance rather than as isolated dots.
		PostProcessVolume->Settings.bOverride_BloomIntensity = true;
	}

	bResolved = true;

	if (bUseNightSkyDome)
	{
		CreateNightSkyDome();
	}

	UE_LOG(LogCitix, Log, TEXT("[Citix] TimeOfDay ready (sun=%s, moon=%s, sky=%s, fog=%s, nightSky=%s)."),
		SunLight ? TEXT("yes") : TEXT("no"),
		MoonLight ? TEXT("yes") : TEXT("no"),
		SkyLight ? TEXT("yes") : TEXT("no"),
		HeightFog ? TEXT("yes") : TEXT("no"),
		NightSkyDome ? TEXT("yes") : TEXT("no"));
}

void ACitixTimeOfDay::CreateNightSkyDome()
{
	if (NightSkyDome)
	{
		return;
	}

	// An inverted sky sphere carries the photographic cloud layer. SkyAtmosphere remains
	// underneath it and is revealed through the material's opacity during daylight.
	UStaticMesh* SkyMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
	// Preferred: the authored sky, which samples a real HDR cloud panorama and blends it into
	// a warm city-glow horizon. Falls back to the engine's procedural sky if it is absent.
	UMaterialInterface* AuthoredSky = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Citix/Materials/M_CitixSky.M_CitixSky"));
	UMaterialInterface* EngineSky = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/MapTemplates/Sky/M_Procedural_Sky_Night.M_Procedural_Sky_Night"));
	UMaterialInterface* SkyMaterial = AuthoredSky ? AuthoredSky : EngineSky;

	if (!SkyMesh || !SkyMaterial)
	{
		UE_LOG(LogCitix, Warning, TEXT("[Citix] Night sky assets missing; keeping the atmosphere sky."));
		return;
	}

	NightSkyDome = NewObject<UStaticMeshComponent>(this, TEXT("NightSkyDome"));
	if (!NightSkyDome)
	{
		return;
	}
	NightSkyDome->SetupAttachment(GetRootComponent());
	NightSkyDome->SetMobility(EComponentMobility::Movable);
	NightSkyDome->SetStaticMesh(SkyMesh);
	NightSkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NightSkyDome->SetCastShadow(false);
	NightSkyDome->SetCanEverAffectNavigation(false);
	NightSkyDome->SetGenerateOverlapEvents(false);

	const FBoxSphereBounds Bounds = SkyMesh->GetBounds();
	const float MeshRadius = FMath::Max(1.f, Bounds.SphereRadius);
	const float Scale = NightSkyRadius / MeshRadius;
	NightSkyDome->SetRelativeScale3D(FVector(Scale));
	NightSkyDome->SetRelativeLocation(FVector(0.f, 0.f, -Bounds.Origin.Z * Scale));
	// Make sure it is never frustum-culled as the player moves around the city.
	NightSkyDome->SetBoundsScale(4.f);
	// The sky light's real-time capture would otherwise treat this bright dome as the
	// sky and flood the whole city with light. Hide it from capture so the atmosphere
	// drives ambient instead; the dome is purely a visible cloud background.
	NightSkyDome->SetHiddenInSceneCapture(true);

	if (UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaterial, this))
	{
		if (AuthoredSky)
		{
			// The HDR cloud source is graded over time; the dome is only the visible background,
			// while SkyAtmosphere and the sky light continue to drive scene illumination.
			if (UTexture* SkyTexture = LoadObject<UTexture>(nullptr,
				TEXT("/Game/Citix/Textures/T_CitixNight.T_CitixNight")))
			{
				Sky->SetTextureParameterValue(TEXT("SkyTexture"), SkyTexture);
			}
			Sky->SetScalarParameterValue(TEXT("SkyExposure"), NightSkyExposure);
			Sky->SetScalarParameterValue(TEXT("SkyOpacity"), 0.f);
			Sky->SetScalarParameterValue(TEXT("SkyRotation"), NightSkyRotation);
			Sky->SetVectorParameterValue(TEXT("SkyTint"), NightSkyTint);
			Sky->SetVectorParameterValue(TEXT("HorizonColor"), NightHorizonGlow);
		}
		else
		{
			// Engine procedural sky: almost black, star-dominant, faint warm horizon.
			Sky->SetVectorParameterValue(TEXT("ZenithColor"), FLinearColor(0.0006f, 0.0009f, 0.0025f));
			Sky->SetVectorParameterValue(TEXT("HorizonColor"), FLinearColor(0.030f, 0.014f, 0.030f));
			Sky->SetVectorParameterValue(TEXT("StarColor"), FLinearColor(0.22f, 0.24f, 0.34f));
			Sky->SetVectorParameterValue(TEXT("RimColor"), FLinearColor(0.048f, 0.020f, 0.036f));
			Sky->SetScalarParameterValue(TEXT("Stars"), 1.f);
		}
		NightSkyDome->SetMaterial(0, Sky);
		NightSkyMaterial = Sky;
	}

	NightSkyDome->RegisterComponent();
	NightSkyDome->SetVisibility(false, true);

	// Verify the material exposes the horizon parameter used to join it to the skyline.
	const FLinearColor ReadBack = NightSkyMaterial
		? NightSkyMaterial->K2_GetVectorParameterValue(TEXT("HorizonColor"))
		: FLinearColor::Black;

	UE_LOG(LogCitix, Log,
		TEXT("[Citix] Night sky dome created (mesh radius %.0f, scale %.0f, horizon param reads %.3f %.3f %.3f)."),
		MeshRadius, Scale, ReadBack.R, ReadBack.G, ReadBack.B);
}

void ACitixTimeOfDay::UpdateSky()
{
	// Sun arc: 06:00 rises in the east, 12:00 overhead, 18:00 sets in the west.
	const float DayFraction = (Hours - 6.f) / 12.f;
	const float SunAngleRadians = DayFraction * PI;
	SunElevation = FMath::Sin(SunAngleRadians) * 90.f;

	const float Azimuth = FMath::Lerp(90.f, 270.f, FMath::Clamp(DayFraction, 0.f, 1.f));
	const FRotator SunRotation(-SunElevation, Azimuth, 0.f);

	// Daylight ramps in just below the horizon so dusk is not pitch black.
	const float DayAlpha = FMath::Clamp((SunElevation + 3.f) / 14.f, 0.f, 1.f);
	const float HorizonAlpha = FMath::Clamp(SunElevation / 22.f, 0.f, 1.f);

	const FLinearColor SunColor = FMath::Lerp(
		FLinearColor(1.0f, 0.46f, 0.18f),   // low sun: warm orange
		FLinearColor(1.0f, 0.96f, 0.90f),   // high sun: near white
		HorizonAlpha);
	const float SunIntensity = FMath::Lerp(0.f, SunPeakIntensity, DayAlpha) * FMath::Lerp(0.35f, 1.f, HorizonAlpha);

	// How "night" it is; drives the moon and the artificial lighting fade-in.
	const float NightAlpha = FMath::Clamp((-SunElevation + 2.f) / 14.f, 0.f, 1.f);

	if (SunLight)
	{
		SunLight->SetActorRotation(SunRotation);
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			Sun->SetIntensity(SunIntensity);
			Sun->SetLightColor(SunColor);
			Sun->SetVisibility(DayAlpha > 0.001f, true);
			// Whichever light is actually up must own forward shading, otherwise the
			// engine warns about competing directional lights.
			Sun->SetForwardShadingPriority(DayAlpha >= NightAlpha ? 10 : 0);
		}
	}

	// Moon: opposite azimuth, dim and cool, only while the sun is down.
	if (MoonLight)
	{
		MoonLight->SetActorRotation(FRotator(-FMath::Lerp(25.f, 55.f, NightAlpha), Azimuth + 180.f, 0.f));
		if (UDirectionalLightComponent* Moon = Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			Moon->SetIntensity(MoonIntensity * NightAlpha);
			Moon->SetLightColor(FLinearColor(0.55f, 0.66f, 1.0f));
			Moon->SetVisibility(NightAlpha > 0.001f, true);
			Moon->SetForwardShadingPriority(NightAlpha > DayAlpha ? 10 : 0);
		}
	}

	if (HeightFog)
	{
		if (UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			// Blue-violet night air, thick enough that distant towers sink into haze and the
			// skyline gains depth instead of being cut out of a flat sky. Day is a paler blue.
			Fog->SetFogDensity(FMath::Lerp(0.005f, 0.0125f, DayAlpha) * (1.f + RainIntensity * 2.2f));
			const FLinearColor NightFog(0.034f, 0.040f, 0.078f);
			const FLinearColor DayFog(0.360f, 0.470f, 0.640f);
			const FLinearColor FogColour = FMath::Lerp(NightFog, DayFog, DayAlpha);
			Fog->SetFogInscatteringColor(FogColour);
			// Warm light thrown back at the horizon by the city below, so the far skyline
			// sits in a glow rather than reading as a hard cut.
			const FLinearColor NightDirectional(0.070f, 0.044f, 0.040f);
			Fog->SetDirectionalInscatteringColor(FMath::Lerp(NightDirectional, FogColour * 1.6f, DayAlpha));
		}
	}

	// Warm city ambient after dark: this is what makes the whole scene read as lit
	// by the city rather than only by street lamps.
	if (SkyLight)
	{
		if (USkyLightComponent* Sky = SkyLight->GetLightComponent())
		{
			Sky->SetIntensity(FMath::Lerp(NightSkyLightIntensity, DaySkyLightIntensity, DayAlpha)
				* FMath::Lerp(1.f, 0.55f, RainIntensity));
			Sky->SetLightColor(FMath::Lerp(NightSkyLightColor, FLinearColor::White, DayAlpha));
		}
	}

	// Wet, reflective streets after dark: much richer to drive through. Rain soaks
	// the roads at any hour, which is what sells a downpour.
	if (bWetStreetsAtNight)
	{
		FCitixSurfaceLibrary::SetNightWetness(FMath::Clamp(FMath::Max(1.f - DayAlpha, RainIntensity), 0.f, 1.f));
	}

	// Preserve the existing dusk. Fade the HDR dome in only once the sun is well below
	// the horizon, then leave the city lights as the focal point of the night scene.
	if (NightSkyDome && NightSkyMaterial)
	{
		const float SkyOpacity = 1.f - FMath::SmoothStep(-20.f, -8.f, SunElevation);
		NightSkyMaterial->SetScalarParameterValue(TEXT("SkyOpacity"), SkyOpacity);
		NightSkyMaterial->SetScalarParameterValue(TEXT("SkyExposure"), NightSkyExposure);
		NightSkyMaterial->SetScalarParameterValue(TEXT("SkyRotation"), NightSkyRotation);
		NightSkyMaterial->SetVectorParameterValue(TEXT("SkyTint"), NightSkyTint);
		NightSkyMaterial->SetVectorParameterValue(TEXT("HorizonColor"), NightHorizonGlow);

		const bool bShowDome = SkyOpacity > 0.001f;
		if (NightSkyDome->IsVisible() != bShowDome)
		{
			NightSkyDome->SetVisibility(bShowDome, true);
		}
	}

	// Push exposure down at night so auto exposure cannot fake a daylight look, and
	// raise bloom so the lit skyline glows from a distance.
	if (PostProcessVolume)
	{
		PostProcessVolume->Settings.AutoExposureBias = FMath::Lerp(NightExposureBias, DayExposureBias, DayAlpha);
		PostProcessVolume->Settings.BloomIntensity = FMath::Lerp(NightBloomIntensity, DayBloomIntensity, DayAlpha);
	}

	// Windows, lamps and vehicle lights glow at night and stay subtle by day. Rain
	// haze takes a little of the glow back, so heavy weather looks softer.
	FCitixSurfaceLibrary::SetEmissiveBoost(
		FMath::Lerp(NightEmissiveBoost, DayEmissiveBoost, DayAlpha) * FMath::Lerp(1.f, 0.82f, RainIntensity));
}

ACitixTimeOfDay* ACitixTimeOfDay::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACitixTimeOfDay> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Console commands
// ---------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GCitixTimeCmd(
	TEXT("Citix.Time"),
	TEXT("Set the hour of day, 0-24. Example: Citix.Time 21.5"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(World);
		if (!TimeOfDay)
		{
			UE_LOG(LogCitix, Warning, TEXT("Citix.Time: no ACitixTimeOfDay in the world."));
			return;
		}
		if (Args.Num() > 0)
		{
			TimeOfDay->SetHours(FCString::Atof(*Args[0]));
		}
		UE_LOG(LogCitix, Log, TEXT("[Citix] Time: %s (sun elevation %.0f deg)"),
			*TimeOfDay->GetTimeString(), TimeOfDay->GetSunElevationDegrees());
	}));

static FAutoConsoleCommandWithWorldAndArgs GCitixTimePauseCmd(
	TEXT("Citix.TimePause"),
	TEXT("Pause or resume the day/night cycle. Citix.TimePause 1 | Citix.TimePause 0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ACitixTimeOfDay* TimeOfDay = ACitixTimeOfDay::Find(World);
		if (!TimeOfDay)
		{
			return;
		}
		const bool bPause = (Args.Num() > 0) ? (FCString::Atoi(*Args[0]) != 0) : !TimeOfDay->IsTimePaused();
		TimeOfDay->SetTimePaused(bPause);
		UE_LOG(LogCitix, Log, TEXT("[Citix] Time %s at %s."),
			bPause ? TEXT("paused") : TEXT("running"), *TimeOfDay->GetTimeString());
	}));
