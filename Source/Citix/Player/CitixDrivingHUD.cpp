// Copyright Epic Games, Inc. All Rights Reserved.
// Code-drawn HUD: speed, hull, boost, minimap, full map, job tracker and prompts.
// Drawn with Canvas primitives so the project needs no UMG assets. The design goal is
// "modern and minimal": thin bars, small type, low-opacity panels, nothing that fights
// the view while driving.

#include "Player/CitixDrivingHUD.h"

#include "Sandbox/CitixPoliceVehicle.h"
#include "Sandbox/CitixPoliceOfficer.h"

#include "Character/CitixOnFootPawn.h"
#include "City/CitixCityGenerator.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "UObject/ConstructorHelpers.h"
#include "CanvasItem.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Misc/App.h"
#include "Pedestrian/CitixPedestrianSystem.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChaseRules.h"
#include "Engine/Texture2D.h"
#include "Chase/CitixChasePlayerState.h"
#include "Player/CitixPlayerState.h"
#include "Sandbox/CitixSandboxDirector.h"
#include "Traffic/CitixTrafficSystem.h"
#include "Vehicle/CitixCarLibrary.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "World/CitixTimeOfDay.h"
#include "Citix.h"

namespace
{
	const FLinearColor PanelColour(0.02f, 0.025f, 0.035f, 0.55f);
	const FLinearColor PanelEdge(0.55f, 0.70f, 0.85f, 0.35f);
	const FLinearColor Ink(0.90f, 0.93f, 0.97f, 1.0f);
	const FLinearColor InkDim(0.62f, 0.68f, 0.75f, 1.0f);
	const FLinearColor Accent(0.35f, 0.78f, 1.00f, 1.0f);
	const FLinearColor RoadColour(0.55f, 0.68f, 0.80f, 0.55f);
	const FLinearColor PlayerColour(1.00f, 0.85f, 0.30f, 1.0f);
	const FLinearColor WaypointColour(0.35f, 0.95f, 0.55f, 1.0f);
	const FLinearColor JobColour(1.00f, 0.55f, 0.20f, 1.0f);
}

void ACitixDrivingHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bShowDrivingHud || !Canvas)
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayerController();
	ACitixDrivingPlayerController* CitixController = Cast<ACitixDrivingPlayerController>(PlayerController);

	// Photo mode: no interface at all, just the view - plus one hint line so the
	// camera stays usable without memorising keys.
	if (CitixController && CitixController->IsPhotoMode())
	{
		DrawPhotoHint(Canvas->SizeX, Canvas->SizeY);
		return;
	}

	APawn* OwningPawn = GetOwningPawn();
	if (!OwningPawn)
	{
		return;
	}

	// Refresh ambient-system references occasionally rather than every frame.
	if (GetWorld()->GetGameState<ACitixChaseGameState>())
	{
		DrawChasePanel(Canvas->SizeX, Canvas->SizeY);
		return;
	}
	if (++TrafficRefreshCounter >= 120 || !CachedTraffic.IsValid() || !CachedPedestrians.IsValid()
		|| !CachedTimeOfDay.IsValid() || !CachedGenerator.IsValid() || !CachedSandbox.IsValid())
	{
		TrafficRefreshCounter = 0;
		CachedTraffic = nullptr;
		CachedPedestrians = nullptr;
		CachedTimeOfDay = nullptr;
		CachedGenerator = nullptr;
		CachedSandbox = nullptr;
		for (TActorIterator<ACitixTrafficSystem> It(GetWorld()); It; ++It) { CachedTraffic = *It; break; }
		for (TActorIterator<ACitixPedestrianSystem> It(GetWorld()); It; ++It) { CachedPedestrians = *It; break; }
		for (TActorIterator<ACitixTimeOfDay> It(GetWorld()); It; ++It) { CachedTimeOfDay = *It; break; }
		for (TActorIterator<ACitixCityGenerator> It(GetWorld()); It; ++It) { CachedGenerator = *It; break; }
		for (TActorIterator<ACitixSandboxDirector> It(GetWorld()); It; ++It) { CachedSandbox = *It; break; }
	}
	CachedController = CitixController;

	const int32 TrafficCount = CachedTraffic.IsValid() ? CachedTraffic->GetActiveVehicleCount() : 0;
	const int32 PedestrianCount = CachedPedestrians.IsValid() ? CachedPedestrians->GetActivePedestrianCount() : 0;
	const FString TimeText = CachedTimeOfDay.IsValid() ? CachedTimeOfDay->GetTimeString() : FString(TEXT("--:--"));

	const float Width = Canvas->SizeX;
	const float Height = Canvas->SizeY;
	const ACitixVehiclePawn* Vehicle = Cast<ACitixVehiclePawn>(OwningPawn);
	const ACitixOnFootPawn* OnFoot = Cast<ACitixOnFootPawn>(OwningPawn);
	const bool bChaseMode = GetWorld() && GetWorld()->GetGameState<ACitixChaseGameState>();
	if (bChaseMode)
	{
		DrawChasePanel(Width, Height);
		return;
	}

	// Full map replaces the driving overlay but keeps the speed readout.
	const bool bMapOpen = CitixController && CitixController->IsMapOpen();
	if (bMapOpen)
	{
		DrawFullMap(OwningPawn, Width, Height);
	}
	else
	{
		// ---------------- Minimap (top right) ----------------
		DrawMinimap(OwningPawn, Width - 198.f, 24.f, 174.f);
	}

	// ---------------- Speed + bars (bottom centre) ----------------
	const float CenterX = Width * 0.5f;
	const float BaseY = Height - 132.f;

	const UCitixVehicleMovementComponent* Movement = Vehicle ? Vehicle->GetVehicleMovement() : nullptr;
	const float SpeedKmh = Vehicle ? Vehicle->GetDisplaySpeedKmh()
		: (OnFoot ? OnFoot->GetVelocity().Size2D() * 0.036f : 0.f);

	const FString SpeedText = FString::Printf(TEXT("%d"), FMath::RoundToInt(SpeedKmh));
	DrawText(SpeedText, Ink, CenterX - 46.f, BaseY, nullptr, 2.6f);
	DrawText(TEXT("km/h"), InkDim, CenterX + 34.f, BaseY + 26.f, nullptr, 1.0f);

	// Speed bar.
	const float BarWidth = 300.f;
	const float BarHeight = 6.f;
	const float BarX = CenterX - BarWidth * 0.5f;
	const float BarY = BaseY + 58.f;
	DrawRect(PanelColour, BarX - 2.f, BarY - 2.f, BarWidth + 4.f, BarHeight + 4.f);
	const float SpeedFraction = FMath::Clamp(SpeedKmh / FMath::Max(1.f, MaxDisplaySpeedKmh), 0.f, 1.f);
	DrawRect(FMath::Lerp(Accent, FLinearColor(1.f, 0.45f, 0.25f), SpeedFraction), BarX, BarY, BarWidth * SpeedFraction, BarHeight);

	// Hull + boost bars, left aligned and stacked.
	const float LeftX = 32.f;
	float StackY = Height - 96.f;
	if (Vehicle && Movement)
	{
		const float HealthFraction = Vehicle->GetDisplayHullFraction();
		const FLinearColor HealthColour = HealthFraction > 0.6f
			? FLinearColor(0.35f, 0.85f, 0.45f)
			: (HealthFraction > 0.3f ? FLinearColor(1.0f, 0.72f, 0.25f) : FLinearColor(1.0f, 0.28f, 0.25f));

		// The car type is part of the HUD: each one drives differently.
		const FString HullLabel = FString::Printf(TEXT("HULL  -  %s"),
			*FString(FCitixCarLibrary::GetTypeName(Vehicle->GetCarType())).ToUpper());
		DrawStatBar(LeftX, StackY, 210.f, HullLabel, HealthFraction, HealthColour);
		StackY += 34.f;
		DrawStatBar(LeftX, StackY, 210.f, TEXT("BOOST"), Vehicle->GetDisplayBoostCharge(),
			Vehicle->IsDisplayBoosting() ? FLinearColor(1.0f, 0.55f, 0.25f) : FLinearColor(0.35f, 0.78f, 1.0f));
		StackY += 34.f;

		if (Vehicle->IsDisplayDestroyed())
		{
			DrawText(TEXT("DESTROYED"), FLinearColor(1.f, 0.3f, 0.25f), LeftX, StackY, nullptr, 1.1f);
		}
	}

	// Gun layer: ammo bar in the same left stack (replaces the old pulse bar).
	if (!bChaseMode && CachedSandbox.IsValid())
	{
		const int32 Weapon = CachedSandbox->GetCurrentWeapon();
		if (Weapon >= 0)
		{
			const FCitixWeaponDef& Def = CachedSandbox->GetWeaponDef(Weapon);
			const float MagFraction = Def.MagSize > 0
				? static_cast<float>(CachedSandbox->GetMagAmmo(Weapon)) / Def.MagSize : 0.f;
			const FString AmmoLabel = CachedSandbox->IsReloading()
				? FString::Printf(TEXT("RELOADING %s"), *Def.Name.ToUpper())
				: FString::Printf(TEXT("%s  %d / %d"), *Def.Name.ToUpper(),
					CachedSandbox->GetMagAmmo(Weapon), CachedSandbox->GetReserveAmmo(Weapon));
			DrawStatBar(LeftX, StackY, 210.f, AmmoLabel,
				CachedSandbox->IsReloading() ? CachedSandbox->GetReloadFraction() : MagFraction,
				CachedSandbox->IsReloading()
					? FLinearColor(1.0f, 0.72f, 0.25f)
					: (MagFraction > 0.25f
						? FLinearColor(0.35f, 0.78f, 1.0f)
						: FLinearColor(1.0f, 0.35f, 0.3f)));
		}
		else
		{
			DrawStatBar(LeftX, StackY, 210.f, TEXT("UNARMED - FIND A GUNSMITH"), 0.f,
				FLinearColor(0.45f, 0.47f, 0.5f, 0.8f));
		}
		StackY += 34.f;
	}

	// Health bars: own (left stack) + every other driver (floating bars).
	DrawPlayerHealthBars(LeftX, StackY);

	// ---------------- Interact prompt (shown when it is actionable) ----------------
	// One compact prompt slot: enter/exit wins, then the gunsmith, then activities.
	// Talking, activities and guns stay on foot: the car is for driving.
	if (!bChaseMode && !bMapOpen && CachedSandbox.IsValid())
	{
		const bool bOnFootPrompt = OnFoot != nullptr;
		FString ShopPrompt;
		const bool bShopPrompt = bOnFootPrompt && CachedSandbox->CanPromptShop(ShopPrompt);
		FString ActivityPrompt;
		const bool bActivityPrompt = bOnFootPrompt && !bShopPrompt && CachedSandbox->CanPromptActivity(ActivityPrompt);

		if (OnFoot)
		{
			ACitixVehiclePawn* NearbyCar = OnFoot->FindNearestVehicle(420.f);
			const bool bOwnCar = NearbyCar
				&& (!NearbyCar->GetOwningController() || NearbyCar->GetOwningController() == CachedController.Get());
			if ((bOwnCar || OnFoot->FindNearestTrafficVehicle(420.f)))
			{
				DrawPrompt(CenterX, Height - 210.f, TEXT("F"), TEXT("ENTER VEHICLE"));
			}
			else if (bShopPrompt)
			{
				DrawPrompt(CenterX, Height - 210.f, TEXT("E"), ShopPrompt);
			}
			else if (bActivityPrompt)
			{
				DrawPrompt(CenterX, Height - 210.f, TEXT("E"), ActivityPrompt);
			}
		}
		else if (Vehicle && Vehicle->IsOccupied() && SpeedKmh < 4.f)
		{
			DrawPrompt(CenterX, Height - 210.f, TEXT("F"), TEXT("EXIT VEHICLE"));
		}
		else if (bShopPrompt)
		{
			DrawPrompt(CenterX, Height - 210.f, TEXT("E"), ShopPrompt);
		}
		else if (bActivityPrompt)
		{
			DrawPrompt(CenterX, Height - 210.f, TEXT("E"), ActivityPrompt);
		}
	}

	// ---------------- Sandbox readouts (toast, job tracker, score) ----------------
	if (!bChaseMode)
	{
		DrawDiscoveryCards(Width, Height);
		DrawActivityPanel(OwningPawn, Width, Height);
		DrawWantedPanel(Width, Height);
		DrawRacePanel(Width, Height);
		DrawWeaponLayer(Width, Height);
		DrawShopUI(Width, Height);
		DrawSandboxPanels(OwningPawn, Width, Height);
	}
	DrawChasePanel(Width, Height);

	// ---------------- Status line ----------------
	if (!bMapOpen)
	{
		const float Rain = CachedTimeOfDay.IsValid() ? CachedTimeOfDay->GetRainIntensity() : 0.f;
		const FString WeatherText = Rain > 0.05f
			? FString::Printf(TEXT("   |   rain %.0f%%"), Rain * 100.f)
			: FString();
		const FString StatusText = bChaseMode
			? FString::Printf(TEXT("%s   |   traffic %d   |   %.0f fps%s   |   F interact / vehicle"), *TimeText, TrafficCount, LastLoggedFps, *WeatherText)
			: FString::Printf(TEXT("%s   |   traffic %d   |   people %d   |   %.0f fps%s   |   M map   G route   E activity   X cancel"), *TimeText, TrafficCount, PedestrianCount, LastLoggedFps, *WeatherText);
		DrawText(StatusText, InkDim, BarX - 26.f, BarY + 20.f, nullptr, 0.9f);
	}

	// Rolling performance log.
	++FramesSincePerfLog;
	PerfAccumSeconds += FApp::GetDeltaTime();
	if (PerfAccumSeconds >= 2.f)
	{
		LastLoggedFps = static_cast<float>(FramesSincePerfLog) / PerfAccumSeconds;
		const float HullPercent = Vehicle ? Vehicle->GetDisplayHullFraction() * 100.f : 100.f;
		UE_LOG(LogCitix, Log, TEXT("[Citix] Perf: %.0f fps (%.2f ms/frame) traffic=%d pedestrians=%d hull=%.0f%%"),
			LastLoggedFps, (PerfAccumSeconds * 1000.f) / FMath::Max(1, FramesSincePerfLog),
			TrafficCount, PedestrianCount, HullPercent);
		FramesSincePerfLog = 0;
		PerfAccumSeconds = 0.f;
	}
}

