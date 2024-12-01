// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

using UnrealBuildTool;

public class KawaiiPhysicsEd : ModuleRules
{
	public KawaiiPhysicsEd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "KawaiiPhysics" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AnimGraph", "BlueprintGraph", "Persona", "UnrealEd", "AnimGraphRuntime", "Slate", "SlateCore",
			"StructUtils"
		});

		BuildVersion Version;
		if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
			if (Version.MajorVersion == 5)
			{
				PrivateDependencyModuleNames.AddRange(new[] { "EditorFramework" });

				// From UE5.1, BaseClass of EditMode move to new Module 
				if (Version.MinorVersion >= 1) PrivateDependencyModuleNames.AddRange(new[] { "AnimationEditMode" });
			}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}