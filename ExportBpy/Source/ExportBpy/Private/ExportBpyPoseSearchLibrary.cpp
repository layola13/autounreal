// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "ExportBpyPoseSearchLibrary.h"

#include "Animation/AnimationAsset.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/Skeleton.h"
#include "EditorAssetLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchRole.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "UObject/UnrealType.h"

namespace
{
template <typename TObjectType>
TObjectType* LoadEditorAssetByPath_ExportBpy(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}

	if (TObjectType* LoadedViaEditorLib = Cast<TObjectType>(UEditorAssetLibrary::LoadAsset(AssetPath)))
	{
		return LoadedViaEditorLib;
	}

	return Cast<TObjectType>(StaticLoadObject(TObjectType::StaticClass(), nullptr, *AssetPath));
}

bool SaveLoadedAsset_ExportBpy(UObject* Asset, FString& OutError)
{
	if (!Asset)
	{
		OutError = TEXT("Asset is null and cannot be saved.");
		return false;
	}

	if (UPackage* Package = Asset->GetOutermost())
	{
		Package->MarkPackageDirty();
	}

	if (!UEditorAssetLibrary::SaveLoadedAsset(Asset))
	{
		OutError = FString::Printf(TEXT("Failed to save asset: %s"), *Asset->GetPathName());
		return false;
	}

	return true;
}
}

bool UExportBpyPoseSearchLibrary::SetPoseSearchSchemaSkeletons(
	const FString& SchemaAssetPath,
	const TArray<FString>& SkeletonAssetPaths,
	const TArray<FString>& RoleNames,
	const TArray<FString>& MirrorDataTableAssetPaths,
	bool bSaveAsset,
	FString& OutError)
{
	OutError.Empty();

	if (SchemaAssetPath.IsEmpty())
	{
		OutError = TEXT("SchemaAssetPath is empty.");
		return false;
	}

	if (SkeletonAssetPaths.Num() == 0)
	{
		OutError = TEXT("SkeletonAssetPaths is empty.");
		return false;
	}

	if (RoleNames.Num() > 0 && RoleNames.Num() != SkeletonAssetPaths.Num())
	{
		OutError = TEXT("RoleNames must be empty or match SkeletonAssetPaths length.");
		return false;
	}

	if (MirrorDataTableAssetPaths.Num() > 0 && MirrorDataTableAssetPaths.Num() != SkeletonAssetPaths.Num())
	{
		OutError = TEXT("MirrorDataTableAssetPaths must be empty or match SkeletonAssetPaths length.");
		return false;
	}

	UPoseSearchSchema* Schema = LoadEditorAssetByPath_ExportBpy<UPoseSearchSchema>(SchemaAssetPath);
	if (!Schema)
	{
		OutError = FString::Printf(TEXT("PoseSearchSchema not found: %s"), *SchemaAssetPath);
		return false;
	}

	FArrayProperty* SkeletonsProperty = FindFProperty<FArrayProperty>(UPoseSearchSchema::StaticClass(), TEXT("Skeletons"));
	if (!SkeletonsProperty)
	{
		OutError = TEXT("UPoseSearchSchema::Skeletons property not found.");
		return false;
	}

	void* SkeletonsArrayAddress = SkeletonsProperty->ContainerPtrToValuePtr<void>(Schema);
	if (!SkeletonsArrayAddress)
	{
		OutError = TEXT("Failed to access UPoseSearchSchema::Skeletons array.");
		return false;
	}

	Schema->Modify();
	FScriptArrayHelper SkeletonsArrayHelper(SkeletonsProperty, SkeletonsArrayAddress);
	SkeletonsArrayHelper.EmptyValues();

	for (int32 Index = 0; Index < SkeletonAssetPaths.Num(); ++Index)
	{
		const FString& SkeletonPath = SkeletonAssetPaths[Index];
		USkeleton* Skeleton = LoadEditorAssetByPath_ExportBpy<USkeleton>(SkeletonPath);
		if (!Skeleton)
		{
			OutError = FString::Printf(TEXT("Skeleton not found at index %d: %s"), Index, *SkeletonPath);
			return false;
		}

		UMirrorDataTable* MirrorDataTable = nullptr;
		if (MirrorDataTableAssetPaths.Num() > 0 && !MirrorDataTableAssetPaths[Index].IsEmpty())
		{
			MirrorDataTable = LoadEditorAssetByPath_ExportBpy<UMirrorDataTable>(MirrorDataTableAssetPaths[Index]);
			if (!MirrorDataTable)
			{
				OutError = FString::Printf(TEXT("MirrorDataTable not found at index %d: %s"), Index, *MirrorDataTableAssetPaths[Index]);
				return false;
			}
		}

		const UE::PoseSearch::FRole Role = (RoleNames.Num() > 0 && !RoleNames[Index].IsEmpty())
			? UE::PoseSearch::FRole(*RoleNames[Index])
			: UE::PoseSearch::DefaultRole;

		Schema->AddSkeleton(Skeleton, MirrorDataTable, Role);
	}

	if (bSaveAsset)
	{
		return SaveLoadedAsset_ExportBpy(Schema, OutError);
	}

	return true;
}