ACitixDrivingHUD::ACitixDrivingHUD()
{
	static ConstructorHelpers::FObjectFinder<UFont> Font(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	ChaseFont = Font.Object;
}

void ACitixDrivingHUD::DrawChaseText(const FString& Text, FLinearColor Colour, float X, float Y, UFont*, float Scale)
{
	const FSlateFontInfo Font(ChaseFont, FMath::RoundToInt(16.f * Scale * ChaseUIScale), TEXT("Regular"));
	FCanvasTextItem Item(FVector2D(X * ChaseUIScale, Y * ChaseUIScale), FText::FromString(Text), Font, Colour);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, .65f));
	Canvas->DrawItem(Item);
}

void ACitixDrivingHUD::DrawChaseRect(FLinearColor Colour, float X, float Y, float Width, float Height)
{
	DrawRect(Colour, X * ChaseUIScale, Y * ChaseUIScale, Width * ChaseUIScale, Height * ChaseUIScale);
}

void ACitixDrivingHUD::DrawChaseLine(float X1, float Y1, float X2, float Y2, FLinearColor Colour, float Thickness)
{
	DrawLine(X1 * ChaseUIScale, Y1 * ChaseUIScale, X2 * ChaseUIScale, Y2 * ChaseUIScale, Colour, Thickness * ChaseUIScale);
}

