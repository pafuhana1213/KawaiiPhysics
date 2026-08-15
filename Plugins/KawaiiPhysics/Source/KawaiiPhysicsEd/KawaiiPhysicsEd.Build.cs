// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

using UnrealBuildTool;

public class KawaiiPhysicsEd : ModuleRules
{
	public KawaiiPhysicsEd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"KawaiiPhysics",
			"GameplayTags"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"InputCore",
			"AnimGraph",
			"BlueprintGraph",
			"Persona",
			"UnrealEd",
			"AnimGraphRuntime",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"Settings",
			"PropertyEditor",
			"ContentBrowser",
			"SourceControl",
			"AssetRegistry",
			"ApplicationCore",
			"Kismet",
			"ToolMenus",
			"Json",
			"JsonUtilities",
			"DesktopPlatform",
			"Projects",
			"WorkspaceMenuStructure"
		});

		if (Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 5))
		{
			PublicDefinitions.Add("KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED=1");
		}
		else
		{
			PublicDefinitions.Add("KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED=0");
		}

		if(Target.Version.MajorVersion >= 5)
		{
			PrivateDependencyModuleNames.Add("EditorFramework");
			// AnimationEditMode was split into its own module starting from 5.1
			if (Target.Version.MajorVersion > 5 || Target.Version.MinorVersion >= 1)
			{
				PrivateDependencyModuleNames.Add("AnimationEditMode");
			}

			// StructUtils plugin has been integrated into the engine starting from 5.5
			if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
			{
				PrivateDependencyModuleNames.Add("StructUtils");
			}
		}
	}
}
