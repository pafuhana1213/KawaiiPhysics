// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

using UnrealBuildTool;

public class KawaiiPhysicsVerification : ModuleRules
{
	public KawaiiPhysicsVerification(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AnimGraphRuntime",
			"GameplayTags",
			"Json",
			"KawaiiPhysics"
		});
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