void ACitixDrivingHUD::DrawChasePanel(float ScreenWidth, float ScreenHeight)
{
 ChaseUIScale=FMath::Min(ScreenWidth/1280.f,ScreenHeight/720.f);
 const float W=ScreenWidth/ChaseUIScale,H=ScreenHeight/ChaseUIScale;
 ACitixDrivingPlayerController* PC=Cast<ACitixDrivingPlayerController>(GetOwningPlayerController());
 const ACitixChaseGameState* S=GetWorld()->GetGameState<ACitixChaseGameState>();
 const ACitixChasePlayerState* PS=PC ? PC->GetPlayerState<ACitixChasePlayerState>() : nullptr;
 if (!S || !PS || S->Phase==ECitixChasePhase::Waiting || S->Phase==ECitixChasePhase::MatchResults) return;
 const float Now=GetWorld()->GetTimeSeconds(), ServerNow=S->GetServerWorldTimeSeconds();
 const FLinearColor Blue(.12f,.64f,1.f), Yellow(1.f,.8f,.18f), Red(1.f,.2f,.26f), Muted(.5f,.63f,.75f), Card(.012f,.022f,.04f,.88f);
 const bool Runner=PS->ChaseRole==ECitixChaseRole::Runner;
 const FLinearColor RoleInk=Runner ? Blue : Red;
 const ACitixVehiclePawn* Car=Cast<ACitixVehiclePawn>(GetOwningPawn());
 const ACitixOnFootPawn* Foot=Cast<ACitixOnFootPawn>(GetOwningPawn());
 auto Triangle=[&](FVector2D A,FVector2D B,FVector2D C,FLinearColor Color) {
  FCanvasTriangleItem Item(A*ChaseUIScale,B*ChaseUIScale,C*ChaseUIScale,Canvas->DefaultTexture->GetResource()); Item.SetColor(Color); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item);
 };
 auto Ring=[&](float X,float Y,float R,float Progress,FLinearColor Color,int Sides=32) {
  for (int I=0; I<Sides; ++I) { const float A=-PI*.5f+I*2*PI/Sides,B=A+2*PI/Sides;
   DrawChaseLine(X+FMath::Cos(A)*R,Y+FMath::Sin(A)*R,X+FMath::Cos(B)*R,Y+FMath::Sin(B)*R,I/static_cast<float>(Sides)<Progress ? Color : Muted.CopyWithNewOpacity(.18f),3);
  }
 };
 // Native-resolution vector charges: smooth silhouette, filled availability.
 auto Charge=[&](float X,float Y,bool Available) {
  const int32 Sides=64;
  const float Radius=10.f, Feather=1.f/FMath::Max(ChaseUIScale,.5f);
  auto Disc=[&](float R,FLinearColor Color) {
   for(int32 I=0;I<Sides;++I) {
    const float A=I*2*PI/Sides,B=(I+1)*2*PI/Sides;
    Triangle(FVector2D(X,Y),FVector2D(X+FMath::Cos(A)*R,Y+FMath::Sin(A)*R),FVector2D(X+FMath::Cos(B)*R,Y+FMath::Sin(B)*R),Color);
   }
  };
  if(Available) { Disc(Radius+Feather,Blue.CopyWithNewOpacity(.18f)); Disc(Radius+Feather*.5f,Blue.CopyWithNewOpacity(.45f)); Disc(Radius,Blue); }
  else {
   const float Inner=Radius-1.5f;
   for(int32 I=0;I<Sides;++I) {
    const float A=I*2*PI/Sides,B=(I+1)*2*PI/Sides;
    const FVector2D OuterA(X+FMath::Cos(A)*Radius,Y+FMath::Sin(A)*Radius),OuterB(X+FMath::Cos(B)*Radius,Y+FMath::Sin(B)*Radius);
    const FVector2D InnerA(X+FMath::Cos(A)*Inner,Y+FMath::Sin(A)*Inner),InnerB(X+FMath::Cos(B)*Inner,Y+FMath::Sin(B)*Inner);
    Triangle(OuterA,OuterB,InnerA,Muted.CopyWithNewOpacity(.4f)); Triangle(OuterB,InnerB,InnerA,Muted.CopyWithNewOpacity(.4f));
   }
  }
 };
 auto Hex=[&](float X,float Y,bool Filled) {
  Ring(X,Y,17,1, Filled ? Blue : Muted.CopyWithNewOpacity(.4f),6);
  if (Filled) { DrawChaseLine(X-6,Y,X-1,Y+5,Blue,2); DrawChaseLine(X-1,Y+5,X+7,Y-6,Blue,2); }
 };
 TArray<FVector2D> MarkerLabels;
 auto Marker=[&](const FVector& Target,FLinearColor Color,const FString& Name) {
  FVector Camera; FRotator Aim; PC->GetPlayerViewPoint(Camera,Aim);
  const FVector Delta=Aim.UnrotateVector(Target-Camera);
  FVector2D Screen; bool Projected=PC->ProjectWorldLocationToScreen(Target,Screen); Screen/=ChaseUIScale;
  const FVector2D Centre(W*.5f,H*.5f);
  const bool Inside=Projected && Screen.X>40 && Screen.X<W-40 && Screen.Y>165 && Screen.Y<H-270;
  FVector2D Direction(0,1);
  if (!Inside) {
   Direction=Projected ? (Screen-Centre).GetSafeNormal() : FVector2D(Delta.Y,-Delta.Z).GetSafeNormal();
   if (Direction.IsNearlyZero()) Direction=FVector2D(0,1);
   if (!Projected && Delta.X<0) Direction.Y=FMath::Max(.35,Direction.Y);
   Direction.Normalize();
   // Keep the pointer's distance label above the enlarged speed card.
   const float VerticalRoom=H*.5f-(Direction.Y>0 ? 280.f : 205.f);
   const float Edge=FMath::Min((W*.5f-45)/FMath::Max(.001, FMath::Abs(Direction.X)),VerticalRoom/FMath::Max(.001,FMath::Abs(Direction.Y)));
   Screen=Centre+Direction*Edge;
  }
  const FVector2D Side(-Direction.Y,Direction.X);
  if (Name==TEXT("BREAKAWAY")) { DrawChaseLine(Screen.X-19,Screen.Y-12,Screen.X-25,Screen.Y,Color,2); DrawChaseLine(Screen.X-25,Screen.Y,Screen.X-18,Screen.Y,Color,2); DrawChaseLine(Screen.X-18,Screen.Y,Screen.X-24,Screen.Y+12,Color,2); }
  if (Name==TEXT("STOP")) { DrawChaseLine(Screen.X-8,Screen.Y-8,Screen.X+8,Screen.Y+8,Color,3); DrawChaseLine(Screen.X-8,Screen.Y+8,Screen.X+8,Screen.Y-8,Color,3); }
  else Triangle(Screen+Direction*10,Screen-Direction*8+Side*7,Screen-Direction*8-Side*7,Color);
  FVector2D Label(FMath::Clamp(static_cast<float>(Screen.X)-50,35.f,W-170),Screen.Y+16);
  for (const FVector2D& Existing:MarkerLabels) if (FMath::Abs(Label.X-Existing.X)<170 && FMath::Abs(Label.Y-Existing.Y)<28) Label.Y=Existing.Y-36;
  if (Label.Y<Screen.Y) DrawChaseLine(Screen.X+12,Screen.Y,Label.X+85,Label.Y+18,Color.CopyWithNewOpacity(.5f),1);
  MarkerLabels.Add(Label);
  DrawChaseText(FString::Printf(TEXT("%s  %.0fm"),*Name,FVector::Dist2D(GetOwningPawn()->GetActorLocation(),Target)*.01f),Color,Label.X,Label.Y,nullptr,.72f);
 };
 DrawChaseRect(Card,24,24,258,116); DrawChaseLine(24,24,282,24,RoleInk,3);
 // Car silhouette and sharp role badge share a compact corner anchor.
 DrawChaseLine(40,55,82,55,RoleInk,3); DrawChaseLine(45,55,53,43,RoleInk,2); DrawChaseLine(53,43,70,43,RoleInk,2); DrawChaseLine(70,43,78,55,RoleInk,2);
 DrawChaseRect(RoleInk,45,57,8,6); DrawChaseRect(RoleInk,70,57,8,6);
 DrawChaseText(Runner ? TEXT("RUNNER") : TEXT("CHASER"),RoleInk,96,38,nullptr,1.65f);
 DrawChaseText(Runner ? (S->bExitsUnlocked ? TEXT("ESCAPE BY CAR") : TEXT("DRIVE THROUGH FIVE RELAYS")) : TEXT("STOP THE RUNNER"),Muted,40,76,nullptr,.67f);
 const ACitixChasePlayerState* TargetState=PS;
 if (!Runner) for (APlayerState* Other:S->PlayerArray) if (const auto* Target=Cast<ACitixChasePlayerState>(Other); Target && Target->ChaseRole==ECitixChaseRole::Runner) { TargetState=Target; break; }
 const auto* TargetCar=!Runner && TargetState!=PS ? Cast<ACitixVehiclePawn>(TargetState->GetPawn()) : nullptr;
 if (TargetCar && !TargetCar->IsDisplayDestroyed()) {
  const float Hull=TargetCar->GetDisplayHullFraction();
  for (int I=0; I<4; ++I) DrawChaseRect(Hull>I*.25f ? Hull>.5f ? Blue : Red : Muted.CopyWithNewOpacity(.2f),40+I*17,109,10,10);
  DrawChaseText(TargetState->bReplacementUsed ? TEXT("FINAL CAR") : TEXT("CAR INTEGRITY"),Muted,119,106,nullptr,.6f);
 } else {
  for (int I=0; I<4; ++I) DrawChaseRect(I<TargetState->PistolHits ? Red : Muted.CopyWithNewOpacity(.2f),40+I*17,109,10,10);
  DrawChaseText(TEXT("PISTOL HITS"),Muted,119,106,nullptr,.6f);
 }
 const int Sec=FMath::Max(0,FMath::CeilToInt(S->PhaseSecondsRemaining));
 DrawChaseRect(Card,W*.5f-125,24,250,90);
 DrawChaseText(FString::Printf(TEXT("%02d:%02d"),Sec/60,Sec%60),Ink,W*.5f-45,28,nullptr,1.8f);
 for (int I=0; I<5; ++I) Hex(W*.5f-76+I*38,86,I<S->CompletedRelays);
 DrawChaseText(FString::Printf(TEXT("ROUND %d / 2"),S->RoundNumber),Muted,W*.5f-38,120,nullptr,.6f);
 if (S->Phase==ECitixChasePhase::Countdown) {
  const float X=W*.5f-280,Y=H*.5f-92;
  DrawChaseRect(Card,X,Y,560,184); DrawChaseLine(X,Y,X+560,Y,RoleInk,3);
  DrawChaseText(Runner ? TEXT("RUNNER / ESCAPE") : TEXT("CHASER / STOP THE RUNNER"),RoleInk,X+24,Y+18,nullptr,1.25f);
  DrawChaseText(Runner ? TEXT("Drive through 5 blue relays, then either exit.") : TEXT("Ram the car 4 times to wreck it."),Ink,X+24,Y+58,nullptr,.8f);
  DrawChaseText(Runner ? TEXT("Yellow gates boost. LMB releases smoke for 5s.") : TEXT("On foot: 4 pistol hits, or hold F to capture."),Muted,X+24,Y+89,nullptr,.75f);
  DrawChaseText(Runner ? TEXT("One replacement. Second wreck ends the round.") : TEXT("Second wreck or time runs out: you win."),Muted,X+24,Y+117,nullptr,.75f);
  DrawChaseText(FString::Printf(TEXT("STARTS IN %d"),Sec),Ink,X+24,Y+151,nullptr,.7f);
  DrawChaseRect(RoleInk.CopyWithNewOpacity(.2f),X+170,Y+157,366,4);
  DrawChaseRect(RoleInk,X+170,Y+157,366*FMath::Clamp(1.f-S->PhaseSecondsRemaining/FCitixChaseRules::CountdownSeconds,0.f,1.f),4);
 }
 if (S->Phase==ECitixChasePhase::RoundResults) { DrawChaseRect(Card,W*.5f-250,H*.4f,500,105); DrawChaseText(S->StatusText,Ink,W*.5f-230,H*.4f+22,nullptr,1); DrawChaseText(TEXT("ROLES SWAP NEXT ROUND"),Blue,W*.5f-115,H*.4f+65,nullptr,.8f); }
 if (S->Phase!=ECitixChasePhase::Pursuit) return;
 if (LastChaseImpactSerial!=S->ImpactSerial) { LastChaseImpactSerial=S->ImpactSerial; ChaseImpactUntil=Now+.5f; }
 if (Now<ChaseImpactUntil) {
  const float Alpha=(ChaseImpactUntil-Now)*2;
  DrawChaseRect(Red.CopyWithNewOpacity(.1f*Alpha),0,0,12,H); DrawChaseRect(Red.CopyWithNewOpacity(.1f*Alpha),W-12,0,12,H);
 }
 if (S->bRunnerRevealed || (Runner && S->NextRevealSecondsRemaining<=5)) {
  DrawChaseRect(Card,24,151,258,30);
  DrawChaseText(FString::Printf(TEXT("%s  %.0fs"),S->bRunnerRevealed ? Runner ? TEXT("SIGNAL EXPOSED") : TEXT("RUNNER SIGNAL") : TEXT("NEXT REVEAL"),S->bRunnerRevealed ? S->RevealSecondsRemaining : S->NextRevealSecondsRemaining),Yellow,40,157,nullptr,.75f);
 }
 if (Runner) {
  const float Cooldown=FMath::Max(0.f,S->BreakawayReadyAt-ServerNow);
  DrawChaseRect(Card,W-240,H-222,216,35);
  DrawChaseText(Cooldown>0 ? FString::Printf(TEXT("GATES RECHARGING  %.0fs"),FMath::CeilToFloat(Cooldown)) : TEXT("BREAKAWAY READY"),Cooldown>0 ? Muted : Yellow,W-220,H-216,nullptr,.66f);
  DrawChaseRect(Muted.CopyWithNewOpacity(.2f),W-220,H-195,176,3); DrawChaseRect(Yellow,W-220,H-195,176*(1-Cooldown/15.f),3);
 } else if (PS->GateSlowUntil>ServerNow) {
  const float End=PS->GateSlowUntil;
  DrawChaseRect(Card,W-240,H-222,216,35); DrawChaseText(FString::Printf(TEXT("%s  %.1fs"),TEXT("GATE DRAG"),End-ServerNow),Red,W-220,H-212,nullptr,.8f);
  DrawChaseRect(Red.CopyWithNewOpacity(.13f),0,0,8,H); DrawChaseRect(Red.CopyWithNewOpacity(.13f),W-8,0,8,H);
 }

 FString Prompt;
 if (Car) {
  DrawChaseRect(Card,W-240,H-177,216,153);
  float DisplaySpeed=Car->GetDisplaySpeedKmh();
  FParse::Value(FCommandLine::Get(),TEXT("CitixHUDSpeed="),DisplaySpeed); // Explicit visual QA fixture only.
  DrawChaseText(FString::Printf(TEXT("%.0f"),DisplaySpeed),Ink,W-220,H-177,nullptr,3.1f);
  DrawChaseText(TEXT("KM/H"),Muted,W-91,H-99,nullptr,.66f);
  DrawChaseRect(Muted.CopyWithNewOpacity(.22f),W-220,H-55,176,6); DrawChaseRect(Blue,W-220,H-55,176*Car->GetDisplayBoostCharge(),6);
  DrawChaseText(TEXT("BOOST"),Muted,W-220,H-78,nullptr,.65f); Prompt=TEXT("F  EXIT CAR");
  if (Runner) {
   DrawChaseRect(Card,24,H-113,314,89); DrawChaseText(PS->bReplacementUsed ? TEXT("FINAL CAR / NO RESERVE") : TEXT("VEHICLE INTEGRITY"),Muted,40,H-103,nullptr,.72f);
   for (int I=0; I<4; ++I) DrawChaseRect(Car->GetDisplayHullFraction()>I*.25f ? Car->GetDisplayHullFraction()>.5f ? Blue : Red : Muted.CopyWithNewOpacity(.15f),40+I*71,H-73,63,24);
  }
 }
 if (Runner) {
  DrawChaseRect(Card,24,H-193,314,70);
  DrawChaseText(TEXT("LMB  SMOKE"),PS->SmokeCharges>0 ? Ink : Muted,40,H-181,nullptr,1.f);
  for (int32 I=0; I<2; ++I) Charge(278+I*30,H-173,PS->SmokeCharges>I);
  const float Refill=FMath::Max(0.f,PS->NextSmokeAt-ServerNow);
  const FString Status=PS->SmokeEmittingUntil>ServerNow ? FString::Printf(TEXT("RELEASING  %.1fs"),PS->SmokeEmittingUntil-ServerNow) : PS->SmokeCharges>=2 ? TEXT("TWO CHARGES READY") : FString::Printf(TEXT("NEXT CHARGE  %.0fs"),FMath::CeilToFloat(Refill));
  DrawChaseText(Status,Muted,40,H-156,nullptr,.85f);
 }
 const bool Recovery=Runner && Foot && PS->CharacterHealth<100 && !PS->bReplacementUsed;
 const float Remaining=FMath::Max(0.f,PS->ReplacementReadyAt-ServerNow);
 AActor* Nearby=Foot ? PC->FindChaseEntryCandidate(300.f) : nullptr;
 if (Foot) Prompt=Nearby && (!Recovery || Remaining<=0) ? TEXT("F  ENTER CAR") : FString();
 if (Recovery) {
  DrawChaseRect(Card,24,H-128,284,104); Ring(76,H-76,31,1-Remaining/40,Blue);
  DrawChaseText(Remaining>0 ? FString::Printf(TEXT("%.0f"),FMath::CeilToFloat(Remaining)) : TEXT("F"),Ink,Remaining>=10 ? 60 : 68,H-91,nullptr,1.4f);
  DrawChaseText(Remaining>0 ? TEXT("RECOVERY") : TEXT("REPLACEMENT READY"),Blue,123,H-102,nullptr,.8f);
  DrawChaseText(Foot->IsInShockwaveRecovery() ? TEXT("SHOCKWAVE PROTECTION") : Remaining>0 ? TEXT("STAY IN COVER") : Nearby ? TEXT("ENTER NEARBY CAR") : TEXT("FIND A SLOW ROAD CAR"),Muted,123,H-72,nullptr,.62f);
  if (AActor* Target=PC->FindChaseEntryCandidate(50000.f)) Marker(Target->GetActorLocation()+FVector(0,0,100),Blue,TEXT("CAR"));
 } else if (Runner && GetOwningPawn()) {
  const TArray<FVector>& Targets=S->bExitsUnlocked ? S->LayoutExitLocations : S->LayoutRelayLocations;
  int Nearest=INDEX_NONE; float Distance=FLT_MAX;
  for (int I=0; I<Targets.Num(); ++I) {
   if (!S->bExitsUnlocked && S->ActivatedRelays.IsValidIndex(I) && S->ActivatedRelays[I]) continue;
   const float D=FVector::DistSquared2D(GetOwningPawn()->GetActorLocation(),Targets[I]); if (D<Distance) { Distance=D; Nearest=I; }
  }
  if (Nearest!=INDEX_NONE) { Marker(Targets[Nearest]+FVector(0,0,70),Blue,S->bExitsUnlocked ? TEXT("EXIT") : TEXT("RELAY"));  }
  Nearest=INDEX_NONE; Distance=FLT_MAX;
  for (int I=0; I<S->BreakawayLocations.Num(); ++I) { const float D=FVector::DistSquared2D(GetOwningPawn()->GetActorLocation(),S->BreakawayLocations[I]); if (D<Distance) { Distance=D; Nearest=I; } }
  if (Nearest!=INDEX_NONE) { Marker(S->BreakawayLocations[Nearest]+FVector(0,0,90),S->BreakawayReadyAt>ServerNow ? Muted : Yellow,TEXT("BREAKAWAY")); }
  if (S->BreakawayUntil>ServerNow) DrawChaseText(FString::Printf(TEXT("BREAKAWAY  %.0fs"),S->BreakawayUntil-ServerNow),Yellow,W*.5f-70,H-98,nullptr,.85f);
 }
 if (Runner && PS->FrozenUntil>ServerNow) {
  const float RemainingIce=PS->FrozenUntil-ServerNow;
  DrawChaseRect(Card,W*.5f-155,H-170,310,50);
  DrawChaseText(FString::Printf(TEXT("FROZEN  %.1fs"),RemainingIce),Blue,W*.5f-137,H-164,nullptr,.95f);
  DrawChaseText(TEXT("-30% SPEED / ACCELERATION LOCKED"),Muted,W*.5f-137,H-141,nullptr,.55f);
  DrawChaseRect(Blue,W*.5f-155,H-120,310*RemainingIce/3.f,3);
 }
 if (!Runner) {
  if (Car) {
   DrawChaseRect(Card,24,H-115,314,91);
   DrawChaseText(TEXT("LMB  ICE WAVE"),PS->IceCharges>0 ? Ink : Muted,40,H-104,nullptr,1.f);
   for (int32 I=0;I<2;++I) Charge(278+I*30,H-96,PS->IceCharges>I);
   const float Refill=FMath::Max(0.f,PS->NextIceAt-ServerNow);
   const FString Status=PS->LastIceAt>0 && ServerNow-PS->LastIceAt<1.8f ? PS->bLastIceHit ? TEXT("RUNNER FROZEN") : TEXT("WAVE RELEASED") : PS->IceCharges>=2 ? TEXT("TWO CHARGES READY") : FString::Printf(TEXT("NEXT CHARGE  %.0fs"),FMath::CeilToFloat(Refill));
   DrawChaseText(Status,Muted,40,H-78,nullptr,.85f);
   DrawChaseText(TEXT("40m / WIDE FORWARD SCAN"),Muted,40,H-49,nullptr,.75f);
  }
  if (GetOwningPawn()) {
   int32 Gate=INDEX_NONE; float D=FLT_MAX;
   for (int32 I=0; I<S->BreakawayLocations.Num(); ++I) { const float Candidate=FVector::DistSquared2D(GetOwningPawn()->GetActorLocation(),S->BreakawayLocations[I]); if (Candidate<D) { D=Candidate; Gate=I; } }
   if (Gate!=INDEX_NONE) Marker(S->BreakawayLocations[Gate]+FVector(0,0,100),Red,TEXT("STOP"));
  }
  if (Foot) {
  DrawChaseRect(Card,24,H-104,250,80);
  DrawChaseText(FString::Printf(TEXT("%02d"),PS->Ammo),Ink,40,H-103,nullptr,2.5f);
  DrawChaseText(TEXT("/ 15"),Muted,122,H-80,nullptr,.9f);
  const float Refill=FMath::Max(0.f,PS->NextAmmoAt-ServerNow);
  DrawChaseText(PS->Ammo<15 ? FString::Printf(TEXT("+1 IN %.1fs"),Refill) : TEXT("RESERVE FULL"),Blue,40,H-49,nullptr,.7f);
  if (PS->Ammo<15) { DrawChaseRect(Muted.CopyWithNewOpacity(.22f),155,H-46,95,5); DrawChaseRect(Blue,155,H-46,95*(1-Refill/8),5); }
  }
  if (Foot) {
   const FLinearColor Cross=PC->ChaseHitConfirmedUntil>Now ? Red : Ink;
   for (float Side : {-1.f,1.f}) { DrawChaseLine(W*.5f+Side*4,H*.5f,W*.5f+Side*10,H*.5f,Cross,1); DrawChaseLine(W*.5f,H*.5f+Side*4,W*.5f,H*.5f+Side*10,Cross,1); }
  }
  for (APlayerState* State:S->PlayerArray) if (const ACitixChasePlayerState* Target=Cast<ACitixChasePlayerState>(State); Target && Target->ChaseRole==ECitixChaseRole::Runner && Target->GetPawn()) {
   const ACitixOnFootPawn* Walker=Cast<ACitixOnFootPawn>(Target->GetPawn());
   if (Foot && Walker && !Walker->IsInShockwaveRecovery() && FVector::DistSquared(Foot->GetActorLocation(),Walker->GetActorLocation())<=FMath::Square(250.f)) Prompt=TEXT("HOLD F  CAPTURE RUNNER");
   if (S->bRunnerRevealed) {
    Marker(Target->GetPawn()->GetActorLocation()+FVector(0,0,140),Red,TEXT("RUNNER"));
    FVector2D Head,Base; const FVector P=Target->GetPawn()->GetActorLocation();
    if (PC->ProjectWorldLocationToScreen(P+FVector(0,0,Walker ? 95.f : 80.f),Head) && PC->ProjectWorldLocationToScreen(P-FVector(0,0,Walker ? 80.f : 70.f),Base)) {
     Head/=ChaseUIScale; Base/=ChaseUIScale; const float R=FMath::Clamp(FMath::Abs(Base.Y-Head.Y)*.55f,12.f,70.f);
     for (float Side:{-1.f,1.f}) { const float X=Head.X+Side*R; DrawChaseLine(X,Head.Y,X,Head.Y+12,Red,2); DrawChaseLine(X,Head.Y,X-Side*10,Head.Y,Red,2); DrawChaseLine(X,Base.Y,X,Base.Y-12,Red,2); DrawChaseLine(X,Base.Y,X-Side*10,Base.Y,Red,2); }
    }
   }
  }
 }
 if (Foot && Foot->IsInShockwaveRecovery()) Prompt=TEXT("RECOVERING — PROTECTED");
 if (PS->bInteractionActive) {
  const float Duration=PS->InteractionType==ECitixChaseInteraction::Breakaway ? 3.f : 2.f;
  Ring(W*.5f,H-95,22,1-PS->InteractionSecondsRemaining/Duration,PS->InteractionType==ECitixChaseInteraction::Breakaway ? Yellow : Blue);
  Prompt=FString::Printf(TEXT("%s  %.1fs"),PS->InteractionType==ECitixChaseInteraction::Relay ? TEXT("SYNCING RELAY") : TEXT("HOLD F"),PS->InteractionSecondsRemaining);
 }
 if (PC->ChaseMessageUntil>Now) Prompt=PC->ChaseMessage;
 if (!Prompt.IsEmpty()) { const float Width=FMath::Min(520.f,Prompt.Len()*8.f+34); DrawChaseRect(Card,W*.5f-Width*.5f,H-56,Width,30); DrawChaseText(Prompt,Ink,W*.5f-Width*.5f+17,H-49,nullptr,.75f); }
}

