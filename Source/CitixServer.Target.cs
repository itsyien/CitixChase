// Copyright Epic Games, Inc. All Rights Reserved.
// Dedicated-server target (rank 21): `Build.bat CitixServer Win64 Development`
// produces a headless server binary so joins work without editor commands.

using UnrealBuildTool;
using System.Collections.Generic;

public class CitixServerTarget : TargetRules
{
	public CitixServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		// See Citix.Target.cs: keep WITH_LIVE_CODING (and therefore WITH_RELOAD) identical to
		// the engine's precompiled game objects, or the UHT compiled-in registration structs
		// disagree in layout and static init crashes. Off in Shipping/Test, matching the engine.
		bWithLiveCoding = Configuration != UnrealTargetConfiguration.Shipping
		               && Configuration != UnrealTargetConfiguration.Test;

		ExtraModuleNames.Add("Citix");
	}
}