bool UExportBpyPoseSearchLibrary::ReplacePoseSearchDatabaseAnimationAssets(
	const FString& DatabaseAssetPath,
	const TArray<FExportBpyPoseSearchAnimationAssetSpec>& AnimationAssets,
	bool bClearExisting,
	bool bSaveAsset,
	FString& OutError)
{
	OutError.Empty();

	if (DatabaseAssetPath.IsEmpty())
	{
		OutError = TEXT("DatabaseAssetPath is empty.");
		return false;
	}

	UPoseSearchDatabase* Database = LoadEditorAssetByPath_ExportBpy<UPoseSearchDatabase>(DatabaseAssetPath);
	if (!Database)
	{
		OutError = FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *DatabaseAssetPath);
		return false;
	}

	Database->Modify();

	if (bClearExisting)
	{
		for (int32 Index = Database->GetNumAnimationAssets() - 1; Index >= 0; --Index)
		{
			Database->RemoveAnimationAssetAt(Index);
		}
	}

	for (int32 Index = 0; Index < AnimationAssets.Num(); ++Index)
	{
		const FExportBpyPoseSearchAnimationAssetSpec& Spec = AnimationAssets[Index];
		if (Spec.AnimationAssetPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("AnimationAssets[%d].AnimationAssetPath is empty."), Index);
			return false;
		}

		UAnimationAsset* AnimationAsset = LoadEditorAssetByPath_ExportBpy<UAnimationAsset>(Spec.AnimationAssetPath);
		if (!AnimationAsset)
		{
			OutError = FString::Printf(TEXT("Animation asset not found at index %d: %s"), Index, *Spec.AnimationAssetPath);
			return false;
		}

		FPoseSearchDatabaseAnimationAsset DatabaseAsset;
		DatabaseAsset.AnimAsset = AnimationAsset;
		DatabaseAsset.bUseSingleSample = Spec.bUseSingleSample;
		DatabaseAsset.SamplingRange = FFloatInterval(Spec.SamplingRangeMin, Spec.SamplingRangeMax);
		Database->AddAnimationAsset(DatabaseAsset);
	}

	if (bSaveAsset)
	{
		return SaveLoadedAsset_ExportBpy(Database, OutError);
	}

	return true;
}

bool UExportBpyPoseSearchLibrary::ReplacePoseSearchDatabaseAnimationAssetsFromPaths(
	const FString& DatabaseAssetPath,
	const TArray<FString>& AnimationAssetPaths,
	bool bClearExisting,
	bool bSaveAsset,
	FString& OutError)
{
	TArray<FExportBpyPoseSearchAnimationAssetSpec> Specs;
	Specs.Reserve(AnimationAssetPaths.Num());

	for (const FString& AnimationAssetPath : AnimationAssetPaths)
	{
		FExportBpyPoseSearchAnimationAssetSpec& Spec = Specs.AddDefaulted_GetRef();
		Spec.AnimationAssetPath = AnimationAssetPath;
	}

	return ReplacePoseSearchDatabaseAnimationAssets(
		DatabaseAssetPath,
		Specs,
		bClearExisting,
		bSaveAsset,
		OutError);
}

bool UExportBpyPoseSearchLibrary::SaveAssetByPath(const FString& AssetPath, FString& OutError)
{
	OutError.Empty();

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return false;
	}

	return SaveLoadedAsset_ExportBpy(Asset, OutError);
}

