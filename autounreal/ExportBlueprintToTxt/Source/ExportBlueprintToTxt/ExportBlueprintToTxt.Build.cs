// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ExportBlueprintToTxt : ModuleRules
{
	public ExportBlueprintToTxt(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);


		PublicDependencyModuleNames.AddRange(
		new string[]
		{
		"Core",
		"CoreUObject",
		"Engine",
		"InputCore",
		"Slate",
		"SlateCore",
		"EditorStyle",
		"UnrealEd",
		"BlueprintGraph",
		"AssetRegistry",
		"ContentBrowser",
		"LevelEditor",
		"Projects",
		"EditorScriptingUtilities",
		"AssetTools",
		"KismetCompiler",
		"AnimGraph",
		"AnimGraphRuntime",
		
	
		}
	);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"BlueprintGraph" // Add this line
				,"AssetTools"
				,"KismetCompiler"
				,"AnimGraph"
				,"AnimGraphRuntime"
				,"BlueprintEditorLibrary"
				,"Json"
				,"JsonUtilities"
				
				// ... add private dependencies that you statically link with here ...	
			}
			);
  // 添加 ControlRig 相关模块
        PrivateDependencyModuleNames.AddRange(new string[] { "ControlRig", "ControlRigDeveloper" });

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
