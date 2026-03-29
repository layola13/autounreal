// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "ExportBlueprintToTxtLibrary.h"
#include "ExportBlueprintToTxt.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditorLibrary.h"
#include "Blueprint/BlueprintSupport.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	static FString NormalizeExportTarget(const FString& Target)
	{
		FString Normalized = Target;
		Normalized.TrimStartAndEndInline();
		Normalized.ReplaceInline(TEXT("/All/Game/"), TEXT("/Game/"));
		if (Normalized.Equals(TEXT("/All/Game"), ESearchCase::IgnoreCase))
		{
			Normalized = TEXT("/Game");
		}

		while (Normalized.Len() > 1 && Normalized.EndsWith(TEXT("/")))
		{
			Normalized.LeftChopInline(1, EAllowShrinking::No);
		}

		return Normalized;
	}

	static bool DoesLibraryClassMatchByName(const UClass* Class, const FName ExpectedClassName)
	{
		for (const UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			if (CurrentClass->GetFName() == ExpectedClassName)
			{
				return true;
			}
		}

		return false;
	}

	static bool IsSupportedExportAsset(const UObject* AssetObject)
	{
		if (!AssetObject)
		{
			return false;
		}

		const UClass* AssetClass = AssetObject->GetClass();
		return Cast<UBlueprint>(AssetObject) != nullptr
			|| Cast<UUserDefinedEnum>(AssetObject) != nullptr
			|| Cast<UUserDefinedStruct>(AssetObject) != nullptr
			|| DoesLibraryClassMatchByName(AssetClass, TEXT("ChooserTable"))
			|| DoesLibraryClassMatchByName(AssetClass, TEXT("ProxyTable"))
			|| DoesLibraryClassMatchByName(AssetClass, TEXT("PoseSearchDatabase"))
			|| DoesLibraryClassMatchByName(AssetClass, TEXT("PoseSearchSchema"));
	}

	static void AddSupportedExportClassPaths(FARFilter& Filter)
	{
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UUserDefinedEnum::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Chooser"), TEXT("ChooserTable")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Chooser"), TEXT("ProxyTable")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/PoseSearch"), TEXT("PoseSearchDatabase")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/PoseSearch"), TEXT("PoseSearchSchema")));
	}

	static bool ResolveSupportedAssetsInFolder(const FString& FolderPath, TArray<FAssetData>& OutAssets, FString& OutError)
	{
		OutAssets.Reset();
		OutError.Reset();

		const FString NormalizedFolderPath = NormalizeExportTarget(FolderPath);
		if (!NormalizedFolderPath.StartsWith(TEXT("/Game")))
		{
			OutError = FString::Printf(TEXT("Folder target must be under /Game: %s"), *FolderPath);
			return false;
		}

		if (!UEditorAssetLibrary::DoesDirectoryExist(NormalizedFolderPath))
		{
			OutError = FString::Printf(TEXT("Folder not found: %s"), *NormalizedFolderPath);
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(*NormalizedFolderPath);
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		AddSupportedExportClassPaths(Filter);

		AssetRegistry.GetAssets(Filter, OutAssets);
		OutAssets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
		});

		if (OutAssets.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No supported assets found in folder: %s"), *NormalizedFolderPath);
			return false;
		}

		return true;
	}

	static UObject* ResolveExportTargetAsset(const FString& Target)
	{
		const FString NormalizedTarget = NormalizeExportTarget(Target);

		UObject* TargetAsset = UEditorAssetLibrary::LoadAsset(NormalizedTarget);
		if (TargetAsset)
		{
			return TargetAsset;
		}

		if (!NormalizedTarget.StartsWith(TEXT("/")))
		{
			return nullptr;
		}

		FString ObjectPath = NormalizedTarget;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(ObjectPath);
			if (!AssetName.IsEmpty())
			{
				ObjectPath += TEXT(".") + AssetName;
			}
		}

		return LoadObject<UObject>(nullptr, *ObjectPath);
	}

	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	static void ClearGraphNodes(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		if (!Blueprint || !Graph)
		{
			return;
		}

		TArray<UEdGraphNode*> NodesToRemove = Graph->Nodes;
		for (UEdGraphNode* Node : NodesToRemove)
		{
			if (Node)
			{
				FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
			}
		}
	}

	static bool IsFunctionRootNode(const UEdGraphNode* Node)
	{
		return Node
			&& (Cast<UK2Node_FunctionEntry>(Node) != nullptr
				|| Cast<UK2Node_FunctionResult>(Node) != nullptr);
	}

	static void ClearGraphNodesPreservingFunctionRoots(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		if (!Blueprint || !Graph)
		{
			return;
		}

		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && !IsFunctionRootNode(Node))
			{
				NodesToRemove.Add(Node);
			}
		}

		for (UEdGraphNode* Node : NodesToRemove)
		{
			FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
		}
	}

	static UBlueprint* ResolveBlueprintAsset(const FString& BlueprintTarget, FString& OutResolvedBlueprintPath)
	{
		OutResolvedBlueprintPath.Reset();

		UObject* TargetAsset = ResolveExportTargetAsset(BlueprintTarget);
		UBlueprint* Blueprint = Cast<UBlueprint>(TargetAsset);
		if (!Blueprint)
		{
			return nullptr;
		}

		OutResolvedBlueprintPath = Blueprint->GetPathName();
		return Blueprint;
	}

	static FString NormalizeAssetDirectory(const FString& DirectoryPath)
	{
		FString Normalized = NormalizeExportTarget(DirectoryPath);
		if (Normalized.IsEmpty())
		{
			return Normalized;
		}

		if (Normalized.Contains(TEXT(".")))
		{
			Normalized = FPackageName::GetLongPackagePath(Normalized);
		}

		while (Normalized.Len() > 1 && Normalized.EndsWith(TEXT("/")))
		{
			Normalized.LeftChopInline(1, EAllowShrinking::No);
		}

		return Normalized;
	}

	static FString GetBlueprintAssetPackagePath(const UBlueprint* Blueprint)
	{
		return Blueprint ? FPackageName::ObjectPathToPackageName(Blueprint->GetPathName()) : FString();
	}

	static FString GetDefaultFunctionExportDirectory(const UBlueprint* Blueprint)
	{
		const FString SourceAssetPath = GetBlueprintAssetPackagePath(Blueprint);
		const FString SourceFolder = FPackageName::GetLongPackagePath(SourceAssetPath);
		const FString SourceAssetName = FPackageName::GetLongPackageAssetName(SourceAssetPath);
		return SourceFolder / FString::Printf(TEXT("%s_func"), *SourceAssetName);
	}

	static bool EnsureAssetDirectory(const FString& DirectoryPath, FString& OutError)
	{
		OutError.Reset();

		if (DirectoryPath.IsEmpty())
		{
			OutError = TEXT("Asset directory path is empty");
			return false;
		}

		if (!DirectoryPath.StartsWith(TEXT("/Game")))
		{
			OutError = FString::Printf(TEXT("Asset directory must be under /Game: %s"), *DirectoryPath);
			return false;
		}

		if (UEditorAssetLibrary::DoesDirectoryExist(DirectoryPath))
		{
			return true;
		}

		if (!UEditorAssetLibrary::MakeDirectory(DirectoryPath))
		{
			OutError = FString::Printf(TEXT("Failed to create asset directory: %s"), *DirectoryPath);
			return false;
		}

		return true;
	}

	static FString GetManifestFilePathForAssetDirectory(const FString& AssetDirectory)
	{
		const FString DiskDirectory = FPackageName::LongPackageNameToFilename(AssetDirectory, TEXT(""));
		return FPaths::Combine(DiskDirectory, TEXT("manifest.json"));
	}

	static FString BuildRawGraphTextForExport(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return FString();
		}

		TSet<UObject*> NodesToExport;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				NodesToExport.Add(Node);
				Node->PrepareForCopying();
			}
		}

		if (NodesToExport.IsEmpty())
		{
			return FString();
		}

		for (UObject* Node : NodesToExport)
		{
			Node->Mark(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));
		}

		FString RawText;
		FEdGraphUtilities::ExportNodesToText(NodesToExport, RawText);
		return RawText;
	}

	static void GatherFunctionGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutFunctionGraphs)
	{
		OutFunctionGraphs.Reset();
		if (!Blueprint)
		{
			return;
		}

		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				OutFunctionGraphs.Add(Graph);
			}
		}
	}

	static FString SanitizeBlueprintAssetName(const FString& Input)
	{
		FString Result;
		Result.Reserve(Input.Len() + 8);

		for (const TCHAR Char : Input)
		{
			if (FChar::IsAlnum(Char) || Char == TEXT('_'))
			{
				Result.AppendChar(Char);
			}
			else
			{
				Result.AppendChar(TEXT('_'));
			}
		}

		if (Result.IsEmpty())
		{
			Result = TEXT("FunctionBlueprint");
		}
		else if (FChar::IsDigit(Result[0]))
		{
			Result = TEXT("BP_") + Result;
		}

		return Result;
	}

	static FString MakeUniqueFunctionAssetName(const FString& GraphName, TSet<FString>& UsedAssetNames)
	{
		const FString SanitizedBase = SanitizeBlueprintAssetName(GraphName.IsEmpty() ? TEXT("Function") : GraphName);
		FString Candidate = SanitizedBase;
		int32 Suffix = 2;

		while (UsedAssetNames.Contains(Candidate))
		{
			Candidate = FString::Printf(TEXT("%s_%d"), *SanitizedBase, Suffix++);
		}

		UsedAssetNames.Add(Candidate);
		return Candidate;
	}

	static TSet<FString> CollectFunctionDependencyClosure(UBlueprint* Blueprint, const FString& PrimaryFunctionName)
	{
		TSet<FString> FunctionsToKeep;
		if (!Blueprint || PrimaryFunctionName.IsEmpty())
		{
			return FunctionsToKeep;
		}

		TMap<FString, UEdGraph*> GraphByName;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				GraphByName.Add(Graph->GetName(), Graph);
			}
		}

		TArray<FString> PendingFunctions;
		PendingFunctions.Add(PrimaryFunctionName);

		while (PendingFunctions.Num() > 0)
		{
			const FString FunctionName = PendingFunctions.Pop(EAllowShrinking::No);
			if (FunctionsToKeep.Contains(FunctionName))
			{
				continue;
			}

			FunctionsToKeep.Add(FunctionName);

			UEdGraph* Graph = GraphByName.FindRef(FunctionName);
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
				if (!CallFunctionNode)
				{
					continue;
				}

				const FString CalledFunctionName = CallFunctionNode->FunctionReference.GetMemberName().ToString();
				if (!CalledFunctionName.IsEmpty() && GraphByName.Contains(CalledFunctionName) && !FunctionsToKeep.Contains(CalledFunctionName))
				{
					PendingFunctions.Add(CalledFunctionName);
				}
			}
		}

		return FunctionsToKeep;
	}

	static bool BuildOrderedFunctionDependencyList(
		UBlueprint* Blueprint,
		const FString& PrimaryFunctionName,
		TArray<FString>& OutIncludedFunctions,
		FString& OutError)
	{
		OutIncludedFunctions.Reset();
		OutError.Reset();

		if (!Blueprint)
		{
			OutError = TEXT("Invalid Blueprint");
			return false;
		}

		const TSet<FString> FunctionsToKeep = CollectFunctionDependencyClosure(Blueprint, PrimaryFunctionName);
		if (!FunctionsToKeep.Contains(PrimaryFunctionName))
		{
			OutError = FString::Printf(TEXT("Primary function not found in Blueprint: %s"), *PrimaryFunctionName);
			return false;
		}

		TArray<UEdGraph*> OrderedGraphs;
		GatherFunctionGraphs(Blueprint, OrderedGraphs);
		for (UEdGraph* Graph : OrderedGraphs)
		{
			if (Graph && FunctionsToKeep.Contains(Graph->GetName()))
			{
				OutIncludedFunctions.Add(Graph->GetName());
			}
		}

		if (OutIncludedFunctions.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No functions collected for export: %s"), *PrimaryFunctionName);
			return false;
		}

		return true;
	}

	static UBlueprint* CreateFunctionBlueprintAsset(const FString& AssetPath, FString& OutError)
	{
		OutError.Reset();
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("Function Blueprint asset path is empty");
			return nullptr;
		}

		UBlueprint* NewBlueprint = UBlueprintEditorLibrary::CreateBlueprintAssetWithParent(
			AssetPath,
			UBlueprintFunctionLibrary::StaticClass());
		if (!NewBlueprint)
		{
			OutError = FString::Printf(TEXT("Failed to create Blueprint Function Library asset: %s"), *AssetPath);
		}

		return NewBlueprint;
	}

	static bool SaveJsonObjectToFile(const TSharedPtr<FJsonObject>& JsonObject, const FString& FilePath, FString& OutError)
	{
		OutError.Reset();
		if (!JsonObject.IsValid())
		{
			OutError = TEXT("Invalid JSON object");
			return false;
		}

		FString JsonString;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
		{
			OutError = FString::Printf(TEXT("Failed to serialize JSON for file: %s"), *FilePath);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(JsonString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8))
		{
			OutError = FString::Printf(TEXT("Failed to save file: %s"), *FilePath);
			return false;
		}

		return true;
	}

	static bool LoadJsonObjectFromFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJsonObject, FString& OutError)
	{
		OutError.Reset();
		OutJsonObject.Reset();

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse JSON file: %s"), *FilePath);
			return false;
		}

		return true;
	}
}

