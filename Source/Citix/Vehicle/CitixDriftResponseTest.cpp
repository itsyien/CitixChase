// Exercises the production movement component against a real Chaos rigid body.
#if WITH_EDITOR && !IS_MONOLITHIC && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Vehicle/CitixVehiclePawn.h"
#include "Chase/CitixChaseGameState.h"
#include "Chase/CitixChasePlayerState.h"
#include "Vehicle/CitixVehicleMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

namespace CitixDriftTest
{
struct FResult
{
 float Yaw = 0.f;
 float EarlySteer = 0.f;
 float OldForwardDistance = 0.f;
 float PeakSpeed = 0.f;
 float ReleaseYaw = 0.f;
 float CounterYawRate = 0.f;
 float SlipDegrees = 0.f;
 int32 GroundedFrames = 0;
 FVector EndVelocity = FVector::ZeroVector;
};

// No level, city, game mode or input injection: the real chase role config and
// movement tick run on an actual simulated chassis above an isolated static floor.
FResult Simulate(bool bRunner, float Steering, float InitialSpeed, int32 Hz, bool bAirborne = false, bool bUnassisted = false, bool bHoldHandbrakeOnRelease = false, bool bFrozen = false, bool bNormal = false, bool bBush = false)
{
 const UWorld::InitializationValues Values = UWorld::InitializationValues()
  .AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
  .CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false);
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
 FResult Result;
 if (!World || !World->GetPhysicsScene()) return Result;

 AActor* FloorActor = World->SpawnActor<AActor>();
 UBoxComponent* Floor = NewObject<UBoxComponent>(FloorActor);
 FloorActor->SetRootComponent(Floor);
 Floor->SetBoxExtent(FVector(200000.f, 200000.f, 50.f));
 Floor->SetWorldLocation(FVector(0, 0, -50));
 Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 Floor->SetCollisionObjectType(ECC_WorldStatic);
 Floor->SetCollisionResponseToAllChannels(ECR_Block);
 Floor->RegisterComponent();

