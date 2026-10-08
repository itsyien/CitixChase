// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class CitixTarget : TargetRules
{
    public CitixTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

        // Match the engine's precompiled UnrealGame objects. This controls WITH_LIVE_CODING,
        // which in turn controls WITH_RELOAD. WITH_RELOAD changes the layout of the UHT
        // registration structs (FStructRegisterCompiledInInfo / FClassRegisterCompiledInInfo /
        // FStructReloadVersionInfo), so if the project module disagrees with the engine objects
        // it was linked against, CoreUObject walks the compiled-in registration arrays with the
        // wrong stride and crashes during static init (exit 777006 / CrashDuringStaticInit).
        // Live coding is a Development/Test feature and is off in Shipping, which the engine's
        // Shipping objects also assume, so mirror that split rather than forcing it on.
        bWithLiveCoding = Configuration != UnrealTargetConfiguration.Shipping
                       && Configuration != UnrealTargetConfiguration.Test;

        ExtraModuleNames.Add("Citix");
    }
}