void ACitixDrivingHUD::DrawStatBar(float X, float Y, float Width, const FString& Label,
	float Fraction, const FLinearColor& Colour)
{
	const float BarHeight = 7.f;
	DrawText(Label, InkDim, X, Y - 15.f, nullptr, 0.85f);
	DrawRect(PanelColour, X - 2.f, Y - 2.f, Width + 4.f, BarHeight + 4.f);
	DrawRect(Colour, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), BarHeight);
}

void ACitixDrivingHUD::DrawPrompt(float CenterX, float Y, const FString& Key, const FString& Action)
{
	const float KeyBox = 26.f;
	const float ActionWidth = Action.Len() * 9.f;
	const float TotalWidth = KeyBox + 10.f + ActionWidth;
	const float X = CenterX - TotalWidth * 0.5f;

	DrawRect(PanelColour, X - 10.f, Y - 6.f, TotalWidth + 20.f, KeyBox + 12.f);
	DrawRect(FLinearColor(0.35f, 0.78f, 1.0f, 0.85f), X, Y, KeyBox, KeyBox);
	DrawText(Key, FLinearColor(0.02f, 0.03f, 0.05f), X + 9.f, Y + 4.f, nullptr, 1.1f);
	DrawText(Action, Ink, X + KeyBox + 10.f, Y + 5.f, nullptr, 1.0f);
}