UAnimBlueprint* UExportBlueprintToTxtLibrary::CreateAnimBlueprintAsset(const FString& AssetPath, USkeleton* TargetSkeleton)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	if (TargetSkeleton)
	{
		Factory->TargetSkeleton = TargetSkeleton;
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	return Cast<UAnimBlueprint>(NewAsset);
}

bool UExportBlueprintToTxtLibrary::CopyVariablesAndInterfaces(UBlueprint* SourceBlueprint, UBlueprint* TargetBlueprint)
{
	if (!SourceBlueprint || !TargetBlueprint)
	{
		return false;
	}

	TargetBlueprint->Modify();

	// Copy variables
	TargetBlueprint->NewVariables = SourceBlueprint->NewVariables;

	// Copy implemented interfaces using utility to ensure proper setup
	for (const FBPInterfaceDescription& InterfaceDesc : SourceBlueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface)
		{
			// UE5.4 expects interface by name/path, not UClass*
			FBlueprintEditorUtils::ImplementNewInterface(
				TargetBlueprint,
				InterfaceDesc.Interface->GetClassPathName());
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
	return true;
}

bool UExportBlueprintToTxtLibrary::EnsureFunctionGraph(UBlueprint* TargetBlueprint, UBlueprint* SourceBlueprint, const FString& GraphName)
{
	if (!TargetBlueprint || GraphName.IsEmpty())
	{
		return false;
	}

	UEdGraph* Existing = FindGraphByName(TargetBlueprint, GraphName);
	if (Existing)
	{
		return true;
	}

	UEdGraph* SourceGraph = SourceBlueprint ? FindGraphByName(SourceBlueprint, GraphName) : nullptr;
	UClass* SchemaClass = SourceGraph && SourceGraph->GetSchema()
		? SourceGraph->GetSchema()->GetClass()
		: UEdGraphSchema_K2::StaticClass();

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		TargetBlueprint,
		FName(*GraphName),
		UEdGraph::StaticClass(),
		SchemaClass);

	if (!NewGraph)
	{
		return false;
	}

	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(TargetBlueprint, NewGraph, /*bIsUserCreated*/ true, nullptr);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
	return true;
}

