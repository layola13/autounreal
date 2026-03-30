// Copyright sonygodx@gmail.com. All Rights Reserved.

using UnrealBuildTool;

public class ExportBpy : ModuleRules
{
	public ExportBpy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor
			"UnrealEd",
			"EditorFramework",
			"EditorSubsystem",
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"ContentBrowserData",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"AppFramework",
			"DesktopPlatform",
			"LevelEditor",
			"Projects",
			"PythonScriptPlugin",

			// Blueprint graph
			"BlueprintGraph",
			"KismetCompiler",
			"Kismet",
			"AnimGraph",
			"AnimGraphRuntime",
			"EnhancedInput",

			// JSON
			"Json",
			"JsonUtilities",

			// Scripting utilities (UEditorAssetLibrary)
			"EditorScriptingUtilities",
			"BlueprintEditorLibrary",

			// Control Rig (optional — remove if not needed)
			// "ControlRig",
			// "ControlRigDeveloper",
		});

		// Only build in editor
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
