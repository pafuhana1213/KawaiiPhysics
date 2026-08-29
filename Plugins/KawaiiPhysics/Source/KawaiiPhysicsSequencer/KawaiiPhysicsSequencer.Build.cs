// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

using UnrealBuildTool;

public class KawaiiPhysicsSequencer : ModuleRules
{
	public KawaiiPhysicsSequencer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MovieScene",
				"KawaiiPhysics",
				"GameplayTags",
			}
		);
	}
}
