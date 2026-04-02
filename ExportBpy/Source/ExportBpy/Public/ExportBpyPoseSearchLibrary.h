// Copyright sonygodx@gmail.com. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ExportBpyPoseSearchLibrary.generated.h"

USTRUCT(BlueprintType)
struct EXPORTBPY_API FExportBpyPoseSearchAnimationAssetSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExportBpy|PoseSearch")
	FString AnimationAssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExportBpy|PoseSearch")
	bool bUseSingleSample = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExportBpy|PoseSearch")
	float SamplingRangeMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExportBpy|PoseSearch")
	float SamplingRangeMax = 0.0f;
};

/**
 * Thin wrappers for PoseSearch editor workflows that are not exposed by Unreal Python.
 */
UCLASS()
class EXPORTBPY_API UExportBpyPoseSearchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "ExportBpy|PoseSearch")
	static bool SetPoseSearchSchemaSkeletons(
		const FString& SchemaAssetPath,
		const TArray<FString>& SkeletonAssetPaths,
		const TArray<FString>& RoleNames,
		const TArray<FString>& MirrorDataTableAssetPaths,
		bool bSaveAsset,
		FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "ExportBpy|PoseSearch")
	static bool ReplacePoseSearchDatabaseAnimationAssets(
		const FString& DatabaseAssetPath,
		const TArray<FExportBpyPoseSearchAnimationAssetSpec>& AnimationAssets,
		bool bClearExisting,
		bool bSaveAsset,
		FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "ExportBpy|PoseSearch")
	static bool ReplacePoseSearchDatabaseAnimationAssetsFromPaths(
		const FString& DatabaseAssetPath,
		const TArray<FString>& AnimationAssetPaths,
		bool bClearExisting,
		bool bSaveAsset,
		FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "ExportBpy|PoseSearch")
	static bool SaveAssetByPath(const FString& AssetPath, FString& OutError);
};