 ACitixVehiclePawn* Pawn = World->SpawnActor<ACitixVehiclePawn>(FVector(0, 0, bAirborne ? 5000.f : 70.f), FRotator::ZeroRotator);
 UBoxComponent* Body = CastChecked<UBoxComponent>(Pawn->GetRootComponent());
 UCitixVehicleMovementComponent* Movement = Pawn->FindComponentByClass<UCitixVehicleMovementComponent>();
 Pawn->ApplyChasePerformance(bRunner);
 if (bUnassisted) Movement->DriftYawRateDegrees = 0.f;
 if (bFrozen)
 {
  ACitixChaseGameState* State = World->SpawnActor<ACitixChaseGameState>();
  World->SetGameState(State);
  State->Phase = ECitixChasePhase::Pursuit;
  ACitixChasePlayerState* Driver = World->SpawnActor<ACitixChasePlayerState>();
  Pawn->SetPlayerState(Driver);
  Driver->FrozenUntil = 100.f;
  Driver->FrozenSpeedLimit = 1500.f;
  Movement->SetThrottleInput(1.f);
  Movement->SetBoostInput(true);
 }
 Body->SetCenterOfMass(FVector(0, 0, -30));
 Body->SetSimulatePhysics(true);
 Movement->SetUpdatedComponent(Body);
 const float Dt = 1.f / Hz;
 const FVector Gravity(0, 0, -980);
 auto Step = [&]()
 {
  Movement->TickComponent(Dt, LEVELTICK_All, nullptr);
  FPhysScene* Scene = World->GetPhysicsScene();
  Scene->SetUpForFrame(&Gravity, Dt, 0.f, .1f, 1.f / 120.f, 16, true);
  Scene->StartFrame();
  Scene->WaitPhysScenes();
  Scene->EndFrame();
 };
 // Settle suspension and flush newly created Chaos bodies before measurement.
 for (int32 I = 0; I < Hz; ++I) Step();
 Body->SetWorldLocationAndRotation(FVector(0, 0, bAirborne ? 5000.f : Body->GetComponentLocation().Z), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
 Body->SetPhysicsLinearVelocity(FVector(InitialSpeed, bAirborne ? 400.f : 0.f, 0));
 Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
 Movement->SetHandbrake(!bNormal);
 Movement->SetSteeringInput(Steering);
 AActor* BushActor=nullptr;
 if (bBush) {
  BushActor=World->SpawnActor<AActor>();
  auto* Bush=NewObject<UBoxComponent>(BushActor); BushActor->SetRootComponent(Bush);
  Bush->SetBoxExtent(FVector(12000,500,120)); Bush->SetWorldLocation(FVector(0,0,100));
  Bush->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Bush->SetCollisionObjectType(ECC_GameTraceChannel2);
  Bush->SetCollisionResponseToAllChannels(ECR_Ignore); Bush->ComponentTags.Add(TEXT("CitixBush")); Bush->RegisterComponent();
 }
 Result.PeakSpeed = InitialSpeed;
 for (int32 I = 0; I < FMath::RoundToInt(.8f * Hz); ++I)
 {
  Step();
  if (I==FMath::RoundToInt(.1f*Hz)-1) Result.EarlySteer=Movement->GetCurrentSteerAngleDegrees();
  Result.PeakSpeed = FMath::Max(Result.PeakSpeed, static_cast<float>(Body->GetPhysicsLinearVelocity().Size2D()));
  Result.GroundedFrames += Movement->IsGrounded() ? 1 : 0;
 }
 Result.Yaw = FRotator::NormalizeAxis(Body->GetComponentRotation().Yaw);
 Result.OldForwardDistance = Body->GetComponentLocation().X;
 Result.EndVelocity = Body->GetPhysicsLinearVelocity();
 Result.SlipDegrees = FMath::Abs(FRotator::NormalizeAxis(Result.EndVelocity.Rotation().Yaw - Result.Yaw));
 Movement->SetSteeringInput(0.f);
 Movement->SetHandbrake(bHoldHandbrakeOnRelease);
 for (int32 I = 0; I < FMath::RoundToInt(.4f * Hz); ++I) Step();
 Result.ReleaseYaw = FMath::Abs(FRotator::NormalizeAxis(Body->GetComponentRotation().Yaw - Result.Yaw));
 Movement->SetHandbrake(true);
 Movement->SetSteeringInput(-Steering);
 for (int32 I = 0; I < FMath::RoundToInt(.4f * Hz); ++I) Step();
 Result.CounterYawRate = Body->GetPhysicsAngularVelocityInDegrees().Z;
 World->DestroyWorld(false);
 return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixDriftPhysicsResponseTest, "CitixChase.Drift.PhysicsResponse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixDriftPhysicsResponseTest::RunTest(const FString& Parameters)
{
 using namespace CitixDriftTest;
 for (bool bRunner : {false, true})
 {
  float Yaw60 = 0.f;
  for (int32 Hz : {60, 30, 120})
  {
   const FResult Right = Simulate(bRunner, 1.f, 3000.f, Hz);
   const FResult Left = Simulate(bRunner, -1.f, 3000.f, Hz);
   const FResult Before = Simulate(bRunner, 1.f, 3000.f, Hz, false, true);
   const FString Label = FString::Printf(TEXT("%s %dHz"), bRunner ? TEXT("Runner") : TEXT("Chaser"), Hz);
   AddInfo(FString::Printf(TEXT("DRIFT_PHYSICS %s right_yaw=%.3f left_yaw=%.3f old_forward_cm=%.3f peak_cm_s=%.3f release_yaw=%.3f counter_deg_s=%.3f grounded=%d"), *Label, Right.Yaw, Left.Yaw, Right.OldForwardDistance, Right.PeakSpeed, Right.ReleaseYaw, Right.CounterYawRate, Right.GroundedFrames));
   AddInfo(FString::Printf(TEXT("DRIFT_BEFORE_AFTER %s before_yaw=%.3f after_yaw=%.3f before_forward_cm=%.3f after_forward_cm=%.3f"), *Label, Before.Yaw, Right.Yaw, Before.OldForwardDistance, Right.OldForwardDistance));
   TestTrue(*(Label + TEXT(" heading improves by at least 25 percent")), Right.Yaw > Before.Yaw * 1.25f);
   // The requested slower entry deliberately carries momentum longer than the
   // previous instant drift response, while still bending it into the turn.
   TestTrue(*(Label + TEXT(" old-forward travel improves by at least 5 percent")), Right.OldForwardDistance < Before.OldForwardDistance * .95f);
   TestTrue(*(Label + TEXT(" real wheel contact throughout turn")), Right.GroundedFrames >= FMath::RoundToInt(.7f * Hz));
   TestTrue(*(Label + TEXT(" turns at least 45 degrees in 0.8 seconds")), Right.Yaw > 45.f);
   TestTrue(*(Label + TEXT(" limits old-forward travel below 22 metres")), Right.OldForwardDistance < 2200.f);
   TestTrue(*(Label + TEXT(" cannot create planar speed")), Right.PeakSpeed <= 3001.f);
   TestTrue(*(Label + TEXT(" mirrored turn response")), FMath::Abs(Right.Yaw + Left.Yaw) < 3.f);
   TestTrue(*(Label + TEXT(" steering release stops continuing spin")), Right.ReleaseYaw < 20.f);
   TestTrue(*(Label + TEXT(" countersteer reverses yaw")), Right.CounterYawRate < 0.f);
   AddInfo(FString::Printf(TEXT("DRIFT_ALIGNMENT %s before_slip_deg=%.3f after_slip_deg=%.3f"), *Label, Before.SlipDegrees, Right.SlipDegrees));
   if (Hz == 60) Yaw60 = Right.Yaw;
   else TestTrue(*(Label + TEXT(" comparable heading across frame rates")), FMath::Abs(Right.Yaw - Yaw60) < 5.f);
  }
  const FResult Normal = Simulate(bRunner, 1.f, 3000.f, 60, false, false, false, false, true);
  const FResult NormalBefore = Simulate(bRunner, 1.f, 3000.f, 60, false, true, false, false, true);
  TestTrue(TEXT("Normal role steering remains identical"), FMath::Abs(Normal.Yaw - NormalBefore.Yaw) < .1f);
  TestTrue(TEXT("Normal role momentum remains identical"), FMath::Abs(Normal.OldForwardDistance - NormalBefore.OldForwardDistance) < .1f);
  const FResult Frozen = Simulate(bRunner, 1.f, 3000.f, 60, false, false, false, true);
  const FResult FrozenBefore = Simulate(bRunner, 1.f, 3000.f, 60, false, true, false, true);
  AddInfo(FString::Printf(TEXT("DRIFT_FREEZE %s before_yaw=%.3f after_yaw=%.3f end_speed_cm_s=%.3f"), bRunner ? TEXT("Runner") : TEXT("Chaser"), FrozenBefore.Yaw, Frozen.Yaw, Frozen.EndVelocity.Size2D()));
  TestTrue(TEXT("Frozen drift steering remains identical"), FMath::Abs(Frozen.Yaw - FrozenBefore.Yaw) < .1f);
  TestTrue(TEXT("Frozen drift momentum remains identical"), FMath::Abs(Frozen.OldForwardDistance - FrozenBefore.OldForwardDistance) < .1f);
  TestTrue(TEXT("IceWave cap remains effective with throttle and boost held"), Frozen.EndVelocity.Size2D() <= 1501.f);
  const FResult HeldRelease = Simulate(bRunner, 1.f, 3000.f, 60, false, false, true);
  AddInfo(FString::Printf(TEXT("DRIFT_HELD_RELEASE %s release_yaw=%.3f counter_deg_s=%.3f"), bRunner ? TEXT("Runner") : TEXT("Chaser"), HeldRelease.ReleaseYaw, HeldRelease.CounterYawRate));
  TestTrue(TEXT("Releasing steering while holding handbrake also stops spin"), HeldRelease.ReleaseYaw < 20.f);
  for (float Speed : {800.f, 6000.f})
  {
   const FResult Edge = Simulate(bRunner, 1.f, Speed, 60);
   AddInfo(FString::Printf(TEXT("DRIFT_SPEED %s start_cm_s=%.0f yaw=%.3f peak_cm_s=%.3f release_yaw=%.3f"), bRunner ? TEXT("Runner") : TEXT("Chaser"), Speed, Edge.Yaw, Edge.PeakSpeed, Edge.ReleaseYaw));
   TestTrue(TEXT("Low/high speed drift turns without running away"), Edge.Yaw > 0.f && Edge.Yaw < 130.f);
   TestTrue(TEXT("Low/high speed drift cannot create speed"), Edge.PeakSpeed <= Speed + 1.f);
  }
  const FResult Air = Simulate(bRunner, 1.f, 3000.f, 60, true);
  AddInfo(FString::Printf(TEXT("DRIFT_PHYSICS %s airborne_yaw=%.3f vertical_cm_s=%.3f"), bRunner ? TEXT("Runner") : TEXT("Chaser"), Air.Yaw, Air.EndVelocity.Z));
  TestEqual(TEXT("No wheel contact in air"), Air.GroundedFrames, 0);
  TestTrue(TEXT("No mid-air yaw assistance"), FMath::Abs(Air.Yaw) < .1f);
  TestTrue(TEXT("Gravity remains active"), Air.EndVelocity.Z < -700.f);
  TestTrue(TEXT("No airborne planar alignment even with sideways momentum"), FMath::Abs(Air.EndVelocity.Y / Air.EndVelocity.X - 400.f / 3000.f) < .001f);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixDriftRampTest, "CitixChase.Drift.ProgressiveSteering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixDriftRampTest::RunTest(const FString& Parameters)
{
 for (bool Runner:{false,true}) for (int32 Hz:{30,60,120}) {
  const auto Held=CitixDriftTest::Simulate(Runner,1.f,3000.f,Hz);
  AddInfo(FString::Printf(TEXT("DRIFT_RAMP runner=%d hz=%d early_steer=%.2f held_yaw=%.2f"),Runner,Hz,Held.EarlySteer,Held.Yaw));
  TestTrue(TEXT("First tenth-second key tap has limited drift steering"),Held.EarlySteer<25.f);
  TestTrue(TEXT("Holding the key still develops a useful turn"),Held.Yaw>35.f);
  TestTrue(TEXT("Releasing the key still arrests yaw"),Held.ReleaseYaw<20.f);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCitixBushDragTest, "CitixChase.Roadside.BushDrag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCitixBushDragTest::RunTest(const FString& Parameters)
{
 for (int32 Hz:{30,60,120}) {
  const auto Road=CitixDriftTest::Simulate(false,0,3000,Hz,false,false,false,false,true);
  const auto Bush=CitixDriftTest::Simulate(false,0,3000,Hz,false,false,false,false,true,true);
  AddInfo(FString::Printf(TEXT("BUSH_DRAG hz=%d road_cm_s=%.2f bush_cm_s=%.2f"),Hz,Road.EndVelocity.Size2D(),Bush.EndVelocity.Size2D()));
  TestTrue(TEXT("Passing through foliage produces substantial drag"),Bush.EndVelocity.Size2D()<Road.EndVelocity.Size2D()*.8f);
  TestTrue(TEXT("Bush remains passable instead of stopping the chassis"),Bush.OldForwardDistance>300.f);
 }
 return true;
}
#endif
