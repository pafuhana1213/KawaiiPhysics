// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

using UnrealBuildTool;

public class KawaiiPhysics : ModuleRules
{
	public KawaiiPhysics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"AnimGraphRuntime",
				"GameplayTags"
			}
		);

		// StructUtils plugin has been integrated into the engine starting from 5.5
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore"
			}
		);

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.Add("UnrealEd");
		}
	}
}