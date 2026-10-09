// Copyright Epic Games, Inc. All Rights Reserved.
// Code-drawn HUD: speedometer, bars, minimap, full map, sandbox readouts.
// No UMG assets required.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CitixDrivingHUD.generated.h"

class ACitixSandboxDirector;

UCLASS()
class CITIX_API ACitixDrivingHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACitixDrivingHUD();
	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|HUD")
	bool bShowDrivingHud = true;

	/** Speed that fills the gauge, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|HUD")
	float MaxDisplaySpeedKmh = 240.f;

	/** Half-extent of the minimap view around the player, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|HUD")
	float MinimapRange = 22000.f;

	/** One thin labelled bar (hull, boost). */
	void DrawStatBar(float X, float Y, float Width, const FString& Label, float Fraction, const FLinearColor& Colour);

	/** "F  ENTER VEHICLE" style prompt. */
	void DrawPrompt(float CenterX, float Y, const FString& Key, const FString& Action);

	/** Minimal road-network minimap with a player marker and the active route. */
	void DrawMinimap(const AActor* Viewer, float PanelX, float PanelY, float PanelSize);

	/** Full-screen city map with POIs, waypoint, route and job marker. */
	void DrawFullMap(const AActor* Viewer, float ScreenWidth, float ScreenHeight);

	/** Toast message, job tracker and discovery/score readout. */
	void DrawSandboxPanels(const AActor* Viewer, float ScreenWidth, float ScreenHeight);

	/** Centred discovery / district cards (name + flavour + progress). */
	void DrawDiscoveryCards(float ScreenWidth, float ScreenHeight);

	/** Contextual activity prompt + active objective panel. */
	void DrawActivityPanel(const AActor* Viewer, float ScreenWidth, float ScreenHeight);

	/** Compact wanted stars + pursuit state + escape progress. */
	void DrawWantedPanel(float ScreenWidth, float ScreenHeight);

	/** Race position/checkpoint panel (own snapshot; hidden when idle). */
	void DrawRacePanel(float ScreenWidth, float ScreenHeight);

	/** m:ss formatting shared with the director's race cards. */
	static FString FormatRaceTime(float Seconds);

	/** Minimal photo-mode hint (HUD is otherwise hidden for the shot). */
	void DrawPhotoHint(float ScreenWidth, float ScreenHeight);

	/** Crosshair, ammo readout and sniper scope for the gun layer. */
	void DrawWeaponLayer(float ScreenWidth, float ScreenHeight);

	/** Own health bar (left stack) + floating bars over other drivers. */
	void DrawPlayerHealthBars(float LeftX, float StackY);

	/** Weapon shop buy UI (centred panel, keyboard driven). */
	void DrawShopUI(float ScreenWidth, float ScreenHeight);

	/** Chase-mode role, timer and objective panel. */
	void DrawChasePanel(float ScreenWidth, float ScreenHeight);

	/** Which shop row sits under a mouse point (for click select). */
	bool ShopRowHitTest(float MouseX, float MouseY, int32& OutRow) const;

	/** Full shop click: buttons first (1 = buy, 2 = leave), else a row. */
	bool ShopClickHitTest(float MouseX, float MouseY, int32& OutRow, int32& OutButton) const;

	// ---- Shared projection helpers ------------------------------------

	/** World XY to panel pixels. */
	FVector2D WorldToPanel(const FVector2D& World, const FVector2D& WorldCentre, float Range,
		float PanelX, float PanelY, float PanelSize) const;

	/** Road graph drawn into a panel, centred on WorldCentre (half-extent Range, cm). */
	void DrawRoadNetwork(const FVector2D& WorldCentre, float Range,
		float PanelX, float PanelY, float PanelSize, bool bClip);

	/** World-space polyline drawn as thin rectangles. */
	void DrawPolyline(const TArray<FVector>& Points, const FVector2D& WorldCentre, float Range,
		float PanelX, float PanelY, float PanelSize, const FLinearColor& Colour, float Thickness, bool bClip);

	/** A small world-space marker in a panel. */
	void DrawWorldMarker(const FVector& World, const FVector2D& WorldCentre, float Range,
		float PanelX, float PanelY, float PanelSize, const FLinearColor& Colour, float Size, bool bClip);

private:
	UPROPERTY() TObjectPtr<UFont> ChaseFont;
	float ChaseUIScale = 1.f;
 void DrawRunnerTracker(APawn* Target,float W,float H);
 bool DrawRunnerESP(APawn* Target,float W,float H);
 void DrawPlayerCarLabels(float W,float H);
 TWeakObjectPtr<class ACitixGroundTracker> GroundTracker;
 bool bTrackerInitialized=false;
 TArray<FVector2D> MarkerLabelPositions;

	void DrawChaseText(const FString& Text, FLinearColor Colour, float X, float Y, UFont* Font, float Scale);
	void DrawChaseRect(FLinearColor Colour, float X, float Y, float Width, float Height);
	void DrawChaseLine(float X1, float Y1, float X2, float Y2, FLinearColor Colour, float Thickness);
	void ShopLayout(float ScreenWidth, float ScreenHeight,
		float& OutX, float& OutY, float& OutW, float OutRowY[5], float& OutRowH,
		FVector4& OutBuyRect, FVector4& OutLeaveRect) const;

	TWeakObjectPtr<class ACitixTrafficSystem> CachedTraffic;
	TWeakObjectPtr<class ACitixPedestrianSystem> CachedPedestrians;
	TWeakObjectPtr<class ACitixTimeOfDay> CachedTimeOfDay;
	TWeakObjectPtr<class ACitixCityGenerator> CachedGenerator;
	TWeakObjectPtr<ACitixSandboxDirector> CachedSandbox;
	TWeakObjectPtr<class ACitixDrivingPlayerController> CachedController;
	int32 TrafficRefreshCounter = 0;

	// Rolling frame-time stats (diagnostics + on-screen FPS).
	int32 FramesSincePerfLog = 0;
	float PerfAccumSeconds = 0.f;
	float LastLoggedFps = 0.f;

	/** Local-only confirmation of a replicated, server-accepted ram. */
	FString LastChaseStatus;
	float ChaseImpactUntil = 0.f;
	float ChaseStatusUntil = 0.f;
	int32 LastChaseImpactSerial = 0;
};
