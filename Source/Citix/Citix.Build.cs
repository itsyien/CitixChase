// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Note: touch this file when adding new source directories so UBT regenerates the makefile.
public class Citix : ModuleRules
{
	public Citix(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Procedural generators use file-local helpers with overlapping names.
		// Compile each translation unit independently as new source files are added.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AudioMixer",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"PhysicsCore",
			"NavigationSystem",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
            "Sockets"
		});

		PublicDependencyModuleNames.AddRange(new string[] { "EOSShared", "SocketSubsystemEOS" });
		PrivateDependencyModuleNames.AddRange(new string[] { "EOSSDK" });

		// Editor-only tooling (material authoring commandlet).
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MaterialEditor", "AssetTools", "MeshDescription", "StaticMeshDescription" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"Citix",
			"Citix/Core",
			"Citix/City",
			"Citix/Character",
			"Citix/Pedestrian",
			"Citix/Traffic",
			"Citix/Vehicle",
			"Citix/World",
			"Citix/Player",
			"Citix/Variant_Horror",
			"Citix/Variant_Horror/UI",
			"Citix/Variant_Shooter",
			"Citix/Variant_Shooter/AI",
			"Citix/Variant_Shooter/UI",
			"Citix/Variant_Shooter/Weapons"
		});
	}
}
