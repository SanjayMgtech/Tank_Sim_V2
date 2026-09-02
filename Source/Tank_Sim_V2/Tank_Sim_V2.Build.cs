// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Tank_Sim_V2 : ModuleRules
{
	public Tank_Sim_V2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Lets code under Core/, Player/, Tank/, etc. #include its own siblings as "Core/TSTypes.h"
		// relative to the module root, matching the Section 14 folder layout.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UMG",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"HeadMountedDisplay",
		});

		// The VoiceChat module is header-only (interfaces resolved at runtime via IModularFeatures) and
		// is ClientOnly, so it is referenced as an include path rather than a normal link dependency -
		// this matches Epic's own EOSVoiceChat.Build.cs.
		PrivateIncludePathModuleNames.AddRange(new string[] { "VoiceChat" });

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