// ---------------------------------------------------------------------------
// Projection helpers
// ---------------------------------------------------------------------------

FVector2D ACitixDrivingHUD::WorldToPanel(const FVector2D& World, const FVector2D& WorldCentre, float Range,
	float PanelX, float PanelY, float PanelSize) const
{
	const float Scale = PanelSize / FMath::Max(1.f, Range * 2.f);
	return FVector2D(
		PanelX + PanelSize * 0.5f + (World.X - WorldCentre.X) * Scale,
		PanelY + PanelSize * 0.5f + (World.Y - WorldCentre.Y) * Scale);
}

void ACitixDrivingHUD::DrawRoadNetwork(const FVector2D& WorldCentre, float Range,
	float PanelX, float PanelY, float PanelSize, bool bClip)
{
	if (!CachedGenerator.IsValid())
	{
		return;
	}
	const FCitixRoadNetwork& Network = CachedGenerator->GetRoadNetwork();
	if (Network.Nodes.Num() == 0)
	{
		return;
	}

	// Roads run at any angle now, so each edge is drawn as a line rather than as a
	// screen-aligned rectangle (which only worked for a grid).
	const float ThicknessScale = FMath::Clamp(PanelSize / 900.f, 1.f, 6.f);

	for (const FCitixRoadEdge& Edge : Network.Edges)
	{
		const FVector2D A = Network.Nodes[Edge.NodeA].Position;
		const FVector2D B = Network.Nodes[Edge.NodeB].Position;
		const FVector2D Mid = (A + B) * 0.5f;
		if (FVector2D::DistSquared(Mid, WorldCentre) > Range * Range)
		{
			continue;
		}

		const FVector2D PanelA = WorldToPanel(A, WorldCentre, Range, PanelX, PanelY, PanelSize);
		const FVector2D PanelB = WorldToPanel(B, WorldCentre, Range, PanelX, PanelY, PanelSize);

		if (bClip)
		{
			const bool bInsideA = PanelA.X >= PanelX && PanelA.X <= PanelX + PanelSize
				&& PanelA.Y >= PanelY && PanelA.Y <= PanelY + PanelSize;
			const bool bInsideB = PanelB.X >= PanelX && PanelB.X <= PanelX + PanelSize
				&& PanelB.Y >= PanelY && PanelB.Y <= PanelY + PanelSize;
			if (!bInsideA && !bInsideB)
			{
				continue;
			}
		}

		const float Thickness = ThicknessScale * (Edge.RoadClass == ECitixRoadClass::Highway ? 3.5f
			: (Edge.RoadClass == ECitixRoadClass::Boulevard ? 3.f
				: (Edge.RoadClass == ECitixRoadClass::Arterial ? 2.5f
					: (Edge.RoadClass == ECitixRoadClass::Collector ? 1.8f : 1.f))));

		DrawLine(static_cast<float>(PanelA.X), static_cast<float>(PanelA.Y),
			static_cast<float>(PanelB.X), static_cast<float>(PanelB.Y), RoadColour, Thickness);
	}
}

void ACitixDrivingHUD::DrawPolyline(const TArray<FVector>& Points, const FVector2D& WorldCentre, float Range,
	float PanelX, float PanelY, float PanelSize, const FLinearColor& Colour, float Thickness, bool bClip)
{
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector2D A = WorldToPanel(FVector2D(Points[Index - 1].X, Points[Index - 1].Y),
			WorldCentre, Range, PanelX, PanelY, PanelSize);
		const FVector2D B = WorldToPanel(FVector2D(Points[Index].X, Points[Index].Y),
			WorldCentre, Range, PanelX, PanelY, PanelSize);

		float X0 = FMath::Min(A.X, B.X);
		float X1 = FMath::Max(A.X, B.X);
		float Y0 = FMath::Min(A.Y, B.Y);
		float Y1 = FMath::Max(A.Y, B.Y);
		if (bClip)
		{
			X0 = FMath::Clamp(X0, PanelX, PanelX + PanelSize);
			X1 = FMath::Clamp(X1, PanelX, PanelX + PanelSize);
			Y0 = FMath::Clamp(Y0, PanelY, PanelY + PanelSize);
			Y1 = FMath::Clamp(Y1, PanelY, PanelY + PanelSize);
		}

		DrawRect(Colour, X0 - Thickness * 0.5f, Y0 - Thickness * 0.5f,
			FMath::Max(Thickness, X1 - X0 + Thickness), FMath::Max(Thickness, Y1 - Y0 + Thickness));
	}
}

void ACitixDrivingHUD::DrawWorldMarker(const FVector& World, const FVector2D& WorldCentre, float Range,
	float PanelX, float PanelY, float PanelSize, const FLinearColor& Colour, float Size, bool bClip)
{
	const FVector2D Panel = WorldToPanel(FVector2D(World.X, World.Y), WorldCentre, Range,
		PanelX, PanelY, PanelSize);
	if (bClip
		&& (Panel.X < PanelX || Panel.X > PanelX + PanelSize
			|| Panel.Y < PanelY || Panel.Y > PanelY + PanelSize))
	{
		return;
	}
	DrawRect(Colour, Panel.X - Size * 0.5f, Panel.Y - Size * 0.5f, Size, Size);
}

// ---------------------------------------------------------------------------
// Minimap
// ---------------------------------------------------------------------------