bool UExportBlueprintToTxtLibrary::ImportNodesFromText(UBlueprint* TargetBlueprint, const FString& GraphName, const FString& Text, bool bClearGraph)
{
	if (!TargetBlueprint || GraphName.IsEmpty())
	{
		return false;
	}

	UEdGraph* Graph = FindGraphByName(TargetBlueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	TargetBlueprint->Modify();
	Graph->Modify();

	if (bClearGraph)
	{
		ClearGraphNodesPreservingFunctionRoots(TargetBlueprint, Graph);
	}

	TSet<UEdGraphNode*> ImportedNodes;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, ImportedNodes);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
	return true;
}

bool UExportBlueprintToTxtLibrary::CompileBlueprint(UBlueprint* TargetBlueprint)
{
	if (!TargetBlueprint)
	{
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(TargetBlueprint);
	return true;
}

bool UExportBlueprintToTxtLibrary::ExportBlueprintAssetToText(const FString& Target, FString& OutResolvedBlueprintPath, FString& OutError)
{
	OutResolvedBlueprintPath.Reset();
	OutError.Reset();

	const FString TrimmedTarget = NormalizeExportTarget(Target);
	if (TrimmedTarget.IsEmpty())
	{
		OutError = TEXT("Target is empty");
		return false;
	}

	FExportBlueprintToTxtModule* ExportModule = FModuleManager::LoadModulePtr<FExportBlueprintToTxtModule>(TEXT("ExportBlueprintToTxt"));
	if (!ExportModule)
	{
		OutError = TEXT("Failed to load ExportBlueprintToTxt module");
		return false;
	}

	if (UEditorAssetLibrary::DoesDirectoryExist(TrimmedTarget))
	{
		TArray<FAssetData> AssetsToExport;
		if (!ResolveSupportedAssetsInFolder(TrimmedTarget, AssetsToExport, OutError))
		{
			return false;
		}

		ExportModule->ExportAssetsToText(AssetsToExport);
		OutResolvedBlueprintPath = TrimmedTarget;
		return true;
	}

	UObject* TargetAsset = ResolveExportTargetAsset(TrimmedTarget);
	if (!TargetAsset)
	{
		OutError = FString::Printf(TEXT("Asset or folder not found: %s"), *TrimmedTarget);
		return false;
	}

	if (!IsSupportedExportAsset(TargetAsset))
	{
		OutError = FString::Printf(TEXT("Unsupported asset type: %s"), *TargetAsset->GetClass()->GetPathName());
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.WaitForCompletion();
	}

	FAssetData TargetAssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(TargetAsset->GetPathName()));
	if (!TargetAssetData.IsValid())
	{
		TargetAssetData = FAssetData(TargetAsset);
	}
	if (!TargetAssetData.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to resolve asset data for asset: %s"), *TargetAsset->GetPathName());
		return false;
	}

	TArray<FAssetData> AssetsToExport;
	AssetsToExport.Add(TargetAssetData);
	ExportModule->ExportAssetsToText(AssetsToExport);

	OutResolvedBlueprintPath = TargetAsset->GetPathName();
	return true;
}

