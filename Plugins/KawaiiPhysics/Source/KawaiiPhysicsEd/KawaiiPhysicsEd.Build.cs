// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class KawaiiPhysicsEd : ModuleRules
{
	public KawaiiPhysicsEd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "KawaiiPhysics" });
        PrivateDependencyModuleNames.AddRange(new string[] { "AnimGraph", "BlueprintGraph", "Persona", "UnrealEd", "AnimGraphRuntime", "SlateCore"});

        if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out var Version))
        {
            if (Version.MajorVersion == 5)
            {
				PrivateDependencyModuleNames.AddRange(new string[] { "EditorFramework" });

				if (Version.MinorVersion >= 1)
				{
					PrivateDependencyModuleNames.AddRange(new string[] { "AnimationEditMode" });
				}
			}
        }

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
