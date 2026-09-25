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
			"AssetRegistry",
			"GameplayTags",
			"DeveloperSettings",
			"KawaiiPhysics",
			"KawaiiPhysicsEd",
			"KawaiiPhysicsSequencer"
		});

		PrivateIncludePaths.AddRange(new[]
		{
			Path.Combine(ModuleDirectory, "..", "KawaiiPhysics", "Private"),
			Path.Combine(ModuleDirectory, "..", "KawaiiPhysicsEd", "Private"),
			Path.Combine(ModuleDirectory, "..", "KawaiiPhysicsSequencer", "Private")
		});

		// StructUtils plugin has been integrated into the engine starting from 5.5
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