bool UExportBlueprintToTxtLibrary::ExportBlueprintFunctionsToDirectory(
	const FString& BlueprintTarget,
	const FString& OutputDirectory,
	FString& OutResolvedBlueprintPath,
	FString& OutResolvedDirectory,
	FString& OutManifestPath,
	int32& OutExportedFunctionCount,
	FString& OutError)
{
	OutResolvedBlueprintPath.Reset();
	OutResolvedDirectory.Reset();
	OutManifestPath.Reset();
	OutError.Reset();
	OutExportedFunctionCount = 0;

	const FString TrimmedTarget = NormalizeExportTarget(BlueprintTarget);
	if (TrimmedTarget.IsEmpty())
	{
		OutError = TEXT("Blueprint target is empty");
		return false;
	}

	UBlueprint* Blueprint = ResolveBlueprintAsset(TrimmedTarget, OutResolvedBlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *TrimmedTarget);
		return false;
	}

	FString TrimmedOutputDirectory = OutputDirectory;
	TrimmedOutputDirectory.TrimStartAndEndInline();
	OutResolvedDirectory = TrimmedOutputDirectory.IsEmpty()
		? GetDefaultFunctionExportDirectory(Blueprint)
		: NormalizeAssetDirectory(TrimmedOutputDirectory);

	if (!EnsureAssetDirectory(OutResolvedDirectory, OutError))
	{
		return false;
	}

	TArray<UEdGraph*> FunctionGraphs;
	GatherFunctionGraphs(Blueprint, FunctionGraphs);
	const int32 TotalFunctionCount = FunctionGraphs.Num();

	FScopedSlowTask SlowTask(
		TotalFunctionCount > 0 ? static_cast<float>(TotalFunctionCount) : 1.0f,
		FText::FromString(FString::Printf(TEXT("Splitting Blueprint functions for %s"), *Blueprint->GetName())));
	SlowTask.MakeDialog(true);

	TSharedPtr<FJsonObject> ManifestObject = MakeShared<FJsonObject>();
	ManifestObject->SetNumberField(TEXT("version"), 1);
	ManifestObject->SetStringField(TEXT("source_blueprint"), OutResolvedBlueprintPath);
	ManifestObject->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	ManifestObject->SetStringField(TEXT("export_directory"), OutResolvedDirectory);

	TArray<TSharedPtr<FJsonValue>> FunctionArray;
	TSet<FString> UsedAssetNames;

	for (int32 FunctionIndex = 0; FunctionIndex < FunctionGraphs.Num(); ++FunctionIndex)
	{
		UEdGraph* Graph = FunctionGraphs[FunctionIndex];
		if (!Graph)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Skipping invalid function graph")));
			continue;
		}

		const int32 Percent = TotalFunctionCount > 0
			? FMath::Clamp(FMath::RoundToInt(((FunctionIndex + 1) * 100.0f) / TotalFunctionCount), 0, 100)
			: 100;
		const FString ProgressText = FString::Printf(
			TEXT("Exporting function %d/%d (%d%%): %s"),
			FunctionIndex + 1,
			TotalFunctionCount,
			Percent,
			*Graph->GetName());
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(ProgressText));
		UE_LOG(LogTemp, Display, TEXT("ExportBlueprintFunctionsToDirectory: %s"), *ProgressText);

		const FString AssetName = MakeUniqueFunctionAssetName(Graph->GetName(), UsedAssetNames);
		const FString AssetPath = OutResolvedDirectory / AssetName;

		if (UEditorAssetLibrary::DoesAssetExist(AssetPath) && !UEditorAssetLibrary::DeleteAsset(AssetPath))
		{
			OutError = FString::Printf(TEXT("Failed to delete existing split Blueprint asset: %s"), *AssetPath);
			return false;
		}

		UBlueprint* SplitBlueprint = CreateFunctionBlueprintAsset(AssetPath, OutError);
		if (!SplitBlueprint)
		{
			return false;
		}

		TArray<FString> IncludedFunctions;
		if (!BuildOrderedFunctionDependencyList(Blueprint, Graph->GetName(), IncludedFunctions, OutError))
		{
			return false;
		}

		for (const FString& IncludedFunction : IncludedFunctions)
		{
			UEdGraph* SourceGraph = FindGraphByName(Blueprint, IncludedFunction);
			if (!SourceGraph)
			{
				OutError = FString::Printf(TEXT("Failed to find source function graph '%s' in Blueprint '%s'"), *IncludedFunction, *OutResolvedBlueprintPath);
				return false;
			}

			if (!EnsureFunctionGraph(SplitBlueprint, Blueprint, IncludedFunction))
			{
				OutError = FString::Printf(TEXT("Failed to create function graph '%s' in split Blueprint asset: %s"), *IncludedFunction, *AssetPath);
				return false;
			}

			const FString RawText = BuildRawGraphTextForExport(SourceGraph);
			if (!ImportNodesFromText(SplitBlueprint, IncludedFunction, RawText, false))
			{
				OutError = FString::Printf(TEXT("Failed to import function graph '%s' into split Blueprint asset: %s"), *IncludedFunction, *AssetPath);
				return false;
			}
		}

		if (!CompileBlueprint(SplitBlueprint))
		{
			OutError = FString::Printf(TEXT("Failed to compile split Blueprint asset: %s"), *AssetPath);
			return false;
		}

		if (!UEditorAssetLibrary::SaveLoadedAsset(SplitBlueprint))
		{
			OutError = FString::Printf(TEXT("Failed to save split Blueprint asset: %s"), *AssetPath);
			return false;
		}

		TSharedPtr<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
		FunctionObject->SetStringField(TEXT("graph_name"), Graph->GetName());
		FunctionObject->SetStringField(TEXT("asset_name"), AssetName);
		FunctionObject->SetStringField(TEXT("asset_path"), AssetPath);
		FunctionObject->SetStringField(TEXT("asset_blueprint_type"), TEXT("BPTYPE_FunctionLibrary"));
		FunctionObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		TArray<TSharedPtr<FJsonValue>> IncludedFunctionValues;
		for (const FString& IncludedFunction : IncludedFunctions)
		{
			IncludedFunctionValues.Add(MakeShared<FJsonValueString>(IncludedFunction));
		}
		FunctionObject->SetArrayField(TEXT("included_functions"), IncludedFunctionValues);
		FunctionArray.Add(MakeShared<FJsonValueObject>(FunctionObject));
	}

	ManifestObject->SetArrayField(TEXT("functions"), FunctionArray);
	ManifestObject->SetNumberField(TEXT("function_count"), FunctionArray.Num());

	OutManifestPath = GetManifestFilePathForAssetDirectory(OutResolvedDirectory);
	if (!SaveJsonObjectToFile(ManifestObject, OutManifestPath, OutError))
	{
		return false;
	}

	OutExportedFunctionCount = FunctionArray.Num();
	return true;
}

