// Copyright sonygodx@gmail.com. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ExportBlueprintToTxtLibrary.generated.h"

class UBlueprint;
class UAnimBlueprint;
class USkeleton;

UCLASS()
class EXPORTBLUEPRINTTOTXT_API UExportBlueprintToTxtLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Create an AnimBlueprint asset at /Game/... path, using provided skeleton.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static UAnimBlueprint* CreateAnimBlueprintAsset(const FString& AssetPath, USkeleton* TargetSkeleton);

	// Copy variables and implemented interfaces from source to target.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool CopyVariablesAndInterfaces(UBlueprint* SourceBlueprint, UBlueprint* TargetBlueprint);

	// Ensure a function graph exists on target blueprint, using source graph schema if provided.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool EnsureFunctionGraph(UBlueprint* TargetBlueprint, UBlueprint* SourceBlueprint, const FString& GraphName);

	// Import nodes from text into a named graph. Optionally clears graph first.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool ImportNodesFromText(UBlueprint* TargetBlueprint, const FString& GraphName, const FString& Text, bool bClearGraph);

	// Compile blueprint.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool CompileBlueprint(UBlueprint* TargetBlueprint);

	// Export one supported asset or a /Game folder (recursively, including subfolders) to text outputs using the plugin exporter.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool ExportBlueprintAssetToText(const FString& Target, FString& OutResolvedBlueprintPath, FString& OutError);

	// Export all function graphs from a Blueprint into a dedicated /Game asset folder as real Blueprint Function Library assets.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool ExportBlueprintFunctionsToDirectory(
		const FString& BlueprintTarget,
		const FString& OutputDirectory,
		FString& OutResolvedBlueprintPath,
		FString& OutResolvedDirectory,
		FString& OutManifestPath,
		int32& OutExportedFunctionCount,
		FString& OutError);

	// Import previously split Blueprint function assets back into the original Blueprint.
	UFUNCTION(BlueprintCallable, Category = "ExportBlueprintToTxt")
	static bool ImportBlueprintFunctionsFromDirectory(
		const FString& InputDirectory,
		const FString& BlueprintTarget,
		FString& OutResolvedBlueprintPath,
		int32& OutImportedFunctionCount,
		FString& OutError);
};
