#include "Editor/CitixEOSDeviceProofCommandlet.h"
#include "Citix.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#if WITH_EDITOR && WITH_EOS_SDK
#include "Network/CitixEOSDeviceProofCore.h"
#endif

UCitixEOSDeviceProofCommandlet::UCitixEOSDeviceProofCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    // EOS logs expected DuplicateNotAllowed at Error severity on repeat runs.
    // The actual Connect result + LoggedIn validation determines proof success.
    UseCommandletResultAsExitCode = true;
    ShowErrorCount = false;
}

int32 UCitixEOSDeviceProofCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR && WITH_EOS_SDK
    FString Config, Output, Device;
    FParse::Value(*Params, TEXT("EOSConfig="), Config);
    FParse::Value(*Params, TEXT("EOSReport="), Output);
    FParse::Value(*Params, TEXT("EOSDeviceTag="), Device);
    if (Config.IsEmpty() || Output.IsEmpty() || Device.IsEmpty())
    {
        UE_LOG(LogCitix, Error, TEXT("[CitixEOSProof] Supply EOSConfig, EOSReport and EOSDeviceTag via Tools/run_eos_device_proof.ps1. Never put a client secret on the command line."));
        return 2;
    }
    Config = FPaths::ConvertRelativePathToFull(Config);
    Output = FPaths::ConvertRelativePathToFull(Output);
    const int Code = CitixEOSProof::Run(TCHAR_TO_UTF8(*Config), TCHAR_TO_UTF8(*Output),
        TCHAR_TO_UTF8(*Device), "UnrealCommandlet", TCHAR_TO_UTF8(*FEngineVersion::Current().ToString()));
    UE_LOG(LogCitix, Display, TEXT("[CitixEOSProof] %s; report=%s; exit=%d"),
        Code == 0 ? TEXT("Connected with Device ID") : TEXT("Proof failed; inspect report stage/error"), *Output, Code);
    return Code;
#else
    UE_LOG(LogCitix, Error, TEXT("[CitixEOSProof] Requires editor build with EOS SDK."));
    return 2;
#endif
}