bool UExportBlueprintToTxtLibrary::ImportBlueprintFunctionsFromDirectory(
	const FString& InputDirectory,
	const FString& BlueprintTarget,
	FString& OutResolvedBlueprintPath,
	int32& OutImportedFunctionCount,
	FString& OutError)
{
	OutResolvedBlueprintPath.Reset();
	OutImportedFunctionCount = 0;
	OutError.Reset();

	const FString ResolvedInputDirectory = NormalizeAssetDirectory(InputDirectory);
	if (ResolvedInputDirectory.IsEmpty())
	{
		OutError = TEXT("Input directory is empty");
		return false;
	}

	if (!ResolvedInputDirectory.StartsWith(TEXT("/Game")))
	{
		OutError = TEXT("Input directory must be a /Game asset folder");
		return false;
	}

	if (!UEditorAssetLibrary::DoesDirectoryExist(ResolvedInputDirectory))
	{
		OutError = FString::Printf(TEXT("Split Blueprint folder not found: %s"), *ResolvedInputDirectory);
		return false;
	}

	const FString ManifestPath = GetManifestFilePathForAssetDirectory(ResolvedInputDirectory);
	TSharedPtr<FJsonObject> ManifestObject;
	if (!LoadJsonObjectFromFile(ManifestPath, ManifestObject, OutError))
	{
		return false;
	}

	FString TargetBlueprintPath = BlueprintTarget;
	TargetBlueprintPath.TrimStartAndEndInline();
	if (TargetBlueprintPath.IsEmpty())
	{
		ManifestObject->TryGetStringField(TEXT("source_blueprint"), TargetBlueprintPath);
	}

	if (TargetBlueprintPath.IsEmpty())
	{
		OutError = TEXT("Blueprint target is empty and manifest does not provide source_blueprint");
		return false;
	}

	UBlueprint* Blueprint = ResolveBlueprintAsset(TargetBlueprintPath, OutResolvedBlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *TargetBlueprintPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* FunctionArray = nullptr;
	if (!ManifestObject->TryGetArrayField(TEXT("functions"), FunctionArray) || !FunctionArray)
	{
		OutError = FString::Printf(TEXT("Manifest missing 'functions' array: %s"), *ManifestPath);
		return false;
	}

	for (const TSharedPtr<FJsonValue>& FunctionValue : *FunctionArray)
	{
		const TSharedPtr<FJsonObject> FunctionObject = FunctionValue.IsValid() ? FunctionValue->AsObject() : nullptr;
		if (!FunctionObject.IsValid())
		{
			OutError = TEXT("Manifest contains invalid function entry");
			return false;
		}

		FString GraphName;
		FString AssetPath;
		if (!FunctionObject->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
		{
			OutError = TEXT("Manifest function entry missing 'graph_name'");
			return false;
		}

		if (!FunctionObject->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Manifest function entry missing 'asset_path' for graph: %s"), *GraphName);
			return false;
		}

		UBlueprint* SplitBlueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!SplitBlueprint)
		{
			OutError = FString::Printf(TEXT("Failed to load split Blueprint asset: %s"), *AssetPath);
			return false;
		}

		UEdGraph* SplitGraph = FindGraphByName(SplitBlueprint, GraphName);
		if (!SplitGraph)
		{
			OutError = FString::Printf(TEXT("Function graph '%s' not found in split Blueprint asset: %s"), *GraphName, *AssetPath);
			return false;
		}

		const FString RawText = BuildRawGraphTextForExport(SplitGraph);

		if (!EnsureFunctionGraph(Blueprint, SplitBlueprint, GraphName))
		{
			OutError = FString::Printf(TEXT("Failed to ensure function graph exists: %s"), *GraphName);
			return false;
		}

		if (!ImportNodesFromText(Blueprint, GraphName, RawText, true))
		{
			OutError = FString::Printf(TEXT("Failed to import function graph: %s"), *GraphName);
			return false;
		}

		++OutImportedFunctionCount;
	}

	if (!CompileBlueprint(Blueprint))
	{
		OutError = FString::Printf(TEXT("Failed to compile blueprint after import: %s"), *OutResolvedBlueprintPath);
		return false;
	}

	if (!UEditorAssetLibrary::SaveLoadedAsset(Blueprint))
	{
		OutError = FString::Printf(TEXT("Imported functions but failed to save blueprint: %s"), *OutResolvedBlueprintPath);
		return false;
	}

	return true;
}