void ACitixDrivingHUD::DrawMinimap(const AActor* Viewer, float PanelX, float PanelY, float PanelSize)
{
	DrawRect(PanelColour, PanelX, PanelY, PanelSize, PanelSize);
	DrawRect(PanelEdge, PanelX, PanelY, PanelSize, 1.f);
	DrawRect(PanelEdge, PanelX, PanelY + PanelSize - 1.f, PanelSize, 1.f);
	DrawRect(PanelEdge, PanelX, PanelY, 1.f, PanelSize);
	DrawRect(PanelEdge, PanelX + PanelSize - 1.f, PanelY, 1.f, PanelSize);

	if (!Viewer || !CachedGenerator.IsValid())
	{
		return;
	}

	const FVector ViewerLocation = Viewer->GetActorLocation();
	const FVector2D ViewerXY(ViewerLocation.X, ViewerLocation.Y);
	const float Range = MinimapRange;
	const FVector2D Centre = ViewerXY;

	DrawRoadNetwork(Centre, Range, PanelX, PanelY, PanelSize, /*bClip*/ true);

	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	if (Sandbox)
	{
		// Route to the active waypoint, tinted like the objective it leads to.
		const FLinearColor RouteColour = Sandbox->HasWaypoint() || Sandbox->GetJobState() == ECitixJobState::Active
			? Sandbox->GetObjectiveColor() : Accent;
		DrawPolyline(Sandbox->GetRoutePoints(), Centre, Range, PanelX, PanelY, PanelSize,
			FLinearColor(RouteColour.R, RouteColour.G, RouteColour.B, 0.85f), 2.f, /*bClip*/ true);

		// Decluttered by design: only the saved waypoint, the delivery/quest
		// destination and the gunsmiths. Everything else lives on the full map
		// or out in the world (discovery cards, beacons, the route guide).
		// (During a delivery the waypoint IS the destination: drawn once.)
		if (Sandbox->HasWaypoint() && Sandbox->GetJobState() != ECitixJobState::Active)
		{
			DrawWorldMarker(Sandbox->GetWaypoint(), Centre, Range, PanelX, PanelY, PanelSize,
				Sandbox->GetObjectiveColor(), 8.f, true);
		}
		if (Sandbox->GetJobState() == ECitixJobState::Active)
		{
			DrawWorldMarker(Sandbox->GetJobDestination(), Centre, Range, PanelX, PanelY, PanelSize,
				JobColour, 9.f, true);
		}

		// Gunsmiths are always on the minimap so a broke player can find one.
		for (const FCitixWeaponShop& Shop : Sandbox->GetShops())
		{
			DrawWorldMarker(Shop.Location, Centre, Range, PanelX, PanelY, PanelSize,
				JobColour, 5.f, true);
		}
	}

	// Player marker: a large bright dot with a long heading tick.
	const float CentreX = PanelX + PanelSize * 0.5f;
	const float CentreY = PanelY + PanelSize * 0.5f;
	DrawRect(PlayerColour, CentreX - 4.5f, CentreY - 4.5f, 9.f, 9.f);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), CentreX - 3.5f, CentreY - 3.5f, 7.f, 7.f);
	DrawRect(PlayerColour, CentreX - 4.5f, CentreY - 4.5f, 9.f, 2.f);
	const FVector Forward = Viewer->GetActorForwardVector();
	const FVector2D Forward2D(Forward.X, Forward.Y);
	if (Forward2D.SizeSquared() > 0.01f)
	{
		const FVector2D Dir = Forward2D.GetSafeNormal();
		DrawRect(FLinearColor(1.f, 0.85f, 0.3f, 0.9f),
			static_cast<float>(CentreX + Dir.X * 16.f) - 2.5f,
			static_cast<float>(CentreY + Dir.Y * 16.f) - 2.5f, 5.f, 5.f);
	}

	// Police units: flashing red squares (2 Hz) so the chase reads at a glance.
	// (Sandbox-guarded: clients run no director until stage 2 replicates it.)
	const ACitixSandboxDirector* PoliceSandbox = CachedSandbox.Get();
	if (PoliceSandbox)
	{
		const float FlashPhase = FMath::Fmod(static_cast<float>(FApp::GetCurrentTime()) * 2.f, 2.f);
		const FLinearColor PoliceColour = (FlashPhase < 1.f)
			? FLinearColor(1.f, 0.2f, 0.2f, 1.f) : FLinearColor(1.f, 0.55f, 0.55f, 1.f);
		for (const FCitixPoliceUnit& Unit : PoliceSandbox->GetPoliceUnits())
		{
			if (Unit.Car)
			{
				DrawWorldMarker(Unit.Car->GetActorLocation(), Centre, Range,
					PanelX, PanelY, PanelSize, PoliceColour, 7.f, true);
			}
		}
		// Dismounted officers: smaller solid red dots.
		for (const FCitixPoliceOfficerState& Officer : PoliceSandbox->GetOfficers())
		{
			if (Officer.Officer)
			{
				DrawWorldMarker(Officer.Officer->GetActorLocation(), Centre, Range,
					PanelX, PanelY, PanelSize, FLinearColor(1.f, 0.25f, 0.25f, 1.f), 5.f, true);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Full map
// ---------------------------------------------------------------------------

void ACitixDrivingHUD::DrawFullMap(const AActor* Viewer, float ScreenWidth, float ScreenHeight)
{
	// Dim the world behind the map.
	DrawRect(FLinearColor(0.01f, 0.015f, 0.02f, 0.88f), 0.f, 0.f, ScreenWidth, ScreenHeight);

	const float PanelSize = FMath::Min(ScreenWidth, ScreenHeight) * 0.80f;
	const float PanelX = (ScreenWidth - PanelSize) * 0.5f;
	const float PanelY = (ScreenHeight - PanelSize) * 0.5f + 12.f;

	// City extents come from the road graph so this works for any city size.
	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	if (CachedGenerator.IsValid())
	{
		for (const FCitixRoadNode& Node : CachedGenerator->GetRoadNetwork().Nodes)
		{
			Min.X = FMath::Min(Min.X, Node.Position.X);
			Min.Y = FMath::Min(Min.Y, Node.Position.Y);
			Max.X = FMath::Max(Max.X, Node.Position.X);
			Max.Y = FMath::Max(Max.Y, Node.Position.Y);
		}
	}
	if (Min.X > Max.X)
	{
		Min = FVector2D(-50000.f);
		Max = FVector2D(50000.f);
	}
	const FVector2D Centre = (Min + Max) * 0.5f;
	const float Range = FMath::Max(Max.X - Min.X, Max.Y - Min.Y) * 0.58f;

	DrawRect(PanelColour, PanelX, PanelY, PanelSize, PanelSize);
	const FLinearColor Edge(0.55f, 0.70f, 0.85f, 0.7f);
	DrawRect(Edge, PanelX, PanelY, PanelSize, 1.f);
	DrawRect(Edge, PanelX, PanelY + PanelSize - 1.f, PanelSize, 1.f);
	DrawRect(Edge, PanelX, PanelY, 1.f, PanelSize);
	DrawRect(Edge, PanelX + PanelSize - 1.f, PanelY, 1.f, PanelSize);

	DrawRoadNetwork(Centre, Range, PanelX, PanelY, PanelSize, /*bClip*/ true);

	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	if (Sandbox)
	{
		for (const FCitixPointOfInterest& POI : Sandbox->GetPOIs())
		{
			DrawWorldMarker(POI.Location, Centre, Range, PanelX, PanelY, PanelSize,
				POI.bDiscovered ? Accent : FLinearColor(0.42f, 0.47f, 0.53f, 0.55f),
				POI.bDiscovered ? 7.f : 5.f, true);
		}

		DrawPolyline(Sandbox->GetRoutePoints(), Centre, Range, PanelX, PanelY, PanelSize,
			(Sandbox->HasWaypoint() || Sandbox->GetJobState() == ECitixJobState::Active)
				? Sandbox->GetObjectiveColor() : Accent, 3.f, true);

		if (Sandbox->HasWaypoint() && Sandbox->GetJobState() != ECitixJobState::Active)
		{
			DrawWorldMarker(Sandbox->GetWaypoint(), Centre, Range, PanelX, PanelY, PanelSize,
				Sandbox->GetObjectiveColor(), 10.f, true);
		}
		if (Sandbox->GetJobState() == ECitixJobState::Active)
		{
			DrawWorldMarker(Sandbox->GetJobDestination(), Centre, Range, PanelX, PanelY, PanelSize,
				JobColour, 11.f, true);
		}

		for (const FCitixWeaponShop& Shop : Sandbox->GetShops())
		{
			DrawWorldMarker(Shop.Location, Centre, Range, PanelX, PanelY, PanelSize,
				JobColour, 6.f, true);
		}

		// Summary line under the map.
		const FString Summary = FString::Printf(
			TEXT("Discovered %d / %d     Score %d     Deliveries %d     Waypoint: %s"),
			Sandbox->GetDiscoveredCount(), Sandbox->GetPOICount(), Sandbox->GetScore(),
			Sandbox->GetJobsCompleted(),
			Sandbox->HasWaypoint() ? *Sandbox->GetWaypointName() : TEXT("none"));
		DrawText(Summary, Ink, PanelX, PanelY + PanelSize + 12.f, nullptr, 1.0f);
	}

	if (Viewer)
	{
		DrawWorldMarker(Viewer->GetActorLocation(), Centre, Range, PanelX, PanelY, PanelSize,
			PlayerColour, 9.f, true);
	}

	DrawText(TEXT("CITY MAP"), Ink, PanelX, PanelY - 30.f, nullptr, 1.5f);
	DrawText(TEXT("M close      G next waypoint      J delivery job      E activity      X cancel      V swap car      P photo mode"),
		InkDim, PanelX, PanelY - 12.f, nullptr, 0.9f);
}

// ---------------------------------------------------------------------------
// Sandbox panels: toast, job tracker, score
// ---------------------------------------------------------------------------

void ACitixDrivingHUD::DrawSandboxPanels(const AActor* Viewer, float ScreenWidth, float ScreenHeight)
{
	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	if (!Sandbox)
	{
		return;
	}

	const float CenterX = ScreenWidth * 0.5f;

	// ---- Toast (upper centre) ----
	if (Sandbox->GetToastRemaining() > 0.f && !Sandbox->GetToast().IsEmpty())
	{
		const FString& Toast = Sandbox->GetToast();
		const float TextWidth = Toast.Len() * 9.5f;
		const float BoxWidth = TextWidth + 36.f;
		DrawRect(FLinearColor(0.02f, 0.03f, 0.04f, 0.72f), CenterX - BoxWidth * 0.5f, 92.f, BoxWidth, 34.f);
		DrawRect(Accent, CenterX - BoxWidth * 0.5f, 92.f, 3.f, 34.f);
		DrawText(Toast, Ink, CenterX - TextWidth * 0.5f, 100.f, nullptr, 1.05f);
	}

	// ---- Job tracker (upper left, below the progress block) ----
	if (Sandbox->GetJobState() == ECitixJobState::Active)
	{
		const float X = 32.f;
		const float Y = 120.f;
		DrawRect(PanelColour, X - 8.f, Y - 8.f, 268.f, 74.f);
		DrawRect(JobColour, X - 8.f, Y - 8.f, 3.f, 74.f);
		DrawText(TEXT("DELIVERY"), JobColour, X, Y, nullptr, 1.0f);
		DrawText(FString::Printf(TEXT("%.0f m to go"), Sandbox->GetJobDistanceRemaining() / 100.f),
			Ink, X, Y + 18.f, nullptr, 0.95f);
		DrawText(FString::Printf(TEXT("%.0f s remaining"), Sandbox->GetJobTimeRemaining()),
			Sandbox->GetJobTimeRemaining() < 10.f ? FLinearColor(1.f, 0.35f, 0.3f) : InkDim,
			X, Y + 36.f, nullptr, 0.95f);
	}

	// ---- Progress (top left, out of the driving view) ----
	// Discovery count is personal online (own snapshot), shared offline.
	int32 DiscoveredShown = Sandbox->GetDiscoveredCount();
	if (UWorld* HudWorld = GetWorld())
	{
		if (HudWorld->GetNetMode() == NM_Client)
		{
			if (APlayerController* HudPC = GetOwningPlayerController())
			{
				if (ACitixPlayerState* HudPS = Cast<ACitixPlayerState>(HudPC->PlayerState))
				{
					DiscoveredShown = HudPS->DiscoveredPOIs.Num();
				}
			}
		}
	}
	DrawText(FString::Printf(TEXT("DISCOVERED  %d / %d"), DiscoveredShown, Sandbox->GetPOICount()),
		InkDim, 32.f, 24.f, nullptr, 0.9f);
	DrawText(FString::Printf(TEXT("SCORE  %d"), Sandbox->GetScore()),
		Ink, 32.f, 40.f, nullptr, 1.0f);
	DrawText(FString::Printf(TEXT("FUNDS  $%d"), Sandbox->GetMoney()),
		FLinearColor(0.55f, 0.85f, 0.55f, 1.f), 32.f, 56.f, nullptr, 1.0f);
	if (Sandbox->HasQuestOffer())
	{
		DrawText(TEXT("! CONTRACT (J)"), FLinearColor(1.f, 0.72f, 0.25f), 32.f, 72.f, nullptr, 0.9f);
	}

	if (Sandbox->HasWaypoint())
	{
		const float Distance = Viewer ? FVector::Dist2D(Viewer->GetActorLocation(), Sandbox->GetWaypoint()) : 0.f;
		DrawText(FString::Printf(TEXT("ROUTE  %s  %.0f m"), *Sandbox->GetWaypointName(), Distance / 100.f),
			WaypointColour, 32.f, 88.f, nullptr, 0.9f);
	}
}

// ---------------------------------------------------------------------------
// Discovery + district cards, activity panel, wanted panel, photo hint
// ---------------------------------------------------------------------------

void ACitixDrivingHUD::DrawDiscoveryCards(float ScreenWidth, float ScreenHeight)
{
	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	if (!Sandbox)
	{
		return;
	}

	// One centred card slot: a fresh discovery wins over the district card.
	FString Title;
	FString Sub;
	float Remaining = 0.f;
	if (Sandbox->HasDiscoveryCard())
	{
		Title = Sandbox->GetDiscoveryCardTitle();
		Sub = Sandbox->GetDiscoveryCardSub();
		Remaining = Sandbox->GetDiscoveryCardRemaining();
	}
	else if (Sandbox->HasDistrictCard())
	{
		Title = Sandbox->GetDistrictCardTitle();
		Sub = Sandbox->GetDistrictCardSub();
		Remaining = Sandbox->GetDistrictCardRemaining();
	}
	else
	{
		return;
	}

	const float CenterX = ScreenWidth * 0.5f;
	const float Y = 14.f;
	const float Fade = FMath::Clamp(FMath::Min(Remaining, 1.f), 0.25f, 1.f);
	const FLinearColor Panel(0.02f, 0.03f, 0.05f, 0.72f * Fade);
	const FLinearColor TitleColour(0.92f, 0.95f, 1.f, Fade);
	const FLinearColor SubColour(0.62f, 0.72f, 0.82f, Fade);

	const float TitleWidth = Title.Len() * 11.f;
	const float SubWidth = Sub.Len() * 7.5f;
	const float BoxWidth = FMath::Max(TitleWidth, SubWidth) + 48.f;
	DrawRect(Panel, CenterX - BoxWidth * 0.5f, Y, BoxWidth, 66.f);
	DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, 0.85f * Fade),
		CenterX - BoxWidth * 0.5f, Y, BoxWidth, 3.f);
	DrawText(Title.ToUpper(), TitleColour, CenterX - TitleWidth * 0.5f, Y + 10.f, nullptr, 1.25f);
	DrawText(Sub, SubColour, CenterX - SubWidth * 0.5f, Y + 36.f, nullptr, 0.9f);
}

void ACitixDrivingHUD::DrawActivityPanel(const AActor* Viewer, float ScreenWidth, float ScreenHeight)
{
	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	if (!Sandbox || Sandbox->GetActivityState() != ECitixActivityState::Active)
	{
		return;
	}

	// Same slot as the delivery tracker (they are mutually exclusive).
	const float X = 32.f;
	const float Y = 120.f;
	const bool bTour = Sandbox->GetActivityType() == ECitixActivityType::Tour;
	const FLinearColor ActivityColour = Sandbox->GetObjectiveColor();

	DrawRect(PanelColour, X - 8.f, Y - 8.f, 300.f, 96.f);
	DrawRect(ActivityColour, X - 8.f, Y - 8.f, 3.f, 96.f);
	DrawText(Sandbox->GetActivityName().ToUpper(), ActivityColour, X, Y, nullptr, 1.0f);

	FString Progress;
	if (bTour)
	{
		Progress = FString::Printf(TEXT("Stop %d / %d"),
			Sandbox->GetActivityStopsDone() + 1, Sandbox->GetActivityStopsTotal());
	}
	else
	{
		Progress = FString::Printf(TEXT("%.0f s remaining"),
			Sandbox->GetActivityTimeRemaining());
	}
	DrawText(Progress,
		(!bTour && Sandbox->GetActivityTimeRemaining() < 10.f)
			? FLinearColor(1.f, 0.35f, 0.3f) : InkDim,
		X, Y + 20.f, nullptr, 0.95f);

	const float Distance = Viewer ? FVector::Dist2D(Viewer->GetActorLocation(),
		Sandbox->GetWaypoint()) : 0.f;
	DrawText(FString::Printf(TEXT("%.0f m to go"), Distance / 100.f), Ink, X, Y + 38.f, nullptr, 0.95f);

	if (Sandbox->IsOffRoute())
	{
		DrawText(TEXT("OFF ROUTE - follow the green line"),
			FLinearColor(1.f, 0.72f, 0.25f), X, Y + 56.f, nullptr, 0.9f);
	}
	else
	{
		DrawText(TEXT("X cancels"), InkDim, X, Y + 56.f, nullptr, 0.9f);
	}
	(void)ScreenHeight;
}

void ACitixDrivingHUD::DrawWantedPanel(float ScreenWidth, float ScreenHeight)
{
	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	if (!Sandbox || Sandbox->GetWantedStars() <= 0)
	{
		return;
	}

	const int32 Stars = Sandbox->GetWantedStars();
	const float X = 32.f;
	const float Y = 200.f;

	DrawRect(PanelColour, X - 8.f, Y - 8.f, 220.f, 58.f);
	DrawRect(FLinearColor(1.f, 0.45f, 0.25f, 0.9f), X - 8.f, Y - 8.f, 3.f, 58.f);

	// Star pips: filled boxes for heat earned, dim for the rest.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FLinearColor Pip = Index < Stars
			? FLinearColor(1.f, 0.72f, 0.25f, 1.f)
			: FLinearColor(0.45f, 0.42f, 0.38f, 0.6f);
		DrawRect(Pip, X + Index * 22.f, Y, 16.f, 16.f);
	}

	FString StateText;
	switch (Sandbox->GetWantedState())
	{
	case ECitixWantedState::Pursuit:   StateText = TEXT("PURSUIT - lose them"); break;
	case ECitixWantedState::Searching: StateText = TEXT("SEARCHING - stay clear"); break;
	default:                            StateText = TEXT("COOLING OFF"); break;
	}
	DrawText(StateText, Ink, X + 74.f, Y + 1.f, nullptr, 0.95f);

	// Escape progress: fills while every unit is far and quiet.
	const float Escape = Sandbox->GetEscapeProgress();
	if (Escape > 0.01f)
	{
		DrawRect(PanelColour, X, Y + 24.f, 200.f, 6.f);
		DrawRect(FLinearColor(0.35f, 0.85f, 0.55f, 1.f), X, Y + 24.f, 200.f * Escape, 6.f);
		DrawText(TEXT("ESCAPING"), InkDim, X, Y + 32.f, nullptr, 0.85f);
	}
	(void)ScreenWidth;
	(void)ScreenHeight;
}

