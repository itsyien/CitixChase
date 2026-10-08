#pragma once

#include "CoreMinimal.h"

/** Pure chase rules: kept independent so the authoritative mode and tests agree. */
struct FCitixChaseRules
{
	static constexpr float IceRange=4000.f;
 static constexpr float IceRecharge=90.f;
 static constexpr float IceDuration=3.f;
 static bool InIceCone(const FVector& Origin,const FVector& Forward,const FVector& Target) {
  const FVector Delta=Target-Origin;
  const float Distance=Delta.Size2D();
  return FMath::Abs(Delta.Z)<=600.f && Distance<=IceRange &&
   (Distance<1.f || FVector::DotProduct(Delta.GetSafeNormal2D(),Forward.GetSafeNormal2D())>=.5f-KINDA_SMALL_NUMBER);
 }
 static void RefillIce(float Now,int32& Charges,float& Next) {
  Charges=FMath::Clamp(Charges,0,2);
  if (Charges>=2) { Next=0; return; }
  if (Next<=0) Next=Now+IceRecharge;
  while (Next<=Now && Charges<2) { ++Charges; Next+=IceRecharge; }
  if (Charges>=2) Next=0;
 }
 static constexpr float CountdownSeconds=10.f;
 static bool IsFinalWreck(bool ReserveUsed,float RemainingHealth) { return ReserveUsed || RemainingHealth<=0.f; }
 static constexpr float RunnerCarHealth = 100.f;
	static constexpr float RamDamage = 25.f;
	static constexpr float MinimumClosingSpeedKmh = 15.f;
	static constexpr float MinimumSeparationSeconds = 1.5f;
	static constexpr int32 RelaysRequired = 5;
	static constexpr float RelayInteractionRadius = 1350.f;
	static constexpr float BreakawayDuration = 5.f;
 static constexpr float GateSpacing = 20000.f; // Minimum connected-road separation between gates.
	static constexpr float ChaserBreakawayScale = .7f;
 static constexpr float ChaserGateDragDuration = 1.2f;
 static float GateSlowScale(float Now,float Started,float Until) {
  if (Until<=Now || Until<=Started) return 1.f;
  const float Ramp=FMath::SmoothStep(0.f,.75f,Now-Started);
  const float Release=FMath::SmoothStep(0.f,.35f,Until-Now);
  return 1.f-.3f*Ramp*Release;
 }
 static void RefillSmoke(float Now,int32& Charges,float& Next) { if (Charges>=2) { Next=0.f; return; } if (Next<=0.f) Next=Now+60.f; while (Next<=Now && Charges<2) { ++Charges; Next+=60.f; } if (Charges==2) Next=0.f; }
 static constexpr float ChaserSpeedScale = 1.3f;
 static constexpr float RunnerSpeedScale = 1.2f;
 static float OnFootSpeedScale(bool Runner, bool Sprinting) { return (Sprinting ? 1.1f : 1.f) * (Runner ? 1.5f : 1.f); }
 static float SpeedLimit(bool Runner) { return (Runner ? 190.f * RunnerSpeedScale : 225.f * ChaserSpeedScale) / .036f; }
 static float EngineScale(bool Runner, float Kmh) { return Runner ? FMath::Lerp(2.5f, .65f, FMath::SmoothStep(80.f, 140.f, Kmh / RunnerSpeedScale)) * RunnerSpeedScale : ChaserSpeedScale; }
 static bool ReplacementReady(float Now, float ReadyAt, bool Used, float Speed, float Distance) { return ReadyAt > 0.f && Now >= ReadyAt && !Used && Speed < 10.f && Distance <= 300.f; }
 static void RefillAmmo(float Now, int32& Ammo, float& Next) { if (Ammo >= 15) { Next = 0.f; return; } if (Next <= 0.f) Next = Now + 8.f; while (Next <= Now && Ammo < 15) { ++Ammo; Next += 8.f; } if (Ammo == 15) Next = 0.f; }
 static FVector LimitVelocity(FVector V, float Limit) { const float Speed = V.Size2D(); if (Limit > 0.f && Speed > Limit) { V.X *= Limit/Speed; V.Y *= Limit/Speed; } return V; }
 static float SceneHour(float Seconds) {
  // Phase origin is 19:00, 11.25 seconds before night begins at 19:30.
  float T = FMath::Fmod(FMath::Max(0.f, Seconds) + 258.75f, 270.f);
  if (T < 101.25f) return FMath::Fmod(19.5f + T * 9.f/101.25f, 24.f);
  T -= 101.25f; if (T < 67.5f) return 4.5f + T*3.f/67.5f;
  T -= 67.5f; if (T < 33.75f) return 7.5f + T*9.f/33.75f;
  return 16.5f + (T-33.75f)*3.f/67.5f;
 }
	static bool IsLethalRunOver(float SpeedKmh, bool bProtected) { return SpeedKmh > 40.f && !bProtected; }
	static bool CanFirePistol(bool bChaser, bool bOnFoot, bool bPursuit) { return bChaser && bOnFoot && bPursuit; }

	static int32 RamsToWreck(float Health) { return FMath::CeilToInt(Health / RamDamage); }
	static float HealthAfterWreck(float Health) { return FMath::Max(0.f, Health - 50.f); }
	static bool IsValidRam(float ClosingSpeedKmh, float SecondsSinceLastRam, bool bCarsSeparated = true) { return ClosingSpeedKmh >= MinimumClosingSpeedKmh && SecondsSinceLastRam >= MinimumSeparationSeconds && bCarsSeparated; }
	static bool AreExitsUnlocked(int32 CompletedRelays) { return CompletedRelays >= RelaysRequired; }
};
