#include "Chase/CitixChaseGameState.h"
#include "Net/UnrealNetwork.h"
#include "Sandbox/CitixPulseBolt.h"
#include "Sandbox/CitixHitSpark.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Player/CitixDrivingPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
void ACitixChaseGameState::MulticastPixelExplosion_Implementation(FVector_NetQuantize Location)
{
	ACitixHitSpark::SpawnPixelBurst(GetWorld(), Location, true);
	if (FParse::Param(FCommandLine::Get(), TEXT("CitixImpactScreenshot")))
	{
		FTimerHandle Timer;
		const FString Tag = GetNetMode() == NM_Client ? TEXT("Guest") : TEXT("Host");
		GetWorldTimerManager().SetTimer(Timer, [Tag]() {
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("PixelExplosion-")+Tag+TEXT(".png")),true,false);
		}, .2f, false);
	}
}
void ACitixChaseGameState::MulticastPistolShot_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, bool bRunnerHit, APlayerState* Shooter)
{
 bool Local=false;
 for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It) if (ACitixDrivingPlayerController* PC=Cast<ACitixDrivingPlayerController>(It->Get()); PC && PC->IsLocalController() && PC->PlayerState==Shooter) {
  Local=true; if (bRunnerHit) PC->ChaseHitConfirmedUntil=GetWorld()->GetTimeSeconds()+.4f;
 }
 if (Shooter && !Local) PlayShotFeedback(GetWorld(),From,false);
	if (bRunnerHit) ACitixHitSpark::SpawnPixelBurst(GetWorld(), To, false, true);
	if (bRunnerHit && From != To && FParse::Param(FCommandLine::Get(), TEXT("CitixImpactScreenshot")))
	{
		FTimerHandle Timer;
		const FString Tag = GetNetMode() == NM_Client ? TEXT("Guest") : TEXT("Host");
		GetWorldTimerManager().SetTimer(Timer, [Tag]() {
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(TEXT("PistolHit-")+Tag+TEXT(".png")),true,false);
		}, .08f, false);
	}
	if (From != To) if (ACitixPulseBolt* Bolt = GetWorld()->SpawnActor<ACitixPulseBolt>())
	{
		Bolt->Fire(From, To, ECitixSurface::EmissiveWarm);
		Bolt->SetLifeSpan(2.f);
	}
}
void ACitixChaseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ACitixChaseGameState, RoomDisplayName); DOREPLIFETIME(ACitixChaseGameState, ImpactSerial); DOREPLIFETIME(ACitixChaseGameState, BreakawayLocations); DOREPLIFETIME(ACitixChaseGameState, BreakawayUntil); DOREPLIFETIME(ACitixChaseGameState, BreakawayReadyAt); DOREPLIFETIME(ACitixChaseGameState, BreakawayWidths); DOREPLIFETIME(ACitixChaseGameState, BreakawayYaws); DOREPLIFETIME(ACitixChaseGameState, Phase); DOREPLIFETIME(ACitixChaseGameState, RoundNumber); DOREPLIFETIME(ACitixChaseGameState, CitySeed); DOREPLIFETIME(ACitixChaseGameState, CityConfigHash); DOREPLIFETIME(ACitixChaseGameState, LayoutRelayLocations); DOREPLIFETIME(ACitixChaseGameState, LayoutExitLocations); DOREPLIFETIME(ACitixChaseGameState, LayoutSpawnLocations); DOREPLIFETIME(ACitixChaseGameState, LayoutReplacementLocations); DOREPLIFETIME(ACitixChaseGameState, ActiveReplacementLocations); DOREPLIFETIME(ACitixChaseGameState, PhaseSecondsRemaining); DOREPLIFETIME(ACitixChaseGameState, CompletedRelays); DOREPLIFETIME(ACitixChaseGameState, ActivatedRelays); DOREPLIFETIME(ACitixChaseGameState, bExitsUnlocked); DOREPLIFETIME(ACitixChaseGameState, StatusText); DOREPLIFETIME(ACitixChaseGameState, bRunnerRevealed); DOREPLIFETIME(ACitixChaseGameState, RevealSecondsRemaining); DOREPLIFETIME(ACitixChaseGameState, NextRevealSecondsRemaining); DOREPLIFETIME(ACitixChaseGameState, bInteractionActive); DOREPLIFETIME(ACitixChaseGameState, InteractionSecondsRemaining); }

void ACitixChaseGameState::PlayShotFeedback(UWorld* World, const FVector& Muzzle, bool Dry)
{
 if (!World || World->GetNetMode()==NM_DedicatedServer) return;
 if (!Dry) for (int I=0; I<3; ++I) if (ACitixHitSpark* Spark=World->SpawnActor<ACitixHitSpark>()) { Spark->Fire(Muzzle,FVector(FMath::RandRange(-80.f,80.f),FMath::RandRange(-80.f,80.f),50.f),ECitixSurface::EmissiveWarm); Spark->SetLifeSpan(.25f); }
 USoundWaveProcedural* Sound=NewObject<USoundWaveProcedural>(World);
 Sound->SetSampleRate(22050); Sound->NumChannels=1; Sound->Duration=Dry ? .04f : .15f;
 TArray<int16> Samples; Samples.SetNum(FMath::RoundToInt(Sound->Duration*22050));
 for (int32 I=0; I<Samples.Num(); ++I) {
  const float T=I/22050.f, Envelope=FMath::Exp(-T*(Dry ? 160.f : 38.f));
  Samples[I]=static_cast<int16>((Dry ? FMath::Sin(T*9000.f) : FMath::FRandRange(-1.f,1.f)*.75f+FMath::Sin(T*700.f)*.25f)*Envelope*(Dry ? 4000.f : 14500.f));
 }
 Sound->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()),Samples.Num()*sizeof(int16));
 UGameplayStatics::PlaySoundAtLocation(World,Sound,Muzzle,.55f);
}