void ACitixDrivingHUD::DrawRacePanel(float ScreenWidth, float ScreenHeight)
{
	APlayerController* OwningPC = GetOwningPlayerController();
	ACitixPlayerState* PS = OwningPC && OwningPC->PlayerState
		? Cast<ACitixPlayerState>(OwningPC->PlayerState) : nullptr;
	if (!PS || PS->RaceState == 0 || PS->RaceCheckpointsTotal <= 0)
	{
		return;
	}

	const float X = 32.f;
	const float Y = 280.f;
	DrawRect(PanelColour, X - 8.f, Y - 8.f, 220.f, 58.f);
	DrawRect(FLinearColor(1.f, 0.72f, 0.25f, 0.9f), X - 8.f, Y - 8.f, 3.f, 58.f);

	if (PS->RaceState == 1)
	{
		DrawText(FString::Printf(TEXT("RACE STARTS IN %.0f"), PS->RaceCountdown),
			Ink, X, Y + 1.f, nullptr, 1.1f);
	}
	else if (PS->RacePosition > 0 && PS->RaceCheckpointsDone >= PS->RaceCheckpointsTotal)
	{
		DrawText(FString::Printf(TEXT("RACE P%d  %s"), PS->RacePosition, *FormatRaceTime(PS->RaceTime)),
			FLinearColor(0.35f, 0.85f, 0.55f, 1.f), X, Y + 1.f, nullptr, 1.1f);
	}
	else
	{
		DrawText(FString::Printf(TEXT("RACE P%d  %d/%d"), PS->RacePosition,
			PS->RaceCheckpointsDone, PS->RaceCheckpointsTotal),
			Ink, X, Y + 1.f, nullptr, 1.1f);
	}
	DrawText(TEXT("Y join/leave   X forfeit"), InkDim, X, Y + 32.f, nullptr, 0.85f);
	(void)ScreenWidth;
	(void)ScreenHeight;
}

FString ACitixDrivingHUD::FormatRaceTime(float Seconds)
{
	const int32 Minutes = FMath::FloorToInt(Seconds / 60.f);
	const int32 Secs = FMath::FloorToInt(Seconds - Minutes * 60.f);
	return FString::Printf(TEXT("%d:%02d"), Minutes, Secs);
}

void ACitixDrivingHUD::DrawPhotoHint(float ScreenWidth, float ScreenHeight)
{
	const float CenterX = ScreenWidth * 0.5f;
	const float Y = ScreenHeight - 64.f;
	DrawText(TEXT("WASD move    Q / E up / down    Mouse look    ENTER capture photo    P exit"),
		FLinearColor(0.90f, 0.93f, 0.97f, 0.85f), CenterX - 300.f, Y, nullptr, 1.0f);

	if (CachedSandbox.IsValid() && CachedSandbox->GetPhotosTaken() > 0)
	{
		const FString Last = FString::Printf(TEXT("Last photo  +%d  (%s)"),
			CachedSandbox->GetLastPhotoScore(), *CachedSandbox->GetLastPhotoBreakdown());
		DrawText(Last, InkDim, CenterX - Last.Len() * 4.5f, Y + 20.f, nullptr, 0.9f);
	}
}

void ACitixDrivingHUD::DrawWeaponLayer(float ScreenWidth, float ScreenHeight)
{
	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	const ACitixDrivingPlayerController* WeaponPC = CachedController.Get();
	if (!Sandbox || Sandbox->GetCurrentWeapon() < 0
		|| (WeaponPC && WeaponPC->IsShopOpen()))
	{
		return;
	}

	const float CenterX = ScreenWidth * 0.5f;
	const float CenterY = ScreenHeight * 0.5f;

	// Sniper scope: dark surround, clear centre, thin mil cross.
	if (Sandbox->IsScopeActive())
	{
		const FLinearColor ScopeBlack(0.0f, 0.0f, 0.0f, 0.96f);
		const float Clear = FMath::Min(ScreenWidth, ScreenHeight) * 0.62f;
		const float ClearX0 = CenterX - Clear * 0.5f;
		const float ClearX1 = CenterX + Clear * 0.5f;
		const float ClearY0 = CenterY - Clear * 0.5f;
		const float ClearY1 = CenterY + Clear * 0.5f;
		DrawRect(ScopeBlack, 0.f, 0.f, ScreenWidth, ClearY0);
		DrawRect(ScopeBlack, 0.f, ClearY1, ScreenWidth, ScreenHeight - ClearY1);
		DrawRect(ScopeBlack, 0.f, ClearY0, ClearX0, Clear);
		DrawRect(ScopeBlack, ClearX1, ClearY0, ScreenWidth - ClearX1, Clear);
		const FLinearColor MilLine(0.9f, 0.9f, 0.9f, 0.8f);
		DrawRect(MilLine, ClearX0, CenterY - 1.f, Clear, 2.f);
		DrawRect(MilLine, CenterX - 1.f, ClearY0, 2.f, Clear);
		return;
	}

	// Crosshair: gap follows live spread, red while reloading.
	const float Gap = 8.f + Sandbox->GetCurrentSpreadDegrees() * 14.f;
	const float Arm = 9.f;
	const FLinearColor Hair = Sandbox->IsReloading()
		? FLinearColor(1.f, 0.35f, 0.3f, 0.95f)
		: FLinearColor(0.95f, 0.97f, 1.f, 0.95f);
	DrawRect(Hair, CenterX - Gap - Arm, CenterY - 1.f, Arm, 2.f);
	DrawRect(Hair, CenterX + Gap, CenterY - 1.f, Arm, 2.f);
	DrawRect(Hair, CenterX - 1.f, CenterY - Gap - Arm, 2.f, Arm);
	DrawRect(Hair, CenterX - 1.f, CenterY + Gap, 2.f, Arm);
	DrawRect(Hair, CenterX - 1.f, CenterY - 1.f, 2.f, 2.f);

	if (Sandbox->GetAdsAmount() > 0.05f)
	{
		DrawText(TEXT("ADS"), InkDim, CenterX + Gap + Arm + 8.f, CenterY - 8.f, nullptr, 0.85f);
	}
}

