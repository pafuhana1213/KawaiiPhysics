// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class KawaiiPhysicsTests : ModuleRules
{
	public KawaiiPhysicsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraphRuntime",
			"PhysicsCore",
			"AnimGraph",
			"BlueprintGraph",
			"UnrealEd",
			"Kismet",
			"MovieScene",
			"Slate",
			"SlateCore",
			"PropertyEditor",
			"AssetRegistry",
			"GameplayTags",
			"DeveloperSettings",
			"TraceLog",
			"KawaiiPhysics",
			"KawaiiPhysicsEd",
			"KawaiiPhysicsSequencer"
		});

		// The tests reach into the shipped plugin's private headers, so both plugins must sit side by side.
		string PluginSourceDirectory = Path.Combine(ModuleDirectory, "..", "..", "..", "KawaiiPhysics", "Source");
		PrivateIncludePaths.AddRange(new[]
		{
			Path.Combine(PluginSourceDirectory, "KawaiiPhysics", "Private"),
			Path.Combine(PluginSourceDirectory, "KawaiiPhysicsEd", "Private"),
			Path.Combine(PluginSourceDirectory, "KawaiiPhysicsSequencer", "Private")
		});

		// StructUtils plugin has been integrated into the engine starting from 5.5
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