void ACitixDrivingHUD::DrawShopUI(float ScreenWidth, float ScreenHeight)
{
	const ACitixSandboxDirector* Sandbox = CachedSandbox.Get();
	const ACitixDrivingPlayerController* ShopPC = CachedController.Get();
	if (!Sandbox || !ShopPC || !ShopPC->IsShopOpen())
	{
		return;
	}

	// Full-screen takeover: the world keeps running, but input stays here.
	DrawRect(FLinearColor(0.01f, 0.015f, 0.02f, 0.78f), 0.f, 0.f, ScreenWidth, ScreenHeight);

	float BoxX = 0.f;
	float BoxY = 0.f;
	float BoxW = 0.f;
	float RowY[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	float RowH = 0.f;
	FVector4 BuyRect = FVector4(0.f, 0.f, 0.f, 0.f);
	FVector4 LeaveRect = FVector4(0.f, 0.f, 0.f, 0.f);
	ShopLayout(ScreenWidth, ScreenHeight, BoxX, BoxY, BoxW, RowY, RowH, BuyRect, LeaveRect);
	const float BoxH = (LeaveRect.Y + LeaveRect.W) - BoxY;

	DrawRect(PanelColour, BoxX, BoxY, BoxW, BoxH);
	DrawRect(PanelEdge, BoxX, BoxY, BoxW, 1.f);
	DrawRect(PanelEdge, BoxX, BoxY + BoxH - 1.f, BoxW, 1.f);
	DrawRect(PanelEdge, BoxX, BoxY, 1.f, BoxH);
	DrawRect(PanelEdge, BoxX + BoxW - 1.f, BoxY, 1.f, BoxH);

	// Hovered row (mouse) joins the keyboard selection highlight.
	int32 HoverRow = INDEX_NONE;
	if (APlayerController* PC = GetOwningPlayerController())
	{
		float MouseX = 0.f;
		float MouseY = 0.f;
		if (PC->GetMousePosition(MouseX, MouseY))
		{
			ShopRowHitTest(MouseX, MouseY, HoverRow);
		}
	}

	const int32 Selected = ShopPC->GetShopSelected();
	DrawText(TEXT("GUNSMITH"), Accent, BoxX + 24.f, BoxY + 18.f, nullptr, 1.8f);
	DrawText(FString::Printf(TEXT("Funds  $%d"), Sandbox->GetMoney()),
		FLinearColor(0.55f, 0.85f, 0.55f, 1.f), BoxX + 24.f, BoxY + 52.f, nullptr, 1.2f);

	for (int32 Row = 0; Row < 4; ++Row)
	{
		const FCitixWeaponDef& Def = Sandbox->GetWeaponDef(Row);
		const bool bOwned = Sandbox->IsWeaponOwned(Row);
		const bool bCurrent = Sandbox->GetCurrentWeapon() == Row;
		const bool bHighlight = Selected == Row || HoverRow == Row;
		if (bHighlight)
		{
			DrawRect(FLinearColor(0.35f, 0.78f, 1.0f, 0.18f), BoxX + 12.f, RowY[Row] - 5.f, BoxW - 24.f, RowH);
		}
		const FLinearColor RowColour = (Selected == Row) ? Ink
			: (HoverRow == Row ? FLinearColor(0.80f, 0.85f, 0.92f, 1.f)
				: (bOwned ? FLinearColor(0.55f, 0.85f, 0.55f, 1.f) : InkDim));
		FString Line;
		if (bOwned)
		{
			Line = FString::Printf(TEXT("%d  %s  %d/%d%s"), Row + 1, *Def.Name,
				Sandbox->GetMagAmmo(Row), Sandbox->GetReserveAmmo(Row),
				bCurrent ? TEXT("  [EQUIPPED]") : TEXT(""));
		}
		else
		{
			Line = FString::Printf(TEXT("%d  %s  $%d"), Row + 1, *Def.Name, Def.Price);
		}
		if (!bOwned && Sandbox->GetMoney() < Def.Price)
		{
			Line += TEXT("  (can't afford - run deliveries)");
		}
		DrawText(Line, RowColour, BoxX + 24.f, RowY[Row], nullptr, 1.15f);
	}

	// Ammo refill row.
	const bool bAmmoHighlight = Selected == 4 || HoverRow == 4;
	if (bAmmoHighlight)
	{
		DrawRect(FLinearColor(0.35f, 0.78f, 1.0f, 0.18f), BoxX + 12.f, RowY[4] - 5.f, BoxW - 24.f, RowH);
	}
	const int32 Current = Sandbox->GetCurrentWeapon();
	FString AmmoLine = TEXT("5  Refill ammo");
	if (Current >= 0)
	{
		const FCitixWeaponDef& Def = Sandbox->GetWeaponDef(Current);
		AmmoLine += FString::Printf(TEXT(" (%s)  $%d"), *Def.Name, Def.AmmoPrice);
	}
	else
	{
		AmmoLine += TEXT("  (buy a weapon first)");
	}
	DrawText(AmmoLine, (Selected == 4) ? Ink : InkDim, BoxX + 24.f, RowY[4], nullptr, 1.15f);

	// Buttons: click BUY to buy the selected row, LEAVE to walk away.
	DrawRect(FLinearColor(0.25f, 0.55f, 0.25f, 0.9f), BuyRect.X, BuyRect.Y, BuyRect.Z, BuyRect.W);
	DrawText(TEXT("BUY"), FLinearColor(1.f, 1.f, 1.f), BuyRect.X + BuyRect.Z * 0.5f - 18.f,
		BuyRect.Y + 8.f, nullptr, 1.2f);
	DrawRect(FLinearColor(0.55f, 0.28f, 0.25f, 0.9f), LeaveRect.X, LeaveRect.Y, LeaveRect.Z, LeaveRect.W);
	DrawText(TEXT("LEAVE"), FLinearColor(1.f, 1.f, 1.f), LeaveRect.X + LeaveRect.Z * 0.5f - 28.f,
		LeaveRect.Y + 8.f, nullptr, 1.2f);
	DrawText(TEXT("Click / 1-5 select      E buy too"), InkDim, BoxX + 24.f, LeaveRect.Y + LeaveRect.W + 8.f, nullptr, 0.9f);
}

void ACitixDrivingHUD::ShopLayout(float ScreenWidth, float ScreenHeight,
	float& OutX, float& OutY, float& OutW, float OutRowY[5], float& OutRowH,
	FVector4& OutBuyRect, FVector4& OutLeaveRect) const
{
	OutW = FMath::Min(680.f, ScreenWidth * 0.7f);
	OutRowH = 46.f;
	const float HeaderH = 108.f;
	const float ButtonH = 44.f;
	const float FooterH = 30.f;
	const float BoxH = HeaderH + 5.f * OutRowH + ButtonH + FooterH;
	OutX = (ScreenWidth - OutW) * 0.5f;
	OutY = (ScreenHeight - BoxH) * 0.5f;
	for (int32 Row = 0; Row < 5; ++Row)
	{
		OutRowY[Row] = OutY + HeaderH + Row * OutRowH;
	}
	const float ButtonY = OutY + HeaderH + 5.f * OutRowH + 8.f;
	const float ButtonW = (OutW - 48.f) * 0.5f;
	OutBuyRect = FVector4(OutX + 24.f, ButtonY, ButtonW, ButtonH);
	OutLeaveRect = FVector4(OutX + 24.f + ButtonW + 8.f, ButtonY, ButtonW, ButtonH);
}

bool ACitixDrivingHUD::ShopRowHitTest(float MouseX, float MouseY, int32& OutRow) const
{
	int32 Button = 0;
	return ShopClickHitTest(MouseX, MouseY, OutRow, Button);
}

bool ACitixDrivingHUD::ShopClickHitTest(float MouseX, float MouseY, int32& OutRow, int32& OutButton) const
{
	OutRow = INDEX_NONE;
	OutButton = 0;
	if (!Canvas)
	{
		return false;
	}
	float BoxX = 0.f;
	float BoxY = 0.f;
	float BoxW = 0.f;
	float RowY[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	float RowH = 0.f;
	FVector4 BuyRect = FVector4(0.f, 0.f, 0.f, 0.f);
	FVector4 LeaveRect = FVector4(0.f, 0.f, 0.f, 0.f);
	ShopLayout(static_cast<float>(Canvas->SizeX), static_cast<float>(Canvas->SizeY),
		BoxX, BoxY, BoxW, RowY, RowH, BuyRect, LeaveRect);

	auto InRect = [](float X, float Y, const FVector4& R)
	{
		return X >= R.X && X <= R.X + R.Z && Y >= R.Y && Y <= R.Y + R.W;
	};
	if (InRect(MouseX, MouseY, BuyRect))
	{
		OutButton = 1;
		return true;
	}
	if (InRect(MouseX, MouseY, LeaveRect))
	{
		OutButton = 2;
		return true;
	}
	if (MouseX < BoxX || MouseX > BoxX + BoxW)
	{
		return false;
	}
	for (int32 Row = 0; Row < 5; ++Row)
	{
		if (MouseY >= RowY[Row] - 5.f && MouseY <= RowY[Row] + RowH)
		{
			OutRow = Row;
			return true;
		}
	}
	return false;
}

void ACitixDrivingHUD::DrawPlayerHealthBars(float LeftX, float StackY)
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	APlayerController* OwningPC = GetOwningPlayerController();
	ACitixPlayerState* OwnPS = OwningPC && OwningPC->PlayerState
		? Cast<ACitixPlayerState>(OwningPC->PlayerState) : nullptr;

	auto HealthColour = [](float Fraction)
	{
		return Fraction > 0.6f ? FLinearColor(0.35f, 0.85f, 0.45f)
			: (Fraction > 0.3f ? FLinearColor(1.0f, 0.72f, 0.25f)
				: FLinearColor(1.0f, 0.28f, 0.25f));
	};

	// Own bar: always in multiplayer, only when damaged in single-player.
	if (OwnPS && OwnPS->MaxHealth > 0.f)
	{
		const float Fraction = FMath::Clamp(OwnPS->Health / OwnPS->MaxHealth, 0.f, 1.f);
		if (World->GetNetMode() != NM_Standalone || Fraction < 1.f)
		{
			DrawStatBar(LeftX, StackY, 210.f, TEXT("HEALTH"), Fraction, HealthColour(Fraction));
		}
	}

	// Floating bars over every other driver with a live pawn. Player controllers
	// only exist locally, so remotes come from the game state's player array.
	AGameStateBase* GS = World->GetGameState();
	if (!OwningPC || !GS)
	{
		return;
	}
	for (TObjectPtr<APlayerState> Member : GS->PlayerArray)
	{
		APlayerState* MemberPS = Member.Get();
		if (!MemberPS || MemberPS == OwningPC->PlayerState)
		{
			continue;
		}
		ACitixPlayerState* PS = Cast<ACitixPlayerState>(MemberPS);
		APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (!PS || !Pawn || PS->MaxHealth <= 0.f)
		{
			continue;
		}
		FVector2D ScreenPos = FVector2D::ZeroVector;
		if (!OwningPC->ProjectWorldLocationToScreen(
			Pawn->GetActorLocation() + FVector(0.f, 0.f, 230.f), ScreenPos))
		{
			continue;
		}
		const float Fraction = FMath::Clamp(PS->Health / PS->MaxHealth, 0.f, 1.f);
		DrawText(PS->GetPlayerName().ToUpper(), InkDim, ScreenPos.X - 40.f, ScreenPos.Y - 26.f, nullptr, 0.8f);
		DrawRect(PanelColour, ScreenPos.X - 42.f, ScreenPos.Y - 12.f, 94.f, 10.f);
		DrawRect(HealthColour(Fraction), ScreenPos.X - 40.f, ScreenPos.Y - 10.f, 90.f * Fraction, 6.f);
	}
}
