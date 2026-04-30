// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "BPDirectImporter.h"
#include "BPDirectExporter.h"

#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimationTransitionSchema.h"
#include "AnimationCustomTransitionGraph.h"
#include "AnimationCustomTransitionSchema.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "AnimGraphNode_CustomProperty.h"
#include "AnimGraphNode_LinkedAnimGraphBase.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_CustomTransitionResult.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateAliasNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphNode_Comment.h"
#include "Engine/MemberReference.h"
#include "K2Node.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_TransitionRuleGetter.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Composite.h"
#include "K2Node_Knot.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Message.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_Select.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_MakeContainer.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Self.h"
#include "K2Node_CreateDelegate.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "BlueprintCompilationManager.h"
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformMisc.h"
#include "Engine/EngineTypes.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "InputCoreTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Internationalization/Regex.h"
#include "Containers/Ticker.h"
#include "EditorAssetLibrary.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include <initializer_list>

namespace
{
enum class ETunnelKind_ImportBpy : uint8
{
	Unknown,
	Entry,
	Exit,
};

struct FStateMachineBindingSnapshot_ImportBpy
{
	FString NodeClassName;
	FString NodeGuid;
	FString GraphName;
	FString StateEnteredText;
	FString StateLeftText;
	FString StateFullyBlendedText;
	FName StateEntryFunctionName = NAME_None;
	FName UpdateFunctionName = NAME_None;
};

USCS_Node* FindComponentNodeByName_ImportBpy(UBlueprint* BP, const FString& ComponentName);
FString GetNodePropString_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson, const TCHAR* Key);
UClass* ResolveNodeClass_ImportBpy(const FString& NodeClassName);
bool RestoreVariableGetPurity_ImportBpy(UK2Node_VariableGet* Node, bool bIsPure);
bool ShouldRestoreImpureVariableGet_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson);
FBPVariableDescription* FindBlueprintVariableDescription_ImportBpy(UBlueprint* BP, const FName& VariableName);
bool CanSafelyOverwritePackageFile_ImportBpy(const FString& PackageFileName, FString& OutError);
bool SyncBlueprintVariableDescriptionFromJson_ImportBpy(
	FBPVariableDescription& Variable,
	const TSharedPtr<FJsonObject>& VarJson,
	const FEdGraphPinType& PinType,
	const FString& DefaultValue);
bool PrepareResolvedNodeForDefaultPins_ImportBpy(UEdGraphNode* Node, FString& OutError);
ETunnelKind_ImportBpy InferTunnelKind_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson);
FString StripGuidSuffix_ImportBpy(const FString& RawName);
bool RestoreCreateDelegateNodesAfterConnections_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	bool bStrict,
	FString& OutError);
bool RestoreStateMachineAliasNodesAfterCreation_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	FString& OutError);
bool ReplayStateMachineAliasNodesFromGraphJsonText_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& GraphJsonText,
	const TCHAR* StageTag,
	FString& OutError);
FString DescribePinType_ImportBpy(const FEdGraphPinType& PinType);
bool TryParseGuid_ImportBpy(const FString& GuidText, FGuid& OutGuid);
bool PopulateNestedGraphFromJsonText_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& GraphJsonText,
	FString& OutError);
void RemoveUnlinkedOrphanPins_ImportBpy(UEdGraphNode* Node);
bool EnsureTransitionCustomGraphExists_ImportBpy(UAnimStateTransitionNode* TransitionNode, FString& OutError);
bool EnsureTransitionBoundGraphOwnership_ImportBpy(UAnimStateTransitionNode* TransitionNode, FString& OutError);
bool EnsureConduitBoundGraphOwnership_ImportBpy(UAnimStateConduitNode* ConduitNode, FString& OutError);
bool IsNodeGuidAlreadyUsedInBlueprint_ImportBpy(UBlueprint* BP, const FGuid& Guid, const UEdGraphNode* IgnoreNode);
void ResetAnimationGraphResultNode_ImportBpy(UEdGraph* Graph);
void AssignAnimationGraphResultNode_ImportBpy(UEdGraph* Graph, UEdGraphNode* Node);
void ResetAllImportedNodeRegistries_ImportBpy();
void RegisterImportedNodeUid_ImportBpy(UBlueprint* BP, const FString& SerializedUid, UEdGraphNode* Node);
UEdGraphNode* FindImportedNodeBySerializedUid_ImportBpy(UBlueprint* BP, const FString& SerializedUid);
UEdGraphNode* FindImportedAnimNodeFromSerializedJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString* OutResolutionMode = nullptr);
UClass* ResolveNativeAnimBlueprintParentClass_ImportBpy(const UBlueprint* BP);
FString GetSpecialNodePropString_ImportBpy(const TSharedPtr<FJsonObject>& NodeObj, const TCHAR* Key);
bool RestoreAnimReferenceNodesAfterCreation_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	FString& OutError);
bool HasEvaluateChooserMetadata_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson, FString& OutMissingProperties);
bool HasEvaluateChooserAssetBound_ImportBpy(const UEdGraphNode* Node);
bool IsEvaluateChooserNode_ImportBpy(const UEdGraphNode* Node);

UEdGraphPin* FindEvaluateChooserContextPinForCurrentImport_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction);
void RepairEvaluateChooserSourceClassPinsForCurrentImport_ImportBpy(UBlueprint* BP);
bool RetargetEvaluateChooserTablesForCurrentBlueprint_ImportBpy(
	UBlueprint* BP,
	bool& bOutAnyRetargeted,
	FString& OutError);
bool ApplyPinIds_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson, FString& OutError);
UEdGraphPin* ResolvePinForSerializedDefault_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	const FString& SerializedPinName);
FString ReadPinRawDefaultValue_ImportBpy(const UEdGraphPin* Pin);
bool AreSerializedDefaultValuesEquivalent_ImportBpy(const FString& ExpectedValue, const FString& ActualValue);
bool ReplayAndValidateSerializedNodeDefaults_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const FString& GraphName,
	FString& OutError);
void AppendDefaultValidationIssue_ImportBpy(
	TArray<TSharedPtr<FJsonValue>>& OutIssues,
	const FString& GraphName,
	const FString& NodeClass,
	const FString& NodeGuid,
	const FString& NodeLabel,
	const FString& PinName,
	const FString& ExpectedValue,
	const FString& ActualValue,
	const FString& IssueType);
bool ValidateBlueprintDefaultsAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	TArray<TSharedPtr<FJsonValue>>& OutMissingGraphs,
	TArray<TSharedPtr<FJsonValue>>& OutMissingDefaultKeys,
	TArray<TSharedPtr<FJsonValue>>& OutDefaultMismatches,
	FString& OutError);
bool ValidateImportedInterfaceBindingsAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError);
bool ValidateAnimBlueprintPoseHistoryContractAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError);
bool ValidateAnimBlueprintStateMachineBindingContractAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError);

FGuid ResolveBlueprintFunctionGuid_ImportBpy(UBlueprint* BP, const FName& FunctionName)
{
	FGuid FunctionGuid;
	if (!BP || FunctionName.IsNone())
	{
		return FunctionGuid;
	}

	if (const UClass* const SkeletonClass = BP->SkeletonGeneratedClass)
	{
		FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
			SkeletonClass,
			FunctionName,
			FunctionGuid);
		if (FunctionGuid.IsValid())
		{
			return FunctionGuid;
		}
	}

	if (const UClass* const GeneratedClass = BP->GeneratedClass)
	{
		FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
			GeneratedClass,
			FunctionName,
			FunctionGuid);
		if (FunctionGuid.IsValid())
		{
			return FunctionGuid;
		}
	}

	for (UEdGraph* FunctionGraph : BP->FunctionGraphs)
	{
		if (FunctionGraph && FunctionGraph->GetFName() == FunctionName)
		{
			return FunctionGraph->GraphGuid;
		}
	}

	return FunctionGuid;
}

bool RepairAnimBlueprintStateMachineEntryBindings_ImportBpy(
	UBlueprint* BP,
	int32& OutRepairedCount,
	FString& OutError,
	const TSharedPtr<FJsonObject>* RootJsonForBpyData = nullptr);
void LogAnimBlueprintStateMachineEntryBindings_ImportBpy(
	UBlueprint* BP,
	const TCHAR* StageName);
bool ValidateAnimBlueprintStateMachineEntryBindingPresence_ImportBpy(
	UBlueprint* BP,
	const TCHAR* StageName,
	FString& OutError);
bool ValidateAnimBlueprintLinkedAnimLayerContractAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError);
bool ValidateRoundtripAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TArray<FString>& CompileWarnings,
	const TCHAR* StageName,
	FString& OutError);
bool IsAnimNodeFunctionRefFieldName_ImportBpy(const FString& PropertyName);
bool RebindAnimNodeFunctionReferencesFromSerializedGraphs_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	bool& bOutAnyChanges,
	FString& OutError);
void CollectStateMachineBindingContractMismatches_ImportBpy(
	const TSharedPtr<FJsonObject>& SourceRoot,
	const TSharedPtr<FJsonObject>& LiveRoot,
	TArray<FString>& OutMismatches);
void CollectSerializedStateMachineBindingSnapshotsFromRootJson_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	TMap<FString, FStateMachineBindingSnapshot_ImportBpy>& OutSnapshotsByNodeGuid);
void CollectLinkedAnimLayerContractMismatches_ImportBpy(
	const TSharedPtr<FJsonObject>& SourceRoot,
	const TSharedPtr<FJsonObject>& LiveRoot,
	TArray<FString>& OutMismatches);
FName ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
	const FString& NodeStructText,
	const TCHAR* HookFieldName);
void CollectSerializedAnimNodeJsonByUidRecursive_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	TSet<FString>& VisitedGraphGuids,
	TMap<FString, TSharedPtr<FJsonObject>>& OutNodeByUid);
bool VerifyExportRoundtripAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& SourceRoot,
	FString& OutError);
void EnsureSerializedTunnelPinContracts_ImportBpy(
	UK2Node_Tunnel* TunnelNode,
	const TSharedPtr<FJsonObject>& NodeJson);
bool ReloadBlueprintAssetForPostSaveValidation_ImportBpy(
	const FString& TargetAssetPath,
	UBlueprint*& OutBlueprint,
	FString& OutError);
bool RunPostSaveReloadValidation_ImportBpy(
	const FString& TargetAssetPath,
	const TSharedPtr<FJsonObject>& Root,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const TArray<FString>& CompileWarnings,
	bool bStrictImportMode,
	UBlueprint*& InOutBlueprint,
	FString& OutError);

bool ReplayAndValidateBlueprintDefaultsContract_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* Stage,
	FString& OutError);
bool ValidateImportedAnimBlueprintAgainstSourceAsset_ImportBpy(
	UBlueprint* ImportedBP,
	const FString& SourceBlueprintPath,
	const TCHAR* Stage,
	FString& OutError);
bool ValidateIncomingExecTopologyAgainstSourceBeforeImport_ImportBpy(
	const TSharedPtr<FJsonObject>& IncomingRoot,
	const FString& SourceBlueprintPath,
	const TCHAR* Stage,
	FString& OutError);
bool ValidateFunctionExecTopologyParityAgainstSource_ImportBpy(
	UBlueprint* SourceBP,
	UBlueprint* ImportedBP,
	const FName& FunctionName,
	const TCHAR* Stage,
	FString& OutError);
void LogImportedAnimBlueprintReachableGraphInventory_ImportBpy(
	UBlueprint* BP,
	const TCHAR* StageTag);
void LogSerializedAnimNodeUidResolutionAudit_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageTag);
void ScheduleDeferredPostImportDiagnostics_ImportBpy(
	const FString& TargetAssetPath,
	const TSharedPtr<FJsonObject>& Root,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const TArray<FString>& CompileWarnings);

template <typename TObject>
TObject* ResolveNamedObject_ImportBpy(const FString& Name)
{
	FString NormalizedName = Name;
	NormalizedName.TrimStartAndEndInline();
	if (NormalizedName.IsEmpty() ||
		NormalizedName.Equals(TEXT("None"), ESearchCase::IgnoreCase) ||
		NormalizedName.Equals(TEXT("Null"), ESearchCase::IgnoreCase) ||
		NormalizedName.Equals(TEXT("nullptr"), ESearchCase::IgnoreCase) ||
		NormalizedName.Equals(TEXT("Object None.None"), ESearchCase::IgnoreCase))
	{
		return nullptr;
	}

	if (TObject* Found = FindObject<TObject>(nullptr, *NormalizedName))
	{
		return Found;
	}
	if (TObject* Found = FindFirstObjectSafe<TObject>(*NormalizedName))
	{
		return Found;
	}

	auto TryLoad = [](const FString& Candidate) -> TObject*
	{
		if (Candidate.IsEmpty())
		{
			return nullptr;
		}

		if (TObject* Loaded = Cast<TObject>(UEditorAssetLibrary::LoadAsset(Candidate)))
		{
			return Loaded;
		}

		return Cast<TObject>(StaticLoadObject(TObject::StaticClass(), nullptr, *Candidate));
	};

	if (TObject* Loaded = TryLoad(NormalizedName))
	{
		return Loaded;
	}

	FString PackagePath = NormalizedName;
	FString ObjectPath = NormalizedName;
	if (const int32 DotIndex = NormalizedName.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd); DotIndex != INDEX_NONE)
	{
		PackagePath = NormalizedName.Left(DotIndex);
	}
	else if (NormalizedName.StartsWith(TEXT("/")))
	{
		const FString AssetName = FPaths::GetBaseFilename(NormalizedName);
		ObjectPath = FString::Printf(TEXT("%s.%s"), *NormalizedName, *AssetName);
	}

	if (PackagePath != NormalizedName)
	{
		if (TObject* Loaded = TryLoad(PackagePath))
		{
			return Loaded;
		}
	}

	if (ObjectPath != NormalizedName)
	{
		if (TObject* Loaded = TryLoad(ObjectPath))
		{
			return Loaded;
		}
	}

	if (!NormalizedName.Contains(TEXT("/")) && !NormalizedName.Contains(TEXT(".")))
	{
		const bool bSupportsAssetRegistryNameLookup =
			TObject::StaticClass()->IsChildOf(UEnum::StaticClass()) ||
			TObject::StaticClass()->IsChildOf(UScriptStruct::StaticClass());

		if (bSupportsAssetRegistryNameLookup)
		{
			FAssetRegistryModule& AssetRegistryModule =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			TArray<FAssetData> CandidateAssets;
			AssetRegistryModule.Get().GetAssetsByClass(
				TObject::StaticClass()->GetClassPathName(),
				CandidateAssets,
				true);

			const FName AssetName(*NormalizedName);
			for (const FAssetData& AssetData : CandidateAssets)
			{
				if (!AssetData.IsValid() || AssetData.AssetName != AssetName)
				{
					continue;
				}

				if (TObject* Loaded = Cast<TObject>(AssetData.GetAsset()))
				{
					return Loaded;
				}
			}
		}
	}

	return nullptr;
}

UClass* ResolveInterfaceClassFromPath_ImportBpy(const FString& InterfaceClassPath)
{
	FString Normalized = InterfaceClassPath;
	Normalized.TrimStartAndEndInline();
	if (Normalized.IsEmpty() || Normalized.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return nullptr;
	}

	if (UClass* DirectClass = ResolveNamedObject_ImportBpy<UClass>(Normalized))
	{
		return DirectClass->GetAuthoritativeClass();
	}

	const FString PackagePath = FPackageName::ObjectPathToPackageName(Normalized);
	const FString ObjectName = FPackageName::ObjectPathToObjectName(Normalized);
	if (!PackagePath.IsEmpty())
	{
		TArray<FString> CandidateBlueprintObjectPaths;
		CandidateBlueprintObjectPaths.Add(FString::Printf(TEXT("%s.%s"), *PackagePath, *FPaths::GetBaseFilename(PackagePath)));
		if (!ObjectName.IsEmpty())
		{
			if (ObjectName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
			{
				const FString BaseObjectName = ObjectName.LeftChop(2);
				CandidateBlueprintObjectPaths.AddUnique(FString::Printf(TEXT("%s.%s"), *PackagePath, *BaseObjectName));
			}
			CandidateBlueprintObjectPaths.AddUnique(FString::Printf(TEXT("%s.%s"), *PackagePath, *ObjectName));
		}
		CandidateBlueprintObjectPaths.AddUnique(PackagePath);

		for (const FString& CandidatePath : CandidateBlueprintObjectPaths)
		{
			if (UBlueprint* BlueprintAsset = ResolveNamedObject_ImportBpy<UBlueprint>(CandidatePath))
			{
				if (BlueprintAsset->GeneratedClass)
				{
					return BlueprintAsset->GeneratedClass->GetAuthoritativeClass();
				}
			}
		}
	}

	return nullptr;
}

TMap<TWeakObjectPtr<UBlueprint>, TMap<FString, TWeakObjectPtr<UEdGraphNode>>> GImportedNodeUidRegistry_ImportBpy;

enum class ESerializedAnimNodeResolutionMode_ImportBpy : uint8
{
	None,
	NodeGuidScan,
	UidRegistry,
	UidGuidScan,
};

const TCHAR* LexToString_ImportBpy(const ESerializedAnimNodeResolutionMode_ImportBpy Mode)
{
	switch (Mode)
	{
	case ESerializedAnimNodeResolutionMode_ImportBpy::NodeGuidScan:
		return TEXT("node_guid_scan");
	case ESerializedAnimNodeResolutionMode_ImportBpy::UidRegistry:
		return TEXT("uid_registry");
	case ESerializedAnimNodeResolutionMode_ImportBpy::UidGuidScan:
		return TEXT("uid_guid_scan");
	default:
		return TEXT("none");
	}
}

FString GetSerializedAnimNodeUid_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return FString();
	}

	FString SerializedUid;
	NodeJson->TryGetStringField(TEXT("uid"), SerializedUid);
	SerializedUid.TrimStartAndEndInline();
	return SerializedUid;
}

FString GetSerializedAnimNodeGuid_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return FString();
	}

	FString SerializedNodeGuid;
	NodeJson->TryGetStringField(TEXT("node_guid"), SerializedNodeGuid);
	SerializedNodeGuid.TrimStartAndEndInline();
	return SerializedNodeGuid;
}

bool DoSerializedAnimNodeUidAndGuidMatch_ImportBpy(
	const FString& SerializedUid,
	const FString& SerializedNodeGuid)
{
	if (SerializedUid.IsEmpty() || SerializedNodeGuid.IsEmpty())
	{
		return false;
	}

	FGuid ParsedUidGuid;
	FGuid ParsedNodeGuid;
	if (TryParseGuid_ImportBpy(SerializedUid, ParsedUidGuid) &&
		TryParseGuid_ImportBpy(SerializedNodeGuid, ParsedNodeGuid))
	{
		return ParsedUidGuid == ParsedNodeGuid;
	}

	return SerializedUid.Equals(SerializedNodeGuid, ESearchCase::IgnoreCase);
}

void ResetAllImportedNodeRegistries_ImportBpy()
{
	GImportedNodeUidRegistry_ImportBpy.Reset();
}

void RegisterImportedNodeUid_ImportBpy(UBlueprint* BP, const FString& SerializedUid, UEdGraphNode* Node)
{
	if (!BP || !Node || SerializedUid.IsEmpty())
	{
		return;
	}

	TMap<FString, TWeakObjectPtr<UEdGraphNode>>& BlueprintNodes =
		GImportedNodeUidRegistry_ImportBpy.FindOrAdd(BP);
	BlueprintNodes.Add(SerializedUid, Node);
}

void GatherReachableGraphs_ImportBpy(UEdGraph* Graph, TSet<UEdGraph*>& VisitedGraphs, TArray<UEdGraph*>& OutGraphs)
{
	if (!Graph || VisitedGraphs.Contains(Graph))
	{
		return;
	}

	VisitedGraphs.Add(Graph);
	OutGraphs.Add(Graph);

	for (UEdGraph* SubGraph : Graph->SubGraphs)
	{
		GatherReachableGraphs_ImportBpy(SubGraph, VisitedGraphs, OutGraphs);
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
		{
			GatherReachableGraphs_ImportBpy(CompositeNode->BoundGraph, VisitedGraphs, OutGraphs);
		}
		if (UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
		{
			GatherReachableGraphs_ImportBpy(StateMachineNode->EditorStateMachineGraph, VisitedGraphs, OutGraphs);
		}
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			GatherReachableGraphs_ImportBpy(StateNode->BoundGraph, VisitedGraphs, OutGraphs);
		}
		if (UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
		{
			GatherReachableGraphs_ImportBpy(ConduitNode->BoundGraph, VisitedGraphs, OutGraphs);
		}
		if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			GatherReachableGraphs_ImportBpy(TransitionNode->BoundGraph, VisitedGraphs, OutGraphs);
			GatherReachableGraphs_ImportBpy(TransitionNode->CustomTransitionGraph, VisitedGraphs, OutGraphs);
		}
	}
}

UEdGraphNode* FindImportedNodeByGuidScan_ImportBpy(UBlueprint* BP, const FGuid& SerializedGuid)
{
	if (!BP || !SerializedGuid.IsValid())
	{
		return nullptr;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == SerializedGuid)
			{
				return Node;
			}
		}
	}

	return nullptr;
}

UEdGraphNode* FindImportedNodeBySerializedUid_ImportBpy(UBlueprint* BP, const FString& SerializedUid)
{
	if (!BP || SerializedUid.IsEmpty())
	{
		return nullptr;
	}

	if (const TMap<FString, TWeakObjectPtr<UEdGraphNode>>* BlueprintNodes =
			GImportedNodeUidRegistry_ImportBpy.Find(BP))
	{
		if (const TWeakObjectPtr<UEdGraphNode>* ExistingNode = BlueprintNodes->Find(SerializedUid))
		{
			return ExistingNode->Get();
		}
	}

	FGuid ParsedGuid;
	if (TryParseGuid_ImportBpy(SerializedUid, ParsedGuid))
	{
		if (UEdGraphNode* ScannedNode = FindImportedNodeByGuidScan_ImportBpy(BP, ParsedGuid))
		{
			RegisterImportedNodeUid_ImportBpy(BP, SerializedUid, ScannedNode);
			return ScannedNode;
		}
	}

	return nullptr;
}

UEdGraphNode* FindImportedAnimNodeFromSerializedJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString* OutResolutionMode)
{
	if (OutResolutionMode)
	{
		OutResolutionMode->Reset();
	}

	if (!BP || !NodeJson.IsValid())
	{
		return nullptr;
	}

	const FString SerializedUid = GetSerializedAnimNodeUid_ImportBpy(NodeJson);
	const FString SerializedNodeGuid = GetSerializedAnimNodeGuid_ImportBpy(NodeJson);

	if (!SerializedNodeGuid.IsEmpty())
	{
		FGuid ParsedNodeGuid;
		if (TryParseGuid_ImportBpy(SerializedNodeGuid, ParsedNodeGuid))
		{
			if (UEdGraphNode* GuidMatchedNode = FindImportedNodeByGuidScan_ImportBpy(BP, ParsedNodeGuid))
			{
				if (!SerializedUid.IsEmpty())
				{
					RegisterImportedNodeUid_ImportBpy(BP, SerializedUid, GuidMatchedNode);
				}
				if (OutResolutionMode)
				{
					*OutResolutionMode = LexToString_ImportBpy(
						ESerializedAnimNodeResolutionMode_ImportBpy::NodeGuidScan);
				}
				return GuidMatchedNode;
			}
		}
	}

	if (!SerializedUid.IsEmpty())
	{
		if (UEdGraphNode* UidRegisteredNode = FindImportedNodeBySerializedUid_ImportBpy(BP, SerializedUid))
		{
			if (OutResolutionMode)
			{
				*OutResolutionMode = LexToString_ImportBpy(
					ESerializedAnimNodeResolutionMode_ImportBpy::UidRegistry);
			}
			return UidRegisteredNode;
		}

		FGuid ParsedUidGuid;
		if (TryParseGuid_ImportBpy(SerializedUid, ParsedUidGuid))
		{
			if (UEdGraphNode* UidGuidMatchedNode = FindImportedNodeByGuidScan_ImportBpy(BP, ParsedUidGuid))
			{
				RegisterImportedNodeUid_ImportBpy(BP, SerializedUid, UidGuidMatchedNode);
				if (OutResolutionMode)
				{
					*OutResolutionMode = LexToString_ImportBpy(
						ESerializedAnimNodeResolutionMode_ImportBpy::UidGuidScan);
				}
				return UidGuidMatchedNode;
			}
		}
	}

	return nullptr;
}

UClass* ResolveNativeAnimBlueprintParentClass_ImportBpy(const UBlueprint* BP)
{
	UClass* NativeClass = BP ? BP->ParentClass : nullptr;
	while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native))
	{
		NativeClass = NativeClass->GetSuperClass();
	}

	return NativeClass;
}

FString GetSpecialNodePropString_ImportBpy(const TSharedPtr<FJsonObject>& NodeObj, const TCHAR* Key)
{
	if (!NodeObj.IsValid() || !Key)
	{
		return FString();
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
	{
		return FString();
	}

	FString Value;
	(*NodePropsObj)->TryGetStringField(Key, Value);
	return Value;
}

template <typename TNodeClass>
TNodeClass* ResolveImportedNodeRefByUid_ImportBpy(UBlueprint* BP, const FString& SerializedUid)
{
	return Cast<TNodeClass>(FindImportedNodeBySerializedUid_ImportBpy(BP, SerializedUid));
}

bool DoesNodeMatchSerializedUid_ImportBpy(const UEdGraphNode* Node, const FString& SerializedUid)
{
	if (!Node)
	{
		return false;
	}

	if (SerializedUid.IsEmpty())
	{
		return true;
	}

	FGuid ParsedGuid;
	return TryParseGuid_ImportBpy(SerializedUid, ParsedGuid) && Node->NodeGuid == ParsedGuid;
}

UAnimGraphNode_StateMachineBase* ResolveOwningStateMachineNodeFromImportedContext_ImportBpy(
	const UEdGraphNode* Node,
	const FString& SerializedUid)
{
	if (!Node)
	{
		return nullptr;
	}

	UObject* Context = Node->GetOuter();
	while (Context)
	{
		if (const UAnimationStateMachineGraph* StateMachineGraph = Cast<UAnimationStateMachineGraph>(Context))
		{
			if (UAnimGraphNode_StateMachineBase* MachineNode =
					Cast<UAnimGraphNode_StateMachineBase>(StateMachineGraph->GetOuter()))
			{
				if (DoesNodeMatchSerializedUid_ImportBpy(MachineNode, SerializedUid))
				{
					return MachineNode;
				}
			}
		}

		Context = Context->GetOuter();
	}

	return nullptr;
}

UAnimStateNodeBase* ResolveOwningStateNodeFromImportedContext_ImportBpy(
	const UEdGraphNode* Node,
	const FString& SerializedUid)
{
	if (!Node)
	{
		return nullptr;
	}

	if (const UEdGraph* Graph = Node->GetGraph())
	{
		if (UAnimStateNodeBase* DirectStateOwner = Cast<UAnimStateNodeBase>(Graph->GetOuter()))
		{
			if (DoesNodeMatchSerializedUid_ImportBpy(DirectStateOwner, SerializedUid))
			{
				return DirectStateOwner;
			}
		}

		if (UAnimStateTransitionNode* TransitionOwner = Cast<UAnimStateTransitionNode>(Graph->GetOuter()))
		{
			if (UAnimStateNodeBase* PrevState = TransitionOwner->GetPreviousState())
			{
				if (DoesNodeMatchSerializedUid_ImportBpy(PrevState, SerializedUid))
				{
					return PrevState;
				}
			}

			if (UAnimStateNodeBase* NextState = TransitionOwner->GetNextState())
			{
				if (DoesNodeMatchSerializedUid_ImportBpy(NextState, SerializedUid))
				{
					return NextState;
				}
			}
		}
	}

	UObject* Context = Node->GetOuter();
	while (Context)
	{
		if (UAnimStateNodeBase* StateOwner = Cast<UAnimStateNodeBase>(Context))
		{
			if (DoesNodeMatchSerializedUid_ImportBpy(StateOwner, SerializedUid))
			{
				return StateOwner;
			}
		}

		Context = Context->GetOuter();
	}

	return nullptr;
}

void PopulateAnimGetterContextsFromFunction_ImportBpy(UK2Node_AnimGetter* AnimGetterNode)
{
	if (!AnimGetterNode)
	{
		return;
	}

	AnimGetterNode->Contexts.Reset();
	if (const UFunction* TargetFunction = AnimGetterNode->GetTargetFunction())
	{
		const FString GetterContext = TargetFunction->GetMetaData(TEXT("GetterContext"));
		if (!GetterContext.IsEmpty())
		{
			GetterContext.ParseIntoArray(AnimGetterNode->Contexts, TEXT("|"), true);
		}
	}
}

void DeriveAnimGetterMachineNodeFromState_ImportBpy(UK2Node_AnimGetter* AnimGetterNode)
{
	if (!AnimGetterNode || AnimGetterNode->SourceNode || !AnimGetterNode->SourceStateNode)
	{
		return;
	}

	if (UAnimationStateMachineGraph* StateMachineGraph =
			Cast<UAnimationStateMachineGraph>(AnimGetterNode->SourceStateNode->GetOuter()))
	{
		if (UAnimGraphNode_StateMachine* MachineNode = Cast<UAnimGraphNode_StateMachine>(StateMachineGraph->GetOuter()))
		{
			AnimGetterNode->SourceNode = MachineNode;
		}
	}
}

bool RestoreAnimGetterBindings_ImportBpy(
	UBlueprint* BP,
	UK2Node_AnimGetter* AnimGetterNode,
	const TSharedPtr<FJsonObject>& NodeObj,
	FString& OutError)
{
	if (!BP || !AnimGetterNode || !NodeObj.IsValid())
	{
		return true;
	}

	const FString SourceNodeUid = GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("AnimGetterSourceNodeUid"));
	const FString SourceStateNodeUid =
		GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("AnimGetterSourceStateNodeUid"));
	if (!SourceStateNodeUid.IsEmpty())
	{
		AnimGetterNode->SourceStateNode =
			ResolveImportedNodeRefByUid_ImportBpy<UAnimStateNodeBase>(BP, SourceStateNodeUid);
		if (!AnimGetterNode->SourceStateNode)
		{
			AnimGetterNode->SourceStateNode =
				ResolveOwningStateNodeFromImportedContext_ImportBpy(AnimGetterNode, SourceStateNodeUid);
		}
		if (!AnimGetterNode->SourceStateNode)
		{
			OutError = FString::Printf(
				TEXT("Cannot resolve AnimGetter source state uid '%s' for %s"),
				*SourceStateNodeUid,
				*AnimGetterNode->GetPathName());
			return false;
		}
	}

	if (!SourceNodeUid.IsEmpty())
	{
		AnimGetterNode->SourceNode = ResolveImportedNodeRefByUid_ImportBpy<UAnimGraphNode_Base>(BP, SourceNodeUid);
		if (!AnimGetterNode->SourceNode)
		{
			AnimGetterNode->SourceNode =
				ResolveOwningStateMachineNodeFromImportedContext_ImportBpy(AnimGetterNode, SourceNodeUid);
		}
	}

	if (const FString GetterClassPath = GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("AnimGetterClass"));
		!GetterClassPath.IsEmpty())
	{
		AnimGetterNode->GetterClass = ResolveNamedObject_ImportBpy<UClass>(GetterClassPath);
	}
	if (!AnimGetterNode->GetterClass)
	{
		AnimGetterNode->GetterClass = ResolveNativeAnimBlueprintParentClass_ImportBpy(BP);
	}

	AnimGetterNode->SourceAnimBlueprint = Cast<UAnimBlueprint>(BP);
	DeriveAnimGetterMachineNodeFromState_ImportBpy(AnimGetterNode);
	PopulateAnimGetterContextsFromFunction_ImportBpy(AnimGetterNode);

	if (!AnimGetterNode->SourceNode && !SourceNodeUid.IsEmpty())
	{
		AnimGetterNode->SourceNode = ResolveImportedNodeRefByUid_ImportBpy<UAnimGraphNode_Base>(BP, SourceNodeUid);
		if (!AnimGetterNode->SourceNode)
		{
			AnimGetterNode->SourceNode =
				ResolveOwningStateMachineNodeFromImportedContext_ImportBpy(AnimGetterNode, SourceNodeUid);
		}
		if (!AnimGetterNode->SourceNode)
		{
			AnimGetterNode->SourceNode =
				ResolveOwningStateMachineNodeFromImportedContext_ImportBpy(AnimGetterNode, FString());
		}
	}
	if (!AnimGetterNode->SourceNode && !SourceNodeUid.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("Cannot resolve AnimGetter source node uid '%s' for %s"),
			*SourceNodeUid,
			*AnimGetterNode->GetPathName());
		return false;
	}

	const FString CachedTitle = GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("AnimGetterTitle"));
	if (!CachedTitle.IsEmpty())
	{
		AnimGetterNode->CachedTitle = FText::FromString(CachedTitle);
	}
	else if (const UFunction* TargetFunction = AnimGetterNode->GetTargetFunction())
	{
		AnimGetterNode->CachedTitle = TargetFunction->GetDisplayNameText();
	}

	if (AnimGetterNode->HasValidBlueprint())
	{
		AnimGetterNode->ReconstructNode();
	}
	return true;
}

bool RestoreTransitionRuleGetterBindings_ImportBpy(
	UBlueprint* BP,
	UK2Node_TransitionRuleGetter* GetterNode,
	const TSharedPtr<FJsonObject>& NodeObj,
	FString& OutError)
{
	if (!BP || !GetterNode || !NodeObj.IsValid())
	{
		return true;
	}

	const FString GetterTypeText = GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("TransitionGetterType"));
	if (!GetterTypeText.IsEmpty())
	{
		int32 GetterTypeValue = 0;
		if (!LexTryParseString(GetterTypeValue, *GetterTypeText))
		{
			OutError = FString::Printf(
				TEXT("Cannot parse TransitionRuleGetter type '%s' for %s"),
				*GetterTypeText,
				*GetterNode->GetPathName());
			return false;
		}

		GetterNode->GetterType = static_cast<ETransitionGetter::Type>(GetterTypeValue);
	}

	const FString AssociatedStateUid =
		GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("TransitionAssociatedStateNodeUid"));
	if (!AssociatedStateUid.IsEmpty())
	{
		GetterNode->AssociatedStateNode =
			ResolveImportedNodeRefByUid_ImportBpy<UAnimStateNode>(BP, AssociatedStateUid);
		if (!GetterNode->AssociatedStateNode)
		{
			OutError = FString::Printf(
				TEXT("Cannot resolve TransitionRuleGetter state uid '%s' for %s"),
				*AssociatedStateUid,
				*GetterNode->GetPathName());
			return false;
		}
	}

	const FString AssociatedAnimNodeUid =
		GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("TransitionAssociatedAnimAssetPlayerNodeUid"));
	if (!AssociatedAnimNodeUid.IsEmpty())
	{
		GetterNode->AssociatedAnimAssetPlayerNode =
			ResolveImportedNodeRefByUid_ImportBpy<UAnimGraphNode_Base>(BP, AssociatedAnimNodeUid);
		if (!GetterNode->AssociatedAnimAssetPlayerNode)
		{
			OutError = FString::Printf(
				TEXT("Cannot resolve TransitionRuleGetter anim node uid '%s' for %s"),
				*AssociatedAnimNodeUid,
				*GetterNode->GetPathName());
			return false;
		}
	}

	GetterNode->ReconstructNode();
	return true;
}

bool RestoreAnimReferenceNodesAfterCreation_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	FString& OutError)
{
	if (!BP || !NodesArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		const FString Uid = NodeObj->GetStringField(TEXT("uid"));
		UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		if (UK2Node_AnimGetter* AnimGetterNode = Cast<UK2Node_AnimGetter>(*ExistingNode))
		{
			if (!RestoreAnimGetterBindings_ImportBpy(BP, AnimGetterNode, NodeObj, OutError))
			{
				return false;
			}
		}
		else if (UK2Node_TransitionRuleGetter* TransitionGetterNode =
					Cast<UK2Node_TransitionRuleGetter>(*ExistingNode))
		{
			if (!RestoreTransitionRuleGetterBindings_ImportBpy(BP, TransitionGetterNode, NodeObj, OutError))
			{
				return false;
			}
		}
	}

	return true;
}

USkeleton* ResolveAnimBlueprintTargetSkeletonFromAssetRegistry_ImportBpy(const FString& BlueprintPath)
{
	if (BlueprintPath.IsEmpty())
	{
		return nullptr;
	}

	FString ObjectPath = BlueprintPath;
	if (BlueprintPath.StartsWith(TEXT("/")) && !BlueprintPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(BlueprintPath);
		if (!AssetName.IsEmpty())
		{
			ObjectPath = FString::Printf(TEXT("%s.%s"), *BlueprintPath, *AssetName);
		}
	}

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData AssetData =
		AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (!AssetData.IsValid())
	{
		return nullptr;
	}

	if (const FAssetDataTagMapSharedView::FFindTagResult TargetSkeletonTag =
			AssetData.TagsAndValues.FindTag(TEXT("TargetSkeleton"));
		TargetSkeletonTag.IsSet())
	{
		return ResolveNamedObject_ImportBpy<USkeleton>(FString(TargetSkeletonTag.GetValue()));
	}

	TArray<FName> Dependencies;
	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	AssetRegistryModule.Get().GetDependencies(
		FName(*PackageName),
		Dependencies,
		UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::EDependencyQuery::Hard);

	for (const FName& Dependency : Dependencies)
	{
		if (USkeleton* Skeleton = ResolveNamedObject_ImportBpy<USkeleton>(Dependency.ToString()))
		{
			return Skeleton;
		}
	}

	return nullptr;
}

bool RestoreVariableGetPurity_ImportBpy(UK2Node_VariableGet* Node, bool bIsPure)
{
	if (!Node)
	{
		return false;
	}

	EGetNodeVariation DesiredVariation = EGetNodeVariation::Pure;
	if (!bIsPure)
	{
		if (const UEdGraphPin* ValuePin = Node->GetValuePin())
		{
			const FEdGraphPinType& PinType = ValuePin->PinType;
			if (!PinType.IsContainer())
			{
				if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
				{
					DesiredVariation = EGetNodeVariation::Branch;
				}
				else if (
					PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
					PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
					PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
					PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
				{
					DesiredVariation = EGetNodeVariation::ValidatedObject;
				}
			}
		}
	}

	FProperty* VariationProperty = FindFProperty<FProperty>(UK2Node_VariableGet::StaticClass(), TEXT("CurrentVariation"));
	if (!VariationProperty)
	{
		return false;
	}

	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(VariationProperty))
	{
		if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
		{
			UnderlyingProperty->SetIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Node), static_cast<uint64>(DesiredVariation));
		}
		else
		{
			return false;
		}
	}
	else if (FByteProperty* ByteProperty = CastField<FByteProperty>(VariationProperty))
	{
		ByteProperty->SetPropertyValue_InContainer(Node, static_cast<uint8>(DesiredVariation));
	}
	else
	{
		return false;
	}

	Node->Modify();
	Node->ReconstructNode();
	return true;
}

FBPVariableDescription* FindBlueprintVariableDescription_ImportBpy(UBlueprint* BP, const FName& VariableName)
{
	if (!BP)
	{
		return nullptr;
	}

	for (FBPVariableDescription& Variable : BP->NewVariables)
	{
		if (Variable.VarName == VariableName)
		{
			return &Variable;
		}
	}

	return nullptr;
}

bool SyncBlueprintVariableDescriptionFromJson_ImportBpy(
	FBPVariableDescription& Variable,
	const TSharedPtr<FJsonObject>& VarJson,
	const FEdGraphPinType& PinType,
	const FString& DefaultValue)
{
	bool bChanged = false;

	if (Variable.VarType != PinType)
	{
		Variable.VarType = PinType;
		bChanged = true;
	}

	if (Variable.DefaultValue != DefaultValue)
	{
		Variable.DefaultValue = DefaultValue;
		bChanged = true;
	}

	FString VariableGuidText;
	if (VarJson->TryGetStringField(TEXT("guid"), VariableGuidText) && !VariableGuidText.IsEmpty())
	{
		FGuid ParsedGuid;
		if (TryParseGuid_ImportBpy(VariableGuidText, ParsedGuid) && Variable.VarGuid != ParsedGuid)
		{
			Variable.VarGuid = ParsedGuid;
			bChanged = true;
		}
	}

	FString Category;
	const bool bHasCategory = VarJson->TryGetStringField(TEXT("category"), Category);
	bool bCategoryExplicit = false;
	VarJson->TryGetBoolField(TEXT("category_explicit"), bCategoryExplicit);
	if (bHasCategory && (bCategoryExplicit || !Category.IsEmpty()))
	{
		const FText CategoryText = FText::FromString(Category);
		if (!Variable.Category.EqualTo(CategoryText))
		{
			Variable.Category = CategoryText;
			bChanged = true;
		}
	}

	FString Tooltip;
	const bool bHasTooltip = VarJson->TryGetStringField(TEXT("tooltip"), Tooltip);
	bool bTooltipExplicit = false;
	VarJson->TryGetBoolField(TEXT("tooltip_explicit"), bTooltipExplicit);
	if (bHasTooltip && (bTooltipExplicit || !Tooltip.IsEmpty()))
	{
		const FString ExistingTooltip =
			Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip)
				? Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip)
				: FString();

		if (Tooltip.IsEmpty())
		{
			if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
			{
				Variable.RemoveMetaData(FBlueprintMetadata::MD_Tooltip);
				bChanged = true;
			}
		}
		else if (ExistingTooltip != Tooltip)
		{
			Variable.SetMetaData(FBlueprintMetadata::MD_Tooltip, Tooltip);
			bChanged = true;
		}
	}

	bool bReplicated = false;
	const bool bHasReplicated = VarJson->TryGetBoolField(TEXT("replicated"), bReplicated);
	bool bReplicatedExplicit = false;
	VarJson->TryGetBoolField(TEXT("replicated_explicit"), bReplicatedExplicit);
	if (bHasReplicated && (bReplicatedExplicit || bReplicated))
	{
		const bool bWasReplicated = (Variable.PropertyFlags & CPF_Net) != 0;
		if (bWasReplicated != bReplicated)
		{
			if (bReplicated)
			{
				Variable.PropertyFlags |= CPF_Net;
			}
			else
			{
				Variable.PropertyFlags &= ~CPF_Net;
			}
			bChanged = true;
		}
	}

	FString RepNotify;
	const bool bHasRepNotify = VarJson->TryGetStringField(TEXT("rep_notify"), RepNotify);
	bool bRepNotifyExplicit = false;
	VarJson->TryGetBoolField(TEXT("rep_notify_explicit"), bRepNotifyExplicit);
	if (bHasRepNotify && (bRepNotifyExplicit || !RepNotify.IsEmpty()))
	{
		const FName RepNotifyName(*RepNotify);
		if (Variable.RepNotifyFunc != RepNotifyName)
		{
			Variable.RepNotifyFunc = RepNotifyName;
			bChanged = true;
		}
	}

	bool bInstanceEditable = false;
	const bool bHasInstanceEditable = VarJson->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable);
	bool bInstanceEditableExplicit = false;
	VarJson->TryGetBoolField(TEXT("instance_editable_explicit"), bInstanceEditableExplicit);
	if (bHasInstanceEditable && (bInstanceEditableExplicit || bInstanceEditable))
	{
		const bool bWasInstanceEditable = (Variable.PropertyFlags & CPF_Edit) != 0;
		if (bWasInstanceEditable != bInstanceEditable)
		{
			if (bInstanceEditable)
			{
				Variable.PropertyFlags |= CPF_Edit;
			}
			else
			{
				Variable.PropertyFlags &= ~CPF_Edit;
			}
			bChanged = true;
		}
	}

	return bChanged;
}

bool CanSafelyOverwritePackageFile_ImportBpy(const FString& PackageFileName, FString& OutError)
{
	if (PackageFileName.IsEmpty() || !FPaths::FileExists(PackageFileName))
	{
		return true;
	}

#if PLATFORM_WINDOWS
	const HANDLE FileHandle = ::CreateFileW(
		*PackageFileName,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (FileHandle == INVALID_HANDLE_VALUE)
	{
		const DWORD LastError = ::GetLastError();
		if (LastError == ERROR_SHARING_VIOLATION || LastError == ERROR_ACCESS_DENIED)
		{
			OutError = FString::Printf(
				TEXT("Package file is locked by another process and cannot be overwritten right now: %s"),
				*PackageFileName);
			return false;
		}

		OutError = FString::Printf(
			TEXT("Cannot open package file for overwrite preflight (Win32 error %lu): %s"),
			static_cast<uint32>(LastError),
			*PackageFileName);
		return false;
	}

	::CloseHandle(FileHandle);
#endif

	return true;
}

static bool HasExplicitExecVariationPins_ImportBpy(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	return
		JsonObject->HasField(TEXT("execute")) ||
		JsonObject->HasField(TEXT("then")) ||
		JsonObject->HasField(TEXT("else"));
}

bool ShouldRestoreImpureVariableGet_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PinIdsObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("pin_ids"), PinIdsObj) && PinIdsObj && HasExplicitExecVariationPins_ImportBpy(*PinIdsObj))
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* PinAliasesObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("pin_aliases"), PinAliasesObj) && PinAliasesObj && HasExplicitExecVariationPins_ImportBpy(*PinAliasesObj))
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) && NodePropsObj && (*NodePropsObj).IsValid())
	{
		bool bBoolValue = true;
		if ((*NodePropsObj)->TryGetBoolField(TEXT("VariableGetIsPure"), bBoolValue))
		{
			return !bBoolValue;
		}

		FString StringValue;
		if ((*NodePropsObj)->TryGetStringField(TEXT("VariableGetIsPure"), StringValue))
		{
			StringValue.TrimStartAndEndInline();
			return
				StringValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) ||
				StringValue.Equals(TEXT("0"), ESearchCase::IgnoreCase) ||
				StringValue.Equals(TEXT("no"), ESearchCase::IgnoreCase) ||
				StringValue.Equals(TEXT("off"), ESearchCase::IgnoreCase);
		}
	}

	// The compiled graph payload normalizes VariableGetIsPure from actual exec
	// connections, so node_props is safe to trust. When none of pin_ids,
	// pin_aliases, or node_props indicate an impure variation, keep the node pure.

	return false;
}

UBlueprint* LoadBlueprintAsset_ImportBpy(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}

	if (UBlueprint* Loaded = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath)))
	{
		return Loaded;
	}

	FString ObjectPath = AssetPath;
	if (!AssetPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
	}

	if (UBlueprint* Loaded = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(ObjectPath)))
	{
		return Loaded;
	}

	return Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath));
}

static FString GCurrentImportSourceBlueprintPath_ImportBpy;
static FString GCurrentImportTargetBlueprintPath_ImportBpy;

FString NormalizeBlueprintObjectPath_ImportBpy(const FString& BlueprintPath)
{
	if (BlueprintPath.IsEmpty())
	{
		return FString();
	}

	if (BlueprintPath.StartsWith(TEXT("/")) && !BlueprintPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(BlueprintPath);
		return FString::Printf(TEXT("%s.%s"), *BlueprintPath, *AssetName);
	}

	return BlueprintPath;
}

FString BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(const FString& BlueprintPath)
{
	const FString ObjectPath = NormalizeBlueprintObjectPath_ImportBpy(BlueprintPath);
	if (ObjectPath.IsEmpty())
	{
		return FString();
	}

	const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
	const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return FString();
	}

	return FString::Printf(TEXT("%s.%s_C"), *PackagePath, *AssetName);
}

FString BuildGeneratedClassObjectNameFromBlueprintPath_ImportBpy(const FString& BlueprintPath)
{
	const FString ObjectPath = NormalizeBlueprintObjectPath_ImportBpy(BlueprintPath);
	if (ObjectPath.IsEmpty())
	{
		return FString();
	}

	const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
	return AssetName.IsEmpty() ? FString() : AssetName + TEXT("_C");
}

bool DoesBlueprintReferenceResolve_ImportBpy(const FString& ReferenceText, const bool bExpectClass)
{
	if (ReferenceText.IsEmpty())
	{
		return false;
	}

	return bExpectClass
		? (ResolveNamedObject_ImportBpy<UClass>(ReferenceText) != nullptr)
		: (ResolveNamedObject_ImportBpy<UObject>(ReferenceText) != nullptr);
}

FString RemapBlueprintReferenceForCurrentImport_ImportBpy(
	const FString& ReferenceText,
	const bool bExpectClass)
{
	if (ReferenceText.IsEmpty() ||
		GCurrentImportSourceBlueprintPath_ImportBpy.IsEmpty() ||
		GCurrentImportTargetBlueprintPath_ImportBpy.IsEmpty())
	{
		return ReferenceText;
	}

	const FString SourceBlueprintObjectPath =
		NormalizeBlueprintObjectPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString TargetBlueprintObjectPath =
		NormalizeBlueprintObjectPath_ImportBpy(GCurrentImportTargetBlueprintPath_ImportBpy);
	const FString SourceGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString TargetGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportTargetBlueprintPath_ImportBpy);

	auto TryResolveReplacement = [&](
		const FString& SearchText,
		const FString& ReplacementText) -> FString
	{
		if (SearchText.IsEmpty() || ReplacementText.IsEmpty() || !ReferenceText.Contains(SearchText))
		{
			return FString();
		}

		FString Candidate = ReferenceText;
		Candidate.ReplaceInline(*SearchText, *ReplacementText, ESearchCase::CaseSensitive);
		return DoesBlueprintReferenceResolve_ImportBpy(Candidate, bExpectClass) ? Candidate : FString();
	};

	if (const FString ExactClassRemap = TryResolveReplacement(SourceGeneratedClassPath, TargetGeneratedClassPath);
		!ExactClassRemap.IsEmpty())
	{
		return ExactClassRemap;
	}

	if (const FString ExactBlueprintRemap = TryResolveReplacement(SourceBlueprintObjectPath, TargetBlueprintObjectPath);
		!ExactBlueprintRemap.IsEmpty())
	{
		return ExactBlueprintRemap;
	}

	if (!ReferenceText.StartsWith(TEXT("/")))
	{
		return ReferenceText;
	}

	const FString SourcePackagePath = FPackageName::ObjectPathToPackageName(SourceBlueprintObjectPath);
	const FString TargetPackagePath = FPackageName::ObjectPathToPackageName(TargetBlueprintObjectPath);
	const FString SourceDirectory = FPackageName::GetLongPackagePath(SourcePackagePath);
	const FString TargetDirectory = FPackageName::GetLongPackagePath(TargetPackagePath);
	const FString SourceAssetName = FPackageName::ObjectPathToObjectName(SourceBlueprintObjectPath);
	const FString TargetAssetName = FPackageName::ObjectPathToObjectName(TargetBlueprintObjectPath);

	if (SourceDirectory.IsEmpty() || TargetDirectory.IsEmpty() || !TargetAssetName.StartsWith(SourceAssetName))
	{
		return ReferenceText;
	}

	const FString TargetSuffix = TargetAssetName.Mid(SourceAssetName.Len());
	if (TargetSuffix.IsEmpty())
	{
		return ReferenceText;
	}

	FString NormalizedReference = ReferenceText;
	if (!NormalizedReference.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(NormalizedReference);
		if (!AssetName.IsEmpty())
		{
			NormalizedReference = FString::Printf(TEXT("%s.%s"), *NormalizedReference, *AssetName);
		}
	}

	const FString ReferencePackagePath = FPackageName::ObjectPathToPackageName(NormalizedReference);
	if (FPackageName::GetLongPackagePath(ReferencePackagePath) != SourceDirectory)
	{
		return ReferenceText;
	}

	FString ReferenceAssetName = FPackageName::GetLongPackageAssetName(ReferencePackagePath);
	if (ReferenceAssetName.IsEmpty())
	{
		ReferenceAssetName = FPackageName::ObjectPathToObjectName(NormalizedReference);
		if (ReferenceAssetName.EndsWith(TEXT("_C")))
		{
			ReferenceAssetName.LeftChopInline(2);
		}
	}

	if (ReferenceAssetName.IsEmpty() || ReferenceAssetName.EndsWith(TargetSuffix))
	{
		return ReferenceText;
	}

	const bool bReferenceIsClassPath =
		FPackageName::ObjectPathToObjectName(NormalizedReference).EndsWith(TEXT("_C"));
	const FString CandidateAssetName = ReferenceAssetName + TargetSuffix;
	const FString CandidatePackagePath =
		FString::Printf(TEXT("%s/%s"), *TargetDirectory, *CandidateAssetName);
	const FString CandidateObjectName =
		bReferenceIsClassPath ? CandidateAssetName + TEXT("_C") : CandidateAssetName;
	const FString CandidateObjectPath =
		FString::Printf(TEXT("%s.%s"), *CandidatePackagePath, *CandidateObjectName);

	return DoesBlueprintReferenceResolve_ImportBpy(CandidateObjectPath, bExpectClass || bReferenceIsClassPath)
		? CandidateObjectPath
		: ReferenceText;
}

FString RemapBlueprintReferencesInSerializedText_ImportBpy(const FString& SerializedText)
{
	if (SerializedText.IsEmpty() ||
		GCurrentImportSourceBlueprintPath_ImportBpy.IsEmpty() ||
		GCurrentImportTargetBlueprintPath_ImportBpy.IsEmpty())
	{
		return SerializedText;
	}

	const FString SourceBlueprintObjectPath =
		NormalizeBlueprintObjectPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString TargetBlueprintObjectPath =
		NormalizeBlueprintObjectPath_ImportBpy(GCurrentImportTargetBlueprintPath_ImportBpy);
	const FString SourceGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString TargetGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportTargetBlueprintPath_ImportBpy);

	FString RemappedText = SerializedText;
	if (!SourceGeneratedClassPath.IsEmpty() && !TargetGeneratedClassPath.IsEmpty())
	{
		RemappedText.ReplaceInline(
			*SourceGeneratedClassPath,
			*TargetGeneratedClassPath,
			ESearchCase::CaseSensitive);
	}

	if (!SourceBlueprintObjectPath.IsEmpty() && !TargetBlueprintObjectPath.IsEmpty())
	{
		RemappedText.ReplaceInline(
			*SourceBlueprintObjectPath,
			*TargetBlueprintObjectPath,
			ESearchCase::CaseSensitive);
	}

	return RemappedText;
}

bool IsSourceGeneratedClassReferenceForCurrentImport_ImportBpy(UClass* CandidateClass)
{
	if (!CandidateClass || GCurrentImportSourceBlueprintPath_ImportBpy.IsEmpty())
	{
		return false;
	}

	const FString SourceBlueprintObjectPath =
		NormalizeBlueprintObjectPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString SourceGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);

	if (!SourceGeneratedClassPath.IsEmpty() &&
		CandidateClass->GetPathName().Equals(SourceGeneratedClassPath, ESearchCase::CaseSensitive))
	{
		return true;
	}

	if (const UObject* ClassGeneratedBy = CandidateClass->ClassGeneratedBy)
	{
		return ClassGeneratedBy->GetPathName().Equals(SourceBlueprintObjectPath, ESearchCase::CaseSensitive);
	}

	return false;
}

void RemapSourceGeneratedClassPinsToCurrentBlueprint_ImportBpy(UEdGraphNode* Node)
{
	if (!Node || GCurrentImportSourceBlueprintPath_ImportBpy.IsEmpty())
	{
		return;
	}

	UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	if (!OwningBlueprint)
	{
		return;
	}

	UClass* TargetGeneratedClass =
		OwningBlueprint->GeneratedClass
			? OwningBlueprint->GeneratedClass
			: OwningBlueprint->SkeletonGeneratedClass;
	if (!TargetGeneratedClass)
	{
		return;
	}


	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin)
		{
			continue;
		}

		if (UClass* PinSubCategoryClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			if (IsSourceGeneratedClassReferenceForCurrentImport_ImportBpy(PinSubCategoryClass) &&
				PinSubCategoryClass != TargetGeneratedClass)
			{
				Pin->PinType.PinSubCategoryObject = TargetGeneratedClass;
			}
		}

		if (UClass* TerminalClass = Cast<UClass>(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get()))
		{
			if (IsSourceGeneratedClassReferenceForCurrentImport_ImportBpy(TerminalClass) &&
				TerminalClass != TargetGeneratedClass)
			{
				Pin->PinType.PinValueType.TerminalSubCategoryObject = TargetGeneratedClass;
			}
		}

		if (UClass* DefaultClass = Cast<UClass>(Pin->DefaultObject))
		{
			if (IsSourceGeneratedClassReferenceForCurrentImport_ImportBpy(DefaultClass) &&
				DefaultClass != TargetGeneratedClass)
			{
				Pin->DefaultObject = TargetGeneratedClass;
			}
		}
	}
}

FString DescribeNode_ImportBpy(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return TEXT("<null>");
	}

	FString Label;
	if (Node->GetNodeTitle(ENodeTitleType::ListView).ToString().TrimStartAndEnd().Len() > 0)
	{
		Label = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	}
	else
	{
		Label = Node->GetClass()->GetName();
	}

	return FString::Printf(TEXT("%s [%s]"), *Label, *Node->GetClass()->GetName());
}

void BreakAllNodeLinks_ImportBpy(UEdGraphNode* Node)
{
	if (!Node)
	{
		return;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			Pin->BreakAllPinLinks();
		}
	}

	Node->NodeConnectionListChanged();
}

void BreakAllGraphLinks_ImportBpy(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		BreakAllNodeLinks_ImportBpy(Node);
	}

	Graph->NotifyGraphChanged();
}

FString NormalizeStandaloneAssetObjectPath_ImportBpy(const FString& AssetPath)
{
	FString Normalized = AssetPath;
	Normalized.TrimStartAndEndInline();
	if (Normalized.StartsWith(TEXT("/")) && !Normalized.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(Normalized);
		if (!AssetName.IsEmpty())
		{
			return FString::Printf(TEXT("%s.%s"), *Normalized, *AssetName);
		}
	}

	return Normalized;
}

UObject* LoadStandaloneAsset_ImportBpy(const FString& AssetPath)
{
	const FString ObjectPath = NormalizeStandaloneAssetObjectPath_ImportBpy(AssetPath);
	if (ObjectPath.IsEmpty())
	{
		return nullptr;
	}

	if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		return Loaded;
	}

	const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
	if (!PackagePath.IsEmpty())
	{
		if (UObject* Loaded = Cast<UObject>(UEditorAssetLibrary::LoadAsset(PackagePath)))
		{
			return Loaded;
		}
	}

	return nullptr;
}

FString ResolveEnhancedInputActionRef_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return FString();
	}

	FString ActionRef = GetNodePropString_ImportBpy(NodeJson, TEXT("InputAction"));
	if (!ActionRef.IsEmpty())
	{
		return ActionRef;
	}

	NodeJson->TryGetStringField(TEXT("input_action_path"), ActionRef);
	if (!ActionRef.IsEmpty())
	{
		return ActionRef;
	}

	NodeJson->TryGetStringField(TEXT("input_action"), ActionRef);
	if (!ActionRef.IsEmpty())
	{
		return ActionRef;
	}

	NodeJson->TryGetStringField(TEXT("member_name"), ActionRef);
	return ActionRef;
}

UInputAction* ResolveInputActionAsset_ImportBpy(const FString& ActionRef)
{
	if (ActionRef.IsEmpty())
	{
		return nullptr;
	}

	if (UInputAction* InputAction = ResolveNamedObject_ImportBpy<UInputAction>(ActionRef))
	{
		return InputAction;
	}

	const FString NormalizedObjectPath = NormalizeStandaloneAssetObjectPath_ImportBpy(ActionRef);
	if (!NormalizedObjectPath.Equals(ActionRef, ESearchCase::CaseSensitive))
	{
		if (UInputAction* InputAction = ResolveNamedObject_ImportBpy<UInputAction>(NormalizedObjectPath))
		{
			return InputAction;
		}
	}

	FString AssetName = ActionRef;
	if (ActionRef.StartsWith(TEXT("/")))
	{
		AssetName = FPackageName::GetLongPackageAssetName(ActionRef);
		if (AssetName.IsEmpty())
		{
			AssetName = FPaths::GetBaseFilename(ActionRef);
		}
	}
	else if (ActionRef.Contains(TEXT(".")))
	{
		AssetName = FPaths::GetBaseFilename(ActionRef);
	}

	AssetName.TrimStartAndEndInline();
	if (AssetName.IsEmpty())
	{
		return nullptr;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(AssetName, ESearchCase::IgnoreCase))
		{
			return Cast<UInputAction>(Asset.GetAsset());
		}
	}

	return nullptr;
}

bool IsEnhancedInputActionNode_ImportBpy(const UEdGraphNode* Node)
{
	return Node && Node->GetClass() && Node->GetClass()->GetName().Equals(TEXT("K2Node_EnhancedInputAction"), ESearchCase::CaseSensitive);
}

FString ResolveGetSubsystemTargetTypeRef_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return FString();
	}

	FString TargetType;
	if (NodeJson->TryGetStringField(TEXT("target_type"), TargetType) && !TargetType.IsEmpty())
	{
		return TargetType;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) && NodePropsObj && NodePropsObj->IsValid())
	{
		if ((*NodePropsObj)->TryGetStringField(TEXT("CustomClass"), TargetType) && !TargetType.IsEmpty())
		{
			return TargetType;
		}

		if ((*NodePropsObj)->TryGetStringField(TEXT("TargetType"), TargetType) && !TargetType.IsEmpty())
		{
			return TargetType;
		}
	}

	return FString();
}

bool IsGetSubsystemNode_ImportBpy(const UEdGraphNode* Node)
{
	if (!Node || !Node->GetClass())
	{
		return false;
	}

	const FString ClassName = Node->GetClass()->GetName();
	return ClassName.Equals(TEXT("K2Node_GetSubsystem"), ESearchCase::CaseSensitive)
		|| ClassName.Equals(TEXT("K2Node_GetSubsystemFromPC"), ESearchCase::CaseSensitive)
		|| ClassName.Equals(TEXT("K2Node_GetEngineSubsystem"), ESearchCase::CaseSensitive)
		|| ClassName.Equals(TEXT("K2Node_GetEditorSubsystem"), ESearchCase::CaseSensitive);
}

bool ApplyGetSubsystemClassToNode_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError)
{
	if (!IsGetSubsystemNode_ImportBpy(Node))
	{
		return true;
	}

	const FString TargetType = ResolveGetSubsystemTargetTypeRef_ImportBpy(NodeJson);
	if (TargetType.IsEmpty())
	{
		return true;
	}

	UClass* const SubsystemClass = ResolveNamedObject_ImportBpy<UClass>(TargetType);
	if (!SubsystemClass)
	{
		OutError = FString::Printf(
			TEXT("Cannot resolve subsystem class '%s' on node %s"),
			*TargetType,
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	FProperty* const Property = Node->GetClass()->FindPropertyByName(TEXT("CustomClass"));
	FObjectPropertyBase* const ObjectProperty = CastField<FObjectPropertyBase>(Property);
	void* const PropertyAddress = ObjectProperty ? ObjectProperty->ContainerPtrToValuePtr<void>(Node) : nullptr;
	if (!ObjectProperty || !PropertyAddress)
	{
		OutError = FString::Printf(
			TEXT("GetSubsystem node %s does not expose CustomClass property for import"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	Node->Modify();
	ObjectProperty->SetObjectPropertyValue(PropertyAddress, SubsystemClass);

	if (UEdGraphPin* ClassPin = Node->FindPin(TEXT("Class")))
	{
		ClassPin->DefaultObject = SubsystemClass;
		ClassPin->DefaultValue = SubsystemClass->GetPathName();
		ClassPin->AutogeneratedDefaultValue = ClassPin->DefaultValue;
	}

	if (UEdGraphPin* ResultPin = Node->FindPin(UEdGraphSchema_K2::PN_ReturnValue))
	{
		ResultPin->PinType.PinSubCategoryObject = SubsystemClass;
	}

	return true;
}

bool ApplyEnhancedInputActionToNode_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError)
{
	if (!IsEnhancedInputActionNode_ImportBpy(Node))
	{
		return true;
	}

	const FString ActionRef = ResolveEnhancedInputActionRef_ImportBpy(NodeJson);
	if (ActionRef.IsEmpty())
	{
		return true;
	}

	UInputAction* const InputAction = ResolveInputActionAsset_ImportBpy(ActionRef);
	if (!InputAction)
	{
		OutError = FString::Printf(
			TEXT("Cannot resolve enhanced input action '%s' on node %s"),
			*ActionRef,
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	FProperty* const Property = Node->GetClass()->FindPropertyByName(TEXT("InputAction"));
	FObjectPropertyBase* const ObjectProperty = CastField<FObjectPropertyBase>(Property);
	void* const PropertyAddress = ObjectProperty ? Property->ContainerPtrToValuePtr<void>(Node) : nullptr;
	if (!ObjectProperty || !PropertyAddress)
	{
		OutError = FString::Printf(
			TEXT("Enhanced input node %s does not expose InputAction property for import"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	Node->Modify();
	ObjectProperty->SetObjectPropertyValue(PropertyAddress, InputAction);

	if (UEdGraphPin* ActionPin = Node->FindPin(TEXT("InputAction")))
	{
		ActionPin->DefaultObject = InputAction;
		ActionPin->DefaultValue = InputAction->GetPathName();
		ActionPin->AutogeneratedDefaultValue = ActionPin->DefaultValue;
	}

	return true;
}

UEdGraphNode* CreateEnhancedInputActionNode_ImportBpy(
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError)
{
	if (!Graph || !NodeJson.IsValid())
	{
		return nullptr;
	}

	const FString ActionRef = ResolveEnhancedInputActionRef_ImportBpy(NodeJson);
	if (!ResolveInputActionAsset_ImportBpy(ActionRef))
	{
		OutError = FString::Printf(TEXT("Cannot resolve enhanced input action '%s'"), *ActionRef);
		return nullptr;
	}

	UClass* const EnhancedInputNodeClass = ResolveNodeClass_ImportBpy(TEXT("K2Node_EnhancedInputAction"));
	if (!EnhancedInputNodeClass || !EnhancedInputNodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		OutError = TEXT("Cannot resolve K2Node_EnhancedInputAction class");
		return nullptr;
	}

	UEdGraphNode* ActionNode = NewObject<UEdGraphNode>(Graph, EnhancedInputNodeClass);
	if (!ActionNode)
	{
		OutError = TEXT("Failed to allocate K2Node_EnhancedInputAction");
		return nullptr;
	}

	ActionNode->CreateNewGuid();
	ActionNode->PostPlacedNewNode();
	Graph->AddNode(ActionNode, false, false);
	if (!ApplyEnhancedInputActionToNode_ImportBpy(ActionNode, NodeJson, OutError))
	{
		return nullptr;
	}
	ActionNode->AllocateDefaultPins();
	if (!ApplyEnhancedInputActionToNode_ImportBpy(ActionNode, NodeJson, OutError))
	{
		return nullptr;
	}
	ActionNode->ReconstructNode();
	if (!ApplyEnhancedInputActionToNode_ImportBpy(ActionNode, NodeJson, OutError))
	{
		return nullptr;
	}
	return ActionNode;
}

UClass* ResolveNodeClass_ImportBpy(const FString& NodeClassName)
{
	if (NodeClassName.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* NodeClass = ResolveNamedObject_ImportBpy<UClass>(NodeClassName))
	{
		return NodeClass;
	}

	if (!NodeClassName.Contains(TEXT("/")) && !NodeClassName.Contains(TEXT(".")))
	{
		const FString NormalizedClassName = NodeClassName.StartsWith(TEXT("U"))
			? NodeClassName.RightChop(1)
			: NodeClassName;

		TArray<const TCHAR*> ScriptModules;
		if (NormalizedClassName.StartsWith(TEXT("K2Node_")))
		{
			ScriptModules = {
				TEXT("BlueprintGraph"),
				TEXT("InputBlueprintNodes"),
				TEXT("PropertyAccessNode"),
				TEXT("ChooserUncooked"),
				TEXT("MoverEditor"),
			};
		}
		else if (NormalizedClassName.StartsWith(TEXT("AnimGraphNode_")))
		{
			ScriptModules = {
				TEXT("AnimGraph"),
				TEXT("ControlRigDeveloper"),
				TEXT("PoseSearchEditor"),
				TEXT("ChooserUncooked"),
			};
		}
		else if (NormalizedClassName.StartsWith(TEXT("AnimState")))
		{
			ScriptModules = {
				TEXT("AnimGraph"),
			};
		}
		else if (NormalizedClassName == TEXT("EdGraphNode_Comment"))
		{
			ScriptModules = {
				TEXT("UnrealEd"),
			};
		}

		for (const TCHAR* ModuleName : ScriptModules)
		{
			if (UClass* NodeClass = ResolveNamedObject_ImportBpy<UClass>(
				FString::Printf(TEXT("/Script/%s.%s"), ModuleName, *NormalizedClassName)))
			{
				return NodeClass;
			}
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(NormalizedClassName, ESearchCase::CaseSensitive)
				|| It->GetName().Equals(NodeClassName, ESearchCase::CaseSensitive))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

UClass* ResolveComponentClass_ImportBpy(const FString& ComponentClassName)
{
	if (ComponentClassName.IsEmpty())
	{
		return nullptr;
	}

	TArray<FString> Candidates;
	Candidates.Add(ComponentClassName);

	if (!ComponentClassName.StartsWith(TEXT("U")))
	{
		Candidates.Add(TEXT("U") + ComponentClassName);
	}
	if (!ComponentClassName.EndsWith(TEXT("Component")))
	{
		Candidates.Add(ComponentClassName + TEXT("Component"));
	}
	if (!ComponentClassName.StartsWith(TEXT("U")) && !ComponentClassName.EndsWith(TEXT("Component")))
	{
		Candidates.Add(TEXT("U") + ComponentClassName + TEXT("Component"));
	}

	for (const FString& Candidate : Candidates)
	{
		if (UClass* ComponentClass = ResolveNamedObject_ImportBpy<UClass>(Candidate))
		{
			return ComponentClass;
		}

		if (!Candidate.Contains(TEXT("/")) && !Candidate.Contains(TEXT(".")))
		{
			if (UClass* EngineClass = ResolveNamedObject_ImportBpy<UClass>(FString::Printf(TEXT("/Script/Engine.%s"), *Candidate)))
			{
				return EngineClass;
			}
		}
	}

	FString DesiredGeneratedClassName = ComponentClassName;
	FString DesiredBlueprintAssetName = ComponentClassName;
	if (!DesiredGeneratedClassName.Contains(TEXT("/")) && !DesiredGeneratedClassName.Contains(TEXT(".")))
	{
		if (DesiredBlueprintAssetName.EndsWith(TEXT("_C")))
		{
			DesiredBlueprintAssetName.LeftChopInline(2);
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(DesiredGeneratedClassName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> BlueprintAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssets, true);
		for (const FAssetData& Asset : BlueprintAssets)
		{
			if (!Asset.AssetName.ToString().Equals(DesiredBlueprintAssetName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset.GetAsset()))
			{
				if (UClass* GeneratedClass = BlueprintAsset->GeneratedClass)
				{
					if (GeneratedClass->GetName().Equals(DesiredGeneratedClassName, ESearchCase::IgnoreCase) ||
						GeneratedClass->IsChildOf(UActorComponent::StaticClass()))
					{
						return GeneratedClass;
					}
				}
			}
		}
	}

	return nullptr;
}

bool CreateOrReplaceStandaloneAsset_ImportBpy(
	const FString& AssetPath,
	const FString& AssetClassPath,
	bool bReplaceExisting,
	UObject*& OutAsset,
	FString& OutError)
{
	OutAsset = nullptr;

	const FString ObjectPath = NormalizeStandaloneAssetObjectPath_ImportBpy(AssetPath);
	const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
	const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Invalid standalone asset path: %s"), *AssetPath);
		return false;
	}

	UClass* AssetClass = ResolveNamedObject_ImportBpy<UClass>(AssetClassPath);
	if (!AssetClass)
	{
		OutError = FString::Printf(TEXT("Cannot load asset class: %s"), *AssetClassPath);
		return false;
	}

	if (UObject* ExistingAsset = LoadStandaloneAsset_ImportBpy(ObjectPath))
	{
		// Prefer in-place update when the existing asset class is compatible.
		// This avoids force-delete failures for assets referenced by async systems.
		if (ExistingAsset->GetClass() == AssetClass ||
			ExistingAsset->GetClass()->IsChildOf(AssetClass) ||
			AssetClass->IsChildOf(ExistingAsset->GetClass()))
		{
			OutAsset = ExistingAsset;
			return true;
		}

		if (!bReplaceExisting)
		{
			OutError = FString::Printf(
				TEXT("Existing asset class mismatch and replace is disabled: %s (existing=%s, requested=%s)"),
				*ObjectPath,
				*ExistingAsset->GetClass()->GetPathName(),
				*AssetClass->GetPathName());
			return false;
		}
	}
	else if (!bReplaceExisting)
	{
		OutError = FString::Printf(TEXT("Target asset does not exist and replace is disabled: %s"), *ObjectPath);
		return false;
	}

	if (bReplaceExisting && UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		if (UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath))
		{
			if (!ExistingPackage->IsFullyLoaded())
			{
				ExistingPackage->FullyLoad();
			}
		}

		if (!UEditorAssetLibrary::DeleteAsset(PackagePath))
		{
			OutError = FString::Printf(TEXT("Failed to delete existing asset before import: %s"), *PackagePath);
			return false;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Cannot create package for asset: %s"), *PackagePath);
		return false;
	}

	// Standalone assets are frequently round-tripped into an existing package path.
	// After delete/recreate, UE can keep the package in a partially-loaded state,
	// which then hard-fails SavePackage. Force the package into a saveable state.
	if (!Package->IsFullyLoaded())
	{
		Package->FullyLoad();
		if (!Package->IsFullyLoaded())
		{
			Package->MarkAsFullyLoaded();
		}
	}

	if (AssetClass->IsChildOf(UUserDefinedEnum::StaticClass()))
	{
		OutAsset = FEnumEditorUtils::CreateUserDefinedEnum(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
	}
	else
	{
		OutAsset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone);
	}

	if (!OutAsset)
	{
		OutError = FString::Printf(TEXT("Failed to create asset: %s (%s)"), *ObjectPath, *AssetClassPath);
		return false;
	}

	FAssetRegistryModule::AssetCreated(OutAsset);
	return true;
}

struct FEnhancedMappingInstancedRefs_ImportBpy
{
	FString ActionRef;
	FString KeyName;
	TArray<FString> ModifierNames;
	TArray<FString> TriggerNames;
};

bool FindMatchingParenthesis_ImportBpy(const FString& Text, int32 OpenIndex, int32& OutCloseIndex)
{
	if (!Text.IsValidIndex(OpenIndex) || Text[OpenIndex] != TEXT('('))
	{
		return false;
	}

	int32 Depth = 0;
	bool bInQuotes = false;
	for (int32 Index = OpenIndex; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		if (Char == TEXT('"'))
		{
			bInQuotes = !bInQuotes;
			continue;
		}

		if (bInQuotes)
		{
			continue;
		}

		if (Char == TEXT('('))
		{
			++Depth;
		}
		else if (Char == TEXT(')'))
		{
			--Depth;
			if (Depth == 0)
			{
				OutCloseIndex = Index;
				return true;
			}
			if (Depth < 0)
			{
				return false;
			}
		}
	}

	return false;
}

TArray<FString> SplitTopLevelCommaSeparated_ImportBpy(const FString& Text)
{
	TArray<FString> Results;
	FString Current;
	int32 Depth = 0;
	bool bInQuotes = false;

	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		if (Char == TEXT('"'))
		{
			bInQuotes = !bInQuotes;
			Current.AppendChar(Char);
			continue;
		}

		if (!bInQuotes)
		{
			if (Char == TEXT('('))
			{
				++Depth;
			}
			else if (Char == TEXT(')'))
			{
				Depth = FMath::Max(0, Depth - 1);
			}
			else if (Char == TEXT(',') && Depth == 0)
			{
				Current.TrimStartAndEndInline();
				if (!Current.IsEmpty())
				{
					Results.Add(Current);
				}
				Current.Reset();
				continue;
			}
		}

		Current.AppendChar(Char);
	}

	Current.TrimStartAndEndInline();
	if (!Current.IsEmpty())
	{
		Results.Add(Current);
	}

	return Results;
}

TArray<FString> ExtractInstancedObjectNamesFromField_ImportBpy(const FString& MappingEntryText, const FString& FieldName)
{
	TArray<FString> Names;
	const FString Token = FieldName + TEXT("=(");
	const int32 TokenIndex = MappingEntryText.Find(Token, ESearchCase::CaseSensitive);
	if (TokenIndex == INDEX_NONE)
	{
		return Names;
	}

	const int32 OpenIndex = TokenIndex + FieldName.Len() + 1;
	int32 CloseIndex = INDEX_NONE;
	if (!FindMatchingParenthesis_ImportBpy(MappingEntryText, OpenIndex, CloseIndex))
	{
		return Names;
	}

	const FString InnerText = MappingEntryText.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1);
	for (FString Entry : SplitTopLevelCommaSeparated_ImportBpy(InnerText))
	{
		Entry.TrimStartAndEndInline();
		if (Entry.IsEmpty() || Entry.Equals(TEXT("None"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		if (Entry.StartsWith(TEXT("\"")) && Entry.EndsWith(TEXT("\"")) && Entry.Len() >= 2)
		{
			Entry = Entry.Mid(1, Entry.Len() - 2);
		}

		int32 ColonIndex = INDEX_NONE;
		if (!Entry.FindLastChar(TEXT(':'), ColonIndex))
		{
			continue;
		}

		FString ObjectName = Entry.Mid(ColonIndex + 1);
		int32 QuoteTailIndex = INDEX_NONE;
		if (ObjectName.FindLastChar(TEXT('\''), QuoteTailIndex))
		{
			ObjectName = ObjectName.Left(QuoteTailIndex);
		}

		ObjectName.TrimStartAndEndInline();
		if (!ObjectName.IsEmpty())
		{
			Names.Add(ObjectName);
		}
	}

	return Names;
}

FString ExtractSingleFieldValueFromMappingEntry_ImportBpy(const FString& MappingEntryText, const FString& FieldName)
{
	const FString Token = FieldName + TEXT("=");
	const int32 TokenIndex = MappingEntryText.Find(Token, ESearchCase::CaseSensitive);
	if (TokenIndex == INDEX_NONE)
	{
		return FString();
	}

	int32 ValueStart = TokenIndex + Token.Len();
	while (ValueStart < MappingEntryText.Len() && FChar::IsWhitespace(MappingEntryText[ValueStart]))
	{
		++ValueStart;
	}

	if (!MappingEntryText.IsValidIndex(ValueStart))
	{
		return FString();
	}

	int32 ValueEnd = ValueStart;
	bool bInQuotes = false;
	if (MappingEntryText[ValueStart] == TEXT('\"'))
	{
		bInQuotes = true;
		++ValueEnd;
		while (ValueEnd < MappingEntryText.Len())
		{
			if (MappingEntryText[ValueEnd] == TEXT('\"'))
			{
				++ValueEnd;
				break;
			}
			++ValueEnd;
		}
	}
	else
	{
		while (ValueEnd < MappingEntryText.Len())
		{
			const TCHAR Char = MappingEntryText[ValueEnd];
			if (Char == TEXT(',') || Char == TEXT(')'))
			{
				break;
			}
			++ValueEnd;
		}
	}

	FString Value = MappingEntryText.Mid(ValueStart, ValueEnd - ValueStart);
	Value.TrimStartAndEndInline();
	if (bInQuotes && Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
	{
		Value = Value.Mid(1, Value.Len() - 2);
	}

	return Value;
}

FKey ResolveInputKey_ImportBpy(const FString& RequestedKeyName)
{
	const FString TrimmedKeyName = RequestedKeyName.TrimStartAndEnd();
	if (TrimmedKeyName.IsEmpty())
	{
		return EKeys::Invalid;
	}

	const FKey DirectKey(*TrimmedKeyName);
	if (DirectKey.IsValid())
	{
		return DirectKey;
	}

	TArray<FKey> AllKeys;
	EKeys::GetAllKeys(AllKeys);
	for (const FKey& CandidateKey : AllKeys)
	{
		if (!CandidateKey.IsValid())
		{
			continue;
		}

		if (CandidateKey.GetFName().ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase) ||
			CandidateKey.ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase) ||
			CandidateKey.GetDisplayName(false).ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase) ||
			CandidateKey.GetDisplayName(true).ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase))
		{
			return CandidateKey;
		}
	}

	return EKeys::Invalid;
}

bool ParseInputMappingInstancedRefs_ImportBpy(
	const FString& DefaultKeyMappingsText,
	TArray<FEnhancedMappingInstancedRefs_ImportBpy>& OutRefs,
	FString& OutError)
{
	OutRefs.Reset();

	const FString Token = TEXT("Mappings=(");
	const int32 TokenIndex = DefaultKeyMappingsText.Find(Token, ESearchCase::CaseSensitive);
	if (TokenIndex == INDEX_NONE)
	{
		return true;
	}

	const int32 OpenIndex = TokenIndex + Token.Len() - 1;
	int32 CloseIndex = INDEX_NONE;
	if (!FindMatchingParenthesis_ImportBpy(DefaultKeyMappingsText, OpenIndex, CloseIndex))
	{
		OutError = TEXT("Failed to parse DefaultKeyMappings text: unmatched parenthesis in Mappings");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
		return false;
	}

	const FString InnerMappingsText = DefaultKeyMappingsText.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1);
	for (const FString& MappingEntryText : SplitTopLevelCommaSeparated_ImportBpy(InnerMappingsText))
	{
		FEnhancedMappingInstancedRefs_ImportBpy MappingRefs;
		MappingRefs.ActionRef = ExtractSingleFieldValueFromMappingEntry_ImportBpy(MappingEntryText, TEXT("Action"));
		MappingRefs.KeyName = ExtractSingleFieldValueFromMappingEntry_ImportBpy(MappingEntryText, TEXT("Key"));
		MappingRefs.ModifierNames = ExtractInstancedObjectNamesFromField_ImportBpy(MappingEntryText, TEXT("Modifiers"));
		MappingRefs.TriggerNames = ExtractInstancedObjectNamesFromField_ImportBpy(MappingEntryText, TEXT("Triggers"));
		OutRefs.Add(MappingRefs);
	}

	return true;
}

TArray<FString> ExtractInstancedObjectNamesFromText_ImportBpy(const FString& Text)
{
	TArray<FString> Names;
	int32 SearchIndex = 0;

	while (SearchIndex < Text.Len())
	{
		const int32 ColonIndex = Text.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchIndex);
		if (ColonIndex == INDEX_NONE)
		{
			break;
		}

		int32 EndIndex = ColonIndex + 1;
		while (EndIndex < Text.Len())
		{
			const TCHAR Char = Text[EndIndex];
			if (Char == TEXT('\'') || Char == TEXT('"') || Char == TEXT(',') || Char == TEXT(')'))
			{
				break;
			}
			++EndIndex;
		}

		FString ObjectName = Text.Mid(ColonIndex + 1, EndIndex - ColonIndex - 1);
		ObjectName.TrimStartAndEndInline();
		if (!ObjectName.IsEmpty())
		{
			Names.Add(ObjectName);
		}

		SearchIndex = EndIndex + 1;
	}

	return Names;
}

struct FInstancedSubobjectRef_ImportBpy
{
	FString Name;
	UClass* Class = nullptr;
};

void ExtractInstancedSubobjectRefsFromSerializedText_ImportBpy(
	const FString& SerializedText,
	TArray<FInstancedSubobjectRef_ImportBpy>& OutRefs)
{
	OutRefs.Reset();

	int32 SearchIndex = 0;
	while (SearchIndex < SerializedText.Len())
	{
		const int32 OpenQuoteIndex =
			SerializedText.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchIndex);
		if (OpenQuoteIndex == INDEX_NONE)
		{
			break;
		}

		int32 ClassStartIndex = OpenQuoteIndex - 1;
		while (ClassStartIndex >= 0)
		{
			const TCHAR Char = SerializedText[ClassStartIndex];
			if (Char == TEXT('(') || Char == TEXT(',') || Char == TEXT('"') || FChar::IsWhitespace(Char))
			{
				break;
			}
			--ClassStartIndex;
		}
		++ClassStartIndex;

		const int32 CloseQuoteIndex =
			SerializedText.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenQuoteIndex + 1);
		if (CloseQuoteIndex == INDEX_NONE)
		{
			break;
		}

		FString ClassPath = SerializedText.Mid(ClassStartIndex, OpenQuoteIndex - ClassStartIndex);
		FString ObjectPath = SerializedText.Mid(OpenQuoteIndex + 1, CloseQuoteIndex - OpenQuoteIndex - 1);
		ClassPath.TrimStartAndEndInline();
		ObjectPath.TrimStartAndEndInline();
		SearchIndex = CloseQuoteIndex + 1;

		if (ClassPath.IsEmpty() || ObjectPath.IsEmpty())
		{
			continue;
		}

		int32 LeafSeparatorIndex = INDEX_NONE;
		if (!ObjectPath.FindLastChar(TEXT('.'), LeafSeparatorIndex))
		{
			ObjectPath.FindLastChar(TEXT(':'), LeafSeparatorIndex);
		}

		FString ObjectName = LeafSeparatorIndex != INDEX_NONE ? ObjectPath.Mid(LeafSeparatorIndex + 1) : ObjectPath;
		ObjectName.TrimStartAndEndInline();
		if (ObjectName.IsEmpty())
		{
			continue;
		}

		if (UClass* SubobjectClass = ResolveNamedObject_ImportBpy<UClass>(ClassPath))
		{
			FInstancedSubobjectRef_ImportBpy& Ref = OutRefs.AddDefaulted_GetRef();
			Ref.Name = ObjectName;
			Ref.Class = SubobjectClass;
		}
	}
}

bool EnsureInstancedSubobjectsExistForSerializedPropertyText_ImportBpy(
	UObject* Outer,
	const FString& SerializedText,
	FString& OutError)
{
	if (!Outer || SerializedText.IsEmpty())
	{
		return true;
	}

	TArray<FInstancedSubobjectRef_ImportBpy> Refs;
	ExtractInstancedSubobjectRefsFromSerializedText_ImportBpy(SerializedText, Refs);
	for (const FInstancedSubobjectRef_ImportBpy& Ref : Refs)
	{
		if (Ref.Name.IsEmpty() || !Ref.Class)
		{
			continue;
		}

		UObject* ExistingObject = FindObject<UObject>(Outer, *Ref.Name);
		if (ExistingObject && !ExistingObject->IsA(Ref.Class))
		{
			ExistingObject->Rename(
				nullptr,
				GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			ExistingObject = nullptr;
		}

		if (!ExistingObject)
		{
			ExistingObject = NewObject<UObject>(Outer, Ref.Class, *Ref.Name, RF_Public | RF_Transactional);
		}

		if (!ExistingObject)
		{
			OutError = FString::Printf(
				TEXT("Failed to create instanced subobject '%s' of class '%s' under '%s'"),
				*Ref.Name,
				*Ref.Class->GetPathName(),
				*Outer->GetPathName());
			return false;
		}
	}

	return true;
}

bool RestoreInputMappingInstancedRefs_ImportBpy(
	UObject* Asset,
	const TSharedPtr<FJsonObject>* StandalonePropertiesObj,
	FString& OutError)
{
	UInputMappingContext* InputMappingContext = Cast<UInputMappingContext>(Asset);
	if (!InputMappingContext || !StandalonePropertiesObj || !(*StandalonePropertiesObj).IsValid())
	{
		return true;
	}

	FString DefaultKeyMappingsText;
	if (!(*StandalonePropertiesObj)->TryGetStringField(TEXT("DefaultKeyMappings"), DefaultKeyMappingsText) ||
		DefaultKeyMappingsText.IsEmpty())
	{
		return true;
	}

	TArray<FEnhancedMappingInstancedRefs_ImportBpy> ParsedRefs;
	if (!ParseInputMappingInstancedRefs_ImportBpy(DefaultKeyMappingsText, ParsedRefs, OutError))
	{
		return false;
	}

	TArray<UObject*> InnerObjects;
	GetObjectsWithOuter(Asset, InnerObjects, false);

	TMap<FString, UObject*> InnerObjectMap;
	for (UObject* InnerObject : InnerObjects)
	{
		if (InnerObject)
		{
			InnerObjectMap.Add(InnerObject->GetName(), InnerObject);
		}
	}

	InputMappingContext->UnmapAll();

	for (int32 MappingIndex = 0; MappingIndex < ParsedRefs.Num(); ++MappingIndex)
	{
		UInputAction* Action = nullptr;
		if (!ParsedRefs[MappingIndex].ActionRef.IsEmpty())
		{
			Action = ResolveInputActionAsset_ImportBpy(ParsedRefs[MappingIndex].ActionRef);
			if (!Action)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve InputMappingContext action '%s' on %s"),
					*ParsedRefs[MappingIndex].ActionRef,
					*Asset->GetPathName());
				UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
				return false;
			}
		}
		else
		{
			OutError = FString::Printf(
				TEXT("InputMappingContext mapping %d is missing an action on %s"),
				MappingIndex,
				*Asset->GetPathName());
			UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
			return false;
		}

		FKey ParsedKey = EKeys::Invalid;
		if (!ParsedRefs[MappingIndex].KeyName.IsEmpty())
		{
			ParsedKey = ResolveInputKey_ImportBpy(ParsedRefs[MappingIndex].KeyName);
			if (!ParsedKey.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve InputMappingContext key '%s' on %s"),
					*ParsedRefs[MappingIndex].KeyName,
					*Asset->GetPathName());
				UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
				return false;
			}
		}

		FEnhancedActionKeyMapping& Mapping = InputMappingContext->MapKey(Action, ParsedKey);
		Mapping.Modifiers.Reset();
		Mapping.Triggers.Reset();

		for (const FString& ModifierName : ParsedRefs[MappingIndex].ModifierNames)
		{
			UObject* const* FoundObject = InnerObjectMap.Find(ModifierName);
			UInputModifier* Modifier = FoundObject ? Cast<UInputModifier>(*FoundObject) : nullptr;
			if (!Modifier)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve InputMappingContext modifier subobject '%s' on %s"),
					*ModifierName,
					*Asset->GetPathName());
				UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
				return false;
			}
			Mapping.Modifiers.Add(Modifier);
		}

		for (const FString& TriggerName : ParsedRefs[MappingIndex].TriggerNames)
		{
			UObject* const* FoundObject = InnerObjectMap.Find(TriggerName);
			UInputTrigger* Trigger = FoundObject ? Cast<UInputTrigger>(*FoundObject) : nullptr;
			if (!Trigger)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve InputMappingContext trigger subobject '%s' on %s"),
					*TriggerName,
					*Asset->GetPathName());
				UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
				return false;
			}
			Mapping.Triggers.Add(Trigger);
		}
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (FArrayProperty* LegacyMappingsProperty = FindFProperty<FArrayProperty>(UInputMappingContext::StaticClass(), TEXT("Mappings")))
	{
		if (TArray<FEnhancedActionKeyMapping>* LegacyMappings =
			LegacyMappingsProperty->ContainerPtrToValuePtr<TArray<FEnhancedActionKeyMapping>>(InputMappingContext))
		{
			*LegacyMappings = InputMappingContext->GetMappings();
		}
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	return true;
}

bool RestoreInputActionInstancedRefs_ImportBpy(
	UObject* Asset,
	const TSharedPtr<FJsonObject>* StandalonePropertiesObj,
	FString& OutError)
{
	UInputAction* InputAction = Cast<UInputAction>(Asset);
	if (!InputAction || !StandalonePropertiesObj || !(*StandalonePropertiesObj).IsValid())
	{
		return true;
	}

	FString TriggersText;
	FString ModifiersText;
	const bool bHasTriggers = (*StandalonePropertiesObj)->TryGetStringField(TEXT("Triggers"), TriggersText);
	const bool bHasModifiers = (*StandalonePropertiesObj)->TryGetStringField(TEXT("Modifiers"), ModifiersText);
	if (!bHasTriggers && !bHasModifiers)
	{
		return true;
	}

	TArray<UObject*> InnerObjects;
	GetObjectsWithOuter(Asset, InnerObjects, false);

	TMap<FString, UObject*> InnerObjectMap;
	for (UObject* InnerObject : InnerObjects)
	{
		if (InnerObject)
		{
			InnerObjectMap.Add(InnerObject->GetName(), InnerObject);
		}
	}

	if (bHasTriggers)
	{
		InputAction->Triggers.Reset();
		for (const FString& TriggerName : ExtractInstancedObjectNamesFromText_ImportBpy(TriggersText))
		{
			UObject* const* FoundObject = InnerObjectMap.Find(TriggerName);
			UInputTrigger* Trigger = FoundObject ? Cast<UInputTrigger>(*FoundObject) : nullptr;
			if (!Trigger)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve InputAction trigger subobject '%s' on %s"),
					*TriggerName,
					*Asset->GetPathName());
				UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
				return false;
			}

			InputAction->Triggers.Add(Trigger);
		}
	}

	if (bHasModifiers)
	{
		InputAction->Modifiers.Reset();
		for (const FString& ModifierName : ExtractInstancedObjectNamesFromText_ImportBpy(ModifiersText))
		{
			UObject* const* FoundObject = InnerObjectMap.Find(ModifierName);
			UInputModifier* Modifier = FoundObject ? Cast<UInputModifier>(*FoundObject) : nullptr;
			if (!Modifier)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve InputAction modifier subobject '%s' on %s"),
					*ModifierName,
					*Asset->GetPathName());
				UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
				return false;
			}

			InputAction->Modifiers.Add(Modifier);
		}
	}

	return true;
}

bool IsChooserTableAsset_ImportBpy(const UObject* Asset)
{
	return Asset &&
		Asset->GetClass() &&
		Asset->GetClass()->GetPathName().Equals(TEXT("/Script/Chooser.ChooserTable"), ESearchCase::CaseSensitive);
}

UScriptStruct* ResolveChooserAssetChooserStruct_ImportBpy()
{
	static const TCHAR* Candidates[] = {
		TEXT("/Script/Chooser.AssetChooser"),
		TEXT("AssetChooser"),
		TEXT("FAssetChooser"),
	};

	for (const TCHAR* Candidate : Candidates)
	{
		if (UScriptStruct* StructObject = FindObject<UScriptStruct>(nullptr, Candidate))
		{
			return StructObject;
		}
		if (UScriptStruct* StructObject = FindFirstObjectSafe<UScriptStruct>(Candidate))
		{
			return StructObject;
		}
	}

	return nullptr;
}

bool SetChooserAssetReferenceInInstancedStruct_ImportBpy(
	FInstancedStruct& StructValue,
	UObject* ReferencedAsset,
	FString& OutError)
{
	UScriptStruct* AssetChooserStruct = ResolveChooserAssetChooserStruct_ImportBpy();
	if (!AssetChooserStruct)
	{
		OutError = TEXT("Cannot resolve chooser struct: /Script/Chooser.AssetChooser");
		return false;
	}

	StructValue.InitializeAs(AssetChooserStruct);
	void* StructMemory = StructValue.GetMutableMemory();
	if (!StructMemory)
	{
		OutError = TEXT("Failed to allocate chooser instanced struct memory.");
		return false;
	}

	FProperty* AssetProperty = AssetChooserStruct->FindPropertyByName(TEXT("Asset"));
	if (!AssetProperty)
	{
		OutError = TEXT("Chooser asset struct is missing Asset property.");
		return false;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(AssetProperty))
	{
		void* ValuePtr = ObjectProperty->ContainerPtrToValuePtr<void>(StructMemory);
		ObjectProperty->SetObjectPropertyValue(ValuePtr, ReferencedAsset);
		return true;
	}

	if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(AssetProperty))
	{
		if (FSoftObjectPtr* SoftObjectPtr = SoftObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(StructMemory))
		{
			*SoftObjectPtr = ReferencedAsset ? FSoftObjectPtr(ReferencedAsset) : FSoftObjectPtr();
			return true;
		}
	}

	OutError = FString::Printf(
		TEXT("Unsupported chooser asset property type on struct: %s"),
		*AssetChooserStruct->GetPathName());
	return false;
}

bool PopulateChooserResultsFromAssetPaths_ImportBpy(
	UObject* Asset,
	const TArray<FString>& ResultAssetPaths,
	FString& OutError)
{
	if (!Asset)
	{
		OutError = TEXT("PopulateChooserResultsFromAssetPaths called with null asset.");
		return false;
	}

	FArrayProperty* ResultsProperty = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("ResultsStructs"));
	if (!ResultsProperty)
	{
		// In non-editor builds ResultsStructs may not exist. Ignore silently.
		return true;
	}

	FStructProperty* InnerStructProperty = CastField<FStructProperty>(ResultsProperty->Inner);
	if (!InnerStructProperty || InnerStructProperty->Struct != FInstancedStruct::StaticStruct())
	{
		OutError = TEXT("Chooser ResultsStructs has unexpected inner type.");
		return false;
	}

	FScriptArrayHelper ResultsArrayHelper(
		ResultsProperty,
		ResultsProperty->ContainerPtrToValuePtr<void>(Asset));
	ResultsArrayHelper.EmptyValues();

	for (const FString& AssetPath : ResultAssetPaths)
	{
		UObject* ReferencedAsset = ResolveNamedObject_ImportBpy<UObject>(AssetPath);
		if (!ReferencedAsset)
		{
			OutError = FString::Printf(
				TEXT("Failed to resolve chooser result asset: %s"),
				*AssetPath);
			return false;
		}

		const int32 NewIndex = ResultsArrayHelper.AddValue();
		FInstancedStruct* RowStruct = reinterpret_cast<FInstancedStruct*>(ResultsArrayHelper.GetRawPtr(NewIndex));
		if (!RowStruct)
		{
			OutError = TEXT("Failed to allocate chooser ResultsStructs row.");
			return false;
		}

		if (!SetChooserAssetReferenceInInstancedStruct_ImportBpy(*RowStruct, ReferencedAsset, OutError))
		{
			return false;
		}
	}

	if (FArrayProperty* ColumnsProperty = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("ColumnsStructs")))
	{
		FScriptArrayHelper ColumnsArrayHelper(
			ColumnsProperty,
			ColumnsProperty->ContainerPtrToValuePtr<void>(Asset));
		ColumnsArrayHelper.EmptyValues();
	}

	if (FArrayProperty* DisabledRowsProperty = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("DisabledRows")))
	{
		if (FBoolProperty* DisabledRowsInner = CastField<FBoolProperty>(DisabledRowsProperty->Inner))
		{
			FScriptArrayHelper DisabledRowsHelper(
				DisabledRowsProperty,
				DisabledRowsProperty->ContainerPtrToValuePtr<void>(Asset));
			DisabledRowsHelper.EmptyValues();
			DisabledRowsHelper.AddValues(ResultAssetPaths.Num());
			for (int32 Index = 0; Index < ResultAssetPaths.Num(); ++Index)
			{
				DisabledRowsInner->SetPropertyValue(DisabledRowsHelper.GetRawPtr(Index), false);
			}
		}
	}

	return true;
}

bool ImportChooserPropertyTextField_ImportBpy(
	UObject* Asset,
	const TSharedPtr<FJsonObject>& StandaloneMetaJson,
	const TCHAR* JsonFieldName,
	const TCHAR* PropertyName,
	FString& OutError)
{
	if (!Asset || !StandaloneMetaJson.IsValid() || !JsonFieldName || !PropertyName)
	{
		return true;
	}

	if (!StandaloneMetaJson->HasField(JsonFieldName))
	{
		return true;
	}

	FString TextValue;
	if (!StandaloneMetaJson->TryGetStringField(JsonFieldName, TextValue))
	{
		OutError = FString::Printf(TEXT("%s must be a string."), JsonFieldName);
		return false;
	}
	TextValue = RemapBlueprintReferencesInSerializedText_ImportBpy(TextValue);

	FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), PropertyName);
	if (!Property)
	{
		OutError = FString::Printf(
			TEXT("Chooser property %s is missing on %s."),
			PropertyName,
			*Asset->GetPathName());
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Asset);
	if (!ValuePtr)
	{
		OutError = FString::Printf(
			TEXT("Failed to access chooser property %s on %s."),
			PropertyName,
			*Asset->GetPathName());
		return false;
	}

	if (TextValue.IsEmpty())
	{
		Property->ClearValue_InContainer(Asset);
		return true;
	}

	if (!Property->ImportText_Direct(*TextValue, ValuePtr, Asset, PPF_None))
	{
		OutError = FString::Printf(
			TEXT("Failed to import chooser field %s into property %s on %s."),
			JsonFieldName,
			PropertyName,
			*Asset->GetPathName());
		return false;
	}

	return true;
}

bool RestoreChooserTableData_ImportBpy(
	UObject* Asset,
	const TSharedPtr<FJsonObject>& StandaloneMetaJson,
	FString& OutError)
{
	if (!IsChooserTableAsset_ImportBpy(Asset) || !StandaloneMetaJson.IsValid())
	{
		return true;
	}

	const bool bHasResultType = StandaloneMetaJson->HasField(TEXT("chooser_result_type"));
	const bool bHasOutputClass = StandaloneMetaJson->HasField(TEXT("chooser_output_object_type"));
	const bool bHasFallbackAsset = StandaloneMetaJson->HasField(TEXT("chooser_fallback_asset"));
	const bool bHasResultAssets = StandaloneMetaJson->HasField(TEXT("chooser_result_assets"));
	const bool bHasFallbackStructText = StandaloneMetaJson->HasField(TEXT("chooser_fallback_result_text"));
	const bool bHasResultsStructsText = StandaloneMetaJson->HasField(TEXT("chooser_results_structs_text"));
	const bool bHasColumnsStructsText = StandaloneMetaJson->HasField(TEXT("chooser_columns_structs_text"));
	const bool bHasDisabledRowsText = StandaloneMetaJson->HasField(TEXT("chooser_disabled_rows_text"));
	const bool bHasAnyStructuredText =
		bHasFallbackStructText || bHasResultsStructsText || bHasColumnsStructsText || bHasDisabledRowsText;

	if (!bHasResultType &&
		!bHasOutputClass &&
		!bHasFallbackAsset &&
		!bHasResultAssets &&
		!bHasAnyStructuredText)
	{
		return true;
	}

	if (bHasResultType)
	{
		FString ResultTypeText;
		if (!StandaloneMetaJson->TryGetStringField(TEXT("chooser_result_type"), ResultTypeText))
		{
			OutError = TEXT("chooser_result_type must be a string.");
			return false;
		}

		if (FProperty* ResultTypeProperty = FindFProperty<FProperty>(Asset->GetClass(), TEXT("ResultType")))
		{
			void* ValuePtr = ResultTypeProperty->ContainerPtrToValuePtr<void>(Asset);
			if (!ResultTypeProperty->ImportText_Direct(*ResultTypeText, ValuePtr, Asset, PPF_None))
			{
				OutError = FString::Printf(
					TEXT("Failed to import chooser_result_type '%s' on %s"),
					*ResultTypeText,
					*Asset->GetPathName());
				return false;
			}
		}
	}

	if (bHasOutputClass)
	{
		FString OutputClassPath;
		if (!StandaloneMetaJson->TryGetStringField(TEXT("chooser_output_object_type"), OutputClassPath))
		{
			OutError = TEXT("chooser_output_object_type must be a string.");
			return false;
		}
		OutputClassPath = RemapBlueprintReferenceForCurrentImport_ImportBpy(OutputClassPath, true);

		UClass* OutputClass = nullptr;
		if (!OutputClassPath.IsEmpty())
		{
			OutputClass = ResolveNamedObject_ImportBpy<UClass>(OutputClassPath);
			if (!OutputClass)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve chooser_output_object_type class: %s"),
					*OutputClassPath);
				return false;
			}
		}

		if (FClassProperty* OutputClassProperty = FindFProperty<FClassProperty>(Asset->GetClass(), TEXT("OutputObjectType")))
		{
			OutputClassProperty->SetPropertyValue_InContainer(Asset, OutputClass);
		}
	}

	if (!bHasAnyStructuredText && bHasFallbackAsset)
	{
		FString FallbackAssetPath;
		if (!StandaloneMetaJson->TryGetStringField(TEXT("chooser_fallback_asset"), FallbackAssetPath))
		{
			OutError = TEXT("chooser_fallback_asset must be a string.");
			return false;
		}
		FallbackAssetPath = RemapBlueprintReferenceForCurrentImport_ImportBpy(FallbackAssetPath, false);

		FStructProperty* FallbackProperty = FindFProperty<FStructProperty>(Asset->GetClass(), TEXT("FallbackResult"));
		if (!FallbackProperty || FallbackProperty->Struct != FInstancedStruct::StaticStruct())
		{
			OutError = TEXT("Chooser FallbackResult property is missing or has unexpected type.");
			return false;
		}

		FInstancedStruct* FallbackStruct = FallbackProperty->ContainerPtrToValuePtr<FInstancedStruct>(Asset);
		if (!FallbackStruct)
		{
			OutError = TEXT("Failed to access chooser FallbackResult.");
			return false;
		}

		if (FallbackAssetPath.IsEmpty())
		{
			FallbackStruct->Reset();
		}
		else
		{
			UObject* FallbackAsset = ResolveNamedObject_ImportBpy<UObject>(FallbackAssetPath);
			if (!FallbackAsset)
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve chooser_fallback_asset: %s"),
					*FallbackAssetPath);
				return false;
			}

			if (!SetChooserAssetReferenceInInstancedStruct_ImportBpy(*FallbackStruct, FallbackAsset, OutError))
			{
				return false;
			}
		}
	}

	if (bHasAnyStructuredText)
	{
		if (!ImportChooserPropertyTextField_ImportBpy(
				Asset,
				StandaloneMetaJson,
				TEXT("chooser_fallback_result_text"),
				TEXT("FallbackResult"),
				OutError))
		{
			return false;
		}
		if (!ImportChooserPropertyTextField_ImportBpy(
				Asset,
				StandaloneMetaJson,
				TEXT("chooser_results_structs_text"),
				TEXT("ResultsStructs"),
				OutError))
		{
			return false;
		}
		if (!ImportChooserPropertyTextField_ImportBpy(
				Asset,
				StandaloneMetaJson,
				TEXT("chooser_columns_structs_text"),
				TEXT("ColumnsStructs"),
				OutError))
		{
			return false;
		}
		if (!ImportChooserPropertyTextField_ImportBpy(
				Asset,
				StandaloneMetaJson,
				TEXT("chooser_disabled_rows_text"),
				TEXT("DisabledRows"),
				OutError))
		{
			return false;
		}

		return true;
	}

	if (!bHasAnyStructuredText && bHasResultAssets)
	{
		const TArray<TSharedPtr<FJsonValue>>* ResultAssetValues = nullptr;
		if (!StandaloneMetaJson->TryGetArrayField(TEXT("chooser_result_assets"), ResultAssetValues) || !ResultAssetValues)
		{
			OutError = TEXT("chooser_result_assets must be an array.");
			return false;
		}

		TArray<FString> ResultAssetPaths;
		ResultAssetPaths.Reserve(ResultAssetValues->Num());
		for (const TSharedPtr<FJsonValue>& Value : *ResultAssetValues)
		{
			if (!Value.IsValid() || Value->Type != EJson::String)
			{
				OutError = TEXT("chooser_result_assets entries must be strings.");
				return false;
			}

			const FString AssetPath = Value->AsString().TrimStartAndEnd();
			if (!AssetPath.IsEmpty())
			{
				ResultAssetPaths.Add(RemapBlueprintReferenceForCurrentImport_ImportBpy(AssetPath, false));
			}
		}

		if (!PopulateChooserResultsFromAssetPaths_ImportBpy(Asset, ResultAssetPaths, OutError))
		{
			return false;
		}
	}

	return true;
}

FString StripGuidSuffix_ImportBpy(const FString& RawName)
{
	FString Result = RawName;
	int32 UnderscoreIndex = INDEX_NONE;
	while (Result.FindLastChar(TEXT('_'), UnderscoreIndex))
	{
		const FString Tail = Result.Mid(UnderscoreIndex + 1);
		if (Tail.Len() < 8)
		{
			break;
		}

		bool bHexish = true;
		int32 HexCount = 0;
		for (TCHAR Ch : Tail)
		{
			if (FChar::IsHexDigit(Ch))
			{
				++HexCount;
				continue;
			}
			if (Ch != TEXT('-'))
			{
				bHexish = false;
				break;
			}
		}

		if (!bHexish || HexCount < 8)
		{
			break;
		}

		Result = Result.Left(UnderscoreIndex);
	}

	while (Result.FindLastChar(TEXT('_'), UnderscoreIndex))
	{
		const FString Tail = Result.Mid(UnderscoreIndex + 1);
		if (Tail.IsEmpty())
		{
			break;
		}

		bool bNumeric = true;
		for (TCHAR Ch : Tail)
		{
			if (!FChar::IsDigit(Ch))
			{
				bNumeric = false;
				break;
			}
		}

		if (!bNumeric)
		{
			break;
		}

		Result = Result.Left(UnderscoreIndex);
	}

	return Result;
}

TArray<FString> GetComponentNameCandidates_ImportBpy(const FString& RawName)
{
	TArray<FString> Candidates;
	auto AddCandidate = [&Candidates](const FString& Candidate)
	{
		if (!Candidate.IsEmpty())
		{
			Candidates.AddUnique(Candidate);
		}
	};

	const FString StrippedName = StripGuidSuffix_ImportBpy(RawName);
	AddCandidate(RawName);
	AddCandidate(StrippedName);

	auto AddLegacyAliases = [&AddCandidate](const FString& Candidate)
	{
		if (Candidate.Equals(TEXT("CollisionCylinder"), ESearchCase::IgnoreCase))
		{
			AddCandidate(TEXT("CapsuleComponent"));
		}
		else if (Candidate.Equals(TEXT("CapsuleComponent"), ESearchCase::IgnoreCase))
		{
			AddCandidate(TEXT("CollisionCylinder"));
		}
		else if (Candidate.Equals(TEXT("CharMoveComp"), ESearchCase::IgnoreCase))
		{
			AddCandidate(TEXT("CharacterMovement"));
		}
		else if (Candidate.Equals(TEXT("CharacterMovement"), ESearchCase::IgnoreCase))
		{
			AddCandidate(TEXT("CharMoveComp"));
		}
		else if (Candidate.Equals(TEXT("CharacterMesh0"), ESearchCase::IgnoreCase) ||
			Candidate.Equals(TEXT("CharacterMesh"), ESearchCase::IgnoreCase))
		{
			AddCandidate(TEXT("Mesh"));
		}
		else if (Candidate.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase))
		{
			AddCandidate(TEXT("CharacterMesh0"));
			AddCandidate(TEXT("CharacterMesh"));
		}
	};

	AddLegacyAliases(RawName);
	if (!StrippedName.Equals(RawName, ESearchCase::CaseSensitive))
	{
		AddLegacyAliases(StrippedName);
	}

	return Candidates;
}

bool ComponentNameMatches_ImportBpy(const FString& RequestedName, const FString& ActualName)
{
	if (RequestedName.IsEmpty() || ActualName.IsEmpty())
	{
		return false;
	}

	const FString ActualStripped = StripGuidSuffix_ImportBpy(ActualName);
	for (const FString& Candidate : GetComponentNameCandidates_ImportBpy(RequestedName))
	{
		if (Candidate.Equals(ActualName, ESearchCase::IgnoreCase) ||
			Candidate.Equals(ActualStripped, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

UActorComponent* FindInheritedComponentByName_ImportBpy(UBlueprint* BP, const FString& ComponentName)
{
	if (!BP || ComponentName.IsEmpty())
	{
		return nullptr;
	}

	auto FindOnActor = [&ComponentName](AActor* Actor) -> UActorComponent*
	{
		if (!Actor)
		{
			return nullptr;
		}

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			if (ComponentNameMatches_ImportBpy(ComponentName, Component->GetFName().ToString()) ||
				ComponentNameMatches_ImportBpy(ComponentName, Component->GetName()))
			{
				return Component;
			}
		}

		return nullptr;
	};

	if (BP->GeneratedClass)
	{
		if (AActor* GeneratedCDO = Cast<AActor>(BP->GeneratedClass->GetDefaultObject(false)))
		{
			if (UActorComponent* Found = FindOnActor(GeneratedCDO))
			{
				return Found;
			}
		}
	}

	if (BP->ParentClass)
	{
		if (AActor* ParentCDO = Cast<AActor>(BP->ParentClass->GetDefaultObject(false)))
		{
			if (UActorComponent* Found = FindOnActor(ParentCDO))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

USceneComponent* FindInheritedSceneComponentByName_ImportBpy(UBlueprint* BP, const FString& ComponentName)
{
	return Cast<USceneComponent>(FindInheritedComponentByName_ImportBpy(BP, ComponentName));
}

UActorComponent* FindParentInheritedComponentByName_ImportBpy(UBlueprint* BP, const FString& ComponentName)
{
	if (!BP || ComponentName.IsEmpty())
	{
		return nullptr;
	}

	auto FindOnActor = [&ComponentName](AActor* Actor) -> UActorComponent*
	{
		if (!Actor)
		{
			return nullptr;
		}

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			if (ComponentNameMatches_ImportBpy(ComponentName, Component->GetFName().ToString()) ||
				ComponentNameMatches_ImportBpy(ComponentName, Component->GetName()))
			{
				return Component;
			}
		}

		return nullptr;
	};

	for (UClass* Class = BP->ParentClass; Class; Class = Class->GetSuperClass())
	{
		if (AActor* ParentCDO = Cast<AActor>(Class->GetDefaultObject(false)))
		{
			if (UActorComponent* Found = FindOnActor(ParentCDO))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

void NormalizeInheritedSceneMobility_ImportBpy(
	UBlueprint* BP,
	const FString& ComponentName,
	UActorComponent* TargetComp,
	const TSharedPtr<FJsonObject>& PropsObj)
{
	if (!BP || !TargetComp || !PropsObj.IsValid() || PropsObj->HasField(TEXT("Mobility")))
	{
		return;
	}

	USceneComponent* TargetScene = Cast<USceneComponent>(TargetComp);
	if (!TargetScene)
	{
		return;
	}

	EComponentMobility::Type DesiredMobility = EComponentMobility::Movable;
	if (const USceneComponent* ParentScene = Cast<USceneComponent>(FindParentInheritedComponentByName_ImportBpy(BP, ComponentName)))
	{
		DesiredMobility = ParentScene->GetMobility();
	}

	if (TargetScene->GetMobility() != DesiredMobility)
	{
		TargetScene->Modify();
		TargetScene->SetMobility(DesiredMobility);
	}
}

USceneComponent* ResolveParentSceneTemplate_ImportBpy(
	UBlueprint* BP,
	const FString& ParentName,
	const TMap<FString, USCS_Node*>& KnownNodes)
{
	if (ParentName.IsEmpty())
	{
		return nullptr;
	}

	auto ResolveTemplateFromNode = [](USCS_Node* Node) -> USceneComponent*
	{
		return Node ? Cast<USceneComponent>(Node->ComponentTemplate) : nullptr;
	};

	if (USCS_Node* const* ParentNodePtr = KnownNodes.Find(ParentName))
	{
		if (USceneComponent* Template = ResolveTemplateFromNode(*ParentNodePtr))
		{
			return Template;
		}
	}

	for (const TPair<FString, USCS_Node*>& Entry : KnownNodes)
	{
		if (ComponentNameMatches_ImportBpy(ParentName, Entry.Key))
		{
			if (USceneComponent* Template = ResolveTemplateFromNode(Entry.Value))
			{
				return Template;
			}
		}
	}

	if (USCS_Node* ParentNode = FindComponentNodeByName_ImportBpy(BP, ParentName))
	{
		if (USceneComponent* Template = ResolveTemplateFromNode(ParentNode))
		{
			return Template;
		}
	}

	if (USceneComponent* InheritedParent = FindInheritedSceneComponentByName_ImportBpy(BP, ParentName))
	{
		return InheritedParent;
	}

	return ResolveNamedObject_ImportBpy<USceneComponent>(ParentName);
}

bool SyncSceneTemplateAttachment_ImportBpy(
	USCS_Node* Node,
	USceneComponent* ParentSceneTemplate,
	const FName AttachSocketName)
{
	if (!Node)
	{
		return false;
	}

	USceneComponent* SceneTemplate = Cast<USceneComponent>(Node->ComponentTemplate);
	if (!SceneTemplate)
	{
		return false;
	}

	const USceneComponent* CurrentParent = SceneTemplate->GetAttachParent();
	const FName CurrentSocket = SceneTemplate->GetAttachSocketName();
	if (CurrentParent == ParentSceneTemplate && CurrentSocket == AttachSocketName)
	{
		return false;
	}

	SceneTemplate->Modify();
	SceneTemplate->SetupAttachment(ParentSceneTemplate, AttachSocketName);
	return true;
}

bool CanResolveComponentParent_ImportBpy(
	UBlueprint* BP,
	const FString& ParentName,
	const TMap<FString, USCS_Node*>& KnownNodes)
{
	if (ParentName.IsEmpty())
	{
		return true;
	}

	for (const TPair<FString, USCS_Node*>& Entry : KnownNodes)
	{
		if (ComponentNameMatches_ImportBpy(ParentName, Entry.Key))
		{
			return true;
		}
	}

	if (FindComponentNodeByName_ImportBpy(BP, ParentName))
	{
		return true;
	}

	if (FindInheritedSceneComponentByName_ImportBpy(BP, ParentName))
	{
		return true;
	}

	return ResolveNamedObject_ImportBpy<USceneComponent>(ParentName) != nullptr;
}

FProperty* FindPropertyByNameOrAlias_ImportBpy(UObject* Object, const FString& PropertyName)
{
	if (!Object || PropertyName.IsEmpty())
	{
		return nullptr;
	}

	if (FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName)))
	{
		return Property;
	}

	TArray<FString> Aliases;
	if (PropertyName.Equals(TEXT("SkeletalMesh"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("SkinnedAsset"), ESearchCase::IgnoreCase))
	{
		Aliases.Add(TEXT("SkeletalMeshAsset"));
	}

	for (const FString& Alias : Aliases)
	{
		if (FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Alias)))
		{
			return Property;
		}
	}

	return nullptr;
}

bool TryParseGuid_ImportBpy(const FString& GuidText, FGuid& OutGuid)
{
	OutGuid.Invalidate();
	if (GuidText.IsEmpty())
	{
		return false;
	}
	return FGuid::Parse(GuidText, OutGuid);
}

FString GetNodePropString_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson, const TCHAR* Key)
{
	if (!NodeJson.IsValid())
	{
		return FString();
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj->IsValid())
	{
		return FString();
	}

	FString Value;
	(*NodePropsObj)->TryGetStringField(Key, Value);
	return Value;
}

bool IsAnimNodeFunctionRefFieldName_ImportBpy(const FString& PropertyName)
{
	return PropertyName.Equals(TEXT("InitialUpdateFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("BecomeRelevantFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("UpdateFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("OnMotionMatchingStateUpdatedFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("StateEntryFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("StateFullyBlendedInFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("StateExitFunction"), ESearchCase::CaseSensitive) ||
		PropertyName.Equals(TEXT("StateFullyBlendedOutFunction"), ESearchCase::CaseSensitive);
}

bool HasEvaluateChooserMetadata_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutMissingProperties)
{
	OutMissingProperties.Reset();

	if (!NodeJson.IsValid())
	{
		OutMissingProperties = TEXT("node_json");
		return false;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
	{
		OutMissingProperties = TEXT("node_props");
		return false;
	}

	auto HasNonEmptyStringProperty = [NodePropsObj](const TCHAR* PropertyName) -> bool
	{
		if (!PropertyName)
		{
			return false;
		}

		FString PropertyValue;
		return (*NodePropsObj)->TryGetStringField(PropertyName, PropertyValue) && !PropertyValue.IsEmpty();
	};

	TArray<FString> MissingProperties;
	if (!HasNonEmptyStringProperty(TEXT("Chooser")))
	{
		MissingProperties.Add(TEXT("Chooser"));
	}
	if (!HasNonEmptyStringProperty(TEXT("Mode")))
	{
		MissingProperties.Add(TEXT("Mode"));
	}

	if (MissingProperties.Num() > 0)
	{
		OutMissingProperties = FString::Join(MissingProperties, TEXT(", "));
		return false;
	}

	return true;
}

bool HasEvaluateChooserAssetBound_ImportBpy(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}

	const FString NodeClassName = Node->GetClass() ? Node->GetClass()->GetName() : FString();
	if (NodeClassName != TEXT("K2Node_EvaluateChooser") && NodeClassName != TEXT("K2Node_EvaluateChooser2"))
	{
		return false;
	}

	const FObjectPropertyBase* ChooserProperty =
		FindFProperty<FObjectPropertyBase>(Node->GetClass(), FName(TEXT("Chooser")));
	if (!ChooserProperty)
	{
		return false;
	}

	return ChooserProperty->GetObjectPropertyValue_InContainer(Node) != nullptr;
}

bool IsEvaluateChooserNodeClass_ImportBpy(const FString& NodeClassName)
{
	return NodeClassName == TEXT("K2Node_EvaluateChooser") || NodeClassName == TEXT("K2Node_EvaluateChooser2");
}

FObjectPropertyBase* FindEvaluateChooserProperty_ImportBpy(const UEdGraphNode* Node)
{
	if (!Node || !Node->GetClass())
	{
		return nullptr;
	}

	const FString NodeClassName = Node->GetClass()->GetName();
	if (!IsEvaluateChooserNodeClass_ImportBpy(NodeClassName))
	{
		return nullptr;
	}

	return FindFProperty<FObjectPropertyBase>(Node->GetClass(), FName(TEXT("Chooser")));
}

void GatherObjectsInSamePackage_ImportBpy(UObject* Object, TArray<UObject*>& OutObjects)
{
	OutObjects.Reset();
	if (!Object)
	{
		return;
	}

	if (UPackage* Package = Object->GetOutermost())
	{
		GetObjectsWithPackage(
			Package,
			OutObjects,
			true,
			RF_Transient,
			EInternalObjectFlags::Garbage);
	}

	if (OutObjects.Num() == 0)
	{
		OutObjects.Add(Object);
	}
}

bool ObjectDirectSerializedPropertyTextContains_ImportBpy(UObject* Object, const FString& Needle)
{
	if (!Object || Needle.IsEmpty())
	{
		return false;
	}

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (!ValuePtr)
		{
			continue;
		}

		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, Object, PPF_None);
		if (!ExportedValue.IsEmpty() &&
			ExportedValue.Contains(Needle, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}

	return false;
}

bool ObjectSerializedPropertyTextContains_ImportBpy(UObject* Object, const FString& Needle)
{
	if (!Object || Needle.IsEmpty())
	{
		return false;
	}

	TArray<UObject*> PackageObjects;
	GatherObjectsInSamePackage_ImportBpy(Object, PackageObjects);
	for (UObject* PackageObject : PackageObjects)
	{
		if (ObjectDirectSerializedPropertyTextContains_ImportBpy(PackageObject, Needle))
		{
			return true;
		}
	}

	return false;
}

bool RemapSingleObjectSerializedPropertyTextInPlace_ImportBpy(
	UObject* Object,
	const FString& SourceText,
	const FString& TargetText,
	bool& bOutChanged,
	FString& OutError)
{
	bOutChanged = false;
	if (!Object ||
		SourceText.IsEmpty() ||
		TargetText.IsEmpty() ||
		SourceText.Equals(TargetText, ESearchCase::CaseSensitive))
	{
		return true;
	}

	Object->Modify();

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (!ValuePtr)
		{
			continue;
		}

		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, Object, PPF_None);
		if (ExportedValue.IsEmpty() ||
			!ExportedValue.Contains(SourceText, ESearchCase::CaseSensitive))
		{
			continue;
		}

		FString RemappedValue = ExportedValue;
		RemappedValue.ReplaceInline(*SourceText, *TargetText, ESearchCase::CaseSensitive);
		if (RemappedValue.Equals(ExportedValue, ESearchCase::CaseSensitive))
		{
			continue;
		}

		if (!Property->ImportText_Direct(*RemappedValue, ValuePtr, Object, PPF_None))
		{
			OutError = FString::Printf(
				TEXT("Failed to remap chooser property '%s' on %s"),
				*Property->GetName(),
				*Object->GetPathName());
			return false;
		}

		bOutChanged = true;
	}

	return true;
}

bool RemapObjectSerializedPropertyTextInPlace_ImportBpy(
	UObject* Object,
	const FString& SourceText,
	const FString& TargetText,
	bool& bOutChanged,
	FString& OutError)
{
	bOutChanged = false;
	if (!Object ||
		SourceText.IsEmpty() ||
		TargetText.IsEmpty() ||
		SourceText.Equals(TargetText, ESearchCase::CaseSensitive))
	{
		return true;
	}

	TArray<UObject*> PackageObjects;
	GatherObjectsInSamePackage_ImportBpy(Object, PackageObjects);
	for (UObject* PackageObject : PackageObjects)
	{
		bool bObjectChanged = false;
		if (!RemapSingleObjectSerializedPropertyTextInPlace_ImportBpy(
				PackageObject,
				SourceText,
				TargetText,
				bObjectChanged,
				OutError))
		{
			return false;
		}

		bOutChanged = bOutChanged || bObjectChanged;
	}

	return true;
}

bool RetargetChooserContextClassesInPackage_ImportBpy(
	UObject* RootObject,
	const FString& SourceGeneratedClassPath,
	UClass* TargetGeneratedClass,
	bool& bOutChanged,
	FString& OutError)
{
	bOutChanged = false;
	if (!RootObject || SourceGeneratedClassPath.IsEmpty() || !TargetGeneratedClass)
	{
		return true;
	}

	TArray<UObject*> PackageObjects;
	GatherObjectsInSamePackage_ImportBpy(RootObject, PackageObjects);

	for (UObject* PackageObject : PackageObjects)
	{
		UChooserSignature* ChooserSignature = Cast<UChooserSignature>(PackageObject);
		if (!ChooserSignature)
		{
			continue;
		}

		bool bSignatureChanged = false;
		for (FInstancedStruct& ContextDataEntry : ChooserSignature->ContextData)
		{
			FContextObjectTypeClass* const ClassContext =
				ContextDataEntry.GetMutablePtr<FContextObjectTypeClass>();
			if (!ClassContext || !ClassContext->Class)
			{
				continue;
			}

			if (!ClassContext->Class->GetPathName().Equals(SourceGeneratedClassPath, ESearchCase::CaseSensitive))
			{
				continue;
			}

			if (!bSignatureChanged)
			{
				ChooserSignature->Modify();
				bSignatureChanged = true;
			}

			ClassContext->Class = TargetGeneratedClass;
		}

		if (bSignatureChanged)
		{
			if (UChooserTable* ChooserTable = Cast<UChooserTable>(ChooserSignature))
			{
				ChooserTable->Compile(true);
				ChooserTable->PostEditChange();
			}

			bOutChanged = true;
		}
	}

	if (!bOutChanged)
	{
		OutError = FString();
	}

	return true;
}

FString BuildRetargetedChooserAssetPathForBlueprint_ImportBpy(
	const FString& SourceChooserAssetPath,
	const FString& TargetBlueprintPath)
{
	const FString TargetBlueprintObjectPath = NormalizeBlueprintObjectPath_ImportBpy(TargetBlueprintPath);
	const FString TargetBlueprintPackagePath =
		FPackageName::ObjectPathToPackageName(TargetBlueprintObjectPath);
	const FString TargetDirectory = FPackageName::GetLongPackagePath(TargetBlueprintPackagePath);
	const FString TargetBlueprintAssetName =
		FPackageName::ObjectPathToObjectName(TargetBlueprintObjectPath);
	const FString SourceChooserAssetName =
		FPackageName::GetLongPackageAssetName(SourceChooserAssetPath);

	if (TargetDirectory.IsEmpty() || TargetBlueprintAssetName.IsEmpty() || SourceChooserAssetName.IsEmpty())
	{
		return FString();
	}

	return FString::Printf(
		TEXT("%s/%s_For_%s"),
		*TargetDirectory,
		*SourceChooserAssetName,
		*TargetBlueprintAssetName);
}


void RepairEvaluateChooserSourceClassPinsForCurrentImport_ImportBpy(UBlueprint* BP)
{
	if (!BP || GCurrentImportSourceBlueprintPath_ImportBpy.IsEmpty() || GCurrentImportTargetBlueprintPath_ImportBpy.IsEmpty())
	{
		return;
	}

	const FString SourceClassPath = BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString SourceClassName = BuildGeneratedClassObjectNameFromBlueprintPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	if (SourceClassPath.IsEmpty() || SourceClassName.IsEmpty())
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);
	TSet<UEdGraph*> VisitedGraphs;
	TArray<UEdGraph*> AllGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, AllGraphs);
	}

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsEvaluateChooserNode_ImportBpy(Node))
			{
				continue;
			}

			UEdGraphPin* TargetPin = FindEvaluateChooserContextPinForCurrentImport_ImportBpy(Node, SourceClassName, EGPD_Input);
			for (int32 PinIndex = Node->Pins.Num() - 1; PinIndex >= 0; --PinIndex)
			{
				UEdGraphPin* Pin = Node->Pins[PinIndex];
				if (!Pin || Pin->Direction != EGPD_Input)
				{
					continue;
				}

				const UClass* PinClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
				const FString PinNameNoGuid = StripGuidSuffix_ImportBpy(Pin->PinName.ToString());
				const bool bIsSourceClassPin =
					(PinClass && PinClass->GetPathName().Equals(SourceClassPath, ESearchCase::CaseSensitive)) ||
					PinNameNoGuid.Equals(SourceClassName, ESearchCase::IgnoreCase) ||
					PinNameNoGuid.Equals(FString(TEXT("S_")) + SourceClassName, ESearchCase::IgnoreCase);
				if (!bIsSourceClassPin)
				{
					continue;
				}

				if (TargetPin && TargetPin != Pin)
				{
					TArray<UEdGraphPin*> LinkedPins = Pin->LinkedTo;
					for (UEdGraphPin* LinkedPin : LinkedPins)
					{
						if (LinkedPin)
						{
							Pin->BreakLinkTo(LinkedPin);
							LinkedPin->MakeLinkTo(TargetPin);
						}
					}
				}

				Pin->BreakAllPinLinks();
				if (Pin->bOrphanedPin || Pin->LinkedTo.Num() == 0)
				{
					Node->RemovePin(Pin);
				}
			}

			Node->NodeConnectionListChanged();
			Graph->NotifyGraphChanged();
		}
	}
}
bool RetargetEvaluateChooserTablesForCurrentBlueprint_ImportBpy(
	UBlueprint* BP,
	bool& bOutAnyRetargeted,
	FString& OutError)
{
	bOutAnyRetargeted = false;

	if (!BP ||
		GCurrentImportSourceBlueprintPath_ImportBpy.IsEmpty() ||
		GCurrentImportTargetBlueprintPath_ImportBpy.IsEmpty())
	{
		return true;
	}

	const FString SourceGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString TargetGeneratedClassPath =
		BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportTargetBlueprintPath_ImportBpy);
	UClass* const TargetGeneratedClass =
		ResolveNamedObject_ImportBpy<UClass>(TargetGeneratedClassPath);

	if (SourceGeneratedClassPath.IsEmpty() ||
		TargetGeneratedClassPath.IsEmpty() ||
		SourceGeneratedClassPath.Equals(TargetGeneratedClassPath, ESearchCase::CaseSensitive))
	{
		return true;
	}
	if (!TargetGeneratedClass)
	{
		OutError = FString::Printf(
			TEXT("Failed to resolve target generated class for chooser retarget: %s"),
			*TargetGeneratedClassPath);
		return false;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> AllGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, AllGraphs);
	}

	TMap<FString, UObject*> SourceChooserAssetToRetargetedAsset;
	bool bUpdatedAnyNode = false;
	bool bCreatedOrUpdatedAnyChooserAsset = false;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			FObjectPropertyBase* ChooserProperty = FindEvaluateChooserProperty_ImportBpy(Node);
			if (!ChooserProperty)
			{
				continue;
			}

			UObject* ChooserAsset = ChooserProperty->GetObjectPropertyValue_InContainer(Node);
			if (!IsChooserTableAsset_ImportBpy(ChooserAsset))
			{
				continue;
			}

			const FString SourceChooserAssetPath =
				FPackageName::ObjectPathToPackageName(ChooserAsset->GetPathName());
			if (SourceChooserAssetPath.IsEmpty())
			{
				continue;
			}

			UObject* RetargetedChooserAsset = nullptr;
			if (UObject** ExistingAsset = SourceChooserAssetToRetargetedAsset.Find(SourceChooserAssetPath))
			{
				RetargetedChooserAsset = *ExistingAsset;
			}
			else
			{
				const FString RetargetedChooserAssetPath =
					BuildRetargetedChooserAssetPathForBlueprint_ImportBpy(
						SourceChooserAssetPath,
						GCurrentImportTargetBlueprintPath_ImportBpy);
				if (RetargetedChooserAssetPath.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Failed to build retargeted chooser asset path for %s"),
						*SourceChooserAssetPath);
					return false;
				}

				if (!UEditorAssetLibrary::DoesAssetExist(RetargetedChooserAssetPath))
				{
					if (!UEditorAssetLibrary::DuplicateAsset(SourceChooserAssetPath, RetargetedChooserAssetPath))
					{
						OutError = FString::Printf(
							TEXT("Failed to duplicate chooser table %s -> %s"),
							*SourceChooserAssetPath,
							*RetargetedChooserAssetPath);
						return false;
					}
				}

				RetargetedChooserAsset = UEditorAssetLibrary::LoadAsset(RetargetedChooserAssetPath);
				if (!RetargetedChooserAsset)
				{
					OutError = FString::Printf(
						TEXT("Failed to load retargeted chooser table: %s"),
						*RetargetedChooserAssetPath);
					return false;
				}

				if (UPackage* RetargetedPackage = RetargetedChooserAsset->GetOutermost())
				{
					FMetaData& MetaData = RetargetedPackage->GetMetaData();
					MetaData.SetValue(
						RetargetedChooserAsset,
						TEXT("ExportBpy.SourceChooserAssetPath"),
						*SourceChooserAssetPath);
				}
				bool bChooserAssetTextChanged = false;
				if (!RemapObjectSerializedPropertyTextInPlace_ImportBpy(
						RetargetedChooserAsset,
						SourceGeneratedClassPath,
						TargetGeneratedClassPath,
						bChooserAssetTextChanged,
						OutError))
				{
					return false;
				}

				bool bChooserContextChanged = false;
				if (!RetargetChooserContextClassesInPackage_ImportBpy(
						RetargetedChooserAsset,
						SourceGeneratedClassPath,
						TargetGeneratedClass,
						bChooserContextChanged,
						OutError))
				{
					return false;
				}

				if (bChooserAssetTextChanged || bChooserContextChanged)
				{
					RetargetedChooserAsset->MarkPackageDirty();
					UEditorAssetLibrary::SaveAsset(RetargetedChooserAssetPath, false);
					bCreatedOrUpdatedAnyChooserAsset = true;
				}

				SourceChooserAssetToRetargetedAsset.Add(SourceChooserAssetPath, RetargetedChooserAsset);
			}

			if (RetargetedChooserAsset && RetargetedChooserAsset != ChooserAsset)
			{
				Node->Modify();
				Node->PreEditChange(ChooserProperty);
				ChooserProperty->SetObjectPropertyValue_InContainer(Node, RetargetedChooserAsset);
				FPropertyChangedEvent ChangeEvent(ChooserProperty, EPropertyChangeType::ValueSet);
				Node->PostEditChangeProperty(ChangeEvent);
				Node->ReconstructNode();
				bUpdatedAnyNode = true;
			}
		}
	}

	if (bUpdatedAnyNode || bCreatedOrUpdatedAnyChooserAsset)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		BP->MarkPackageDirty();
		bOutAnyRetargeted = true;
	}

	return true;
}

bool NodeJsonContainsPinName_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	const TCHAR* FieldName,
	const TCHAR* PinName)
{
	if (!NodeJson.IsValid() || !FieldName || !PinName)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PinsObj = nullptr;
	if (!NodeJson->TryGetObjectField(FieldName, PinsObj) || !PinsObj || !PinsObj->IsValid())
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*PinsObj)->Values)
	{
		if (Entry.Key.Equals(PinName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool NodeJsonRequestsExecPinsForCallNode_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	return
		NodeJsonContainsPinName_ImportBpy(NodeJson, TEXT("pin_ids"), TEXT("execute")) ||
		NodeJsonContainsPinName_ImportBpy(NodeJson, TEXT("pin_ids"), TEXT("then")) ||
		NodeJsonContainsPinName_ImportBpy(NodeJson, TEXT("defaults"), TEXT("execute")) ||
		NodeJsonContainsPinName_ImportBpy(NodeJson, TEXT("defaults"), TEXT("then"));
}

void EnsureCallFunctionExecPins_ImportBpy(UK2Node_CallFunction* CallNode)
{
	if (!CallNode)
	{
		return;
	}

	if (!CallNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input))
	{
		CallNode->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	}

	if (!CallNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output))
	{
		CallNode->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	}
}

FString NormalizeLooseFunctionNameLocal_ImportBpy(FString Name)
{
	Name.TrimStartAndEndInline();
	Name.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
	Name.ReplaceInline(TEXT("_"), TEXT(""), ESearchCase::CaseSensitive);
	Name.ToLowerInline();
	return Name;
}

UK2Node_CustomEvent* FindMatchingCustomEventNodeForCall_ImportBpy(UK2Node_CallFunction* CallNode)
{
	if (!CallNode || !CallNode->GetGraph())
	{
		return nullptr;
	}

	const FString RequestedLoose =
		NormalizeLooseFunctionNameLocal_ImportBpy(CallNode->FunctionReference.GetMemberName().ToString());
	if (RequestedLoose.IsEmpty())
	{
		return nullptr;
	}

	for (UEdGraphNode* GraphNode : CallNode->GetGraph()->Nodes)
	{
		UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(GraphNode);
		if (!CustomEventNode)
		{
			continue;
		}

		const FString CustomEventName = CustomEventNode->CustomFunctionName.ToString();
		if (NormalizeLooseFunctionNameLocal_ImportBpy(CustomEventName) == RequestedLoose)
		{
			return CustomEventNode;
		}

		const FString CustomEventDisplay = FName::NameToDisplayString(CustomEventName, false);
		if (NormalizeLooseFunctionNameLocal_ImportBpy(CustomEventDisplay) == RequestedLoose)
		{
			return CustomEventNode;
		}
	}

	return nullptr;
}

void EnsureCallFunctionPinsFromMatchingCustomEvent_ImportBpy(UK2Node_CallFunction* CallNode)
{
	if (!CallNode)
	{
		return;
	}

	EnsureCallFunctionExecPins_ImportBpy(CallNode);

	UK2Node_CustomEvent* CustomEventNode = FindMatchingCustomEventNodeForCall_ImportBpy(CallNode);
	if (!CustomEventNode)
	{
		return;
	}

	for (UEdGraphPin* EventPin : CustomEventNode->Pins)
	{
		if (!EventPin || EventPin->Direction != EGPD_Output)
		{
			continue;
		}

		if (EventPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			continue;
		}

		if (CallNode->FindPin(EventPin->PinName, EGPD_Input))
		{
			continue;
		}

		UEdGraphPin* NewPin = CallNode->CreatePin(EGPD_Input, EventPin->PinType, EventPin->PinName);
		if (!NewPin)
		{
			continue;
		}

		NewPin->DefaultValue = EventPin->DefaultValue;
		NewPin->DefaultTextValue = EventPin->DefaultTextValue;
		NewPin->DefaultObject = EventPin->DefaultObject;
	}
}

FString GetGraphObjectNameFromPath_ImportBpy(const FString& GraphPath)
{
	if (GraphPath.IsEmpty())
	{
		return FString();
	}

	int32 SeparatorIndex = INDEX_NONE;
	if (GraphPath.FindLastChar(TEXT(':'), SeparatorIndex) || GraphPath.FindLastChar(TEXT('.'), SeparatorIndex))
	{
		return GraphPath.Mid(SeparatorIndex + 1);
	}

	return GraphPath;
}

UEdGraph* ResolveMacroGraph_ImportBpy(const FString& GraphPath, const FString& MacroName)
{
	if (!GraphPath.IsEmpty())
	{
		if (UEdGraph* Graph = ResolveNamedObject_ImportBpy<UEdGraph>(GraphPath))
		{
			return Graph;
		}
	}

	if (MacroName.IsEmpty())
	{
		return nullptr;
	}

	for (TObjectIterator<UEdGraph> It; It; ++It)
	{
		UEdGraph* Graph = *It;
		if (!Graph || Graph->GetName() != MacroName)
		{
			continue;
		}

		if (Graph->GetSchema() && Graph->GetSchema()->GetGraphType(Graph) == GT_Macro)
		{
			return Graph;
		}
	}

	return nullptr;
}

UEdGraph* ResolveMacroGraph_ImportBpy(UEdGraph* ContextGraph, const FString& GraphPath, const FString& MacroName)
{
	if (ContextGraph)
	{
		if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(ContextGraph))
		{
			const FString DesiredName = !MacroName.IsEmpty() ? MacroName : GetGraphObjectNameFromPath_ImportBpy(GraphPath);
			if (!DesiredName.IsEmpty())
			{
				for (UEdGraph* MacroGraph : Blueprint->MacroGraphs)
				{
					if (MacroGraph && MacroGraph->GetName() == DesiredName)
					{
						return MacroGraph;
					}
				}
			}
		}
	}

	return ResolveMacroGraph_ImportBpy(GraphPath, MacroName);
}

UBlueprint* ResolveOwningBlueprintForGraph_ImportBpy(UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
	{
		return Blueprint;
	}

	if (UEdGraphNode* OuterNode = Cast<UEdGraphNode>(Graph->GetOuter()))
	{
		if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(OuterNode))
		{
			return Blueprint;
		}

		if (UEdGraph* OwnerGraph = OuterNode->GetGraph())
		{
			if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(OwnerGraph))
			{
				return Blueprint;
			}
		}
	}

	if (UBlueprint* OuterBlueprint = Graph->GetTypedOuter<UBlueprint>())
	{
		return OuterBlueprint;
	}

	return nullptr;
}

UAnimBlueprint* ResolveOwningAnimBlueprintForGraph_ImportBpy(UEdGraph* Graph)
{
	return Cast<UAnimBlueprint>(ResolveOwningBlueprintForGraph_ImportBpy(Graph));
}

UFunction* ResolveFunctionOnBlueprintContext_ImportBpy(UEdGraph* Graph, const FString& FuncName)
{
	if (!Graph || FuncName.IsEmpty())
	{
		return nullptr;
	}

	auto FindOnClass = [&FuncName](UClass* Class) -> UFunction*
	{
		return Class ? Class->FindFunctionByName(FName(*FuncName)) : nullptr;
	};

	if (UBlueprint* Blueprint = ResolveOwningBlueprintForGraph_ImportBpy(Graph))
	{
		if (UFunction* Func = FindOnClass(Blueprint->SkeletonGeneratedClass))
		{
			return Func;
		}
		if (UFunction* Func = FindOnClass(Blueprint->GeneratedClass))
		{
			return Func;
		}
		if (UFunction* Func = FindOnClass(Blueprint->ParentClass))
		{
			return Func;
		}
	}

	return nullptr;
}

FString NormalizeLooseFunctionName_ImportBpy(FString Name)
{
	Name.TrimStartAndEndInline();
	Name.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
	Name.ReplaceInline(TEXT("_"), TEXT(""), ESearchCase::CaseSensitive);
	Name.ToLowerInline();
	return Name;
}

FString NormalizeAggressiveFunctionName_ImportBpy(FString Name)
{
	Name.TrimStartAndEndInline();
	FString Out;
	Out.Reserve(Name.Len());
	for (const TCHAR Ch : Name)
	{
		if (FChar::IsAlnum(Ch))
		{
			Out.AppendChar(FChar::ToLower(Ch));
		}
	}
	return Out;
}

bool IsAnimNotifyEventName_ImportBpy(const FString& EventName)
{
	FString Normalized = EventName;
	Normalized.TrimStartAndEndInline();
	return Normalized.StartsWith(TEXT("AnimNotify_"), ESearchCase::IgnoreCase);
}

bool IsAnimNotifyTransitionEventName_ImportBpy(const FString& EventName)
{
	FString Normalized = EventName;
	Normalized.TrimStartAndEndInline();
	return Normalized.StartsWith(TEXT("AnimNotify_Transition"), ESearchCase::IgnoreCase);
}

void ConfigureAnimNotifyEventNodeReference_ImportBpy(UK2Node_Event* EventNode, const FString& EventName)
{
	if (!EventNode || EventName.IsEmpty())
	{
		return;
	}

	const bool bIsTransitionNotify = IsAnimNotifyTransitionEventName_ImportBpy(EventName);

	// Transition notifies in ABP collapsed graphs are internal K2 events and
	// must keep internal-event semantics; regular AnimNotify_* callbacks should
	// stay override events on AnimInstance.
	EventNode->EventReference.SetExternalMember(FName(*EventName), UAnimInstance::StaticClass());
	EventNode->bOverrideFunction = !bIsTransitionNotify;
	EventNode->bInternalEvent = bIsTransitionNotify;
	EventNode->CustomFunctionName = NAME_None;
}

UFunction* ResolveFunctionOnBlueprintContextByAggressiveName_ImportBpy(UEdGraph* Graph, const FString& FuncName)
{
	if (!Graph || FuncName.IsEmpty())
	{
		return nullptr;
	}

	const FString RequestedAggressive = NormalizeAggressiveFunctionName_ImportBpy(FuncName);
	if (RequestedAggressive.IsEmpty())
	{
		return nullptr;
	}

	auto FindOnClass = [&RequestedAggressive](UClass* Class) -> UFunction*
	{
		if (!Class)
		{
			return nullptr;
		}

		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Candidate = *It;
			if (!Candidate)
			{
				continue;
			}

			const FString CandidateName = Candidate->GetFName().ToString();
			if (NormalizeAggressiveFunctionName_ImportBpy(CandidateName) == RequestedAggressive)
			{
				return Candidate;
			}

			const FString CandidateDisplay = FName::NameToDisplayString(CandidateName, false);
			if (NormalizeAggressiveFunctionName_ImportBpy(CandidateDisplay) == RequestedAggressive)
			{
				return Candidate;
			}
		}
		return nullptr;
	};

	if (UBlueprint* Blueprint = ResolveOwningBlueprintForGraph_ImportBpy(Graph))
	{
		if (UFunction* Func = FindOnClass(Blueprint->SkeletonGeneratedClass))
		{
			return Func;
		}
		if (UFunction* Func = FindOnClass(Blueprint->GeneratedClass))
		{
			return Func;
		}
		if (UFunction* Func = FindOnClass(Blueprint->ParentClass))
		{
			return Func;
		}
	}

	return nullptr;
}

UFunction* ResolveBlueprintDeclaredFunctionByLooseName_ImportBpy(UBlueprint* Blueprint, const FString& FuncName)
{
	if (!Blueprint || FuncName.IsEmpty() || !Blueprint->SkeletonGeneratedClass)
	{
		return nullptr;
	}

	const FString RequestedLoose = NormalizeLooseFunctionName_ImportBpy(FuncName);
	if (RequestedLoose.IsEmpty())
	{
		return nullptr;
	}

	for (TFieldIterator<UFunction> It(Blueprint->SkeletonGeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		UFunction* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		const FString CandidateName = Candidate->GetFName().ToString();
		if (NormalizeLooseFunctionName_ImportBpy(CandidateName) == RequestedLoose)
		{
			return Candidate;
		}

		const FString CandidateDisplayName = FName::NameToDisplayString(CandidateName, false);
		if (NormalizeLooseFunctionName_ImportBpy(CandidateDisplayName) == RequestedLoose)
		{
			return Candidate;
		}
	}

	return nullptr;
}

UFunction* ResolveSelfContextFunction_ImportBpy(UEdGraph* Graph, const FString& FuncName)
{
	if (!Graph || FuncName.IsEmpty())
	{
		return nullptr;
	}

	if (UFunction* Func = ResolveFunctionOnBlueprintContext_ImportBpy(Graph, FuncName))
	{
		return Func;
	}

	TArray<FString> Candidates;
	Candidates.Reserve(3);
	Candidates.Add(FuncName);

	FString SpacesToUnderscore = FuncName;
	SpacesToUnderscore.ReplaceInline(TEXT(" "), TEXT("_"), ESearchCase::CaseSensitive);
	if (!SpacesToUnderscore.Equals(FuncName, ESearchCase::CaseSensitive))
	{
		Candidates.AddUnique(SpacesToUnderscore);
	}

	FString RemoveSpaces = FuncName;
	RemoveSpaces.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
	if (!RemoveSpaces.Equals(FuncName, ESearchCase::CaseSensitive))
	{
		Candidates.AddUnique(RemoveSpaces);
	}

	for (const FString& Candidate : Candidates)
	{
		if (UFunction* Func = ResolveFunctionOnBlueprintContext_ImportBpy(Graph, Candidate))
		{
			return Func;
		}
	}

	if (UBlueprint* Blueprint = ResolveOwningBlueprintForGraph_ImportBpy(Graph))
	{
		for (const FString& Candidate : Candidates)
		{
			if (UFunction* Func = ResolveBlueprintDeclaredFunctionByLooseName_ImportBpy(Blueprint, Candidate))
			{
				return Func;
			}
		}
	}

	return nullptr;
}

bool RebindUnresolvedSelfContextCallNode_ImportBpy(UK2Node_CallFunction* CallNode)
{
	if (!CallNode || !CallNode->FunctionReference.IsSelfContext() || CallNode->GetTargetFunction())
	{
		return false;
	}

	UEdGraph* Graph = CallNode->GetGraph();
	if (!Graph)
	{
		return false;
	}

	const FString FuncName = CallNode->FunctionReference.GetMemberName().ToString();
	UFunction* ResolvedFunc = ResolveSelfContextFunction_ImportBpy(Graph, FuncName);
	if (!ResolvedFunc)
	{
		return false;
	}

	CallNode->SetFromFunction(ResolvedFunc);
	CallNode->ReconstructNode();
	return true;
}

void RebindUnresolvedSelfContextCallNodes_ImportBpy(const TMap<FString, UEdGraphNode*>& NodeMap)
{
	for (const TPair<FString, UEdGraphNode*>& NodePair : NodeMap)
	{
		RebindUnresolvedSelfContextCallNode_ImportBpy(Cast<UK2Node_CallFunction>(NodePair.Value));
	}
}

bool IsQualifiedFunctionReference_ImportBpy(const FString& FunctionRef)
{
	return FunctionRef.Contains(TEXT("::")) ||
		FunctionRef.Contains(TEXT("/")) ||
		FunctionRef.Contains(TEXT("."));
}

UEdGraph* FindRootGraphByName_ImportBpy(UBlueprint* BP, const FString& Name);
void EnsureFunctionPins_ImportBpy(UK2Node_FunctionEntry* EntryNode, const TArray<TPair<FString, FEdGraphPinType>>& Inputs);
void EnsureFunctionPins_ImportBpy(UK2Node_FunctionResult* ResultNode, const TArray<TPair<FString, FEdGraphPinType>>& Outputs);
void ApplyFunctionGraphMetadata_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson, UK2Node_FunctionEntry* EntryNode);
void ReplayFunctionGraphMetadataToSignature_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs);
void ParsePinTypeString_ImportBpy(const FString& TypeStr, FEdGraphPinType& OutType);
void ParseGraphPins_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson, const TCHAR* FieldName, TArray<TPair<FString, FEdGraphPinType>>& OutPins);
bool GraphJsonContainsAnimNodes_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson);

struct FAnimNodeBindingDescriptor_ImportBpy;
bool CollectAnimNodeBindingDescriptorsFromLiveNode_ImportBpy(
	UEdGraphNode* Node,
	TMap<FString, FAnimNodeBindingDescriptor_ImportBpy>& OutDescriptors,
	FString& OutError);
void LogMotionMatchingBindingMapSnapshot_ImportBpy(
	UEdGraphNode* Node,
	const TCHAR* Phase,
	const FString* SourceBindingsText = nullptr);
void LogMotionMatchingBindingSnapshotsForBlueprint_ImportBpy(
	UBlueprint* BP,
	const TCHAR* Phase);

int32 GetGraphImportPriority_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson)
{
	if (!GraphJson.IsValid())
	{
		return MAX_int32;
	}

	const FString GraphType = GraphJson->GetStringField(TEXT("graph_type"));
	if (GraphType == TEXT("macro"))
	{
		return 0;
	}
	if (GraphType == TEXT("function"))
	{
		const FString GraphName = GraphJson->GetStringField(TEXT("name"));
		const bool bContainsAnimNodes = GraphJsonContainsAnimNodes_ImportBpy(GraphJson);
		const bool bIsPrimaryAnimGraph =
			GraphName.Equals(UEdGraphSchema_K2::GN_AnimGraph.ToString(), ESearchCase::CaseSensitive);

		// Import non-anim function graphs first so anim-node function references
		// (OnUpdate/OnBecomeRelevant/OnStateEntry bindings) can resolve when
		// secondary anim graphs are reconstructed.
		if (!bContainsAnimNodes)
		{
			return 1;
		}
		// Anim layer / secondary animation graphs must exist before the primary
		// AnimGraph tries to materialize linked-layer input pins from them.
		if (bContainsAnimNodes && !bIsPrimaryAnimGraph)
		{
			return 2;
		}
		return 3;
	}
	if (GraphType == TEXT("event_graph"))
	{
		return 4;
	}
	return 5;
}

bool IsNodeOwnedNestedGraphJson_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson)
{
	if (!GraphJson.IsValid())
	{
		return false;
	}

	FString GraphOuterKind;
	if (!GraphJson->TryGetStringField(TEXT("graph_outer"), GraphOuterKind))
	{
		return false;
	}

	GraphOuterKind.TrimStartAndEndInline();
	return GraphOuterKind.Equals(TEXT("Node"), ESearchCase::IgnoreCase);
}

void SetUseCachedPoseNameOfCache_ImportBpy(UAnimGraphNode_UseCachedPose* UseCachedPoseNode, const FString& CachePoseName)
{
	if (!UseCachedPoseNode || CachePoseName.IsEmpty())
	{
		return;
	}

	if (FStrProperty* NameOfCacheProperty =
			FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache")))
	{
		NameOfCacheProperty->SetPropertyValue_InContainer(UseCachedPoseNode, CachePoseName);
	}
}

FString GetSerializedCachePoseNameFromNodeProps_ImportBpy(const TSharedPtr<FJsonObject>& NodePropsObj)
{
	if (!NodePropsObj.IsValid())
	{
		return FString();
	}

	FString CachePoseName;
	if (!NodePropsObj->TryGetStringField(TEXT("CachePoseName"), CachePoseName) || CachePoseName.IsEmpty())
	{
		NodePropsObj->TryGetStringField(TEXT("CacheName"), CachePoseName);
	}
	return CachePoseName;
}

void LogCachedPoseNodeSnapshot_ImportBpy(UEdGraphNode* Node, const TCHAR* Phase)
{
	if (!Node)
	{
		return;
	}

	if (UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode = Cast<UAnimGraphNode_SaveCachedPose>(Node))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][CachedPose][%s] save graph=%s node=%s cache_name=%s node_cache_pose_name=%s"),
			Phase ? Phase : TEXT("unknown"),
			Node->GetGraph() ? *Node->GetGraph()->GetName() : TEXT("<null>"),
			*DescribeNode_ImportBpy(Node),
			*SaveCachedPoseNode->CacheName,
			*SaveCachedPoseNode->Node.CachePoseName.ToString());
		return;
	}

	if (UAnimGraphNode_UseCachedPose* UseCachedPoseNode = Cast<UAnimGraphNode_UseCachedPose>(Node))
	{
		FString NameOfCache;
		if (const FStrProperty* NameOfCacheProperty =
				FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache")))
		{
			NameOfCache = NameOfCacheProperty->GetPropertyValue_InContainer(UseCachedPoseNode);
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][CachedPose][%s] use graph=%s node=%s node_cache_pose_name=%s name_of_cache=%s save_node=%s"),
			Phase ? Phase : TEXT("unknown"),
			Node->GetGraph() ? *Node->GetGraph()->GetName() : TEXT("<null>"),
			*DescribeNode_ImportBpy(Node),
			*UseCachedPoseNode->Node.CachePoseName.ToString(),
			*NameOfCache,
			UseCachedPoseNode->SaveCachedPoseNode.IsValid()
				? *DescribeNode_ImportBpy(UseCachedPoseNode->SaveCachedPoseNode.Get())
				: TEXT("<null>"));
	}
}

void ReplayCachedPoseNodeState_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodePropsObj)
{
	if (!Node || !NodePropsObj.IsValid())
	{
		return;
	}

	const FString CachePoseName = GetSerializedCachePoseNameFromNodeProps_ImportBpy(NodePropsObj);
	if (CachePoseName.IsEmpty())
	{
		return;
	}

	if (UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode = Cast<UAnimGraphNode_SaveCachedPose>(Node))
	{
		SaveCachedPoseNode->CacheName = CachePoseName;
		SaveCachedPoseNode->Node.CachePoseName = FName(*CachePoseName);
		return;
	}

	if (UAnimGraphNode_UseCachedPose* UseCachedPoseNode = Cast<UAnimGraphNode_UseCachedPose>(Node))
	{
		UseCachedPoseNode->Node.CachePoseName = FName(*CachePoseName);
		SetUseCachedPoseNameOfCache_ImportBpy(UseCachedPoseNode, CachePoseName);
	}
}

void LogCachedPoseGraphSnapshot_ImportBpy(UEdGraph* Graph, const TCHAR* Phase)
{
	if (!Graph)
	{
		return;
	}

	TArray<UAnimGraphNode_SaveCachedPose*> SaveCachedPoseNodes;
	Graph->GetNodesOfClass(SaveCachedPoseNodes);
	for (UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode : SaveCachedPoseNodes)
	{
		LogCachedPoseNodeSnapshot_ImportBpy(SaveCachedPoseNode, Phase);
	}

	TArray<UAnimGraphNode_UseCachedPose*> UseCachedPoseNodes;
	Graph->GetNodesOfClass(UseCachedPoseNodes);
	for (UAnimGraphNode_UseCachedPose* UseCachedPoseNode : UseCachedPoseNodes)
	{
		LogCachedPoseNodeSnapshot_ImportBpy(UseCachedPoseNode, Phase);
	}
}

void ResolveUseCachedPoseLinksInGraph_ImportBpy(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	TArray<UAnimGraphNode_SaveCachedPose*> SaveCachedPoseNodes;
	Graph->GetNodesOfClass(SaveCachedPoseNodes);

	TMap<FString, UAnimGraphNode_SaveCachedPose*> SaveCachedPoseByName;
	for (UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode : SaveCachedPoseNodes)
	{
		if (!SaveCachedPoseNode || SaveCachedPoseNode->CacheName.IsEmpty())
		{
			continue;
		}

		SaveCachedPoseNode->Node.CachePoseName = FName(*SaveCachedPoseNode->CacheName);
		SaveCachedPoseByName.Add(SaveCachedPoseNode->CacheName, SaveCachedPoseNode);
	}

	TArray<UAnimGraphNode_UseCachedPose*> UseCachedPoseNodes;
	Graph->GetNodesOfClass(UseCachedPoseNodes);

	for (UAnimGraphNode_UseCachedPose* UseCachedPoseNode : UseCachedPoseNodes)
	{
		if (!UseCachedPoseNode)
		{
			continue;
		}

		FString CachePoseName = UseCachedPoseNode->Node.CachePoseName.ToString();
		if (CachePoseName.IsEmpty() && UseCachedPoseNode->SaveCachedPoseNode.IsValid())
		{
			CachePoseName = UseCachedPoseNode->SaveCachedPoseNode->CacheName;
		}
		if (CachePoseName.IsEmpty())
		{
			if (const FStrProperty* NameOfCacheProperty =
					FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache")))
			{
				CachePoseName = NameOfCacheProperty->GetPropertyValue_InContainer(UseCachedPoseNode);
			}
		}
		if (CachePoseName.IsEmpty())
		{
			continue;
		}

		UseCachedPoseNode->Node.CachePoseName = FName(*CachePoseName);
		SetUseCachedPoseNameOfCache_ImportBpy(UseCachedPoseNode, CachePoseName);

		if (UAnimGraphNode_SaveCachedPose* const* SaveCachedPoseNode = SaveCachedPoseByName.Find(CachePoseName))
		{
			UseCachedPoseNode->SaveCachedPoseNode = *SaveCachedPoseNode;
		}
	}
}

bool IsAnimBlueprintFunctionGraph_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& GraphType,
	const FString& GraphName)
{
	if (GraphType != TEXT("function") || !Cast<UAnimBlueprint>(BP))
	{
		return false;
	}

	if (Graph && (Graph->IsA<UAnimationGraph>() || (Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationGraphSchema>())))
	{
		return true;
	}

	return GraphName.Equals(UEdGraphSchema_K2::GN_AnimGraph.ToString(), ESearchCase::CaseSensitive);
}

bool GraphJsonContainsAnimNodes_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson)
{
	if (!GraphJson.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphJson->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		const FString NodeClass = NodeObj->GetStringField(TEXT("node_class"));
		if (NodeClass.StartsWith(TEXT("AnimGraphNode_")))
		{
			return true;
		}
	}

	return false;
}

bool IsAnimBlueprintFunctionGraph_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& GraphJson,
	const FString& GraphType,
	const FString& GraphName)
{
	return IsAnimBlueprintFunctionGraph_ImportBpy(BP, Graph, GraphType, GraphName) ||
		(GraphType == TEXT("function") && Cast<UAnimBlueprint>(BP) && GraphJsonContainsAnimNodes_ImportBpy(GraphJson));
}

UClass* GetInterfaceOwnerClass_ImportBpy(UBlueprint* BP, const FString& GraphName, UFunction** OutInterfaceFunction = nullptr)
{
	if (OutInterfaceFunction)
	{
		*OutInterfaceFunction = nullptr;
	}

	if (!BP || GraphName.IsEmpty())
	{
		return nullptr;
	}

	UFunction* InterfaceFunction = FBlueprintEditorUtils::GetInterfaceFunction(BP, FName(*GraphName));
	if (InterfaceFunction)
	{
		if (OutInterfaceFunction)
		{
			*OutInterfaceFunction = InterfaceFunction;
		}

		if (UClass* InterfaceClass = Cast<UClass>(InterfaceFunction->GetOuter()))
		{
			return InterfaceClass->GetAuthoritativeClass();
		}
	}

	// Fallback path for early import stages where GetInterfaceFunction can be
	// temporarily unresolved even though the interface list is present.
	const FName FunctionName(*GraphName);
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		UClass* InterfaceClass = InterfaceDesc.Interface
			? InterfaceDesc.Interface->GetAuthoritativeClass()
			: nullptr;
		if (!InterfaceClass)
		{
			continue;
		}

		UFunction* CandidateFunction = InterfaceClass->FindFunctionByName(FunctionName);
		if (!CandidateFunction)
		{
			continue;
		}

		if (OutInterfaceFunction)
		{
			*OutInterfaceFunction = CandidateFunction;
		}
		return InterfaceClass;
	}

	return nullptr;
}

bool IsGraphBoundToInterfaceAuthoritative_ImportBpy(
	const UBlueprint* BP,
	const UEdGraph* Graph,
	const UClass* InterfaceClass)
{
	if (!BP || !Graph || !InterfaceClass)
	{
		return false;
	}

	const UClass* ExpectedInterface = InterfaceClass->GetAuthoritativeClass();
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		const UClass* ActualInterface = InterfaceDesc.Interface
			? InterfaceDesc.Interface->GetAuthoritativeClass()
			: nullptr;
		if (ActualInterface != ExpectedInterface)
		{
			continue;
		}

		if (InterfaceDesc.Graphs.Contains(const_cast<UEdGraph*>(Graph)))
		{
			return true;
		}
	}

	return false;
}

bool EnsureInterfaceGraphBinding_ImportBpy(UBlueprint* BP, UEdGraph* Graph, UClass* InterfaceClass, UFunction* InterfaceFunction)
{
	if (!BP || !Graph || !InterfaceClass || !InterfaceFunction)
	{
		return false;
	}

	const UClass* CanonicalInterfaceClass = InterfaceClass->GetAuthoritativeClass();
	if (!CanonicalInterfaceClass)
	{
		return false;
	}

	bool bModified = false;
	bool bHasMatchingInterfaceDescription = false;
	for (FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		const UClass* ExistingInterfaceClass = InterfaceDesc.Interface
			? InterfaceDesc.Interface->GetAuthoritativeClass()
			: nullptr;
		if (ExistingInterfaceClass != CanonicalInterfaceClass)
		{
			continue;
		}
		bHasMatchingInterfaceDescription = true;

		if (InterfaceDesc.Graphs.Find(Graph) == INDEX_NONE)
		{
			InterfaceDesc.Graphs.Add(Graph);
			bModified = true;
		}
		break;
	}

	// Do not demote regular function graphs unless this blueprint truly owns
	// the interface entry. Otherwise we can accidentally drop graphs from both
	// FunctionGraphs and ImplementedInterfaces, causing import parity loss.
	if (!bHasMatchingInterfaceDescription)
	{
		return false;
	}

	Graph->bAllowDeletion = false;
	const FGuid InterfaceGuid = FBlueprintEditorUtils::FindInterfaceFunctionGuid(
		InterfaceFunction,
		const_cast<UClass*>(CanonicalInterfaceClass));
	if (Graph->InterfaceGuid != InterfaceGuid)
	{
		Graph->InterfaceGuid = InterfaceGuid;
		bModified = true;
	}

	if (bModified)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}

	return bModified;
}

bool RepairImplementedInterfaceGraphsFromExistingGraphs_ImportBpy(UBlueprint* BP)
{
	if (!BP)
	{
		return false;
	}

	bool bModified = false;

	for (FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		UClass* InterfaceClass = InterfaceDesc.Interface
			? InterfaceDesc.Interface->GetAuthoritativeClass()
			: nullptr;
		if (!InterfaceClass)
		{
			continue;
		}

		for (TFieldIterator<UFunction> FunctionIter(InterfaceClass, EFieldIteratorFlags::IncludeSuper); FunctionIter; ++FunctionIter)
		{
			UFunction* InterfaceFunction = *FunctionIter;
			if (!InterfaceFunction)
			{
				continue;
			}

			const bool bIsAnimFunction = InterfaceFunction->HasMetaData(FBlueprintMetadata::MD_AnimBlueprintFunction);
			const bool bRequiresFunctionGraph =
				(UEdGraphSchema_K2::CanKismetOverrideFunction(InterfaceFunction) &&
				 !UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(InterfaceFunction)) ||
				bIsAnimFunction;
			if (!bRequiresFunctionGraph)
			{
				continue;
			}

			UEdGraph* Graph = FindObject<UEdGraph>(BP, *InterfaceFunction->GetName());
			if (!Graph)
			{
				continue;
			}

			if (InterfaceDesc.Graphs.Find(Graph) == INDEX_NONE)
			{
				InterfaceDesc.Graphs.Add(Graph);
				bModified = true;
			}

			const FGuid ExpectedGuid = FBlueprintEditorUtils::FindInterfaceFunctionGuid(InterfaceFunction, InterfaceClass);
			if (Graph->InterfaceGuid != ExpectedGuid)
			{
				Graph->InterfaceGuid = ExpectedGuid;
				bModified = true;
			}

			Graph->bAllowDeletion = false;
		}
	}

	if (bModified)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}

	return bModified;
}

bool EnsureGraphExists_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& GraphJson,
	UEdGraph*& OutGraph,
	FString& OutGraphType,
	FString& OutGraphName,
	FString& OutError)
{
	OutGraph = nullptr;
	OutGraphType = GraphJson.IsValid() ? GraphJson->GetStringField(TEXT("graph_type")) : FString();
	OutGraphName = GraphJson.IsValid() ? GraphJson->GetStringField(TEXT("name")) : FString();

	if (!BP || !GraphJson.IsValid())
	{
		OutError = TEXT("Invalid graph json");
		return false;
	}

	if (IsAnimBlueprintFunctionGraph_ImportBpy(BP, nullptr, GraphJson, OutGraphType, OutGraphName))
	{
		UEdGraph* Existing = FindObject<UEdGraph>(BP, *OutGraphName);
		if (!Existing)
		{
			OutGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*OutGraphName),
				UAnimationGraph::StaticClass(),
				UAnimationGraphSchema::StaticClass());
			FBlueprintEditorUtils::AddDomainSpecificGraph(BP, OutGraph);
		}
		else
		{
			OutGraph = Existing;
		}
	}
	else if (OutGraphType == TEXT("function"))
	{
		UEdGraph* Existing = FindObject<UEdGraph>(BP, *OutGraphName);
		UFunction* InterfaceFunction = nullptr;
		UClass* InterfaceClass = GetInterfaceOwnerClass_ImportBpy(BP, OutGraphName, &InterfaceFunction);
		if (!Existing)
		{
			UClass* OverrideFunctionClass =
				FBlueprintEditorUtils::GetOverrideFunctionClass(BP, FName(*OutGraphName));

			OutGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*OutGraphName),
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			if (InterfaceClass && InterfaceFunction)
			{
				EnsureInterfaceGraphBinding_ImportBpy(BP, OutGraph, InterfaceClass, InterfaceFunction);
				FBlueprintEditorUtils::AddInterfaceGraph(BP, OutGraph, InterfaceClass);
				EnsureInterfaceGraphBinding_ImportBpy(BP, OutGraph, InterfaceClass, InterfaceFunction);
				if (!IsGraphBoundToInterfaceAuthoritative_ImportBpy(BP, OutGraph, InterfaceClass))
				{
					OutError = FString::Printf(
						TEXT("Interface graph registration failed: graph=%s interface=%s"),
						*OutGraphName,
						*GetPathNameSafe(InterfaceClass->GetAuthoritativeClass()));
					return false;
				}
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			}
			else
			{
				FBlueprintEditorUtils::AddFunctionGraph<UClass>(
					BP,
					OutGraph,
					OverrideFunctionClass == nullptr,
					OverrideFunctionClass);
			}
		}
		else
		{
			OutGraph = Existing;
			if (InterfaceClass && InterfaceFunction)
			{
				EnsureInterfaceGraphBinding_ImportBpy(BP, OutGraph, InterfaceClass, InterfaceFunction);
				if (!IsGraphBoundToInterfaceAuthoritative_ImportBpy(BP, OutGraph, InterfaceClass))
				{
					OutError = FString::Printf(
						TEXT("Interface graph binding failed for existing graph: graph=%s interface=%s"),
						*OutGraphName,
						*GetPathNameSafe(InterfaceClass->GetAuthoritativeClass()));
					return false;
				}
			}
		}
	}
	else if (OutGraphType == TEXT("macro"))
	{
		UEdGraph* Existing = FindObject<UEdGraph>(BP, *OutGraphName);
		if (Existing)
		{
			OutGraph = Existing;
		}
		else
		{
			OutGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*OutGraphName),
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddMacroGraph(BP, OutGraph, false, nullptr);
		}
	}
	else
	{
		if (BP->UbergraphPages.Num() > 0)
		{
			OutGraph = BP->UbergraphPages[0];
		}
		else
		{
			OutGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*OutGraphName),
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			BP->UbergraphPages.Add(OutGraph);
		}
	}

	if (!OutGraph)
	{
		OutError = FString::Printf(TEXT("Cannot create graph: %s"), *OutGraphName);
		return false;
	}

	const bool bTreatAsRegularFunctionGraph =
		(OutGraphType == TEXT("function")) &&
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, OutGraph, GraphJson, OutGraphType, OutGraphName);

	if (bTreatAsRegularFunctionGraph)
	{
		TArray<TPair<FString, FEdGraphPinType>> GraphInputs;
		TArray<TPair<FString, FEdGraphPinType>> GraphOutputs;
		ParseGraphPins_ImportBpy(GraphJson, TEXT("inputs"), GraphInputs);
		ParseGraphPins_ImportBpy(GraphJson, TEXT("outputs"), GraphOutputs);

		TArray<UK2Node_FunctionEntry*> EntryNodes;
		OutGraph->GetNodesOfClass(EntryNodes);
		if (EntryNodes.Num() == 0)
		{
			UK2Node_FunctionEntry* Entry = NewObject<UK2Node_FunctionEntry>(OutGraph);
			Entry->CreateNewGuid();
			Entry->PostPlacedNewNode();
			Entry->AllocateDefaultPins();
			OutGraph->AddNode(Entry, false, false);
			EntryNodes.Add(Entry);
		}
		ApplyFunctionGraphMetadata_ImportBpy(GraphJson, EntryNodes[0]);
		EnsureFunctionPins_ImportBpy(EntryNodes[0], GraphInputs);

		TArray<UK2Node_FunctionResult*> ResultNodes;
		OutGraph->GetNodesOfClass(ResultNodes);
		if (GraphOutputs.Num() > 0 && ResultNodes.Num() == 0)
		{
			UK2Node_FunctionResult* ResultNode = NewObject<UK2Node_FunctionResult>(OutGraph);
			ResultNode->CreateNewGuid();
			ResultNode->PostPlacedNewNode();
			ResultNode->AllocateDefaultPins();
			OutGraph->AddNode(ResultNode, false, false);
			ResultNodes.Add(ResultNode);
		}
		for (UK2Node_FunctionResult* ResultNode : ResultNodes)
		{
			EnsureFunctionPins_ImportBpy(ResultNode, GraphOutputs);
		}
	}

	return true;
}

void ApplyFunctionGraphMetadata_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphJson,
	UK2Node_FunctionEntry* EntryNode)
{
	if (!GraphJson.IsValid() || !EntryNode)
	{
		return;
	}

	bool bThreadSafe = false;
	if (GraphJson->TryGetBoolField(TEXT("thread_safe"), bThreadSafe))
	{
		EntryNode->MetaData.bThreadSafe = bThreadSafe;
		if (UFunction* SignatureFunction = EntryNode->FindSignatureFunction())
		{
			if (bThreadSafe)
			{
				SignatureFunction->SetMetaData(FBlueprintMetadata::MD_ThreadSafe, TEXT("true"));
			}
			else if (SignatureFunction->HasMetaData(FBlueprintMetadata::MD_ThreadSafe))
			{
				SignatureFunction->RemoveMetaData(FBlueprintMetadata::MD_ThreadSafe);
			}
		}
	}

	FString CategoryText;
	if (GraphJson->TryGetStringField(TEXT("category"), CategoryText))
	{
		EntryNode->MetaData.Category = FText::FromString(CategoryText);
		if (UFunction* SignatureFunction = EntryNode->FindSignatureFunction())
		{
			if (!CategoryText.IsEmpty())
			{
				SignatureFunction->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *CategoryText);
			}
			else if (SignatureFunction->HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
			{
				SignatureFunction->RemoveMetaData(FBlueprintMetadata::MD_FunctionCategory);
			}
		}
	}

	bool bIsPure = false;
	if (GraphJson->TryGetBoolField(TEXT("is_pure"), bIsPure))
	{
		int32 ExtraFlags = EntryNode->GetExtraFlags();
		if (bIsPure)
		{
			ExtraFlags |= FUNC_BlueprintPure;
		}
		else
		{
			ExtraFlags &= ~FUNC_BlueprintPure;
		}
		EntryNode->SetExtraFlags(ExtraFlags);

		if (UFunction* SignatureFunction = EntryNode->FindSignatureFunction())
		{
			if (bIsPure)
			{
				SignatureFunction->FunctionFlags |= FUNC_BlueprintPure;
			}
			else
			{
				SignatureFunction->FunctionFlags &= ~FUNC_BlueprintPure;
			}
		}

		EntryNode->ReconstructNode();
	}
}

void ReplayFunctionGraphMetadataToSignature_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs)
{
	if (!BP)
	{
		return;
	}

	auto IncrementNameCount = [](TMap<FString, int32>& Map, UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}

		const FString Name = Graph->GetName();
		if (Name.IsEmpty())
		{
			return;
		}

		int32& Count = Map.FindOrAdd(Name);
		++Count;
	};

	TMap<FString, int32> RootGraphNameCounts;
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		IncrementNameCount(RootGraphNameCounts, Graph);
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		IncrementNameCount(RootGraphNameCounts, Graph);
	}
	for (UEdGraph* Graph : BP->MacroGraphs)
	{
		IncrementNameCount(RootGraphNameCounts, Graph);
	}
	for (UEdGraph* Graph : BP->DelegateSignatureGraphs)
	{
		IncrementNameCount(RootGraphNameCounts, Graph);
	}
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			IncrementNameCount(RootGraphNameCounts, Graph);
		}
	}

	TMap<FString, int32> AllGraphNameCounts;
	{
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			IncrementNameCount(AllGraphNameCounts, Graph);
		}
	}

	int32 ExpectedFunctionGraphCount = 0;
	int32 ExpectedMetadataReplayCount = 0;
	int32 ReplayedGraphCount = 0;
	int32 SkippedAnimLayerGraphCount = 0;
	TArray<FString> SkippedAnimLayerSamples;
	TArray<FString> MissingGraphSamples;
	for (const TSharedPtr<FJsonObject>& GraphJson : SortedGraphs)
	{
		if (!GraphJson.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphJson))
		{
			continue;
		}

		FString GraphType;
		FString GraphName;
		GraphJson->TryGetStringField(TEXT("graph_type"), GraphType);
		GraphJson->TryGetStringField(TEXT("name"), GraphName);
		if (!GraphType.Equals(TEXT("function"), ESearchCase::CaseSensitive) || GraphName.IsEmpty())
		{
			continue;
		}
		++ExpectedFunctionGraphCount;
		++ExpectedMetadataReplayCount;

		UEdGraph* Graph = FindRootGraphByName_ImportBpy(BP, GraphName);
		if (!Graph)
		{
			if (MissingGraphSamples.Num() < 12)
			{
				const int32 RootCount = RootGraphNameCounts.FindRef(GraphName);
				const int32 AllCount = AllGraphNameCounts.FindRef(GraphName);
				FString GraphOuterKind;
				GraphJson->TryGetStringField(TEXT("graph_outer"), GraphOuterKind);
				MissingGraphSamples.Add(FString::Printf(
					TEXT("%s(reason=no_root_match,outer=%s,root_count=%d,all_count=%d)"),
					*GraphName,
					GraphOuterKind.IsEmpty() ? TEXT("<none>") : *GraphOuterKind,
					RootCount,
					AllCount));
			}
			continue;
		}

		TArray<UK2Node_FunctionEntry*> EntryNodes;
		Graph->GetNodesOfClass(EntryNodes);
		if (EntryNodes.Num() == 0 || !EntryNodes[0])
		{
			bool bHasAnimGraphRoot = false;
			if (Graph)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && Node->GetClass() &&
						Node->GetClass()->GetName().Equals(TEXT("AnimGraphNode_Root"), ESearchCase::CaseSensitive))
					{
						bHasAnimGraphRoot = true;
						break;
					}
				}
			}

			if (bHasAnimGraphRoot)
			{
				--ExpectedMetadataReplayCount;
				++SkippedAnimLayerGraphCount;
				if (SkippedAnimLayerSamples.Num() < 12)
				{
					SkippedAnimLayerSamples.Add(FString::Printf(
						TEXT("%s(path=%s)"),
						*GraphName,
						*GetPathNameSafe(Graph)));
				}
				continue;
			}

			if (MissingGraphSamples.Num() < 12)
			{
				const int32 RootCount = RootGraphNameCounts.FindRef(GraphName);
				const int32 AllCount = AllGraphNameCounts.FindRef(GraphName);
				MissingGraphSamples.Add(FString::Printf(
					TEXT("%s(reason=no_entry_node,root_count=%d,all_count=%d,path=%s)"),
					*GraphName,
					RootCount,
					AllCount,
					*GetPathNameSafe(Graph)));
			}
			continue;
		}

		ApplyFunctionGraphMetadata_ImportBpy(GraphJson, EntryNodes[0]);
		++ReplayedGraphCount;

		if (GraphName.Equals(TEXT("Get_MMBlendTime"), ESearchCase::CaseSensitive) ||
			GraphName.Equals(TEXT("Get_MMNotifyRecencyTimeOut"), ESearchCase::CaseSensitive))
		{
			if (UFunction* Signature = EntryNodes[0]->FindSignatureFunction())
			{
				const bool bIsPure = (Signature->FunctionFlags & FUNC_BlueprintPure) != 0;
				const bool bThreadSafe =
					Signature->HasMetaData(FBlueprintMetadata::MD_ThreadSafe) ||
					Signature->GetMetaData(FBlueprintMetadata::MD_ThreadSafe).Equals(TEXT("true"), ESearchCase::IgnoreCase);
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy][ImportDiag][FunctionMetaReplay] function=%s pure=%d thread_safe=%d category=%s"),
					*GraphName,
					bIsPure ? 1 : 0,
					bThreadSafe ? 1 : 0,
					*Signature->GetMetaData(FBlueprintMetadata::MD_FunctionCategory));
			}
			else
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy][ImportDiag][FunctionMetaReplay] function=%s signature_not_ready"),
					*GraphName);
			}
		}
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag][FunctionMetaReplay] blueprint=%s expected_function_graphs=%d expected_metadata_replay_graphs=%d replayed_function_graphs=%d missing_function_graphs=%d skipped_anim_layer_graphs=%d skipped_anim_layer_samples=%s missing_samples=%s"),
		*BP->GetPathName(),
		ExpectedFunctionGraphCount,
		ExpectedMetadataReplayCount,
		ReplayedGraphCount,
		FMath::Max(0, ExpectedMetadataReplayCount - ReplayedGraphCount),
		SkippedAnimLayerGraphCount,
		SkippedAnimLayerSamples.Num() > 0 ? *FString::Join(SkippedAnimLayerSamples, TEXT(" | ")) : TEXT("<none>"),
		MissingGraphSamples.Num() > 0 ? *FString::Join(MissingGraphSamples, TEXT(" | ")) : TEXT("<none>"));
}

void ParseGraphPins_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphJson,
	const TCHAR* FieldName,
	TArray<TPair<FString, FEdGraphPinType>>& OutPins)
{
	const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
	if (!GraphJson.IsValid() || !GraphJson->TryGetArrayField(FieldName, PinsArray))
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& PinValue : *PinsArray)
	{
		const TSharedPtr<FJsonObject> PinObj = PinValue->AsObject();
		if (!PinObj.IsValid())
		{
			continue;
		}

		FString PinName;
		FString PinTypeString;
		if (!PinObj->TryGetStringField(TEXT("name"), PinName) ||
			!PinObj->TryGetStringField(TEXT("type"), PinTypeString))
		{
			continue;
		}

		FEdGraphPinType PinType;
		ParsePinTypeString_ImportBpy(PinTypeString, PinType);
		OutPins.Add(TPair<FString, FEdGraphPinType>(PinName, PinType));
	}
}

FString JsonValueToDefaultString_ImportBpy(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return FString();
	}

	switch (Value->Type)
	{
	case EJson::Boolean:
		return Value->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::Number:
	{
		const double Number = Value->AsNumber();
		const double Rounded = FMath::RoundToDouble(Number);
		if (FMath::IsNearlyEqual(Number, Rounded))
		{
			return LexToString(static_cast<int64>(Rounded));
		}
		FString NumberText = FString::SanitizeFloat(Number);
		NumberText.RemoveFromEnd(TEXT(".000000"));
		return NumberText;
	}
	case EJson::String:
		return Value->AsString();
	default:
		return Value->AsString();
	}
}

bool IsObjectLikePinCategory_ImportBpy(const FName& PinCategory)
{
	return PinCategory == UEdGraphSchema_K2::PC_Object ||
		PinCategory == UEdGraphSchema_K2::PC_Class ||
		PinCategory == UEdGraphSchema_K2::PC_Interface ||
		PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		PinCategory == UEdGraphSchema_K2::PC_SoftClass;
}

bool IsScalarLiteralPinCategory_ImportBpy(const FName& PinCategory)
{
	return PinCategory == UEdGraphSchema_K2::PC_Boolean ||
		PinCategory == UEdGraphSchema_K2::PC_Byte ||
		PinCategory == UEdGraphSchema_K2::PC_Int ||
		PinCategory == UEdGraphSchema_K2::PC_Int64 ||
		PinCategory == UEdGraphSchema_K2::PC_Real ||
		PinCategory == UEdGraphSchema_K2::PC_Float ||
		PinCategory == UEdGraphSchema_K2::PC_Double;
}

bool IsSimpleLiteralDefaultForPromotable_ImportBpy(const FString& DefaultValue)
{
	if (DefaultValue.IsEmpty())
	{
		return false;
	}

	if (DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
		DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	double ParsedNumber = 0.0;
	return LexTryParseString(ParsedNumber, *DefaultValue);
}

void ApplyDefaultToPin_ImportBpy(UEdGraphPin* Pin, const TSharedPtr<FJsonValue>& Value)
{
	if (!Pin || !Value.IsValid())
	{
		return;
	}

	const FString DefaultValue = JsonValueToDefaultString_ImportBpy(Value);
	FString EffectiveDefaultValue = DefaultValue;
	const UEdGraphSchema* Schema = Pin->GetSchema();
	const FName& PinCategory = Pin->PinType.PinCategory;
	const bool bIsWildcardPin = PinCategory == UEdGraphSchema_K2::PC_Wildcard;
	const FString OwningNodeClassName =
		Pin->GetOwningNode() && Pin->GetOwningNode()->GetClass()
			? Pin->GetOwningNode()->GetClass()->GetName()
			: FString();
	const bool bIsPromotableOperatorPin =
		OwningNodeClassName == TEXT("K2Node_PromotableOperator") ||
		OwningNodeClassName == TEXT("K2Node_CommutativeAssociativeBinaryOperator");
	bool bAppliedUniformVectorFallback = false;

	// Some promotable math nodes in .bp.py payloads carry scalar literals on vector pins
	// (for example `B=5` on vector multiply). Promote these to explicit FVector text.
	if (bIsPromotableOperatorPin &&
		Pin->Direction == EGPD_Input &&
		Pin->LinkedTo.Num() == 0 &&
		PinCategory == UEdGraphSchema_K2::PC_Struct &&
		Pin->PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get() &&
		!DefaultValue.Contains(TEXT(",")) &&
		!DefaultValue.Contains(TEXT("(")) &&
		!DefaultValue.Contains(TEXT("=")))
	{
		double ScalarValue = 0.0;
		if (LexTryParseString(ScalarValue, *DefaultValue))
		{
			const FString ScalarText = FString::SanitizeFloat(ScalarValue);
			EffectiveDefaultValue = FString::Printf(
				TEXT("%s,%s,%s"),
				*ScalarText,
				*ScalarText,
				*ScalarText);
			bAppliedUniformVectorFallback = true;
		}
	}

	if (IsObjectLikePinCategory_ImportBpy(PinCategory) && !EffectiveDefaultValue.IsEmpty())
	{
		UObject* DefaultObject = nullptr;
		if (PinCategory == UEdGraphSchema_K2::PC_Class || PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			DefaultObject = ResolveNamedObject_ImportBpy<UClass>(EffectiveDefaultValue);
		}
		else
		{
			DefaultObject = ResolveNamedObject_ImportBpy<UObject>(EffectiveDefaultValue);
		}

		if (DefaultObject)
		{
			if (Schema)
			{
				Schema->TrySetDefaultObject(*Pin, DefaultObject, false);
			}

			if (Pin->DefaultObject != DefaultObject)
			{
				Pin->DefaultObject = DefaultObject;
			}
			Pin->DefaultValue.Reset();

			return;
		}
	}

	if (PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		const FText TextValue = FText::FromString(EffectiveDefaultValue);
		if (Schema)
		{
			Schema->TrySetDefaultText(*Pin, TextValue, false);
		}
		else
		{
			Pin->DefaultTextValue = TextValue;
		}
		return;
	}

	if (Schema)
	{
		Schema->TrySetDefaultValue(*Pin, EffectiveDefaultValue, false);
		if (!bIsWildcardPin &&
			!bIsPromotableOperatorPin &&
			!Pin->DefaultValue.Equals(EffectiveDefaultValue, ESearchCase::CaseSensitive))
		{
			Pin->DefaultValue = EffectiveDefaultValue;
		}
	}
	else
	{
		if (!bIsWildcardPin && !bIsPromotableOperatorPin)
		{
			Pin->DefaultValue = EffectiveDefaultValue;
		}
	}

	// Promotable operator pins can lose explicit numeric defaults (e.g. +2, -0.5)
	// after type promotion/rebind/reconstruct passes. If schema assignment did not
	// stick, restore the serialized literal on safe input pins.
	const bool bCanForcePromotableDefault =
		bIsPromotableOperatorPin &&
		Pin->Direction == EGPD_Input &&
		Pin->LinkedTo.Num() == 0 &&
		Pin->SubPins.Num() == 0 &&
		(bIsWildcardPin || IsScalarLiteralPinCategory_ImportBpy(Pin->PinType.PinCategory)) &&
		!IsObjectLikePinCategory_ImportBpy(Pin->PinType.PinCategory) &&
		Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
		IsSimpleLiteralDefaultForPromotable_ImportBpy(EffectiveDefaultValue) &&
		!Pin->DefaultValue.Equals(EffectiveDefaultValue, ESearchCase::CaseSensitive);
	if (bCanForcePromotableDefault)
	{
		Pin->DefaultValue = EffectiveDefaultValue;
	}

	const bool bCanForcePromotableVectorDefault =
		bAppliedUniformVectorFallback &&
		bIsPromotableOperatorPin &&
		Pin->Direction == EGPD_Input &&
		Pin->LinkedTo.Num() == 0 &&
		PinCategory == UEdGraphSchema_K2::PC_Struct &&
		Pin->PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get() &&
		!Pin->DefaultValue.Equals(EffectiveDefaultValue, ESearchCase::CaseSensitive);
	if (bCanForcePromotableVectorDefault)
	{
		Pin->DefaultValue = EffectiveDefaultValue;
	}

	Pin->bDefaultValueIsIgnored = false;
	// Keep the pin's auto-generated default intact. K2 uses this field to tell whether
	// an imported value is an explicit override relative to a function signature default.
}

UScriptStruct* ResolveCommonStructType_ImportBpy(const FString& TypeName)
{
	if (TypeName == TEXT("Vector"))
	{
		return TBaseStructure<FVector>::Get();
	}
	if (TypeName == TEXT("Vector2D"))
	{
		return TBaseStructure<FVector2D>::Get();
	}
	if (TypeName == TEXT("Vector4"))
	{
		return TBaseStructure<FVector4>::Get();
	}
	if (TypeName == TEXT("Rotator"))
	{
		return TBaseStructure<FRotator>::Get();
	}
	if (TypeName == TEXT("Transform"))
	{
		return TBaseStructure<FTransform>::Get();
	}
	if (TypeName == TEXT("LinearColor"))
	{
		return TBaseStructure<FLinearColor>::Get();
	}
	if (TypeName == TEXT("Color"))
	{
		return TBaseStructure<FColor>::Get();
	}
	if (TypeName == TEXT("HitResult"))
	{
		return TBaseStructure<FHitResult>::Get();
	}
	return nullptr;
}

void ParsePinTypeString_ImportBpy(const FString& TypeStr, FEdGraphPinType& OutType)
{
	OutType = FEdGraphPinType();

	const int32 FirstQualifierIndex = TypeStr.Find(TEXT("|"), ESearchCase::IgnoreCase);
	const FString BaseType =
		FirstQualifierIndex == INDEX_NONE ? TypeStr : TypeStr.Left(FirstQualifierIndex);
	FString MapValueTypeStr;
	if (TypeStr.Contains(TEXT("|array"), ESearchCase::IgnoreCase))
	{
		OutType.ContainerType = EPinContainerType::Array;
	}
	else if (TypeStr.Contains(TEXT("|set"), ESearchCase::IgnoreCase))
	{
		OutType.ContainerType = EPinContainerType::Set;
	}
	else if (TypeStr.Contains(TEXT("|map"), ESearchCase::IgnoreCase))
	{
		OutType.ContainerType = EPinContainerType::Map;
	}
	OutType.bIsReference = TypeStr.Contains(TEXT("|ref"), ESearchCase::IgnoreCase);
	OutType.bIsConst = TypeStr.Contains(TEXT("|const"), ESearchCase::IgnoreCase);
	const bool bMapValueConst = TypeStr.Contains(TEXT("|mapvalueconst"), ESearchCase::IgnoreCase);
	const bool bMapValueWeak = TypeStr.Contains(TEXT("|mapvalueweak"), ESearchCase::IgnoreCase);
	const bool bMapValueWrapper = TypeStr.Contains(TEXT("|mapvaluewrapper"), ESearchCase::IgnoreCase);

	const int32 MapValueTokenIndex = TypeStr.Find(TEXT("|mapvalue="), ESearchCase::IgnoreCase);
	if (MapValueTokenIndex != INDEX_NONE)
	{
		const int32 MapValueStart = MapValueTokenIndex + FCString::Strlen(TEXT("|mapvalue="));
		int32 MapValueEnd = TypeStr.Find(TEXT("|"), ESearchCase::IgnoreCase, ESearchDir::FromStart, MapValueStart);
		if (MapValueEnd == INDEX_NONE)
		{
			MapValueEnd = TypeStr.Len();
		}
		MapValueTypeStr = TypeStr.Mid(MapValueStart, MapValueEnd - MapValueStart);
	}

	FString Category;
	FString Sub;
	if (!BaseType.Split(TEXT("/"), &Category, &Sub))
	{
		OutType.PinCategory = FName(*BaseType);
		return;
	}

	OutType.PinCategory = FName(*Category);
	const bool bLooksLikeObjectPath =
		!Sub.IsEmpty() &&
		!Sub.StartsWith(TEXT("/")) &&
		(Sub.Contains(TEXT("/")) || Sub.Contains(TEXT(".")));
	const FString NormalizedSub = bLooksLikeObjectPath ? TEXT("/") + Sub : Sub;

	UObject* SubObject = nullptr;
	if (OutType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		SubObject = ResolveNamedObject_ImportBpy<UScriptStruct>(NormalizedSub);
		if (!SubObject)
		{
			SubObject = ResolveCommonStructType_ImportBpy(Sub);
		}
	}
	else if (OutType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		SubObject = ResolveNamedObject_ImportBpy<UEnum>(NormalizedSub);
	}
	else if (OutType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_Interface ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		SubObject = ResolveNamedObject_ImportBpy<UClass>(NormalizedSub);
	}
	else if (OutType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		OutType.PinSubCategory = FName(*Sub);
		return;
	}
	else if (NormalizedSub.StartsWith(TEXT("/")) || Sub.Contains(TEXT(".")))
	{
		SubObject = ResolveNamedObject_ImportBpy<UObject>(NormalizedSub);
	}

	if (SubObject)
	{
		OutType.PinSubCategoryObject = SubObject;
	}
	else if (!Sub.IsEmpty())
	{
		OutType.PinSubCategory = FName(*NormalizedSub);
	}

	if (!MapValueTypeStr.IsEmpty())
	{
		FEdGraphPinType MapValuePinType;
		ParsePinTypeString_ImportBpy(MapValueTypeStr, MapValuePinType);
		if (TypeStr.Contains(TEXT("MovementModeMap")) || MapValueTypeStr.Contains(TEXT("E_MovementMode")))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] ParsePinTypeString mapvalue source=%s parsed=%s"),
				*MapValueTypeStr,
				*DescribePinType_ImportBpy(MapValuePinType));
		}
		OutType.PinValueType = FEdGraphTerminalType::FromPinType(MapValuePinType);
		OutType.PinValueType.bTerminalIsConst =
			OutType.PinValueType.bTerminalIsConst || bMapValueConst;
		OutType.PinValueType.bTerminalIsWeakPointer =
			OutType.PinValueType.bTerminalIsWeakPointer || bMapValueWeak;
		OutType.PinValueType.bTerminalIsUObjectWrapper =
			OutType.PinValueType.bTerminalIsUObjectWrapper || bMapValueWrapper;
		if (TypeStr.Contains(TEXT("MovementModeMap")) || MapValueTypeStr.Contains(TEXT("E_MovementMode")))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] ParsePinTypeString mapvalue terminal cat=%s sub=%s obj=%s"),
				*OutType.PinValueType.TerminalCategory.ToString(),
				*OutType.PinValueType.TerminalSubCategory.ToString(),
				OutType.PinValueType.TerminalSubCategoryObject.IsValid()
					? *OutType.PinValueType.TerminalSubCategoryObject->GetPathName()
					: TEXT(""));
		}
	}
}

FString NormalizeRequestedPinName_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName)
{
	if (!Node)
	{
		return RequestedPinName;
	}

	if (Node->IsA<UK2Node_MacroInstance>())
	{
		if (
			RequestedPinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
			RequestedPinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase))
		{
			return TEXT("execute");
		}
	}
	else if (RequestedPinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase))
	{
		return TEXT("execute");
	}

	if (RequestedPinName == TEXT("self_"))
	{
		return UEdGraphSchema_K2::PN_Self.ToString();
	}
	if (Node->IsA<UK2Node_IfThenElse>())
	{
		if (RequestedPinName.Equals(TEXT("True"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Then.ToString();
		}
		if (RequestedPinName.Equals(TEXT("False"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Else.ToString();
		}
	}

	FString NormalizedChooserPinName = RequestedPinName;

	// UE 5.7 chooser nodes can expose runtime pin names without the legacy "S_" prefix.
	// Normalize exported legacy names so we bind to durable pins and avoid stale links.
	if (IsEvaluateChooserNode_ImportBpy(Node) &&
		NormalizedChooserPinName.StartsWith(TEXT("S_"), ESearchCase::IgnoreCase) &&
		NormalizedChooserPinName.Len() > 2)
	{
		NormalizedChooserPinName = NormalizedChooserPinName.RightChop(2);
	}

	return NormalizedChooserPinName;
}

FString DescribePinType_ImportBpy(const FEdGraphPinType& PinType)
{
	const FString ValueObjectPath =
		PinType.PinValueType.TerminalSubCategoryObject.IsValid()
			? PinType.PinValueType.TerminalSubCategoryObject->GetPathName()
			: FString();
	const FString KeyObjectPath =
		PinType.PinSubCategoryObject.IsValid()
			? PinType.PinSubCategoryObject->GetPathName()
			: FString();
	return FString::Printf(
		TEXT("cat=%s sub=%s key_obj=%s container=%d value_cat=%s value_sub=%s value_obj=%s"),
		*PinType.PinCategory.ToString(),
		*PinType.PinSubCategory.ToString(),
		*KeyObjectPath,
		static_cast<int32>(PinType.ContainerType),
		*PinType.PinValueType.TerminalCategory.ToString(),
		*PinType.PinValueType.TerminalSubCategory.ToString(),
		*ValueObjectPath);
}

void ApplyPinContainerString_ImportBpy(const FString& ContainerText, FEdGraphPinType& PinType)
{
	if (ContainerText == TEXT("array"))
	{
		PinType.ContainerType = EPinContainerType::Array;
	}
	else if (ContainerText == TEXT("set"))
	{
		PinType.ContainerType = EPinContainerType::Set;
	}
	else if (ContainerText == TEXT("map"))
	{
		PinType.ContainerType = EPinContainerType::Map;
	}
	else
	{
		PinType.ContainerType = EPinContainerType::None;
	}
}

void EnsureSetFieldsPinVisible_ImportBpy(UK2Node_SetFieldsInStruct* SetFieldsNode, const FString& RequestedPinName)
{
	if (!SetFieldsNode)
	{
		return;
	}

	const FString RequestedNoGuid = StripGuidSuffix_ImportBpy(RequestedPinName);
	for (FOptionalPinFromProperty& Record : SetFieldsNode->ShowPinForProperties)
	{
		const FString RecordName = Record.PropertyName.ToString();
		const FString RecordNameNoGuid = StripGuidSuffix_ImportBpy(RecordName);
		if (!RecordName.Equals(RequestedPinName, ESearchCase::IgnoreCase) &&
			!RecordName.Equals(RequestedNoGuid, ESearchCase::IgnoreCase) &&
			!RecordNameNoGuid.Equals(RequestedPinName, ESearchCase::IgnoreCase) &&
			!RecordNameNoGuid.Equals(RequestedNoGuid, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!Record.bShowPin)
		{
			Record.bShowPin = true;
			SetFieldsNode->ReconstructNode();
		}
		return;
	}
}

void ApplySetFieldsVisiblePins_ImportBpy(UK2Node_SetFieldsInStruct* SetFieldsNode, const TSet<FName>& VisiblePins)
{
	if (!SetFieldsNode)
	{
		return;
	}

	bool bChanged = false;
	for (FOptionalPinFromProperty& Record : SetFieldsNode->ShowPinForProperties)
	{
		const bool bShouldShow =
			VisiblePins.Contains(Record.PropertyName) ||
			VisiblePins.Contains(FName(*StripGuidSuffix_ImportBpy(Record.PropertyName.ToString())));
		if (Record.bShowPin != bShouldShow)
		{
			Record.bShowPin = bShouldShow;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		SetFieldsNode->ReconstructNode();
	}
}

template <typename TNodeType>
void ApplyStructMemberVisiblePins_ImportBpy(TNodeType* StructNode, const TSet<FName>& VisiblePins)
{
	if (!StructNode)
	{
		return;
	}

	bool bChanged = false;
	for (FOptionalPinFromProperty& Record : StructNode->ShowPinForProperties)
	{
		const bool bShouldShow =
			VisiblePins.Contains(Record.PropertyName) ||
			VisiblePins.Contains(FName(*StripGuidSuffix_ImportBpy(Record.PropertyName.ToString())));
		if (Record.bShowPin != bShouldShow)
		{
			Record.bShowPin = bShouldShow;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		StructNode->ReconstructNode();
	}
}

int32 FindMaxBlendListByEnumPinIndex_ImportBpy(const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!JsonObj.IsValid())
	{
		return INDEX_NONE;
	}

	int32 MaxPinIndex = INDEX_NONE;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : JsonObj->Values)
	{
		const FString& PinName = Entry.Key;
		int32 PrefixLength = INDEX_NONE;
		if (PinName.StartsWith(TEXT("BlendPose_")))
		{
			PrefixLength = FString(TEXT("BlendPose_")).Len();
		}
		else if (PinName.StartsWith(TEXT("BlendTime_")))
		{
			PrefixLength = FString(TEXT("BlendTime_")).Len();
		}

		if (PrefixLength == INDEX_NONE)
		{
			continue;
		}

		const FString IndexText = PinName.RightChop(PrefixLength);
		int32 ParsedIndex = INDEX_NONE;
		if (LexTryParseString(ParsedIndex, *IndexText) && ParsedIndex >= 0)
		{
			MaxPinIndex = FMath::Max(MaxPinIndex, ParsedIndex);
		}
	}

	return MaxPinIndex;
}

TArray<FName> GetFallbackBlendListByEnumEntries_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	UEnum* EnumObject)
{
	TArray<FName> Result;
	if (!NodeJson.IsValid() || !EnumObject)
	{
		return Result;
	}

	int32 MaxVisiblePinIndex = INDEX_NONE;

	const TSharedPtr<FJsonObject>* PinIdsObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("pin_ids"), PinIdsObj) && PinIdsObj && (*PinIdsObj).IsValid())
	{
		MaxVisiblePinIndex = FMath::Max(MaxVisiblePinIndex, FindMaxBlendListByEnumPinIndex_ImportBpy(*PinIdsObj));
	}

	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsObj) && DefaultsObj && (*DefaultsObj).IsValid())
	{
		MaxVisiblePinIndex = FMath::Max(MaxVisiblePinIndex, FindMaxBlendListByEnumPinIndex_ImportBpy(*DefaultsObj));
	}

	if (MaxVisiblePinIndex < 0)
	{
		return Result;
	}

	const int32 DesiredVisibleEntryCount = MaxVisiblePinIndex + 1;
	const int32 MaxEnumIndex = FMath::Max(0, EnumObject->NumEnums() - 1);
	for (int32 EnumIndex = 0; EnumIndex < MaxEnumIndex && Result.Num() < DesiredVisibleEntryCount; ++EnumIndex)
	{
		const FName EnumEntryName = EnumObject->GetNameByIndex(EnumIndex);
		if (!EnumEntryName.IsNone())
		{
			Result.Add(EnumEntryName);
		}
	}

	return Result;
}

bool SetBlendListByEnumVisibleEntries_ImportBpy(
	UAnimGraphNode_BlendListByEnum* BlendListByEnumNode,
	const TArray<FName>& RequestedEntries)
{
	if (!BlendListByEnumNode)
	{
		return false;
	}

	FArrayProperty* VisibleEnumEntriesProperty =
		FindFProperty<FArrayProperty>(UAnimGraphNode_BlendListByEnum::StaticClass(), TEXT("VisibleEnumEntries"));
	FNameProperty* VisibleEnumEntryProperty =
		VisibleEnumEntriesProperty ? CastField<FNameProperty>(VisibleEnumEntriesProperty->Inner) : nullptr;
	FArrayProperty* BlendPoseProperty =
		FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendPose"));

	if (!VisibleEnumEntriesProperty || !VisibleEnumEntryProperty || !BlendPoseProperty)
	{
		return false;
	}

	FScriptArrayHelper BlendPoseHelper(
		BlendPoseProperty,
		BlendPoseProperty->ContainerPtrToValuePtr<void>(&BlendListByEnumNode->Node));
	int32 CurrentPoseCount = BlendPoseHelper.Num();
	const int32 DesiredPoseCount = RequestedEntries.Num() + 1;

	while (CurrentPoseCount < DesiredPoseCount)
	{
		BlendListByEnumNode->Node.AddPose();
		++CurrentPoseCount;
	}

	while (CurrentPoseCount > DesiredPoseCount && CurrentPoseCount > 1)
	{
		BlendListByEnumNode->Node.RemovePose(CurrentPoseCount - 1);
		--CurrentPoseCount;
	}

	FScriptArrayHelper VisibleEnumEntriesHelper(
		VisibleEnumEntriesProperty,
		VisibleEnumEntriesProperty->ContainerPtrToValuePtr<void>(BlendListByEnumNode));
	VisibleEnumEntriesHelper.EmptyValues();
	VisibleEnumEntriesHelper.AddValues(RequestedEntries.Num());

	for (int32 EntryIndex = 0; EntryIndex < RequestedEntries.Num(); ++EntryIndex)
	{
		VisibleEnumEntryProperty->SetPropertyValue(
			VisibleEnumEntriesHelper.GetRawPtr(EntryIndex),
			RequestedEntries[EntryIndex]);
	}

	return true;
}

void RestoreBlendListByEnumEntries_ImportBpy(
	UAnimGraphNode_BlendListByEnum* BlendListByEnumNode,
	const TSharedPtr<FJsonObject>& NodeJson,
	const TSharedPtr<FJsonObject>& NodePropsObj,
	UEnum* EnumObject)
{
	if (!BlendListByEnumNode || !NodeJson.IsValid() || !NodePropsObj.IsValid() || !EnumObject)
	{
		return;
	}

	TArray<FName> RequestedEntries;
	FString VisibleEnumEntriesText;
	if (NodePropsObj->TryGetStringField(TEXT("VisibleEnumEntries"), VisibleEnumEntriesText) &&
		!VisibleEnumEntriesText.IsEmpty())
	{
		TArray<FString> VisibleEntryNames;
		VisibleEnumEntriesText.ParseIntoArray(VisibleEntryNames, TEXT("|"), true);
		for (const FString& VisibleEntryName : VisibleEntryNames)
		{
			if (VisibleEntryName.IsEmpty())
			{
				continue;
			}

			const int32 EnumIndex = EnumObject->GetIndexByName(FName(*VisibleEntryName));
			if (EnumIndex == INDEX_NONE)
			{
				continue;
			}

			const FName CanonicalEntryName = EnumObject->GetNameByIndex(EnumIndex);
			if (!CanonicalEntryName.IsNone() && !RequestedEntries.Contains(CanonicalEntryName))
			{
				RequestedEntries.Add(CanonicalEntryName);
			}
		}
	}

	if (RequestedEntries.Num() == 0)
	{
		RequestedEntries = GetFallbackBlendListByEnumEntries_ImportBpy(NodeJson, EnumObject);
	}

	BlendListByEnumNode->ReloadEnum(EnumObject);
	SetBlendListByEnumVisibleEntries_ImportBpy(BlendListByEnumNode, RequestedEntries);
}

bool IsEvaluateChooserNode_ImportBpy(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}

	const FString NodeClassName = Node->GetClass()->GetName();
	return NodeClassName == TEXT("K2Node_EvaluateChooser") || NodeClassName == TEXT("K2Node_EvaluateChooser2");
}

void EnsureEvaluateChooserPinsForRequest_ImportBpy(
	UEdGraphNode* Node,
	const FString& RequestedPinName,
	EEdGraphPinDirection Direction)
{
	if (!IsEvaluateChooserNode_ImportBpy(Node) || RequestedPinName.IsEmpty())
	{
		return;
	}

	const FString CanonicalPinName = StripGuidSuffix_ImportBpy(RequestedPinName);
	if (CanonicalPinName.IsEmpty())
	{
		return;
	}

	if (Node->FindPin(FName(*CanonicalPinName), Direction))
	{
		return;
	}

	if (Direction == EGPD_Input && CanonicalPinName.Equals(TEXT("AnimInstance"), ESearchCase::IgnoreCase))
	{
		Node->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UAnimInstance::StaticClass(), FName(TEXT("AnimInstance")));
		return;
	}

	if (Direction == EGPD_Output && CanonicalPinName.Equals(TEXT("Result"), ESearchCase::IgnoreCase))
	{
		if (HasEvaluateChooserAssetBound_ImportBpy(Node))
		{
			Node->ReconstructNode();
		}
		return;
	}

	if (CanonicalPinName.StartsWith(TEXT("S_"), ESearchCase::IgnoreCase))
	{
		const FString PinNameWithoutPrefix = CanonicalPinName.RightChop(2);
		if (!PinNameWithoutPrefix.IsEmpty())
		{
			if (Node->FindPin(FName(*PinNameWithoutPrefix), Direction))
			{
				return;
			}

			// Let chooser node rebuild canonical pins from the bound table instead of
			// creating synthetic legacy S_* pins that become stale after reconstruct.
			if (HasEvaluateChooserAssetBound_ImportBpy(Node))
			{
				Node->ReconstructNode();
				if (Node->FindPin(FName(*PinNameWithoutPrefix), Direction))
				{
					return;
				}
			}
		}

		return;
	}
}

void EnsureDynamicPinsForRequest_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
	EnsureEvaluateChooserPinsForRequest_ImportBpy(Node, RequestedPinName, Direction);

	if (UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
	{
		if (Direction == EGPD_Input && !RequestedPinName.IsEmpty())
		{
			if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint()))
			{
				UEdGraph* LayerGraph = nullptr;
				for (UEdGraph* CandidateGraph : CurrentBlueprint->FunctionGraphs)
				{
					if (CandidateGraph && CandidateGraph->GetFName() == LinkedLayerNode->Node.Layer)
					{
						LayerGraph = CandidateGraph;
						break;
					}
				}

				if (LayerGraph)
				{
					const FEdGraphPinType PosePinType = UAnimationGraphSchema::MakeLocalSpacePosePin();
					for (UEdGraphNode* LayerGraphNode : LayerGraph->Nodes)
					{
						UAnimGraphNode_LinkedInputPose* const LinkedInputPoseNode =
							Cast<UAnimGraphNode_LinkedInputPose>(LayerGraphNode);
						if (!LinkedInputPoseNode || LinkedInputPoseNode->Node.Name.IsNone())
						{
							continue;
						}

						if (LinkedInputPoseNode->Node.Name.ToString().Equals(RequestedPinName, ESearchCase::IgnoreCase))
						{
							if (!Node->FindPin(LinkedInputPoseNode->Node.Name, EGPD_Input))
							{
								UEdGraphPin* const NewPin =
									Node->CreatePin(EGPD_Input, PosePinType, LinkedInputPoseNode->Node.Name);
								if (NewPin)
								{
									NewPin->PinFriendlyName = FText::FromName(LinkedInputPoseNode->Node.Name);
								}
							}
							break;
						}
					}
				}
			}
		}
	}

	if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
	{
		if (Direction == EGPD_Input &&
			RequestedPinName != UEdGraphSchema_K2::PN_Execute.ToString() &&
			RequestedPinName != UEdGraphSchema_K2::PN_Then.ToString() &&
			RequestedPinName != TEXT("StructRef") &&
			RequestedPinName != TEXT("StructOut"))
		{
			EnsureSetFieldsPinVisible_ImportBpy(SetFieldsNode, RequestedPinName);
		}
	}

	if (UK2Node_ExecutionSequence* SequenceNode = Cast<UK2Node_ExecutionSequence>(Node))
	{
		if (Direction == EGPD_Output && RequestedPinName.StartsWith(TEXT("then_"), ESearchCase::IgnoreCase))
		{
			const FString RequestedIndexText = RequestedPinName.RightChop(5);
			int32 RequestedIndex = INDEX_NONE;
			if (LexTryParseString(RequestedIndex, *RequestedIndexText) && RequestedIndex >= 0)
			{
				while (!SequenceNode->GetThenPinGivenIndex(RequestedIndex))
				{
					SequenceNode->AddInputPin();
				}
			}
		}
	}

	if (UK2Node_SwitchInteger* SwitchInt = Cast<UK2Node_SwitchInteger>(Node))
	{
		if (Direction == EGPD_Output)
		{
			int32 RequestedCase = 0;
			if (LexTryParseString(RequestedCase, *RequestedPinName))
			{
				while (!SwitchInt->FindPin(FName(*RequestedPinName), EGPD_Output))
				{
					SwitchInt->AddPinToSwitchNode();
				}
			}
		}
	}

	if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
	{
		if (Direction == EGPD_Input && RequestedPinName.StartsWith(TEXT("Option ")))
		{
			while (!SelectNode->FindPin(FName(*RequestedPinName), EGPD_Input) && SelectNode->CanAddPin())
			{
				SelectNode->AddInputPin();
			}
		}
	}

	if (UK2Node_MakeContainer* MakeContainerNode = Cast<UK2Node_MakeContainer>(Node))
	{
		if (Direction == EGPD_Input &&
			RequestedPinName.Len() >= 3 &&
			RequestedPinName.StartsWith(TEXT("[")) &&
			RequestedPinName.EndsWith(TEXT("]")))
		{
			const FString RequestedIndexText = RequestedPinName.Mid(1, RequestedPinName.Len() - 2);
			int32 RequestedIndex = INDEX_NONE;
			if (LexTryParseString(RequestedIndex, *RequestedIndexText) && RequestedIndex >= 0)
			{
				int32 Guard = 0;
				const FName CanonicalPinName = MakeContainerNode->GetPinName(RequestedIndex);
				while (!MakeContainerNode->FindPin(CanonicalPinName, EGPD_Input) && Guard <= RequestedIndex + 1)
				{
					MakeContainerNode->AddInputPin();
					++Guard;
				}
			}
		}
	}

	if (Direction == EGPD_Input && RequestedPinName.Len() == 1)
	{
		const TCHAR RequestedChar = FChar::ToUpper(RequestedPinName[0]);
		if (RequestedChar >= TCHAR('A') && RequestedChar <= TCHAR('Z'))
		{
			if (IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(Node))
			{
				const FString CanonicalPinName(1, &RequestedChar);
				while (!Node->FindPin(FName(*CanonicalPinName), EGPD_Input) && AddPinNode->CanAddPin())
				{
					AddPinNode->AddInputPin();
				}
			}
		}
	}
}


UEdGraphPin* FindEvaluateChooserContextPinForCurrentImport_ImportBpy(
	UEdGraphNode* Node,
	const FString& RequestedPinName,
	EEdGraphPinDirection Direction)
{
	if (!IsEvaluateChooserNode_ImportBpy(Node) || Direction != EGPD_Input)
	{
		return nullptr;
	}

	const FString SourceClassName = BuildGeneratedClassObjectNameFromBlueprintPath_ImportBpy(
		GCurrentImportSourceBlueprintPath_ImportBpy);
	const FString TargetClassName = BuildGeneratedClassObjectNameFromBlueprintPath_ImportBpy(
		GCurrentImportTargetBlueprintPath_ImportBpy);
	if (SourceClassName.IsEmpty() || TargetClassName.IsEmpty())
	{
		return nullptr;
	}

	const FString RequestedNoGuid = StripGuidSuffix_ImportBpy(RequestedPinName);
	if (!RequestedNoGuid.Equals(SourceClassName, ESearchCase::IgnoreCase) &&
		!RequestedNoGuid.Equals(FString(TEXT("S_")) + SourceClassName, ESearchCase::IgnoreCase))
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input)
		{
			continue;
		}

		const FString PinNameNoGuid = StripGuidSuffix_ImportBpy(Pin->PinName.ToString());
		const FString FriendlyName = Pin->PinFriendlyName.ToString();
		const bool bNameMatchesTarget =
			PinNameNoGuid.Equals(TargetClassName, ESearchCase::IgnoreCase) ||
			PinNameNoGuid.Equals(FString(TEXT("S_")) + TargetClassName, ESearchCase::IgnoreCase) ||
			FriendlyName.Equals(TargetClassName, ESearchCase::IgnoreCase) ||
			FriendlyName.Equals(FString(TEXT("S_")) + TargetClassName, ESearchCase::IgnoreCase);
		const UClass* PinClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
		const bool bTypeMatchesTarget =
			PinClass &&
			!GCurrentImportTargetBlueprintPath_ImportBpy.IsEmpty() &&
			PinClass->GetPathName().Equals(
				BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(GCurrentImportTargetBlueprintPath_ImportBpy),
				ESearchCase::CaseSensitive);
		if (bNameMatchesTarget || bTypeMatchesTarget)
		{
			return Pin;
		}
	}

	return nullptr;
}
UEdGraphPin* FindExistingPinFlexible_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(Node, RequestedPinName);
	if (UEdGraphPin* RetargetedChooserPin = FindEvaluateChooserContextPinForCurrentImport_ImportBpy(Node, NormalizedRequested, Direction))
	{
		return RetargetedChooserPin;
	}

	TArray<FString> CandidateNames;
	CandidateNames.Add(NormalizedRequested);
	if (Node->IsA<UK2Node_MacroInstance>())
	{
		if (NormalizedRequested.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
		{
			CandidateNames.Add(TEXT("exec"));
		}
		else if (NormalizedRequested.Equals(TEXT("exec"), ESearchCase::IgnoreCase))
		{
			CandidateNames.Add(TEXT("execute"));
		}
	}
	if (IsEvaluateChooserNode_ImportBpy(Node))
	{
		if (NormalizedRequested.StartsWith(TEXT("S_"), ESearchCase::IgnoreCase) && NormalizedRequested.Len() > 2)
		{
			CandidateNames.Add(NormalizedRequested.RightChop(2));
		}
		else if (!NormalizedRequested.IsEmpty())
		{
			CandidateNames.Add(FString(TEXT("S_")) + NormalizedRequested);
		}
	}
	if (UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node))
	{
		const bool bIsExitTunnel = TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs;
		const bool bIsEntryTunnel = TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs;

		if (bIsExitTunnel && Direction == EGPD_Input)
		{
			if (NormalizedRequested.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
			{
				CandidateNames.Add(TEXT("then"));
			}
			else if (NormalizedRequested.Equals(TEXT("then"), ESearchCase::IgnoreCase))
			{
				CandidateNames.Add(TEXT("execute"));
			}
		}
		else if (bIsEntryTunnel && Direction == EGPD_Output)
		{
			if (NormalizedRequested.Equals(TEXT("then"), ESearchCase::IgnoreCase))
			{
				CandidateNames.Add(TEXT("execute"));
			}
		}
	}

	for (const FString& CandidateName : CandidateNames)
	{
		if (UEdGraphPin* ExactPin = Node->FindPin(FName(*CandidateName), Direction))
		{
			return ExactPin;
		}
	}

	const FString RequestedNoGuid = StripGuidSuffix_ImportBpy(NormalizedRequested);
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != Direction)
		{
			continue;
		}

		const FString PinName = Pin->PinName.ToString();
		if (PinName.Equals(NormalizedRequested, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
		for (const FString& CandidateName : CandidateNames)
		{
			if (PinName.Equals(CandidateName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		if (StripGuidSuffix_ImportBpy(PinName).Equals(RequestedNoGuid, ESearchCase::IgnoreCase))
		{
			return Pin;
		}

		const FString FriendlyName = Pin->PinFriendlyName.ToString();
		bool bFriendlyMatch = !FriendlyName.IsEmpty() &&
			(FriendlyName.Equals(NormalizedRequested, ESearchCase::IgnoreCase) ||
			 FriendlyName.Equals(RequestedNoGuid, ESearchCase::IgnoreCase));
		if (!bFriendlyMatch)
		{
			for (const FString& CandidateName : CandidateNames)
			{
				if (!FriendlyName.IsEmpty() && FriendlyName.Equals(CandidateName, ESearchCase::IgnoreCase))
				{
					bFriendlyMatch = true;
					break;
				}
			}
		}
		if (bFriendlyMatch)
		{
			return Pin;
		}
	}

	return nullptr;
}

bool EnsureSplitPinsForRequest_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
	if (!Node || !RequestedPinName.Contains(TEXT("_")))
	{
		return false;
	}

	const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(Node->GetSchema());
	if (!K2Schema)
	{
		return false;
	}

	const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(Node, RequestedPinName);
	if (FindExistingPinFlexible_ImportBpy(Node, NormalizedRequested, Direction))
	{
		return true;
	}

	TArray<FString> Parts;
	NormalizedRequested.ParseIntoArray(Parts, TEXT("_"), true);
	if (Parts.Num() < 2)
	{
		return false;
	}

	for (int32 PrefixLength = Parts.Num() - 1; PrefixLength >= 1; --PrefixLength)
	{
		FString ParentName = Parts[0];
		for (int32 Index = 1; Index < PrefixLength; ++Index)
		{
			ParentName += TEXT("_") + Parts[Index];
		}

		UEdGraphPin* ParentPin = FindExistingPinFlexible_ImportBpy(Node, ParentName, Direction);
		if (!ParentPin || !K2Schema->CanSplitStructPin(*ParentPin))
		{
			continue;
		}

		K2Schema->SplitPin(ParentPin, false);
		if (FindExistingPinFlexible_ImportBpy(Node, NormalizedRequested, Direction))
		{
			return true;
		}

		return EnsureSplitPinsForRequest_ImportBpy(Node, NormalizedRequested, Direction);
	}

	return false;
}

UEdGraphPin* FindPinFlexible_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(Node, RequestedPinName);
	EnsureDynamicPinsForRequest_ImportBpy(Node, NormalizedRequested, Direction);

	if (UEdGraphPin* ExactPin = FindExistingPinFlexible_ImportBpy(Node, NormalizedRequested, Direction))
	{
		return ExactPin;
	}

	if (EnsureSplitPinsForRequest_ImportBpy(Node, NormalizedRequested, Direction))
	{
		return FindExistingPinFlexible_ImportBpy(Node, NormalizedRequested, Direction);
	}

	return FindExistingPinFlexible_ImportBpy(Node, NormalizedRequested, Direction);
}

FString ResolveNodePinAlias_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson, const FString& PinName);

UEdGraphPin* FindSerializedPinOnNode_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	const FString& SerializedPinName,
	EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	const FString AliasResolvedPinName = ResolveNodePinAlias_ImportBpy(NodeJson, SerializedPinName);
	if (!AliasResolvedPinName.IsEmpty())
	{
		if (UEdGraphPin* Pin = FindPinFlexible_ImportBpy(Node, AliasResolvedPinName, Direction))
		{
			return Pin;
		}
	}

	if (!SerializedPinName.Equals(AliasResolvedPinName, ESearchCase::CaseSensitive))
	{
		// Alias-resolved names are often opaque internal pin names. Preserve the
		// original DSL pin name as a structural hint so split pins like B_X/B_Y/B_Z
		// can be materialized before we look up their actual runtime names.
		EnsureDynamicPinsForRequest_ImportBpy(Node, SerializedPinName, Direction);
		EnsureSplitPinsForRequest_ImportBpy(Node, SerializedPinName, Direction);

		if (!AliasResolvedPinName.IsEmpty())
		{
			if (UEdGraphPin* Pin = FindPinFlexible_ImportBpy(Node, AliasResolvedPinName, Direction))
			{
				return Pin;
			}
		}
	}

	return FindPinFlexible_ImportBpy(Node, SerializedPinName, Direction);
}

FString NormalizeGuidForTracking_ImportBpy(const FString& GuidText)
{
	FString Normalized = GuidText;
	Normalized.TrimStartAndEndInline();
	Normalized.ReplaceInline(TEXT("-"), TEXT(""));
	Normalized.ToUpperInline();
	return Normalized;
}

bool IsPromotableTraceEnabled_ImportBpy()
{
	static const FString EnvValue =
		FPlatformMisc::GetEnvironmentVariable(TEXT("EXPORTBPY_TRACE_PROMOTABLE_IMPORT"));
	return EnvValue.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("yes"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("on"), ESearchCase::IgnoreCase);
}

bool IsConnectionTraceEnabled_ImportBpy()
{
	static const FString EnvValue =
		FPlatformMisc::GetEnvironmentVariable(TEXT("EXPORTBPY_TRACE_CONNECTION_IMPORT"));
	return EnvValue.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("yes"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("on"), ESearchCase::IgnoreCase);
}

bool IsTrackedTraversalConnection_ImportBpy(const FString& SrcUid, const FString& DstUid)
{
	const FString SrcGuid = NormalizeGuidForTracking_ImportBpy(SrcUid);
	const FString DstGuid = NormalizeGuidForTracking_ImportBpy(DstUid);

	return
		(SrcGuid == TEXT("21A313E74ECF345881D695A0E01EF5C1") && DstGuid == TEXT("704CEB6C48B936A1AB62BAA12C1C11CC")) ||
		(SrcGuid == TEXT("2D1BE5F04CC49B225D3A6DBEDE484870") && DstGuid == TEXT("9488168A47D712B10795B49BB0910A69")) ||
		(SrcGuid == TEXT("2C7F4536451C956EFBFE379EC7FE82FF") && DstGuid == TEXT("F4B688874906C3B912AF3988FB783B4D"));
}

bool IsTrackedTraversalGuid_ImportBpy(const FString& GuidText)
{
	const FString Normalized = NormalizeGuidForTracking_ImportBpy(GuidText);
	return Normalized == TEXT("21A313E74ECF345881D695A0E01EF5C1") ||
		Normalized == TEXT("2D1BE5F04CC49B225D3A6DBEDE484870") ||
		Normalized == TEXT("2C7F4536451C956EFBFE379EC7FE82FF") ||
		Normalized == TEXT("704CEB6C48B936A1AB62BAA12C1C11CC") ||
		Normalized == TEXT("9488168A47D712B10795B49BB0910A69") ||
		Normalized == TEXT("F4B688874906C3B912AF3988FB783B4D");
}

bool IsTrackedTraversalNodeJson_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	FString* OutMatchedGuid = nullptr)
{
	if (!NodeJson.IsValid())
	{
		return false;
	}

	FString GuidCandidate;
	if (NodeJson->TryGetStringField(TEXT("node_guid"), GuidCandidate) &&
		IsTrackedTraversalGuid_ImportBpy(GuidCandidate))
	{
		if (OutMatchedGuid)
		{
			*OutMatchedGuid = NormalizeGuidForTracking_ImportBpy(GuidCandidate);
		}
		return true;
	}

	if (NodeJson->TryGetStringField(TEXT("uid"), GuidCandidate) &&
		IsTrackedTraversalGuid_ImportBpy(GuidCandidate))
	{
		if (OutMatchedGuid)
		{
			*OutMatchedGuid = NormalizeGuidForTracking_ImportBpy(GuidCandidate);
		}
		return true;
	}

	return false;
}

bool IsTrackedTraversalNode_ImportBpy(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}

	return IsTrackedTraversalGuid_ImportBpy(Node->NodeGuid.ToString(EGuidFormats::Digits));
}

FString DescribeTrackedPinState_ImportBpy(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("<missing>");
	}

	const FString DefaultObjectPath = Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT("");
	return FString::Printf(
		TEXT("name=%s type=%s links=%d default='%s' auto='%s' obj='%s' ignored=%d"),
		*Pin->GetName(),
		*DescribePinType_ImportBpy(Pin->PinType),
		Pin->LinkedTo.Num(),
		*Pin->DefaultValue,
		*Pin->AutogeneratedDefaultValue,
		*DefaultObjectPath,
		Pin->bDefaultValueIsIgnored ? 1 : 0);
}

void LogTrackedPromotableNodeState_ImportBpy(
	const TCHAR* PhaseLabel,
	UK2Node_CallFunction* CallNode,
	const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!IsPromotableTraceEnabled_ImportBpy())
	{
		return;
	}

	if (!CallNode)
	{
		return;
	}

	FString TrackedGuid;
	const bool bTrackedFromJson = IsTrackedTraversalNodeJson_ImportBpy(NodeJson, &TrackedGuid);
	if (!bTrackedFromJson && !IsTrackedTraversalNode_ImportBpy(CallNode))
	{
		return;
	}

	if (TrackedGuid.IsEmpty())
	{
		TrackedGuid = NormalizeGuidForTracking_ImportBpy(CallNode->NodeGuid.ToString(EGuidFormats::Digits));
	}

	FString FunctionRef;
	if (NodeJson.IsValid())
	{
		NodeJson->TryGetStringField(TEXT("function_ref"), FunctionRef);
	}

	const UFunction* TargetFunction = CallNode->GetTargetFunction();
	const FString TargetFunctionPath = TargetFunction ? TargetFunction->GetPathName() : TEXT("<null>");
	const FString NodeTitle = CallNode->GetNodeTitle(ENodeTitleType::ListView).ToString();

	UEdGraphPin* PinA = NodeJson.IsValid()
		? FindSerializedPinOnNode_ImportBpy(CallNode, NodeJson, TEXT("A"), EGPD_Input)
		: FindPinFlexible_ImportBpy(CallNode, TEXT("A"), EGPD_Input);
	UEdGraphPin* PinB = NodeJson.IsValid()
		? FindSerializedPinOnNode_ImportBpy(CallNode, NodeJson, TEXT("B"), EGPD_Input)
		: FindPinFlexible_ImportBpy(CallNode, TEXT("B"), EGPD_Input);
	UEdGraphPin* ReturnPin = NodeJson.IsValid()
		? FindSerializedPinOnNode_ImportBpy(CallNode, NodeJson, TEXT("ReturnValue"), EGPD_Output)
		: FindPinFlexible_ImportBpy(CallNode, TEXT("ReturnValue"), EGPD_Output);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][PromotableTrace][%s] guid=%s node=%s class=%s title='%s' function_ref='%s' target='%s' A={%s} B={%s} Return={%s}"),
		PhaseLabel ? PhaseLabel : TEXT("<unknown>"),
		*TrackedGuid,
		*DescribeNode_ImportBpy(CallNode),
		*CallNode->GetClass()->GetName(),
		*NodeTitle,
		*FunctionRef,
		*TargetFunctionPath,
		*DescribeTrackedPinState_ImportBpy(PinA),
		*DescribeTrackedPinState_ImportBpy(PinB),
		*DescribeTrackedPinState_ImportBpy(ReturnPin));
}

void LogTrackedPromotableNodesFromMap_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const TCHAR* PhaseLabel)
{
	if (!IsPromotableTraceEnabled_ImportBpy())
	{
		return;
	}

	if (!NodesArr)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		if (!IsTrackedTraversalNodeJson_ImportBpy(NodeObj))
		{
			continue;
		}

		FString Uid;
		NodeObj->TryGetStringField(TEXT("uid"), Uid);
		UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(*ExistingNode))
		{
			LogTrackedPromotableNodeState_ImportBpy(PhaseLabel, CallNode, NodeObj);
		}
	}
}

UEdGraphPin* FindPinById_ImportBpy(UEdGraphNode* Node, const FString& PinIdText)
{
	if (!Node)
	{
		return nullptr;
	}

	FGuid PinGuid;
	if (!TryParseGuid_ImportBpy(PinIdText, PinGuid))
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinId == PinGuid)
		{
			return Pin;
		}
	}

	return nullptr;
}

FString ResolveNodePinAlias_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson, const FString& PinName)
{
	if (!NodeJson.IsValid())
	{
		return PinName;
	}

	const TSharedPtr<FJsonObject>* PinAliasesObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("pin_aliases"), PinAliasesObj) || !PinAliasesObj->IsValid())
	{
		return PinName;
	}

	FString FullPinName;
	if ((*PinAliasesObj)->TryGetStringField(PinName, FullPinName) && !FullPinName.IsEmpty())
	{
		return FullPinName;
	}

	return PinName;
}

void ApplyJsonValueToProperty_ImportBpy(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value)
{
	if (!Object || !Property || !Value.IsValid())
	{
		return;
	}

	void* PropertyAddress = Property->ContainerPtrToValuePtr<void>(Object);
	if (!PropertyAddress)
	{
		return;
	}

	int32 PortFlags = PPF_None;
	const bool bCanUseInstanceSubobjects =
		CastField<FObjectPropertyBase>(Property) ||
		CastField<FArrayProperty>(Property) ||
		CastField<FSetProperty>(Property) ||
		CastField<FMapProperty>(Property);
	if (bCanUseInstanceSubobjects &&
		Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference | CPF_InstancedReference))
	{
		PortFlags |= PPF_InstanceSubobjects;
	}

	if (Value->Type == EJson::String &&
		!CastField<FStrProperty>(Property) &&
		!CastField<FTextProperty>(Property) &&
		!CastField<FNameProperty>(Property) &&
		!CastField<FObjectPropertyBase>(Property) &&
		!CastField<FSoftObjectProperty>(Property))
	{
		const FString TextValue =
			RemapBlueprintReferencesInSerializedText_ImportBpy(Value->AsString());
		Property->ImportText_Direct(*TextValue, PropertyAddress, Object, PortFlags);
		return;
	}

	if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		const FString ReferenceText =
			RemapBlueprintReferenceForCurrentImport_ImportBpy(Value->AsString(), true);
		UClass* ClassValue = ResolveNamedObject_ImportBpy<UClass>(ReferenceText);
		ClassProperty->SetObjectPropertyValue(PropertyAddress, ClassValue);
		return;
	}

	if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
	{
		if (FSoftObjectPtr* SoftClassValue = SoftClassProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Object))
		{
			const FString ReferenceText =
				RemapBlueprintReferenceForCurrentImport_ImportBpy(Value->AsString(), true);
			*SoftClassValue = FSoftObjectPath(ReferenceText);
		}
		return;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		const FString ReferenceText =
			RemapBlueprintReferenceForCurrentImport_ImportBpy(Value->AsString(), false);
		UObject* ObjectValue = ResolveNamedObject_ImportBpy<UObject>(ReferenceText);
		ObjectProperty->SetObjectPropertyValue(PropertyAddress, ObjectValue);
		return;
	}

	if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		if (FSoftObjectPtr* SoftObjectValue = SoftObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Object))
		{
			const FString ReferenceText =
				RemapBlueprintReferenceForCurrentImport_ImportBpy(Value->AsString(), false);
			*SoftObjectValue = FSoftObjectPath(ReferenceText);
		}
		return;
	}

	if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		NameProperty->SetPropertyValue(PropertyAddress, FName(*Value->AsString()));
		return;
	}

	if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		StringProperty->SetPropertyValue(PropertyAddress, Value->AsString());
		return;
	}

	if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		TextProperty->SetPropertyValue(PropertyAddress, FText::FromString(Value->AsString()));
		return;
	}

	if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		BoolProperty->SetPropertyValue(PropertyAddress, Value->AsBool());
		return;
	}

	if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
	{
		IntProperty->SetPropertyValue(PropertyAddress, static_cast<int32>(Value->AsNumber()));
		return;
	}

	if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
	{
		FloatProperty->SetPropertyValue(PropertyAddress, static_cast<float>(Value->AsNumber()));
		return;
	}

	if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
	{
		DoubleProperty->SetPropertyValue(PropertyAddress, Value->AsNumber());
		return;
	}

	// Fallback: use ImportText for structs and other complex types (FRotator, FVector, etc.)
	{
		FString TextValue = Value->AsString();
		if (!TextValue.IsEmpty())
		{
			Property->ImportText_Direct(*TextValue, PropertyAddress, Object, PortFlags);
		}
	}
}

void ApplyJsonObjectToObject_ImportBpy(UObject* Object, const TSharedPtr<FJsonObject>& PropertiesJson)
{
	if (!Object || !PropertiesJson.IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : PropertiesJson->Values)
	{
		if (!Entry.Value.IsValid())
		{
			continue;
		}

		if (FProperty* Property = FindPropertyByNameOrAlias_ImportBpy(Object, Entry.Key))
		{
			ApplyJsonValueToProperty_ImportBpy(Object, Property, Entry.Value);
		}
	}
}

bool ShouldSkipStandaloneAssetProperty_ImportBpy(const UObject* Asset, const FString& PropertyName)
{
	if (!Asset || PropertyName.IsEmpty())
	{
		return false;
	}

	if (IsChooserTableAsset_ImportBpy(Asset))
	{
		return PropertyName == TEXT("FallbackResult") ||
			PropertyName == TEXT("ResultsStructs") ||
			PropertyName == TEXT("ColumnsStructs") ||
			PropertyName == TEXT("DisabledRows");
	}

	if (Asset->IsA(UInputMappingContext::StaticClass()))
	{
		return PropertyName == TEXT("DefaultKeyMappings") ||
			PropertyName == TEXT("Mappings") ||
			PropertyName == TEXT("MappingProfileOverrides");
	}

	if (Asset->IsA(UInputAction::StaticClass()))
	{
		return PropertyName == TEXT("Triggers") ||
			PropertyName == TEXT("Modifiers");
	}

	return false;
}

void ApplyStandaloneAssetProperties_ImportBpy(UObject* Asset, const TSharedPtr<FJsonObject>& PropertiesJson)
{
	if (!Asset || !PropertiesJson.IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : PropertiesJson->Values)
	{
		if (!Entry.Value.IsValid() || ShouldSkipStandaloneAssetProperty_ImportBpy(Asset, Entry.Key))
		{
			continue;
		}

		if (FProperty* Property = FindPropertyByNameOrAlias_ImportBpy(Asset, Entry.Key))
		{
			ApplyJsonValueToProperty_ImportBpy(Asset, Property, Entry.Value);
		}
	}
}

bool ApplyPrePinNodeProperties_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	std::initializer_list<const TCHAR*> PropertyNames,
	FString& OutError)
{
	if (!Node || !NodeJson.IsValid())
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
	{
		return true;
	}

	for (const TCHAR* PropertyName : PropertyNames)
	{
		if (!PropertyName)
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* JsonValue = (*NodePropsObj)->Values.Find(PropertyName);
		if (!JsonValue || !JsonValue->IsValid())
		{
			continue;
		}

		FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
		if (!Property)
		{
			if (FCString::Strcmp(PropertyName, TEXT("Node")) == 0 &&
				Node->GetClass()->GetName().StartsWith(TEXT("AnimGraphNode_")))
			{
				continue;
			}

			OutError = FString::Printf(
				TEXT("Node %s does not expose pre-pin property '%s'"),
				*DescribeNode_ImportBpy(Node),
				PropertyName);
			return false;
		}

		ApplyJsonValueToProperty_ImportBpy(Node, Property, *JsonValue);
	}

	return true;
}

UEdGraphNode* CreateResolvedNodeWithDefaultPins_ImportBpy(
	UEdGraph* Graph,
	const FString& NodeClassName,
	const TSharedPtr<FJsonObject>& NodeJson,
	std::initializer_list<const TCHAR*> PrePinProperties,
	FString& OutError,
	bool bCallPostPlacedNewNode = true)
{
	if (!Graph)
	{
		OutError = TEXT("Cannot create node without graph");
		return nullptr;
	}

	UClass* const NodeClass = ResolveNodeClass_ImportBpy(NodeClassName);
	if (!NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Cannot resolve node class '%s'"), *NodeClassName);
		return nullptr;
	}

	UEdGraphNode* const Node = NewObject<UEdGraphNode>(Graph, NodeClass);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Failed to allocate node class '%s'"), *NodeClassName);
		return nullptr;
	}

	Node->CreateNewGuid();
	if (bCallPostPlacedNewNode)
	{
		Node->PostPlacedNewNode();
	}
	Graph->AddNode(Node, false, false);

	if (!ApplyPrePinNodeProperties_ImportBpy(Node, NodeJson, PrePinProperties, OutError))
	{
		return nullptr;
	}

	if (IsGetSubsystemNode_ImportBpy(Node))
	{
		if (!ApplyGetSubsystemClassToNode_ImportBpy(Node, NodeJson, OutError))
		{
			return nullptr;
		}
	}

	if (!PrepareResolvedNodeForDefaultPins_ImportBpy(Node, OutError))
	{
		return nullptr;
	}

	Node->AllocateDefaultPins();

	if (IsGetSubsystemNode_ImportBpy(Node))
	{
		if (!ApplyGetSubsystemClassToNode_ImportBpy(Node, NodeJson, OutError))
		{
			return nullptr;
		}
	}

	return Node;
}

bool PrepareResolvedNodeForDefaultPins_ImportBpy(UEdGraphNode* Node, FString& OutError)
{
	if (!Node)
	{
		return true;
	}

	auto RestoreLinkedAnimLayerInterfaceFromLayer = [](UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode) -> bool
	{
		if (!LinkedLayerNode || LinkedLayerNode->Node.Layer == NAME_None)
		{
			return true;
		}

		UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint());
		if (!CurrentBlueprint)
		{
			return false;
		}

		LinkedLayerNode->Node.Interface = nullptr;
		LinkedLayerNode->InterfaceGuid.Invalidate();
		const FName LayerName = LinkedLayerNode->Node.Layer;
		UFunction* InterfaceFunction = nullptr;
		if (UClass* InterfaceClass = GetInterfaceOwnerClass_ImportBpy(
				CurrentBlueprint,
				LayerName.ToString(),
				&InterfaceFunction))
		{
			LinkedLayerNode->Node.Interface = InterfaceClass;
			if (InterfaceFunction)
			{
				LinkedLayerNode->InterfaceGuid =
					FBlueprintEditorUtils::FindInterfaceFunctionGuid(
						InterfaceFunction,
						InterfaceClass);
			}
		}

		if (LinkedLayerNode->Node.Interface.Get() == nullptr)
		{
			for (FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
			{
				for (UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
				{
					if (InterfaceGraph && InterfaceGraph->GetFName() == LayerName)
					{
						LinkedLayerNode->Node.Interface = InterfaceDesc.Interface;
						LinkedLayerNode->InterfaceGuid = InterfaceGraph->InterfaceGuid;
						break;
					}
				}

				if (LinkedLayerNode->Node.Interface.Get() != nullptr)
				{
					break;
				}
			}
		}

		if (LinkedLayerNode->Node.Interface.Get() == nullptr)
		{
			LinkedLayerNode->Node.InstanceClass = nullptr;
			return false;
		}

		return true;
	};

	if (UAnimGraphNode_LinkedAnimLayer* const LinkedLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
	{
		RestoreLinkedAnimLayerInterfaceFromLayer(LinkedLayerNode);
	}

	UAnimGraphNode_LinkedAnimGraphBase* const LinkedAnimNode = Cast<UAnimGraphNode_LinkedAnimGraphBase>(Node);
	if (!LinkedAnimNode)
	{
		return true;
	}

	const FAnimNode_LinkedAnimGraph* const RuntimeNode = LinkedAnimNode->GetLinkedAnimGraphNode();
	if (!RuntimeNode)
	{
		return true;
	}

	const FName FunctionName = RuntimeNode->GetDynamicLinkFunctionName();
	if (FunctionName == NAME_None)
	{
		return true;
	}

	FStructProperty* const FunctionReferenceProperty =
		FindFProperty<FStructProperty>(LinkedAnimNode->GetClass(), TEXT("FunctionReference"));
	if (!FunctionReferenceProperty || FunctionReferenceProperty->Struct != FMemberReference::StaticStruct())
	{
		OutError = FString::Printf(
			TEXT("Linked anim node %s does not expose FunctionReference"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	FMemberReference* const FunctionReference =
		FunctionReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(LinkedAnimNode);
	if (!FunctionReference)
	{
		OutError = FString::Printf(
			TEXT("Cannot access FunctionReference on linked anim node %s"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	if (UClass* TargetClass = LinkedAnimNode->GetTargetSkeletonClass())
	{
		UClass* const MostUpToDateClass = FBlueprintEditorUtils::GetMostUpToDateClass(TargetClass);
		FGuid FunctionGuid;
		FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
			MostUpToDateClass ? MostUpToDateClass : TargetClass,
			FunctionName,
			FunctionGuid);

		if (FunctionGuid.IsValid())
		{
			FunctionReference->SetExternalMember(FunctionName, TargetClass, FunctionGuid);
		}
		else
		{
			FunctionReference->SetExternalMember(FunctionName, TargetClass);
		}
	}
	else
	{
		FunctionReference->SetSelfMember(FunctionName);
	}

	return true;
}

ETunnelKind_ImportBpy InferTunnelKind_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return ETunnelKind_ImportBpy::Unknown;
	}

	FString TunnelType;
	if (NodeJson->TryGetStringField(TEXT("tunnel_type"), TunnelType))
	{
		if (TunnelType == TEXT("entry"))
		{
			return ETunnelKind_ImportBpy::Entry;
		}
		if (TunnelType == TEXT("exit"))
		{
			return ETunnelKind_ImportBpy::Exit;
		}
	}

	const TSharedPtr<FJsonObject>* PinIdsObj = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("pin_ids"), PinIdsObj) && PinIdsObj && PinIdsObj->IsValid())
	{
		if ((*PinIdsObj)->Values.Contains(TEXT("execute")))
		{
			return ETunnelKind_ImportBpy::Entry;
		}
		if ((*PinIdsObj)->Values.Contains(TEXT("then")))
		{
			return ETunnelKind_ImportBpy::Exit;
		}
	}

	const TSharedPtr<FJsonObject>* InputPinTypesObj = nullptr;
	const bool bHasSerializedInputPins =
		NodeJson->TryGetObjectField(TEXT("input_pin_types"), InputPinTypesObj) &&
		InputPinTypesObj &&
		InputPinTypesObj->IsValid() &&
		(*InputPinTypesObj)->Values.Num() > 0;

	const TSharedPtr<FJsonObject>* OutputPinTypesObj = nullptr;
	const bool bHasSerializedOutputPins =
		NodeJson->TryGetObjectField(TEXT("output_pin_types"), OutputPinTypesObj) &&
		OutputPinTypesObj &&
		OutputPinTypesObj->IsValid() &&
		(*OutputPinTypesObj)->Values.Num() > 0;

	if (bHasSerializedOutputPins && !bHasSerializedInputPins)
	{
		return ETunnelKind_ImportBpy::Entry;
	}
	if (bHasSerializedInputPins && !bHasSerializedOutputPins)
	{
		return ETunnelKind_ImportBpy::Exit;
	}

	return ETunnelKind_ImportBpy::Unknown;
}

FString ExtractRelativeStandaloneSubobjectGate_ImportBpy(const FString& Gate, const FString& FallbackName)
{
	FString RelativeGate = Gate.TrimStartAndEnd();

	int32 ColonIndex = INDEX_NONE;
	if (RelativeGate.FindLastChar(TEXT(':'), ColonIndex) && ColonIndex + 1 < RelativeGate.Len())
	{
		RelativeGate = RelativeGate.Mid(ColonIndex + 1);
	}
	else
	{
		int32 DotIndex = INDEX_NONE;
		if (RelativeGate.FindLastChar(TEXT('.'), DotIndex) && DotIndex + 1 < RelativeGate.Len())
		{
			RelativeGate = RelativeGate.Mid(DotIndex + 1);
		}
	}

	RelativeGate.TrimStartAndEndInline();
	return RelativeGate.IsEmpty() ? FallbackName : RelativeGate;
}

FString GetParentRelativeStandaloneSubobjectGate_ImportBpy(const FString& RelativeGate)
{
	int32 DotIndex = INDEX_NONE;
	if (RelativeGate.FindLastChar(TEXT('.'), DotIndex))
	{
		return RelativeGate.Left(DotIndex);
	}

	return FString();
}

int32 GetStandaloneSubobjectGateDepth_ImportBpy(const FString& RelativeGate)
{
	if (RelativeGate.IsEmpty())
	{
		return 0;
	}

	int32 Depth = 1;
	for (const TCHAR Character : RelativeGate)
	{
		if (Character == TEXT('.'))
		{
			++Depth;
		}
	}

	return Depth;
}

bool RecreateStandaloneAssetSubobjects_ImportBpy(
	UObject* Asset,
	const TArray<TSharedPtr<FJsonValue>>* SubobjectValues,
	FString& OutError)
{
	if (!Asset || !SubobjectValues)
	{
		return true;
	}

	struct FStandaloneSubobjectImport_ImportBpy
	{
		FString Name;
		FString RelativeGate;
		FString ParentRelativeGate;
		int32 Depth = 0;
		UClass* Class = nullptr;
		TSharedPtr<FJsonObject> Json;
		UObject* Instance = nullptr;
	};

	TArray<FStandaloneSubobjectImport_ImportBpy> ParsedSubobjects;
	ParsedSubobjects.Reserve(SubobjectValues->Num());

	TSet<FString> DesiredSubobjectGates;
	TSet<UClass*> DesiredSubobjectClasses;

	for (const TSharedPtr<FJsonValue>& SubobjectValue : *SubobjectValues)
	{
		if (!SubobjectValue.IsValid() || SubobjectValue->Type != EJson::Object)
		{
			OutError = TEXT("Standalone asset subobject entry must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> SubobjectJson = SubobjectValue->AsObject();
		FString SubobjectName;
		FString SubobjectGate;
		FString SubobjectClassPath;
		SubobjectJson->TryGetStringField(TEXT("name"), SubobjectName);
		SubobjectJson->TryGetStringField(TEXT("gate"), SubobjectGate);
		SubobjectJson->TryGetStringField(TEXT("class"), SubobjectClassPath);
		if (SubobjectName.IsEmpty() || SubobjectClassPath.IsEmpty())
		{
			OutError = TEXT("Standalone asset subobject entry is missing name or class");
			return false;
		}

		UClass* SubobjectClass = ResolveNamedObject_ImportBpy<UClass>(SubobjectClassPath);
		if (!SubobjectClass)
		{
			OutError = FString::Printf(TEXT("Cannot load standalone subobject class: %s"), *SubobjectClassPath);
			return false;
		}

		FStandaloneSubobjectImport_ImportBpy& Parsed = ParsedSubobjects.AddDefaulted_GetRef();
		Parsed.Name = SubobjectName;
		Parsed.RelativeGate =
			ExtractRelativeStandaloneSubobjectGate_ImportBpy(SubobjectGate, SubobjectName);
		Parsed.ParentRelativeGate =
			GetParentRelativeStandaloneSubobjectGate_ImportBpy(Parsed.RelativeGate);
		Parsed.Depth = GetStandaloneSubobjectGateDepth_ImportBpy(Parsed.RelativeGate);
		Parsed.Class = SubobjectClass;
		Parsed.Json = SubobjectJson;

		DesiredSubobjectGates.Add(Parsed.RelativeGate);
		DesiredSubobjectClasses.Add(SubobjectClass);
	}

	ParsedSubobjects.Sort([](
		const FStandaloneSubobjectImport_ImportBpy& A,
		const FStandaloneSubobjectImport_ImportBpy& B)
	{
		if (A.Depth != B.Depth)
		{
			return A.Depth < B.Depth;
		}

		return A.RelativeGate < B.RelativeGate;
	});

	TArray<UObject*> ExistingSubobjects;
	GetObjectsWithOuter(Asset, ExistingSubobjects, /*bIncludeNestedObjects=*/ true);
	for (UObject* ExistingSubobject : ExistingSubobjects)
	{
		if (!ExistingSubobject ||
			ExistingSubobject->HasAnyFlags(RF_Transient | RF_ClassDefaultObject))
		{
			continue;
		}

		const FString ExistingRelativeGate =
			ExtractRelativeStandaloneSubobjectGate_ImportBpy(
				ExistingSubobject->GetPathName(),
				ExistingSubobject->GetName());
		if (DesiredSubobjectGates.Contains(ExistingRelativeGate))
		{
			continue;
		}

		if (!DesiredSubobjectClasses.Contains(ExistingSubobject->GetClass()))
		{
			continue;
		}

		ExistingSubobject->Rename(
			nullptr,
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional);
	}

	TMap<FString, UObject*> CreatedSubobjectsByGate;

	for (FStandaloneSubobjectImport_ImportBpy& ParsedSubobject : ParsedSubobjects)
	{
		UObject* DesiredOuter = Asset;
		if (!ParsedSubobject.ParentRelativeGate.IsEmpty())
		{
			if (UObject* const* ParentObject =
					CreatedSubobjectsByGate.Find(ParsedSubobject.ParentRelativeGate))
			{
				DesiredOuter = *ParentObject;
			}
			else
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve standalone subobject parent '%s' for '%s'"),
					*ParsedSubobject.ParentRelativeGate,
					*ParsedSubobject.RelativeGate);
				return false;
			}
		}

		UObject* Subobject = FindObject<UObject>(DesiredOuter, *ParsedSubobject.Name);
		if (Subobject && !Subobject->IsA(ParsedSubobject.Class))
		{
			Subobject->Rename(
				nullptr,
				GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			Subobject = nullptr;
		}

		if (!Subobject)
		{
			Subobject = NewObject<UObject>(
				DesiredOuter,
				ParsedSubobject.Class,
				*ParsedSubobject.Name,
				RF_Public | RF_Transactional);
		}
		if (!Subobject)
		{
			OutError = FString::Printf(TEXT("Failed to create standalone subobject: %s"), *ParsedSubobject.Name);
			return false;
		}

		ParsedSubobject.Instance = Subobject;
		CreatedSubobjectsByGate.Add(ParsedSubobject.RelativeGate, Subobject);
	}

	for (const FStandaloneSubobjectImport_ImportBpy& ParsedSubobject : ParsedSubobjects)
	{
		UObject* Subobject = ParsedSubobject.Instance;
		if (!Subobject)
		{
			OutError = FString::Printf(
				TEXT("Standalone subobject instance was not created: %s"),
				*ParsedSubobject.RelativeGate);
			return false;
		}

		const TSharedPtr<FJsonObject>* PropertiesJson = nullptr;
		if (ParsedSubobject.Json->TryGetObjectField(TEXT("properties"), PropertiesJson) &&
			PropertiesJson && PropertiesJson->IsValid())
		{
			ApplyJsonObjectToObject_ImportBpy(Subobject, *PropertiesJson);
		}

		Subobject->Modify();
		Subobject->PostEditChange();
	}

	return true;
}

bool CleanupUnexpectedStandaloneSubobjects_ImportBpy(
	UObject* Asset,
	const TArray<TSharedPtr<FJsonValue>>* SubobjectValues,
	FString& OutError)
{
	if (!Asset || !SubobjectValues)
	{
		return true;
	}

	TSet<FString> DesiredSubobjectNames;
	TSet<UClass*> DesiredSubobjectClasses;
	for (const TSharedPtr<FJsonValue>& SubobjectValue : *SubobjectValues)
	{
		if (!SubobjectValue.IsValid() || SubobjectValue->Type != EJson::Object)
		{
			OutError = TEXT("Standalone asset subobject entry must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> SubobjectJson = SubobjectValue->AsObject();
		FString SubobjectName;
		FString SubobjectClassPath;
		SubobjectJson->TryGetStringField(TEXT("name"), SubobjectName);
		SubobjectJson->TryGetStringField(TEXT("class"), SubobjectClassPath);
		if (SubobjectName.IsEmpty() || SubobjectClassPath.IsEmpty())
		{
			OutError = TEXT("Standalone asset subobject entry is missing name or class");
			return false;
		}

		UClass* SubobjectClass = ResolveNamedObject_ImportBpy<UClass>(SubobjectClassPath);
		if (!SubobjectClass)
		{
			OutError = FString::Printf(TEXT("Cannot load standalone subobject class: %s"), *SubobjectClassPath);
			return false;
		}

		DesiredSubobjectNames.Add(SubobjectName);
		DesiredSubobjectClasses.Add(SubobjectClass);
	}

	TArray<UObject*> ExistingSubobjects;
	GetObjectsWithOuter(Asset, ExistingSubobjects, /*bIncludeNestedObjects=*/false);
	for (UObject* ExistingSubobject : ExistingSubobjects)
	{
		if (!ExistingSubobject ||
			ExistingSubobject->HasAnyFlags(RF_Transient | RF_ClassDefaultObject))
		{
			continue;
		}

		if (DesiredSubobjectNames.Contains(ExistingSubobject->GetName()))
		{
			continue;
		}

		if (!DesiredSubobjectClasses.Contains(ExistingSubobject->GetClass()))
		{
			continue;
		}

		ExistingSubobject->Rename(
			nullptr,
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional);
	}

	return true;
}

FString SanitizeImportedUserDefinedEnumShortName_ImportBpy(const FString& RawName, int32 EntryIndex)
{
	FString WorkingName = RawName;
	WorkingName.TrimStartAndEndInline();

	const int32 ScopeSeparatorIndex =
		WorkingName.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (ScopeSeparatorIndex != INDEX_NONE)
	{
		WorkingName = WorkingName.Mid(ScopeSeparatorIndex + 2);
	}

	if (WorkingName.IsEmpty())
	{
		WorkingName = FString::Printf(TEXT("NewEnumerator%d"), EntryIndex);
	}

	FString SanitizedName;
	SanitizedName.Reserve(WorkingName.Len());
	for (const TCHAR Character : WorkingName)
	{
		const bool bAllowedCharacter = FChar::IsAlnum(Character) || (Character == TEXT('_'));
		SanitizedName.AppendChar(bAllowedCharacter ? Character : TEXT('_'));
	}

	while (SanitizedName.ReplaceInline(TEXT("__"), TEXT("_")) > 0)
	{
	}

	SanitizedName.TrimStartAndEndInline();
	if (SanitizedName.IsEmpty())
	{
		SanitizedName = FString::Printf(TEXT("NewEnumerator%d"), EntryIndex);
	}

	if (FChar::IsDigit(SanitizedName[0]))
	{
		SanitizedName = FString::Printf(TEXT("Enum_%s"), *SanitizedName);
	}

	if (SanitizedName.EndsWith(TEXT("_MAX"), ESearchCase::IgnoreCase))
	{
		SanitizedName += TEXT("_Entry");
	}

	if (!FName(*SanitizedName).IsValidXName(INVALID_OBJECTNAME_CHARACTERS))
	{
		SanitizedName = FString::Printf(TEXT("NewEnumerator%d"), EntryIndex);
	}

	return SanitizedName;
}

bool RestoreUserDefinedEnumEntries_ImportBpy(
	UObject* Asset,
	const TSharedPtr<FJsonObject>& StandaloneMetaJson,
	FString& OutError)
{
	UUserDefinedEnum* const UserDefinedEnum = Cast<UUserDefinedEnum>(Asset);
	if (!UserDefinedEnum || !StandaloneMetaJson.IsValid())
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* EnumEntries = nullptr;
	if (!StandaloneMetaJson->TryGetArrayField(TEXT("enum_entries"), EnumEntries) || !EnumEntries)
	{
		return true;
	}

	TArray<TPair<FName, int64>> DesiredEnums;
	TArray<FString> DesiredShortNames;
	TArray<FText> DesiredDisplayNames;
	TSet<FName> UsedNames;
	TSet<FString> UsedDisplayNames;
	DesiredEnums.Reserve(EnumEntries->Num());
	DesiredShortNames.Reserve(EnumEntries->Num());
	DesiredDisplayNames.Reserve(EnumEntries->Num());

	UserDefinedEnum->Modify();

	int32 EntryIndex = 0;
	for (const TSharedPtr<FJsonValue>& EntryValue : *EnumEntries)
	{
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			OutError = TEXT("enum_entries entry must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		FString EnumName;
		EntryObject->TryGetStringField(TEXT("name"), EnumName);
		const FString BaseName = SanitizeImportedUserDefinedEnumShortName_ImportBpy(EnumName, EntryIndex);

		FString CandidateName = BaseName;
		int32 Suffix = 0;
		while (UsedNames.Contains(FName(*CandidateName)))
		{
			++Suffix;
			CandidateName = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
		}

		const FName FinalName(*CandidateName);
		if (!FinalName.IsValidXName(INVALID_OBJECTNAME_CHARACTERS))
		{
			OutError = FString::Printf(
				TEXT("enum_entries name is invalid after sanitization: %s (asset=%s)"),
				*CandidateName,
				*UserDefinedEnum->GetPathName());
			return false;
		}
		UsedNames.Add(FinalName);

		double NumericValue = static_cast<double>(EntryIndex);
		EntryObject->TryGetNumberField(TEXT("value"), NumericValue);
		if (!FMath::IsFinite(NumericValue))
		{
			OutError = FString::Printf(
				TEXT("enum_entries value is not finite for %s in %s"),
				*CandidateName,
				*UserDefinedEnum->GetPathName());
			return false;
		}

		const FString FullEnumName = FString::Printf(TEXT("%s::%s"), *UserDefinedEnum->GetName(), *CandidateName);
		DesiredEnums.Emplace(FName(*FullEnumName), static_cast<int64>(NumericValue));
		DesiredShortNames.Add(CandidateName);

		FString DisplayName;
		EntryObject->TryGetStringField(TEXT("display_name"), DisplayName);
		DisplayName.TrimStartAndEndInline();
		if (DisplayName.IsEmpty())
		{
			DisplayName = CandidateName;
		}
		if (UsedDisplayNames.Contains(DisplayName))
		{
			OutError = FString::Printf(
				TEXT("Duplicate enum_entries display_name is not allowed: %s (asset=%s)"),
				*DisplayName,
				*UserDefinedEnum->GetPathName());
			return false;
		}
		UsedDisplayNames.Add(DisplayName);
		DesiredDisplayNames.Add(FText::FromString(DisplayName));
		++EntryIndex;
	}

	if (DesiredEnums.Num() == 0)
	{
		// Allow round-trip of intentionally empty/corrupted enum exports without
		// failing the entire import. In this case we leave current enum data as-is.
		return true;
	}

	if (!UserDefinedEnum->SetEnums(DesiredEnums, UEnum::ECppForm::Namespaced, EEnumFlags::None, true))
	{
		OutError = FString::Printf(
			TEXT("Failed to set enum entries for UserDefinedEnum: %s"),
			*UserDefinedEnum->GetPathName());
		return false;
	}

	const int32 NumUserEntries = FMath::Max(0, UserDefinedEnum->NumEnums() - 1);
	if (NumUserEntries != DesiredDisplayNames.Num())
	{
		OutError = FString::Printf(
			TEXT("UserDefinedEnum entry count mismatch after SetEnums: expected=%d actual=%d (%s)"),
			DesiredDisplayNames.Num(),
			NumUserEntries,
			*UserDefinedEnum->GetPathName());
		return false;
	}

	UserDefinedEnum->DisplayNameMap.Empty(NumUserEntries);
	for (int32 Index = 0; Index < NumUserEntries; ++Index)
	{
		const FName EnumEntryName(*UserDefinedEnum->GetNameStringByIndex(Index));
		UserDefinedEnum->DisplayNameMap.Add(EnumEntryName, DesiredDisplayNames[Index]);
	}
	FEnumEditorUtils::EnsureAllDisplayNamesExist(UserDefinedEnum);

	for (int32 Index = 0; Index < NumUserEntries; ++Index)
	{
		const FString ActualShortName = UserDefinedEnum->GetNameStringByIndex(Index);
		if (!ActualShortName.Equals(DesiredShortNames[Index], ESearchCase::CaseSensitive))
		{
			OutError = FString::Printf(
				TEXT("UserDefinedEnum short name mismatch at index %d: expected=%s actual=%s (%s)"),
				Index,
				*DesiredShortNames[Index],
				*ActualShortName,
				*UserDefinedEnum->GetPathName());
			return false;
		}

		const FString ActualDisplayName = UserDefinedEnum->GetDisplayNameTextByIndex(Index).ToString();
		const FString ExpectedDisplayName = DesiredDisplayNames[Index].ToString();
		if (!ActualDisplayName.Equals(ExpectedDisplayName, ESearchCase::CaseSensitive))
		{
			OutError = FString::Printf(
				TEXT("UserDefinedEnum display_name mismatch at index %d: expected=%s actual=%s (%s)"),
				Index,
				*ExpectedDisplayName,
				*ActualDisplayName,
				*UserDefinedEnum->GetPathName());
			return false;
		}
	}

	return true;
}

USCS_Node* FindComponentNodeByName_ImportBpy(UBlueprint* BP, const FString& ComponentName)
{
	if (!BP || !BP->SimpleConstructionScript || ComponentName.IsEmpty())
	{
		return nullptr;
	}

	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && ComponentNameMatches_ImportBpy(ComponentName, Node->GetVariableName().ToString()))
		{
			return Node;
		}
	}

	return nullptr;
}

void DetachNodeFromSCS_ImportBpy(USimpleConstructionScript* SCS, USCS_Node* Node)
{
	if (!SCS || !Node || !SCS->GetAllNodes().Contains(Node))
	{
		return;
	}

	// Use the engine's own detach path so RootNodes/ChildNodes/AllNodes stay consistent.
	SCS->RemoveNode(Node, /*bValidateSceneRootNodes=*/false);
}

bool AttachComponentNode_ImportBpy(
	UBlueprint* BP,
	USCS_Node* Node,
	const FString& ParentName,
	const TMap<FString, USCS_Node*>& KnownNodes,
	FString& OutError)
{
	if (!BP || !BP->SimpleConstructionScript || !Node)
	{
		OutError = TEXT("Invalid blueprint/component when attaching component");
		return false;
	}

	USimpleConstructionScript* const SCS = BP->SimpleConstructionScript;

	auto ClearExternalParentMetadata = [Node]()
	{
		if (!Node)
		{
			return;
		}

		Node->Modify();
		Node->bIsParentComponentNative = false;
		Node->ParentComponentOrVariableName = NAME_None;
		Node->ParentComponentOwnerClassName = NAME_None;
	};

	auto ReattachToCurrentBlueprintParent = [SCS, Node, &ClearExternalParentMetadata](USCS_Node* ParentNode) -> bool
	{
		if (!SCS || !Node || !ParentNode)
		{
			return false;
		}

		// For components in the current Blueprint, the relationship belongs in the
		// SCS tree (ChildNodes), not in ParentComponentOwnerClassName metadata.
		ClearExternalParentMetadata();
		ParentNode->AddChildNode(Node);
		SCS->ValidateSceneRootNodes();
		return true;
	};

	auto ReattachToExternalParent = [SCS, Node](const USceneComponent* ParentSceneComponent) -> bool
	{
		if (!SCS || !Node || !ParentSceneComponent)
		{
			return false;
		}

		// Native or inherited Blueprint parents are represented as root-node
		// attachments via ParentComponentOrVariableName metadata.
		Node->SetParent(ParentSceneComponent);
		SCS->AddNode(Node);
		return true;
	};

	if (ParentName.IsEmpty())
	{
		DetachNodeFromSCS_ImportBpy(SCS, Node);
		ClearExternalParentMetadata();

		SCS->AddNode(Node);
		return true;
	}

	if (USCS_Node* const* ParentNodePtr = KnownNodes.Find(ParentName))
	{
		if (USCS_Node* ParentNode = *ParentNodePtr)
		{
			DetachNodeFromSCS_ImportBpy(SCS, Node);
			return ReattachToCurrentBlueprintParent(ParentNode);
		}
	}

	for (const TPair<FString, USCS_Node*>& Entry : KnownNodes)
	{
		if (USCS_Node* ParentNode = Entry.Value; ParentNode && ComponentNameMatches_ImportBpy(ParentName, Entry.Key))
		{
			DetachNodeFromSCS_ImportBpy(SCS, Node);
			return ReattachToCurrentBlueprintParent(ParentNode);
		}
	}

	if (USCS_Node* ParentNode = FindComponentNodeByName_ImportBpy(BP, ParentName))
	{
		DetachNodeFromSCS_ImportBpy(SCS, Node);
		return ReattachToCurrentBlueprintParent(ParentNode);
	}

	if (USceneComponent* ParentSceneComponent = FindInheritedSceneComponentByName_ImportBpy(BP, ParentName))
	{
		DetachNodeFromSCS_ImportBpy(SCS, Node);
		return ReattachToExternalParent(ParentSceneComponent);
	}

	if (USceneComponent* ParentSceneComponent = ResolveNamedObject_ImportBpy<USceneComponent>(ParentName))
	{
		DetachNodeFromSCS_ImportBpy(SCS, Node);
		return ReattachToExternalParent(ParentSceneComponent);
	}

	OutError = FString::Printf(TEXT("Parent component not found: %s"), *ParentName);
	return false;
}

FString ResolveCurrentComponentParentName_ImportBpy(UBlueprint* BP, USCS_Node* Node)
{
	if (!BP || !BP->SimpleConstructionScript || !Node)
	{
		return FString();
	}

	if (USCS_Node* ParentNode = BP->SimpleConstructionScript->FindParentNode(Node))
	{
		return ParentNode->GetVariableName().ToString();
	}

	if (Node->ParentComponentOrVariableName != NAME_None)
	{
		return Node->ParentComponentOrVariableName.ToString();
	}

	TFunction<const USCS_Node*(const USCS_Node*)> FindParentRecursive =
		[&FindParentRecursive, Node](const USCS_Node* SearchNode) -> const USCS_Node*
	{
		if (!SearchNode)
		{
			return nullptr;
		}

		for (const USCS_Node* ChildNode : SearchNode->GetChildNodes())
		{
			if (!ChildNode)
			{
				continue;
			}

			if (ChildNode == Node)
			{
				return SearchNode;
			}

			if (const USCS_Node* FoundParent = FindParentRecursive(ChildNode))
			{
				return FoundParent;
			}
		}

		return nullptr;
	};

	for (const USCS_Node* RootNode : BP->SimpleConstructionScript->GetRootNodes())
	{
		if (!RootNode)
		{
			continue;
		}

		if (RootNode == Node)
		{
			return FString();
		}

		if (const USCS_Node* ParentNode = FindParentRecursive(RootNode))
		{
			return ParentNode->GetVariableName().ToString();
		}
	}

	return FString();
}

bool ApplyComponentTemplateProperties_ImportBpy(
	UActorComponent* ComponentTemplate,
	const TSharedPtr<FJsonObject>& PropertiesObj,
	FString& OutError)
{
	if (!ComponentTemplate || !PropertiesObj.IsValid())
	{
		return true;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : PropertiesObj->Values)
	{
		if (!Entry.Value.IsValid() || Entry.Value->Type != EJson::String)
		{
			continue;
		}

		FProperty* Property = FindPropertyByNameOrAlias_ImportBpy(ComponentTemplate, Entry.Key);
		if (!Property ||
			!Property->HasAnyPropertyFlags(CPF_ContainsInstancedReference | CPF_InstancedReference))
		{
			continue;
		}

		if (!EnsureInstancedSubobjectsExistForSerializedPropertyText_ImportBpy(
				ComponentTemplate,
				RemapBlueprintReferencesInSerializedText_ImportBpy(Entry.Value->AsString()),
				OutError))
		{
			return false;
		}
	}

	ApplyJsonObjectToObject_ImportBpy(ComponentTemplate, PropertiesObj);

	if (USkeletalMeshComponent* SkeletalMeshTemplate = Cast<USkeletalMeshComponent>(ComponentTemplate))
	{
		const TSharedPtr<FJsonValue>* AnimClassValue = PropertiesObj->Values.Find(TEXT("AnimClass"));
		if (AnimClassValue && AnimClassValue->IsValid() && (*AnimClassValue)->Type == EJson::String)
		{
			const FString AnimClassReference =
				RemapBlueprintReferenceForCurrentImport_ImportBpy((*AnimClassValue)->AsString(), true);
			if (UClass* AnimClass = ResolveNamedObject_ImportBpy<UClass>(AnimClassReference))
			{
				SkeletalMeshTemplate->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				SkeletalMeshTemplate->SetAnimInstanceClass(AnimClass);
			}
		}
	}

	return true;
}

bool ReplayComponentTemplatePropertiesAfterCompile_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonValue>>& ComponentsArr,
	bool* bOutReappliedAny,
	FString& OutError)
{
	if (bOutReappliedAny)
	{
		*bOutReappliedAny = false;
	}

	if (!BP || !BP->SimpleConstructionScript || ComponentsArr.Num() == 0)
	{
		return true;
	}

	bool bReappliedAny = false;
	for (const TSharedPtr<FJsonValue>& ComponentValue : ComponentsArr)
	{
		const TSharedPtr<FJsonObject> ComponentJson = ComponentValue.IsValid() ? ComponentValue->AsObject() : nullptr;
		if (!ComponentJson.IsValid())
		{
			continue;
		}

		FString ComponentName;
		if (!ComponentJson->TryGetStringField(TEXT("name"), ComponentName) || ComponentName.IsEmpty())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
		if (!ComponentJson->TryGetObjectField(TEXT("properties"), PropertiesObj) ||
			!PropertiesObj ||
			!PropertiesObj->IsValid())
		{
			continue;
		}

		USCS_Node* ComponentNode = FindComponentNodeByName_ImportBpy(BP, ComponentName);
		if (!ComponentNode || !ComponentNode->ComponentTemplate)
		{
			OutError = FString::Printf(
				TEXT("Cannot find component template to replay properties after compile: %s"),
				*ComponentName);
			return false;
		}

		ComponentNode->ComponentTemplate->Modify();
		if (!ApplyComponentTemplateProperties_ImportBpy(ComponentNode->ComponentTemplate, *PropertiesObj, OutError))
		{
			return false;
		}
		bReappliedAny = true;
	}

	if (bReappliedAny)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		BP->MarkPackageDirty();
	}

	if (bOutReappliedAny)
	{
		*bOutReappliedAny = bReappliedAny;
	}

	return true;
}

bool ImportComponents_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonValue>>& ComponentsArr,
	FString& OutError)
{
	if (!BP || ComponentsArr.Num() == 0)
	{
		return true;
	}

	if (!BP->SimpleConstructionScript)
	{
		return true;
	}

	TMap<FString, USCS_Node*> KnownNodes;
	for (USCS_Node* ExistingNode : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (ExistingNode)
		{
			KnownNodes.Add(ExistingNode->GetVariableName().ToString(), ExistingNode);
		}
	}

	TArray<TSharedPtr<FJsonObject>> PendingComponents;
	PendingComponents.Reserve(ComponentsArr.Num());
	for (const TSharedPtr<FJsonValue>& ComponentValue : ComponentsArr)
	{
		if (const TSharedPtr<FJsonObject> ComponentObj = ComponentValue->AsObject(); ComponentObj.IsValid())
		{
			PendingComponents.Add(ComponentObj);
		}
	}

	bool bCreatedOrUpdatedComponents = false;
	while (PendingComponents.Num() > 0)
	{
		bool bMadeProgress = false;

		for (int32 Index = 0; Index < PendingComponents.Num();)
		{
			const TSharedPtr<FJsonObject> ComponentJson = PendingComponents[Index];
			if (!ComponentJson.IsValid())
			{
				PendingComponents.RemoveAt(Index);
				bMadeProgress = true;
				continue;
			}

			FString ComponentName;
			FString ComponentClassName;
			if (!ComponentJson->TryGetStringField(TEXT("name"), ComponentName) ||
				!ComponentJson->TryGetStringField(TEXT("class_name"), ComponentClassName) ||
				ComponentName.IsEmpty())
			{
				OutError = TEXT("Component json is missing required fields");
				return false;
			}

			FString ParentName;
			ComponentJson->TryGetStringField(TEXT("parent"), ParentName);
			if (!ParentName.IsEmpty() && ParentName != ComponentName &&
				!CanResolveComponentParent_ImportBpy(BP, ParentName, KnownNodes))
			{
				++Index;
				continue;
			}

			FString AttachToName;
			const bool bHasAttachToName = ComponentJson->TryGetStringField(TEXT("attach_to_name"), AttachToName);

			USCS_Node* ComponentNode = nullptr;
			if (USCS_Node** ExistingNodePtr = KnownNodes.Find(ComponentName))
			{
				ComponentNode = *ExistingNodePtr;
			}
			else
			{
				UClass* ComponentClass = ResolveComponentClass_ImportBpy(ComponentClassName);
				if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
				{
					OutError = FString::Printf(TEXT("Unknown component type: %s"), *ComponentClassName);
					return false;
				}

				ComponentNode = BP->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
				if (!ComponentNode || !ComponentNode->ComponentTemplate)
				{
					OutError = FString::Printf(TEXT("Failed to create component node: %s"), *ComponentName);
					return false;
				}

				if (!AttachComponentNode_ImportBpy(BP, ComponentNode, ParentName, KnownNodes, OutError))
				{
					return false;
				}

				KnownNodes.Add(ComponentName, ComponentNode);
				bCreatedOrUpdatedComponents = true;
			}

			const FString CurrentParentName = ResolveCurrentComponentParentName_ImportBpy(BP, ComponentNode);
			if (!ParentName.IsEmpty() && !ComponentNameMatches_ImportBpy(ParentName, CurrentParentName))
			{
				if (!AttachComponentNode_ImportBpy(BP, ComponentNode, ParentName, KnownNodes, OutError))
				{
					return false;
				}

				bCreatedOrUpdatedComponents = true;
			}

			if (bHasAttachToName)
			{
				const FName DesiredAttachName = AttachToName.IsEmpty() ? NAME_None : FName(*AttachToName);
				if (ComponentNode->AttachToName != DesiredAttachName)
				{
					ComponentNode->AttachToName = DesiredAttachName;
					bCreatedOrUpdatedComponents = true;
				}
			}

			const FName EffectiveAttachName = ComponentNode->AttachToName;
			USceneComponent* ParentSceneTemplate = ResolveParentSceneTemplate_ImportBpy(BP, ParentName, KnownNodes);
			if (SyncSceneTemplateAttachment_ImportBpy(ComponentNode, ParentSceneTemplate, EffectiveAttachName))
			{
				bCreatedOrUpdatedComponents = true;
			}

			if (ComponentNode->ComponentTemplate)
			{
				const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
				if (ComponentJson->TryGetObjectField(TEXT("properties"), PropertiesObj) && PropertiesObj && PropertiesObj->IsValid())
				{
					if (!ApplyComponentTemplateProperties_ImportBpy(
							ComponentNode->ComponentTemplate,
							*PropertiesObj,
							OutError))
					{
						return false;
					}
				}
			}

			PendingComponents.RemoveAt(Index);
			bMadeProgress = true;
			continue;
		}

		if (!bMadeProgress)
		{
			TArray<FString> UnresolvedComponents;
			for (const TSharedPtr<FJsonObject>& ComponentJson : PendingComponents)
			{
				if (!ComponentJson.IsValid())
				{
					continue;
				}

				FString ComponentName;
				FString ParentName;
				ComponentJson->TryGetStringField(TEXT("name"), ComponentName);
				ComponentJson->TryGetStringField(TEXT("parent"), ParentName);
				UnresolvedComponents.Add(FString::Printf(TEXT("%s(parent=%s)"), *ComponentName, *ParentName));
			}

			OutError = FString::Printf(
				TEXT("Failed to resolve component parent chain: %s"),
				*FString::Join(UnresolvedComponents, TEXT(", ")));
			return false;
		}
	}

	if (bCreatedOrUpdatedComponents)
	{
		BP->SimpleConstructionScript->ValidateSceneRootNodes();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}

	return true;
}

bool IsBlendStackGraphLike_ImportBpy(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}

	const FString GraphClassName = Graph->GetClass() ? Graph->GetClass()->GetName() : FString();
	if (GraphClassName.Contains(TEXT("AnimationBlendStackGraph"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (const UEdGraphSchema* Schema = Graph->GetSchema())
	{
		const FString SchemaClassName = Schema->GetClass() ? Schema->GetClass()->GetName() : FString();
		if (SchemaClassName.Contains(TEXT("AnimationBlendStackGraphSchema"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

UEdGraph* ResolveBlendStackGraph_ImportBpy(UEdGraphNode* Node)
{
	if (!Node || !Node->GetClass())
	{
		return nullptr;
	}

	auto TryGetGraphProperty = [Node](const TCHAR* PropertyName) -> UEdGraph*
	{
		if (!PropertyName)
		{
			return nullptr;
		}

		const FObjectPropertyBase* GraphProperty =
			FindFProperty<FObjectPropertyBase>(Node->GetClass(), FName(PropertyName));
		if (!GraphProperty || !GraphProperty->PropertyClass ||
			!GraphProperty->PropertyClass->IsChildOf(UEdGraph::StaticClass()))
		{
			return nullptr;
		}

		return Cast<UEdGraph>(GraphProperty->GetObjectPropertyValue_InContainer(Node));
	};

	constexpr const TCHAR* PreferredPropertyNames[] = {
		TEXT("BoundGraph"),
		TEXT("BlendStackGraph"),
		TEXT("AnimationBlendStackGraph"),
		TEXT("EditorBlendStackGraph")
	};
	for (const TCHAR* PropertyName : PreferredPropertyNames)
	{
		if (UEdGraph* Graph = TryGetGraphProperty(PropertyName))
		{
			if (IsBlendStackGraphLike_ImportBpy(Graph) &&
				Graph->GetOuter() == Node)
			{
				return Graph;
			}
		}
	}

	UEdGraph* BestGraph = nullptr;
	int32 BestScore = MIN_int32;
	for (TFieldIterator<FProperty> It(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FObjectPropertyBase* GraphProperty = CastField<FObjectPropertyBase>(*It);
		if (!GraphProperty || !GraphProperty->PropertyClass ||
			!GraphProperty->PropertyClass->IsChildOf(UEdGraph::StaticClass()))
		{
			continue;
		}

		UEdGraph* Graph = Cast<UEdGraph>(GraphProperty->GetObjectPropertyValue_InContainer(Node));
		if (!Graph)
		{
			continue;
		}

		const FString PropertyName = GraphProperty->GetName();
		const bool bPropertyLooksRelevant =
			PropertyName.Contains(TEXT("BlendStack"), ESearchCase::IgnoreCase) ||
			PropertyName.Contains(TEXT("AnimationBlendStackGraph"), ESearchCase::IgnoreCase);
		if (!bPropertyLooksRelevant && !IsBlendStackGraphLike_ImportBpy(Graph))
		{
			continue;
		}

		int32 Score = Graph->Nodes.Num();
		if (PropertyName.Contains(TEXT("BlendStack"), ESearchCase::IgnoreCase))
		{
			Score += 500;
		}
		if (IsBlendStackGraphLike_ImportBpy(Graph))
		{
			Score += 2000;
		}
		if (Graph->GetOuter() == Node)
		{
			Score += 4000;
		}

		if (!BestGraph || Score > BestScore)
		{
			BestGraph = Graph;
			BestScore = Score;
		}
	}

	return BestGraph;
}

FString DescribeGraphOuterKind_ImportBpy(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return FString();
	}

	const UObject* Outer = Graph->GetOuter();
	if (!Outer)
	{
		return TEXT("None");
	}

	if (Outer->IsA<UEdGraphNode>())
	{
		return TEXT("Node");
	}
	if (Outer->IsA<UBlueprint>())
	{
		return TEXT("Blueprint");
	}

	return Outer->GetClass() ? Outer->GetClass()->GetName() : TEXT("Unknown");
}

bool EnsureBlendStackGraphOwnership_ImportBpy(
	UEdGraphNode* BlendStackNode,
	UEdGraph* BlendStackGraph,
	FString& OutError)
{
	if (!BlendStackNode || !BlendStackGraph)
	{
		return true;
	}

	if (BlendStackGraph->GetOuter() == BlendStackNode)
	{
		return true;
	}

	const UObject* Outer = BlendStackGraph->GetOuter();
	OutError = FString::Printf(
		TEXT("BlendStack graph ownership mismatch on node %s: expected graph outer '%s' (Node), got '%s' (outer_kind=%s)."),
		*DescribeNode_ImportBpy(BlendStackNode),
		*BlendStackNode->GetName(),
		Outer ? *Outer->GetName() : TEXT("<null>"),
	*DescribeGraphOuterKind_ImportBpy(BlendStackGraph));
	return false;
}

static void LogBlendStackGraphBindingsAndDuplicates_ImportBpy(
	UEdGraphNode* Node,
	const TCHAR* StageTag)
{
	if (!Node || !Node->GetClass())
	{
		return;
	}

	if (!ResolveBlendStackGraph_ImportBpy(Node))
	{
		return;
	}

	const TCHAR* SafeStageTag = StageTag ? StageTag : TEXT("Unknown");

	TArray<FString> PropertySummaries;
	for (TFieldIterator<FProperty> It(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FObjectPropertyBase* GraphProperty = CastField<FObjectPropertyBase>(*It);
		if (!GraphProperty || !GraphProperty->PropertyClass ||
			!GraphProperty->PropertyClass->IsChildOf(UEdGraph::StaticClass()))
		{
			continue;
		}

		UEdGraph* const Graph = Cast<UEdGraph>(GraphProperty->GetObjectPropertyValue_InContainer(Node));
		if (!Graph)
		{
			continue;
		}

		PropertySummaries.Add(FString::Printf(
			TEXT("%s=%s class=%s outer=%s outer_kind=%s nodes=%d guid=%s"),
			*GraphProperty->GetName(),
			*Graph->GetPathName(),
			*GetNameSafe(Graph->GetClass()),
			*GetPathNameSafe(Graph->GetOuter()),
			*DescribeGraphOuterKind_ImportBpy(Graph),
			Graph->Nodes.Num(),
			*Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens)));
	}
	PropertySummaries.Sort();

	for (const FString& Summary : PropertySummaries)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] node=%s prop=%s"),
			SafeStageTag,
			*DescribeNode_ImportBpy(Node),
			*Summary);
	}

	if (UEdGraph* const ResolvedGraph = ResolveBlendStackGraph_ImportBpy(Node))
	{
		if (ResolvedGraph->Nodes.Num() <= 2)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] node=%s resolved=%s class=%s outer=%s outer_kind=%s nodes=%d guid=%s"),
				SafeStageTag,
				*DescribeNode_ImportBpy(Node),
				*ResolvedGraph->GetPathName(),
				*GetNameSafe(ResolvedGraph->GetClass()),
				*GetPathNameSafe(ResolvedGraph->GetOuter()),
				*DescribeGraphOuterKind_ImportBpy(ResolvedGraph),
				ResolvedGraph->Nodes.Num(),
				*ResolvedGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] node=%s resolved=%s class=%s outer=%s outer_kind=%s nodes=%d guid=%s"),
				SafeStageTag,
				*DescribeNode_ImportBpy(Node),
				*ResolvedGraph->GetPathName(),
				*GetNameSafe(ResolvedGraph->GetClass()),
				*GetPathNameSafe(ResolvedGraph->GetOuter()),
				*DescribeGraphOuterKind_ImportBpy(ResolvedGraph),
				ResolvedGraph->Nodes.Num(),
				*ResolvedGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] node=%s resolved=<null>"),
			SafeStageTag,
			*DescribeNode_ImportBpy(Node));
	}

	UBlueprint* const OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	if (!OwningBlueprint)
	{
		return;
	}

	TArray<UObject*> NestedObjects;
	GetObjectsWithOuter(OwningBlueprint, NestedObjects, /*bIncludeNestedObjects=*/ true);

	TArray<FString> CandidateSummaries;
	for (UObject* NestedObject : NestedObjects)
	{
		UEdGraph* const Graph = Cast<UEdGraph>(NestedObject);
		if (!Graph || !Graph->GetName().Equals(TEXT("AnimationBlendStackGraph_0"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		CandidateSummaries.Add(FString::Printf(
			TEXT("%s class=%s outer=%s outer_kind=%s nodes=%d guid=%s"),
			*Graph->GetPathName(),
			*GetNameSafe(Graph->GetClass()),
			*GetPathNameSafe(Graph->GetOuter()),
			*DescribeGraphOuterKind_ImportBpy(Graph),
			Graph->Nodes.Num(),
			*Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens)));
	}
	CandidateSummaries.Sort();

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] node=%s duplicate_name_candidates=%d"),
		SafeStageTag,
		*DescribeNode_ImportBpy(Node),
		CandidateSummaries.Num());

	for (const FString& Summary : CandidateSummaries)
	{
		const bool bLooksWrongTwoNodeGraph =
			Summary.Contains(TEXT("nodes=2"), ESearchCase::CaseSensitive);
		if (bLooksWrongTwoNodeGraph)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] candidate=%s"),
				SafeStageTag,
				*Summary);
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] candidate=%s"),
				SafeStageTag,
				*Summary);
		}
	}
}

// Binding import must refresh both serialized maps and editor node state, otherwise
// AnimGraph compilation can silently keep stale literal defaults.
int32 CountSerializedBindingTrueFlags_ImportBpy(const FString& SerializedBindings)
{
	int32 Count = 0;
	const TCHAR* Tokens[] = { TEXT("bIsBound=True"), TEXT("bIsBound=true") };
	for (const TCHAR* Token : Tokens)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < SerializedBindings.Len())
		{
			const int32 Hit = SerializedBindings.Find(
				Token,
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart,
				SearchFrom);
			if (Hit == INDEX_NONE)
			{
				break;
			}

			++Count;
			SearchFrom = Hit + FCString::Strlen(Token);
		}
	}

	return Count;
}

void ExtractBindingPropertyNamesForPins_ImportBpy(
	const FString& SerializedBindings,
	TSet<FName>& OutPropertyNames)
{
	OutPropertyNames.Reset();
	static const FString PropertyNameToken = TEXT("PropertyName=\"");
	int32 SearchFrom = 0;
	while (SearchFrom < SerializedBindings.Len())
	{
		const int32 Hit = SerializedBindings.Find(
			PropertyNameToken,
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			SearchFrom);
		if (Hit == INDEX_NONE)
		{
			break;
		}

		const int32 NameStart = Hit + PropertyNameToken.Len();
		const int32 NameEnd = SerializedBindings.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			NameStart);
		if (NameEnd == INDEX_NONE || NameEnd <= NameStart)
		{
			break;
		}

		const FString NameText =
			SerializedBindings.Mid(NameStart, NameEnd - NameStart).TrimStartAndEnd();
		if (!NameText.IsEmpty())
		{
			FName PropertyName(*NameText);
			PropertyName.SetNumber(0);
			OutPropertyNames.Add(PropertyName);
		}

		SearchFrom = NameEnd + 1;
	}
}

int32 EnsureAnimNodeBindingDrivenPinsVisible_ImportBpy(
	UEdGraphNode* Node,
	const FString& SerializedBindings)
{
	UAnimGraphNode_Base* const AnimNode = Cast<UAnimGraphNode_Base>(Node);
	if (!AnimNode || SerializedBindings.IsEmpty() ||
		SerializedBindings.Equals(TEXT("()"), ESearchCase::CaseSensitive))
	{
		return 0;
	}

	TSet<FName> BindingPropertyNames;
	ExtractBindingPropertyNamesForPins_ImportBpy(SerializedBindings, BindingPropertyNames);
	if (BindingPropertyNames.Num() == 0)
	{
		return 0;
	}

	int32 ChangedCount = 0;
	for (FOptionalPinFromProperty& OptionalPin : AnimNode->ShowPinForProperties)
	{
		FName OptionalName = OptionalPin.PropertyName;
		OptionalName.SetNumber(0);
		if (BindingPropertyNames.Contains(OptionalName) && !OptionalPin.bShowPin)
		{
			OptionalPin.bShowPin = true;
			++ChangedCount;
		}
	}

	if (ChangedCount > 0)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[ExportBpy][ImportDiag][AnimBinding] exposed %d binding-driven pins on %s"),
			ChangedCount,
			*DescribeNode_ImportBpy(Node));
	}

	return ChangedCount;
}

bool ValidateAnimNodeBindingImportText_ImportBpy(
	FProperty* BindingsProperty,
	void* ValuePtr,
	UObject* OwnerObject,
	UEdGraphNode* Node,
	const FString& SourceBindings,
	int32 ExpectedKeyCount,
	int32 ExpectedBoundFlagCount,
	const TCHAR* Phase,
	FString& OutError)
{
	if (!BindingsProperty || !ValuePtr || !OwnerObject)
	{
		OutError = FString::Printf(
			TEXT("Cannot validate anim node bindings on %s during %s: invalid storage"),
			*DescribeNode_ImportBpy(Node),
			Phase ? Phase : TEXT("unknown"));
		return false;
	}

	const bool bHasNonEmptyBindingEntries =
		!SourceBindings.IsEmpty() && !SourceBindings.Equals(TEXT("()"), ESearchCase::CaseSensitive);

	if (const FMapProperty* MapProperty = CastField<FMapProperty>(BindingsProperty))
	{
		FScriptMapHelper MapHelper(MapProperty, ValuePtr);
		const int32 ImportedKeyCount = MapHelper.Num();
		if (bHasNonEmptyBindingEntries && ImportedKeyCount < ExpectedKeyCount)
		{
			OutError = FString::Printf(
				TEXT("Anim node binding partial import on %s during %s: expected_keys=%d imported_keys=%d"),
				*DescribeNode_ImportBpy(Node),
				Phase ? Phase : TEXT("unknown"),
				ExpectedKeyCount,
				ImportedKeyCount);
			return false;
		}
	}

	FString ExportedBindings;
	BindingsProperty->ExportTextItem_Direct(
		ExportedBindings,
		ValuePtr,
		nullptr,
		OwnerObject,
		PPF_None);
	const int32 ImportedBoundFlagCount =
		CountSerializedBindingTrueFlags_ImportBpy(ExportedBindings);
	if (bHasNonEmptyBindingEntries && ImportedBoundFlagCount < ExpectedBoundFlagCount)
	{
		OutError = FString::Printf(
			TEXT("Anim node binding bIsBound flag loss on %s during %s: expected_bound=%d imported_bound=%d"),
			*DescribeNode_ImportBpy(Node),
			Phase ? Phase : TEXT("unknown"),
			ExpectedBoundFlagCount,
			ImportedBoundFlagCount);
		return false;
	}

	return true;
}

bool ApplyAnimNodeBindingPropertyBindings_ImportBpy(
	UEdGraphNode* Node,
	const FString& SerializedBindings,
	FString& OutError)
{
	if (SerializedBindings.IsEmpty())
	{
		return true;
	}

	if (!Node)
	{
		OutError = TEXT("Cannot apply anim node binding: node is null");
		return false;
	}

	const FString RemappedBindings =
		RemapBlueprintReferencesInSerializedText_ImportBpy(SerializedBindings);
	const bool bHasNonEmptyBindingEntries =
		!RemappedBindings.IsEmpty() && !RemappedBindings.Equals(TEXT("()"), ESearchCase::CaseSensitive);

	// Count expected PropertyName tokens in the source text so we can detect
	// silent drops by FMapProperty::ImportText_Direct (e.g., FText/NSLOCTEXT
	// sub-field parse failures inside FAnimGraphNodePropertyBinding).
	int32 ExpectedKeyCount = 0;
	if (bHasNonEmptyBindingEntries)
	{
		const FString PropertyNameToken = TEXT("PropertyName=\"");
		int32 SearchFrom = 0;
		while (SearchFrom < RemappedBindings.Len())
		{
			const int32 Hit = RemappedBindings.Find(
				PropertyNameToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Hit == INDEX_NONE) break;
			++ExpectedKeyCount;
			SearchFrom = Hit + PropertyNameToken.Len();
		}
	}

	FObjectPropertyBase* BindingProperty =
		FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("Binding"));
	const int32 ExpectedBoundFlagCount =
		CountSerializedBindingTrueFlags_ImportBpy(RemappedBindings);

	// Prefer the Binding subobject path when available (UE5.7+), because
	// node-level PropertyBindings can resolve to deprecated storage.
	if (!BindingProperty)
	{
		// Fallback for branches where bindings are still stored directly on node.
		if (FMapProperty* DirectBindingsProperty =
				FindFProperty<FMapProperty>(Node->GetClass(), TEXT("PropertyBindings")))
		{
			void* ValuePtr = DirectBindingsProperty->ContainerPtrToValuePtr<void>(Node);
			if (!ValuePtr)
			{
				OutError = FString::Printf(
					TEXT("Cannot access PropertyBindings map on node %s"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			// Signal that we are about to mutate the map; without PreEditChange the
			// node's editor-side caches (incl. ExposedValueHandler staging state)
			// won't invalidate and the next compile may silently reuse stale data.
			Node->Modify();
			Node->PreEditChange(DirectBindingsProperty);

			if (!DirectBindingsProperty->ImportText_Direct(*RemappedBindings, ValuePtr, Node, PPF_None))
			{
				OutError = FString::Printf(
					TEXT("Failed to import PropertyBindings on node %s"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}
			LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("A_after_import_text"), &RemappedBindings);
			EnsureAnimNodeBindingDrivenPinsVisible_ImportBpy(Node, RemappedBindings);

			if (!ValidateAnimNodeBindingImportText_ImportBpy(
					DirectBindingsProperty,
					ValuePtr,
					Node,
					Node,
					RemappedBindings,
					ExpectedKeyCount,
					ExpectedBoundFlagCount,
					TEXT("after_import_text"),
					OutError))
			{
				return false;
			}

			{
				FPropertyChangedEvent ChangeEvent(DirectBindingsProperty, EPropertyChangeType::ValueSet);
				Node->PostEditChangeProperty(ChangeEvent);
			}
			LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("B_after_post_edit_change"), nullptr);

			// Re-apply bindings after editor lifecycle hooks to preserve mapping if
			// node post-edit refresh mutates/rebuilds binding containers.
			if (!DirectBindingsProperty->ImportText_Direct(*RemappedBindings, ValuePtr, Node, PPF_None))
			{
				OutError = FString::Printf(
					TEXT("Failed to re-import PropertyBindings on node %s after PostEditChangeProperty"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}
			LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("C_after_reimport_post_edit"), &RemappedBindings);
			if (!ValidateAnimNodeBindingImportText_ImportBpy(
					DirectBindingsProperty,
					ValuePtr,
					Node,
					Node,
					RemappedBindings,
					ExpectedKeyCount,
					ExpectedBoundFlagCount,
					TEXT("after_reimport_post_edit"),
					OutError))
			{
				return false;
			}

			Node->ReconstructNode();
			LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("D_after_reconstruct"), nullptr);

			ValuePtr = DirectBindingsProperty->ContainerPtrToValuePtr<void>(Node);
			if (!ValuePtr)
			{
				OutError = FString::Printf(
					TEXT("Cannot access PropertyBindings map on node %s after ReconstructNode"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}
			EnsureAnimNodeBindingDrivenPinsVisible_ImportBpy(Node, RemappedBindings);
			if (!DirectBindingsProperty->ImportText_Direct(*RemappedBindings, ValuePtr, Node, PPF_None))
			{
				OutError = FString::Printf(
					TEXT("Failed to re-import PropertyBindings on node %s after ReconstructNode"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}
			LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("E_after_reimport_reconstruct"), &RemappedBindings);
			if (!ValidateAnimNodeBindingImportText_ImportBpy(
					DirectBindingsProperty,
					ValuePtr,
					Node,
					Node,
					RemappedBindings,
					ExpectedKeyCount,
					ExpectedBoundFlagCount,
					TEXT("after_reimport_reconstruct"),
					OutError))
			{
				return false;
			}

			// Mark the owning blueprint structurally modified so the upcoming full
			// compile does NOT take the "skeleton-only" shortcut which skips
			// PropertyAccess extension regeneration.
			if (UBlueprint* OwningBP = FBlueprintEditorUtils::FindBlueprintForNode(Node))
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(OwningBP);
			}

			return true;
		}
	}

	// Preferred path: bindings stored on nested Binding UObject.
	if (!BindingProperty)
	{
		OutError = FString::Printf(
			TEXT("Node %s has neither PropertyBindings nor Binding property"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	UObject* BindingObject = BindingProperty->GetObjectPropertyValue_InContainer(Node);
	if (!BindingObject)
	{
		UClass* BindingClass = BindingProperty->PropertyClass;
		if (!BindingClass)
		{
			OutError = FString::Printf(
				TEXT("Node %s has Binding property without class metadata"),
				*DescribeNode_ImportBpy(Node));
			return false;
		}

		BindingObject = NewObject<UObject>(
			Node,
			BindingClass,
			NAME_None,
			RF_Transactional);
		BindingProperty->SetObjectPropertyValue_InContainer(Node, BindingObject);
	}

	if (!BindingObject)
	{
		OutError = FString::Printf(
			TEXT("Node %s failed to allocate Binding object"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	FProperty* PropertyBindingsProperty =
		BindingObject->GetClass()->FindPropertyByName(TEXT("PropertyBindings"));
	if (!PropertyBindingsProperty)
	{
		OutError = FString::Printf(
			TEXT("Binding object %s on node %s does not expose PropertyBindings"),
			*GetPathNameSafe(BindingObject),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	void* ValuePtr = PropertyBindingsProperty->ContainerPtrToValuePtr<void>(BindingObject);
	if (!ValuePtr)
	{
		OutError = FString::Printf(
			TEXT("Cannot access Binding.PropertyBindings for node %s"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	Node->Modify();
	BindingObject->Modify();
	Node->PreEditChange(BindingProperty);

	if (!PropertyBindingsProperty->ImportText_Direct(*RemappedBindings, ValuePtr, BindingObject, PPF_None))
	{
		OutError = FString::Printf(
			TEXT("Failed to import BindingPropertyBindings on node %s"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}
	LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("A_after_import_text_legacy_binding"), &RemappedBindings);
	EnsureAnimNodeBindingDrivenPinsVisible_ImportBpy(Node, RemappedBindings);

	if (!ValidateAnimNodeBindingImportText_ImportBpy(
			PropertyBindingsProperty,
			ValuePtr,
			BindingObject,
			Node,
			RemappedBindings,
			ExpectedKeyCount,
			ExpectedBoundFlagCount,
			TEXT("after_import_text_legacy_binding"),
			OutError))
	{
		return false;
	}

	{
		FPropertyChangedEvent ChangeEvent(BindingProperty, EPropertyChangeType::ValueSet);
		Node->PostEditChangeProperty(ChangeEvent);
	}
	LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("B_after_post_edit_change_legacy_binding"), nullptr);

	// Re-apply after post-edit refresh to survive node reconstruction hooks.
	if (!PropertyBindingsProperty->ImportText_Direct(*RemappedBindings, ValuePtr, BindingObject, PPF_None))
	{
		OutError = FString::Printf(
			TEXT("Failed to re-import BindingPropertyBindings on node %s after PostEditChangeProperty"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}
	LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("C_after_reimport_post_edit_legacy_binding"), &RemappedBindings);

	if (!ValidateAnimNodeBindingImportText_ImportBpy(
			PropertyBindingsProperty,
			ValuePtr,
			BindingObject,
			Node,
			RemappedBindings,
			ExpectedKeyCount,
			ExpectedBoundFlagCount,
			TEXT("after_reimport_post_edit_legacy_binding"),
			OutError))
	{
		return false;
	}

	Node->ReconstructNode();
	LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("D_after_reconstruct_legacy_binding"), nullptr);

	BindingObject = BindingProperty->GetObjectPropertyValue_InContainer(Node);
	if (!BindingObject)
	{
		OutError = FString::Printf(
			TEXT("Node %s lost Binding object after ReconstructNode"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	PropertyBindingsProperty =
		BindingObject->GetClass()->FindPropertyByName(TEXT("PropertyBindings"));
	if (!PropertyBindingsProperty)
	{
		OutError = FString::Printf(
			TEXT("Binding object %s on node %s lost PropertyBindings after ReconstructNode"),
			*GetPathNameSafe(BindingObject),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	ValuePtr = PropertyBindingsProperty->ContainerPtrToValuePtr<void>(BindingObject);
	if (!ValuePtr)
	{
		OutError = FString::Printf(
			TEXT("Cannot access Binding.PropertyBindings for node %s after ReconstructNode"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	BindingObject->Modify();
	EnsureAnimNodeBindingDrivenPinsVisible_ImportBpy(Node, RemappedBindings);
	if (!PropertyBindingsProperty->ImportText_Direct(*RemappedBindings, ValuePtr, BindingObject, PPF_None))
	{
		OutError = FString::Printf(
			TEXT("Failed to re-import BindingPropertyBindings on node %s after ReconstructNode"),
			*DescribeNode_ImportBpy(Node));
		return false;
	}
	LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("E_after_reimport_reconstruct_legacy_binding"), &RemappedBindings);
	if (!ValidateAnimNodeBindingImportText_ImportBpy(
			PropertyBindingsProperty,
			ValuePtr,
			BindingObject,
			Node,
			RemappedBindings,
			ExpectedKeyCount,
			ExpectedBoundFlagCount,
			TEXT("after_reimport_reconstruct_legacy_binding"),
			OutError))
	{
		return false;
	}

	if (UBlueprint* OwningBP = FBlueprintEditorUtils::FindBlueprintForNode(Node))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(OwningBP);
	}

	return true;
}


void SyncStateResultFunctionRefsToRuntimeNode_ImportBpy(UAnimGraphNode_StateResult* StateResultNode)
{
	if (!StateResultNode)
	{
		return;
	}

	static const FName FunctionRefPropertyNames[] = {
		GET_MEMBER_NAME_CHECKED(UAnimGraphNode_StateResult, StateEntryFunction),
		GET_MEMBER_NAME_CHECKED(UAnimGraphNode_StateResult, StateFullyBlendedInFunction),
		GET_MEMBER_NAME_CHECKED(UAnimGraphNode_StateResult, StateExitFunction),
		GET_MEMBER_NAME_CHECKED(UAnimGraphNode_StateResult, StateFullyBlendedOutFunction)
	};

	StateResultNode->Modify();
	for (const FName PropertyName : FunctionRefPropertyNames)
	{
		if (FProperty* Property = UAnimGraphNode_StateResult::StaticClass()->FindPropertyByName(PropertyName))
		{
			FPropertyChangedEvent ChangeEvent(Property, EPropertyChangeType::ValueSet);
			StateResultNode->PostEditChangeProperty(ChangeEvent);
		}
	}
}

bool ApplyNodeProps_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError,
	bool bDeferNestedGraphImports = false)
{
	if (!Node || !NodeJson.IsValid())
	{
		return true;
	}

	bool bNeedsReconstruct = false;
	bool bApplySelectIndexTypePostReconstruct = false;
	bool bApplySelectValueTypePostReconstruct = false;
	bool bApplySetFieldsVisiblePinsPostReconstruct = false;
	bool bApplyBreakStructVisiblePinsPostReconstruct = false;
	bool bApplyMakeStructVisiblePinsPostReconstruct = false;
	FEdGraphPinType SelectIndexPinType;
	FEdGraphPinType SelectValuePinType;
	TSet<FName> SetFieldsVisiblePins;
	TSet<FName> BreakStructVisiblePins;
	TSet<FName> MakeStructVisiblePins;
	FString CompositeBoundGraphJsonTextPostReconstruct;
	FString StateMachineGraphJsonTextPostReconstruct;
	FString BlendStackGraphJsonTextPostReconstruct;
	FString StateBoundGraphJsonTextPostReconstruct;
	FString ConduitBoundGraphJsonTextPostReconstruct;
	FString TransitionBoundGraphJsonTextPostReconstruct;
	FString TransitionCustomGraphJsonTextPostReconstruct;
	FString BindingPropertyBindingsTextPostReconstruct;

	if (IsEnhancedInputActionNode_ImportBpy(Node))
	{
		if (!ApplyEnhancedInputActionToNode_ImportBpy(Node, NodeJson, OutError))
		{
			return false;
		}

		bNeedsReconstruct = true;
	}

	if (IsGetSubsystemNode_ImportBpy(Node))
	{
		if (!ApplyGetSubsystemClassToNode_ImportBpy(Node, NodeJson, OutError))
		{
			return false;
		}

		bNeedsReconstruct = true;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj->IsValid())
	{
		if (bNeedsReconstruct)
		{
			Node->ReconstructNode();
			if (IsGetSubsystemNode_ImportBpy(Node))
			{
				if (!ApplyGetSubsystemClassToNode_ImportBpy(Node, NodeJson, OutError))
				{
					return false;
				}
			}
			if (IsEnhancedInputActionNode_ImportBpy(Node))
			{
				if (!ApplyEnhancedInputActionToNode_ImportBpy(Node, NodeJson, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
	{
		FString EnumPath;
		if ((*NodePropsObj)->TryGetStringField(TEXT("Enum"), EnumPath))
		{
			if (UEnum* EnumObject = ResolveNamedObject_ImportBpy<UEnum>(EnumPath))
			{
				SelectNode->SetEnum(EnumObject, true);
				bNeedsReconstruct = true;
			}
			else
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve Select enum '%s' on node %s"),
					*EnumPath,
					*DescribeNode_ImportBpy(Node));
				return false;
			}
		}

		FString IndexTypeString;
		if ((*NodePropsObj)->TryGetStringField(TEXT("IndexType"), IndexTypeString))
		{
			ParsePinTypeString_ImportBpy(IndexTypeString, SelectIndexPinType);

			FString IndexContainer;
			if ((*NodePropsObj)->TryGetStringField(TEXT("IndexContainer"), IndexContainer))
			{
				ApplyPinContainerString_ImportBpy(IndexContainer, SelectIndexPinType);
			}

			bApplySelectIndexTypePostReconstruct = true;
			bNeedsReconstruct = true;
		}

		FString ValueTypeString;
		if ((*NodePropsObj)->TryGetStringField(TEXT("ValueType"), ValueTypeString))
		{
			ParsePinTypeString_ImportBpy(ValueTypeString, SelectValuePinType);

			FString ValueContainer;
			if ((*NodePropsObj)->TryGetStringField(TEXT("ValueContainer"), ValueContainer))
			{
				ApplyPinContainerString_ImportBpy(ValueContainer, SelectValuePinType);
			}

			bApplySelectValueTypePostReconstruct = true;
		}
	}

	if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
	{
		FString VisiblePinsText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("VisiblePins"), VisiblePinsText))
		{
			TArray<FString> VisiblePinNames;
			VisiblePinsText.ParseIntoArray(VisiblePinNames, TEXT("|"), false);
			SetFieldsVisiblePins.Reset();
			for (const FString& VisiblePinName : VisiblePinNames)
			{
				if (!VisiblePinName.IsEmpty())
				{
					SetFieldsVisiblePins.Add(FName(*VisiblePinName));
				}
			}

			bApplySetFieldsVisiblePinsPostReconstruct = true;
		}
	}
	else if (UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
	{
		FString VisiblePinsText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("VisiblePins"), VisiblePinsText))
		{
			TArray<FString> VisiblePinNames;
			VisiblePinsText.ParseIntoArray(VisiblePinNames, TEXT("|"), false);
			BreakStructVisiblePins.Reset();
			for (const FString& VisiblePinName : VisiblePinNames)
			{
				if (!VisiblePinName.IsEmpty())
				{
					BreakStructVisiblePins.Add(FName(*VisiblePinName));
				}
			}

			bApplyBreakStructVisiblePinsPostReconstruct = true;
		}
	}
	else if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
	{
		FString VisiblePinsText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("VisiblePins"), VisiblePinsText))
		{
			TArray<FString> VisiblePinNames;
			VisiblePinsText.ParseIntoArray(VisiblePinNames, TEXT("|"), false);
			MakeStructVisiblePins.Reset();
			for (const FString& VisiblePinName : VisiblePinNames)
			{
				if (!VisiblePinName.IsEmpty())
				{
					MakeStructVisiblePins.Add(FName(*VisiblePinName));
				}
			}

			bApplyMakeStructVisiblePinsPostReconstruct = true;
		}
	}

	if (UK2Node_EnumEquality* EnumEqualityNode = Cast<UK2Node_EnumEquality>(Node))
	{
		FString EnumPath;
		if ((*NodePropsObj)->TryGetStringField(TEXT("Enum"), EnumPath) && !EnumPath.IsEmpty())
		{
			UEnum* const EnumObject = ResolveNamedObject_ImportBpy<UEnum>(EnumPath);
			if (!EnumObject)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve enum equality enum '%s' on node %s"),
					*EnumPath,
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			auto ApplyEnumPinType = [EnumObject](UEdGraphPin* Pin)
			{
				if (!Pin)
				{
					return;
				}

				Pin->PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				Pin->PinType.PinSubCategory = NAME_None;
				Pin->PinType.PinSubCategoryObject = EnumObject;
				Pin->PinType.PinValueType = FEdGraphTerminalType();
				Pin->PinType.ContainerType = EPinContainerType::None;
				Pin->PinType.bIsReference = false;
				Pin->PinType.bIsConst = false;
			};

			ApplyEnumPinType(EnumEqualityNode->GetInput1Pin());
			ApplyEnumPinType(EnumEqualityNode->GetInput2Pin());
		}
	}

	if (UAnimGraphNode_BlendListByEnum* BlendListByEnumNode = Cast<UAnimGraphNode_BlendListByEnum>(Node))
	{
		FString EnumPath;
		if ((*NodePropsObj)->TryGetStringField(TEXT("Enum"), EnumPath) && !EnumPath.IsEmpty())
		{
			UEnum* const EnumObject = ResolveNamedObject_ImportBpy<UEnum>(EnumPath);
			if (!EnumObject)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve blend-list enum '%s' on node %s"),
					*EnumPath,
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			RestoreBlendListByEnumEntries_ImportBpy(BlendListByEnumNode, NodeJson, *NodePropsObj, EnumObject);
			bNeedsReconstruct = true;
		}
	}

	if (UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode = Cast<UAnimGraphNode_SaveCachedPose>(Node))
	{
		ReplayCachedPoseNodeState_ImportBpy(SaveCachedPoseNode, *NodePropsObj);
		LogCachedPoseNodeSnapshot_ImportBpy(SaveCachedPoseNode, TEXT("pre_reconstruct_seed"));
		bNeedsReconstruct = true;
	}

	if (UAnimGraphNode_UseCachedPose* UseCachedPoseNode = Cast<UAnimGraphNode_UseCachedPose>(Node))
	{
		ReplayCachedPoseNodeState_ImportBpy(UseCachedPoseNode, *NodePropsObj);
		LogCachedPoseNodeSnapshot_ImportBpy(UseCachedPoseNode, TEXT("pre_reconstruct_seed"));
		bNeedsReconstruct = true;
	}

	if (!bDeferNestedGraphImports)
	{
		FString BoundGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("BoundGraphJson"), BoundGraphJsonText) && !BoundGraphJsonText.IsEmpty())
		{
			if (Cast<UK2Node_Composite>(Node))
			{
				CompositeBoundGraphJsonTextPostReconstruct = BoundGraphJsonText;
				bNeedsReconstruct = true;
			}
			else if (Cast<UAnimStateNode>(Node))
			{
				StateBoundGraphJsonTextPostReconstruct = BoundGraphJsonText;
				bNeedsReconstruct = true;
			}
			else if (Cast<UAnimStateConduitNode>(Node))
			{
				ConduitBoundGraphJsonTextPostReconstruct = BoundGraphJsonText;
				bNeedsReconstruct = true;
			}
			else if (Cast<UAnimStateTransitionNode>(Node))
			{
				TransitionBoundGraphJsonTextPostReconstruct = BoundGraphJsonText;
				bNeedsReconstruct = true;
			}
		}

		FString StateMachineGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("StateMachineGraphJson"), StateMachineGraphJsonText) &&
			!StateMachineGraphJsonText.IsEmpty())
		{
			StateMachineGraphJsonTextPostReconstruct = StateMachineGraphJsonText;
			bNeedsReconstruct = true;
		}

		FString BlendStackGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("BlendStackGraphJson"), BlendStackGraphJsonText) &&
			!BlendStackGraphJsonText.IsEmpty())
		{
			BlendStackGraphJsonTextPostReconstruct = BlendStackGraphJsonText;
			bNeedsReconstruct = true;
		}

		if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			FString CustomTransitionGraphJsonText;
			if ((*NodePropsObj)->TryGetStringField(TEXT("CustomTransitionGraphJson"), CustomTransitionGraphJsonText) &&
				!CustomTransitionGraphJsonText.IsEmpty())
			{
				TransitionCustomGraphJsonTextPostReconstruct = CustomTransitionGraphJsonText;
				bNeedsReconstruct = true;
			}
		}
	}

	auto ShouldSkipGenericNodePropertyApply = [&Node](const FString& Key) -> bool
	{
		if (Key.StartsWith(TEXT("Variable")))
		{
			return true;
		}
		if (Key == TEXT("BoundGraphJson"))
		{
			return true;
		}
		if (Key == TEXT("BlendStackGraphJson"))
		{
			return true;
		}
		if (Key == TEXT("StateMachineGraphJson") ||
			Key == TEXT("CustomTransitionGraphJson") ||
			Key == TEXT("AliasedStateUids"))
		{
			return true;
		}
		if (Key == TEXT("BoundGraph") ||
			Key == TEXT("EditorStateMachineGraph") ||
			Key == TEXT("CustomTransitionGraph"))
		{
			return true;
		}
		if (Cast<UK2Node_EnumEquality>(Node) && Key == TEXT("Enum"))
		{
			return true;
		}
		if (Cast<UAnimGraphNode_BlendListByEnum>(Node) && Key == TEXT("Enum"))
		{
			return true;
		}
		if ((Cast<UAnimGraphNode_SaveCachedPose>(Node) || Cast<UAnimGraphNode_UseCachedPose>(Node)) &&
			(Key == TEXT("CacheName") || Key == TEXT("CachePoseName")))
		{
			return true;
		}
		if (Cast<UK2Node_Select>(Node) &&
			(Key == TEXT("Enum") ||
			 Key == TEXT("IndexType") ||
			 Key == TEXT("IndexContainer") ||
			 Key == TEXT("ValueType") ||
			 Key == TEXT("ValueContainer")))
		{
			return true;
		}
		if (Cast<UK2Node_SetFieldsInStruct>(Node) && Key == TEXT("VisiblePins"))
		{
			return true;
		}
		if (Key == TEXT("BindingPropertyBindings"))
		{
			return true;
		}
		return false;
	};

	auto ReplayNestedGraphsPostReconstruct = [&]() -> bool
	{
		UBlueprint* const OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
		auto ResolveNestedGraphBlueprint = [&](UEdGraph* NestedGraph) -> UBlueprint*
		{
			if (OwningBlueprint)
			{
				return OwningBlueprint;
			}

			return NestedGraph ? FBlueprintEditorUtils::FindBlueprintForGraph(NestedGraph) : nullptr;
		};

		if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			if (!EnsureTransitionBoundGraphOwnership_ImportBpy(TransitionNode, OutError))
			{
				return false;
			}
		}

		if (UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
		{
			if (!EnsureConduitBoundGraphOwnership_ImportBpy(ConduitNode, OutError))
			{
				return false;
			}
		}

		if (!CompositeBoundGraphJsonTextPostReconstruct.IsEmpty())
		{
			UK2Node_Composite* const CompositeNode = Cast<UK2Node_Composite>(Node);
			if (!CompositeNode || !CompositeNode->BoundGraph)
			{
				OutError = FString::Printf(
					TEXT("Composite node %s lost its bound graph after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			TSharedPtr<FJsonObject> BoundGraphJson;
			TSharedRef<TJsonReader<>> Reader =
				TJsonReaderFactory<>::Create(CompositeBoundGraphJsonTextPostReconstruct);
			if (!FJsonSerializer::Deserialize(Reader, BoundGraphJson) || !BoundGraphJson.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Cannot parse BoundGraphJson on node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!OwningBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for composite node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!UBPDirectImporter::PopulateGraph(
					OwningBlueprint,
					CompositeNode->BoundGraph,
					BoundGraphJson,
					true,
					OutError))
			{
				return false;
			}
		}

		if (!StateMachineGraphJsonTextPostReconstruct.IsEmpty())
		{
			UAnimGraphNode_StateMachineBase* const StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
			if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
			{
				OutError = FString::Printf(
					TEXT("State machine node %s lost its editor graph after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			UBlueprint* const NestedBlueprint = ResolveNestedGraphBlueprint(StateMachineNode->EditorStateMachineGraph);
			if (!NestedBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for state machine node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					NestedBlueprint,
					StateMachineNode->EditorStateMachineGraph,
					StateMachineGraphJsonTextPostReconstruct,
					OutError))
			{
				return false;
			}

			if (!ReplayStateMachineAliasNodesFromGraphJsonText_ImportBpy(
					NestedBlueprint,
					StateMachineNode->EditorStateMachineGraph,
					StateMachineGraphJsonTextPostReconstruct,
					TEXT("StateMachineNestedReplay"),
					OutError))
			{
				return false;
			}
		}

		if (!BlendStackGraphJsonTextPostReconstruct.IsEmpty())
		{
			LogBlendStackGraphBindingsAndDuplicates_ImportBpy(
				Node,
				TEXT("NestedReplayBeforePopulate"));

			UEdGraph* const BlendStackGraph = ResolveBlendStackGraph_ImportBpy(Node);
			if (!BlendStackGraph)
			{
				OutError = FString::Printf(
					TEXT("BlendStack node %s lost its blend stack graph after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}
			if (!EnsureBlendStackGraphOwnership_ImportBpy(Node, BlendStackGraph, OutError))
			{
				return false;
			}

			UBlueprint* const NestedBlueprint = ResolveNestedGraphBlueprint(BlendStackGraph);
			if (!NestedBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for blend stack node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					NestedBlueprint,
					BlendStackGraph,
					BlendStackGraphJsonTextPostReconstruct,
					OutError))
			{
				return false;
			}

			LogBlendStackGraphBindingsAndDuplicates_ImportBpy(
				Node,
				TEXT("NestedReplayAfterPopulate"));
		}

		if (!StateBoundGraphJsonTextPostReconstruct.IsEmpty())
		{
			UAnimStateNode* const StateNode = Cast<UAnimStateNode>(Node);
			if (!StateNode || !StateNode->BoundGraph)
			{
				OutError = FString::Printf(
					TEXT("State node %s lost its bound graph after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			UBlueprint* const NestedBlueprint = ResolveNestedGraphBlueprint(StateNode->BoundGraph);
			if (!NestedBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for state node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					NestedBlueprint,
					StateNode->BoundGraph,
					StateBoundGraphJsonTextPostReconstruct,
					OutError))
			{
				return false;
			}
		}

		if (!ConduitBoundGraphJsonTextPostReconstruct.IsEmpty())
		{
			UAnimStateConduitNode* const ConduitNode = Cast<UAnimStateConduitNode>(Node);
			if (!ConduitNode)
			{
				OutError = FString::Printf(
					TEXT("Conduit node %s lost its bound graph after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!EnsureConduitBoundGraphOwnership_ImportBpy(ConduitNode, OutError))
			{
				return false;
			}

			if (!ConduitNode->BoundGraph)
			{
				OutError = FString::Printf(
					TEXT("Conduit node %s is missing a bound graph after ownership repair"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			UBlueprint* const NestedBlueprint = ResolveNestedGraphBlueprint(ConduitNode->BoundGraph);
			if (!NestedBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for conduit node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					NestedBlueprint,
					ConduitNode->BoundGraph,
					ConduitBoundGraphJsonTextPostReconstruct,
					OutError))
			{
				return false;
			}
		}

		if (!TransitionBoundGraphJsonTextPostReconstruct.IsEmpty())
		{
			UAnimStateTransitionNode* const TransitionNode = Cast<UAnimStateTransitionNode>(Node);
			if (!TransitionNode)
			{
				OutError = FString::Printf(
					TEXT("Transition node %s lost its bound graph after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!EnsureTransitionBoundGraphOwnership_ImportBpy(TransitionNode, OutError))
			{
				return false;
			}

			if (!TransitionNode->GetBoundGraph())
			{
				OutError = FString::Printf(
					TEXT("Transition node %s is missing a bound graph after ownership repair"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			UBlueprint* const NestedBlueprint = ResolveNestedGraphBlueprint(TransitionNode->GetBoundGraph());
			if (!NestedBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for transition node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					NestedBlueprint,
					TransitionNode->GetBoundGraph(),
					TransitionBoundGraphJsonTextPostReconstruct,
					OutError))
			{
				return false;
			}
		}

		if (!TransitionCustomGraphJsonTextPostReconstruct.IsEmpty())
		{
			UAnimStateTransitionNode* const TransitionNode = Cast<UAnimStateTransitionNode>(Node);
			if (!TransitionNode)
			{
				OutError = FString::Printf(
					TEXT("Transition node %s is invalid after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!EnsureTransitionCustomGraphExists_ImportBpy(TransitionNode, OutError))
			{
				return false;
			}

			UBlueprint* const NestedBlueprint = ResolveNestedGraphBlueprint(TransitionNode->GetCustomTransitionGraph());
			if (!NestedBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for transition custom graph node %s after reconstruct"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					NestedBlueprint,
					TransitionNode->GetCustomTransitionGraph(),
					TransitionCustomGraphJsonTextPostReconstruct,
					OutError))
			{
				return false;
			}
		}

		return true;
	};

	auto ApplyDeferredAnimBindingPropertyBindings = [&]() -> bool
	{
		if (BindingPropertyBindingsTextPostReconstruct.IsEmpty())
		{
			return true;
		}

		if (!ApplyAnimNodeBindingPropertyBindings_ImportBpy(
				Node,
				BindingPropertyBindingsTextPostReconstruct,
				OutError))
		{
			return false;
		}

		return true;
	};

	auto ValidateBlendStackGraphPostReplay = [&](const TCHAR* StageTag) -> bool
	{
		if (BlendStackGraphJsonTextPostReconstruct.IsEmpty())
		{
			return true;
		}

		TSharedPtr<FJsonObject> BlendStackGraphJson;
		TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(BlendStackGraphJsonTextPostReconstruct);
		if (!FJsonSerializer::Deserialize(Reader, BlendStackGraphJson) || !BlendStackGraphJson.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Cannot parse BlendStackGraphJson for node %s during %s"),
				*DescribeNode_ImportBpy(Node),
				StageTag ? StageTag : TEXT("post_replay_validation"));
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ExpectedNodes = nullptr;
		const int32 ExpectedNodeCount =
			BlendStackGraphJson->TryGetArrayField(TEXT("nodes"), ExpectedNodes) && ExpectedNodes
				? ExpectedNodes->Num()
				: 0;

		UEdGraph* const LiveBlendStackGraph = ResolveBlendStackGraph_ImportBpy(Node);
		const int32 LiveNodeCount = LiveBlendStackGraph ? LiveBlendStackGraph->Nodes.Num() : 0;

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[ExportBpy][ImportDiag][BlendStackGate][%s] node=%s expected_nodes=%d live_nodes=%d live_graph=%s"),
			StageTag ? StageTag : TEXT("post_replay_validation"),
			*DescribeNode_ImportBpy(Node),
			ExpectedNodeCount,
			LiveNodeCount,
			*GetPathNameSafe(LiveBlendStackGraph));

		if (ExpectedNodeCount > 2 && LiveNodeCount <= 2)
		{
			OutError = FString::Printf(
				TEXT("BlendStack graph regression on %s (%s): expected_nodes=%d live_nodes=%d. ")
				TEXT("Likely post-binding reconstruct wiped nested graph."),
				*DescribeNode_ImportBpy(Node),
				StageTag ? StageTag : TEXT("post_replay_validation"),
				ExpectedNodeCount,
				LiveNodeCount);
			return false;
		}

		return true;
	};

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*NodePropsObj)->Values)
	{
		const FString& Key = Entry.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Entry.Value;
		if (!JsonValue.IsValid())
		{
			continue;
		}

		if (ShouldSkipGenericNodePropertyApply(Key))
		{
			if (Key == TEXT("BindingPropertyBindings"))
			{
				BindingPropertyBindingsTextPostReconstruct = JsonValue->AsString();
			}
			continue;
		}

		if (UK2Node_SwitchEnum* SwitchEnumNode = Cast<UK2Node_SwitchEnum>(Node))
		{
			if (Key == TEXT("Enum"))
			{
				if (UEnum* EnumObject = ResolveNamedObject_ImportBpy<UEnum>(JsonValue->AsString()))
				{
					SwitchEnumNode->Enum = EnumObject;
					bNeedsReconstruct = true;
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Cannot resolve switch enum '%s' on node %s"),
						*JsonValue->AsString(),
						*DescribeNode_ImportBpy(Node));
					return false;
				}
				continue;
			}
		}

		if (UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node))
		{
			if (Key == TEXT("StructType"))
			{
				if (UScriptStruct* StructType = ResolveNamedObject_ImportBpy<UScriptStruct>(JsonValue->AsString()))
				{
					StructNode->StructType = StructType;
					bNeedsReconstruct = true;
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Cannot resolve struct type '%s' on node %s"),
						*JsonValue->AsString(),
						*DescribeNode_ImportBpy(Node));
					return false;
				}
				continue;
			}
		}

		if (UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
		{
			if (Key == TEXT("TargetType"))
			{
				if (UClass* TargetClass = ResolveNamedObject_ImportBpy<UClass>(JsonValue->AsString()))
				{
					DynamicCastNode->TargetType = TargetClass;
					bNeedsReconstruct = true;
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Cannot resolve dynamic cast target '%s' on node %s"),
						*JsonValue->AsString(),
						*DescribeNode_ImportBpy(Node));
					return false;
				}
				continue;
			}
			if (Key == TEXT("CastIsPure"))
			{
				const FString BoolText = JsonValue->AsString();
				const bool bIsPureCast =
					BoolText.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
					BoolText == TEXT("1");
				DynamicCastNode->SetPurity(bIsPureCast);
				continue;
			}
		}

		if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
		{
			if (Key == TEXT("MacroGraph"))
			{
				if (UEdGraph* MacroGraph = ResolveMacroGraph_ImportBpy(Node->GetGraph(), JsonValue->AsString(), FString()))
				{
					MacroNode->SetMacroGraph(MacroGraph);
					bNeedsReconstruct = true;
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Cannot resolve macro graph '%s' on node %s"),
						*JsonValue->AsString(),
						*DescribeNode_ImportBpy(Node));
					return false;
				}
				continue;
			}
		}

		if (FProperty* Property = Node->GetClass()->FindPropertyByName(FName(*Key)))
		{
			const bool bIsAnimNodeFunctionRefProperty = IsAnimNodeFunctionRefFieldName_ImportBpy(Key);
			ApplyJsonValueToProperty_ImportBpy(Node, Property, JsonValue);
			if (!bIsAnimNodeFunctionRefProperty)
			{
				bNeedsReconstruct = true;
			}
		}
	}

	if (bNeedsReconstruct)
	{
		const UK2Node_AnimGetter* const AnimGetterNode = Cast<UK2Node_AnimGetter>(Node);
		const bool bSkipReconstructForAnimGetterWithoutOwner =
			AnimGetterNode &&
			!AnimGetterNode->HasValidBlueprint();
		const bool bSkipReconstructForNestedAnimGraphOwner =
			Cast<UAnimGraphNode_StateMachineBase>(Node) != nullptr ||
			Cast<UAnimStateNode>(Node) != nullptr ||
			Cast<UAnimStateConduitNode>(Node) != nullptr ||
			Cast<UAnimStateTransitionNode>(Node) != nullptr;
		const bool bAllowReconstruct =
			!bSkipReconstructForAnimGetterWithoutOwner &&
			!bSkipReconstructForNestedAnimGraphOwner;

		if (bAllowReconstruct)
		{
			LogCachedPoseNodeSnapshot_ImportBpy(Node, TEXT("before_reconstruct"));
			Node->ReconstructNode();
			LogCachedPoseNodeSnapshot_ImportBpy(Node, TEXT("after_reconstruct"));
			if (IsGetSubsystemNode_ImportBpy(Node))
			{
				if (!ApplyGetSubsystemClassToNode_ImportBpy(Node, NodeJson, OutError))
				{
					return false;
				}
			}
			if (IsEnhancedInputActionNode_ImportBpy(Node))
			{
				if (!ApplyEnhancedInputActionToNode_ImportBpy(Node, NodeJson, OutError))
				{
					return false;
				}
			}

			// ReconstructNode() can reset editor node state for AnimGraph and transition nodes.
			// Replay safe property writes once more so exported node_props survive round-trip.
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*NodePropsObj)->Values)
			{
				const FString& Key = Entry.Key;
				const TSharedPtr<FJsonValue>& JsonValue = Entry.Value;
				if (!JsonValue.IsValid() || ShouldSkipGenericNodePropertyApply(Key))
				{
					continue;
				}

				if (FProperty* Property = Node->GetClass()->FindPropertyByName(FName(*Key)))
				{
					ApplyJsonValueToProperty_ImportBpy(Node, Property, JsonValue);
				}
			}

			ReplayCachedPoseNodeState_ImportBpy(Node, *NodePropsObj);
			LogCachedPoseNodeSnapshot_ImportBpy(Node, TEXT("after_cached_pose_replay_post_reconstruct"));
		}

		// Apply binding map before nested graph replay. PostEditChangeProperty() in
		// binding application may trigger a hidden reconstruct on AnimGraph nodes.
		// Replaying nested graphs afterwards guarantees final graph contents survive.
		if (!ApplyDeferredAnimBindingPropertyBindings())
		{
			return false;
		}

		if (!ReplayNestedGraphsPostReconstruct())
		{
			return false;
		}

		if (!ValidateBlendStackGraphPostReplay(TEXT("post_reconstruct_nested_replay")))
		{
			return false;
		}
	}

	if (bApplySetFieldsVisiblePinsPostReconstruct)
	{
		if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
		{
			ApplySetFieldsVisiblePins_ImportBpy(SetFieldsNode, SetFieldsVisiblePins);
		}
	}

	if (bApplyBreakStructVisiblePinsPostReconstruct)
	{
		if (UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
		{
			ApplyStructMemberVisiblePins_ImportBpy(BreakStructNode, BreakStructVisiblePins);
		}
	}

	if (bApplyMakeStructVisiblePinsPostReconstruct)
	{
		if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
		{
			ApplyStructMemberVisiblePins_ImportBpy(MakeStructNode, MakeStructVisiblePins);
		}
	}

	if (bApplySelectIndexTypePostReconstruct)
	{
		if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
		{
			if (UEdGraphPin* IndexPin = SelectNode->GetIndexPin())
			{
				IndexPin->PinType = SelectIndexPinType;
				SelectNode->ChangePinType(IndexPin);
			}
		}
	}

	if (bApplySelectValueTypePostReconstruct)
	{
		if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
		{
			if (UEdGraphPin* ReturnPin = SelectNode->GetReturnValuePin())
			{
				ReturnPin->PinType = SelectValuePinType;
				SelectNode->ChangePinType(ReturnPin);
			}
		}
	}

	if (!bNeedsReconstruct)
	{
		if (!ApplyDeferredAnimBindingPropertyBindings())
		{
			return false;
		}
	}

	if (Cast<UAnimGraphNode_SaveCachedPose>(Node) || Cast<UAnimGraphNode_UseCachedPose>(Node))
	{
		ReplayCachedPoseNodeState_ImportBpy(Node, *NodePropsObj);
		ResolveUseCachedPoseLinksInGraph_ImportBpy(Node->GetGraph());
		LogCachedPoseNodeSnapshot_ImportBpy(Node, TEXT("after_graph_cached_pose_resolve"));
	}

	if (UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
	{
		const FString ExpectedLayerName = GetNodePropString_ImportBpy(NodeJson, TEXT("LinkedAnimLayerLayer"));
		const FString ExpectedInterfaceClassPath = GetNodePropString_ImportBpy(NodeJson, TEXT("LinkedAnimLayerInterfaceClass"));
		if (!ExpectedLayerName.IsEmpty() && LinkedLayerNode->Node.Layer == NAME_None)
		{
			LinkedLayerNode->Node.Layer = FName(*ExpectedLayerName);
		}

		if (LinkedLayerNode->Node.Layer != NAME_None)
		{
			bool bResolvedInterface = false;
			UClass* ExpectedInterfaceClass = nullptr;
			const bool bSourceForcesNoInterface =
				ExpectedInterfaceClassPath.IsEmpty() ||
				ExpectedInterfaceClassPath.Equals(TEXT("None"), ESearchCase::IgnoreCase);
			const bool bHasExplicitExpectedInterface =
				!ExpectedInterfaceClassPath.IsEmpty() && !bSourceForcesNoInterface;

			if (bSourceForcesNoInterface)
			{
				// Respect source self-layer contract when Interface=None is serialized.
				LinkedLayerNode->Node.Interface = nullptr;
				LinkedLayerNode->InterfaceGuid.Invalidate();
				bResolvedInterface = true;
			}

			if (bHasExplicitExpectedInterface)
			{
				ExpectedInterfaceClass = ResolveInterfaceClassFromPath_ImportBpy(ExpectedInterfaceClassPath);
			}

			if (ExpectedInterfaceClass)
			{
				LinkedLayerNode->Node.Interface = ExpectedInterfaceClass;
				if (UFunction* ExpectedLayerFunction = ExpectedInterfaceClass->FindFunctionByName(LinkedLayerNode->Node.Layer))
				{
					LinkedLayerNode->InterfaceGuid =
						FBlueprintEditorUtils::FindInterfaceFunctionGuid(
							ExpectedLayerFunction,
							ExpectedInterfaceClass);
				}
				else if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint()))
				{
					for (const FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
					{
						const UClass* DescClass = InterfaceDesc.Interface
							? InterfaceDesc.Interface->GetAuthoritativeClass()
							: nullptr;
						if (!DescClass || DescClass != ExpectedInterfaceClass->GetAuthoritativeClass())
						{
							continue;
						}

						for (UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
						{
							if (InterfaceGraph && InterfaceGraph->GetFName() == LinkedLayerNode->Node.Layer)
							{
								LinkedLayerNode->InterfaceGuid = InterfaceGraph->InterfaceGuid;
								break;
							}
						}
					}
				}

				// Treat explicit interface class assignment as resolved even when
				// layer function metadata is unavailable at this stage. Final
				// strict contract checks after compile/reload still enforce parity.
				bResolvedInterface = LinkedLayerNode->Node.Interface.Get() != nullptr;
			}

			if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint()))
			{
				if (!bResolvedInterface)
				{
					LinkedLayerNode->Node.Interface = nullptr;
					LinkedLayerNode->InterfaceGuid.Invalidate();
				}
				const FName LayerName = LinkedLayerNode->Node.Layer;
				UFunction* InterfaceFunction = nullptr;
				if (!bResolvedInterface &&
					bHasExplicitExpectedInterface)
				{
					for (FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
					{
						UClass* DescInterfaceClass = InterfaceDesc.Interface
							? InterfaceDesc.Interface->GetAuthoritativeClass()
							: nullptr;
						if (!DescInterfaceClass)
						{
							continue;
						}

						const FString DescClassPath = DescInterfaceClass->GetPathName();
						const FString DescInterfacePath = InterfaceDesc.Interface->GetPathName();
						if (!DescClassPath.Equals(ExpectedInterfaceClassPath, ESearchCase::CaseSensitive) &&
							!DescInterfacePath.Equals(ExpectedInterfaceClassPath, ESearchCase::CaseSensitive))
						{
							continue;
						}

						LinkedLayerNode->Node.Interface = DescInterfaceClass;
						for (UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
						{
							if (InterfaceGraph && InterfaceGraph->GetFName() == LayerName)
							{
								LinkedLayerNode->InterfaceGuid = InterfaceGraph->InterfaceGuid;
								break;
							}
						}
						bResolvedInterface = LinkedLayerNode->Node.Interface.Get() != nullptr;
						if (bResolvedInterface)
						{
							break;
						}
					}
				}
				if (!bResolvedInterface)
				{
					if (UClass* InterfaceClass = GetInterfaceOwnerClass_ImportBpy(
							CurrentBlueprint,
							LayerName.ToString(),
							&InterfaceFunction))
					{
						LinkedLayerNode->Node.Interface = InterfaceClass;
						bResolvedInterface = true;
						if (InterfaceFunction)
						{
							LinkedLayerNode->InterfaceGuid =
								FBlueprintEditorUtils::FindInterfaceFunctionGuid(
									InterfaceFunction,
									InterfaceClass);
						}
					}
				}

				if (!bResolvedInterface)
				{
					for (FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
					{
						for (UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
						{
							if (InterfaceGraph && InterfaceGraph->GetFName() == LayerName)
							{
								LinkedLayerNode->Node.Interface = InterfaceDesc.Interface;
								LinkedLayerNode->InterfaceGuid = InterfaceGraph->InterfaceGuid;
								bResolvedInterface = LinkedLayerNode->Node.Interface.Get() != nullptr;
								break;
							}
						}
						if (bResolvedInterface)
						{
							break;
						}
					}
				}
			}

			if (!bSourceForcesNoInterface &&
				(!bResolvedInterface || LinkedLayerNode->Node.Interface.Get() == nullptr))
			{
				if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint()))
				{
					// Final fallback: if exactly one interface is implemented on this ABP,
					// bind it to avoid losing layer wiring due transient lookup timing.
					int32 InterfaceCount = 0;
					UClass* SoleInterfaceClass = nullptr;
					for (const FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
					{
						if (UClass* CandidateClass = InterfaceDesc.Interface
								? InterfaceDesc.Interface->GetAuthoritativeClass()
								: nullptr)
						{
							++InterfaceCount;
							SoleInterfaceClass = CandidateClass;
						}
					}
					if (InterfaceCount == 1 && SoleInterfaceClass)
					{
						LinkedLayerNode->Node.Interface = SoleInterfaceClass;
						bResolvedInterface = true;
					}
				}
			}

			if (!bSourceForcesNoInterface &&
				(!bResolvedInterface || LinkedLayerNode->Node.Interface.Get() == nullptr))
			{
				TArray<FString> ImplementedInterfaces;
				if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint()))
				{
					for (const FBPInterfaceDescription& InterfaceDesc : CurrentBlueprint->ImplementedInterfaces)
					{
						if (InterfaceDesc.Interface)
						{
							ImplementedInterfaces.Add(GetPathNameSafe(InterfaceDesc.Interface));
						}
					}
				}
				UE_LOG(
					LogTemp,
					Error,
					TEXT("[ExportBpy][ImportDiag] LinkedAnimLayer interface unresolved during node apply: node=%s layer=%s implemented_interfaces=[%s]"),
					*DescribeNode_ImportBpy(LinkedLayerNode),
					*LinkedLayerNode->Node.Layer.ToString(),
					*FString::Join(ImplementedInterfaces, TEXT(",")));
				const bool bCanDeferResolutionCheck =
					!ExpectedInterfaceClassPath.IsEmpty() &&
					!ExpectedInterfaceClassPath.Equals(TEXT("None"), ESearchCase::IgnoreCase);
				if (!bCanDeferResolutionCheck)
				{
					OutError = FString::Printf(
						TEXT("LinkedAnimLayer interface unresolved: node=%s layer=%s"),
						*DescribeNode_ImportBpy(LinkedLayerNode),
						*LinkedLayerNode->Node.Layer.ToString());
					return false;
				}
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy][ImportDiag] Deferring LinkedAnimLayer unresolved check to strict post-compile validation: node=%s layer=%s expected_interface=%s"),
					*DescribeNode_ImportBpy(LinkedLayerNode),
					*LinkedLayerNode->Node.Layer.ToString(),
					*ExpectedInterfaceClassPath);
			}

			if (ExpectedInterfaceClass)
			{
				UClass* LiveInterfaceClass = LinkedLayerNode->Node.Interface.Get();
				const UClass* LiveCanonical = LiveInterfaceClass ? LiveInterfaceClass->GetAuthoritativeClass() : nullptr;
				const UClass* ExpectedCanonical = ExpectedInterfaceClass->GetAuthoritativeClass();
				if (!LiveCanonical || !ExpectedCanonical || LiveCanonical != ExpectedCanonical)
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[ExportBpy][ImportDiag] Deferring LinkedAnimLayer mismatch to strict post-compile validation: node=%s layer=%s expected=%s actual=%s"),
						*DescribeNode_ImportBpy(LinkedLayerNode),
						*LinkedLayerNode->Node.Layer.ToString(),
						*GetPathNameSafe(ExpectedCanonical),
						*GetPathNameSafe(LiveCanonical));
				}
			}
		}
	}

	if (UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(Node))
	{
		SyncStateResultFunctionRefsToRuntimeNode_ImportBpy(StateResultNode);
	}

	RemoveUnlinkedOrphanPins_ImportBpy(Node);

	return true;
}

bool ApplyPinDefaults_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError,
	bool bAllowDeferredUnresolvedPins = false)
{
	if (!Node || !NodeJson.IsValid())
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsObj) || !DefaultsObj->IsValid())
	{
		return true;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*DefaultsObj)->Values)
	{
		const FString& SerializedPinName = Entry.Key;
		const auto ResolvePinForDefault = [&]() -> UEdGraphPin*
		{
			if (UEdGraphPin* InputPin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Input))
			{
				return InputPin;
			}

			// Function entry parameter defaults live on output pins.
			if (Node->IsA<UK2Node_FunctionEntry>())
			{
				if (UEdGraphPin* OutputPin =
						FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Output))
				{
					return OutputPin;
				}
			}

			return nullptr;
		};

		if (UEdGraphPin* Pin = ResolvePinForDefault())
		{
			ApplyDefaultToPin_ImportBpy(Pin, Entry.Value);
		}
		else if (bAllowDeferredUnresolvedPins)
		{
			continue;
		}
		else
		{
			Node->ReconstructNode();
			if (UEdGraphPin* RetriedPin = ResolvePinForDefault())
			{
				ApplyDefaultToPin_ImportBpy(RetriedPin, Entry.Value);
				continue;
			}

			OutError = FString::Printf(
				TEXT("Cannot resolve pin '%s' while applying default on node %s"),
				*SerializedPinName,
				*DescribeNode_ImportBpy(Node));
			return false;
		}
	}

	return true;
}

UEdGraphPin* ResolvePinForSerializedDefault_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	const FString& SerializedPinName)
{
	if (!Node)
	{
		return nullptr;
	}

	if (UEdGraphPin* InputPin =
			FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Input))
	{
		return InputPin;
	}

	if (Node->IsA<UK2Node_FunctionEntry>())
	{
		if (UEdGraphPin* OutputPin =
				FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Output))
		{
			return OutputPin;
		}
	}

	return nullptr;
}

FString ReadPinRawDefaultValue_ImportBpy(const UEdGraphPin* Pin)
{
	if (!Pin || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return FString();
	}

	if (Pin->DefaultObject)
	{
		return Pin->DefaultObject->GetPathName();
	}

	if (!Pin->DefaultTextValue.IsEmpty())
	{
		return Pin->DefaultTextValue.ToString();
	}

	return Pin->DefaultValue;
}

bool TryParseBooleanText_ImportBpy(const FString& Text, bool& OutValue)
{
	FString Normalized = Text;
	Normalized.TrimStartAndEndInline();
	if (Normalized.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Normalized == TEXT("1"))
	{
		OutValue = true;
		return true;
	}
	if (Normalized.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Normalized == TEXT("0"))
	{
		OutValue = false;
		return true;
	}
	return false;
}

bool TryParseNumericText_ImportBpy(const FString& Text, double& OutValue)
{
	FString Normalized = Text;
	Normalized.TrimStartAndEndInline();
	if (Normalized.IsEmpty())
	{
		return false;
	}

	if (Normalized.StartsWith(TEXT("\"")) && Normalized.EndsWith(TEXT("\"")) && Normalized.Len() >= 2)
	{
		Normalized = Normalized.Mid(1, Normalized.Len() - 2);
		Normalized.TrimStartAndEndInline();
	}

	return LexTryParseString(OutValue, *Normalized);
}

FString CanonicalizeSerializedDefaultValue_ImportBpy(const FString& Value)
{
	FString Normalized = Value;
	Normalized.TrimStartAndEndInline();
	if (Normalized.StartsWith(TEXT("\"")) && Normalized.EndsWith(TEXT("\"")) && Normalized.Len() >= 2)
	{
		Normalized = Normalized.Mid(1, Normalized.Len() - 2);
		Normalized.TrimStartAndEndInline();
	}

	// UE 5.7 may emit equivalent empty-array forms as "Samples=()" or "Samples=".
	int32 SearchStart = 0;
	const FString SamplesToken = TEXT("Samples=");
	while (true)
	{
		const int32 TokenIndex = Normalized.Find(SamplesToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
		if (TokenIndex == INDEX_NONE)
		{
			break;
		}
		const int32 ValueStart = TokenIndex + SamplesToken.Len();
		if (ValueStart + 1 < Normalized.Len() &&
			Normalized.Mid(ValueStart, 2).Equals(TEXT("()"), ESearchCase::CaseSensitive))
		{
			Normalized.RemoveAt(ValueStart, 2, EAllowShrinking::No);
		}
		SearchStart = ValueStart + 1;
	}
	return Normalized;
}

bool AreSerializedDefaultValuesEquivalent_ImportBpy(const FString& ExpectedValue, const FString& ActualValue)
{
	FString Expected = CanonicalizeSerializedDefaultValue_ImportBpy(ExpectedValue);
	FString Actual = CanonicalizeSerializedDefaultValue_ImportBpy(ActualValue);

	if (Expected.Equals(Actual, ESearchCase::CaseSensitive))
	{
		return true;
	}

	bool ExpectedBool = false;
	bool ActualBool = false;
	if (TryParseBooleanText_ImportBpy(Expected, ExpectedBool) &&
		TryParseBooleanText_ImportBpy(Actual, ActualBool))
	{
		return ExpectedBool == ActualBool;
	}

	double ExpectedNumber = 0.0;
	double ActualNumber = 0.0;
	if (TryParseNumericText_ImportBpy(Expected, ExpectedNumber) &&
		TryParseNumericText_ImportBpy(Actual, ActualNumber))
	{
		return FMath::IsNearlyEqual(ExpectedNumber, ActualNumber, KINDA_SMALL_NUMBER);
	}

	return false;
}

bool ReplayAndValidateSerializedNodeDefaults_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const FString& GraphName,
	FString& OutError)
{
	if (!NodesArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
		if (!NodeObj.IsValid())
		{
			continue;
		}

		const FString Uid = NodeObj->GetStringField(TEXT("uid"));
		UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		if (!ApplyPinDefaults_ImportBpy(*ExistingNode, NodeObj, OutError, false))
		{
			return false;
		}
		if (!ApplyPinIds_ImportBpy(*ExistingNode, NodeObj, OutError))
		{
			return false;
		}
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
		if (!NodeObj.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
		if (!NodeObj->TryGetObjectField(TEXT("defaults"), DefaultsObj) || !DefaultsObj || !(*DefaultsObj).IsValid())
		{
			continue;
		}

		const FString Uid = NodeObj->GetStringField(TEXT("uid"));
		UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		UEdGraphNode* const Node = *ExistingNode;
		const FString NodeGuidText = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*DefaultsObj)->Values)
		{
			const FString& SerializedPinName = Entry.Key;
			UEdGraphPin* Pin = ResolvePinForSerializedDefault_ImportBpy(Node, NodeObj, SerializedPinName);
			if (!Pin)
			{
				OutError = FString::Printf(
					TEXT("[%s] Missing default pin '%s' on node %s (guid=%s) during final defaults validation"),
					*GraphName,
					*SerializedPinName,
					*DescribeNode_ImportBpy(Node),
					*NodeGuidText);
				return false;
			}

			const FString ExpectedValue = JsonValueToDefaultString_ImportBpy(Entry.Value);
			const FString ActualValue = ReadPinRawDefaultValue_ImportBpy(Pin);
			if (!AreSerializedDefaultValuesEquivalent_ImportBpy(ExpectedValue, ActualValue))
			{
				const FString NormalizedNodeGuid = NormalizeGuidForTracking_ImportBpy(NodeGuidText);
				if (IsTrackedTraversalGuid_ImportBpy(NormalizedNodeGuid))
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[ExportBpy][PromotableTrace][strict_default_mismatch] graph=%s guid=%s node=%s pin=%s expected='%s' actual='%s' pin_state={%s}"),
						*GraphName,
						*NormalizedNodeGuid,
						*DescribeNode_ImportBpy(Node),
						*SerializedPinName,
						*ExpectedValue,
						*ActualValue,
						*DescribeTrackedPinState_ImportBpy(Pin));
				}

				OutError = FString::Printf(
					TEXT("[%s] Default mismatch on node %s (guid=%s), pin '%s': expected='%s' actual='%s'"),
					*GraphName,
					*DescribeNode_ImportBpy(Node),
					*NodeGuidText,
					*SerializedPinName,
					*ExpectedValue,
					*ActualValue);
				return false;
			}
		}
	}

	return true;
}

bool ShouldSkipStrictDefaultValidation_ImportBpy()
{
	static const FString EnvValue =
		FPlatformMisc::GetEnvironmentVariable(TEXT("EXPORTBPY_SKIP_STRICT_DEFAULT_VALIDATION"));
	return EnvValue.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("yes"), ESearchCase::IgnoreCase) ||
		EnvValue.Equals(TEXT("on"), ESearchCase::IgnoreCase);
}

void AppendDefaultValidationIssue_ImportBpy(
	TArray<TSharedPtr<FJsonValue>>& OutIssues,
	const FString& GraphName,
	const FString& NodeClass,
	const FString& NodeGuid,
	const FString& NodeLabel,
	const FString& PinName,
	const FString& ExpectedValue,
	const FString& ActualValue,
	const FString& IssueType)
{
	const TSharedRef<FJsonObject> IssueObj = MakeShared<FJsonObject>();
	IssueObj->SetStringField(TEXT("type"), IssueType);
	IssueObj->SetStringField(TEXT("graph"), GraphName);
	IssueObj->SetStringField(TEXT("node_class"), NodeClass);
	IssueObj->SetStringField(TEXT("node_guid"), NodeGuid);
	IssueObj->SetStringField(TEXT("node"), NodeLabel);
	IssueObj->SetStringField(TEXT("pin"), PinName);
	IssueObj->SetStringField(TEXT("expected"), ExpectedValue);
	IssueObj->SetStringField(TEXT("actual"), ActualValue);
	OutIssues.Add(MakeShared<FJsonValueObject>(IssueObj));
}

bool ValidateBlueprintDefaultsAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	TArray<TSharedPtr<FJsonValue>>& OutMissingGraphs,
	TArray<TSharedPtr<FJsonValue>>& OutMissingDefaultKeys,
	TArray<TSharedPtr<FJsonValue>>& OutDefaultMismatches,
	FString& OutError)
{
	if (!BP || !Root.IsValid())
	{
		OutError = TEXT("Invalid validation context (blueprint/root)");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return true;
	}

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		GatherReachableGraphs_ImportBpy(Graph, VisitedGraphs, ReachableGraphs);
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		GatherReachableGraphs_ImportBpy(Graph, VisitedGraphs, ReachableGraphs);
	}
	for (UEdGraph* Graph : BP->MacroGraphs)
	{
		GatherReachableGraphs_ImportBpy(Graph, VisitedGraphs, ReachableGraphs);
	}
	for (UEdGraph* Graph : BP->DelegateSignatureGraphs)
	{
		GatherReachableGraphs_ImportBpy(Graph, VisitedGraphs, ReachableGraphs);
	}
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			GatherReachableGraphs_ImportBpy(Graph, VisitedGraphs, ReachableGraphs);
		}
	}

	auto FindGraphByGuid = [&](const FGuid& Guid) -> UEdGraph*
	{
		for (UEdGraph* Graph : ReachableGraphs)
		{
			if (Graph && Graph->GraphGuid == Guid)
			{
				return Graph;
			}
		}
		return nullptr;
	};

	auto FindGraphByNameAndOuterKind = [&](const FString& Name, const FString& OuterKind) -> UEdGraph*
	{
		for (UEdGraph* Graph : ReachableGraphs)
		{
			if (!Graph || !Graph->GetName().Equals(Name, ESearchCase::CaseSensitive))
			{
				continue;
			}

			if (OuterKind.IsEmpty())
			{
				return Graph;
			}

			const FString ExistingOuterKind = DescribeGraphOuterKind_ImportBpy(Graph);
			if (ExistingOuterKind.Equals(OuterKind, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}
		return nullptr;
	};

	auto FindNodeByGuid = [](UEdGraph* Graph, const FGuid& Guid) -> UEdGraphNode*
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid)
			{
				return Node;
			}
		}
		return nullptr;
	};

	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid())
		{
			continue;
		}

		FString GraphName;
		GraphObj->TryGetStringField(TEXT("name"), GraphName);
		FString GraphOuterKind;
		GraphObj->TryGetStringField(TEXT("graph_outer"), GraphOuterKind);

		UEdGraph* LiveGraph = nullptr;
		FString GraphGuidText;
		if (GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuidText) && !GraphGuidText.IsEmpty())
		{
			FGuid GraphGuid;
			if (TryParseGuid_ImportBpy(GraphGuidText, GraphGuid))
			{
				LiveGraph = FindGraphByGuid(GraphGuid);
			}
		}
		if (!LiveGraph)
		{
			LiveGraph = FindGraphByNameAndOuterKind(GraphName, GraphOuterKind);
		}

		if (!LiveGraph)
		{
			const TSharedRef<FJsonObject> MissingGraphObj = MakeShared<FJsonObject>();
			MissingGraphObj->SetStringField(TEXT("graph"), GraphName);
			MissingGraphObj->SetStringField(TEXT("graph_guid"), GraphGuidText);
			MissingGraphObj->SetStringField(TEXT("graph_outer"), GraphOuterKind);
			OutMissingGraphs.Add(MakeShared<FJsonValueObject>(MissingGraphObj));
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
			if (!NodeObj->TryGetObjectField(TEXT("defaults"), DefaultsObj) || !DefaultsObj || !(*DefaultsObj).IsValid())
			{
				continue;
			}

			FString NodeGuidText;
			NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuidText);
			FString NodeClass;
			NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);
			FString NodeLabel;
			NodeObj->TryGetStringField(TEXT("readable_name"), NodeLabel);
			if (NodeLabel.IsEmpty())
			{
				NodeObj->TryGetStringField(TEXT("uid"), NodeLabel);
			}

			UEdGraphNode* LiveNode = nullptr;
			if (!NodeGuidText.IsEmpty())
			{
				FGuid NodeGuid;
				if (TryParseGuid_ImportBpy(NodeGuidText, NodeGuid))
				{
					LiveNode = FindNodeByGuid(LiveGraph, NodeGuid);
				}
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*DefaultsObj)->Values)
			{
				const FString& SerializedPinName = Entry.Key;
				const FString ExpectedValue = JsonValueToDefaultString_ImportBpy(Entry.Value);

				if (!LiveNode)
				{
					AppendDefaultValidationIssue_ImportBpy(
						OutMissingDefaultKeys,
						GraphName,
						NodeClass,
						NodeGuidText,
						NodeLabel,
						SerializedPinName,
						ExpectedValue,
						TEXT("__MISSING_NODE__"),
						TEXT("missing_node"));
					continue;
				}

				UEdGraphPin* Pin = ResolvePinForSerializedDefault_ImportBpy(LiveNode, NodeObj, SerializedPinName);
				if (!Pin)
				{
					AppendDefaultValidationIssue_ImportBpy(
						OutMissingDefaultKeys,
						GraphName,
						NodeClass,
						NodeGuidText,
						NodeLabel,
						SerializedPinName,
						ExpectedValue,
						TEXT("__MISSING__"),
						TEXT("missing_pin"));
					continue;
				}

				const FString ActualValue = ReadPinRawDefaultValue_ImportBpy(Pin);
				if (!AreSerializedDefaultValuesEquivalent_ImportBpy(ExpectedValue, ActualValue))
				{
					AppendDefaultValidationIssue_ImportBpy(
						OutDefaultMismatches,
						GraphName,
						NodeClass,
						NodeGuidText,
						NodeLabel,
						SerializedPinName,
						ExpectedValue,
						ActualValue,
						TEXT("default_mismatch"));
				}
			}
		}
	}

	return true;
}

bool ApplyPinIds_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson, FString& OutError)
{
	if (!Node || !NodeJson.IsValid())
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* PinIdsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("pin_ids"), PinIdsObj) || !PinIdsObj->IsValid())
	{
		return true;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*PinIdsObj)->Values)
	{
		const FString& SerializedPinName = Entry.Key;
		UEdGraphPin* InputPin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Input);
		UEdGraphPin* OutputPin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Output);
		UEdGraphPin* Pin = nullptr;
		if (InputPin && OutputPin)
		{
			const bool bBothExecPins =
				InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
				OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
			Pin = bBothExecPins ? OutputPin : InputPin;
		}
		else
		{
			Pin = InputPin ? InputPin : OutputPin;
		}
		if (!Pin)
		{
			static const FString LogPinIdSkipsEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EXPORTBPY_LOG_PIN_ID_SKIPS"));
			const bool bLogPinIdSkips =
				LogPinIdSkipsEnv.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
				LogPinIdSkipsEnv.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
				LogPinIdSkipsEnv.Equals(TEXT("yes"), ESearchCase::IgnoreCase) ||
				LogPinIdSkipsEnv.Equals(TEXT("on"), ESearchCase::IgnoreCase);
			if (bLogPinIdSkips)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("BPDirectImporter: skipping pin id for unresolved pin '%s' on node %s"),
					*SerializedPinName,
					*DescribeNode_ImportBpy(Node));
			}
			continue;
		}

		FGuid ParsedGuid;
		if (TryParseGuid_ImportBpy(Entry.Value->AsString(), ParsedGuid))
		{
			Pin->PinId = ParsedGuid;
		}
		else
		{
			OutError = FString::Printf(
				TEXT("Invalid pin guid '%s' for pin '%s' on node %s"),
				*Entry.Value->AsString(),
				*SerializedPinName,
				*DescribeNode_ImportBpy(Node));
			return false;
		}
	}

	return true;
}

bool RebindUnresolvedSelfContextCallsAndReplaySerializedPins_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	FString& OutError)
{
	if (!BP)
	{
		return true;
	}

	bool bReboundAnyCallNode = false;
	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		if (!GraphObj.IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString SerializedUid;
			if (!NodeObj->TryGetStringField(TEXT("uid"), SerializedUid) || SerializedUid.IsEmpty())
			{
				continue;
			}

			UK2Node_CallFunction* const CallNode =
				Cast<UK2Node_CallFunction>(FindImportedNodeBySerializedUid_ImportBpy(BP, SerializedUid));
			if (!CallNode)
			{
				continue;
			}

			if (RebindUnresolvedSelfContextCallNode_ImportBpy(CallNode))
			{
				bReboundAnyCallNode = true;
			}
		}
	}

	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		if (!GraphObj.IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString SerializedUid;
			if (!NodeObj->TryGetStringField(TEXT("uid"), SerializedUid) || SerializedUid.IsEmpty())
			{
				continue;
			}

			UEdGraphNode* const ExistingNode = FindImportedNodeBySerializedUid_ImportBpy(BP, SerializedUid);
			if (!ExistingNode)
			{
				continue;
			}

			if (!ApplyPinDefaults_ImportBpy(ExistingNode, NodeObj, OutError, false))
			{
				return false;
			}
			if (!ApplyPinIds_ImportBpy(ExistingNode, NodeObj, OutError))
			{
				return false;
			}
		}
	}

	return true;
}

bool RestoreCreateDelegateNodesAfterConnections_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	bool bStrict,
	FString& OutError)
{
	if (!NodesArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		const FString Uid = NodeObj->GetStringField(TEXT("uid"));
		UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		UK2Node_CreateDelegate* const CreateDelegateNode = Cast<UK2Node_CreateDelegate>(*ExistingNode);
		if (!CreateDelegateNode)
		{
			continue;
		}

		FString SelectedFunctionName = GetNodePropString_ImportBpy(NodeObj, TEXT("SelectedFunctionName"));
		if (SelectedFunctionName.IsEmpty() || SelectedFunctionName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			NodeObj->TryGetStringField(TEXT("member_name"), SelectedFunctionName);
		}
		if (SelectedFunctionName.IsEmpty() || SelectedFunctionName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const UEdGraphPin* DelegateOutPinBefore = CreateDelegateNode->GetDelegateOutPin();
		const int32 DelegateLinkCountBefore = DelegateOutPinBefore ? DelegateOutPinBefore->LinkedTo.Num() : -1;
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy] RestoreCreateDelegate(%s) node=%s requested=%s current=%s links_before=%d"),
			bStrict ? TEXT("strict") : TEXT("deferred"),
			*DescribeNode_ImportBpy(CreateDelegateNode),
			*SelectedFunctionName,
			*CreateDelegateNode->GetFunctionName().ToString(),
			DelegateLinkCountBefore);

		CreateDelegateNode->SetFunction(FName(*SelectedFunctionName));
		CreateDelegateNode->HandleAnyChangeWithoutNotifying();
		const UEdGraphPin* DelegateOutPinAfter = CreateDelegateNode->GetDelegateOutPin();
		const int32 DelegateLinkCountAfter = DelegateOutPinAfter ? DelegateOutPinAfter->LinkedTo.Num() : -1;
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy] RestoreCreateDelegate(%s) node=%s after_set=%s links_after=%d"),
			bStrict ? TEXT("strict") : TEXT("deferred"),
			*DescribeNode_ImportBpy(CreateDelegateNode),
			*CreateDelegateNode->GetFunctionName().ToString(),
			DelegateLinkCountAfter);

		if (CreateDelegateNode->GetFunctionName().IsNone())
		{
			if (bStrict)
			{
				OutError = FString::Printf(
					TEXT("Failed to restore CreateDelegate binding '%s' on node %s after connections"),
					*SelectedFunctionName,
					*DescribeNode_ImportBpy(CreateDelegateNode));
				return false;
			}

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] Deferring CreateDelegate binding '%s' on node %s (delegate signature not ready yet)"),
				*SelectedFunctionName,
				*DescribeNode_ImportBpy(CreateDelegateNode));
		}
	}

	return true;
}

bool HasSerializedPinTypeContract_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return false;
	}

	const auto HasNonEmptyObjectField = [&](const TCHAR* FieldName) -> bool
	{
		const TSharedPtr<FJsonObject>* FieldObj = nullptr;
		return NodeJson->TryGetObjectField(FieldName, FieldObj) &&
			FieldObj &&
			FieldObj->IsValid() &&
			(*FieldObj)->Values.Num() > 0;
	};

	return HasNonEmptyObjectField(TEXT("input_pin_types")) ||
		HasNonEmptyObjectField(TEXT("output_pin_types"));
}

bool HasPromotableDefaultContractHint_ImportBpy(
	UK2Node_CallFunction* CallNode,
	const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!CallNode || !NodeJson.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsObj) || !DefaultsObj || !DefaultsObj->IsValid())
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*DefaultsObj)->Values)
	{
		if (!Entry.Value.IsValid())
		{
			continue;
		}

		UEdGraphPin* InputPin =
			FindSerializedPinOnNode_ImportBpy(CallNode, NodeJson, Entry.Key, EGPD_Input);
		if (!InputPin || InputPin->LinkedTo.Num() > 0)
		{
			continue;
		}

		const FString DefaultValue = JsonValueToDefaultString_ImportBpy(Entry.Value);
		if (IsSimpleLiteralDefaultForPromotable_ImportBpy(DefaultValue))
		{
			return true;
		}
	}

	return false;
}

bool HasWildcardPins_ImportBpy(const UK2Node_CallFunction* CallNode)
{
	if (!CallNode)
	{
		return false;
	}

	for (const UEdGraphPin* Pin : CallNode->Pins)
	{
		if (!Pin || Pin->Direction == EGPD_MAX)
		{
			continue;
		}
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			return true;
		}
	}

	return false;
}

bool HydrateWildcardPinsFromTargetFunction_ImportBpy(UK2Node_CallFunction* CallNode)
{
	if (!CallNode)
	{
		return false;
	}

	const UFunction* TargetFunction = CallNode->GetTargetFunction();
	if (!TargetFunction)
	{
		return false;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (!K2Schema)
	{
		return false;
	}

	bool bAnyPinHydrated = false;
	for (UEdGraphPin* Pin : CallNode->Pins)
	{
		if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
		{
			continue;
		}

		FEdGraphPinType ResolvedPinType;
		bool bResolved = false;
		for (TFieldIterator<FProperty> It(TargetFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const bool bMatchesByName = Property->GetFName() == Pin->PinName;
			const bool bMatchesReturnAlias =
				Property->HasAnyPropertyFlags(CPF_ReturnParm) &&
				Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue;
			if (!bMatchesByName && !bMatchesReturnAlias)
			{
				continue;
			}

			if (K2Schema->ConvertPropertyToPinType(Property, ResolvedPinType))
			{
				bResolved = true;
				break;
			}
		}

		if (!bResolved)
		{
			continue;
		}

		Pin->PinType = ResolvedPinType;
		bAnyPinHydrated = true;
	}

	return bAnyPinHydrated;
}

bool RestoreCallFunctionBindingFromJson_ImportBpy(
	UK2Node_CallFunction* CallNode,
	const TSharedPtr<FJsonObject>& NodeJson,
	bool bAllowPromotableFunctionRebind,
	FString& OutError)
{
	if (!CallNode || !NodeJson.IsValid())
	{
		return true;
	}

	FString FunctionRef;
	if (!NodeJson->TryGetStringField(TEXT("function_ref"), FunctionRef) || FunctionRef.IsEmpty())
	{
		return true;
	}

	FString ClassName;
	FString FuncName;
	if (!FunctionRef.Split(TEXT("::"), &ClassName, &FuncName))
	{
		FuncName = FunctionRef;
		ClassName.Reset();
	}

	const FString OwnerClassPath = GetNodePropString_ImportBpy(NodeJson, TEXT("FunctionOwnerClass"));
	UFunction* ResolvedFunction = nullptr;
	UClass* ResolvedOwnerClass = nullptr;

	if (!OwnerClassPath.IsEmpty())
	{
		ResolvedOwnerClass = ResolveNamedObject_ImportBpy<UClass>(OwnerClassPath);
		if (ResolvedOwnerClass)
		{
			ResolvedFunction = ResolvedOwnerClass->FindFunctionByName(FName(*FuncName));
		}
	}

	if (!ResolvedFunction && !ClassName.IsEmpty())
	{
		ResolvedOwnerClass = ResolveNamedObject_ImportBpy<UClass>(ClassName);
		if (ResolvedOwnerClass)
		{
			ResolvedFunction = ResolvedOwnerClass->FindFunctionByName(FName(*FuncName));
		}
	}

	const bool bSelfContextCall = OwnerClassPath.IsEmpty() && ClassName.IsEmpty();
	if (!ResolvedFunction && bSelfContextCall)
	{
		ResolvedFunction = ResolveSelfContextFunction_ImportBpy(CallNode->GetGraph(), FuncName);
	}

	if (!ResolvedFunction && (!bSelfContextCall || IsQualifiedFunctionReference_ImportBpy(FunctionRef)))
	{
		ResolvedFunction = ResolveNamedObject_ImportBpy<UFunction>(FunctionRef);
	}

	const bool bIsPromotableOperator =
		CallNode->GetClass()->GetName() == TEXT("K2Node_PromotableOperator") ||
		CallNode->GetClass()->GetName() == TEXT("K2Node_CommutativeAssociativeBinaryOperator");
	if (bIsPromotableOperator)
	{
		LogTrackedPromotableNodeState_ImportBpy(TEXT("restore_binding_before"), CallNode, NodeJson);
	}

	if (!ResolvedFunction)
	{
		if (bIsPromotableOperator)
		{
			OutError = FString::Printf(
				TEXT("Cannot resolve promotable operator function '%s' on node %s"),
				*FunctionRef,
				*DescribeNode_ImportBpy(CallNode));
			return false;
		}
		return true;
	}

	const UFunction* ExistingTargetFunction = CallNode->GetTargetFunction();
	const bool bHasDefaultContractHint =
		bIsPromotableOperator && HasPromotableDefaultContractHint_ImportBpy(CallNode, NodeJson);
	const bool bHasWildcardPins = bIsPromotableOperator && HasWildcardPins_ImportBpy(CallNode);
	if (bIsPromotableOperator &&
		!bAllowPromotableFunctionRebind &&
		!bHasDefaultContractHint &&
		!bHasWildcardPins &&
		ExistingTargetFunction)
	{
		// .bp.py + _meta payloads do not serialize full promotable pin-type contracts.
		// In that mode, preserve the live promoted overload inferred from links/defaults
		// instead of forcing the raw function_ref overload back onto the node.
		LogTrackedPromotableNodeState_ImportBpy(TEXT("restore_binding_skip_rebind"), CallNode, NodeJson);
		return true;
	}

	const bool bShouldRebindFunction =
		ExistingTargetFunction == nullptr ||
		ExistingTargetFunction != ResolvedFunction ||
		(bIsPromotableOperator && bHasWildcardPins);
	if (bShouldRebindFunction)
	{
		CallNode->SetFromFunction(ResolvedFunction);
		if (bSelfContextCall)
		{
			CallNode->FunctionReference.SetSelfMember(FName(*FuncName));
		}
		else
		{
			CallNode->FunctionReference.SetExternalMember(
				FName(*FuncName),
				ResolvedOwnerClass ? ResolvedOwnerClass : ResolvedFunction->GetOwnerClass());
		}

		// Promotable operators can keep stale promoted pin shapes after SetFromFunction.
		// Force a reconstruct so pin categories/default channels match the restored overload.
		if (bIsPromotableOperator)
		{
			CallNode->ReconstructNode();
		}
	}
	if (bIsPromotableOperator)
	{
		LogTrackedPromotableNodeState_ImportBpy(TEXT("restore_binding_after"), CallNode, NodeJson);
	}

	return true;
}

bool RestorePromotableOperatorBindingsAfterConnections_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const TCHAR* PhaseLabel,
	FString& OutError)
{
	if (!NodesArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeUid;
		NodeObj->TryGetStringField(TEXT("uid"), NodeUid);
		if (NodeUid.IsEmpty())
		{
			continue;
		}

		UEdGraphNode* const* ExistingNode = NodeMap.Find(NodeUid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		UK2Node_CallFunction* const CallNode = Cast<UK2Node_CallFunction>(*ExistingNode);
		if (!CallNode)
		{
			continue;
		}

		const FString NodeClassName = CallNode->GetClass()->GetName();
		const bool bIsPromotableOperator =
			NodeClassName == TEXT("K2Node_PromotableOperator") ||
			NodeClassName == TEXT("K2Node_CommutativeAssociativeBinaryOperator");
		if (!bIsPromotableOperator)
		{
			continue;
		}

		LogTrackedPromotableNodeState_ImportBpy(PhaseLabel, CallNode, NodeObj);

		const bool bHasSerializedPinTypeContract = HasSerializedPinTypeContract_ImportBpy(NodeObj);
		if (!RestoreCallFunctionBindingFromJson_ImportBpy(
				CallNode,
				NodeObj,
				bHasSerializedPinTypeContract,
				OutError))
		{
			return false;
		}

		const auto ApplySerializedPinTypes = [&](const TCHAR* FieldName, EEdGraphPinDirection Direction) -> bool
		{
			const TSharedPtr<FJsonObject>* PinTypesObj = nullptr;
			if (!NodeObj->TryGetObjectField(FieldName, PinTypesObj) || !PinTypesObj || !PinTypesObj->IsValid())
			{
				return true;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*PinTypesObj)->Values)
			{
				if (!Entry.Value.IsValid())
				{
					continue;
				}

				FEdGraphPinType ParsedPinType;
				ParsePinTypeString_ImportBpy(Entry.Value->AsString(), ParsedPinType);

				UEdGraphPin* Pin = FindSerializedPinOnNode_ImportBpy(CallNode, NodeObj, Entry.Key, Direction);
				if (!Pin)
				{
					continue;
				}

				Pin->PinType = ParsedPinType;
			}

			return true;
		};

		if (!ApplySerializedPinTypes(TEXT("input_pin_types"), EGPD_Input))
		{
			return false;
		}
		if (!ApplySerializedPinTypes(TEXT("output_pin_types"), EGPD_Output))
		{
			return false;
		}

		if (HasWildcardPins_ImportBpy(CallNode))
		{
			HydrateWildcardPinsFromTargetFunction_ImportBpy(CallNode);
		}

		// SetFromFunction can rebuild pins; replay serialized defaults and pin IDs so
		// promotable operators stay bit-identical to exported call signatures.
		if (!ApplyPinDefaults_ImportBpy(CallNode, NodeObj, OutError, false))
		{
			return false;
		}
		if (!ApplyPinIds_ImportBpy(CallNode, NodeObj, OutError))
		{
			return false;
		}

		if (IsPromotableTraceEnabled_ImportBpy() && HasWildcardPins_ImportBpy(CallNode))
		{
			FString FunctionRefText;
			NodeObj->TryGetStringField(TEXT("function_ref"), FunctionRefText);

			TArray<FString> WildcardPins;
			WildcardPins.Reserve(CallNode->Pins.Num());
			for (const UEdGraphPin* Pin : CallNode->Pins)
			{
				if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
				{
					continue;
				}

				WildcardPins.Add(FString::Printf(
					TEXT("%s(dir=%s links=%d default='%s')"),
					*Pin->GetName(),
					Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
					Pin->LinkedTo.Num(),
					*Pin->DefaultValue));
			}

			const UFunction* TargetFunction = CallNode->GetTargetFunction();
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][PromotableWildcard] phase=%s node=%s function_ref='%s' target='%s' wildpins=[%s]"),
				PhaseLabel ? PhaseLabel : TEXT("<unknown>"),
				*DescribeNode_ImportBpy(CallNode),
				*FunctionRefText,
				TargetFunction ? *TargetFunction->GetPathName() : TEXT("<null>"),
				WildcardPins.Num() > 0 ? *FString::Join(WildcardPins, TEXT("; ")) : TEXT("<none>"));
		}

		LogTrackedPromotableNodeState_ImportBpy(PhaseLabel, CallNode, NodeObj);
	}

	return true;
}

bool ApplyNodeJsonToNode_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError,
	bool bDeferNestedGraphImports = false)
{
	if (!Node || !NodeJson.IsValid())
	{
		return true;
	}

	double PosX = 0.0;
	double PosY = 0.0;
	NodeJson->TryGetNumberField(TEXT("pos_x"), PosX);
	NodeJson->TryGetNumberField(TEXT("pos_y"), PosY);
	Node->NodePosX = (int32)PosX;
	Node->NodePosY = (int32)PosY;

	const auto RestoreSerializedNodeGuid = [&]() -> void
	{
		FString NodeGuidText;
		if (!NodeJson->TryGetStringField(TEXT("node_guid"), NodeGuidText))
		{
			return;
		}

		FGuid ParsedGuid;
		if (!TryParseGuid_ImportBpy(NodeGuidText, ParsedGuid))
		{
			return;
		}

		UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
		if (IsNodeGuidAlreadyUsedInBlueprint_ImportBpy(OwningBlueprint, ParsedGuid, Node))
		{
			Node->CreateNewGuid();
			return;
		}

		Node->NodeGuid = ParsedGuid;
	};

	RestoreSerializedNodeGuid();

	if (!ApplyNodeProps_ImportBpy(Node, NodeJson, OutError, bDeferNestedGraphImports))
	{
		return false;
	}
	RestoreSerializedNodeGuid();
	if (UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node))
	{
		EnsureSerializedTunnelPinContracts_ImportBpy(TunnelNode, NodeJson);
	}
	if (!ApplyPinDefaults_ImportBpy(Node, NodeJson, OutError, true))
	{
		return false;
	}
	RemapSourceGeneratedClassPinsToCurrentBlueprint_ImportBpy(Node);
	if (!ApplyPinIds_ImportBpy(Node, NodeJson, OutError))
	{
		return false;
	}
	LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, TEXT("C_after_node_json_apply"), nullptr);

	AssignAnimationGraphResultNode_ImportBpy(Node->GetGraph(), Node);

	return true;
}

static bool SyncFunctionEntryPinTypes_ImportBpy(
	UK2Node_FunctionEntry* EntryNode,
	const TArray<TPair<FString, FEdGraphPinType>>& Inputs)
{
	if (!EntryNode)
	{
		return false;
	}

	bool bChanged = false;
	for (const TPair<FString, FEdGraphPinType>& Input : Inputs)
	{
		const FName PinName(*Input.Key);
		for (TSharedPtr<FUserPinInfo>& UserPinInfo : EntryNode->UserDefinedPins)
		{
			if (UserPinInfo.IsValid() && UserPinInfo->PinName == PinName)
			{
				if (!(UserPinInfo->PinType == Input.Value))
				{
					UserPinInfo->PinType = Input.Value;
					bChanged = true;
				}
				break;
			}
		}

		if (UEdGraphPin* ExistingPin = FindPinFlexible_ImportBpy(EntryNode, Input.Key, EGPD_Output))
		{
			if (!(ExistingPin->PinType == Input.Value))
			{
				ExistingPin->PinType = Input.Value;
				bChanged = true;
			}
		}
	}

	return bChanged;
}

static bool SyncFunctionResultPinTypes_ImportBpy(
	UK2Node_FunctionResult* ResultNode,
	const TArray<TPair<FString, FEdGraphPinType>>& Outputs)
{
	if (!ResultNode)
	{
		return false;
	}

	bool bChanged = false;
	for (const TPair<FString, FEdGraphPinType>& Output : Outputs)
	{
		const FName PinName(*Output.Key);
		for (TSharedPtr<FUserPinInfo>& UserPinInfo : ResultNode->UserDefinedPins)
		{
			if (UserPinInfo.IsValid() && UserPinInfo->PinName == PinName)
			{
				if (!(UserPinInfo->PinType == Output.Value))
				{
					UserPinInfo->PinType = Output.Value;
					bChanged = true;
				}
				break;
			}
		}

		if (UEdGraphPin* ExistingPin = FindPinFlexible_ImportBpy(ResultNode, Output.Key, EGPD_Input))
		{
			if (!(ExistingPin->PinType == Output.Value))
			{
				ExistingPin->PinType = Output.Value;
				bChanged = true;
			}
		}
	}

	return bChanged;
}

void EnsureFunctionPins_ImportBpy(UK2Node_FunctionEntry* EntryNode, const TArray<TPair<FString, FEdGraphPinType>>& Inputs)
{
	if (!EntryNode)
	{
		return;
	}

	bool bAddedPins = false;
	for (const TPair<FString, FEdGraphPinType>& Input : Inputs)
	{
		if (!FindPinFlexible_ImportBpy(EntryNode, Input.Key, EGPD_Output))
		{
			EntryNode->CreateUserDefinedPin(FName(*Input.Key), Input.Value, EGPD_Output, false);
			bAddedPins = true;
		}
	}

	if (bAddedPins)
	{
		EntryNode->ReconstructNode();
	}

	SyncFunctionEntryPinTypes_ImportBpy(EntryNode, Inputs);
}

void EnsureFunctionPins_ImportBpy(UK2Node_FunctionResult* ResultNode, const TArray<TPair<FString, FEdGraphPinType>>& Outputs)
{
	if (!ResultNode)
	{
		return;
	}

	bool bAddedPins = false;
	for (const TPair<FString, FEdGraphPinType>& Output : Outputs)
	{
		if (!FindPinFlexible_ImportBpy(ResultNode, Output.Key, EGPD_Input))
		{
			ResultNode->CreateUserDefinedPin(FName(*Output.Key), Output.Value, EGPD_Input, false);
			bAddedPins = true;
		}
	}

	if (bAddedPins)
	{
		ResultNode->ReconstructNode();
	}

	SyncFunctionResultPinTypes_ImportBpy(ResultNode, Outputs);
}

void EnsureTunnelPins_ImportBpy(
	UK2Node_Tunnel* TunnelNode,
	const TArray<TPair<FString, FEdGraphPinType>>& Pins,
	EEdGraphPinDirection Direction)
{
	if (!TunnelNode)
	{
		return;
	}

	bool bAddedPins = false;
	for (const TPair<FString, FEdGraphPinType>& PinDef : Pins)
	{
		if (!FindPinFlexible_ImportBpy(TunnelNode, PinDef.Key, Direction))
		{
			TunnelNode->CreateUserDefinedPin(FName(*PinDef.Key), PinDef.Value, Direction, false);
			bAddedPins = true;
		}
	}

	if (bAddedPins)
	{
		TunnelNode->ReconstructNode();
		if (UK2Node_Tunnel* MirroredTunnel = (Direction == EGPD_Output) ? TunnelNode->OutputSourceNode : TunnelNode->InputSinkNode)
		{
			MirroredTunnel->ReconstructNode();
		}
	}
}

void CollectSerializedNodePinTypes_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	const TCHAR* FieldName,
	TArray<TPair<FString, FEdGraphPinType>>& OutPins)
{
	OutPins.Reset();
	if (!NodeJson.IsValid() || !FieldName)
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PinTypesObj = nullptr;
	if (!NodeJson->TryGetObjectField(FieldName, PinTypesObj) || !PinTypesObj || !(*PinTypesObj).IsValid())
	{
		return;
	}

	TArray<FString> PinNames;
	(*PinTypesObj)->Values.GetKeys(PinNames);
	PinNames.Sort();
	for (const FString& PinName : PinNames)
	{
		const TSharedPtr<FJsonValue>* PinTypeValue = (*PinTypesObj)->Values.Find(PinName);
		if (!PinTypeValue || !PinTypeValue->IsValid())
		{
			continue;
		}

		FEdGraphPinType ParsedPinType;
		ParsePinTypeString_ImportBpy((*PinTypeValue)->AsString(), ParsedPinType);
		OutPins.Add(TPair<FString, FEdGraphPinType>(PinName, ParsedPinType));
	}
}

void EnsureSerializedTunnelPinContracts_ImportBpy(
	UK2Node_Tunnel* TunnelNode,
	const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!TunnelNode || !NodeJson.IsValid())
	{
		return;
	}

	TArray<TPair<FString, FEdGraphPinType>> InputPins;
	TArray<TPair<FString, FEdGraphPinType>> OutputPins;
	CollectSerializedNodePinTypes_ImportBpy(NodeJson, TEXT("input_pin_types"), InputPins);
	CollectSerializedNodePinTypes_ImportBpy(NodeJson, TEXT("output_pin_types"), OutputPins);

	if (InputPins.Num() > 0)
	{
		EnsureTunnelPins_ImportBpy(TunnelNode, InputPins, EGPD_Input);
	}
	if (OutputPins.Num() > 0)
	{
		EnsureTunnelPins_ImportBpy(TunnelNode, OutputPins, EGPD_Output);
	}
}

static void MergeGraphPinContracts_ImportBpy(
	const TArray<TPair<FString, FEdGraphPinType>>& SourcePins,
	TArray<TPair<FString, FEdGraphPinType>>& InOutTargetPins)
{
	for (const TPair<FString, FEdGraphPinType>& SourcePin : SourcePins)
	{
		bool bAlreadyPresent = false;
		for (const TPair<FString, FEdGraphPinType>& ExistingPin : InOutTargetPins)
		{
			if (ExistingPin.Key == SourcePin.Key)
			{
				bAlreadyPresent = true;
				break;
			}
		}

		if (!bAlreadyPresent)
		{
			InOutTargetPins.Add(SourcePin);
		}
	}
}

static void RecoverGraphPinContractsFromTunnelNodes_ImportBpy(
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	TArray<TPair<FString, FEdGraphPinType>>& InOutGraphInputs,
	TArray<TPair<FString, FEdGraphPinType>>& InOutGraphOutputs)
{
	if (!NodesArr)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}
		if (GetNodePropString_ImportBpy(NodeObj, TEXT("node_class")) != TEXT("K2Node_Tunnel"))
		{
			continue;
		}

		const ETunnelKind_ImportBpy TunnelKind = InferTunnelKind_ImportBpy(NodeObj);
		TArray<TPair<FString, FEdGraphPinType>> InputPins;
		TArray<TPair<FString, FEdGraphPinType>> OutputPins;
		CollectSerializedNodePinTypes_ImportBpy(NodeObj, TEXT("input_pin_types"), InputPins);
		CollectSerializedNodePinTypes_ImportBpy(NodeObj, TEXT("output_pin_types"), OutputPins);

		if (TunnelKind == ETunnelKind_ImportBpy::Entry && OutputPins.Num() > 0)
		{
			MergeGraphPinContracts_ImportBpy(OutputPins, InOutGraphInputs);
		}
		else if (TunnelKind == ETunnelKind_ImportBpy::Exit && InputPins.Num() > 0)
		{
			MergeGraphPinContracts_ImportBpy(InputPins, InOutGraphOutputs);
		}
	}
}

void ClearGraphNodes_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	bool bPreserveTunnelNodes,
	bool bPreserveFunctionEntryNodes)
{
	if (!BP || !Graph)
	{
		return;
	}

	ResetAnimationGraphResultNode_ImportBpy(Graph);

	TArray<UEdGraphNode*> ExistingNodes = Graph->Nodes;
	for (UEdGraphNode* Node : ExistingNodes)
	{
		if (!Node)
		{
			continue;
		}

		// Function graphs must keep their generated entry/result nodes so the
		// graph retains a valid function name/signature binding during compile.
		if (bPreserveFunctionEntryNodes &&
			(Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>()))
		{
			continue;
		}

		// Macro graphs likewise need their tunnel endpoints preserved.
		if (bPreserveTunnelNodes && Node->IsA<UK2Node_Tunnel>())
		{
			continue;
		}

		FBlueprintEditorUtils::RemoveNode(BP, Node, true);
	}
}

bool RepairStateMachineSubgraphOwnershipBeforeClear_ImportBpy(UEdGraph* Graph, FString& OutError)
{
	if (!Graph || !Graph->IsA<UAnimationStateMachineGraph>())
	{
		return true;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			if (!EnsureTransitionBoundGraphOwnership_ImportBpy(TransitionNode, OutError))
			{
				return false;
			}
			continue;
		}

		if (UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
		{
			if (!EnsureConduitBoundGraphOwnership_ImportBpy(ConduitNode, OutError))
			{
				return false;
			}
		}
	}

	return true;
}

bool PopulateNestedGraphFromJsonText_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& GraphJsonText,
	FString& OutError)
{
	if (GraphJsonText.IsEmpty())
	{
		return true;
	}

	if (!BP || !Graph)
	{
		OutError = TEXT("Invalid nested graph import context");
		return false;
	}

	UBlueprint* const GraphOwnerBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	UEdGraphNode* const GraphOuterNode = Cast<UEdGraphNode>(Graph->GetOuter());
	UBlueprint* const OuterNodeOwnerBlueprint =
		GraphOuterNode ? FBlueprintEditorUtils::FindBlueprintForNode(GraphOuterNode) : nullptr;
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][NestedGraph] graph=%s class=%s outer=%s bp_arg=%s graph_owner=%s outer_node=%s outer_node_owner=%s"),
		*Graph->GetPathName(),
		*GetNameSafe(Graph->GetClass()),
		*GetPathNameSafe(Graph->GetOuter()),
		*GetPathNameSafe(BP),
		*GetPathNameSafe(GraphOwnerBlueprint),
		*GetPathNameSafe(GraphOuterNode),
		*GetPathNameSafe(OuterNodeOwnerBlueprint));

	TSharedPtr<FJsonObject> GraphJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphJsonText);
	if (!FJsonSerializer::Deserialize(Reader, GraphJson) || !GraphJson.IsValid())
	{
		OutError = TEXT("Cannot parse nested graph json");
		return false;
	}

	return UBPDirectImporter::PopulateGraph(BP, Graph, GraphJson, false, OutError);
}

void RemoveUnlinkedOrphanPins_ImportBpy(UEdGraphNode* Node)
{
	if (!Node)
	{
		return;
	}

	for (int32 PinIndex = Node->Pins.Num() - 1; PinIndex >= 0; --PinIndex)
	{
		UEdGraphPin* Pin = Node->Pins[PinIndex];
		if (!Pin || !Pin->bOrphanedPin || Pin->LinkedTo.Num() > 0)
		{
			continue;
		}

		Node->RemovePin(Pin);
	}
}

bool EnsureTransitionCustomGraphExists_ImportBpy(UAnimStateTransitionNode* TransitionNode, FString& OutError)
{
	if (!TransitionNode || TransitionNode->GetCustomTransitionGraph())
	{
		return true;
	}

	UEdGraph* ParentGraph = TransitionNode->GetGraph();
	if (!ParentGraph)
	{
		OutError = FString::Printf(
			TEXT("Transition node %s does not have a parent graph"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	UEdGraph* NewCustomGraph = FBlueprintEditorUtils::CreateNewGraph(
		TransitionNode,
		NAME_None,
		UAnimationCustomTransitionGraph::StaticClass(),
		UAnimationCustomTransitionSchema::StaticClass());
	if (!NewCustomGraph)
	{
		OutError = FString::Printf(
			TEXT("Failed to create custom transition graph for node %s"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	FEdGraphUtilities::RenameGraphToNameOrCloseToName(NewCustomGraph, TEXT("CustomTransition"));

	const UEdGraphSchema* Schema = NewCustomGraph->GetSchema();
	if (!Schema)
	{
		OutError = FString::Printf(
			TEXT("Custom transition graph for node %s is missing a graph schema"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	Schema->CreateDefaultNodesForGraph(*NewCustomGraph);

	if (ParentGraph->SubGraphs.Find(NewCustomGraph) == INDEX_NONE)
	{
		ParentGraph->Modify();
		ParentGraph->SubGraphs.Add(NewCustomGraph);
	}

	FObjectPropertyBase* CustomTransitionGraphProperty =
		FindFProperty<FObjectPropertyBase>(UAnimStateTransitionNode::StaticClass(), TEXT("CustomTransitionGraph"));
	if (!CustomTransitionGraphProperty)
	{
		OutError = TEXT("Cannot resolve UAnimStateTransitionNode.CustomTransitionGraph property");
		return false;
	}

	CustomTransitionGraphProperty->SetObjectPropertyValue_InContainer(TransitionNode, NewCustomGraph);
	return TransitionNode->GetCustomTransitionGraph() != nullptr;
}

bool EnsureTransitionBoundGraphOwnership_ImportBpy(UAnimStateTransitionNode* TransitionNode, FString& OutError)
{
	if (!TransitionNode)
	{
		return false;
	}

	UEdGraph* ExistingBoundGraph = TransitionNode->GetBoundGraph();
	if (ExistingBoundGraph && ExistingBoundGraph->GetOuter() == TransitionNode)
	{
		return true;
	}

	UEdGraph* ParentGraph = TransitionNode->GetGraph();
	if (!ParentGraph)
	{
		OutError = FString::Printf(
			TEXT("Transition node %s does not have a parent graph"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	UEdGraph* NewBoundGraph = FBlueprintEditorUtils::CreateNewGraph(
		TransitionNode,
		NAME_None,
		UAnimationTransitionGraph::StaticClass(),
		UAnimationTransitionSchema::StaticClass());
	if (!NewBoundGraph)
	{
		OutError = FString::Printf(
			TEXT("Failed to create transition bound graph for node %s"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	FEdGraphUtilities::RenameGraphToNameOrCloseToName(NewBoundGraph, TEXT("Transition"));

	const UEdGraphSchema* Schema = NewBoundGraph->GetSchema();
	if (!Schema)
	{
		OutError = FString::Printf(
			TEXT("Transition bound graph for node %s is missing a graph schema"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	Schema->CreateDefaultNodesForGraph(*NewBoundGraph);

	if (ParentGraph->SubGraphs.Find(NewBoundGraph) == INDEX_NONE)
	{
		ParentGraph->Modify();
		ParentGraph->SubGraphs.Add(NewBoundGraph);
	}

	FObjectPropertyBase* BoundGraphProperty =
		FindFProperty<FObjectPropertyBase>(UAnimStateNodeBase::StaticClass(), TEXT("BoundGraph"));
	if (!BoundGraphProperty)
	{
		OutError = TEXT("Cannot resolve UAnimStateNodeBase.BoundGraph property");
		return false;
	}

	BoundGraphProperty->SetObjectPropertyValue_InContainer(TransitionNode, NewBoundGraph);
	UEdGraph* FinalBoundGraph = TransitionNode->GetBoundGraph();
	if (!FinalBoundGraph || FinalBoundGraph->GetOuter() != TransitionNode)
	{
		OutError = FString::Printf(
			TEXT("Transition bound graph ownership mismatch on node %s after repair"),
			*DescribeNode_ImportBpy(TransitionNode));
		return false;
	}

	return true;
}

bool EnsureConduitBoundGraphOwnership_ImportBpy(UAnimStateConduitNode* ConduitNode, FString& OutError)
{
	if (!ConduitNode)
	{
		return false;
	}

	UEdGraph* ExistingBoundGraph = ConduitNode->BoundGraph;
	if (ExistingBoundGraph && ExistingBoundGraph->GetOuter() == ConduitNode)
	{
		return true;
	}

	UEdGraph* ParentGraph = ConduitNode->GetGraph();
	if (!ParentGraph)
	{
		OutError = FString::Printf(
			TEXT("Conduit node %s does not have a parent graph"),
			*DescribeNode_ImportBpy(ConduitNode));
		return false;
	}

	UEdGraph* NewBoundGraph = FBlueprintEditorUtils::CreateNewGraph(
		ConduitNode,
		NAME_None,
		UAnimationTransitionGraph::StaticClass(),
		UAnimationTransitionSchema::StaticClass());
	if (!NewBoundGraph)
	{
		OutError = FString::Printf(
			TEXT("Failed to create conduit bound graph for node %s"),
			*DescribeNode_ImportBpy(ConduitNode));
		return false;
	}

	FEdGraphUtilities::RenameGraphToNameOrCloseToName(NewBoundGraph, TEXT("Conduit"));

	const UEdGraphSchema* Schema = NewBoundGraph->GetSchema();
	if (!Schema)
	{
		OutError = FString::Printf(
			TEXT("Conduit bound graph for node %s is missing a graph schema"),
			*DescribeNode_ImportBpy(ConduitNode));
		return false;
	}

	Schema->CreateDefaultNodesForGraph(*NewBoundGraph);

	if (ParentGraph->SubGraphs.Find(NewBoundGraph) == INDEX_NONE)
	{
		ParentGraph->Modify();
		ParentGraph->SubGraphs.Add(NewBoundGraph);
	}

	FObjectPropertyBase* BoundGraphProperty =
		FindFProperty<FObjectPropertyBase>(UAnimStateNodeBase::StaticClass(), TEXT("BoundGraph"));
	if (!BoundGraphProperty)
	{
		OutError = TEXT("Cannot resolve UAnimStateNodeBase.BoundGraph property");
		return false;
	}

	BoundGraphProperty->SetObjectPropertyValue_InContainer(ConduitNode, NewBoundGraph);
	UEdGraph* FinalBoundGraph = ConduitNode->BoundGraph;
	if (!FinalBoundGraph || FinalBoundGraph->GetOuter() != ConduitNode)
	{
		OutError = FString::Printf(
			TEXT("Conduit bound graph ownership mismatch on node %s after repair"),
			*DescribeNode_ImportBpy(ConduitNode));
		return false;
	}

	return true;
}

bool IsNodeGuidAlreadyUsedInBlueprint_ImportBpy(UBlueprint* BP, const FGuid& Guid, const UEdGraphNode* IgnoreNode)
{
	if (!BP || !Guid.IsValid())
	{
		return false;
	}

	// Keep GUID uniqueness blueprint-wide. AnimBlueprint compile enforces this
	// across child graphs and will regenerate duplicates otherwise.

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			if (!ExistingNode || ExistingNode == IgnoreNode)
			{
				continue;
			}

			if (ExistingNode->NodeGuid == Guid)
			{
				return true;
			}
		}
	}

	return false;
}

void ResetAnimationGraphResultNode_ImportBpy(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}

	FObjectPropertyBase* ResultNodeProperty =
		FindFProperty<FObjectPropertyBase>(Graph->GetClass(), TEXT("MyResultNode"));
	if (ResultNodeProperty)
	{
		ResultNodeProperty->SetObjectPropertyValue_InContainer(Graph, nullptr);
	}

	// Blend stack graphs store their output root in ResultNode instead of
	// MyResultNode. Clear it when rebuilding the graph so we don't keep a stale
	// pointer to the previous default Output Pose node.
	FObjectPropertyBase* BlendStackResultNodeProperty =
		FindFProperty<FObjectPropertyBase>(Graph->GetClass(), TEXT("ResultNode"));
	if (BlendStackResultNodeProperty &&
		BlendStackResultNodeProperty->PropertyClass &&
		BlendStackResultNodeProperty->PropertyClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		BlendStackResultNodeProperty->SetObjectPropertyValue_InContainer(Graph, nullptr);
	}
}

void AssignAnimationGraphResultNode_ImportBpy(UEdGraph* Graph, UEdGraphNode* Node)
{
	if (!Graph || !Node)
	{
		return;
	}

	const bool bIsStateGraphResult =
		Graph->IsA<UAnimationStateGraph>() && Node->IsA<UAnimGraphNode_StateResult>() &&
		!Node->IsA<UAnimGraphNode_CustomTransitionResult>();
	const bool bIsTransitionGraphResult =
		Graph->IsA<UAnimationTransitionGraph>() && Node->IsA<UAnimGraphNode_TransitionResult>();
	const bool bIsCustomTransitionGraphResult =
		Graph->IsA<UAnimationCustomTransitionGraph>() && Node->IsA<UAnimGraphNode_CustomTransitionResult>();
	const bool bIsBlendStackGraphResult =
		Graph->GetClass() &&
		Graph->GetClass()->GetName().Contains(TEXT("AnimationBlendStackGraph"), ESearchCase::IgnoreCase) &&
		Node->GetClass() &&
		Node->GetClass()->GetName().Contains(TEXT("AnimGraphNode_BlendStackResult"), ESearchCase::IgnoreCase);
	if (!bIsStateGraphResult && !bIsTransitionGraphResult && !bIsCustomTransitionGraphResult && !bIsBlendStackGraphResult)
	{
		return;
	}

	const TCHAR* ResultPropertyName = bIsBlendStackGraphResult ? TEXT("ResultNode") : TEXT("MyResultNode");
	FObjectPropertyBase* ResultNodeProperty =
		FindFProperty<FObjectPropertyBase>(Graph->GetClass(), ResultPropertyName);
	if (ResultNodeProperty &&
		ResultNodeProperty->PropertyClass &&
		ResultNodeProperty->PropertyClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		ResultNodeProperty->SetObjectPropertyValue_InContainer(Graph, Node);
	}
}

FString NormalizeAliasedStateUidList_ImportBpy(const FString& AliasedStateUids)
{
	TArray<FString> AliasedUidList;
	AliasedStateUids.ParseIntoArray(AliasedUidList, TEXT("|"), true);
	for (FString& AliasedUid : AliasedUidList)
	{
		AliasedUid.TrimStartAndEndInline();
	}
	AliasedUidList.RemoveAll([](const FString& Value)
	{
		return Value.IsEmpty();
	});
	AliasedUidList.Sort();
	return FString::Join(AliasedUidList, TEXT("|"));
}

bool IsSerializedStateAliasNodeClass_ImportBpy(const FString& NodeClassName)
{
	return NodeClassName.Equals(TEXT("AnimStateAliasNode"), ESearchCase::CaseSensitive) ||
		NodeClassName.Equals(TEXT("K2Node_AnimStateAliasNode"), ESearchCase::CaseSensitive);
}

UAnimStateNodeBase* ResolveAliasedStateNodeFromImportedMap_ImportBpy(
	UEdGraph* Graph,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const FString& AliasedStateIdentity)
{
	if (AliasedStateIdentity.IsEmpty())
	{
		return nullptr;
	}

	if (const UEdGraphNode* const* ExactNode = NodeMap.Find(AliasedStateIdentity))
	{
		if (const UAnimStateNodeBase* ExactState = Cast<UAnimStateNodeBase>(*ExactNode))
		{
			return const_cast<UAnimStateNodeBase*>(ExactState);
		}
	}

	FGuid ParsedGuid;
	const bool bHasParsedGuid = TryParseGuid_ImportBpy(AliasedStateIdentity, ParsedGuid);
	auto MatchesIdentity = [&AliasedStateIdentity, bHasParsedGuid, &ParsedGuid](const UEdGraphNode* Node) -> bool
	{
		if (!Node)
		{
			return false;
		}
		if (bHasParsedGuid && Node->NodeGuid == ParsedGuid)
		{
			return true;
		}

		const FString NodeGuidDigits = Node->NodeGuid.ToString(EGuidFormats::Digits);
		const FString NodeGuidHyphenated = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		return NodeGuidDigits.Equals(AliasedStateIdentity, ESearchCase::IgnoreCase) ||
			NodeGuidHyphenated.Equals(AliasedStateIdentity, ESearchCase::IgnoreCase);
	};

	for (const TPair<FString, UEdGraphNode*>& Pair : NodeMap)
	{
		if (UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Pair.Value))
		{
			if (MatchesIdentity(StateNode))
			{
				return StateNode;
			}
		}
	}

	if (!Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node))
		{
			if (MatchesIdentity(StateNode))
			{
				return StateNode;
			}
		}
	}

	return nullptr;
}

bool RestoreStateMachineAliasNodesAfterCreation_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	FString& OutError)
{
	if (!NodesArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		FString SerializedNodeClass;
		if (!NodeObj.IsValid() ||
			!NodeObj->TryGetStringField(TEXT("node_class"), SerializedNodeClass) ||
			!IsSerializedStateAliasNodeClass_ImportBpy(SerializedNodeClass))
		{
			continue;
		}

		const FString Uid = NodeObj->GetStringField(TEXT("uid"));
		UAnimStateAliasNode* AliasNode = nullptr;
		if (UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid))
		{
			AliasNode = Cast<UAnimStateAliasNode>(*ExistingNode);
		}

		if (!AliasNode)
		{
			UEdGraphNode* const LiveNode = FindImportedAnimNodeFromSerializedJson_ImportBpy(BP, NodeObj);
			if (LiveNode && (!Graph || LiveNode->GetGraph() == Graph))
			{
				AliasNode = Cast<UAnimStateAliasNode>(LiveNode);
			}
		}

		if (!AliasNode)
		{
			FString AliasNodeGuid;
			NodeObj->TryGetStringField(TEXT("node_guid"), AliasNodeGuid);
			if (AliasNodeGuid.IsEmpty())
			{
				AliasNodeGuid = Uid;
			}
			OutError = FString::Printf(
				TEXT("State alias replay could not resolve alias node uid='%s' guid='%s' on graph '%s'"),
				*Uid,
				*AliasNodeGuid,
				Graph ? *Graph->GetPathName() : TEXT("<null>"));
			return false;
		}

		const FString AliasedStateUids = GetNodePropString_ImportBpy(NodeObj, TEXT("AliasedStateUids"));
		TSet<TWeakObjectPtr<UAnimStateNodeBase>>& AliasedStates = AliasNode->GetAliasedStates();
		AliasedStates.Reset();

		if (AliasedStateUids.IsEmpty())
		{
			continue;
		}

		TArray<FString> AliasedUidList;
		AliasedStateUids.ParseIntoArray(AliasedUidList, TEXT("|"), true);
		for (const FString& AliasedUid : AliasedUidList)
		{
			UAnimStateNodeBase* const TargetState =
				ResolveAliasedStateNodeFromImportedMap_ImportBpy(
					AliasNode->GetGraph() ? AliasNode->GetGraph() : Graph,
					NodeMap,
					AliasedUid);
			if (!TargetState)
			{
				OutError = FString::Printf(
					TEXT("Alias node '%s' references missing aliased state '%s'"),
					*DescribeNode_ImportBpy(AliasNode),
					*AliasedUid);
				return false;
			}

			AliasedStates.Add(TargetState);
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[ExportBpy][ImportDiag][StateAlias] graph=%s node=%s restored_alias_refs=%d serialized_aliases=%s"),
			AliasNode->GetGraph() ? *AliasNode->GetGraph()->GetPathName() : TEXT("<null>"),
			*DescribeNode_ImportBpy(AliasNode),
			AliasedStates.Num(),
			*NormalizeAliasedStateUidList_ImportBpy(AliasedStateUids));
	}

	return true;
}

bool ReplayStateMachineAliasNodesFromGraphJsonText_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& GraphJsonText,
	const TCHAR* StageTag,
	FString& OutError)
{
	if (!BP || !Graph || GraphJsonText.IsEmpty())
	{
		return true;
	}

	TSharedPtr<FJsonObject> GraphJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphJsonText);
	if (!FJsonSerializer::Deserialize(Reader, GraphJson) || !GraphJson.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Cannot parse state machine graph json for alias replay on graph %s"),
			*GetPathNameSafe(Graph));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphJson->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return true;
	}

	TMap<FString, UEdGraphNode*> NodeMap;
	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString SerializedUid;
		NodeObj->TryGetStringField(TEXT("uid"), SerializedUid);
		if (SerializedUid.IsEmpty())
		{
			continue;
		}

		if (UEdGraphNode* ImportedNode = FindImportedAnimNodeFromSerializedJson_ImportBpy(BP, NodeObj))
		{
			if (ImportedNode->GetGraph() == Graph)
			{
				NodeMap.Add(SerializedUid, ImportedNode);
			}
		}
	}

	if (!RestoreStateMachineAliasNodesAfterCreation_ImportBpy(BP, Graph, NodesArr, NodeMap, OutError))
	{
		return false;
	}

	int32 AliasNodeCount = 0;
	int32 AliasedRefCount = 0;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UAnimStateAliasNode* AliasNode = Cast<UAnimStateAliasNode>(Node))
		{
			++AliasNodeCount;
			AliasedRefCount += AliasNode->GetAliasedStates().Num();
		}
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[ExportBpy][ImportDiag][StateAlias][%s] graph=%s alias_nodes=%d aliased_refs=%d"),
		StageTag ? StageTag : TEXT("Unknown"),
		*GetPathNameSafe(Graph),
		AliasNodeCount,
		AliasedRefCount);

	return true;
}

bool ValidateStateMachineGraphReplayGate_ImportBpy(
	UAnimGraphNode_StateMachineBase* StateMachineNode,
	const TSharedPtr<FJsonObject>& GraphJson,
	const TCHAR* StageTag,
	FString& OutError)
{
	if (!StateMachineNode || !GraphJson.IsValid())
	{
		return true;
	}

	UEdGraph* const LiveGraph = StateMachineNode->EditorStateMachineGraph;
	const TArray<TSharedPtr<FJsonValue>>* ExpectedNodes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ExpectedConnections = nullptr;
	const int32 ExpectedNodeCount =
		GraphJson->TryGetArrayField(TEXT("nodes"), ExpectedNodes) && ExpectedNodes
			? ExpectedNodes->Num()
			: 0;
	const int32 ExpectedConnectionCount =
		GraphJson->TryGetArrayField(TEXT("connections"), ExpectedConnections) && ExpectedConnections
			? ExpectedConnections->Num()
			: 0;

	int32 ExpectedStateCount = 0;
	int32 ExpectedTransitionCount = 0;
	int32 ExpectedEntryCount = 0;
	if (ExpectedNodes)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *ExpectedNodes)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString NodeClass;
			NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);
			if (NodeClass == TEXT("K2Node_AnimStateEntryNode"))
			{
				++ExpectedEntryCount;
			}
			else if (NodeClass.Contains(TEXT("AnimStateTransitionNode")))
			{
				++ExpectedTransitionCount;
			}
			else if (NodeClass.Contains(TEXT("AnimStateNode")) || NodeClass.Contains(TEXT("AnimStateConduitNode")))
			{
				++ExpectedStateCount;
			}
		}
	}

	int32 LiveNodeCount = LiveGraph ? LiveGraph->Nodes.Num() : 0;
	int32 LiveConnectionCount = 0;
	int32 LiveStateCount = 0;
	int32 LiveTransitionCount = 0;
	int32 LiveEntryCount = 0;
	if (LiveGraph)
	{
		for (UEdGraphNode* Node : LiveGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			if (Node->IsA<UAnimStateEntryNode>())
			{
				++LiveEntryCount;
			}
			else if (Node->IsA<UAnimStateTransitionNode>())
			{
				++LiveTransitionCount;
			}
			else if (Node->IsA<UAnimStateNodeBase>())
			{
				++LiveStateCount;
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output)
				{
					LiveConnectionCount += Pin->LinkedTo.Num();
				}
			}
		}
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[ExportBpy][ImportDiag][StateMachineGate][%s] node=%s expected_nodes=%d live_nodes=%d expected_connections=%d live_connections=%d expected_entry=%d live_entry=%d expected_states=%d live_states=%d expected_transitions=%d live_transitions=%d graph=%s"),
		StageTag ? StageTag : TEXT("unknown"),
		*DescribeNode_ImportBpy(StateMachineNode),
		ExpectedNodeCount,
		LiveNodeCount,
		ExpectedConnectionCount,
		LiveConnectionCount,
		ExpectedEntryCount,
		LiveEntryCount,
		ExpectedStateCount,
		LiveStateCount,
		ExpectedTransitionCount,
		LiveTransitionCount,
		*GetPathNameSafe(LiveGraph));

	if (ExpectedNodeCount > 2 && (!LiveGraph || LiveNodeCount <= 2))
	{
		OutError = FString::Printf(
			TEXT("State machine graph regression on %s (%s): expected_nodes=%d live_nodes=%d. Likely late reconstruct wiped EditorStateMachineGraph."),
			*DescribeNode_ImportBpy(StateMachineNode),
			StageTag ? StageTag : TEXT("unknown"),
			ExpectedNodeCount,
			LiveNodeCount);
		return false;
	}

	if (ExpectedStateCount > 0 && LiveStateCount == 0)
	{
		OutError = FString::Printf(
			TEXT("State machine graph regression on %s (%s): expected_states=%d live_states=0."),
			*DescribeNode_ImportBpy(StateMachineNode),
			StageTag ? StageTag : TEXT("unknown"),
			ExpectedStateCount);
		return false;
	}

	if (ExpectedConnectionCount > 0 && LiveConnectionCount == 0)
	{
		OutError = FString::Printf(
			TEXT("State machine graph regression on %s (%s): expected_connections=%d live_connections=0."),
			*DescribeNode_ImportBpy(StateMachineNode),
			StageTag ? StageTag : TEXT("unknown"),
			ExpectedConnectionCount);
		return false;
	}

	return true;
}

bool ReplayStateMachineGraphFromJsonTextFinal_ImportBpy(
	UBlueprint* BP,
	UAnimGraphNode_StateMachineBase* StateMachineNode,
	const FString& GraphJsonText,
	const TCHAR* StageTag,
	FString& OutError)
{
	if (!BP || !StateMachineNode || GraphJsonText.IsEmpty())
	{
		return true;
	}

	if (!StateMachineNode->EditorStateMachineGraph)
	{
		OutError = FString::Printf(
			TEXT("State machine node %s is missing EditorStateMachineGraph during %s"),
			*DescribeNode_ImportBpy(StateMachineNode),
			StageTag ? StageTag : TEXT("state_machine_replay"));
		return false;
	}

	TSharedPtr<FJsonObject> GraphJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphJsonText);
	if (!FJsonSerializer::Deserialize(Reader, GraphJson) || !GraphJson.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Cannot parse StateMachineGraphJson for node %s during %s"),
			*DescribeNode_ImportBpy(StateMachineNode),
			StageTag ? StageTag : TEXT("state_machine_replay"));
		return false;
	}

	if (!UBPDirectImporter::PopulateGraph(BP, StateMachineNode->EditorStateMachineGraph, GraphJson, false, OutError))
	{
		return false;
	}

	if (!ReplayStateMachineAliasNodesFromGraphJsonText_ImportBpy(
			BP,
			StateMachineNode->EditorStateMachineGraph,
			GraphJsonText,
			StageTag,
			OutError))
	{
		return false;
	}

	return ValidateStateMachineGraphReplayGate_ImportBpy(StateMachineNode, GraphJson, StageTag, OutError);
}
}

// ─── Public entry points ──────────────────────────────────────────────────────

void ImportInterfaces_ImportBpy(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& InterfacesArr)
{
	if (!BP) return;

	auto IsInterfaceAlreadyImplemented = [BP](UClass* InterfaceClass) -> bool
	{
		if (!BP || !InterfaceClass)
		{
			return false;
		}

		const UClass* TargetInterface = InterfaceClass->GetAuthoritativeClass();
		for (const FBPInterfaceDescription& Existing : BP->ImplementedInterfaces)
		{
			const UClass* ExistingInterface = Existing.Interface
				? Existing.Interface->GetAuthoritativeClass()
				: nullptr;
			if (ExistingInterface == TargetInterface)
			{
				return true;
			}
		}

		return false;
	};

	for (const TSharedPtr<FJsonValue>& Val : InterfacesArr)
	{
		if (!Val.IsValid()) continue;
		FString InterfacePath = Val->AsString();
		if (InterfacePath.IsEmpty()) continue;

		UClass* InterfaceClass = ResolveNamedObject_ImportBpy<UClass>(InterfacePath);
		if (!InterfaceClass)
		{
			// Handle exported Blueprint interface class paths like:
			// /Game/X/BPI_Foo.BPI_Foo_C
			// by resolving through the asset first.
			FString InterfaceAssetPath = InterfacePath;
			const int32 DotIndex = InterfaceAssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (DotIndex != INDEX_NONE)
			{
				InterfaceAssetPath = InterfaceAssetPath.Left(DotIndex);
			}
			InterfaceAssetPath.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);

			if (UObject* InterfaceAsset = UEditorAssetLibrary::LoadAsset(InterfaceAssetPath))
			{
				if (const UBlueprint* InterfaceBP = Cast<UBlueprint>(InterfaceAsset))
				{
					InterfaceClass = InterfaceBP->GeneratedClass
						? static_cast<UClass*>(InterfaceBP->GeneratedClass)
						: static_cast<UClass*>(InterfaceBP->SkeletonGeneratedClass);
				}
			}
		}

		UClass* CanonicalInterfaceClass = InterfaceClass ? InterfaceClass->GetAuthoritativeClass() : nullptr;
		if (!CanonicalInterfaceClass)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] Skip unresolved interface during import: %s"),
				*InterfacePath);
			continue;
		}

		if (!CanonicalInterfaceClass->HasAnyClassFlags(CLASS_Interface))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] Skip non-interface class during interface import: %s -> %s"),
				*InterfacePath,
				*GetPathNameSafe(CanonicalInterfaceClass));
			continue;
		}

		if (IsInterfaceAlreadyImplemented(CanonicalInterfaceClass))
		{
			continue;
		}

		const FTopLevelAssetPath InterfaceClassPath = CanonicalInterfaceClass->GetClassPathName();
		FBlueprintEditorUtils::ImplementNewInterface(BP, InterfaceClassPath);

		if (!IsInterfaceAlreadyImplemented(CanonicalInterfaceClass))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] Failed to implement interface during import: %s"),
				*GetPathNameSafe(CanonicalInterfaceClass));
		}
	}
}

void ImportClassDefaults_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonValue>>& DefaultsArr,
	const FString& SourceBlueprintPath)
{
	if (!BP)
	{
		return;
	}

	UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject(false) : nullptr;
	TSet<FString> ImportedPropertyNames;
	bool bModifiedBlueprintAsset = false;

	auto ApplyDefaultToObject = [&ImportedPropertyNames](UObject* TargetObject, const FString& PropName, const TSharedPtr<FJsonValue>& PropValue) -> bool
	{
		if (!TargetObject || PropName.IsEmpty() || !PropValue.IsValid())
		{
			return false;
		}

		if (FProperty* Property = FindPropertyByNameOrAlias_ImportBpy(TargetObject, PropName))
		{
			TargetObject->Modify();
			ApplyJsonValueToProperty_ImportBpy(TargetObject, Property, PropValue);
			ImportedPropertyNames.Add(PropName);
			return true;
		}

		return false;
	};

	for (const TSharedPtr<FJsonValue>& Val : DefaultsArr)
	{
		const TSharedPtr<FJsonObject>* EntryObj = nullptr;
		if (!Val.IsValid() || !Val->TryGetObject(EntryObj) || !EntryObj->IsValid()) continue;

		FString PropName;
		if (!(*EntryObj)->TryGetStringField(TEXT("name"), PropName) || PropName.IsEmpty()) continue;

		const TSharedPtr<FJsonValue>* PropValue = (*EntryObj)->Values.Find(TEXT("value"));
		if (!PropValue || !PropValue->IsValid()) continue;

		if (ApplyDefaultToObject(CDO, PropName, *PropValue))
		{
			continue;
		}

		if (ApplyDefaultToObject(BP, PropName, *PropValue))
		{
			bModifiedBlueprintAsset = true;
		}
	}

	if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(BP))
	{
		const bool bNeedsTargetSkeleton =
			!ImportedPropertyNames.Contains(TEXT("TargetSkeleton")) &&
			AnimBlueprint->TargetSkeleton == nullptr;
		const bool bNeedsPreviewMesh =
			!ImportedPropertyNames.Contains(TEXT("PreviewSkeletalMesh")) &&
			AnimBlueprint->GetPreviewMesh(false) == nullptr;

		if (bNeedsTargetSkeleton)
		{
			if (USkeleton* SourceTargetSkeleton =
					ResolveAnimBlueprintTargetSkeletonFromAssetRegistry_ImportBpy(SourceBlueprintPath))
			{
				AnimBlueprint->Modify();
				AnimBlueprint->TargetSkeleton = SourceTargetSkeleton;
				bModifiedBlueprintAsset = true;
			}
		}

		if (bNeedsPreviewMesh && AnimBlueprint->TargetSkeleton)
		{
			if (USkeletalMesh* SourcePreviewMesh = AnimBlueprint->TargetSkeleton->GetPreviewMesh(true))
			{
				AnimBlueprint->Modify();
				AnimBlueprint->SetPreviewMesh(SourcePreviewMesh, false);
				bModifiedBlueprintAsset = true;
			}
		}
	}

	if (bModifiedBlueprintAsset)
	{
		BP->MarkPackageDirty();
	}
}

void ImportInheritedComponents_ImportBpy(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& InheritedArr)
{
	if (!BP || !BP->GeneratedClass) return;

	UObject* CDO = BP->GeneratedClass->GetDefaultObject(false);
	if (!CDO) return;

	AActor* CDOActor = Cast<AActor>(CDO);
	if (!CDOActor) return;

	TArray<UActorComponent*> Components;
	CDOActor->GetComponents(Components);

	for (const TSharedPtr<FJsonValue>& Val : InheritedArr)
	{
		const TSharedPtr<FJsonObject>* EntryObj = nullptr;
		if (!Val.IsValid() || !Val->TryGetObject(EntryObj) || !EntryObj->IsValid()) continue;

		FString CompName;
		if (!(*EntryObj)->TryGetStringField(TEXT("name"), CompName) || CompName.IsEmpty()) continue;

		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (!(*EntryObj)->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj->IsValid()) continue;

		UActorComponent* TargetComp = nullptr;
		for (UActorComponent* Comp : Components)
		{
			if (Comp &&
				(ComponentNameMatches_ImportBpy(CompName, Comp->GetFName().ToString()) ||
					ComponentNameMatches_ImportBpy(CompName, Comp->GetName())))
			{
				TargetComp = Comp;
				break;
			}
		}
		if (!TargetComp) continue;

		NormalizeInheritedSceneMobility_ImportBpy(BP, CompName, TargetComp, *PropsObj);
		ApplyJsonObjectToObject_ImportBpy(TargetComp, *PropsObj);
	}
}

static UEdGraphNode* FindImportedTopLevelGraphNodeBySerializedUid_ImportBpy(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& SerializedUid)
{
	if (!BP || !Graph || SerializedUid.IsEmpty())
	{
		return nullptr;
	}

	if (UEdGraphNode* RegisteredNode = FindImportedNodeBySerializedUid_ImportBpy(BP, SerializedUid))
	{
		if (RegisteredNode->GetGraph() == Graph)
		{
			return RegisteredNode;
		}
	}

	FGuid ParsedGuid;
	if (!TryParseGuid_ImportBpy(SerializedUid, ParsedGuid))
	{
		return nullptr;
	}

	for (UEdGraphNode* ExistingNode : Graph->Nodes)
	{
		if (ExistingNode && ExistingNode->NodeGuid == ParsedGuid)
		{
			RegisterImportedNodeUid_ImportBpy(BP, SerializedUid, ExistingNode);
			return ExistingNode;
		}
	}

	return nullptr;
}

static bool ReplayTopLevelGraphSerializedConnectionsAfterCompile_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const bool bRepairMissingConnections,
	bool& bOutAnyGraphRepaired,
	FString& OutError)
{
	bOutAnyGraphRepaired = false;
	if (!BP)
	{
		return true;
	}

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedReachableGraphs;
	auto RefreshReachableGraphs = [&]()
	{
		ReachableGraphs.Reset();
		VisitedReachableGraphs.Reset();
		for (UEdGraph* RootGraph : BP->UbergraphPages)
		{
			GatherReachableGraphs_ImportBpy(RootGraph, VisitedReachableGraphs, ReachableGraphs);
		}
		for (UEdGraph* RootGraph : BP->FunctionGraphs)
		{
			GatherReachableGraphs_ImportBpy(RootGraph, VisitedReachableGraphs, ReachableGraphs);
		}
		for (UEdGraph* RootGraph : BP->MacroGraphs)
		{
			GatherReachableGraphs_ImportBpy(RootGraph, VisitedReachableGraphs, ReachableGraphs);
		}
		for (UEdGraph* RootGraph : BP->DelegateSignatureGraphs)
		{
			GatherReachableGraphs_ImportBpy(RootGraph, VisitedReachableGraphs, ReachableGraphs);
		}
		for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
		{
			for (UEdGraph* RootGraph : InterfaceDesc.Graphs)
			{
				GatherReachableGraphs_ImportBpy(RootGraph, VisitedReachableGraphs, ReachableGraphs);
			}
		}
	};
	RefreshReachableGraphs();

	auto ResolveGraphForConnectionReplay = [&](
		const TSharedPtr<FJsonObject>& GraphObj,
		UEdGraph*& OutGraph,
		FString& OutGraphType,
		FString& OutGraphName) -> bool
	{
		OutGraph = nullptr;
		OutGraphType.Reset();
		OutGraphName.Reset();
		if (!GraphObj.IsValid())
		{
			OutError = TEXT("Invalid graph json during post-compile connection replay");
			return false;
		}

		GraphObj->TryGetStringField(TEXT("graph_type"), OutGraphType);
		GraphObj->TryGetStringField(TEXT("name"), OutGraphName);

		if (!IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			return EnsureGraphExists_ImportBpy(BP, GraphObj, OutGraph, OutGraphType, OutGraphName, OutError);
		}

		FString GraphOuterKind;
		FString GraphGuidText;
		GraphObj->TryGetStringField(TEXT("graph_outer"), GraphOuterKind);
		GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuidText);

		auto ResolveFromReachableInventory = [&]() -> UEdGraph*
		{
			TArray<UEdGraph*> Candidates;
			Candidates.Reserve(ReachableGraphs.Num());

			if (!GraphGuidText.IsEmpty())
			{
				FGuid ParsedGraphGuid;
				if (TryParseGuid_ImportBpy(GraphGuidText, ParsedGraphGuid))
				{
					for (UEdGraph* CandidateGraph : ReachableGraphs)
					{
						if (!CandidateGraph)
						{
							continue;
						}
						if (CandidateGraph->GraphGuid != ParsedGraphGuid)
						{
							continue;
						}
						if (!OutGraphName.IsEmpty() &&
							!CandidateGraph->GetName().Equals(OutGraphName, ESearchCase::CaseSensitive))
						{
							continue;
						}
						if (!GraphOuterKind.IsEmpty() &&
							!DescribeGraphOuterKind_ImportBpy(CandidateGraph).Equals(
								GraphOuterKind, ESearchCase::IgnoreCase))
						{
							continue;
						}
						Candidates.Add(CandidateGraph);
					}
				}
			}

			if (Candidates.Num() == 0)
			{
				if (OutGraphName.IsEmpty())
				{
					return nullptr;
				}

				for (UEdGraph* CandidateGraph : ReachableGraphs)
				{
					if (!CandidateGraph ||
						!CandidateGraph->GetName().Equals(OutGraphName, ESearchCase::CaseSensitive))
					{
						continue;
					}
					if (GraphOuterKind.IsEmpty() ||
						DescribeGraphOuterKind_ImportBpy(CandidateGraph).Equals(
							GraphOuterKind, ESearchCase::IgnoreCase))
					{
						Candidates.Add(CandidateGraph);
					}
				}
			}

			if (Candidates.Num() <= 1)
			{
				return Candidates.Num() == 1 ? Candidates[0] : nullptr;
			}

			const TArray<TSharedPtr<FJsonValue>>* SerializedNodesArr = nullptr;
			int32 ExpectedNodeCount = INDEX_NONE;
			TSet<FGuid> SerializedNodeGuids;
			if (GraphObj->TryGetArrayField(TEXT("nodes"), SerializedNodesArr) && SerializedNodesArr)
			{
				ExpectedNodeCount = SerializedNodesArr->Num();
				for (const TSharedPtr<FJsonValue>& NodeValue : *SerializedNodesArr)
				{
					const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
					if (!NodeObj.IsValid())
					{
						continue;
					}
					FString NodeGuidText;
					if (!NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuidText) || NodeGuidText.IsEmpty())
					{
						continue;
					}
					FGuid ParsedNodeGuid;
					if (TryParseGuid_ImportBpy(NodeGuidText, ParsedNodeGuid))
					{
						SerializedNodeGuids.Add(ParsedNodeGuid);
					}
				}
			}

			struct FCandidateScore
			{
				UEdGraph* Graph = nullptr;
				int32 NodeCountDelta = TNumericLimits<int32>::Max();
				int32 GuidHits = -1;
			};

			TArray<FCandidateScore> ScoredCandidates;
			ScoredCandidates.Reserve(Candidates.Num());
			for (UEdGraph* CandidateGraph : Candidates)
			{
				if (!CandidateGraph)
				{
					continue;
				}
				FCandidateScore Score;
				Score.Graph = CandidateGraph;
				if (ExpectedNodeCount != INDEX_NONE)
				{
					Score.NodeCountDelta = FMath::Abs(CandidateGraph->Nodes.Num() - ExpectedNodeCount);
				}
				if (SerializedNodeGuids.Num() > 0)
				{
					int32 Hits = 0;
					for (UEdGraphNode* Node : CandidateGraph->Nodes)
					{
						if (Node && SerializedNodeGuids.Contains(Node->NodeGuid))
						{
							++Hits;
						}
					}
					Score.GuidHits = Hits;
				}
				ScoredCandidates.Add(Score);
			}

			ScoredCandidates.Sort([](const FCandidateScore& A, const FCandidateScore& B)
			{
				if (A.NodeCountDelta != B.NodeCountDelta)
				{
					return A.NodeCountDelta < B.NodeCountDelta;
				}
				if (A.GuidHits != B.GuidHits)
				{
					return A.GuidHits > B.GuidHits;
				}
				return A.Graph < B.Graph;
			});

			return ScoredCandidates.Num() > 0 ? ScoredCandidates[0].Graph : nullptr;
		};

		OutGraph = ResolveFromReachableInventory();
		if (!OutGraph)
		{
			RefreshReachableGraphs();
			OutGraph = ResolveFromReachableInventory();
		}

		if (!OutGraph)
		{
			OutError = FString::Printf(
				TEXT("Post-compile replay could not resolve node-owned graph: name=%s guid=%s outer=%s"),
				*OutGraphName,
				*GraphGuidText,
				*GraphOuterKind);
			return false;
		}

		return true;
	};

	auto ResolveExistingConnectionPin = [](
		UEdGraphNode* Node,
		const FString& PinName,
		const FString& PinFullName,
		const FString& PinId,
		EEdGraphPinDirection Direction) -> UEdGraphPin*
	{
		if (!Node)
		{
			return nullptr;
		}

		UEdGraphPin* Pin = FindPinById_ImportBpy(Node, PinId);
		if (Pin && Pin->Direction != Direction)
		{
			Pin = nullptr;
		}
		if (!Pin && !PinFullName.IsEmpty())
		{
			Pin = FindExistingPinFlexible_ImportBpy(Node, PinFullName, Direction);
		}
		if (!Pin && !PinName.IsEmpty())
		{
			Pin = FindExistingPinFlexible_ImportBpy(Node, PinName, Direction);
		}
		return Pin;
	};

	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		if (!GraphObj.IsValid())
		{
			continue;
		}

		UEdGraph* Graph = nullptr;
		FString GraphType;
		FString GraphName;
		if (!ResolveGraphForConnectionReplay(GraphObj, Graph, GraphType, GraphName))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("connections"), ConnsArr) || !ConnsArr || ConnsArr->Num() == 0)
		{
			continue;
		}

		TMap<FString, UEdGraphNode*> NodeMap;
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString Uid;
			NodeObj->TryGetStringField(TEXT("uid"), Uid);
			if (Uid.IsEmpty())
			{
				continue;
			}

			UEdGraphNode* ExistingNode = FindImportedTopLevelGraphNodeBySerializedUid_ImportBpy(BP, Graph, Uid);
			if (ExistingNode)
			{
				NodeMap.Add(Uid, ExistingNode);
			}
		}

		auto CountMissingConnections = [&]() -> int32
		{
			int32 MissingCount = 0;
			for (const TSharedPtr<FJsonValue>& ConnValue : *ConnsArr)
			{
				const TSharedPtr<FJsonObject> ConnObj = ConnValue.IsValid() ? ConnValue->AsObject() : nullptr;
				if (!ConnObj.IsValid())
				{
					continue;
				}

				FString SrcUid;
				FString DstUid;
				FString SrcPin;
				FString DstPin;
				FString SrcPinFull;
				FString DstPinFull;
				FString SrcPinId;
				FString DstPinId;
				ConnObj->TryGetStringField(TEXT("src_node"), SrcUid);
				ConnObj->TryGetStringField(TEXT("dst_node"), DstUid);
				ConnObj->TryGetStringField(TEXT("src_pin"), SrcPin);
				ConnObj->TryGetStringField(TEXT("dst_pin"), DstPin);
				ConnObj->TryGetStringField(TEXT("src_pin_full"), SrcPinFull);
				ConnObj->TryGetStringField(TEXT("dst_pin_full"), DstPinFull);
				ConnObj->TryGetStringField(TEXT("src_pin_id"), SrcPinId);
				ConnObj->TryGetStringField(TEXT("dst_pin_id"), DstPinId);

				UEdGraphNode* const* SrcNodePtr = NodeMap.Find(SrcUid);
				UEdGraphNode* const* DstNodePtr = NodeMap.Find(DstUid);
				UEdGraphPin* SrcLivePin = ResolveExistingConnectionPin(
					SrcNodePtr ? *SrcNodePtr : nullptr,
					SrcPin,
					SrcPinFull,
					SrcPinId,
					EGPD_Output);
				UEdGraphPin* DstLivePin = ResolveExistingConnectionPin(
					DstNodePtr ? *DstNodePtr : nullptr,
					DstPin,
					DstPinFull,
					DstPinId,
					EGPD_Input);

				const bool bConnected =
					SrcLivePin &&
					DstLivePin &&
					SrcLivePin->LinkedTo.Contains(DstLivePin) &&
					DstLivePin->LinkedTo.Contains(SrcLivePin);
				if (!bConnected)
				{
					++MissingCount;
				}
			}
			return MissingCount;
		};

		const int32 MissingBeforeReplay = CountMissingConnections();
		if (MissingBeforeReplay == 0)
		{
			continue;
		}

		if (!bRepairMissingConnections)
		{
			OutError = FString::Printf(
				TEXT("Post-compile connection parity mismatch in graph %s: missing=%d"),
				*Graph->GetName(),
				MissingBeforeReplay);
			return false;
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy] Replaying graph connections after compile: graph=%s missing_before=%d"),
			*Graph->GetName(),
			MissingBeforeReplay);

		BreakAllGraphLinks_ImportBpy(Graph);

		TArray<TSharedPtr<FJsonObject>> PendingConnections;
		for (const TSharedPtr<FJsonValue>& ConnValue : *ConnsArr)
		{
			const TSharedPtr<FJsonObject> ConnObj = ConnValue.IsValid() ? ConnValue->AsObject() : nullptr;
			if (ConnObj.IsValid())
			{
				PendingConnections.Add(ConnObj);
			}
		}

		auto ConnectPinsForReplay = [&](const TSharedPtr<FJsonObject>& ConnObj, FString& ConnectError) -> bool
		{
			if (!ConnObj.IsValid())
			{
				ConnectError = TEXT("invalid connection payload");
				return false;
			}

			FString SrcUid;
			FString SrcPin;
			FString DstUid;
			FString DstPin;
			FString SrcPinFull;
			FString DstPinFull;
			FString SrcPinId;
			FString DstPinId;
			ConnObj->TryGetStringField(TEXT("src_node"), SrcUid);
			ConnObj->TryGetStringField(TEXT("src_pin"), SrcPin);
			ConnObj->TryGetStringField(TEXT("dst_node"), DstUid);
			ConnObj->TryGetStringField(TEXT("dst_pin"), DstPin);
			ConnObj->TryGetStringField(TEXT("src_pin_full"), SrcPinFull);
			ConnObj->TryGetStringField(TEXT("dst_pin_full"), DstPinFull);
			ConnObj->TryGetStringField(TEXT("src_pin_id"), SrcPinId);
			ConnObj->TryGetStringField(TEXT("dst_pin_id"), DstPinId);

			UEdGraphNode** SrcNodePtr = NodeMap.Find(SrcUid);
			UEdGraphNode** DstNodePtr = NodeMap.Find(DstUid);
			if (!SrcNodePtr || !DstNodePtr || !*SrcNodePtr || !*DstNodePtr)
			{
				ConnectError = FString::Printf(TEXT("missing node(s): %s -> %s"), *SrcUid, *DstUid);
				return false;
			}

			UEdGraphNode* SrcNode = *SrcNodePtr;
			UEdGraphNode* DstNode = *DstNodePtr;

			UEdGraphPin* SrcLivePin = FindPinById_ImportBpy(SrcNode, SrcPinId);
			UEdGraphPin* DstLivePin = FindPinById_ImportBpy(DstNode, DstPinId);
			if (SrcLivePin && SrcLivePin->Direction != EGPD_Output)
			{
				SrcLivePin = nullptr;
			}
			if (DstLivePin && DstLivePin->Direction != EGPD_Input)
			{
				DstLivePin = nullptr;
			}
			if (!SrcLivePin && !SrcPinFull.IsEmpty())
			{
				SrcLivePin = FindPinFlexible_ImportBpy(SrcNode, SrcPinFull, EGPD_Output);
			}
			if (!SrcLivePin && !SrcPin.IsEmpty())
			{
				SrcLivePin = FindPinFlexible_ImportBpy(SrcNode, SrcPin, EGPD_Output);
			}
			if (!DstLivePin && !DstPinFull.IsEmpty())
			{
				DstLivePin = FindPinFlexible_ImportBpy(DstNode, DstPinFull, EGPD_Input);
			}
			if (!DstLivePin && !DstPin.IsEmpty())
			{
				DstLivePin = FindPinFlexible_ImportBpy(DstNode, DstPin, EGPD_Input);
			}

			if (!SrcLivePin || !DstLivePin)
			{
				ConnectError = FString::Printf(
					TEXT("cannot resolve pins: %s.%s -> %s.%s"),
					*DescribeNode_ImportBpy(SrcNode),
					*(!SrcPinFull.IsEmpty() ? SrcPinFull : SrcPin),
					*DescribeNode_ImportBpy(DstNode),
					*(!DstPinFull.IsEmpty() ? DstPinFull : DstPin));
				return false;
			}

			const bool bAlreadyConnected =
				SrcLivePin->LinkedTo.Contains(DstLivePin) &&
				DstLivePin->LinkedTo.Contains(SrcLivePin);
			if (bAlreadyConnected)
			{
				return true;
			}

			const UEdGraphSchema* Schema = SrcLivePin->GetSchema();
			if (Schema && Schema->TryCreateConnection(SrcLivePin, DstLivePin))
			{
				return SrcLivePin->LinkedTo.Contains(DstLivePin) &&
					DstLivePin->LinkedTo.Contains(SrcLivePin);
			}

			const bool bBothExecPins =
				SrcLivePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
				DstLivePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
			if (!bBothExecPins)
			{
				ConnectError = FString::Printf(
					TEXT("schema rejected typed connection: %s.%s -> %s.%s"),
					*DescribeNode_ImportBpy(SrcNode),
					*SrcLivePin->GetName(),
					*DescribeNode_ImportBpy(DstNode),
					*DstLivePin->GetName());
				return false;
			}

			SrcLivePin->MakeLinkTo(DstLivePin);
			SrcNode->PinConnectionListChanged(SrcLivePin);
			DstNode->PinConnectionListChanged(DstLivePin);
			SrcNode->NodeConnectionListChanged();
			DstNode->NodeConnectionListChanged();
			if (UEdGraph* SrcGraph = SrcNode->GetGraph())
			{
				SrcGraph->NotifyGraphChanged();
			}

			return SrcLivePin->LinkedTo.Contains(DstLivePin) &&
				DstLivePin->LinkedTo.Contains(SrcLivePin);
		};

		while (PendingConnections.Num() > 0)
		{
			bool bMadeProgress = false;
			FString LastConnectError;
			TArray<TSharedPtr<FJsonObject>> RemainingConnections;
			RemainingConnections.Reserve(PendingConnections.Num());

			for (const TSharedPtr<FJsonObject>& ConnObj : PendingConnections)
			{
				if (!ConnObj.IsValid())
				{
					continue;
				}

				FString ConnectError;
				if (ConnectPinsForReplay(ConnObj, ConnectError))
				{
					bMadeProgress = true;
					continue;
				}

				LastConnectError = ConnectError;
				RemainingConnections.Add(ConnObj);
			}

			if (!bMadeProgress)
			{
				OutError = FString::Printf(
					TEXT("Post-compile replay failed for graph %s: %s"),
					*Graph->GetName(),
					LastConnectError.IsEmpty() ? TEXT("no progress") : *LastConnectError);
				return false;
			}

			PendingConnections = MoveTemp(RemainingConnections);
		}

		const int32 MissingAfterReplay = CountMissingConnections();
		if (MissingAfterReplay > 0)
		{
			OutError = FString::Printf(
				TEXT("Post-compile replay incomplete for graph %s: missing_after=%d"),
				*Graph->GetName(),
				MissingAfterReplay);
			return false;
		}

		bOutAnyGraphRepaired = true;
	}

	return true;
}

static bool ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	FString& OutError)
{
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return true;
	}

	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		if (!GraphObj.IsValid())
		{
			continue;
		}
		if (IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		UEdGraph* Graph = nullptr;
		FString GraphType;
		FString GraphName;
		if (!EnsureGraphExists_ImportBpy(BP, GraphObj, Graph, GraphType, GraphName, OutError))
		{
			return false;
		}

		const bool bIsAnimationGraph =
			Graph &&
			(Graph->IsA<UAnimationGraph>() ||
				(Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationGraphSchema>()));
		if (!bIsAnimationGraph)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		bool bTouchedCachedPoseNodes = false;

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* ExistingNode =
				FindImportedTopLevelGraphNodeBySerializedUid_ImportBpy(BP, Graph, Uid);
			UAnimGraphNode_StateMachineBase* const StateMachineNode =
				Cast<UAnimGraphNode_StateMachineBase>(ExistingNode);
			const bool bIsBlendStackNode = ResolveBlendStackGraph_ImportBpy(ExistingNode) != nullptr;
			const bool bIsCachedPoseNode =
				Cast<UAnimGraphNode_SaveCachedPose>(ExistingNode) != nullptr ||
				Cast<UAnimGraphNode_UseCachedPose>(ExistingNode) != nullptr;
			if (!StateMachineNode && !bIsBlendStackNode && !bIsCachedPoseNode)
			{
				continue;
			}

			if (bIsBlendStackNode)
			{
				LogBlendStackGraphBindingsAndDuplicates_ImportBpy(
					ExistingNode,
					TEXT("PostCompileReplayBeforeApply"));
			}
			if (bIsCachedPoseNode)
			{
				LogCachedPoseNodeSnapshot_ImportBpy(ExistingNode, TEXT("post_compile_replay_before_apply"));
			}

			if (!ApplyNodeJsonToNode_ImportBpy(ExistingNode, NodeObj, OutError, false))
			{
				return false;
			}

			if (bIsBlendStackNode)
			{
				LogBlendStackGraphBindingsAndDuplicates_ImportBpy(
					ExistingNode,
					TEXT("PostCompileReplayAfterApply"));

				if (UEdGraph* const BlendStackGraph = ResolveBlendStackGraph_ImportBpy(ExistingNode))
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[ExportBpy][ImportDiag][BlendStackReplay] node=%s graph=%s nodes=%d"),
						*DescribeNode_ImportBpy(ExistingNode),
						*BlendStackGraph->GetName(),
						BlendStackGraph->Nodes.Num());
				}
			}

			if (bIsCachedPoseNode)
			{
				bTouchedCachedPoseNodes = true;
				LogCachedPoseNodeSnapshot_ImportBpy(ExistingNode, TEXT("post_compile_replay_after_apply"));
			}
		}

		if (bTouchedCachedPoseNodes)
		{
			ResolveUseCachedPoseLinksInGraph_ImportBpy(Graph);
			LogCachedPoseGraphSnapshot_ImportBpy(Graph, TEXT("post_compile_replay_after_graph_resolve"));
		}
	}

	return true;
}

static void LogAnimBlueprintBlendStackReplayState_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const TCHAR* StageTag)
{
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return;
	}

	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		UEdGraph* Graph = nullptr;
		FString GraphType;
		FString GraphName;
		FString EnsureError;
		if (!EnsureGraphExists_ImportBpy(BP, GraphObj, Graph, GraphType, GraphName, EnsureError))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag][BlendStackReplay][%s] ensure_graph_failed graph=%s error=%s"),
				StageTag ? StageTag : TEXT("Unknown"),
				*GraphName,
				*EnsureError);
			continue;
		}

		const bool bIsAnimationGraph =
			Graph &&
			(Graph->IsA<UAnimationGraph>() ||
				(Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationGraphSchema>()));
		if (!bIsAnimationGraph)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* ExistingNode =
				FindImportedTopLevelGraphNodeBySerializedUid_ImportBpy(BP, Graph, Uid);
			if (!ExistingNode)
			{
				continue;
			}

			UEdGraph* const BlendStackGraph = ResolveBlendStackGraph_ImportBpy(ExistingNode);
			if (!BlendStackGraph)
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[ExportBpy][ImportDiag][BlendStackReplay][%s] node=%s graph=%s outer=%s nodes=%d"),
				StageTag ? StageTag : TEXT("Unknown"),
				*DescribeNode_ImportBpy(ExistingNode),
				*BlendStackGraph->GetPathName(),
				*GetPathNameSafe(BlendStackGraph->GetOuter()),
				BlendStackGraph->Nodes.Num());
		}
	}
}

static void LogSerializedAnimBlueprintBlendStackBindingState_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const TCHAR* StageTag)
{
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return;
	}

	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		UEdGraph* Graph = nullptr;
		FString GraphType;
		FString GraphName;
		FString EnsureError;
		if (!EnsureGraphExists_ImportBpy(BP, GraphObj, Graph, GraphType, GraphName, EnsureError))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag][BlendStackBinding][%s] ensure_graph_failed graph=%s error=%s"),
				StageTag ? StageTag : TEXT("Unknown"),
				*GraphName,
				*EnsureError);
			continue;
		}

		const bool bIsAnimationGraph =
			Graph &&
			(Graph->IsA<UAnimationGraph>() ||
				(Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationGraphSchema>()));
		if (!bIsAnimationGraph)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* ExistingNode =
				FindImportedTopLevelGraphNodeBySerializedUid_ImportBpy(BP, Graph, Uid);
			if (!ExistingNode || !ResolveBlendStackGraph_ImportBpy(ExistingNode))
			{
				continue;
			}

			LogBlendStackGraphBindingsAndDuplicates_ImportBpy(ExistingNode, StageTag);
		}
	}
}

struct FNodeClassCounts_ImportBpy
{
	int32 EventNodes = 0;
	int32 CustomEventNodes = 0;
	int32 CreateDelegateNodes = 0;
};

void AccumulateNodeClassCountByName_ImportBpy(const FString& NodeClassName, FNodeClassCounts_ImportBpy& InOutCounts)
{
	if (NodeClassName == TEXT("K2Node_CustomEvent"))
	{
		++InOutCounts.CustomEventNodes;
	}
	else if (NodeClassName == TEXT("K2Node_Event"))
	{
		++InOutCounts.EventNodes;
	}
	else if (NodeClassName == TEXT("K2Node_CreateDelegate"))
	{
		++InOutCounts.CreateDelegateNodes;
	}
}

bool AccumulateSerializedNodeClassCountsFromGraphJson_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	FNodeClassCounts_ImportBpy& InOutCounts,
	TSet<FString>& VisitedGraphGuids,
	FString& OutError)
{
	if (!GraphObj.IsValid())
	{
		return true;
	}

	FString GraphGuid;
	if (GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuid) && !GraphGuid.IsEmpty())
	{
		if (VisitedGraphGuids.Contains(GraphGuid))
		{
			return true;
		}
		VisitedGraphGuids.Add(GraphGuid);
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return true;
	}

	constexpr std::initializer_list<const TCHAR*> NestedGraphJsonFields =
	{
		TEXT("BoundGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClassName;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
		AccumulateNodeClassCountByName_ImportBpy(NodeClassName, InOutCounts);

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		if (!NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
		{
			continue;
		}

		for (const TCHAR* FieldName : NestedGraphJsonFields)
		{
			FString NestedGraphJsonText;
			if (!(*NodePropsObj)->TryGetStringField(FieldName, NestedGraphJsonText) || NestedGraphJsonText.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphObj;
			TSharedRef<TJsonReader<>> NestedReader = TJsonReaderFactory<>::Create(NestedGraphJsonText);
			if (!FJsonSerializer::Deserialize(NestedReader, NestedGraphObj) || !NestedGraphObj.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Failed to parse %s while collecting import topology stats"),
					FieldName);
				return false;
			}

			if (!AccumulateSerializedNodeClassCountsFromGraphJson_ImportBpy(
					NestedGraphObj,
					InOutCounts,
					VisitedGraphGuids,
					OutError))
			{
				return false;
			}
		}
	}

	return true;
}

void CollectImportedBlueprintNodeClassCounts_ImportBpy(
	UBlueprint* BP,
	FNodeClassCounts_ImportBpy& OutCounts)
{
	OutCounts = FNodeClassCounts_ImportBpy{};
	if (!BP)
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			if (Node->IsA<UK2Node_CustomEvent>())
			{
				++OutCounts.CustomEventNodes;
				continue;
			}

			if (Node->IsA<UK2Node_Event>())
			{
				++OutCounts.EventNodes;
				continue;
			}

			if (Node->IsA<UK2Node_CreateDelegate>())
			{
				++OutCounts.CreateDelegateNodes;
			}
		}
	}
}

int32 CountBlueprintDispatcherVariables_ImportBpy(UBlueprint* BP)
{
	if (!BP)
	{
		return 0;
	}

	int32 DispatcherCount = 0;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate ||
			Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
		{
			++DispatcherCount;
		}
	}

	return DispatcherCount;
}

bool ValidateImportedBlueprintStructuralParityAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const bool bRelaxRootNodeCountChecks,
	FString& OutError)
{
	if (!BP || !Root.IsValid())
	{
		OutError = TEXT("Invalid validation context while checking import structural parity");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return true;
	}

	TMap<FString, int32> ExpectedRootGraphNodeCounts;
	int32 ExpectedRootGraphCount = 0;
	int32 ExpectedFunctionGraphCount = 0;
	int32 ExpectedMacroGraphCount = 0;
	int32 ExpectedEventGraphCount = 0;
	FNodeClassCounts_ImportBpy ExpectedNodeClassCounts;
	TSet<FString> VisitedSerializedGraphGuids;

	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid())
		{
			continue;
		}
		if (IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		++ExpectedRootGraphCount;

		FString GraphType;
		GraphObj->TryGetStringField(TEXT("graph_type"), GraphType);
		if (GraphType.Equals(TEXT("function"), ESearchCase::IgnoreCase))
		{
			++ExpectedFunctionGraphCount;
		}
		else if (GraphType.Equals(TEXT("macro"), ESearchCase::IgnoreCase))
		{
			++ExpectedMacroGraphCount;
		}
		else if (GraphType.Equals(TEXT("event_graph"), ESearchCase::IgnoreCase))
		{
			++ExpectedEventGraphCount;
		}

		FString GraphName;
		GraphObj->TryGetStringField(TEXT("name"), GraphName);
		if (!GraphName.IsEmpty())
		{
			const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
			const int32 NodeCount =
				GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) && NodesArr ? NodesArr->Num() : 0;
			ExpectedRootGraphNodeCounts.Add(GraphName, NodeCount);
		}

		if (!AccumulateSerializedNodeClassCountsFromGraphJson_ImportBpy(
				GraphObj,
				ExpectedNodeClassCounts,
				VisitedSerializedGraphGuids,
				OutError))
		{
			return false;
		}
	}

	TMap<FString, UEdGraph*> ActualRootGraphsByName;
	auto RegisterRootGraph = [&ActualRootGraphsByName](UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}

		const FString GraphName = Graph->GetName();
		if (GraphName.IsEmpty() || ActualRootGraphsByName.Contains(GraphName))
		{
			return;
		}

		ActualRootGraphsByName.Add(GraphName, Graph);
	};

	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		RegisterRootGraph(Graph);
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		RegisterRootGraph(Graph);
	}
	for (UEdGraph* Graph : BP->MacroGraphs)
	{
		RegisterRootGraph(Graph);
	}
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			RegisterRootGraph(Graph);
		}
	}

	const int32 ActualRootGraphCount = ActualRootGraphsByName.Num();
	int32 ActualFunctionGraphCount = BP->FunctionGraphs.Num();
	{
		TSet<const UEdGraph*> CountedFunctionGraphs;
		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (Graph)
			{
				CountedFunctionGraphs.Add(Graph);
			}
		}
		for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
		{
			for (UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				if (Graph && !CountedFunctionGraphs.Contains(Graph))
				{
					CountedFunctionGraphs.Add(Graph);
					++ActualFunctionGraphCount;
				}
			}
		}
	}
	const int32 ActualMacroGraphCount = BP->MacroGraphs.Num();
	const int32 ActualEventGraphCount = BP->UbergraphPages.Num();

	FNodeClassCounts_ImportBpy ActualNodeClassCounts;
	CollectImportedBlueprintNodeClassCounts_ImportBpy(BP, ActualNodeClassCounts);

	int32 ExpectedDispatchers = 0;
	const TArray<TSharedPtr<FJsonValue>>* DispatchersArr = nullptr;
	if (Root->TryGetArrayField(TEXT("dispatchers"), DispatchersArr) && DispatchersArr)
	{
		ExpectedDispatchers = DispatchersArr->Num();
	}
	const int32 ActualDispatchers = CountBlueprintDispatcherVariables_ImportBpy(BP);

	TArray<FString> Mismatches;
	auto AddMismatch = [&Mismatches](const FString& Key, const int32 ExpectedValue, const int32 ActualValue)
	{
		if (ExpectedValue != ActualValue)
		{
			Mismatches.Add(FString::Printf(TEXT("%s expected=%d actual=%d"), *Key, ExpectedValue, ActualValue));
		}
	};

	AddMismatch(TEXT("root_graph_count"), ExpectedRootGraphCount, ActualRootGraphCount);
	AddMismatch(TEXT("function_graph_count"), ExpectedFunctionGraphCount, ActualFunctionGraphCount);
	AddMismatch(TEXT("macro_graph_count"), ExpectedMacroGraphCount, ActualMacroGraphCount);
	AddMismatch(TEXT("event_graph_count"), ExpectedEventGraphCount, ActualEventGraphCount);
	AddMismatch(TEXT("dispatcher_count"), ExpectedDispatchers, ActualDispatchers);

	AddMismatch(TEXT("K2Node_Event_count"), ExpectedNodeClassCounts.EventNodes, ActualNodeClassCounts.EventNodes);
	AddMismatch(TEXT("K2Node_CustomEvent_count"), ExpectedNodeClassCounts.CustomEventNodes, ActualNodeClassCounts.CustomEventNodes);
	AddMismatch(
		TEXT("K2Node_CreateDelegate_count"),
		ExpectedNodeClassCounts.CreateDelegateNodes,
		ActualNodeClassCounts.CreateDelegateNodes);

	for (const TPair<FString, int32>& ExpectedEntry : ExpectedRootGraphNodeCounts)
	{
		UEdGraph* const* ActualGraphPtr = ActualRootGraphsByName.Find(ExpectedEntry.Key);
		if (!ActualGraphPtr || !*ActualGraphPtr)
		{
			if (UEdGraph* ExistingUnregisteredGraph = FindObject<UEdGraph>(BP, *ExpectedEntry.Key))
			{
				Mismatches.Add(FString::Printf(
					TEXT("missing_root_graph name=%s (unregistered_graph_found outer=%s class=%s)"),
					*ExpectedEntry.Key,
					*GetPathNameSafe(ExistingUnregisteredGraph->GetOuter()),
					*GetNameSafe(ExistingUnregisteredGraph->GetClass())));
			}
			else
			{
				Mismatches.Add(FString::Printf(TEXT("missing_root_graph name=%s (graph_object_not_found)"), *ExpectedEntry.Key));
			}
			continue;
		}

		if (!bRelaxRootNodeCountChecks)
		{
			const int32 ActualNodeCount = (*ActualGraphPtr)->Nodes.Num();
			if (ActualNodeCount != ExpectedEntry.Value)
			{
				Mismatches.Add(FString::Printf(
					TEXT("graph_nodes[%s] expected=%d actual=%d"),
					*ExpectedEntry.Key,
					ExpectedEntry.Value,
					ActualNodeCount));
			}
		}
	}

	if (Mismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("Import structural parity mismatch (%s): %s"),
			bRelaxRootNodeCountChecks ? TEXT("post_compile") : TEXT("pre_compile"),
			*FString::Join(Mismatches, TEXT("; ")));
		return false;
	}

	return true;
}

namespace
{
UEdGraph* FindRootGraphByName_ImportBpy(UBlueprint* BP, const FString& GraphName)
{
	if (!BP || GraphName.IsEmpty())
	{
		return nullptr;
	}

	auto MatchByName = [&GraphName](UEdGraph* Graph) -> bool
	{
		return Graph && Graph->GetName().Equals(GraphName, ESearchCase::CaseSensitive);
	};

	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (MatchByName(Graph))
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (MatchByName(Graph))
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : BP->MacroGraphs)
	{
		if (MatchByName(Graph))
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : BP->DelegateSignatureGraphs)
	{
		if (MatchByName(Graph))
		{
			return Graph;
		}
	}
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			if (MatchByName(Graph))
			{
				return Graph;
			}
		}
	}

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (MatchByName(Graph) && Graph->GetOuter() == BP)
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : AllGraphs)
	{
		if (MatchByName(Graph))
		{
			return Graph;
		}
	}

	return nullptr;
}

bool GraphIsBoundToInterface_ImportBpy(
	const UBlueprint* BP,
	const UEdGraph* Graph,
	const UClass* InterfaceClass)
{
	if (!BP || !Graph || !InterfaceClass)
	{
		return false;
	}

	const UClass* ExpectedInterface = InterfaceClass->GetAuthoritativeClass();
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		const UClass* ActualInterface = InterfaceDesc.Interface ? InterfaceDesc.Interface->GetAuthoritativeClass() : nullptr;
		if (ActualInterface != ExpectedInterface)
		{
			continue;
		}

		if (InterfaceDesc.Graphs.Contains(const_cast<UEdGraph*>(Graph)))
		{
			return true;
		}
	}

	return false;
}

bool BlueprintDeclaresOwnFunctionByName_ImportBpy(const UBlueprint* BP, const FName FunctionName)
{
	if (!BP || FunctionName.IsNone())
	{
		return false;
	}

	auto ClassDeclaresOwnFunction = [FunctionName](const UClass* OwnerClass) -> bool
	{
		if (!OwnerClass)
		{
			return false;
		}

		const UFunction* Func = OwnerClass->FindFunctionByName(FunctionName);
		if (!Func)
		{
			return false;
		}

		return Func->GetOuterUClass() == OwnerClass;
	};

	if (ClassDeclaresOwnFunction(BP->GeneratedClass))
	{
		return true;
	}

	if (ClassDeclaresOwnFunction(BP->SkeletonGeneratedClass))
	{
		return true;
	}

	return false;
}

bool RootGraphHasPoseHistoryCollectorNode_ImportBpy(const TSharedPtr<FJsonObject>& GraphObj)
{
	if (!GraphObj.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClass;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);
		if (NodeClass.Contains(TEXT("PoseSearchHistoryCollector"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

FString ReadNodePropertyAsText_ImportBpy(UEdGraphNode* Node, const TCHAR* PropertyName)
{
	if (!Node || !PropertyName)
	{
		return FString();
	}

	FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
	if (!Property)
	{
		return FString();
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
	if (!ValuePtr)
	{
		return FString();
	}

	FString Exported;
	Property->ExportTextItem_Direct(Exported, ValuePtr, nullptr, Node, PPF_None);
	Exported.TrimStartAndEndInline();
	if (Exported.StartsWith(TEXT("\"")) && Exported.EndsWith(TEXT("\"")) && Exported.Len() >= 2)
	{
		Exported = Exported.Mid(1, Exported.Len() - 2);
	}

	return Exported;
}

bool GraphContainsPoseHistoryCollectorRuntimeNode_ImportBpy(UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || !Node->GetClass())
		{
			continue;
		}

		if (Node->GetClass()->GetName().Contains(TEXT("PoseSearchHistoryCollector"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool ValidateImportedInterfaceBindingsAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError)
{
	if (!BP || !Root.IsValid())
	{
		OutError = TEXT("Invalid context while validating interface graph bindings");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* InterfacesArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("interfaces"), InterfacesArr) || !InterfacesArr || InterfacesArr->Num() == 0)
	{
		return true;
	}

	TArray<UClass*> ExpectedInterfaces;
	TArray<FString> Mismatches;
	TSet<FName> ExpectedInterfaceFunctionNames;
	for (const TSharedPtr<FJsonValue>& InterfaceValue : *InterfacesArr)
	{
		if (!InterfaceValue.IsValid())
		{
			continue;
		}

		const FString InterfacePath = InterfaceValue->AsString();
		if (InterfacePath.IsEmpty())
		{
			continue;
		}

		UClass* InterfaceClass = ResolveNamedObject_ImportBpy<UClass>(InterfacePath);
		if (!InterfaceClass)
		{
			Mismatches.Add(FString::Printf(TEXT("unresolved_interface=%s"), *InterfacePath));
			continue;
		}

		InterfaceClass = InterfaceClass->GetAuthoritativeClass();
		ExpectedInterfaces.AddUnique(InterfaceClass);
	}

	for (UClass* ExpectedInterface : ExpectedInterfaces)
	{
		bool bFound = false;
		for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
		{
			const UClass* ActualClass = InterfaceDesc.Interface ? InterfaceDesc.Interface->GetAuthoritativeClass() : nullptr;
			if (ActualClass == ExpectedInterface)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			Mismatches.Add(FString::Printf(TEXT("missing_implemented_interface=%s"), *GetPathNameSafe(ExpectedInterface)));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("graphs"), GraphsArr) && GraphsArr)
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
		{
			const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
			if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
			{
				continue;
			}

			FString GraphType;
			GraphObj->TryGetStringField(TEXT("graph_type"), GraphType);
			if (!GraphType.Equals(TEXT("function"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FString GraphName;
			GraphObj->TryGetStringField(TEXT("name"), GraphName);
			if (GraphName.IsEmpty())
			{
				continue;
			}

			TArray<UClass*> InterfaceOwners;
			const FName FunctionName(*GraphName);
			for (UClass* InterfaceClass : ExpectedInterfaces)
			{
				if (InterfaceClass && InterfaceClass->FindFunctionByName(FunctionName))
				{
					InterfaceOwners.Add(InterfaceClass);
				}
			}

			if (InterfaceOwners.Num() == 0)
			{
				continue;
			}
			ExpectedInterfaceFunctionNames.Add(FunctionName);

			UEdGraph* Graph = FindRootGraphByName_ImportBpy(BP, GraphName);
			if (!Graph)
			{
				Mismatches.Add(FString::Printf(TEXT("missing_interface_function_graph=%s"), *GraphName));
				continue;
			}

			for (UClass* InterfaceClass : InterfaceOwners)
			{
				if (!GraphIsBoundToInterface_ImportBpy(BP, Graph, InterfaceClass))
				{
					Mismatches.Add(
						FString::Printf(
							TEXT("interface_binding_mismatch graph=%s interface=%s"),
							*GraphName,
							*GetPathNameSafe(InterfaceClass)));
				}
			}
		}
	}

	const bool bPostCompileStage =
		StageName &&
		(FCString::Strifind(StageName, TEXT("post")) != nullptr);
	if (bPostCompileStage)
	{
		for (const FName FunctionName : ExpectedInterfaceFunctionNames)
		{
			if (!BlueprintDeclaresOwnFunctionByName_ImportBpy(BP, FunctionName))
			{
				Mismatches.Add(FString::Printf(
					TEXT("missing_interface_function_declaration=%s"),
					*FunctionName.ToString()));
			}
		}
	}

	if (Mismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("Import interface binding validation failed (%s): %s"),
			StageName ? StageName : TEXT("unknown"),
			*FString::Join(Mismatches, TEXT("; ")));
		return false;
	}

	return true;
}

bool ValidateAnimBlueprintPoseHistoryContractAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError)
{
	if (!BP || !Root.IsValid() || !Cast<UAnimBlueprint>(BP))
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return true;
	}

	FString PoseHistoryGraphName = TEXT("Get_PoseHistory");
	int32 ExpectedPoseHistoryGraphNodeCount = -1;
	TSet<FString> ExpectedAnimNodeReferenceTags;
	bool bExpectConvertToPoseHistoryNode = false;
	bool bExpectGetPoseHistoryReferenceNode = false;
	bool bExpectPoseSearchHistoryCollectorNode = false;

	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		FString GraphName;
		GraphObj->TryGetStringField(TEXT("name"), GraphName);
		FString GraphType;
		GraphObj->TryGetStringField(TEXT("graph_type"), GraphType);

		if (GraphName.Equals(UEdGraphSchema_K2::GN_AnimGraph.ToString(), ESearchCase::CaseSensitive))
		{
			bExpectPoseSearchHistoryCollectorNode = RootGraphHasPoseHistoryCollectorNode_ImportBpy(GraphObj);
		}

		if (!GraphType.Equals(TEXT("function"), ESearchCase::IgnoreCase) ||
			!GraphName.Equals(PoseHistoryGraphName, ESearchCase::CaseSensitive))
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		ExpectedPoseHistoryGraphNodeCount = NodesArr->Num();

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString NodeClass;
			NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);

			FString FunctionRef;
			NodeObj->TryGetStringField(TEXT("function_ref"), FunctionRef);
			if (FunctionRef.Contains(TEXT("ConvertToPoseHistoryNodePure"), ESearchCase::IgnoreCase))
			{
				bExpectConvertToPoseHistoryNode = true;
			}
			if (FunctionRef.Contains(TEXT("GetPoseHistoryReference"), ESearchCase::IgnoreCase))
			{
				bExpectGetPoseHistoryReferenceNode = true;
			}

			if (NodeClass.Equals(TEXT("K2Node_AnimNodeReference"), ESearchCase::CaseSensitive))
			{
				const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
				if (!NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
				{
					continue;
				}

				FString TagValue;
				if ((*NodePropsObj)->TryGetStringField(TEXT("Tag"), TagValue) && !TagValue.IsEmpty())
				{
					ExpectedAnimNodeReferenceTags.Add(TagValue);
				}
			}
		}
	}

	if (ExpectedPoseHistoryGraphNodeCount < 0 &&
		ExpectedAnimNodeReferenceTags.Num() == 0 &&
		!bExpectConvertToPoseHistoryNode &&
		!bExpectGetPoseHistoryReferenceNode &&
		!bExpectPoseSearchHistoryCollectorNode)
	{
		return true;
	}

	UEdGraph* PoseHistoryGraph = FindRootGraphByName_ImportBpy(BP, PoseHistoryGraphName);
	if (!PoseHistoryGraph)
	{
		OutError = FString::Printf(
			TEXT("Import runtime contract validation failed (%s): missing function graph '%s'"),
			StageName ? StageName : TEXT("unknown"),
			*PoseHistoryGraphName);
		return false;
	}

	if (ExpectedPoseHistoryGraphNodeCount >= 0 &&
		PoseHistoryGraph->Nodes.Num() != ExpectedPoseHistoryGraphNodeCount)
	{
		OutError = FString::Printf(
			TEXT("Import runtime contract validation failed (%s): graph '%s' node_count expected=%d actual=%d"),
			StageName ? StageName : TEXT("unknown"),
			*PoseHistoryGraphName,
			ExpectedPoseHistoryGraphNodeCount,
			PoseHistoryGraph->Nodes.Num());
		return false;
	}

	TSet<FString> ActualAnimNodeReferenceTags;
	bool bActualHasConvertToPoseHistoryNode = false;
	bool bActualHasGetPoseHistoryReferenceNode = false;
	for (UEdGraphNode* Node : PoseHistoryGraph->Nodes)
	{
		if (!Node || !Node->GetClass())
		{
			continue;
		}

		const FString NodeClassName = Node->GetClass()->GetName();
		if (NodeClassName.Equals(TEXT("K2Node_AnimNodeReference"), ESearchCase::CaseSensitive))
		{
			const FString Tag = ReadNodePropertyAsText_ImportBpy(Node, TEXT("Tag"));
			if (!Tag.IsEmpty())
			{
				ActualAnimNodeReferenceTags.Add(Tag);
			}
		}

		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			FString FunctionName = CallNode->FunctionReference.GetMemberName().ToString();
			if (FunctionName.IsEmpty())
			{
				if (const UFunction* TargetFunction = CallNode->GetTargetFunction())
				{
					FunctionName = TargetFunction->GetName();
				}
			}

			if (FunctionName.Equals(TEXT("ConvertToPoseHistoryNodePure"), ESearchCase::CaseSensitive))
			{
				bActualHasConvertToPoseHistoryNode = true;
			}
			if (FunctionName.Equals(TEXT("GetPoseHistoryReference"), ESearchCase::CaseSensitive))
			{
				bActualHasGetPoseHistoryReferenceNode = true;
			}
		}
	}

	for (const FString& ExpectedTag : ExpectedAnimNodeReferenceTags)
	{
		if (!ActualAnimNodeReferenceTags.Contains(ExpectedTag))
		{
			OutError = FString::Printf(
				TEXT("Import runtime contract validation failed (%s): graph '%s' missing AnimNodeReference tag '%s'"),
				StageName ? StageName : TEXT("unknown"),
				*PoseHistoryGraphName,
				*ExpectedTag);
			return false;
		}
	}

	if (bExpectConvertToPoseHistoryNode && !bActualHasConvertToPoseHistoryNode)
	{
		OutError = FString::Printf(
			TEXT("Import runtime contract validation failed (%s): graph '%s' missing ConvertToPoseHistoryNodePure call"),
			StageName ? StageName : TEXT("unknown"),
			*PoseHistoryGraphName);
		return false;
	}

	if (bExpectGetPoseHistoryReferenceNode && !bActualHasGetPoseHistoryReferenceNode)
	{
		OutError = FString::Printf(
			TEXT("Import runtime contract validation failed (%s): graph '%s' missing GetPoseHistoryReference call"),
			StageName ? StageName : TEXT("unknown"),
			*PoseHistoryGraphName);
		return false;
	}

	if (bExpectPoseSearchHistoryCollectorNode)
	{
		UEdGraph* AnimGraph = FindRootGraphByName_ImportBpy(BP, UEdGraphSchema_K2::GN_AnimGraph.ToString());
		if (!AnimGraph || !GraphContainsPoseHistoryCollectorRuntimeNode_ImportBpy(AnimGraph))
		{
			OutError = FString::Printf(
				TEXT("Import runtime contract validation failed (%s): AnimGraph missing PoseSearchHistoryCollector runtime node"),
				StageName ? StageName : TEXT("unknown"));
			return false;
		}
	}

	return true;
}

bool RepairAnimBlueprintStateMachineEntryBindings_ImportBpy(
	UBlueprint* BP,
	int32& OutRepairedCount,
	FString& OutError,
	const TSharedPtr<FJsonObject>* RootJsonForBpyData)
{
	// Keep state-node entry event names aligned with state-result hook functions.
	OutRepairedCount = 0;
	OutError.Reset();
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return true;
	}

	// Pre-collect snapshots from bpy data if RootJson is provided
	// This is needed because AnimGraphNode_StateResult.Node is only populated AFTER compilation,
	// but we need the StateEntryFunctionName from the bpy data for repair
	TMap<FString, FStateMachineBindingSnapshot_ImportBpy> BpyDataSnapshots;
	if (RootJsonForBpyData && RootJsonForBpyData->IsValid())
	{
		CollectSerializedStateMachineBindingSnapshotsFromRootJson_ImportBpy(
			*RootJsonForBpyData,
			BpyDataSnapshots);
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	TSet<FName> BlueprintFunctionNames;
	for (UEdGraph* FunctionGraph : BP->FunctionGraphs)
	{
		if (FunctionGraph)
		{
			BlueprintFunctionNames.Add(FName(*FunctionGraph->GetName()));
		}
	}

	auto IsPlaceholderHookName = [](const FName& Name) -> bool
	{
		return Name == FName(TEXT("OnStateEntry")) || Name == FName(TEXT("OnUpdate"));
	};

	auto NormalizeKey = [](const FString& Input) -> FString
	{
		FString Out;
		Out.Reserve(Input.Len());
		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			const TCHAR Ch = Input[Index];
			if (FChar::IsAlnum(Ch))
			{
				Out.AppendChar(FChar::ToLower(Ch));
			}
		}
		return Out;
	};

	auto BuildStateCamelToken = [](const FString& Input) -> FString
	{
		FString Out;
		bool bNewWord = true;
		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			const TCHAR Ch = Input[Index];
			if (!FChar::IsAlnum(Ch))
			{
				bNewWord = true;
				continue;
			}

			if (bNewWord)
			{
				Out.AppendChar(FChar::ToUpper(Ch));
				bNewWord = false;
			}
			else
			{
				Out.AppendChar(FChar::ToLower(Ch));
			}
		}
		return Out;
	};

	auto ResolveBestEntryFunctionNameForState = [&BlueprintFunctionNames, &NormalizeKey, &BuildStateCamelToken](
		const UAnimStateNode* StateNode,
		const FName ParsedStateEntryName) -> FName
	{
		if (!StateNode)
		{
			return ParsedStateEntryName;
		}

		const FString StateTitle = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		const FString StateGraphName = StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : FString();
		const FString StateTokenSource = !StateTitle.IsEmpty() ? StateTitle : StateGraphName;
		const FString StateCamel = BuildStateCamelToken(StateTokenSource);
		const FString StateKey = NormalizeKey(StateTokenSource);
		if (!StateCamel.IsEmpty())
		{
			const FName ExactCandidate(*FString::Printf(TEXT("OnStateEntry_%s"), *StateCamel));
			if (BlueprintFunctionNames.Contains(ExactCandidate))
			{
				return ExactCandidate;
			}
		}

		if (!StateKey.IsEmpty())
		{
			for (const FName CandidateName : BlueprintFunctionNames)
			{
				const FString Candidate = CandidateName.ToString();
				if (!Candidate.StartsWith(TEXT("OnStateEntry_"), ESearchCase::CaseSensitive))
				{
					continue;
				}

				const FString CandidateSuffix = Candidate.Mid(13);
				if (NormalizeKey(CandidateSuffix).Equals(StateKey, ESearchCase::CaseSensitive))
				{
					return CandidateName;
				}
			}
		}

		if (!ParsedStateEntryName.IsNone() &&
			ParsedStateEntryName != FName(TEXT("OnStateEntry")) &&
			ParsedStateEntryName != FName(TEXT("OnUpdate")) &&
			BlueprintFunctionNames.Contains(ParsedStateEntryName))
		{
			return ParsedStateEntryName;
		}

		return ParsedStateEntryName;
	};

	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimStateNode* const StateNode = Cast<UAnimStateNode>(Node);
			if (!StateNode || !StateNode->BoundGraph)
			{
				continue;
			}

			FName DesiredNotifyName = NAME_None;
			for (UEdGraphNode* BoundNode : StateNode->BoundGraph->Nodes)
			{
				if (UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(BoundNode))
				{
					FString StateResultNodeText;
					FName StateEntryFunctionFromBpy = NAME_None;

					// First try to get from bpy data snapshots (which have the correct pre-compilation value)
					if (!BpyDataSnapshots.IsEmpty())
					{
						// Normalize GUID to match how it's stored in snapshots (uppercase, DigitsWithHyphens)
						const FString StateResultGuid = StateResultNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).ToUpper();
						const FStateMachineBindingSnapshot_ImportBpy* Snapshot = BpyDataSnapshots.Find(StateResultGuid);
						if (Snapshot && !Snapshot->StateEntryFunctionName.IsNone())
						{
							StateEntryFunctionFromBpy = Snapshot->StateEntryFunctionName;
						}
					}

					// Use bpy data if found, otherwise fall back to live node property
					if (!StateEntryFunctionFromBpy.IsNone())
					{
						DesiredNotifyName = StateEntryFunctionFromBpy;
					}
					else
					{
						// Live node's Node property - only populated after compilation
						StateResultNodeText =
							ReadNodePropertyAsText_ImportBpy(StateResultNode, TEXT("Node"));
						DesiredNotifyName =
							ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
								StateResultNodeText,
								TEXT("StateEntryFunction"));
					}

					if (!DesiredNotifyName.IsNone())
					{
						break;
					}
				}
			}

			DesiredNotifyName = ResolveBestEntryFunctionNameForState(StateNode, DesiredNotifyName);
			if (DesiredNotifyName.IsNone())
			{
				continue;
			}

			const FName ExistingNotifyName = StateNode->StateEntered.NotifyName;
			const FGuid DesiredNotifyGuid = ResolveBlueprintFunctionGuid_ImportBpy(BP, DesiredNotifyName);
			const bool bExistingIsPlaceholder =
				ExistingNotifyName == FName(TEXT("OnStateEntry")) ||
				ExistingNotifyName == FName(TEXT("OnUpdate"));
			const bool bExistingFunctionExists = BlueprintFunctionNames.Contains(ExistingNotifyName);
			const bool bExistingNameIsAcceptable =
				ExistingNotifyName == DesiredNotifyName ||
				(!ExistingNotifyName.IsNone() && !bExistingIsPlaceholder && bExistingFunctionExists);
			const bool bExistingGuidMatches =
				DesiredNotifyGuid.IsValid() && StateNode->StateEntered.Guid == DesiredNotifyGuid;
			if (bExistingNameIsAcceptable && (!DesiredNotifyGuid.IsValid() || bExistingGuidMatches))
			{
				continue;
			}

			const FName PreviousNotifyName = ExistingNotifyName;
			const FGuid PreviousNotifyGuid = StateNode->StateEntered.Guid;
			StateNode->Modify();
			if (ExistingNotifyName.IsNone() || bExistingIsPlaceholder || !bExistingFunctionExists)
			{
				StateNode->StateEntered.NotifyName = DesiredNotifyName;
			}
			if (DesiredNotifyGuid.IsValid())
			{
				StateNode->StateEntered.Guid = DesiredNotifyGuid;
			}

			++OutRepairedCount;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag][StateEntryRepair] graph=%s state=%s node=%s from=%s/%s to=%s/%s"),
				*Graph->GetName(),
				*StateNode->GetName(),
				*DescribeNode_ImportBpy(StateNode),
				PreviousNotifyName.IsNone() ? TEXT("None") : *PreviousNotifyName.ToString(),
				PreviousNotifyGuid.IsValid() ? *PreviousNotifyGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("None"),
				*StateNode->StateEntered.NotifyName.ToString(),
				StateNode->StateEntered.Guid.IsValid() ? *StateNode->StateEntered.Guid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("None"));
		}
	}

	if (OutRepairedCount > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}

	return true;
}

bool ValidateAnimBlueprintStateMachineEntryBindingPresence_ImportBpy(
	UBlueprint* BP,
	const TCHAR* StageName,
	FString& OutError)
{
	OutError.Reset();
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return true;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	TSet<FName> BlueprintFunctionNames;
	for (UEdGraph* FunctionGraph : BP->FunctionGraphs)
	{
		if (FunctionGraph)
		{
			BlueprintFunctionNames.Add(FName(*FunctionGraph->GetName()));
		}
	}

	auto NormalizeKey = [](const FString& Input) -> FString
	{
		FString Out;
		Out.Reserve(Input.Len());
		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			const TCHAR Ch = Input[Index];
			if (FChar::IsAlnum(Ch))
			{
				Out.AppendChar(FChar::ToLower(Ch));
			}
		}
		return Out;
	};

	auto BuildStateCamelToken = [](const FString& Input) -> FString
	{
		FString Out;
		bool bNewWord = true;
		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			const TCHAR Ch = Input[Index];
			if (!FChar::IsAlnum(Ch))
			{
				bNewWord = true;
				continue;
			}

			if (bNewWord)
			{
				Out.AppendChar(FChar::ToUpper(Ch));
				bNewWord = false;
			}
			else
			{
				Out.AppendChar(FChar::ToLower(Ch));
			}
		}
		return Out;
	};

	auto ResolveBestEntryFunctionNameForState = [&BlueprintFunctionNames, &NormalizeKey, &BuildStateCamelToken](
		const UAnimStateNode* StateNode,
		const FName ParsedStateEntryName) -> FName
	{
		if (!StateNode)
		{
			return ParsedStateEntryName;
		}

		const FString StateTitle = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		const FString StateGraphName = StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : FString();
		const FString StateTokenSource = !StateTitle.IsEmpty() ? StateTitle : StateGraphName;
		const FString StateCamel = BuildStateCamelToken(StateTokenSource);
		const FString StateKey = NormalizeKey(StateTokenSource);
		if (!StateCamel.IsEmpty())
		{
			const FName ExactCandidate(*FString::Printf(TEXT("OnStateEntry_%s"), *StateCamel));
			if (BlueprintFunctionNames.Contains(ExactCandidate))
			{
				return ExactCandidate;
			}
		}

		if (!StateKey.IsEmpty())
		{
			for (const FName CandidateName : BlueprintFunctionNames)
			{
				const FString Candidate = CandidateName.ToString();
				if (!Candidate.StartsWith(TEXT("OnStateEntry_"), ESearchCase::CaseSensitive))
				{
					continue;
				}

				const FString CandidateSuffix = Candidate.Mid(13);
				if (NormalizeKey(CandidateSuffix).Equals(StateKey, ESearchCase::CaseSensitive))
				{
					return CandidateName;
				}
			}
		}

		if (!ParsedStateEntryName.IsNone() &&
			ParsedStateEntryName != FName(TEXT("OnStateEntry")) &&
			ParsedStateEntryName != FName(TEXT("OnUpdate")) &&
			BlueprintFunctionNames.Contains(ParsedStateEntryName))
		{
			return ParsedStateEntryName;
		}

		return ParsedStateEntryName;
	};

	TArray<FString> MissingBindings;
	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UAnimStateNode* const StateNode = Cast<UAnimStateNode>(Node);
			if (!StateNode || !StateNode->BoundGraph)
			{
				continue;
			}

			FName StateEntryFunctionName = NAME_None;
			for (UEdGraphNode* BoundNode : StateNode->BoundGraph->Nodes)
			{
				if (UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(BoundNode))
				{
					const FString StateResultNodeText =
						ReadNodePropertyAsText_ImportBpy(StateResultNode, TEXT("Node"));
					StateEntryFunctionName =
						ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
							StateResultNodeText,
							TEXT("StateEntryFunction"));
					if (!StateEntryFunctionName.IsNone())
					{
						break;
					}
				}
			}

			StateEntryFunctionName = ResolveBestEntryFunctionNameForState(StateNode, StateEntryFunctionName);
			if (StateEntryFunctionName.IsNone())
			{
				continue;
			}
			if (StateEntryFunctionName == FName(TEXT("OnStateEntry")) ||
				StateEntryFunctionName == FName(TEXT("OnUpdate")))
			{
				// Exported state-result text can carry placeholder names for transition states.
				// Treat placeholders as non-authoritative and skip hard validation for them.
				continue;
			}

			const FName ActualNotifyName = StateNode->StateEntered.NotifyName;
			const FString RawStateEnteredText =
				ReadNodePropertyAsText_ImportBpy(const_cast<UAnimStateNode*>(StateNode), TEXT("StateEntered"));
			int32 NotifyNameTokenCount = 0;
			{
				int32 SearchFrom = 0;
				while (true)
				{
					const int32 FoundAt = RawStateEnteredText.Find(
						TEXT("NotifyName="),
						ESearchCase::CaseSensitive,
						ESearchDir::FromStart,
						SearchFrom);
					if (FoundAt == INDEX_NONE)
					{
						break;
					}
					++NotifyNameTokenCount;
					SearchFrom = FoundAt + 1;
				}
			}

			if (NotifyNameTokenCount > 1)
			{
				MissingBindings.Add(FString::Printf(
					TEXT("graph=%s state=%s node=%s invalid_state_entered_text=%s"),
					*Graph->GetName(),
					*StateNode->GetName(),
					*DescribeNode_ImportBpy(StateNode),
					*RawStateEnteredText));
				continue;
			}

			const bool bMissingNotify = ActualNotifyName.IsNone();
			const bool bPlaceholderNotify =
				ActualNotifyName == FName(TEXT("OnStateEntry")) ||
				ActualNotifyName == FName(TEXT("OnUpdate"));
			const bool bUnresolvedNotify =
				!bMissingNotify &&
				ResolveSelfContextFunction_ImportBpy(StateNode->BoundGraph, ActualNotifyName.ToString()) == nullptr;
			if (bMissingNotify || bPlaceholderNotify || bUnresolvedNotify)
			{
				MissingBindings.Add(FString::Printf(
					TEXT("graph=%s state=%s node=%s expected_notify=%s actual_notify=%s"),
					*Graph->GetName(),
					*StateNode->GetName(),
					*DescribeNode_ImportBpy(StateNode),
					*StateEntryFunctionName.ToString(),
					ActualNotifyName.IsNone() ? TEXT("None") : *ActualNotifyName.ToString()));
				continue;
			}

			if (ActualNotifyName != StateEntryFunctionName)
			{
				MissingBindings.Add(FString::Printf(
					TEXT("graph=%s state=%s node=%s notify_mismatch expected=%s actual=%s"),
					*Graph->GetName(),
					*StateNode->GetName(),
					*DescribeNode_ImportBpy(StateNode),
					*StateEntryFunctionName.ToString(),
					*ActualNotifyName.ToString()));
			}

			const FGuid ExpectedNotifyGuid = ResolveBlueprintFunctionGuid_ImportBpy(BP, StateEntryFunctionName);
			if (ExpectedNotifyGuid.IsValid() && StateNode->StateEntered.Guid != ExpectedNotifyGuid)
			{
				MissingBindings.Add(FString::Printf(
					TEXT("graph=%s state=%s node=%s notify_guid_mismatch expected=%s/%s actual=%s/%s"),
					*Graph->GetName(),
					*StateNode->GetName(),
					*DescribeNode_ImportBpy(StateNode),
					*StateEntryFunctionName.ToString(),
					*ExpectedNotifyGuid.ToString(EGuidFormats::DigitsWithHyphens),
					ActualNotifyName.IsNone() ? TEXT("None") : *ActualNotifyName.ToString(),
					StateNode->StateEntered.Guid.IsValid() ? *StateNode->StateEntered.Guid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("None")));
			}
		}
	}

	if (MissingBindings.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("State entry bindings missing (%s): %s"),
			StageName ? StageName : TEXT("unknown"),
			*FString::Join(MissingBindings, TEXT("; ")));
		return false;
	}

	return true;
}

bool ValidateAnimBlueprintStateMachineBindingContractAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError)
{
	if (!BP || !Root.IsValid() || !Cast<UAnimBlueprint>(BP))
	{
		return true;
	}

	TSharedPtr<FJsonObject> LiveRoot = UBPDirectExporter::SerializeBlueprintToJson(BP);
	if (!LiveRoot.IsValid())
	{
		OutError = FString::Printf(
			TEXT("State-machine binding validation failed (%s): cannot serialize live blueprint"),
			StageName ? StageName : TEXT("unknown"));
		return false;
	}

	TArray<FString> BindingMismatches;
	CollectStateMachineBindingContractMismatches_ImportBpy(
		Root,
		LiveRoot,
		BindingMismatches);
	if (BindingMismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("State-machine binding validation failed (%s): %s"),
			StageName ? StageName : TEXT("unknown"),
			*FString::Join(BindingMismatches, TEXT("; ")));
		return false;
	}

	return true;
}

bool ValidateAnimBlueprintLinkedAnimLayerContractAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageName,
	FString& OutError)
{
	if (!BP || !Root.IsValid() || !Cast<UAnimBlueprint>(BP))
	{
		return true;
	}

	TSharedPtr<FJsonObject> LiveRoot = UBPDirectExporter::SerializeBlueprintToJson(BP);
	if (!LiveRoot.IsValid())
	{
		OutError = FString::Printf(
			TEXT("LinkedAnimLayer contract validation failed (%s): cannot serialize live blueprint"),
			StageName ? StageName : TEXT("unknown"));
		return false;
	}

	TArray<FString> BindingMismatches;
	CollectLinkedAnimLayerContractMismatches_ImportBpy(
		Root,
		LiveRoot,
		BindingMismatches);
	if (BindingMismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("LinkedAnimLayer contract validation failed (%s): %s"),
			StageName ? StageName : TEXT("unknown"),
			*FString::Join(BindingMismatches, TEXT("; ")));
		return false;
	}

	return true;
}

struct FRoundtripTopologyStats_ImportBpy
{
	int32 RootGraphCount = 0;
	int32 RootGraphNodeCount = 0;
	int32 RootGraphConnectionCount = 0;
	int32 RecursiveGraphCount = 0;
	int32 RecursiveNodeCount = 0;
	int32 RecursiveConnectionCount = 0;
	int32 RecursiveAnimNodeCount = 0;
	int32 FunctionGraphCount = 0;
	int32 InterfaceCount = 0;
	int32 VariableCount = 0;
	int32 TopLevelAnimGraphAnimNodeCount = -1;
	TMap<FString, int32> RootGraphNodeCounts;
	TMap<FString, int32> RootGraphConnectionCounts;
	TMap<FString, int32> RecursiveGraphNodeCountsByGuid;
	TMap<FString, int32> RecursiveGraphConnectionCountsByGuid;
	TMap<FString, int32> RecursiveAnimNodeCountsByGuid;
};

FString NormalizeGuidTextForRoundtrip_ImportBpy(const FString& GuidText)
{
	FGuid ParsedGuid;
	if (TryParseGuid_ImportBpy(GuidText, ParsedGuid))
	{
		return ParsedGuid.ToString(EGuidFormats::DigitsWithHyphens).ToUpper();
	}

	FString Normalized = GuidText;
	Normalized.TrimStartAndEndInline();
	return Normalized.ToUpper();
}

FString GetNormalizedGraphGuidForRoundtrip_ImportBpy(const UEdGraph* Graph)
{
	if (!Graph || !Graph->GraphGuid.IsValid())
	{
		return FString();
	}

	return Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens).ToUpper();
}

bool IsAnimGraphNodeClassName_ImportBpy(const FString& NodeClassName)
{
	return NodeClassName.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive);
}

void GatherReachableGraphsForRoundtripStats_ImportBpy(
	UEdGraph* Graph,
	TSet<UEdGraph*>& VisitedGraphs,
	TArray<UEdGraph*>& OutGraphs)
{
	if (!Graph || VisitedGraphs.Contains(Graph))
	{
		return;
	}

	VisitedGraphs.Add(Graph);
	OutGraphs.Add(Graph);

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
		{
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				CompositeNode->BoundGraph,
				VisitedGraphs,
				OutGraphs);
		}

		if (UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
		{
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				StateMachineNode->EditorStateMachineGraph,
				VisitedGraphs,
				OutGraphs);
		}

		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				StateNode->BoundGraph,
				VisitedGraphs,
				OutGraphs);
		}

		if (UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
		{
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				ConduitNode->BoundGraph,
				VisitedGraphs,
				OutGraphs);
		}

		if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				TransitionNode->BoundGraph,
				VisitedGraphs,
				OutGraphs);
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				TransitionNode->CustomTransitionGraph,
				VisitedGraphs,
				OutGraphs);
		}

		if (UEdGraph* BlendStackGraph = ResolveBlendStackGraph_ImportBpy(Node))
		{
			GatherReachableGraphsForRoundtripStats_ImportBpy(
				BlendStackGraph,
				VisitedGraphs,
				OutGraphs);
		}
	}
}

int32 CountGraphConnections_ImportBpy(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return 0;
	}

	int32 ConnectionCount = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (LinkedNode && LinkedNode->GetGraph() == Graph)
				{
					++ConnectionCount;
				}
			}
		}
	}

	return ConnectionCount;
}

bool AccumulateSerializedRoundtripStatsRecursive_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	FRoundtripTopologyStats_ImportBpy& InOutStats,
	TSet<FString>& VisitedGraphGuids,
	FString& OutError)
{
	if (!GraphObj.IsValid())
	{
		return true;
	}

	FString GraphGuid;
	const bool bHasGraphGuid = GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuid) && !GraphGuid.IsEmpty();
	const FString NormalizedGraphGuid = bHasGraphGuid
		? NormalizeGuidTextForRoundtrip_ImportBpy(GraphGuid)
		: FString();
	if (!NormalizedGraphGuid.IsEmpty())
	{
		if (VisitedGraphGuids.Contains(NormalizedGraphGuid))
		{
			return true;
		}
		VisitedGraphGuids.Add(NormalizedGraphGuid);
	}

	++InOutStats.RecursiveGraphCount;

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	const int32 NodeCount =
		GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) && NodesArr ? NodesArr->Num() : 0;
	InOutStats.RecursiveNodeCount += NodeCount;

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArr = nullptr;
	const int32 ConnectionCount =
		GraphObj->TryGetArrayField(TEXT("connections"), ConnectionsArr) && ConnectionsArr ? ConnectionsArr->Num() : 0;
	InOutStats.RecursiveConnectionCount += ConnectionCount;
	if (!NormalizedGraphGuid.IsEmpty())
	{
		InOutStats.RecursiveGraphNodeCountsByGuid.Add(NormalizedGraphGuid, NodeCount);
		InOutStats.RecursiveGraphConnectionCountsByGuid.Add(NormalizedGraphGuid, ConnectionCount);
	}

	if (!NodesArr || NodesArr->Num() == 0)
	{
		return true;
	}

	static const TCHAR* NestedGraphFields[] = {
		TEXT("BoundGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};
	int32 GraphAnimNodeCount = 0;

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClassName;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
		if (IsAnimGraphNodeClassName_ImportBpy(NodeClassName))
		{
			++GraphAnimNodeCount;
			++InOutStats.RecursiveAnimNodeCount;
		}

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		if (!NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
		{
			continue;
		}

		for (const TCHAR* FieldName : NestedGraphFields)
		{
			FString NestedGraphJson;
			if (!(*NodePropsObj)->TryGetStringField(FieldName, NestedGraphJson) || NestedGraphJson.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphObj;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NestedGraphJson);
			if (!FJsonSerializer::Deserialize(Reader, NestedGraphObj) || !NestedGraphObj.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Failed to parse nested graph JSON for field '%s' while computing roundtrip stats"),
					FieldName);
				return false;
			}

			if (!AccumulateSerializedRoundtripStatsRecursive_ImportBpy(
					NestedGraphObj,
					InOutStats,
					VisitedGraphGuids,
					OutError))
			{
				return false;
			}
		}
	}

	if (!NormalizedGraphGuid.IsEmpty())
	{
		InOutStats.RecursiveAnimNodeCountsByGuid.Add(NormalizedGraphGuid, GraphAnimNodeCount);
	}

	return true;
}

bool CollectSerializedRoundtripStatsFromRootJson_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	FRoundtripTopologyStats_ImportBpy& OutStats,
	FString& OutError)
{
	OutStats = FRoundtripTopologyStats_ImportBpy{};
	if (!Root.IsValid())
	{
		OutError = TEXT("Invalid root json while collecting serialized roundtrip stats");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* VariablesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("variables"), VariablesArr) && VariablesArr)
	{
		OutStats.VariableCount = VariablesArr->Num();
	}

	const TArray<TSharedPtr<FJsonValue>>* InterfacesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("interfaces"), InterfacesArr) && InterfacesArr)
	{
		OutStats.InterfaceCount = InterfacesArr->Num();
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return true;
	}

	TSet<FString> VisitedGraphGuids;
	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		++OutStats.RootGraphCount;

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		const int32 RootNodeCount =
			GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) && NodesArr ? NodesArr->Num() : 0;
		OutStats.RootGraphNodeCount += RootNodeCount;

		const TArray<TSharedPtr<FJsonValue>>* ConnectionsArr = nullptr;
		const int32 RootConnectionCount =
			GraphObj->TryGetArrayField(TEXT("connections"), ConnectionsArr) && ConnectionsArr ? ConnectionsArr->Num() : 0;
		OutStats.RootGraphConnectionCount += RootConnectionCount;

		FString GraphName;
		GraphObj->TryGetStringField(TEXT("name"), GraphName);
		if (!GraphName.IsEmpty())
		{
			OutStats.RootGraphNodeCounts.Add(GraphName, RootNodeCount);
			OutStats.RootGraphConnectionCounts.Add(GraphName, RootConnectionCount);
		}

		FString GraphType;
		GraphObj->TryGetStringField(TEXT("graph_type"), GraphType);
		if (GraphType.Equals(TEXT("function"), ESearchCase::IgnoreCase))
		{
			++OutStats.FunctionGraphCount;
		}

		if (GraphName.Equals(UEdGraphSchema_K2::GN_AnimGraph.ToString(), ESearchCase::CaseSensitive) && NodesArr)
		{
			int32 AnimNodeCount = 0;
			for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
			{
				const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
				if (!NodeObj.IsValid())
				{
					continue;
				}

				FString NodeClass;
				NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);
				if (NodeClass.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
				{
					++AnimNodeCount;
				}
			}
			OutStats.TopLevelAnimGraphAnimNodeCount = AnimNodeCount;
		}

		if (!AccumulateSerializedRoundtripStatsRecursive_ImportBpy(
				GraphObj,
				OutStats,
				VisitedGraphGuids,
				OutError))
		{
			return false;
		}
	}

	return true;
}

void CollectActualRoundtripStatsFromBlueprint_ImportBpy(
	UBlueprint* BP,
	FRoundtripTopologyStats_ImportBpy& OutStats)
{
	OutStats = FRoundtripTopologyStats_ImportBpy{};
	if (!BP)
	{
		return;
	}

	TMap<FString, UEdGraph*> RootGraphsByName;
	auto RegisterRootGraph = [&RootGraphsByName](UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}
		const FString GraphName = Graph->GetName();
		if (!GraphName.IsEmpty() && !RootGraphsByName.Contains(GraphName))
		{
			RootGraphsByName.Add(GraphName, Graph);
		}
	};

	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		RegisterRootGraph(Graph);
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		RegisterRootGraph(Graph);
	}
	for (UEdGraph* Graph : BP->MacroGraphs)
	{
		RegisterRootGraph(Graph);
	}
	for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			RegisterRootGraph(Graph);
		}
	}

	OutStats.RootGraphCount = RootGraphsByName.Num();
	for (const TPair<FString, UEdGraph*>& Pair : RootGraphsByName)
	{
		const UEdGraph* Graph = Pair.Value;
		const int32 NodeCount = Graph ? Graph->Nodes.Num() : 0;
		const int32 ConnectionCount = CountGraphConnections_ImportBpy(Graph);
		OutStats.RootGraphNodeCount += NodeCount;
		OutStats.RootGraphConnectionCount += ConnectionCount;
		OutStats.RootGraphNodeCounts.Add(Pair.Key, NodeCount);
		OutStats.RootGraphConnectionCounts.Add(Pair.Key, ConnectionCount);
	}

	int32 ActualFunctionGraphCount = BP->FunctionGraphs.Num();
	{
		TSet<const UEdGraph*> CountedFunctionGraphs;
		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (Graph)
			{
				CountedFunctionGraphs.Add(Graph);
			}
		}

		for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
		{
			for (UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				if (Graph && !CountedFunctionGraphs.Contains(Graph))
				{
					CountedFunctionGraphs.Add(Graph);
					++ActualFunctionGraphCount;
				}
			}
		}
	}
	OutStats.FunctionGraphCount = ActualFunctionGraphCount;
	OutStats.InterfaceCount = BP->ImplementedInterfaces.Num();
	OutStats.VariableCount = BP->NewVariables.Num();

	TArray<UEdGraph*> RootGraphs;
	RootGraphsByName.GenerateValueArray(RootGraphs);
	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphsForRoundtripStats_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	OutStats.RecursiveGraphCount = ReachableGraphs.Num();
	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		const int32 NodeCount = Graph->Nodes.Num();
		const int32 ConnectionCount = CountGraphConnections_ImportBpy(Graph);
		OutStats.RecursiveNodeCount += NodeCount;
		OutStats.RecursiveConnectionCount += ConnectionCount;
		int32 GraphAnimNodeCount = 0;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !Node->GetClass())
			{
				continue;
			}

			if (IsAnimGraphNodeClassName_ImportBpy(Node->GetClass()->GetName()))
			{
				++GraphAnimNodeCount;
				++OutStats.RecursiveAnimNodeCount;
			}
		}

		const FString GraphGuid = GetNormalizedGraphGuidForRoundtrip_ImportBpy(Graph);
		if (!GraphGuid.IsEmpty())
		{
			OutStats.RecursiveGraphNodeCountsByGuid.Add(GraphGuid, NodeCount);
			OutStats.RecursiveGraphConnectionCountsByGuid.Add(GraphGuid, ConnectionCount);
			OutStats.RecursiveAnimNodeCountsByGuid.Add(GraphGuid, GraphAnimNodeCount);
		}
	}

	if (UEdGraph* AnimGraph = FindRootGraphByName_ImportBpy(BP, UEdGraphSchema_K2::GN_AnimGraph.ToString()))
	{
		int32 AnimNodeCount = 0;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (Node && Node->GetClass() &&
				Node->GetClass()->GetName().StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
			{
				++AnimNodeCount;
			}
		}
		OutStats.TopLevelAnimGraphAnimNodeCount = AnimNodeCount;
	}
}

FString HashTextSha1_ImportBpy(const FString& InputText)
{
	FTCHARToUTF8 Utf8(*InputText);
	uint8 Digest[FSHA1::DigestSize];
	FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
	return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
}

struct FCanonicalizationState_ImportBpy
{
	FString SelfBlueprintPath;
	FString SelfBlueprintGeneratedClassPath;
	TMap<FString, FString> GuidTokenMap;
	int32 NextGuidTokenIndex = 0;
};

FString NormalizeCanonicalStringTokens_ImportBpy(
	const FString& InputText,
	FCanonicalizationState_ImportBpy& State)
{
	FString Result = InputText;
	if (!State.SelfBlueprintGeneratedClassPath.IsEmpty())
	{
		Result = Result.Replace(
			*State.SelfBlueprintGeneratedClassPath,
			TEXT("__SELF_BP_CLASS__"),
			ESearchCase::CaseSensitive);
	}
	if (!State.SelfBlueprintPath.IsEmpty())
	{
		Result = Result.Replace(
			*State.SelfBlueprintPath,
			TEXT("__SELF_BP__"),
			ESearchCase::CaseSensitive);
	}

	static const FRegexPattern GuidPattern(
		TEXT("(?i)([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}|[0-9a-f]{32})"));

	FRegexMatcher Matcher(GuidPattern, Result);
	FString Normalized;
	int32 LastIndex = 0;

	while (Matcher.FindNext())
	{
		const int32 MatchStart = Matcher.GetMatchBeginning();
		const int32 MatchEnd = Matcher.GetMatchEnding();
		if (MatchStart > LastIndex)
		{
			Normalized += Result.Mid(LastIndex, MatchStart - LastIndex);
		}

		const FString MatchedGuid = Result.Mid(MatchStart, MatchEnd - MatchStart).ToLower();
		FString& Token = State.GuidTokenMap.FindOrAdd(MatchedGuid);
		if (Token.IsEmpty())
		{
			Token = FString::Printf(TEXT("__GUID_%d__"), State.NextGuidTokenIndex++);
		}
		Normalized += Token;
		LastIndex = MatchEnd;
	}

	if (LastIndex < Result.Len())
	{
		Normalized += Result.Mid(LastIndex);
	}

	return Normalized;
}

FString EscapeCanonicalJsonString_ImportBpy(const FString& InputText)
{
	FString Escaped;
	Escaped.Reserve(InputText.Len() + 16);
	for (const TCHAR Ch : InputText)
	{
		switch (Ch)
		{
		case TEXT('\\'):
			Escaped += TEXT("\\\\");
			break;
		case TEXT('\"'):
			Escaped += TEXT("\\\"");
			break;
		case TEXT('\n'):
			Escaped += TEXT("\\n");
			break;
		case TEXT('\r'):
			Escaped += TEXT("\\r");
			break;
		case TEXT('\t'):
			Escaped += TEXT("\\t");
			break;
		default:
			Escaped.AppendChar(Ch);
			break;
		}
	}
	return Escaped;
}

bool ShouldIgnoreCanonicalFieldKey_ImportBpy(const FString& Key)
{
	// Connection pin metadata can differ across import/export passes without
	// affecting graph semantics; compare by logical pin names instead.
	return Key.Equals(TEXT("src_pin_id"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("dst_pin_id"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("src_pin_full"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("dst_pin_full"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("connections"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("graph_outer"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("graph_outer_node"), ESearchCase::CaseSensitive) ||
		Key.Equals(TEXT("metadata"), ESearchCase::CaseSensitive);
}

bool ShouldIgnoreCanonicalArrayElement_ImportBpy(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Obj = Value->AsObject();
	if (!Obj.IsValid())
	{
		return false;
	}

	FString SrcPin;
	FString DstPin;
	if (!Obj->TryGetStringField(TEXT("src_pin"), SrcPin) ||
		!Obj->TryGetStringField(TEXT("dst_pin"), DstPin))
	{
		return false;
	}

	// Exec-only flow links are editor-generated/normalized differently across
	// import/export passes. Structural validators already cover required graph
	// contracts, so exclude these from byte-for-byte canonical comparison.
	return SrcPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase) &&
		DstPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase);
}

void AppendCanonicalJsonValue_ImportBpy(
	const TSharedPtr<FJsonValue>& Value,
	FString& OutText,
	FCanonicalizationState_ImportBpy& State)
{
	if (!Value.IsValid() || Value->Type == EJson::None || Value->Type == EJson::Null)
	{
		OutText += TEXT("null");
		return;
	}

	switch (Value->Type)
	{
	case EJson::String:
	{
		const FString Normalized = NormalizeCanonicalStringTokens_ImportBpy(Value->AsString(), State);
		OutText += TEXT("\"");
		OutText += EscapeCanonicalJsonString_ImportBpy(Normalized);
		OutText += TEXT("\"");
		break;
	}
	case EJson::Number:
		OutText += LexToString(Value->AsNumber());
		break;
	case EJson::Boolean:
		OutText += Value->AsBool() ? TEXT("true") : TEXT("false");
		break;
	case EJson::Array:
	{
		OutText += TEXT("[");
		const TArray<TSharedPtr<FJsonValue>>& ArrayValues = Value->AsArray();
		bool bFirstArrayItem = true;
		for (int32 Index = 0; Index < ArrayValues.Num(); ++Index)
		{
			if (ShouldIgnoreCanonicalArrayElement_ImportBpy(ArrayValues[Index]))
			{
				continue;
			}

			if (!bFirstArrayItem)
			{
				OutText += TEXT(",");
			}
			bFirstArrayItem = false;
			AppendCanonicalJsonValue_ImportBpy(ArrayValues[Index], OutText, State);
		}
		OutText += TEXT("]");
		break;
	}
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
		if (!ObjectValue.IsValid())
		{
			OutText += TEXT("{}");
			break;
		}

		TArray<FString> Keys;
		ObjectValue->Values.GenerateKeyArray(Keys);
		Keys.Sort();

		OutText += TEXT("{");
		bool bFirst = true;
		for (const FString& Key : Keys)
		{
			if (ShouldIgnoreCanonicalFieldKey_ImportBpy(Key))
			{
				continue;
			}

			const TSharedPtr<FJsonValue>* FieldValue = ObjectValue->Values.Find(Key);
			if (!FieldValue)
			{
				continue;
			}

			if (!bFirst)
			{
				OutText += TEXT(",");
			}
			bFirst = false;

			const FString NormalizedKey = NormalizeCanonicalStringTokens_ImportBpy(Key, State);
			OutText += TEXT("\"");
			OutText += EscapeCanonicalJsonString_ImportBpy(NormalizedKey);
			OutText += TEXT("\":");
			AppendCanonicalJsonValue_ImportBpy(*FieldValue, OutText, State);
		}
		OutText += TEXT("}");
		break;
	}
	default:
		OutText += TEXT("null");
		break;
	}
}

FString BuildCanonicalJsonString_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	const FString& SelfBlueprintPath)
{
	FCanonicalizationState_ImportBpy State;
	if (!SelfBlueprintPath.IsEmpty())
	{
		State.SelfBlueprintPath = NormalizeBlueprintObjectPath_ImportBpy(SelfBlueprintPath);
		State.SelfBlueprintGeneratedClassPath = BuildGeneratedClassObjectPathFromBlueprintPath_ImportBpy(SelfBlueprintPath);
	}

	FString Canonical;
	AppendCanonicalJsonValue_ImportBpy(MakeShared<FJsonValueObject>(Root), Canonical, State);
	return Canonical;
}

FString BuildClassDefaultsHashFromRootJson_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	int32& OutClassDefaultsCount)
{
	OutClassDefaultsCount = 0;
	if (!Root.IsValid())
	{
		return HashTextSha1_ImportBpy(TEXT(""));
	}

	const TArray<TSharedPtr<FJsonValue>>* ClassDefaultsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("class_defaults"), ClassDefaultsArr) || !ClassDefaultsArr)
	{
		return HashTextSha1_ImportBpy(TEXT(""));
	}

	TArray<FString> CanonicalEntries;
	for (const TSharedPtr<FJsonValue>& EntryValue : *ClassDefaultsArr)
	{
		const TSharedPtr<FJsonObject> EntryObj = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		if (!EntryObj.IsValid())
		{
			continue;
		}

		FString PropertyName;
		if (!EntryObj->TryGetStringField(TEXT("name"), PropertyName) || PropertyName.IsEmpty())
		{
			continue;
		}

		const TSharedPtr<FJsonValue>* ValueField = EntryObj->Values.Find(TEXT("value"));
		if (!ValueField || !ValueField->IsValid())
		{
			continue;
		}

		FCanonicalizationState_ImportBpy LocalState;
		FString CanonicalValue;
		AppendCanonicalJsonValue_ImportBpy(*ValueField, CanonicalValue, LocalState);
		CanonicalEntries.Add(FString::Printf(TEXT("%s=%s"), *PropertyName, *CanonicalValue));
	}

	CanonicalEntries.Sort();
	OutClassDefaultsCount = CanonicalEntries.Num();
	return HashTextSha1_ImportBpy(FString::Join(CanonicalEntries, TEXT("\n")));
}

FName ExtractFunctionNameFromMemberReferenceText_ImportBpy(const FString& SerializedText)
{
	if (SerializedText.IsEmpty())
	{
		return NAME_None;
	}

	static const TCHAR* Needles[] = {
		TEXT("MemberName=\""),
		TEXT("FunctionName=\"")
	};

	for (const TCHAR* Needle : Needles)
	{
		const int32 NeedlePos = SerializedText.Find(Needle, ESearchCase::CaseSensitive);
		if (NeedlePos == INDEX_NONE)
		{
			continue;
		}

		const int32 ValueStart = NeedlePos + FCString::Strlen(Needle);
		const int32 ValueEnd = SerializedText.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			ValueStart);
		if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
		{
			continue;
		}

		const FString FunctionName =
			SerializedText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
		if (!FunctionName.IsEmpty() && !FunctionName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return FName(*FunctionName);
		}
	}

	return NAME_None;
}

FString GetSerializedNodePropStringFromNodeJson_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	const TCHAR* PropertyName)
{
	if (!NodeJson.IsValid() || !PropertyName)
	{
		return FString();
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
	{
		return FString();
	}

	FString Value;
	(*NodePropsObj)->TryGetStringField(PropertyName, Value);
	return Value;
}

void ExtractBindingPropertyKeysFromSerializedText_ImportBpy(
	const FString& SerializedBindings,
	TSet<FString>& OutKeys)
{
	OutKeys.Reset();
	if (SerializedBindings.IsEmpty() || SerializedBindings == TEXT("()"))
	{
		return;
	}

	// Preferred map export form:
	// (("BlendTime", (...)),("NotifyRecencyTimeOut", (...)))
	int32 EntryMarkerPos = SerializedBindings.Find(TEXT("((\""), ESearchCase::CaseSensitive);
	if (EntryMarkerPos != INDEX_NONE)
	{
		int32 KeyStart = EntryMarkerPos + 3;
		while (KeyStart >= 0 && KeyStart < SerializedBindings.Len())
		{
			const int32 KeyEnd = SerializedBindings.Find(
				TEXT("\""),
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart,
				KeyStart);
			if (KeyEnd == INDEX_NONE || KeyEnd <= KeyStart)
			{
				break;
			}

			const FString Key = SerializedBindings.Mid(KeyStart, KeyEnd - KeyStart).TrimStartAndEnd();
			if (!Key.IsEmpty())
			{
				OutKeys.Add(Key);
			}

			const int32 NextMarkerPos = SerializedBindings.Find(
				TEXT("),(\""),
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart,
				KeyEnd);
			if (NextMarkerPos == INDEX_NONE)
			{
				break;
			}
			KeyStart = NextMarkerPos + 4;
		}

		if (OutKeys.Num() > 0)
		{
			return;
		}
	}

	// Legacy fallback where keys must be inferred from struct payload fields.
	static const FString PropertyNameToken = TEXT("PropertyName=\"");
	int32 SearchFrom = 0;
	while (SearchFrom < SerializedBindings.Len())
	{
		const int32 TokenIndex = SerializedBindings.Find(
			PropertyNameToken,
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			SearchFrom);
		if (TokenIndex == INDEX_NONE)
		{
			break;
		}

		const int32 NameStart = TokenIndex + PropertyNameToken.Len();
		const int32 NameEnd = SerializedBindings.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			NameStart);
		if (NameEnd == INDEX_NONE || NameEnd <= NameStart)
		{
			break;
		}

		const FString PropertyName = SerializedBindings.Mid(NameStart, NameEnd - NameStart).TrimStartAndEnd();
		if (!PropertyName.IsEmpty())
		{
			OutKeys.Add(PropertyName);
		}
		SearchFrom = NameEnd + 1;
	}
}

struct FAnimNodeBindingDescriptor_ImportBpy
{
	FString PathAsText;
	FString TypeName;
	bool bIsBound = false;
	bool bRequiresThreadSafe = false;
};

bool ExtractBindingEntryBodyByKeyFromSerializedText_ImportBpy(
	const FString& SerializedBindings,
	const FString& Key,
	FString& OutBody)
{
	OutBody.Reset();
	if (SerializedBindings.IsEmpty() || Key.IsEmpty())
	{
		return false;
	}

	const FString KeyToken = FString::Printf(TEXT("(\"%s\", ("), *Key);
	const int32 EntryPos = SerializedBindings.Find(KeyToken, ESearchCase::CaseSensitive);
	if (EntryPos == INDEX_NONE)
	{
		return false;
	}

	const int32 BodyStart = EntryPos + KeyToken.Len();
	const int32 Len = SerializedBindings.Len();
	int32 Depth = 1;
	bool bInQuotes = false;
	bool bEscape = false;

	for (int32 Index = BodyStart; Index < Len; ++Index)
	{
		const TCHAR Ch = SerializedBindings[Index];
		if (bEscape)
		{
			bEscape = false;
			continue;
		}

		if (bInQuotes && Ch == TEXT('\\'))
		{
			bEscape = true;
			continue;
		}

		if (Ch == TEXT('"'))
		{
			bInQuotes = !bInQuotes;
			continue;
		}

		if (bInQuotes)
		{
			continue;
		}

		if (Ch == TEXT('('))
		{
			++Depth;
		}
		else if (Ch == TEXT(')'))
		{
			--Depth;
			if (Depth == 0)
			{
				OutBody = SerializedBindings.Mid(BodyStart, Index - BodyStart);
				return true;
			}
		}
	}

	return false;
}

FString ExtractQuotedFieldValueFromBindingBody_ImportBpy(
	const FString& EntryBody,
	const TCHAR* FieldToken)
{
	if (!FieldToken || EntryBody.IsEmpty())
	{
		return FString();
	}

	const int32 TokenPos = EntryBody.Find(FieldToken, ESearchCase::CaseSensitive);
	if (TokenPos == INDEX_NONE)
	{
		return FString();
	}

	const int32 ValueStart = TokenPos + FCString::Strlen(FieldToken);
	const int32 ValueEnd = EntryBody.Find(
		TEXT("\""),
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart,
		ValueStart);
	if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
	{
		return FString();
	}

	return EntryBody.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
}

void CollectExpectedAnimNodeBindingDescriptorsFromSerializedText_ImportBpy(
	const FString& SerializedBindings,
	TMap<FString, FAnimNodeBindingDescriptor_ImportBpy>& OutDescriptors)
{
	OutDescriptors.Reset();
	TSet<FString> Keys;
	ExtractBindingPropertyKeysFromSerializedText_ImportBpy(SerializedBindings, Keys);
	for (const FString& Key : Keys)
	{
		FString EntryBody;
		if (!ExtractBindingEntryBodyByKeyFromSerializedText_ImportBpy(SerializedBindings, Key, EntryBody))
		{
			continue;
		}

		FAnimNodeBindingDescriptor_ImportBpy Descriptor;
		Descriptor.PathAsText =
			ExtractQuotedFieldValueFromBindingBody_ImportBpy(EntryBody, TEXT("PathAsText=\""));
		if (EntryBody.Contains(TEXT("Type=Function"), ESearchCase::CaseSensitive))
		{
			Descriptor.TypeName = TEXT("Function");
		}
		else if (EntryBody.Contains(TEXT("Type=Property"), ESearchCase::CaseSensitive))
		{
			Descriptor.TypeName = TEXT("Property");
		}
		Descriptor.bIsBound =
			EntryBody.Contains(TEXT("bIsBound=True"), ESearchCase::CaseSensitive) ||
			EntryBody.Contains(TEXT("bIsBound=true"), ESearchCase::CaseSensitive);
		Descriptor.bRequiresThreadSafe =
			EntryBody.Contains(TEXT("WorkerThread"), ESearchCase::IgnoreCase);

		OutDescriptors.Add(Key, Descriptor);
	}
}

bool CollectAnimNodeBindingPropertyKeysFromLiveNode_ImportBpy(
	UEdGraphNode* Node,
	TSet<FString>& OutKeys,
	FString& OutError)
{
	OutKeys.Reset();
	if (!Node)
	{
		OutError = TEXT("binding key collection failed: node is null");
		return false;
	}

	auto CollectKeysFromMap = [&OutKeys](FMapProperty* MapProperty, void* ValuePtr) -> bool
	{
		if (!MapProperty || !ValuePtr)
		{
			return false;
		}

		const FNameProperty* NameKeyProperty = CastField<FNameProperty>(MapProperty->KeyProp);
		if (!NameKeyProperty)
		{
			return false;
		}

		FScriptMapHelper MapHelper(MapProperty, ValuePtr);
		for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
		{
			if (!MapHelper.IsValidIndex(Index))
			{
				continue;
			}

			const FName KeyName = NameKeyProperty->GetPropertyValue(MapHelper.GetKeyPtr(Index));
			if (!KeyName.IsNone())
			{
				OutKeys.Add(KeyName.ToString());
			}
		}
		return true;
	};

	// Prefer Binding subobject map (UE5.7+).
	FObjectPropertyBase* BindingProperty =
		FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("Binding"));
	if (BindingProperty)
	{
		UObject* BindingObject = BindingProperty->GetObjectPropertyValue_InContainer(Node);
		if (!BindingObject)
		{
			return true;
		}

		FMapProperty* PropertyBindingsProperty =
			FindFProperty<FMapProperty>(BindingObject->GetClass(), TEXT("PropertyBindings"));
		if (!PropertyBindingsProperty)
		{
			OutError = FString::Printf(
				TEXT("binding key collection failed: Binding object %s has no PropertyBindings map"),
				*GetPathNameSafe(BindingObject));
			return false;
		}

		void* ValuePtr = PropertyBindingsProperty->ContainerPtrToValuePtr<void>(BindingObject);
		if (!ValuePtr)
		{
			OutError = FString::Printf(
				TEXT("binding key collection failed: cannot access Binding.PropertyBindings on node %s"),
				*DescribeNode_ImportBpy(Node));
			return false;
		}

		if (!CollectKeysFromMap(PropertyBindingsProperty, ValuePtr))
		{
			OutError = FString::Printf(
				TEXT("binding key collection failed: invalid Binding.PropertyBindings map schema on node %s"),
				*DescribeNode_ImportBpy(Node));
			return false;
		}

		return true;
	}

	// Fallback for branches where map still lives on the node.
	if (FMapProperty* DirectBindingsProperty =
			FindFProperty<FMapProperty>(Node->GetClass(), TEXT("PropertyBindings")))
	{
		void* ValuePtr = DirectBindingsProperty->ContainerPtrToValuePtr<void>(Node);
		if (!ValuePtr)
		{
			OutError = FString::Printf(
				TEXT("binding key collection failed: cannot access node.PropertyBindings on %s"),
				*DescribeNode_ImportBpy(Node));
			return false;
		}

		if (!CollectKeysFromMap(DirectBindingsProperty, ValuePtr))
		{
			OutError = FString::Printf(
				TEXT("binding key collection failed: invalid node.PropertyBindings map schema on %s"),
				*DescribeNode_ImportBpy(Node));
			return false;
		}
		return true;
	}

	// Node has no binding container at all; treat as empty.
	return true;
}

bool CollectAnimNodeBindingDescriptorsFromLiveNode_ImportBpy(
	UEdGraphNode* Node,
	TMap<FString, FAnimNodeBindingDescriptor_ImportBpy>& OutDescriptors,
	FString& OutError)
{
	OutDescriptors.Reset();
	if (!Node)
	{
		OutError = TEXT("binding descriptor collection failed: node is null");
		return false;
	}

	FString SerializedBindings;
	auto ExportMapText = [&SerializedBindings](
							 FMapProperty* MapProperty,
							 void* ValuePtr,
							 UObject* OwnerObject) -> bool
	{
		if (!MapProperty || !ValuePtr)
		{
			return true;
		}

		SerializedBindings.Reset();
		MapProperty->ExportTextItem_Direct(SerializedBindings, ValuePtr, nullptr, OwnerObject, PPF_None);
		SerializedBindings.TrimStartAndEndInline();
		if (SerializedBindings.IsEmpty())
		{
			SerializedBindings = TEXT("()");
		}
		return true;
	};

	// Prefer Binding subobject map (UE5.7+).
	FObjectPropertyBase* BindingProperty =
		FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("Binding"));
	if (BindingProperty)
	{
		UObject* BindingObject = BindingProperty->GetObjectPropertyValue_InContainer(Node);
		if (IsValid(BindingObject))
		{
			FMapProperty* PropertyBindingsProperty =
				FindFProperty<FMapProperty>(BindingObject->GetClass(), TEXT("PropertyBindings"));
			if (!PropertyBindingsProperty)
			{
				OutError = FString::Printf(
					TEXT("binding descriptor collection failed: Binding object %s has no PropertyBindings map"),
					*GetPathNameSafe(BindingObject));
				return false;
			}

			void* ValuePtr = PropertyBindingsProperty->ContainerPtrToValuePtr<void>(BindingObject);
			if (!ExportMapText(PropertyBindingsProperty, ValuePtr, BindingObject))
			{
				OutError = FString::Printf(
					TEXT("binding descriptor collection failed: cannot export Binding.PropertyBindings on %s"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}
		}
	}
	else if (FMapProperty* DirectBindingsProperty =
				 FindFProperty<FMapProperty>(Node->GetClass(), TEXT("PropertyBindings")))
	{
		// Fallback for branches where map still lives directly on node.
		void* ValuePtr = DirectBindingsProperty->ContainerPtrToValuePtr<void>(Node);
		if (!ExportMapText(DirectBindingsProperty, ValuePtr, Node))
		{
			OutError = FString::Printf(
				TEXT("binding descriptor collection failed: cannot export node.PropertyBindings on %s"),
				*DescribeNode_ImportBpy(Node));
			return false;
		}
	}

	if (!SerializedBindings.IsEmpty() && SerializedBindings != TEXT("()"))
	{
		CollectExpectedAnimNodeBindingDescriptorsFromSerializedText_ImportBpy(
			SerializedBindings,
			OutDescriptors);
	}

	return true;
}

void LogMotionMatchingBindingMapSnapshot_ImportBpy(
	UEdGraphNode* Node,
	const TCHAR* Phase,
	const FString* SourceBindingsText)
{
	if (!Node || !Node->GetClass())
	{
		return;
	}

	if (!Node->GetClass()->GetName().Contains(TEXT("AnimGraphNode_MotionMatching"), ESearchCase::CaseSensitive))
	{
		return;
	}

	TMap<FString, FAnimNodeBindingDescriptor_ImportBpy> Descriptors;
	FString CollectError;
	if (!CollectAnimNodeBindingDescriptorsFromLiveNode_ImportBpy(Node, Descriptors, CollectError))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][MotionBinding][%s] graph=%s node=%s collect_failed=%s"),
			Phase ? Phase : TEXT("unknown"),
			Node->GetGraph() ? *Node->GetGraph()->GetName() : TEXT("<null>"),
			*DescribeNode_ImportBpy(Node),
			*CollectError);
		return;
	}

	TArray<FString> Keys;
	Descriptors.GetKeys(Keys);
	Keys.Sort();

	TArray<FString> DetailParts;
	for (const FString& Key : Keys)
	{
		const FAnimNodeBindingDescriptor_ImportBpy* Descriptor = Descriptors.Find(Key);
		if (!Descriptor)
		{
			continue;
		}

		DetailParts.Add(FString::Printf(
			TEXT("%s{type=%s,path=%s,bound=%d}"),
			*Key,
			Descriptor->TypeName.IsEmpty() ? TEXT("<none>") : *Descriptor->TypeName,
			Descriptor->PathAsText.IsEmpty() ? TEXT("<none>") : *Descriptor->PathAsText,
			Descriptor->bIsBound ? 1 : 0));
	}

	FString SourceSnippet;
	if (SourceBindingsText)
	{
		SourceSnippet = *SourceBindingsText;
		SourceSnippet.ReplaceInline(TEXT("\r"), TEXT(" "));
		SourceSnippet.ReplaceInline(TEXT("\n"), TEXT(" "));
		SourceSnippet.TrimStartAndEndInline();
		if (SourceSnippet.Len() > 220)
		{
			SourceSnippet = SourceSnippet.Left(220) + TEXT("...");
		}
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag][MotionBinding][%s] graph=%s node=%s entries=%d details=%s source=%s"),
		Phase ? Phase : TEXT("unknown"),
		Node->GetGraph() ? *Node->GetGraph()->GetName() : TEXT("<null>"),
		*DescribeNode_ImportBpy(Node),
		Descriptors.Num(),
		DetailParts.Num() > 0 ? *FString::Join(DetailParts, TEXT("; ")) : TEXT("<empty>"),
		SourceSnippet.IsEmpty() ? TEXT("<none>") : *SourceSnippet);
}

void LogMotionMatchingBindingSnapshotsForBlueprint_ImportBpy(
	UBlueprint* BP,
	const TCHAR* Phase)
{
	if (!BP)
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);
	for (UEdGraph* Graph : RootGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			LogMotionMatchingBindingMapSnapshot_ImportBpy(Node, Phase, nullptr);
		}
	}
}

void CollectSerializedAnimNodeJsonByUidFromRootJson_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	TMap<FString, TSharedPtr<FJsonObject>>& OutNodeByUid)
{
	OutNodeByUid.Reset();
	if (!Root.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return;
	}

	TSet<FString> VisitedGraphGuids;
	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		CollectSerializedAnimNodeJsonByUidRecursive_ImportBpy(
			GraphObj,
			VisitedGraphGuids,
			OutNodeByUid);
	}
}

FString BuildSerializedAnimNodeDiagnosticSummary_ImportBpy(const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!NodeJson.IsValid())
	{
		return TEXT("node_json=<invalid>");
	}

	FString NodeClassName;
	NodeJson->TryGetStringField(TEXT("node_class"), NodeClassName);

	TArray<FString> Parts;
	Parts.Add(FString::Printf(
		TEXT("class=%s"),
		NodeClassName.IsEmpty() ? TEXT("<unknown>") : *NodeClassName));

	const FString SerializedUid = GetSerializedAnimNodeUid_ImportBpy(NodeJson);
	if (!SerializedUid.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("uid=%s"), *SerializedUid));
	}

	FString SerializedNodeGuid;
	if (NodeJson->TryGetStringField(TEXT("node_guid"), SerializedNodeGuid) && !SerializedNodeGuid.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("node_guid=%s"), *SerializedNodeGuid));
	}

	if (!SerializedUid.IsEmpty() || !SerializedNodeGuid.IsEmpty())
	{
		Parts.Add(FString::Printf(
			TEXT("uid_eq_node_guid=%d"),
			DoSerializedAnimNodeUidAndGuidMatch_ImportBpy(SerializedUid, SerializedNodeGuid) ? 1 : 0));
	}

	FString ReadableName;
	if (NodeJson->TryGetStringField(TEXT("readable_name"), ReadableName) && !ReadableName.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("readable_name=%s"), *ReadableName));
	}

	FString MemberName;
	if (NodeJson->TryGetStringField(TEXT("member_name"), MemberName) && !MemberName.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("member_name=%s"), *MemberName));
	}

	const FString BindingText =
		GetSerializedNodePropStringFromNodeJson_ImportBpy(NodeJson, TEXT("BindingPropertyBindings"));
	if (!BindingText.IsEmpty() && !BindingText.Equals(TEXT("()"), ESearchCase::CaseSensitive))
	{
		TSet<FString> BindingKeys;
		ExtractBindingPropertyKeysFromSerializedText_ImportBpy(BindingText, BindingKeys);
		TArray<FString> SortedKeys = BindingKeys.Array();
		SortedKeys.Sort();
		if (SortedKeys.Num() > 8)
		{
			SortedKeys.SetNum(8);
		}

		Parts.Add(FString::Printf(
			TEXT("binding_keys=%s"),
			SortedKeys.Num() > 0 ? *FString::Join(SortedKeys, TEXT(",")) : TEXT("<parse_failed>")));
	}

	static const TCHAR* FunctionRefFieldNames[] = {
		TEXT("InitialUpdateFunction"),
		TEXT("BecomeRelevantFunction"),
		TEXT("UpdateFunction"),
		TEXT("OnMotionMatchingStateUpdatedFunction")
	};

	for (const TCHAR* FieldName : FunctionRefFieldNames)
	{
		const FString RefText = GetSerializedNodePropStringFromNodeJson_ImportBpy(NodeJson, FieldName);
		if (RefText.IsEmpty() || RefText.Equals(TEXT("()"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		const FName FunctionName = ExtractFunctionNameFromMemberReferenceText_ImportBpy(RefText);
		Parts.Add(FString::Printf(
			TEXT("%s=%s"),
			FieldName,
			FunctionName.IsNone() ? *RefText.Left(96) : *FunctionName.ToString()));
	}

	return FString::Join(Parts, TEXT(" "));
}

void LogImportedAnimBlueprintReachableGraphInventory_ImportBpy(
	UBlueprint* BP,
	const TCHAR* StageTag)
{
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag][AnimInventory][%s] bp=%s root_graphs=%d reachable_graphs=%d"),
		StageTag ? StageTag : TEXT("Unknown"),
		*GetPathNameSafe(BP),
		RootGraphs.Num(),
		ReachableGraphs.Num());

	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		int32 AnimNodeCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetClass() &&
				Node->GetClass()->GetName().StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
			{
				++AnimNodeCount;
			}
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][AnimInventory][%s] graph=%s class=%s outer=%s nodes=%d anim_nodes=%d guid=%s"),
			StageTag ? StageTag : TEXT("Unknown"),
			*Graph->GetPathName(),
			*GetNameSafe(Graph->GetClass()),
			*GetPathNameSafe(Graph->GetOuter()),
			Graph->Nodes.Num(),
			AnimNodeCount,
			*Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens));

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !Node->GetClass() ||
				!Node->GetClass()->GetName().StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag][AnimInventory][%s] anim_node guid=%s graph=%s node=%s class=%s"),
				StageTag ? StageTag : TEXT("Unknown"),
				*Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
				*Graph->GetPathName(),
				*DescribeNode_ImportBpy(Node),
				*GetNameSafe(Node->GetClass()));
		}
	}
}

void LogSerializedAnimNodeUidResolutionAudit_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* StageTag)
{
	if (!BP || !Cast<UAnimBlueprint>(BP) || !Root.IsValid())
	{
		return;
	}

	TMap<FString, TSharedPtr<FJsonObject>> SerializedAnimNodeByUid;
	CollectSerializedAnimNodeJsonByUidFromRootJson_ImportBpy(Root, SerializedAnimNodeByUid);

	TArray<FString> SortedUids;
	SerializedAnimNodeByUid.GetKeys(SortedUids);
	SortedUids.Sort();

	int32 MatchedCount = 0;
	int32 MissingCount = 0;
	int32 ResolvedByNodeGuidCount = 0;
	int32 ResolvedByUidRegistryCount = 0;
	int32 ResolvedByUidGuidScanCount = 0;
	int32 UidEqualsNodeGuidCount = 0;
	int32 UidDiffersFromNodeGuidCount = 0;
	for (const FString& SerializedUid : SortedUids)
	{
		const TSharedPtr<FJsonObject>* NodeJson = SerializedAnimNodeByUid.Find(SerializedUid);
		const FString SerializedNodeGuid =
			(NodeJson && NodeJson->IsValid())
				? GetSerializedAnimNodeGuid_ImportBpy(*NodeJson)
				: FString();
		const bool bUidEqualsNodeGuid =
			(NodeJson && NodeJson->IsValid()) &&
			DoSerializedAnimNodeUidAndGuidMatch_ImportBpy(SerializedUid, SerializedNodeGuid);
		if (bUidEqualsNodeGuid)
		{
			++UidEqualsNodeGuidCount;
		}
		else
		{
			++UidDiffersFromNodeGuidCount;
		}

		FString ResolutionMode;
		UEdGraphNode* LiveNode =
			(NodeJson && NodeJson->IsValid())
				? FindImportedAnimNodeFromSerializedJson_ImportBpy(BP, *NodeJson, &ResolutionMode)
				: FindImportedNodeBySerializedUid_ImportBpy(BP, SerializedUid);

		if (!LiveNode)
		{
			++MissingCount;
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[ExportBpy][ImportDiag][AnimUidAudit][%s] missing uid=%s %s"),
				StageTag ? StageTag : TEXT("Unknown"),
				*SerializedUid,
				NodeJson && NodeJson->IsValid()
					? *BuildSerializedAnimNodeDiagnosticSummary_ImportBpy(*NodeJson)
					: TEXT("node_json=<missing>"));
			continue;
		}

		++MatchedCount;
		if (ResolutionMode.Equals(TEXT("node_guid_scan"), ESearchCase::CaseSensitive))
		{
			++ResolvedByNodeGuidCount;
		}
		else if (ResolutionMode.Equals(TEXT("uid_registry"), ESearchCase::CaseSensitive))
		{
			++ResolvedByUidRegistryCount;
		}
		else if (ResolutionMode.Equals(TEXT("uid_guid_scan"), ESearchCase::CaseSensitive))
		{
			++ResolvedByUidGuidScanCount;
		}
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][AnimUidAudit][%s] resolved uid=%s node_guid=%s mode=%s graph=%s node=%s class=%s live_guid=%s"),
			StageTag ? StageTag : TEXT("Unknown"),
			*SerializedUid,
			SerializedNodeGuid.IsEmpty() ? TEXT("<none>") : *SerializedNodeGuid,
			ResolutionMode.IsEmpty() ? TEXT("unknown") : *ResolutionMode,
			LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetPathName() : TEXT("<null>"),
			*DescribeNode_ImportBpy(LiveNode),
			*GetNameSafe(LiveNode->GetClass()),
			*LiveNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag][AnimUidAudit][%s] bp=%s serialized_anim_nodes=%d matched=%d missing=%d resolved_by_node_guid=%d resolved_by_uid_registry=%d resolved_by_uid_guid_scan=%d uid_eq_node_guid=%d uid_diff_node_guid=%d"),
		StageTag ? StageTag : TEXT("Unknown"),
		*GetPathNameSafe(BP),
		SortedUids.Num(),
		MatchedCount,
		MissingCount,
		ResolvedByNodeGuidCount,
		ResolvedByUidRegistryCount,
		ResolvedByUidGuidScanCount,
		UidEqualsNodeGuidCount,
		UidDiffersFromNodeGuidCount);
}

void CollectAnimNodeFunctionRefBindingMismatches_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!BP || !Root.IsValid())
	{
		return;
	}

	TMap<FString, TSharedPtr<FJsonObject>> SerializedAnimNodeByUid;
	CollectSerializedAnimNodeJsonByUidFromRootJson_ImportBpy(Root, SerializedAnimNodeByUid);
	if (SerializedAnimNodeByUid.Num() == 0)
	{
		return;
	}

	static const TCHAR* FunctionRefFieldNames[] = {
		TEXT("InitialUpdateFunction"),
		TEXT("BecomeRelevantFunction"),
		TEXT("UpdateFunction"),
		TEXT("OnMotionMatchingStateUpdatedFunction")
	};

	for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : SerializedAnimNodeByUid)
	{
		const FString& SerializedUid = Pair.Key;
		const TSharedPtr<FJsonObject>& NodeJson = Pair.Value;
		if (!NodeJson.IsValid())
		{
			continue;
		}

		UEdGraphNode* LiveNode = FindImportedAnimNodeFromSerializedJson_ImportBpy(BP, NodeJson);

		if (!LiveNode)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("missing_live_anim_node_for_function_ref uid=%s node_guid=%s"),
				*SerializedUid,
				*GetSerializedAnimNodeGuid_ImportBpy(NodeJson)));
			continue;
		}

		for (const TCHAR* FieldName : FunctionRefFieldNames)
		{
			const FString ExpectedRefText =
				GetSerializedNodePropStringFromNodeJson_ImportBpy(NodeJson, FieldName);
			const FName ExpectedFunctionName =
				ExtractFunctionNameFromMemberReferenceText_ImportBpy(ExpectedRefText);
			if (ExpectedFunctionName.IsNone())
			{
				continue;
			}

			const FString ActualRefText = ReadNodePropertyAsText_ImportBpy(LiveNode, FieldName);
			const FName ActualFunctionName =
				ExtractFunctionNameFromMemberReferenceText_ImportBpy(ActualRefText);
			if (!ActualFunctionName.IsEqual(ExpectedFunctionName))
			{
				OutMismatches.Add(FString::Printf(
					TEXT("anim_node_function_ref_mismatch uid=%s graph=%s node=%s field=%s expected=%s actual=%s"),
					*SerializedUid,
					LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
					*DescribeNode_ImportBpy(LiveNode),
					FieldName,
					*ExpectedFunctionName.ToString(),
					ActualFunctionName.IsNone() ? TEXT("None") : *ActualFunctionName.ToString()));
			}

			if (!ActualFunctionName.IsNone())
			{
				UFunction* const ResolvedFunction =
					ResolveSelfContextFunction_ImportBpy(
						LiveNode->GetGraph(),
						ActualFunctionName.ToString());
				if (!ResolvedFunction)
				{
					OutMismatches.Add(FString::Printf(
						TEXT("anim_node_function_ref_unresolved uid=%s graph=%s node=%s field=%s function=%s"),
						*SerializedUid,
						LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
						*DescribeNode_ImportBpy(LiveNode),
						FieldName,
						*ActualFunctionName.ToString()));
				}
			}
		}
	}
}

FString GetSerializedStateAliasNodeGuid_ImportBpy(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (!NodeObj.IsValid())
	{
		return FString();
	}

	FString NodeGuid;
	NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuid);
	if (NodeGuid.IsEmpty())
	{
		NodeObj->TryGetStringField(TEXT("uid"), NodeGuid);
	}
	return NodeGuid;
}

void CollectSerializedStateAliasNodesRecursive_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	TSet<FString>& VisitedGraphGuids,
	TMap<FString, FString>& OutAliasNodeToAliasedStateUids)
{
	if (!GraphObj.IsValid())
	{
		return;
	}

	FString GraphGuid;
	if (GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuid) && !GraphGuid.IsEmpty())
	{
		if (VisitedGraphGuids.Contains(GraphGuid))
		{
			return;
		}
		VisitedGraphGuids.Add(GraphGuid);
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return;
	}

	static const TCHAR* NestedGraphFields[] = {
		TEXT("BoundGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClassName;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
		if (IsSerializedStateAliasNodeClass_ImportBpy(NodeClassName))
		{
			const FString NodeGuid = GetSerializedStateAliasNodeGuid_ImportBpy(NodeObj);
			if (!NodeGuid.IsEmpty())
			{
				OutAliasNodeToAliasedStateUids.Add(
					NodeGuid,
					NormalizeAliasedStateUidList_ImportBpy(
						GetNodePropString_ImportBpy(NodeObj, TEXT("AliasedStateUids"))));
			}
		}

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		if (!NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
		{
			continue;
		}

		for (const TCHAR* FieldName : NestedGraphFields)
		{
			FString NestedGraphJson;
			if (!(*NodePropsObj)->TryGetStringField(FieldName, NestedGraphJson) || NestedGraphJson.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphObj;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NestedGraphJson);
			if (!FJsonSerializer::Deserialize(Reader, NestedGraphObj) || !NestedGraphObj.IsValid())
			{
				continue;
			}

			CollectSerializedStateAliasNodesRecursive_ImportBpy(
				NestedGraphObj,
				VisitedGraphGuids,
				OutAliasNodeToAliasedStateUids);
		}
	}
}

void CollectStateMachineAliasNodeMismatches_ImportBpy(
	const TSharedPtr<FJsonObject>& SourceRoot,
	const TSharedPtr<FJsonObject>& LiveRoot,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!SourceRoot.IsValid() || !LiveRoot.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* SourceGraphsArr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* LiveGraphsArr = nullptr;
	if (!SourceRoot->TryGetArrayField(TEXT("graphs"), SourceGraphsArr) || !SourceGraphsArr ||
		!LiveRoot->TryGetArrayField(TEXT("graphs"), LiveGraphsArr) || !LiveGraphsArr)
	{
		return;
	}

	TMap<FString, FString> ExpectedAliasesByNodeGuid;
	TMap<FString, FString> ActualAliasesByNodeGuid;
	TSet<FString> VisitedGraphGuids;
	for (const TSharedPtr<FJsonValue>& GraphValue : *SourceGraphsArr)
	{
		CollectSerializedStateAliasNodesRecursive_ImportBpy(
			GraphValue.IsValid() ? GraphValue->AsObject() : nullptr,
			VisitedGraphGuids,
			ExpectedAliasesByNodeGuid);
	}

	VisitedGraphGuids.Reset();
	for (const TSharedPtr<FJsonValue>& GraphValue : *LiveGraphsArr)
	{
		CollectSerializedStateAliasNodesRecursive_ImportBpy(
			GraphValue.IsValid() ? GraphValue->AsObject() : nullptr,
			VisitedGraphGuids,
			ActualAliasesByNodeGuid);
	}

	for (const TPair<FString, FString>& ExpectedPair : ExpectedAliasesByNodeGuid)
	{
		const FString* ActualAliases = ActualAliasesByNodeGuid.Find(ExpectedPair.Key);
		if (!ActualAliases)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("missing_state_alias_node guid=%s expected_aliases=%s"),
				*ExpectedPair.Key,
				*ExpectedPair.Value));
			continue;
		}

		if (!ActualAliases->Equals(ExpectedPair.Value, ESearchCase::CaseSensitive))
		{
			OutMismatches.Add(FString::Printf(
				TEXT("state_alias_uids_mismatch guid=%s expected=%s actual=%s"),
				*ExpectedPair.Key,
				*ExpectedPair.Value,
				**ActualAliases));
		}
	}
}

bool IsSerializedStructTextEmpty_ImportBpy(const FString& InText)
{
	FString Text = InText;
	Text.TrimStartAndEndInline();
	if (Text.IsEmpty())
	{
		return true;
	}
	if (Text.StartsWith(TEXT("\"")) && Text.EndsWith(TEXT("\"")) && Text.Len() >= 2)
	{
		Text = Text.Mid(1, Text.Len() - 2);
		Text.TrimStartAndEndInline();
	}
	return Text.IsEmpty() ||
		Text.Equals(TEXT("()"), ESearchCase::CaseSensitive) ||
		Text.Equals(TEXT("None"), ESearchCase::IgnoreCase) ||
		Text.Equals(TEXT("Null"), ESearchCase::IgnoreCase);
}

FName ExtractAnimStateNotifyNameFromSerializedText_ImportBpy(const FString& SerializedText)
{
	if (SerializedText.IsEmpty())
	{
		return NAME_None;
	}

	static const TCHAR* Needle = TEXT("NotifyName=\"");
	const int32 NeedlePos = SerializedText.Find(Needle, ESearchCase::CaseSensitive);
	if (NeedlePos == INDEX_NONE)
	{
		return NAME_None;
	}

	const int32 ValueStart = NeedlePos + FCString::Strlen(Needle);
	const int32 ValueEnd = SerializedText.Find(
		TEXT("\""),
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart,
		ValueStart);
	if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
	{
		return NAME_None;
	}

	const FString NotifyName =
		SerializedText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
	if (NotifyName.IsEmpty() || NotifyName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return NAME_None;
	}

	return FName(*NotifyName);
}

TArray<FName> ExtractAnimStateNotifyNamesFromSerializedText_ImportBpy(const FString& SerializedText)
{
	TArray<FName> Result;
	if (SerializedText.IsEmpty())
	{
		return Result;
	}

	static const TCHAR* Needle = TEXT("NotifyName=\"");
	const int32 NeedleLen = FCString::Strlen(Needle);
	int32 SearchPos = 0;
	while (SearchPos < SerializedText.Len())
	{
		const int32 NeedlePos = SerializedText.Find(
			Needle,
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			SearchPos);
		if (NeedlePos == INDEX_NONE)
		{
			break;
		}

		const int32 ValueStart = NeedlePos + NeedleLen;
		const int32 ValueEnd = SerializedText.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			ValueStart);
		if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
		{
			break;
		}

		const FString NotifyName =
			SerializedText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
		if (!NotifyName.IsEmpty() && !NotifyName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Result.Add(FName(*NotifyName));
		}

		SearchPos = ValueEnd + 1;
	}

	return Result;
}

FName ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
	const FString& NodeStructText,
	const TCHAR* HookFieldName)
{
	if (NodeStructText.IsEmpty() || !HookFieldName)
	{
		return NAME_None;
	}

	if (!HookFieldName || !*HookFieldName)
	{
		const FString MemberNeedle = TEXT("MemberName=\"");
		const int32 MemberPos = NodeStructText.Find(MemberNeedle, ESearchCase::CaseSensitive);
		if (MemberPos != INDEX_NONE)
		{
			const int32 ValueStart = MemberPos + MemberNeedle.Len();
			const int32 ValueEnd = NodeStructText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			if (ValueEnd != INDEX_NONE && ValueEnd > ValueStart)
			{
				const FString FunctionName = NodeStructText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
				if (!FunctionName.IsEmpty() && !FunctionName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
				{
					return FName(*FunctionName);
				}
			}
		}
		return NAME_None;
	}

	const FString FunctionNeedle =
		FString::Printf(TEXT("%s=(FunctionName=\""), HookFieldName);
	int32 NeedlePos = NodeStructText.Find(FunctionNeedle, ESearchCase::CaseSensitive);
	if (NeedlePos != INDEX_NONE)
	{
		const int32 ValueStart = NeedlePos + FunctionNeedle.Len();
		const int32 ValueEnd = NodeStructText.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			ValueStart);
		if (ValueEnd != INDEX_NONE && ValueEnd > ValueStart)
		{
			const FString FunctionName =
				NodeStructText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
			if (!FunctionName.IsEmpty() && !FunctionName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				return FName(*FunctionName);
			}
		}
	}

	const FString MemberNeedle =
		FString::Printf(TEXT("%s=(MemberName=\""), HookFieldName);
	NeedlePos = NodeStructText.Find(MemberNeedle, ESearchCase::CaseSensitive);
	if (NeedlePos != INDEX_NONE)
	{
		const int32 ValueStart = NeedlePos + MemberNeedle.Len();
		const int32 ValueEnd = NodeStructText.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			ValueStart);
		if (ValueEnd != INDEX_NONE && ValueEnd > ValueStart)
		{
			const FString FunctionName =
				NodeStructText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
			if (!FunctionName.IsEmpty() && !FunctionName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				return FName(*FunctionName);
			}
		}
	}

	return NAME_None;
}

void LogAnimBlueprintStateMachineEntryBindings_ImportBpy(
	UBlueprint* BP,
	const TCHAR* StageName)
{
	if (!BP || !Cast<UAnimBlueprint>(BP))
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	int32 LoggedStateCount = 0;
	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UAnimStateNode* const StateNode = Cast<UAnimStateNode>(Node);
			if (!StateNode || !StateNode->BoundGraph)
			{
				continue;
			}

			FName StateEntryFunctionName = NAME_None;
			for (UEdGraphNode* BoundNode : StateNode->BoundGraph->Nodes)
			{
				if (UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(BoundNode))
				{
					const FString StateResultNodeText =
						ReadNodePropertyAsText_ImportBpy(StateResultNode, TEXT("Node"));
					StateEntryFunctionName =
						ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
							StateResultNodeText,
							TEXT("StateEntryFunction"));
					if (!StateEntryFunctionName.IsNone())
					{
						break;
					}
				}
			}

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag][StateEntrySnapshot][%s] graph=%s state=%s node=%s notify=%s state_entry_func=%s"),
				StageName ? StageName : TEXT("unknown"),
				*Graph->GetName(),
				*StateNode->GetName(),
				*DescribeNode_ImportBpy(StateNode),
				StateNode->StateEntered.NotifyName.IsNone()
					? TEXT("None")
					: *StateNode->StateEntered.NotifyName.ToString(),
				StateEntryFunctionName.IsNone()
					? TEXT("None")
					: *StateEntryFunctionName.ToString());
			++LoggedStateCount;
		}
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag][StateEntrySnapshot][%s] total_states=%d blueprint=%s"),
		StageName ? StageName : TEXT("unknown"),
		LoggedStateCount,
		*GetPathNameSafe(BP));
}

void CollectSerializedStateMachineBindingSnapshotsRecursive_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	TSet<FString>& VisitedGraphGuids,
	TMap<FString, FStateMachineBindingSnapshot_ImportBpy>& OutSnapshotsByNodeGuid)
{
	if (!GraphObj.IsValid())
	{
		return;
	}

	FString GraphGuidText;
	if (GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuidText) && !GraphGuidText.IsEmpty())
	{
		const FString NormalizedGraphGuid =
			NormalizeGuidTextForRoundtrip_ImportBpy(GraphGuidText);
		if (VisitedGraphGuids.Contains(NormalizedGraphGuid))
		{
			return;
		}
		VisitedGraphGuids.Add(NormalizedGraphGuid);
	}

	FString GraphName;
	GraphObj->TryGetStringField(TEXT("name"), GraphName);

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return;
	}

	static const TCHAR* NestedGraphFields[] = {
		TEXT("BoundGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClassName;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);

		FString NodeGuidText;
		NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuidText);
		if (NodeGuidText.IsEmpty())
		{
			NodeObj->TryGetStringField(TEXT("uid"), NodeGuidText);
		}

		const FString NodeGuidNormalized =
			NormalizeGuidTextForRoundtrip_ImportBpy(NodeGuidText);
		const bool bHasNodeGuid = !NodeGuidNormalized.IsEmpty();

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		const bool bHasNodeProps =
			NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) &&
			NodePropsObj &&
			NodePropsObj->IsValid();

		const bool bIsStateNode =
			NodeClassName.Equals(TEXT("AnimStateNode"), ESearchCase::CaseSensitive) ||
			NodeClassName.Equals(TEXT("K2Node_AnimStateNode"), ESearchCase::CaseSensitive);
		const bool bIsStateResultNode =
			NodeClassName.Equals(TEXT("AnimGraphNode_StateResult"), ESearchCase::CaseSensitive);

		if (bHasNodeGuid && bHasNodeProps && (bIsStateNode || bIsStateResultNode))
		{
			FStateMachineBindingSnapshot_ImportBpy& Snapshot =
				OutSnapshotsByNodeGuid.FindOrAdd(NodeGuidNormalized);
			Snapshot.NodeClassName = NodeClassName;
			Snapshot.NodeGuid = NodeGuidNormalized;
			if (!GraphName.IsEmpty())
			{
				Snapshot.GraphName = GraphName;
			}

			if (bIsStateNode)
			{
				(*NodePropsObj)->TryGetStringField(TEXT("StateEntered"), Snapshot.StateEnteredText);
				(*NodePropsObj)->TryGetStringField(TEXT("StateLeft"), Snapshot.StateLeftText);
				(*NodePropsObj)->TryGetStringField(TEXT("StateFullyBlended"), Snapshot.StateFullyBlendedText);
			}
			else if (bIsStateResultNode)
			{
				FString EditorStateEntryFunctionText;
				if ((*NodePropsObj)->TryGetStringField(TEXT("StateEntryFunction"), EditorStateEntryFunctionText) &&
					!EditorStateEntryFunctionText.IsEmpty())
				{
					Snapshot.StateEntryFunctionName =
						ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
							EditorStateEntryFunctionText,
							TEXT(""));
				}

				FString NodeStructText;
				if ((*NodePropsObj)->TryGetStringField(TEXT("Node"), NodeStructText) &&
					!NodeStructText.IsEmpty())
				{
					if (Snapshot.StateEntryFunctionName.IsNone())
					{
						Snapshot.StateEntryFunctionName =
							ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
								NodeStructText,
								TEXT("StateEntryFunction"));
					}
					Snapshot.UpdateFunctionName =
						ExtractStateResultHookFunctionNameFromStructText_ImportBpy(
							NodeStructText,
							TEXT("UpdateFunction"));
				}
			}
		}

		if (!bHasNodeProps)
		{
			continue;
		}

		for (const TCHAR* FieldName : NestedGraphFields)
		{
			FString NestedGraphJsonText;
			if (!(*NodePropsObj)->TryGetStringField(FieldName, NestedGraphJsonText) ||
				NestedGraphJsonText.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphObj;
			const TSharedRef<TJsonReader<>> Reader =
				TJsonReaderFactory<>::Create(NestedGraphJsonText);
			if (!FJsonSerializer::Deserialize(Reader, NestedGraphObj) || !NestedGraphObj.IsValid())
			{
				continue;
			}

			CollectSerializedStateMachineBindingSnapshotsRecursive_ImportBpy(
				NestedGraphObj,
				VisitedGraphGuids,
				OutSnapshotsByNodeGuid);
		}
	}
}

void CollectSerializedStateMachineBindingSnapshotsFromRootJson_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	TMap<FString, FStateMachineBindingSnapshot_ImportBpy>& OutSnapshotsByNodeGuid)
{
	OutSnapshotsByNodeGuid.Reset();
	if (!Root.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return;
	}

	TSet<FString> VisitedGraphGuids;
	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		CollectSerializedStateMachineBindingSnapshotsRecursive_ImportBpy(
			GraphValue.IsValid() ? GraphValue->AsObject() : nullptr,
			VisitedGraphGuids,
			OutSnapshotsByNodeGuid);
	}
}

void CollectStateMachineBindingContractMismatches_ImportBpy(
	const TSharedPtr<FJsonObject>& SourceRoot,
	const TSharedPtr<FJsonObject>& LiveRoot,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!SourceRoot.IsValid() || !LiveRoot.IsValid())
	{
		return;
	}

	TMap<FString, FStateMachineBindingSnapshot_ImportBpy> ExpectedSnapshots;
	TMap<FString, FStateMachineBindingSnapshot_ImportBpy> ActualSnapshots;
	CollectSerializedStateMachineBindingSnapshotsFromRootJson_ImportBpy(
		SourceRoot,
		ExpectedSnapshots);
	CollectSerializedStateMachineBindingSnapshotsFromRootJson_ImportBpy(
		LiveRoot,
		ActualSnapshots);

	for (const TPair<FString, FStateMachineBindingSnapshot_ImportBpy>& ExpectedPair : ExpectedSnapshots)
	{
		const FString& NodeGuid = ExpectedPair.Key;
		const FStateMachineBindingSnapshot_ImportBpy& Expected = ExpectedPair.Value;
		const FStateMachineBindingSnapshot_ImportBpy* Actual = ActualSnapshots.Find(NodeGuid);

		const bool bExpectedStateEnteredBound =
			!IsSerializedStructTextEmpty_ImportBpy(Expected.StateEnteredText);
		const bool bExpectedStateLeftBound =
			!IsSerializedStructTextEmpty_ImportBpy(Expected.StateLeftText);
		const bool bExpectedStateFullyBlendedBound =
			!IsSerializedStructTextEmpty_ImportBpy(Expected.StateFullyBlendedText);
		const bool bExpectedAnyStateNodeBinding =
			bExpectedStateEnteredBound ||
			bExpectedStateLeftBound ||
			bExpectedStateFullyBlendedBound;

		const bool bExpectedStateResultStateEntryBound =
			!Expected.StateEntryFunctionName.IsNone();
		const bool bExpectedStateResultUpdateBound =
			!Expected.UpdateFunctionName.IsNone();
		const bool bExpectedAnyStateResultBinding =
			bExpectedStateResultStateEntryBound ||
			bExpectedStateResultUpdateBound;

		if (!bExpectedAnyStateNodeBinding && !bExpectedAnyStateResultBinding)
		{
			continue;
		}

		if (!Actual)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("missing_state_machine_binding_node guid=%s class=%s graph=%s"),
				*NodeGuid,
				*Expected.NodeClassName,
				*Expected.GraphName));
			continue;
		}

		auto ValidateStateNotifyBinding = [&OutMismatches, &Expected, Actual, &NodeGuid](
			const TCHAR* FieldName,
			const FString& ExpectedText,
			const FString& ActualText)
		{
			const bool bExpectedBound =
				!IsSerializedStructTextEmpty_ImportBpy(ExpectedText);
			if (!bExpectedBound)
			{
				return;
			}

			const bool bActualBound =
				!IsSerializedStructTextEmpty_ImportBpy(ActualText);
			if (!bActualBound)
			{
				OutMismatches.Add(FString::Printf(
					TEXT("state_node_binding_lost guid=%s graph=%s node_class=%s field=%s expected=%s actual=%s"),
					*NodeGuid,
					*Expected.GraphName,
					*Expected.NodeClassName,
					FieldName ? FieldName : TEXT("<null>"),
					*ExpectedText,
					*ActualText));
				return;
			}

			const TArray<FName> ExpectedNotifyNamesRaw =
				ExtractAnimStateNotifyNamesFromSerializedText_ImportBpy(ExpectedText);
			const TArray<FName> ActualNotifyNamesRaw =
				ExtractAnimStateNotifyNamesFromSerializedText_ImportBpy(ActualText);

			auto IsPrimaryStateNotifyForField = [](const FName& NotifyName, const TCHAR* InFieldName) -> bool
			{
				if (NotifyName.IsNone() || !InFieldName)
				{
					return false;
				}
				const FString Name = NotifyName.ToString();
				if (FCString::Stricmp(InFieldName, TEXT("StateEntered")) == 0)
				{
					return Name.StartsWith(TEXT("OnStateEntry_"), ESearchCase::CaseSensitive);
				}
				if (FCString::Stricmp(InFieldName, TEXT("StateLeft")) == 0)
				{
					return Name.StartsWith(TEXT("OnStateLeft_"), ESearchCase::CaseSensitive);
				}
				if (FCString::Stricmp(InFieldName, TEXT("StateFullyBlended")) == 0)
				{
					return Name.StartsWith(TEXT("OnStateFullyBlended_"), ESearchCase::CaseSensitive);
				}
				return false;
			};

			auto CollectPrimaryNotifyListForField = [&](const TArray<FName>& RawNames, const TCHAR* InFieldName) -> TArray<FName>
			{
				TArray<FName> Primary;
				for (const FName Name : RawNames)
				{
					if (IsPrimaryStateNotifyForField(Name, InFieldName))
					{
						Primary.Add(Name);
					}
				}
				return Primary;
			};

			const TArray<FName> ExpectedPrimaryNotifyNames =
				CollectPrimaryNotifyListForField(ExpectedNotifyNamesRaw, FieldName);
			const TArray<FName> ActualPrimaryNotifyNames =
				CollectPrimaryNotifyListForField(ActualNotifyNamesRaw, FieldName);

			// Enforce only semantic state hook names (OnStateEntry_*/OnStateLeft_*...).
			// Debug/auxiliary notifies (e.g. PrintDelta) are allowed to drift.
			if (ExpectedPrimaryNotifyNames.Num() == 0)
			{
				return;
			}

			const TArray<FName>& ExpectedNotifyNames = ExpectedPrimaryNotifyNames;
			const TArray<FName>& ActualNotifyNames =
				ActualPrimaryNotifyNames.Num() > 0 ? ActualPrimaryNotifyNames : ActualNotifyNamesRaw;

			const FName ExpectedNotifyName =
				ExpectedNotifyNames.Num() > 0 ? ExpectedNotifyNames[0] : NAME_None;
			const FName ActualNotifyName =
				ActualNotifyNames.Num() > 0 ? ActualNotifyNames[0] : NAME_None;
			if (!ExpectedNotifyName.IsNone() && ExpectedNotifyName != ActualNotifyName)
			{
				OutMismatches.Add(FString::Printf(
					TEXT("state_node_notify_name_mismatch guid=%s graph=%s node_class=%s field=%s expected=%s actual=%s"),
					*NodeGuid,
					*Expected.GraphName,
					*Expected.NodeClassName,
					FieldName ? FieldName : TEXT("<null>"),
					*ExpectedNotifyName.ToString(),
					*ActualNotifyName.ToString()));
			}

			if (ExpectedNotifyNames.Num() != ActualNotifyNames.Num())
			{
				TArray<FString> ExpectedNotifyNameStrings;
				TArray<FString> ActualNotifyNameStrings;
				ExpectedNotifyNameStrings.Reserve(ExpectedNotifyNames.Num());
				ActualNotifyNameStrings.Reserve(ActualNotifyNames.Num());
				for (const FName Name : ExpectedNotifyNames)
				{
					ExpectedNotifyNameStrings.Add(Name.ToString());
				}
				for (const FName Name : ActualNotifyNames)
				{
					ActualNotifyNameStrings.Add(Name.ToString());
				}
				OutMismatches.Add(FString::Printf(
					TEXT("state_node_notify_count_mismatch guid=%s graph=%s node_class=%s field=%s expected=%s actual=%s"),
					*NodeGuid,
					*Expected.GraphName,
					*Expected.NodeClassName,
					FieldName ? FieldName : TEXT("<null>"),
					*FString::Join(ExpectedNotifyNameStrings, TEXT(",")),
					*FString::Join(ActualNotifyNameStrings, TEXT(","))));
				return;
			}

			for (int32 Index = 0; Index < ExpectedNotifyNames.Num(); ++Index)
			{
				if (ExpectedNotifyNames[Index] != ActualNotifyNames[Index])
				{
					TArray<FString> ExpectedNotifyNameStrings;
					TArray<FString> ActualNotifyNameStrings;
					ExpectedNotifyNameStrings.Reserve(ExpectedNotifyNames.Num());
					ActualNotifyNameStrings.Reserve(ActualNotifyNames.Num());
					for (const FName Name : ExpectedNotifyNames)
					{
						ExpectedNotifyNameStrings.Add(Name.ToString());
					}
					for (const FName Name : ActualNotifyNames)
					{
						ActualNotifyNameStrings.Add(Name.ToString());
					}
					OutMismatches.Add(FString::Printf(
						TEXT("state_node_notify_list_mismatch guid=%s graph=%s node_class=%s field=%s expected=%s actual=%s"),
						*NodeGuid,
						*Expected.GraphName,
						*Expected.NodeClassName,
						FieldName ? FieldName : TEXT("<null>"),
						*FString::Join(ExpectedNotifyNameStrings, TEXT(",")),
						*FString::Join(ActualNotifyNameStrings, TEXT(","))));
					return;
				}
			}
		};

		ValidateStateNotifyBinding(
			TEXT("StateEntered"),
			Expected.StateEnteredText,
			Actual->StateEnteredText);
		ValidateStateNotifyBinding(
			TEXT("StateLeft"),
			Expected.StateLeftText,
			Actual->StateLeftText);
		ValidateStateNotifyBinding(
			TEXT("StateFullyBlended"),
			Expected.StateFullyBlendedText,
			Actual->StateFullyBlendedText);

		if (bExpectedStateResultStateEntryBound &&
			Actual->StateEntryFunctionName != Expected.StateEntryFunctionName)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("state_result_state_entry_binding_mismatch guid=%s graph=%s expected=%s actual=%s"),
				*NodeGuid,
				*Expected.GraphName,
				*Expected.StateEntryFunctionName.ToString(),
				*Actual->StateEntryFunctionName.ToString()));
		}

		if (bExpectedStateResultUpdateBound &&
			Actual->UpdateFunctionName != Expected.UpdateFunctionName)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("state_result_update_binding_mismatch guid=%s graph=%s expected=%s actual=%s"),
				*NodeGuid,
				*Expected.GraphName,
				*Expected.UpdateFunctionName.ToString(),
				*Actual->UpdateFunctionName.ToString()));
		}
	}
}

struct FLinkedAnimLayerContractSnapshot_ImportBpy
{
	FString NodeGuid;
	FString GraphName;
	FName LayerName = NAME_None;
	bool bHasInterfaceField = false;
	bool bInterfaceIsNone = true;
	FString InterfaceValue;
};

FName ExtractLinkedAnimLayerNameFromNodeStructText_ImportBpy(const FString& NodeStructText)
{
	if (NodeStructText.IsEmpty())
	{
		return NAME_None;
	}

	static const TCHAR* Needle = TEXT("Layer=\"");
	const int32 NeedlePos = NodeStructText.Find(Needle, ESearchCase::CaseSensitive);
	if (NeedlePos == INDEX_NONE)
	{
		return NAME_None;
	}

	const int32 ValueStart = NeedlePos + FCString::Strlen(Needle);
	const int32 ValueEnd = NodeStructText.Find(
		TEXT("\""),
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart,
		ValueStart);
	if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
	{
		return NAME_None;
	}

	const FString LayerName =
		NodeStructText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
	if (LayerName.IsEmpty() || LayerName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return NAME_None;
	}

	return FName(*LayerName);
}

bool TryExtractLinkedAnimLayerInterfaceTokenFromNodeStructText_ImportBpy(
	const FString& NodeStructText,
	FString& OutInterfaceToken)
{
	OutInterfaceToken.Reset();
	if (NodeStructText.IsEmpty())
	{
		return false;
	}

	static const TCHAR* Needle = TEXT("Interface=");
	const int32 NeedlePos = NodeStructText.Find(Needle, ESearchCase::CaseSensitive);
	if (NeedlePos == INDEX_NONE)
	{
		return false;
	}

	const int32 ValueStart = NeedlePos + FCString::Strlen(Needle);
	int32 ValueEnd = NodeStructText.Find(
		TEXT(","),
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart,
		ValueStart);
	if (ValueEnd == INDEX_NONE)
	{
		ValueEnd = NodeStructText.Find(
			TEXT(")"),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			ValueStart);
	}
	if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
	{
		return false;
	}

	OutInterfaceToken = NodeStructText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
	if (OutInterfaceToken.StartsWith(TEXT("\"")) && OutInterfaceToken.EndsWith(TEXT("\"")) && OutInterfaceToken.Len() >= 2)
	{
		OutInterfaceToken = OutInterfaceToken.Mid(1, OutInterfaceToken.Len() - 2);
		OutInterfaceToken.TrimStartAndEndInline();
	}

	return true;
}

void CollectLinkedAnimLayerContractSnapshotsRecursive_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	TSet<FString>& VisitedGraphGuids,
	TMap<FString, FLinkedAnimLayerContractSnapshot_ImportBpy>& OutSnapshotsByNodeGuid)
{
	if (!GraphObj.IsValid())
	{
		return;
	}

	FString GraphGuidText;
	if (GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuidText) && !GraphGuidText.IsEmpty())
	{
		const FString NormalizedGraphGuid =
			NormalizeGuidTextForRoundtrip_ImportBpy(GraphGuidText);
		if (VisitedGraphGuids.Contains(NormalizedGraphGuid))
		{
			return;
		}
		VisitedGraphGuids.Add(NormalizedGraphGuid);
	}

	FString GraphName;
	GraphObj->TryGetStringField(TEXT("name"), GraphName);

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return;
	}

	static const TCHAR* NestedGraphFields[] = {
		TEXT("BoundGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClassName;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);

		FString NodeGuidText;
		NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuidText);
		if (NodeGuidText.IsEmpty())
		{
			NodeObj->TryGetStringField(TEXT("uid"), NodeGuidText);
		}
		const FString NodeGuid =
			NormalizeGuidTextForRoundtrip_ImportBpy(NodeGuidText);

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		const bool bHasNodeProps =
			NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) &&
			NodePropsObj &&
			NodePropsObj->IsValid();

		if (!NodeGuid.IsEmpty() &&
			bHasNodeProps &&
			NodeClassName.Equals(TEXT("AnimGraphNode_LinkedAnimLayer"), ESearchCase::CaseSensitive))
		{
			FString NodeStructText;
			if ((*NodePropsObj)->TryGetStringField(TEXT("Node"), NodeStructText) && !NodeStructText.IsEmpty())
			{
				FLinkedAnimLayerContractSnapshot_ImportBpy& Snapshot =
					OutSnapshotsByNodeGuid.FindOrAdd(NodeGuid);
				Snapshot.NodeGuid = NodeGuid;
				Snapshot.GraphName = GraphName;
				Snapshot.LayerName = ExtractLinkedAnimLayerNameFromNodeStructText_ImportBpy(NodeStructText);

				FString InterfaceToken;
				if (TryExtractLinkedAnimLayerInterfaceTokenFromNodeStructText_ImportBpy(
						NodeStructText,
						InterfaceToken))
				{
					Snapshot.bHasInterfaceField = true;
					Snapshot.InterfaceValue = InterfaceToken;
					Snapshot.bInterfaceIsNone =
						InterfaceToken.IsEmpty() ||
						InterfaceToken.Equals(TEXT("None"), ESearchCase::IgnoreCase) ||
						InterfaceToken.Equals(TEXT("Null"), ESearchCase::IgnoreCase);
				}
				else
				{
					Snapshot.bHasInterfaceField = false;
					Snapshot.InterfaceValue.Reset();
					Snapshot.bInterfaceIsNone = true;
				}
			}
		}

		if (!bHasNodeProps)
		{
			continue;
		}

		for (const TCHAR* FieldName : NestedGraphFields)
		{
			FString NestedGraphJsonText;
			if (!(*NodePropsObj)->TryGetStringField(FieldName, NestedGraphJsonText) ||
				NestedGraphJsonText.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphObj;
			const TSharedRef<TJsonReader<>> Reader =
				TJsonReaderFactory<>::Create(NestedGraphJsonText);
			if (!FJsonSerializer::Deserialize(Reader, NestedGraphObj) || !NestedGraphObj.IsValid())
			{
				continue;
			}

			CollectLinkedAnimLayerContractSnapshotsRecursive_ImportBpy(
				NestedGraphObj,
				VisitedGraphGuids,
				OutSnapshotsByNodeGuid);
		}
	}
}

void CollectLinkedAnimLayerContractMismatches_ImportBpy(
	const TSharedPtr<FJsonObject>& SourceRoot,
	const TSharedPtr<FJsonObject>& LiveRoot,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!SourceRoot.IsValid() || !LiveRoot.IsValid())
	{
		return;
	}

	TMap<FString, FLinkedAnimLayerContractSnapshot_ImportBpy> ExpectedSnapshots;
	TMap<FString, FLinkedAnimLayerContractSnapshot_ImportBpy> ActualSnapshots;
	TSet<FString> VisitedGraphGuids;
	const TArray<TSharedPtr<FJsonValue>>* SourceGraphs = nullptr;
	if (SourceRoot->TryGetArrayField(TEXT("graphs"), SourceGraphs) && SourceGraphs)
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *SourceGraphs)
		{
			CollectLinkedAnimLayerContractSnapshotsRecursive_ImportBpy(
				GraphValue.IsValid() ? GraphValue->AsObject() : nullptr,
				VisitedGraphGuids,
				ExpectedSnapshots);
		}
	}

	VisitedGraphGuids.Reset();
	const TArray<TSharedPtr<FJsonValue>>* LiveGraphs = nullptr;
	if (LiveRoot->TryGetArrayField(TEXT("graphs"), LiveGraphs) && LiveGraphs)
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *LiveGraphs)
		{
			CollectLinkedAnimLayerContractSnapshotsRecursive_ImportBpy(
				GraphValue.IsValid() ? GraphValue->AsObject() : nullptr,
				VisitedGraphGuids,
				ActualSnapshots);
		}
	}

	for (const TPair<FString, FLinkedAnimLayerContractSnapshot_ImportBpy>& ExpectedPair : ExpectedSnapshots)
	{
		const FString& NodeGuid = ExpectedPair.Key;
		const FLinkedAnimLayerContractSnapshot_ImportBpy& Expected = ExpectedPair.Value;
		if (Expected.LayerName.IsNone())
		{
			continue;
		}

		const FLinkedAnimLayerContractSnapshot_ImportBpy* Actual =
			ActualSnapshots.Find(NodeGuid);
		if (!Actual)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("missing_linked_anim_layer_node guid=%s expected_layer=%s"),
				*NodeGuid,
				*Expected.LayerName.ToString()));
			continue;
		}

		if (Actual->LayerName != Expected.LayerName)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("linked_anim_layer_name_mismatch guid=%s expected=%s actual=%s"),
				*NodeGuid,
				*Expected.LayerName.ToString(),
				*Actual->LayerName.ToString()));
		}

		const bool bExpectedRequiresInterface =
			Expected.bHasInterfaceField &&
			!Expected.bInterfaceIsNone;
		const bool bExpectedShouldBeNone =
			!Expected.bHasInterfaceField ||
			Expected.bInterfaceIsNone;
		if (bExpectedShouldBeNone)
		{
			if (Actual->bHasInterfaceField && !Actual->bInterfaceIsNone)
			{
				OutMismatches.Add(FString::Printf(
					TEXT("linked_anim_layer_interface_should_be_none guid=%s graph=%s layer=%s expected=None actual=%s"),
					*NodeGuid,
					*Actual->GraphName,
					*Actual->LayerName.ToString(),
					*Actual->InterfaceValue));
			}
			continue;
		}
		if (!bExpectedRequiresInterface)
		{
			continue;
		}

		if (!Actual->bHasInterfaceField || Actual->bInterfaceIsNone)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("linked_anim_layer_interface_missing guid=%s graph=%s layer=%s interface=%s"),
				*NodeGuid,
				*Actual->GraphName,
				*Actual->LayerName.ToString(),
				Actual->bHasInterfaceField ? *Actual->InterfaceValue : TEXT("<field_missing>")));
			continue;
		}

		auto NormalizeInterfaceToken = [](FString Value) -> FString
		{
			Value.TrimStartAndEndInline();
			if (Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")) && Value.Len() >= 2)
			{
				Value = Value.Mid(1, Value.Len() - 2);
				Value.TrimStartAndEndInline();
			}
			Value.ReplaceInline(TEXT(" "), TEXT(""));
			Value.ToLowerInline();
			return Value;
		};

		const FString ExpectedInterfaceNormalized =
			NormalizeInterfaceToken(Expected.InterfaceValue);
		const FString ActualInterfaceNormalized =
			NormalizeInterfaceToken(Actual->InterfaceValue);
		if (!ExpectedInterfaceNormalized.IsEmpty() &&
			ExpectedInterfaceNormalized != ActualInterfaceNormalized)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("linked_anim_layer_interface_mismatch guid=%s graph=%s layer=%s expected=%s actual=%s"),
				*NodeGuid,
				*Actual->GraphName,
				*Actual->LayerName.ToString(),
				*Expected.InterfaceValue,
				*Actual->InterfaceValue));
		}
	}
}

void CollectMotionMatchingPoseHistoryMismatches_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!BP || !Root.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return;
	}

	int32 ExpectedMotionMatchingNodeCount = 0;
	bool bExpectPoseHistoryCollector = false;
	bool bHasSourceAnimGraph = false;
	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid() || IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
		{
			continue;
		}

		FString GraphName;
		GraphObj->TryGetStringField(TEXT("name"), GraphName);
		if (!GraphName.Equals(UEdGraphSchema_K2::GN_AnimGraph.ToString(), ESearchCase::CaseSensitive))
		{
			continue;
		}

		bHasSourceAnimGraph = true;
		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			break;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString NodeClassName;
			NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
			if (NodeClassName.Contains(TEXT("MotionMatching"), ESearchCase::IgnoreCase))
			{
				++ExpectedMotionMatchingNodeCount;
			}
			if (NodeClassName.Contains(TEXT("PoseSearchHistoryCollector"), ESearchCase::IgnoreCase))
			{
				bExpectPoseHistoryCollector = true;
			}
		}

		break;
	}

	if (!bHasSourceAnimGraph)
	{
		return;
	}

	UEdGraph* AnimGraph = FindRootGraphByName_ImportBpy(BP, UEdGraphSchema_K2::GN_AnimGraph.ToString());
	if (!AnimGraph)
	{
		OutMismatches.Add(TEXT("motion_matching_missing_anim_graph"));
		return;
	}

	int32 ActualMotionMatchingNodeCount = 0;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (!Node || !Node->GetClass())
		{
			continue;
		}

		if (Node->GetClass()->GetName().Contains(TEXT("MotionMatching"), ESearchCase::IgnoreCase))
		{
			++ActualMotionMatchingNodeCount;
		}
	}

	if (ActualMotionMatchingNodeCount != ExpectedMotionMatchingNodeCount)
	{
		OutMismatches.Add(FString::Printf(
			TEXT("motion_matching_node_count expected=%d actual=%d"),
			ExpectedMotionMatchingNodeCount,
			ActualMotionMatchingNodeCount));
	}

	if (bExpectPoseHistoryCollector && !GraphContainsPoseHistoryCollectorRuntimeNode_ImportBpy(AnimGraph))
	{
		OutMismatches.Add(FString::Printf(
			TEXT("motion_matching_missing_pose_history_collector graph=%s"),
			*AnimGraph->GetName()));
	}
}

void CollectAnimNodePropertyBindingMismatches_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!BP || !Root.IsValid())
	{
		return;
	}

	TMap<FString, TSharedPtr<FJsonObject>> SerializedAnimNodeByUid;
	CollectSerializedAnimNodeJsonByUidFromRootJson_ImportBpy(Root, SerializedAnimNodeByUid);
	if (SerializedAnimNodeByUid.Num() == 0)
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : SerializedAnimNodeByUid)
	{
		const FString& SerializedUid = Pair.Key;
		const TSharedPtr<FJsonObject>& NodeJson = Pair.Value;
		if (!NodeJson.IsValid())
		{
			continue;
		}

		const FString SerializedBindings =
			GetSerializedNodePropStringFromNodeJson_ImportBpy(NodeJson, TEXT("BindingPropertyBindings"));
		if (SerializedBindings.IsEmpty())
		{
			// No binding contract serialized for this node.
			continue;
		}

		TSet<FString> ExpectedKeys;
		ExtractBindingPropertyKeysFromSerializedText_ImportBpy(SerializedBindings, ExpectedKeys);
		TMap<FString, FAnimNodeBindingDescriptor_ImportBpy> ExpectedDescriptors;
		CollectExpectedAnimNodeBindingDescriptorsFromSerializedText_ImportBpy(
			SerializedBindings,
			ExpectedDescriptors);
		if (SerializedBindings != TEXT("()") && ExpectedKeys.Num() == 0)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("binding_keys_parse_failed uid=%s serialized=%s"),
				*SerializedUid,
				*SerializedBindings.Left(256)));
			continue;
		}

		UEdGraphNode* LiveNode = FindImportedAnimNodeFromSerializedJson_ImportBpy(BP, NodeJson);
		if (!LiveNode)
		{
			OutMismatches.Add(FString::Printf(
				TEXT("missing_live_anim_node_for_binding_check uid=%s node_guid=%s"),
				*SerializedUid,
				*GetSerializedAnimNodeGuid_ImportBpy(NodeJson)));
			continue;
		}

		TSet<FString> ActualKeys;
		FString CollectError;
		if (!CollectAnimNodeBindingPropertyKeysFromLiveNode_ImportBpy(LiveNode, ActualKeys, CollectError))
		{
			OutMismatches.Add(FString::Printf(
				TEXT("binding_live_keys_read_failed uid=%s graph=%s node=%s error=%s"),
				*SerializedUid,
				LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
				*DescribeNode_ImportBpy(LiveNode),
				*CollectError));
			continue;
		}

		for (const FString& ExpectedKey : ExpectedKeys)
		{
			if (!ActualKeys.Contains(ExpectedKey))
			{
				OutMismatches.Add(FString::Printf(
					TEXT("binding_key_missing uid=%s graph=%s node=%s key=%s"),
					*SerializedUid,
					LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
					*DescribeNode_ImportBpy(LiveNode),
					*ExpectedKey));
			}
		}

		TMap<FString, FAnimNodeBindingDescriptor_ImportBpy> ActualDescriptors;
		if (!CollectAnimNodeBindingDescriptorsFromLiveNode_ImportBpy(LiveNode, ActualDescriptors, CollectError))
		{
			OutMismatches.Add(FString::Printf(
				TEXT("binding_live_descriptors_read_failed uid=%s graph=%s node=%s error=%s"),
				*SerializedUid,
				LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
				*DescribeNode_ImportBpy(LiveNode),
				*CollectError));
			continue;
		}

		for (const TPair<FString, FAnimNodeBindingDescriptor_ImportBpy>& PairExpected : ExpectedDescriptors)
		{
			const FString& Key = PairExpected.Key;
			const FAnimNodeBindingDescriptor_ImportBpy& ExpectedDescriptor = PairExpected.Value;
			const FAnimNodeBindingDescriptor_ImportBpy* ActualDescriptor = ActualDescriptors.Find(Key);
			if (!ActualDescriptor)
			{
				continue;
			}

			// UE can preserve binding keys/types while omitting PathAsText in serialized map text
			// after reload. Treat empty actual path text as "not comparable" instead of corruption.
			if (!ExpectedDescriptor.PathAsText.IsEmpty() &&
				!ActualDescriptor->PathAsText.IsEmpty() &&
				!ActualDescriptor->PathAsText.Equals(ExpectedDescriptor.PathAsText, ESearchCase::CaseSensitive))
			{
				OutMismatches.Add(FString::Printf(
					TEXT("binding_path_mismatch uid=%s graph=%s node=%s key=%s expected=%s actual=%s"),
					*SerializedUid,
					LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
					*DescribeNode_ImportBpy(LiveNode),
					*Key,
					*ExpectedDescriptor.PathAsText,
					*ActualDescriptor->PathAsText));
			}

			if (!ExpectedDescriptor.TypeName.IsEmpty() &&
				!ActualDescriptor->TypeName.Equals(ExpectedDescriptor.TypeName, ESearchCase::CaseSensitive))
			{
				OutMismatches.Add(FString::Printf(
					TEXT("binding_type_mismatch uid=%s graph=%s node=%s key=%s expected=%s actual=%s"),
					*SerializedUid,
					LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
					*DescribeNode_ImportBpy(LiveNode),
					*Key,
					*ExpectedDescriptor.TypeName,
					*ActualDescriptor->TypeName));
			}

			if (ExpectedDescriptor.bIsBound && !ActualDescriptor->bIsBound)
			{
				OutMismatches.Add(FString::Printf(
					TEXT("binding_bound_flag_mismatch uid=%s graph=%s node=%s key=%s expected=true actual=false"),
					*SerializedUid,
					LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
					*DescribeNode_ImportBpy(LiveNode),
					*Key));
			}

			const bool bExpectedFunctionBinding =
				ExpectedDescriptor.TypeName.Equals(TEXT("Function"), ESearchCase::CaseSensitive);
			const bool bActualFunctionBinding =
				ActualDescriptor->TypeName.Equals(TEXT("Function"), ESearchCase::CaseSensitive);
			if (bExpectedFunctionBinding || bActualFunctionBinding)
			{
				const FString FunctionRef =
					!ActualDescriptor->PathAsText.IsEmpty()
						? ActualDescriptor->PathAsText
						: ExpectedDescriptor.PathAsText;
				UFunction* ResolvedFunction =
					ResolveSelfContextFunction_ImportBpy(LiveNode->GetGraph(), FunctionRef);
				if (!ResolvedFunction)
				{
					OutMismatches.Add(FString::Printf(
						TEXT("binding_function_unresolved uid=%s graph=%s node=%s key=%s function=%s"),
						*SerializedUid,
						LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
						*DescribeNode_ImportBpy(LiveNode),
						*Key,
						*FunctionRef));
					continue;
				}

				const bool bIsPure = (ResolvedFunction->FunctionFlags & FUNC_BlueprintPure) != 0;
				const bool bThreadSafe =
					ResolvedFunction->HasMetaData(FBlueprintMetadata::MD_ThreadSafe) ||
					ResolvedFunction->GetMetaData(FBlueprintMetadata::MD_ThreadSafe).Equals(TEXT("true"), ESearchCase::IgnoreCase);
				const bool bRequiresThreadSafe =
					ExpectedDescriptor.bRequiresThreadSafe ||
					ActualDescriptor->bRequiresThreadSafe;
				if (!bIsPure || (bRequiresThreadSafe && !bThreadSafe))
				{
					OutMismatches.Add(FString::Printf(
						TEXT("binding_function_metadata_invalid uid=%s graph=%s node=%s key=%s function=%s pure=%d thread_safe=%d requires_thread_safe=%d"),
						*SerializedUid,
						LiveNode->GetGraph() ? *LiveNode->GetGraph()->GetName() : TEXT("<null>"),
						*DescribeNode_ImportBpy(LiveNode),
						*Key,
						*ResolvedFunction->GetName(),
						bIsPure ? 1 : 0,
						bThreadSafe ? 1 : 0,
						bRequiresThreadSafe ? 1 : 0));
				}
			}
		}
	}
}

void EnforceAnimNodeBindingDrivenPinVisibility_ImportBpy(
	UBlueprint* BP,
	int32& OutChangedNodeCount)
{
	// Source exports already carry authoritative ShowPinForProperties flags.
	// Do not mutate pin visibility during import finalization, otherwise valid
	// bound properties (e.g. MotionMatching.BlendTime) disappear from the node UI.
	OutChangedNodeCount = 0;
	(void)BP;
}

void CollectEventAndDelegateBindingMismatches_ImportBpy(
	UBlueprint* BP,
	TArray<FString>& OutMismatches)
{
	OutMismatches.Reset();
	if (!BP)
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	BP->GetAllGraphs(RootGraphs);

	TArray<UEdGraph*> ReachableGraphs;
	TSet<UEdGraph*> VisitedGraphs;
	for (UEdGraph* RootGraph : RootGraphs)
	{
		GatherReachableGraphs_ImportBpy(RootGraph, VisitedGraphs, ReachableGraphs);
	}

	for (UEdGraph* Graph : ReachableGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				const FName MemberName = EventNode->EventReference.GetMemberName();
				if (MemberName.IsNone())
				{
					continue;
				}

				const FString EventName = MemberName.ToString();
				if (!IsAnimNotifyEventName_ImportBpy(EventName))
				{
					continue;
				}

				const bool bIsTransitionNotify = IsAnimNotifyTransitionEventName_ImportBpy(EventName);
				if (!bIsTransitionNotify && !EventNode->bOverrideFunction)
				{
					OutMismatches.Add(FString::Printf(
						TEXT("anim_notify_event_not_override graph=%s node=%s event=%s"),
						*Graph->GetName(),
						*DescribeNode_ImportBpy(EventNode),
						*EventName));
				}

				if (bIsTransitionNotify && !EventNode->bInternalEvent)
				{
					OutMismatches.Add(FString::Printf(
						TEXT("anim_notify_transition_not_internal graph=%s node=%s event=%s"),
						*Graph->GetName(),
						*DescribeNode_ImportBpy(EventNode),
						*EventName));
				}
				else if (!bIsTransitionNotify)
				{
					UFunction* const ResolvedEventFunction =
						ResolveSelfContextFunction_ImportBpy(Graph, EventName);
					if (!ResolvedEventFunction)
					{
						OutMismatches.Add(FString::Printf(
							TEXT("anim_notify_event_unresolved graph=%s node=%s event=%s"),
							*Graph->GetName(),
							*DescribeNode_ImportBpy(EventNode),
							*EventName));
					}
				}

				continue;
			}

			if (const UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
			{
				if (CreateDelegateNode->GetFunctionName().IsNone())
				{
					OutMismatches.Add(FString::Printf(
						TEXT("create_delegate_unbound graph=%s node=%s"),
						*Graph->GetName(),
						*DescribeNode_ImportBpy(CreateDelegateNode)));
				}
			}
		}
	}
}

bool IsCompileWarningWhitelisted_ImportBpy(const FString& WarningText)
{
	return WarningText.IsEmpty();
}

bool ValidateRoundtripAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TArray<FString>& CompileWarnings,
	const TCHAR* StageName,
	FString& OutError)
{
	if (!BP || !Root.IsValid())
	{
		OutError = TEXT("Invalid context for ValidateRoundtrip");
		return false;
	}

	FRoundtripTopologyStats_ImportBpy ExpectedStats;
	if (!CollectSerializedRoundtripStatsFromRootJson_ImportBpy(Root, ExpectedStats, OutError))
	{
		return false;
	}

	FRoundtripTopologyStats_ImportBpy ActualStats;
	CollectActualRoundtripStatsFromBlueprint_ImportBpy(BP, ActualStats);

	TArray<FString> Mismatches;
	auto AddCountMismatch = [&Mismatches](const FString& Key, const int32 Expected, const int32 Actual)
	{
		if (Expected != Actual)
		{
			Mismatches.Add(FString::Printf(TEXT("%s expected=%d actual=%d"), *Key, Expected, Actual));
		}
	};

	AddCountMismatch(TEXT("root_graph_count"), ExpectedStats.RootGraphCount, ActualStats.RootGraphCount);
	AddCountMismatch(TEXT("root_node_count"), ExpectedStats.RootGraphNodeCount, ActualStats.RootGraphNodeCount);
	AddCountMismatch(TEXT("root_connection_count"), ExpectedStats.RootGraphConnectionCount, ActualStats.RootGraphConnectionCount);
	AddCountMismatch(TEXT("function_graph_count"), ExpectedStats.FunctionGraphCount, ActualStats.FunctionGraphCount);
	AddCountMismatch(TEXT("interface_count"), ExpectedStats.InterfaceCount, ActualStats.InterfaceCount);
	AddCountMismatch(TEXT("variable_count"), ExpectedStats.VariableCount, ActualStats.VariableCount);

	for (const TPair<FString, int32>& ExpectedNodeCountByGuid : ExpectedStats.RecursiveGraphNodeCountsByGuid)
	{
		const int32* ActualNodeCount = ActualStats.RecursiveGraphNodeCountsByGuid.Find(ExpectedNodeCountByGuid.Key);
		if (!ActualNodeCount)
		{
			Mismatches.Add(FString::Printf(
				TEXT("missing_recursive_graph_by_guid guid=%s expected_nodes=%d"),
				*ExpectedNodeCountByGuid.Key,
				ExpectedNodeCountByGuid.Value));
			continue;
		}

		AddCountMismatch(
			FString::Printf(TEXT("recursive_graph_nodes[%s]"), *ExpectedNodeCountByGuid.Key),
			ExpectedNodeCountByGuid.Value,
			*ActualNodeCount);

		const int32* ExpectedConnectionCount =
			ExpectedStats.RecursiveGraphConnectionCountsByGuid.Find(ExpectedNodeCountByGuid.Key);
		const int32* ActualConnectionCount =
			ActualStats.RecursiveGraphConnectionCountsByGuid.Find(ExpectedNodeCountByGuid.Key);
		if (!ExpectedConnectionCount || !ActualConnectionCount)
		{
			Mismatches.Add(FString::Printf(
				TEXT("missing_recursive_graph_connections_by_guid guid=%s expected=%d actual=%d"),
				*ExpectedNodeCountByGuid.Key,
				ExpectedConnectionCount ? *ExpectedConnectionCount : -1,
				ActualConnectionCount ? *ActualConnectionCount : -1));
			continue;
		}

		AddCountMismatch(
			FString::Printf(TEXT("recursive_graph_connections[%s]"), *ExpectedNodeCountByGuid.Key),
			*ExpectedConnectionCount,
			*ActualConnectionCount);

		const int32* ExpectedAnimNodeCount =
			ExpectedStats.RecursiveAnimNodeCountsByGuid.Find(ExpectedNodeCountByGuid.Key);
		const int32* ActualAnimNodeCount =
			ActualStats.RecursiveAnimNodeCountsByGuid.Find(ExpectedNodeCountByGuid.Key);
		if (!ExpectedAnimNodeCount || !ActualAnimNodeCount)
		{
			Mismatches.Add(FString::Printf(
				TEXT("missing_recursive_anim_nodes_by_guid guid=%s expected=%d actual=%d"),
				*ExpectedNodeCountByGuid.Key,
				ExpectedAnimNodeCount ? *ExpectedAnimNodeCount : -1,
				ActualAnimNodeCount ? *ActualAnimNodeCount : -1));
			continue;
		}

		AddCountMismatch(
			FString::Printf(TEXT("recursive_anim_nodes[%s]"), *ExpectedNodeCountByGuid.Key),
			*ExpectedAnimNodeCount,
			*ActualAnimNodeCount);
	}

	for (const TPair<FString, int32>& ExpectedNodeCount : ExpectedStats.RootGraphNodeCounts)
	{
		const int32* ActualNodeCount = ActualStats.RootGraphNodeCounts.Find(ExpectedNodeCount.Key);
		if (!ActualNodeCount)
		{
			Mismatches.Add(FString::Printf(
				TEXT("missing_root_graph_for_nodes name=%s expected=%d actual=<missing>"),
				*ExpectedNodeCount.Key,
				ExpectedNodeCount.Value));
			continue;
		}
		AddCountMismatch(
			FString::Printf(TEXT("root_graph_nodes[%s]"), *ExpectedNodeCount.Key),
			ExpectedNodeCount.Value,
			*ActualNodeCount);
	}

	for (const TPair<FString, int32>& ExpectedConnectionCount : ExpectedStats.RootGraphConnectionCounts)
	{
		const int32* ActualConnectionCount = ActualStats.RootGraphConnectionCounts.Find(ExpectedConnectionCount.Key);
		if (!ActualConnectionCount)
		{
			Mismatches.Add(FString::Printf(
				TEXT("missing_root_graph_for_connections name=%s expected=%d actual=<missing>"),
				*ExpectedConnectionCount.Key,
				ExpectedConnectionCount.Value));
			continue;
		}
		AddCountMismatch(
			FString::Printf(TEXT("root_graph_connections[%s]"), *ExpectedConnectionCount.Key),
			ExpectedConnectionCount.Value,
			*ActualConnectionCount);
	}

	TSharedPtr<FJsonObject> LiveRoot = UBPDirectExporter::SerializeBlueprintToJson(BP);
	if (!LiveRoot.IsValid())
	{
		OutError = TEXT("ValidateRoundtrip failed: cannot serialize live blueprint to JSON");
		return false;
	}

	int32 ExpectedClassDefaultCount = 0;
	int32 ActualClassDefaultCount = 0;
	const TArray<TSharedPtr<FJsonValue>>* SourceClassDefaultsArr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* LiveClassDefaultsArr = nullptr;
	const bool bSourceHasClassDefaults =
		Root->TryGetArrayField(TEXT("class_defaults"), SourceClassDefaultsArr) &&
		SourceClassDefaultsArr != nullptr;
	const bool bLiveHasClassDefaults =
		LiveRoot->TryGetArrayField(TEXT("class_defaults"), LiveClassDefaultsArr) &&
		LiveClassDefaultsArr != nullptr;
	if (bSourceHasClassDefaults && bLiveHasClassDefaults)
	{
		const FString ExpectedClassDefaultHash =
			BuildClassDefaultsHashFromRootJson_ImportBpy(Root, ExpectedClassDefaultCount);
		const FString ActualClassDefaultHash =
			BuildClassDefaultsHashFromRootJson_ImportBpy(LiveRoot, ActualClassDefaultCount);
		if (ExpectedClassDefaultHash != ActualClassDefaultHash)
		{
			Mismatches.Add(FString::Printf(
				TEXT("class_defaults_hash_mismatch expected=%s actual=%s expected_count=%d actual_count=%d"),
				*ExpectedClassDefaultHash,
				*ActualClassDefaultHash,
				ExpectedClassDefaultCount,
				ActualClassDefaultCount));
		}
	}
	else if (bSourceHasClassDefaults && !bLiveHasClassDefaults)
	{
		Mismatches.Add(FString::Printf(
			TEXT("class_defaults_missing_in_live_export path=%s"),
			*BP->GetPathName()));
	}

	if (const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(BP))
	{
		TArray<FString> AnimNodeBindingMismatches;
		CollectAnimNodePropertyBindingMismatches_ImportBpy(BP, Root, AnimNodeBindingMismatches);
		Mismatches.Append(AnimNodeBindingMismatches);

		TArray<FString> FunctionRefBindingMismatches;
		CollectAnimNodeFunctionRefBindingMismatches_ImportBpy(BP, Root, FunctionRefBindingMismatches);
		Mismatches.Append(FunctionRefBindingMismatches);

		TArray<FString> PoseHistoryMismatches;
		CollectMotionMatchingPoseHistoryMismatches_ImportBpy(BP, Root, PoseHistoryMismatches);
		Mismatches.Append(PoseHistoryMismatches);

		TArray<FString> StateMachineBindingMismatches;
		CollectStateMachineBindingContractMismatches_ImportBpy(
			Root,
			LiveRoot,
			StateMachineBindingMismatches);
		Mismatches.Append(StateMachineBindingMismatches);

		TArray<FString> LinkedAnimLayerMismatches;
		CollectLinkedAnimLayerContractMismatches_ImportBpy(
			Root,
			LiveRoot,
			LinkedAnimLayerMismatches);
		Mismatches.Append(LinkedAnimLayerMismatches);

		TArray<FString> StateAliasMismatches;
		CollectStateMachineAliasNodeMismatches_ImportBpy(Root, LiveRoot, StateAliasMismatches);
		Mismatches.Append(StateAliasMismatches);

		TArray<FString> EventAndDelegateMismatches;
		CollectEventAndDelegateBindingMismatches_ImportBpy(BP, EventAndDelegateMismatches);
		Mismatches.Append(EventAndDelegateMismatches);
	}

	for (const FString& WarningText : CompileWarnings)
	{
		if (!WarningText.IsEmpty() && !IsCompileWarningWhitelisted_ImportBpy(WarningText))
		{
			Mismatches.Add(FString::Printf(TEXT("compile_warning_not_whitelisted=%s"), *WarningText));
		}
	}

	if (Mismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("ValidateRoundtrip failed (%s): %s"),
			StageName ? StageName : TEXT("unknown"),
			*FString::Join(Mismatches, TEXT("; ")));
		return false;
	}

	return true;
}

void CollectSerializedAnimNodeJsonByUidRecursive_ImportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	TSet<FString>& VisitedGraphGuids,
	TMap<FString, TSharedPtr<FJsonObject>>& OutNodeByUid)
{
	if (!GraphObj.IsValid())
	{
		return;
	}

	FString GraphGuid;
	if (GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuid) && !GraphGuid.IsEmpty())
	{
		if (VisitedGraphGuids.Contains(GraphGuid))
		{
			return;
		}
		VisitedGraphGuids.Add(GraphGuid);
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return;
	}

	static const TCHAR* NestedGraphFields[] = {
		TEXT("BoundGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};

	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeObj.IsValid())
		{
			continue;
		}

		FString NodeClassName;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
		if (NodeClassName.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
		{
			FString Uid;
			if (NodeObj->TryGetStringField(TEXT("uid"), Uid) && !Uid.IsEmpty() && !OutNodeByUid.Contains(Uid))
			{
				OutNodeByUid.Add(Uid, NodeObj);
			}
		}

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		if (!NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
		{
			continue;
		}

		for (const TCHAR* FieldName : NestedGraphFields)
		{
			FString NestedGraphJson;
			if (!(*NodePropsObj)->TryGetStringField(FieldName, NestedGraphJson) || NestedGraphJson.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphObj;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NestedGraphJson);
			if (!FJsonSerializer::Deserialize(Reader, NestedGraphObj) || !NestedGraphObj.IsValid())
			{
				continue;
			}

			CollectSerializedAnimNodeJsonByUidRecursive_ImportBpy(NestedGraphObj, VisitedGraphGuids, OutNodeByUid);
		}
	}
}

bool RebindAnimNodeFunctionRefPropertyFromSerializedText_ImportBpy(
	UEdGraphNode* Node,
	const FString& PropertyName,
	const FString& SerializedText,
	bool& bOutChanged,
	FString& OutError)
{
	bOutChanged = false;
	if (!Node || !Node->GetClass() || PropertyName.IsEmpty())
	{
		return true;
	}

	FStructProperty* const StructProperty =
		FindFProperty<FStructProperty>(Node->GetClass(), FName(*PropertyName));
	if (!StructProperty)
	{
		return true;
	}

	void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Node);
	if (!ValuePtr)
	{
		OutError = FString::Printf(
			TEXT("Cannot access anim-node function ref property '%s' on node %s"),
			*PropertyName,
			*DescribeNode_ImportBpy(Node));
		return false;
	}

	FString BeforeText;
	StructProperty->ExportTextItem_Direct(BeforeText, ValuePtr, nullptr, Node, PPF_None);

	const FString RemappedText = RemapBlueprintReferencesInSerializedText_ImportBpy(SerializedText);
	StructProperty->ImportText_Direct(*RemappedText, ValuePtr, Node, PPF_None);

	if (StructProperty->Struct == FMemberReference::StaticStruct())
	{
		FMemberReference* const MemberReference =
			StructProperty->ContainerPtrToValuePtr<FMemberReference>(Node);
		if (MemberReference)
		{
			const FName FunctionName = MemberReference->GetMemberName();
			const FGuid SerializedMemberGuid = MemberReference->GetMemberGuid();
			if (!FunctionName.IsNone())
			{
				UFunction* const ResolvedFunction =
					ResolveSelfContextFunction_ImportBpy(Node->GetGraph(), FunctionName.ToString());
				if (ResolvedFunction)
				{
					UClass* const OwnerClass = ResolvedFunction->GetOwnerClass();
					UClass* const MostUpToDateOwnerClass =
						OwnerClass ? FBlueprintEditorUtils::GetMostUpToDateClass(OwnerClass) : nullptr;

					FGuid FunctionGuid;
					if (MostUpToDateOwnerClass || OwnerClass)
					{
						FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
							MostUpToDateOwnerClass ? MostUpToDateOwnerClass : OwnerClass,
							FunctionName,
							FunctionGuid);
					}

					const bool bForceSelfContext =
						MemberReference->IsSelfContext() ||
						RemappedText.Contains(TEXT("bSelfContext=True"), ESearchCase::IgnoreCase);
					if (bForceSelfContext)
					{
						// Preserve serialized guid when available to avoid anim-node function
						// reference drift across roundtrips.
						if (SerializedMemberGuid.IsValid())
						{
							MemberReference->SetSelfMember(FunctionName, SerializedMemberGuid);
						}
						else if (FunctionGuid.IsValid())
						{
							MemberReference->SetSelfMember(FunctionName, FunctionGuid);
						}
						else
						{
							MemberReference->SetSelfMember(FunctionName);
						}
					}
					else if (OwnerClass)
					{
						if (SerializedMemberGuid.IsValid())
						{
							MemberReference->SetExternalMember(FunctionName, OwnerClass, SerializedMemberGuid);
						}
						else if (FunctionGuid.IsValid())
						{
							MemberReference->SetExternalMember(FunctionName, OwnerClass, FunctionGuid);
						}
						else
						{
							MemberReference->SetExternalMember(FunctionName, OwnerClass);
						}
					}
				}
			}
		}
	}

	FString AfterText;
	StructProperty->ExportTextItem_Direct(AfterText, ValuePtr, nullptr, Node, PPF_None);
	bOutChanged = !BeforeText.Equals(AfterText, ESearchCase::CaseSensitive);
	return true;
}

bool RebindAnimNodeFunctionRefsFromNodeJson_ImportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeJson,
	bool& bOutChanged,
	FString& OutError)
{
	bOutChanged = false;
	if (!Node || !Node->GetClass() || !NodeJson.IsValid())
	{
		return true;
	}

	const FString NodeClassName = Node->GetClass()->GetName();
	if (!NodeClassName.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj || !NodePropsObj->IsValid())
	{
		return true;
	}

	static const TCHAR* FunctionRefFieldNames[] = {
		TEXT("InitialUpdateFunction"),
		TEXT("BecomeRelevantFunction"),
		TEXT("UpdateFunction"),
		TEXT("OnMotionMatchingStateUpdatedFunction")
	};

	for (const TCHAR* FieldName : FunctionRefFieldNames)
	{
		const TSharedPtr<FJsonValue>* FieldValue = (*NodePropsObj)->Values.Find(FieldName);
		if (!FieldValue || !FieldValue->IsValid())
		{
			continue;
		}

		const FString FieldText = (*FieldValue)->AsString();
		bool bFieldChanged = false;
		if (!RebindAnimNodeFunctionRefPropertyFromSerializedText_ImportBpy(
				Node,
				FieldName,
				FieldText,
				bFieldChanged,
				OutError))
		{
			return false;
		}

		bOutChanged = bOutChanged || bFieldChanged;
	}

	return true;
}

bool RebindAnimNodeFunctionReferencesFromSerializedGraphs_ImportBpy(
	UBlueprint* BP,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	bool& bOutAnyChanges,
	FString& OutError)
{
	bOutAnyChanges = false;
	if (!BP || SortedGraphs.Num() == 0)
	{
		return true;
	}

	TSet<FString> VisitedGraphGuids;
	TMap<FString, TSharedPtr<FJsonObject>> SerializedAnimNodeByUid;
	for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
	{
		CollectSerializedAnimNodeJsonByUidRecursive_ImportBpy(
			GraphObj,
			VisitedGraphGuids,
			SerializedAnimNodeByUid);
	}

	const int32 SerializedAnimNodeCount = SerializedAnimNodeByUid.Num();
	int32 MatchedLiveNodeCount = 0;
	int32 MissingLiveNodeCount = 0;
	int32 ChangedNodeCount = 0;
	TArray<FString> MissingUidSamples;
	MissingUidSamples.Reserve(5);

	for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : SerializedAnimNodeByUid)
	{
		UEdGraphNode* LiveNode = FindImportedAnimNodeFromSerializedJson_ImportBpy(BP, Pair.Value);
		if (!LiveNode)
		{
			++MissingLiveNodeCount;
			if (MissingUidSamples.Num() < 5)
			{
				MissingUidSamples.Add(FString::Printf(
					TEXT("%s|%s"),
					*Pair.Key,
					*GetSerializedAnimNodeGuid_ImportBpy(Pair.Value)));
			}
			continue;
		}
		++MatchedLiveNodeCount;

		bool bNodeChanged = false;
		if (!RebindAnimNodeFunctionRefsFromNodeJson_ImportBpy(LiveNode, Pair.Value, bNodeChanged, OutError))
		{
			return false;
		}

		if (bNodeChanged)
		{
			LiveNode->Modify();
			bOutAnyChanges = true;
			++ChangedNodeCount;
		}
	}

	if (bOutAnyChanges)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}

	const FString MissingSampleText =
		MissingUidSamples.Num() > 0
			? FString::Join(MissingUidSamples, TEXT(","))
			: TEXT("<none>");
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag] AnimNodeFunctionRefRebind target=%s serialized_anim_nodes=%d matched_live=%d missing_live=%d changed=%d missing_uid_samples=%s"),
		*BP->GetPathName(),
		SerializedAnimNodeCount,
		MatchedLiveNodeCount,
		MissingLiveNodeCount,
		ChangedNodeCount,
		*MissingSampleText);

	return true;
}

bool VerifyExportRoundtripAgainstRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& SourceRoot,
	FString& OutError)
{
	if (!BP || !SourceRoot.IsValid())
	{
		OutError = TEXT("Invalid context for VerifyExportRoundtrip");
		return false;
	}

	TSharedPtr<FJsonObject> LiveRoot = UBPDirectExporter::SerializeBlueprintToJson(BP);
	if (!LiveRoot.IsValid())
	{
		OutError = TEXT("VerifyExportRoundtrip failed: cannot serialize imported blueprint");
		return false;
	}

	FString SourceBlueprintPath;
	SourceRoot->TryGetStringField(TEXT("path"), SourceBlueprintPath);
	const FString TargetBlueprintPath = BP->GetPathName();

	const FString CanonicalSource = BuildCanonicalJsonString_ImportBpy(SourceRoot, SourceBlueprintPath);
	const FString CanonicalLive = BuildCanonicalJsonString_ImportBpy(LiveRoot, TargetBlueprintPath);
	if (CanonicalSource == CanonicalLive)
	{
		return true;
	}

	const FString SourceHash = HashTextSha1_ImportBpy(CanonicalSource);
	const FString LiveHash = HashTextSha1_ImportBpy(CanonicalLive);
	const int32 MinLength = FMath::Min(CanonicalSource.Len(), CanonicalLive.Len());
	int32 FirstDiffIndex = INDEX_NONE;
	for (int32 Index = 0; Index < MinLength; ++Index)
	{
		if (CanonicalSource[Index] != CanonicalLive[Index])
		{
			FirstDiffIndex = Index;
			break;
		}
	}
	if (FirstDiffIndex == INDEX_NONE && CanonicalSource.Len() != CanonicalLive.Len())
	{
		FirstDiffIndex = MinLength;
	}

	const auto BuildDiffSnippet = [](const FString& Text, int32 DiffIndex) -> FString
	{
		if (Text.IsEmpty())
		{
			return TEXT("<empty>");
		}
		const int32 SafeIndex = FMath::Clamp(DiffIndex, 0, Text.Len() - 1);
		const int32 SnippetRadius = 160;
		const int32 Start = FMath::Max(0, SafeIndex - SnippetRadius);
		const int32 End = FMath::Min(Text.Len(), SafeIndex + SnippetRadius);
		const int32 Count = FMath::Max(0, End - Start);
		FString Snippet = Text.Mid(Start, Count);
		Snippet.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Snippet.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Snippet.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Snippet;
	};

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][ImportDiag][VerifyRoundtrip] first_diff_index=%d source_snippet=%s live_snippet=%s"),
		FirstDiffIndex,
		*BuildDiffSnippet(CanonicalSource, FirstDiffIndex),
		*BuildDiffSnippet(CanonicalLive, FirstDiffIndex));

	OutError = FString::Printf(
		TEXT("VerifyExportRoundtrip mismatch: source_hash=%s actual_hash=%s source_len=%d actual_len=%d first_diff_index=%d"),
		*SourceHash,
		*LiveHash,
		CanonicalSource.Len(),
		CanonicalLive.Len(),
		FirstDiffIndex);
	return false;
}

bool ReloadBlueprintAssetForPostSaveValidation_ImportBpy(
	const FString& TargetAssetPath,
	UBlueprint*& OutBlueprint,
	FString& OutError)
{
	OutBlueprint = nullptr;

	const FString ObjectPath = NormalizeBlueprintObjectPath_ImportBpy(TargetAssetPath);
	if (ObjectPath.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("Post-save reload validation failed: invalid blueprint path '%s'"),
			*TargetAssetPath);
		return false;
	}

	UBlueprint* ExistingBlueprint = LoadBlueprintAsset_ImportBpy(ObjectPath);
	if (!ExistingBlueprint)
	{
		OutError = FString::Printf(
			TEXT("Post-save reload validation failed: could not load blueprint before reload: %s"),
			*ObjectPath);
		return false;
	}

	UPackage* const Package = ExistingBlueprint->GetOutermost();
	if (!Package)
	{
		OutError = FString::Printf(
			TEXT("Post-save reload validation failed: blueprint has no package: %s"),
			*ObjectPath);
		return false;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[ExportBpy][ImportDiag][PostSaveReload] target=%s package=%s existing_bp=%s"),
		*ObjectPath,
		*Package->GetName(),
		*GetPathNameSafe(ExistingBlueprint));

	// Drain any pending BP compilation work before we attempt a package reload. ReloadPackages
	// is not resilient when the target blueprint is still compiling or carrying a REINST class.
	FBlueprintCompilationManager::FlushCompilationQueue(nullptr);

	const UWorld* const EditorWorld =
		(GIsEditor && GEditor) ? GEditor->GetEditorWorldContext(false).World() : nullptr;
	const bool bEditorWorldAvailable = (EditorWorld != nullptr);
	const bool bEditorBusy =
		GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor);
	const bool bBlueprintStillCompiling = ExistingBlueprint->bBeingCompiled;
	const bool bHasReinstancedClass =
		ExistingBlueprint->GeneratedClass &&
		ExistingBlueprint->GeneratedClass->GetName().StartsWith(TEXT("REINST_"));

	if (!bEditorWorldAvailable || bEditorBusy || bBlueprintStillCompiling || bHasReinstancedClass)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag][PostSaveReload] skipping_reload target=%s "
			     "world=%d busy=%d compiling=%d reinst=%d; using in-memory blueprint for validation."),
			*ObjectPath,
			bEditorWorldAvailable ? 1 : 0,
			bEditorBusy ? 1 : 0,
			bBlueprintStillCompiling ? 1 : 0,
			bHasReinstancedClass ? 1 : 0);

		OutBlueprint = ExistingBlueprint;
		return true;
	}

	const TArray<UPackage*> PackagesToReload{Package};
	FText ReloadErrorText;
	const bool bReloaded = UPackageTools::ReloadPackages(
		PackagesToReload,
		ReloadErrorText,
		EReloadPackagesInteractionMode::AssumePositive);

	if (!ReloadErrorText.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("Post-save reload validation failed while reloading '%s': %s"),
			*ObjectPath,
			*ReloadErrorText.ToString());
		return false;
	}

	if (!bReloaded)
	{
		OutError = FString::Printf(
			TEXT("Post-save reload validation failed: UPackageTools::ReloadPackages returned false for '%s'"),
			*ObjectPath);
		return false;
	}

	ResetAllImportedNodeRegistries_ImportBpy();

	OutBlueprint = LoadBlueprintAsset_ImportBpy(ObjectPath);
	if (!OutBlueprint)
	{
		OutError = FString::Printf(
			TEXT("Post-save reload validation failed: blueprint disappeared after reload: %s"),
			*ObjectPath);
		return false;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[ExportBpy][ImportDiag][PostSaveReload] reloaded_bp=%s package=%s"),
		*GetPathNameSafe(OutBlueprint),
		*GetPathNameSafe(OutBlueprint->GetOutermost()));

	return true;
}

void ScheduleDeferredPostImportDiagnostics_ImportBpy(
	const FString& TargetAssetPath,
	const TSharedPtr<FJsonObject>& Root,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const TArray<FString>& CompileWarnings)
{
	const FString ObjectPath = NormalizeBlueprintObjectPath_ImportBpy(TargetAssetPath);
	if (ObjectPath.IsEmpty() || !Root.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject> RootCopy = Root;
	const TArray<TSharedPtr<FJsonObject>> SortedGraphsCopy = SortedGraphs;
	const TArray<FString> CompileWarningsCopy = CompileWarnings;

	auto ScheduleStage =
		[ObjectPath, RootCopy, SortedGraphsCopy, CompileWarningsCopy](const TCHAR* StageTag, const float DelaySeconds)
	{
		const FString Stage = StageTag ? StageTag : TEXT("PostImportDeferredTick");
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[ObjectPath, RootCopy, SortedGraphsCopy, CompileWarningsCopy, Stage](float)
				{
					UBlueprint* DeferredBP = LoadBlueprintAsset_ImportBpy(ObjectPath);
					if (!DeferredBP)
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] failed_to_load target=%s"),
							*Stage,
							*ObjectPath);
						return false;
					}

					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[ExportBpy][ImportDiag][%s] begin bp=%s"),
						*Stage,
						*GetPathNameSafe(DeferredBP));

					if (Cast<UAnimBlueprint>(DeferredBP))
					{
						LogImportedAnimBlueprintReachableGraphInventory_ImportBpy(
							DeferredBP,
							*Stage);
						LogAnimBlueprintBlendStackReplayState_ImportBpy(
							DeferredBP,
							SortedGraphsCopy,
							*Stage);
						LogSerializedAnimBlueprintBlendStackBindingState_ImportBpy(
							DeferredBP,
							SortedGraphsCopy,
							*Stage);
						LogSerializedAnimNodeUidResolutionAudit_ImportBpy(
							DeferredBP,
							RootCopy,
							*Stage);
					}

					FString ValidateError;
					if (!ValidateImportedBlueprintStructuralParityAgainstRootJson_ImportBpy(
							DeferredBP,
							RootCopy,
							true,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] structural_parity_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					ValidateError.Reset();
					if (!ValidateImportedInterfaceBindingsAgainstRootJson_ImportBpy(
							DeferredBP,
							RootCopy,
							*Stage,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] interface_validation_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					ValidateError.Reset();
					if (!ValidateAnimBlueprintPoseHistoryContractAgainstRootJson_ImportBpy(
							DeferredBP,
							RootCopy,
							*Stage,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] pose_history_validation_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					ValidateError.Reset();
					if (!ValidateAnimBlueprintStateMachineBindingContractAgainstRootJson_ImportBpy(
							DeferredBP,
							RootCopy,
							*Stage,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] state_machine_binding_validation_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					ValidateError.Reset();
					if (!ValidateAnimBlueprintStateMachineEntryBindingPresence_ImportBpy(
							DeferredBP,
							*Stage,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] state_machine_entry_presence_validation_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					ValidateError.Reset();
					if (!ValidateAnimBlueprintLinkedAnimLayerContractAgainstRootJson_ImportBpy(
							DeferredBP,
							RootCopy,
							*Stage,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] linked_anim_layer_validation_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					ValidateError.Reset();
					if (!ValidateRoundtripAgainstRootJson_ImportBpy(
							DeferredBP,
							RootCopy,
							CompileWarningsCopy,
							*Stage,
							ValidateError))
					{
						UE_LOG(
							LogTemp,
							Error,
							TEXT("[ExportBpy][ImportDiag][%s] roundtrip_validation_failed error=%s"),
							*Stage,
							*ValidateError);
					}

					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[ExportBpy][ImportDiag][%s] end bp=%s"),
						*Stage,
						*GetPathNameSafe(DeferredBP));
					return false;
				}),
			DelaySeconds);
	};

	ScheduleStage(TEXT("PostImportDeferredTick"), 0.0f);
	ScheduleStage(TEXT("PostImportDelayedTick"), 0.25f);
}

bool RunPostSaveReloadValidation_ImportBpy(
	const FString& TargetAssetPath,
	const TSharedPtr<FJsonObject>& Root,
	const TArray<TSharedPtr<FJsonObject>>& SortedGraphs,
	const TArray<FString>& CompileWarnings,
	bool bStrictImportMode,
	UBlueprint*& InOutBlueprint,
	FString& OutError)
{
	UBlueprint* ReloadedBP = nullptr;
	if (!ReloadBlueprintAssetForPostSaveValidation_ImportBpy(TargetAssetPath, ReloadedBP, OutError))
	{
		return false;
	}

	ScheduleDeferredPostImportDiagnostics_ImportBpy(
		TargetAssetPath,
		Root,
		SortedGraphs,
		CompileWarnings);

	if (Cast<UAnimBlueprint>(ReloadedBP))
	{
		int32 RepairedStateEntryBindingsAfterReload = 0;
		if (!RepairAnimBlueprintStateMachineEntryBindings_ImportBpy(
				ReloadedBP,
				RepairedStateEntryBindingsAfterReload,
				OutError,
				&Root))
		{
			return false;
		}
		if (RepairedStateEntryBindingsAfterReload > 0)
		{
			if (bStrictImportMode)
			{
				OutError = FString::Printf(
					TEXT("State entry bindings drifted after reload and required repair (%d). Import aborted in strict mode."),
					RepairedStateEntryBindingsAfterReload);
				return false;
			}
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag] Non-strict import: state entry bindings drifted after reload and were repaired (%d)."),
				RepairedStateEntryBindingsAfterReload);
		}

		LogAnimBlueprintStateMachineEntryBindings_ImportBpy(
			ReloadedBP,
			TEXT("post_save_reload"));

		LogImportedAnimBlueprintReachableGraphInventory_ImportBpy(
			ReloadedBP,
			TEXT("PostSaveReload"));
		LogAnimBlueprintBlendStackReplayState_ImportBpy(ReloadedBP, SortedGraphs, TEXT("PostSaveReload"));
		LogSerializedAnimBlueprintBlendStackBindingState_ImportBpy(ReloadedBP, SortedGraphs, TEXT("PostSaveReload"));
		LogSerializedAnimNodeUidResolutionAudit_ImportBpy(
			ReloadedBP,
			Root,
			TEXT("PostSaveReload"));

		if (!ReplayAndValidateBlueprintDefaultsContract_ImportBpy(
				ReloadedBP,
				Root,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}

		if (!ValidateImportedAnimBlueprintAgainstSourceAsset_ImportBpy(
				ReloadedBP,
				GCurrentImportSourceBlueprintPath_ImportBpy,
				TEXT("post_save_reload_source_contract"),
				OutError))
		{
			if (bStrictImportMode)
			{
				return false;
			}
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag] Non-strict import: ignored post_save_reload_source_contract mismatch: %s"),
				*OutError);
			OutError.Reset();
		}
	}

	if (bStrictImportMode)
	{
		if (!ValidateImportedBlueprintStructuralParityAgainstRootJson_ImportBpy(
				ReloadedBP,
				Root,
				true,
				OutError))
		{
			return false;
		}

		if (!ValidateImportedInterfaceBindingsAgainstRootJson_ImportBpy(
				ReloadedBP,
				Root,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintPoseHistoryContractAgainstRootJson_ImportBpy(
				ReloadedBP,
				Root,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintStateMachineBindingContractAgainstRootJson_ImportBpy(
				ReloadedBP,
				Root,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintStateMachineEntryBindingPresence_ImportBpy(
				ReloadedBP,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintLinkedAnimLayerContractAgainstRootJson_ImportBpy(
				ReloadedBP,
				Root,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}

		if (!ValidateRoundtripAgainstRootJson_ImportBpy(
				ReloadedBP,
				Root,
				CompileWarnings,
				TEXT("post_save_reload"),
				OutError))
		{
			return false;
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag] Non-strict import: skipped post_save_reload strict contract validation gates."));
	}

	InOutBlueprint = ReloadedBP;
	return true;
}

bool ReplayAndValidateBlueprintDefaultsContract_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* Stage,
	FString& OutError)
{
	if (!BP || !Root.IsValid())
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid())
		{
			continue;
		}

		UEdGraph* Graph = nullptr;
		FString GraphType;
		FString GraphName;
		if (!EnsureGraphExists_ImportBpy(BP, GraphObj, Graph, GraphType, GraphName, OutError))
		{
			OutError = FString::Printf(
				TEXT("Defaults contract (%s): cannot resolve graph: %s"),
				Stage ? Stage : TEXT("unknown"),
				*OutError);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr || NodesArr->Num() == 0)
		{
			continue;
		}

		TMap<FString, UEdGraphNode*> NodeMap;
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString NodeUid;
			if (!NodeObj->TryGetStringField(TEXT("uid"), NodeUid) || NodeUid.IsEmpty())
			{
				continue;
			}

			UEdGraphNode* LiveNode = FindImportedNodeBySerializedUid_ImportBpy(BP, NodeUid);
			if (!LiveNode)
			{
				FGuid ParsedGuid;
				if (TryParseGuid_ImportBpy(NodeUid, ParsedGuid))
				{
					LiveNode = FindImportedNodeByGuidScan_ImportBpy(BP, ParsedGuid);
				}
			}

			if (LiveNode)
			{
				NodeMap.Add(NodeUid, LiveNode);
			}
		}

		if (!ReplayAndValidateSerializedNodeDefaults_ImportBpy(NodesArr, NodeMap, GraphName, OutError))
		{
			OutError = FString::Printf(
				TEXT("Defaults contract (%s) failed on graph '%s': %s"),
				Stage ? Stage : TEXT("unknown"),
				*GraphName,
				*OutError);
			return false;
		}
	}

	TArray<TSharedPtr<FJsonValue>> MissingGraphs;
	TArray<TSharedPtr<FJsonValue>> MissingDefaultKeys;
	TArray<TSharedPtr<FJsonValue>> DefaultMismatches;
	if (!ValidateBlueprintDefaultsAgainstRootJson_ImportBpy(
			BP,
			Root,
			MissingGraphs,
			MissingDefaultKeys,
			DefaultMismatches,
			OutError))
	{
		OutError = FString::Printf(
			TEXT("Defaults contract (%s) validation failed: %s"),
			Stage ? Stage : TEXT("unknown"),
			*OutError);
		return false;
	}

	if (MissingGraphs.Num() > 0 || MissingDefaultKeys.Num() > 0 || DefaultMismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("Defaults contract mismatch (%s): missing_graphs=%d missing_default_keys=%d default_mismatches=%d"),
			Stage ? Stage : TEXT("unknown"),
			MissingGraphs.Num(),
			MissingDefaultKeys.Num(),
			DefaultMismatches.Num());
		return false;
	}

	return true;
}

UEdGraph* FindFunctionGraphByName_ImportBpy(UBlueprint* BP, const FName& FunctionName)
{
	if (!BP || FunctionName.IsNone())
	{
		return nullptr;
	}

	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FunctionName)
		{
			return Graph;
		}
	}
	return nullptr;
}

TMap<const UEdGraphNode*, FString> BuildSemanticNodeIdsForGraph_ImportBpy(UEdGraph* Graph)
{
	TMap<const UEdGraphNode*, FString> Result;
	if (!Graph)
	{
		return Result;
	}

	TMap<FString, int32> SeenCounts;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		if (Node->NodeGuid.IsValid())
		{
			Result.Add(Node, FString::Printf(TEXT("guid:%s"), *Node->NodeGuid.ToString(EGuidFormats::Digits)));
			continue;
		}

		const FString ClassName = Node->GetClass()->GetName();
		const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().TrimStartAndEnd();
		const FString BaseKey = FString::Printf(TEXT("%s|%s"), *ClassName, *Title);
		int32& Ordinal = SeenCounts.FindOrAdd(BaseKey);
		++Ordinal;
		Result.Add(Node, FString::Printf(TEXT("%s#%d"), *BaseKey, Ordinal));
	}

	return Result;
}

void CollectExecEdgesForGraph_ImportBpy(UEdGraph* Graph, TSet<FString>& OutEdges)
{
	OutEdges.Reset();
	if (!Graph)
	{
		return;
	}

	const TMap<const UEdGraphNode*, FString> NodeIds = BuildSemanticNodeIdsForGraph_ImportBpy(Graph);
	auto IsExecRerouteNode = [](const UEdGraphNode* Node) -> bool
	{
		return Node && Node->IsA<UK2Node_Knot>();
	};

	auto GatherResolvedExecTargets =
		[&](UEdGraphPin* StartPin, TSet<const UEdGraphNode*>& OutTargets)
	{
		OutTargets.Reset();
		if (!StartPin)
		{
			return;
		}

		TArray<UEdGraphPin*> Stack;
		TSet<const UEdGraphPin*> VisitedPins;
		Stack.Add(StartPin);

		while (Stack.Num() > 0)
		{
			UEdGraphPin* Current = Stack.Pop(EAllowShrinking::No);
			if (!Current || VisitedPins.Contains(Current))
			{
				continue;
			}
			VisitedPins.Add(Current);

			UEdGraphNode* Owner = Current->GetOwningNode();
			if (!Owner)
			{
				continue;
			}

			if (!IsExecRerouteNode(Owner))
			{
				if (Current->Direction == EGPD_Input)
				{
					OutTargets.Add(Owner);
				}
				continue;
			}

			for (UEdGraphPin* ReroutePin : Owner->Pins)
			{
				if (!ReroutePin ||
					ReroutePin->Direction != EGPD_Output ||
					ReroutePin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
				{
					continue;
				}

				for (UEdGraphPin* Next : ReroutePin->LinkedTo)
				{
					if (Next)
					{
						Stack.Add(Next);
					}
				}
			}
		}
	};

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		if (IsExecRerouteNode(Node))
		{
			continue;
		}

		const FString* SrcId = NodeIds.Find(Node);
		if (!SrcId)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin ||
				Pin->Direction != EGPD_Output ||
				Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin)
				{
					continue;
				}

				TSet<const UEdGraphNode*> ResolvedTargets;
				GatherResolvedExecTargets(LinkedPin, ResolvedTargets);
				for (const UEdGraphNode* TargetNode : ResolvedTargets)
				{
					const FString* DstId = NodeIds.Find(TargetNode);
					if (!DstId)
					{
						continue;
					}

					OutEdges.Add(FString::Printf(
						TEXT("%s -> %s @ %s"),
						**SrcId,
						**DstId,
						*Pin->PinName.ToString()));
				}
			}
		}
	}
}

bool ValidateFunctionExecTopologyParityAgainstSource_ImportBpy(
	UBlueprint* SourceBP,
	UBlueprint* ImportedBP,
	const FName& FunctionName,
	const TCHAR* Stage,
	FString& OutError)
{
	if (!SourceBP || !ImportedBP || FunctionName.IsNone())
	{
		return true;
	}

	UEdGraph* SourceGraph = FindFunctionGraphByName_ImportBpy(SourceBP, FunctionName);
	UEdGraph* ImportedGraph = FindFunctionGraphByName_ImportBpy(ImportedBP, FunctionName);
	if (!SourceGraph || !ImportedGraph)
	{
		OutError = FString::Printf(
			TEXT("Source topology validation (%s) missing function graph '%s' (source=%s imported=%s)"),
			Stage ? Stage : TEXT("unknown"),
			*FunctionName.ToString(),
			SourceGraph ? TEXT("yes") : TEXT("no"),
			ImportedGraph ? TEXT("yes") : TEXT("no"));
		return false;
	}

	TSet<FString> SourceEdges;
	TSet<FString> ImportedEdges;
	CollectExecEdgesForGraph_ImportBpy(SourceGraph, SourceEdges);
	CollectExecEdgesForGraph_ImportBpy(ImportedGraph, ImportedEdges);

	TArray<FString> MissingEdges;
	TArray<FString> ExtraEdges;
	for (const FString& Edge : SourceEdges)
	{
		if (!ImportedEdges.Contains(Edge))
		{
			MissingEdges.Add(Edge);
		}
	}
	for (const FString& Edge : ImportedEdges)
	{
		if (!SourceEdges.Contains(Edge))
		{
			ExtraEdges.Add(Edge);
		}
	}

	if (MissingEdges.Num() > 0 || ExtraEdges.Num() > 0)
	{
		const FString MissingSample = MissingEdges.Num() > 0 ? MissingEdges[0] : TEXT("<none>");
		const FString ExtraSample = ExtraEdges.Num() > 0 ? ExtraEdges[0] : TEXT("<none>");
		OutError = FString::Printf(
			TEXT("Source topology mismatch (%s) function '%s': missing_exec_edges=%d extra_exec_edges=%d missing_sample=%s extra_sample=%s"),
			Stage ? Stage : TEXT("unknown"),
			*FunctionName.ToString(),
			MissingEdges.Num(),
			ExtraEdges.Num(),
			*MissingSample,
			*ExtraSample);
		return false;
	}

	return true;
}

FString BuildSerializedGraphIdentity_ImportBpy(const TSharedPtr<FJsonObject>& GraphObj)
{
	if (!GraphObj.IsValid())
	{
		return FString();
	}

	FString GraphGuid;
	GraphObj->TryGetStringField(TEXT("graph_guid"), GraphGuid);
	if (!GraphGuid.IsEmpty())
	{
		return FString::Printf(TEXT("guid:%s"), *GraphGuid);
	}

	FString GraphName;
	FString GraphType;
	FString GraphOuter;
	FString GraphOuterNode;
	GraphObj->TryGetStringField(TEXT("name"), GraphName);
	GraphObj->TryGetStringField(TEXT("graph_type"), GraphType);
	GraphObj->TryGetStringField(TEXT("graph_outer"), GraphOuter);
	GraphObj->TryGetStringField(TEXT("graph_outer_node"), GraphOuterNode);
	return FString::Printf(
		TEXT("name:%s|type:%s|outer:%s|owner:%s"),
		*GraphName,
		*GraphType,
		*GraphOuter,
		*GraphOuterNode);
}

FString BuildSerializedNodeIdentity_ImportBpy(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (!NodeObj.IsValid())
	{
		return TEXT("<invalid>");
	}

	FString NodeGuid;
	NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuid);
	if (!NodeGuid.IsEmpty())
	{
		return FString::Printf(TEXT("guid:%s"), *NodeGuid);
	}

	FString Uid;
	NodeObj->TryGetStringField(TEXT("uid"), Uid);
	if (!Uid.IsEmpty())
	{
		return FString::Printf(TEXT("uid:%s"), *Uid);
	}

	FString NodeClass;
	FString MemberName;
	FString FunctionRef;
	FString TargetType;
	NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);
	NodeObj->TryGetStringField(TEXT("member_name"), MemberName);
	NodeObj->TryGetStringField(TEXT("function_ref"), FunctionRef);
	NodeObj->TryGetStringField(TEXT("target_type"), TargetType);
	return FString::Printf(
		TEXT("fp:%s|member:%s|fn:%s|target:%s"),
		*NodeClass,
		*MemberName,
		*FunctionRef,
		*TargetType);
}

bool IsSerializedExecOutputPin_ImportBpy(
	const TSharedPtr<FJsonObject>& NodeObj,
	const FString& PinName)
{
	if (!NodeObj.IsValid() || PinName.IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* OutputPinTypesObj = nullptr;
	if (NodeObj->TryGetObjectField(TEXT("output_pin_types"), OutputPinTypesObj) &&
		OutputPinTypesObj && OutputPinTypesObj->IsValid())
	{
		FString PinType;
		if ((*OutputPinTypesObj)->TryGetStringField(PinName, PinType))
		{
			return PinType.StartsWith(TEXT("exec"), ESearchCase::IgnoreCase);
		}
	}

	// Compatibility fallback for payloads that omit output_pin_types.
	return PinName.StartsWith(TEXT("then"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("completed"), ESearchCase::IgnoreCase);
}

FString NormalizeExecPinSemanticName_ImportBpy(const FString& PinName)
{
	FString Canonical;
	Canonical.Reserve(PinName.Len());
	for (const TCHAR Ch : PinName)
	{
		if (FChar::IsAlnum(Ch))
		{
			Canonical.AppendChar(FChar::ToLower(Ch));
		}
	}

	if (Canonical == TEXT("true") || Canonical == TEXT("then"))
	{
		return TEXT("then");
	}
	if (Canonical == TEXT("false") || Canonical == TEXT("else"))
	{
		return TEXT("else");
	}
	if (Canonical == TEXT("exec") || Canonical == TEXT("execute"))
	{
		return TEXT("exec");
	}

	return Canonical.IsEmpty() ? TEXT("exec") : Canonical;
}

void CollectSerializedExecEdgesByGraph_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	TMap<FString, TSet<FString>>& OutExecEdgesByGraph)
{
	OutExecEdgesByGraph.Reset();
	if (!Root.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid())
		{
			continue;
		}

		const FString GraphId = BuildSerializedGraphIdentity_ImportBpy(GraphObj);
		if (GraphId.IsEmpty())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		TMap<FString, TSharedPtr<FJsonObject>> NodeByUid;
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString Uid;
			NodeObj->TryGetStringField(TEXT("uid"), Uid);
			if (!Uid.IsEmpty())
			{
				NodeByUid.Add(Uid, NodeObj);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ConnectionsArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("connections"), ConnectionsArr) || !ConnectionsArr)
		{
			continue;
		}

		TSet<FString>& ExecEdges = OutExecEdgesByGraph.FindOrAdd(GraphId);
		for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArr)
		{
			const TSharedPtr<FJsonObject> ConnObj = ConnValue.IsValid() ? ConnValue->AsObject() : nullptr;
			if (!ConnObj.IsValid())
			{
				continue;
			}

			FString SrcUid;
			FString DstUid;
			FString SrcPin;
			ConnObj->TryGetStringField(TEXT("src_node"), SrcUid);
			ConnObj->TryGetStringField(TEXT("dst_node"), DstUid);
			ConnObj->TryGetStringField(TEXT("src_pin"), SrcPin);
			if (SrcUid.IsEmpty() || DstUid.IsEmpty() || SrcPin.IsEmpty())
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* SrcNodePtr = NodeByUid.Find(SrcUid);
			const TSharedPtr<FJsonObject>* DstNodePtr = NodeByUid.Find(DstUid);
			if (!SrcNodePtr || !DstNodePtr)
			{
				continue;
			}

			if (!IsSerializedExecOutputPin_ImportBpy(*SrcNodePtr, SrcPin))
			{
				continue;
			}

			const FString SrcNodeId = BuildSerializedNodeIdentity_ImportBpy(*SrcNodePtr);
			const FString DstNodeId = BuildSerializedNodeIdentity_ImportBpy(*DstNodePtr);
			const FString PinIdentity = NormalizeExecPinSemanticName_ImportBpy(SrcPin);
			ExecEdges.Add(FString::Printf(TEXT("%s -> %s @ pin:%s"), *SrcNodeId, *DstNodeId, *PinIdentity));
		}
	}
}

bool ValidateSerializedExecTopologyParityBetweenRoots_ImportBpy(
	const TSharedPtr<FJsonObject>& SourceRoot,
	const TSharedPtr<FJsonObject>& CandidateRoot,
	const TCHAR* Stage,
	FString& OutError)
{
	if (!SourceRoot.IsValid() || !CandidateRoot.IsValid())
	{
		return true;
	}

	TMap<FString, TSet<FString>> SourceExecEdgesByGraph;
	TMap<FString, TSet<FString>> CandidateExecEdgesByGraph;
	CollectSerializedExecEdgesByGraph_ImportBpy(SourceRoot, SourceExecEdgesByGraph);
	CollectSerializedExecEdgesByGraph_ImportBpy(CandidateRoot, CandidateExecEdgesByGraph);

	for (const TPair<FString, TSet<FString>>& SourceEntry : SourceExecEdgesByGraph)
	{
		const FString& GraphId = SourceEntry.Key;
		const TSet<FString>* CandidateEdges = CandidateExecEdgesByGraph.Find(GraphId);
		if (!CandidateEdges)
		{
			OutError = FString::Printf(
				TEXT("Exec topology mismatch (%s): missing graph '%s'"),
				Stage ? Stage : TEXT("unknown"),
				*GraphId);
			return false;
		}

		TArray<FString> MissingEdges;
		TArray<FString> ExtraEdges;
		for (const FString& Edge : SourceEntry.Value)
		{
			if (!CandidateEdges->Contains(Edge))
			{
				MissingEdges.Add(Edge);
			}
		}
		for (const FString& Edge : *CandidateEdges)
		{
			if (!SourceEntry.Value.Contains(Edge))
			{
				ExtraEdges.Add(Edge);
			}
		}

		if (MissingEdges.Num() > 0 || ExtraEdges.Num() > 0)
		{
			OutError = FString::Printf(
				TEXT("Exec topology mismatch (%s) graph='%s': missing_exec_edges=%d extra_exec_edges=%d missing_sample=%s extra_sample=%s"),
				Stage ? Stage : TEXT("unknown"),
				*GraphId,
				MissingEdges.Num(),
				ExtraEdges.Num(),
				MissingEdges.Num() > 0 ? *MissingEdges[0] : TEXT("<none>"),
				ExtraEdges.Num() > 0 ? *ExtraEdges[0] : TEXT("<none>"));
			return false;
		}
	}

	return true;
}

bool ValidateIncomingExecTopologyAgainstSourceBeforeImport_ImportBpy(
	const TSharedPtr<FJsonObject>& IncomingRoot,
	const FString& SourceBlueprintPath,
	const TCHAR* Stage,
	FString& OutError)
{
	if (!IncomingRoot.IsValid() || SourceBlueprintPath.IsEmpty())
	{
		return true;
	}

	UBlueprint* const SourceBP = LoadBlueprintAsset_ImportBpy(SourceBlueprintPath);
	if (!SourceBP || !Cast<UAnimBlueprint>(SourceBP))
	{
		return true;
	}

	TSharedPtr<FJsonObject> SourceRoot = UBPDirectExporter::SerializeBlueprintToJson(SourceBP);
	if (!SourceRoot.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Pre-import exec topology validation (%s) failed to serialize source blueprint '%s'"),
			Stage ? Stage : TEXT("unknown"),
			*SourceBlueprintPath);
		return false;
	}

	return ValidateSerializedExecTopologyParityBetweenRoots_ImportBpy(
		SourceRoot,
		IncomingRoot,
		Stage,
		OutError);
}

bool ValidateImportedAnimBlueprintAgainstSourceAsset_ImportBpy(
	UBlueprint* ImportedBP,
	const FString& SourceBlueprintPath,
	const TCHAR* Stage,
	FString& OutError)
{
	if (!ImportedBP || SourceBlueprintPath.IsEmpty() || !Cast<UAnimBlueprint>(ImportedBP))
	{
		return true;
	}

	const FString ImportedPath = ImportedBP->GetPathName();
	if (ImportedPath.Equals(SourceBlueprintPath, ESearchCase::CaseSensitive))
	{
		return true;
	}

	UBlueprint* const SourceBP = LoadBlueprintAsset_ImportBpy(SourceBlueprintPath);
	if (!SourceBP || !Cast<UAnimBlueprint>(SourceBP))
	{
		return true;
	}

	FString SourceSerializedJson;
	TSharedPtr<FJsonObject> SourceJsonRoot = UBPDirectExporter::SerializeBlueprintToJson(SourceBP);
	if (!SourceJsonRoot.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Source contract validation (%s) failed to serialize source blueprint '%s'"),
			Stage ? Stage : TEXT("unknown"),
			*SourceBlueprintPath);
		return false;
	}
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SourceSerializedJson);
		if (!FJsonSerializer::Serialize(SourceJsonRoot.ToSharedRef(), Writer))
		{
			OutError = FString::Printf(
				TEXT("Source contract validation (%s) failed to emit serialized source blueprint '%s'"),
				Stage ? Stage : TEXT("unknown"),
				*SourceBlueprintPath);
			return false;
		}
	}

	TSharedPtr<FJsonObject> SourceRoot;
	{
		const TSharedRef<TJsonReader<>> SourceReader =
			TJsonReaderFactory<>::Create(SourceSerializedJson);
		if (!FJsonSerializer::Deserialize(SourceReader, SourceRoot) || !SourceRoot.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Source contract validation (%s) failed to parse source blueprint json for '%s'"),
				Stage ? Stage : TEXT("unknown"),
				*SourceBlueprintPath);
			return false;
		}
	}

	TArray<FString> NoWarnings;
	if (!ValidateRoundtripAgainstRootJson_ImportBpy(
			ImportedBP,
			SourceRoot,
			NoWarnings,
			Stage ? Stage : TEXT("source_contract"),
			OutError))
	{
		OutError = FString::Printf(
			TEXT("Source contract mismatch (%s) imported='%s' source='%s': %s"),
			Stage ? Stage : TEXT("unknown"),
			*ImportedPath,
			*SourceBlueprintPath,
			*OutError);
		return false;
	}

	TSharedPtr<FJsonObject> ImportedRoot = UBPDirectExporter::SerializeBlueprintToJson(ImportedBP);
	if (!ImportedRoot.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Source contract validation (%s) failed to serialize imported blueprint '%s'"),
			Stage ? Stage : TEXT("unknown"),
			*ImportedPath);
		return false;
	}

	if (!ValidateSerializedExecTopologyParityBetweenRoots_ImportBpy(
			SourceRoot,
			ImportedRoot,
			Stage ? Stage : TEXT("source_exec_contract"),
			OutError))
	{
		OutError = FString::Printf(
			TEXT("Source exec topology mismatch (%s) imported='%s' source='%s': %s"),
			Stage ? Stage : TEXT("unknown"),
			*ImportedPath,
			*SourceBlueprintPath,
			*OutError);
		return false;
	}

	if (!ValidateFunctionExecTopologyParityAgainstSource_ImportBpy(
			SourceBP,
			ImportedBP,
			TEXT("SetBlendStackAnimFromChooser"),
			Stage,
			OutError))
	{
		return false;
	}

	if (!ValidateFunctionExecTopologyParityAgainstSource_ImportBpy(
			SourceBP,
			ImportedBP,
			TEXT("Update_States"),
			Stage,
			OutError))
	{
		return false;
	}

	return true;
}

}


bool AppendJsonStringsForPreflight_ImportBpy(const TSharedPtr<FJsonObject>& Obj, FString& OutText)
{
	if (!Obj.IsValid())
	{
		return false;
	}

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
	{
		return false;
	}
	OutText += JsonText;
	return true;
}

int32 CountSubstringForPreflight_ImportBpy(const FString& Text, const TCHAR* Needle)
{
	if (!Needle || !Needle[0])
	{
		return 0;
	}

	int32 Count = 0;
	int32 SearchStart = 0;
	const FString NeedleText(Needle);
	while (Text.Find(NeedleText, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart) != INDEX_NONE)
	{
		const int32 FoundAt = Text.Find(NeedleText, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
		++Count;
		SearchStart = FoundAt + NeedleText.Len();
	}
	return Count;
}

bool ValidateStandaloneChooserJsonPreflight_ImportBpy(
	const FString& EffectiveAssetPath,
	const TSharedPtr<FJsonObject>& PropsObj,
	FString& OutError)
{
	if (!PropsObj.IsValid())
	{
		return true;
	}

	FString AssetClassPath;
	PropsObj->TryGetStringField(TEXT("asset_class"), AssetClassPath);
	if (!AssetClassPath.Contains(TEXT("ChooserTable")) || !EffectiveAssetPath.Contains(TEXT("_For_")))
	{
		return true;
	}

	FString AllText;
	AppendJsonStringsForPreflight_ImportBpy(PropsObj, AllText);
	if (AllText.Contains(TEXT("Class=None")))
	{
		OutError = FString::Printf(
			TEXT("Standalone Chooser META preflight failed for %s: ContextData Class=None would break AnimBlueprint runtime."),
			*EffectiveAssetPath);
		return false;
	}
	if (AllText.Contains(TEXT("SandboxCharacter_CMC_ABP_C")))
	{
		OutError = FString::Printf(
			TEXT("Standalone Chooser META preflight failed for %s: still references source SandboxCharacter_CMC_ABP_C."),
			*EffectiveAssetPath);
		return false;
	}

	FRegexMatcher TargetMatcher(FRegexPattern(TEXT("_For_(SandboxCharacter_Mover_ABP[^./']*)")), EffectiveAssetPath);
	if (TargetMatcher.FindNext())
	{
		const FString TargetName = TargetMatcher.GetCaptureGroup(1);
		const FString ExpectedClass = FString::Printf(TEXT("/Game/Blueprints/Test/%s.%s_C"), *TargetName, *TargetName);
		if (AllText.Contains(TEXT("ContextObjectTypeClass")) && !AllText.Contains(ExpectedClass))
		{
			OutError = FString::Printf(
				TEXT("Standalone Chooser META preflight failed for %s: missing expected target context %s."),
				*EffectiveAssetPath,
				*ExpectedClass);
			return false;
		}
	}

	return true;
}

bool ValidateAnimRoundtripJsonPreflight_ImportBpy(
	const TSharedPtr<FJsonObject>& Root,
	const FString& TargetAssetPath,
	FString& OutError)
{
	if (!Root.IsValid())
	{
		return true;
	}

	FString RootText;
	AppendJsonStringsForPreflight_ImportBpy(Root, RootText);
	const bool bLooksLikeSandboxAnimImport =
		TargetAssetPath.Contains(TEXT("SandboxCharacter_Mover_ABP")) ||
		RootText.Contains(TEXT("SandboxCharacter_CMC_ABP")) ||
		RootText.Contains(TEXT("State_Controller")) ||
		RootText.Contains(TEXT("EvaluateChooser2"));
	if (!bLooksLikeSandboxAnimImport)
	{
		return true;
	}

	const int32 StateEntryCount = CountSubstringForPreflight_ImportBpy(RootText, TEXT("StateEntryFunction"));
	const int32 OnStateEntryCount = CountSubstringForPreflight_ImportBpy(RootText, TEXT("OnStateEntry"));
	if (RootText.Contains(TEXT("State_Controller")) && StateEntryCount < 7)
	{
		OutError = FString::Printf(
			TEXT("BPY animation preflight failed: State Controller has too few StateEntryFunction bindings: %d < 7."),
			StateEntryCount);
		return false;
	}
	if (RootText.Contains(TEXT("State_Controller")) && OnStateEntryCount < 20)
	{
		OutError = FString::Printf(
			TEXT("BPY animation preflight failed: State Controller has too few OnStateEntry references: %d < 20."),
			OnStateEntryCount);
		return false;
	}

	if (RootText.Contains(TEXT("EvaluateChooser2")) &&
		!RootText.Contains(TEXT("CHT_PoseSearchDatabases.CHT_PoseSearchDatabases")))
	{
		OutError = TEXT("BPY animation preflight failed: Update_MotionMatching EvaluateChooser2 no longer references original CHT_PoseSearchDatabases.");
		return false;
	}

	return true;
}

bool UBPDirectImporter::ImportBlueprintFromJson(
	const FString& JsonData,
	const FString& TargetAssetPath,
	bool bCompileBlueprint,
	FString& OutError)
{
	// Live Coding can leave static registry storage in a bad state across patch reloads.
	// Avoid hard reset at import entry; registries are overwritten per-uid during import.

	if (GEditor && GEditor->PlayWorld)
	{
		OutError = TEXT("Cannot import blueprint while the editor is in play mode. Stop PIE and retry.");
		return false;
	}

	// 	// Parse JSON
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Failed to parse JSON");
		return false;
	}

	if (!ValidateAnimRoundtripJsonPreflight_ImportBpy(Root, TargetAssetPath, OutError))
	{
		return false;
	}

#if !UE_BUILD_SHIPPING
	{
		int32 TotalGraphCount = 0;
		int32 FunctionGraphCount = 0;
		int32 AnimNodeCount = 0;
		int32 AnimNodeUidEqualsNodeGuidCount = 0;
		int32 AnimNodeUidDiffersFromNodeGuidCount = 0;
		bool bHasGetGait = false;
		bool bHasGetInteractionTransform = false;
		bool bHasGetPoseHistory = false;
		bool bHasSetInteractionTransform = false;
		bool bHasSetNotifyTransitionReTransition = false;
		bool bHasSetNotifyTransitionToLoop = false;
		TArray<FString> AnimNodeIdentitySamples;
		AnimNodeIdentitySamples.Reserve(5);

		const TArray<TSharedPtr<FJsonValue>>* GraphsArrForDiag = nullptr;
		if (Root->TryGetArrayField(TEXT("graphs"), GraphsArrForDiag) && GraphsArrForDiag)
		{
			TotalGraphCount = GraphsArrForDiag->Num();
			for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArrForDiag)
			{
				const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
				if (!GraphObj.IsValid())
				{
					continue;
				}

				FString GraphType;
				GraphObj->TryGetStringField(TEXT("graph_type"), GraphType);
				if (GraphType.Equals(TEXT("function"), ESearchCase::IgnoreCase))
				{
					++FunctionGraphCount;
				}

				FString GraphName;
				GraphObj->TryGetStringField(TEXT("name"), GraphName);
				bHasGetGait |= GraphName.Equals(TEXT("Get_Gait"), ESearchCase::CaseSensitive);
				bHasGetInteractionTransform |= GraphName.Equals(TEXT("Get_InteractionTransform"), ESearchCase::CaseSensitive);
				bHasGetPoseHistory |= GraphName.Equals(TEXT("Get_PoseHistory"), ESearchCase::CaseSensitive);
				bHasSetInteractionTransform |= GraphName.Equals(TEXT("Set_InteractionTransform"), ESearchCase::CaseSensitive);
				bHasSetNotifyTransitionReTransition |= GraphName.Equals(TEXT("Set_NotifyTransition_ReTransition"), ESearchCase::CaseSensitive);
				bHasSetNotifyTransitionToLoop |= GraphName.Equals(TEXT("Set_NotifyTransition_ToLoop"), ESearchCase::CaseSensitive);

				const TArray<TSharedPtr<FJsonValue>>* NodesArrForDiag = nullptr;
				if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArrForDiag) || !NodesArrForDiag)
				{
					continue;
				}

				for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArrForDiag)
				{
					const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
					if (!NodeObj.IsValid())
					{
						continue;
					}

					FString NodeClassName;
					NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
					if (!NodeClassName.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
					{
						continue;
					}

					++AnimNodeCount;
					const FString SerializedUid = GetSerializedAnimNodeUid_ImportBpy(NodeObj);
					const FString SerializedNodeGuid = GetSerializedAnimNodeGuid_ImportBpy(NodeObj);
					const bool bUidEqualsNodeGuid =
						DoSerializedAnimNodeUidAndGuidMatch_ImportBpy(SerializedUid, SerializedNodeGuid);
					if (bUidEqualsNodeGuid)
					{
						++AnimNodeUidEqualsNodeGuidCount;
					}
					else
					{
						++AnimNodeUidDiffersFromNodeGuidCount;
						if (AnimNodeIdentitySamples.Num() < 5)
						{
							AnimNodeIdentitySamples.Add(FString::Printf(
								TEXT("%s:%s uid=%s node_guid=%s"),
								*GraphName,
								*NodeClassName,
								SerializedUid.IsEmpty() ? TEXT("<none>") : *SerializedUid,
								SerializedNodeGuid.IsEmpty() ? TEXT("<none>") : *SerializedNodeGuid));
						}
					}
				}
			}
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag] target=%s graphs=%d function_graphs=%d anim_nodes=%d anim_uid_eq_node_guid=%d anim_uid_diff_node_guid=%d identity_samples=%s has_Get_Gait=%d has_Get_InteractionTransform=%d has_Get_PoseHistory=%d has_Set_InteractionTransform=%d has_Set_NotifyTransition_ReTransition=%d has_Set_NotifyTransition_ToLoop=%d"),
			*TargetAssetPath,
			TotalGraphCount,
			FunctionGraphCount,
			AnimNodeCount,
			AnimNodeUidEqualsNodeGuidCount,
			AnimNodeUidDiffersFromNodeGuidCount,
			AnimNodeIdentitySamples.Num() > 0 ? *FString::Join(AnimNodeIdentitySamples, TEXT(" | ")) : TEXT("<none>"),
			bHasGetGait ? 1 : 0,
			bHasGetInteractionTransform ? 1 : 0,
			bHasGetPoseHistory ? 1 : 0,
			bHasSetInteractionTransform ? 1 : 0,
			bHasSetNotifyTransitionReTransition ? 1 : 0,
			bHasSetNotifyTransitionToLoop ? 1 : 0);
	}
#endif

	const FString SourceBlueprintPath =
		Root->HasTypedField<EJson::String>(TEXT("path"))
			? Root->GetStringField(TEXT("path"))
			: FString();
	const TGuardValue<FString> SourceBlueprintPathGuard(
		GCurrentImportSourceBlueprintPath_ImportBpy,
		SourceBlueprintPath);
	const TGuardValue<FString> TargetBlueprintPathGuard(
		GCurrentImportTargetBlueprintPath_ImportBpy,
		TargetAssetPath);

	FString PartialMode = Root->HasTypedField<EJson::String>(TEXT("partial_mode"))
		? Root->GetStringField(TEXT("partial_mode")).TrimStartAndEnd()
		: FString();
	const bool bIsPartialImportMode =
		!PartialMode.IsEmpty() &&
		!PartialMode.Equals(TEXT("full"), ESearchCase::IgnoreCase);
	const bool bHasStrictModeField = Root->HasTypedField<EJson::String>(TEXT("strict_mode"));
	FString StrictMode = bHasStrictModeField
		? Root->GetStringField(TEXT("strict_mode")).TrimStartAndEnd()
		: (bIsPartialImportMode ? FString(TEXT("normal")) : FString(TEXT("strict")));
	if (StrictMode.IsEmpty())
	{
		StrictMode = TEXT("strict");
	}
	const bool bStrictImportMode =
		!(
			StrictMode.Equals(TEXT("normal"), ESearchCase::IgnoreCase) ||
			StrictMode.Equals(TEXT("general"), ESearchCase::IgnoreCase) ||
			StrictMode.Equals(TEXT("一般"), ESearchCase::CaseSensitive));

	if (!bIsPartialImportMode &&
		!ValidateIncomingExecTopologyAgainstSourceBeforeImport_ImportBpy(
			Root,
			SourceBlueprintPath,
			TEXT("pre_import_source_exec_contract"),
			OutError))
	{
		return false;
	}

	// Determine parent class
	FString ParentClassPath = Root->GetStringField(TEXT("parent"));
	UClass* ParentClass = ResolveNamedObject_ImportBpy<UClass>(ParentClassPath);
	if (!ParentClass)
		ParentClass = AActor::StaticClass();

	// Create or load blueprint asset
	UBlueprint* BP = nullptr;
	if (UEditorAssetLibrary::DoesAssetExist(TargetAssetPath))
	{
		BP = LoadBlueprintAsset_ImportBpy(TargetAssetPath);
	}
	if (!BP)
	{
		BP = CreateBlueprintAsset(TargetAssetPath, ParentClass, OutError);
		if (!BP) return false;
	}
	const bool bIsAnimBlueprint = Cast<UAnimBlueprint>(BP) != nullptr;

	TArray<FString> ImportCompileWarnings;
	auto CompileAndTrackWarnings = [&BP, &ImportCompileWarnings, &OutError](const TCHAR* CompileStage) -> bool
	{
		TArray<FString> StageWarnings;
		FString CompileError;
		if (!UBPDirectImporter::CompileBlueprint(BP, &StageWarnings, &CompileError))
		{
			OutError = FString::Printf(
				TEXT("Blueprint compile failed (%s): %s"),
				CompileStage ? CompileStage : TEXT("unknown"),
				CompileError.IsEmpty() ? TEXT("unknown compile error") : *CompileError);
			return false;
		}

		for (const FString& Warning : StageWarnings)
		{
			if (!Warning.IsEmpty())
			{
				ImportCompileWarnings.AddUnique(Warning);
			}
		}

		if (Cast<UAnimBlueprint>(BP))
		{
			FString PhaseLabel = TEXT("D_post_compile");
			if (CompileStage && FCString::Strlen(CompileStage) > 0)
			{
				PhaseLabel += TEXT("_");
				PhaseLabel += CompileStage;
			}
			LogMotionMatchingBindingSnapshotsForBlueprint_ImportBpy(BP, *PhaseLabel);
		}

		return true;
	};

	// Variables
	const TArray<TSharedPtr<FJsonValue>>* VarsArr;
	if (Root->TryGetArrayField(TEXT("variables"), VarsArr))
	{
		for (auto& V : *VarsArr)
			CreateVariable(BP, V->AsObject());
	}

	// Components
	const TArray<TSharedPtr<FJsonValue>>* ComponentsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("components"), ComponentsArr) && ComponentsArr)
	{
		if (!ImportComponents_ImportBpy(BP, *ComponentsArr, OutError))
		{
			return false;
		}
	}

	// Interfaces
	const TArray<TSharedPtr<FJsonValue>>* InterfacesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("interfaces"), InterfacesArr) && InterfacesArr)
	{
		ImportInterfaces_ImportBpy(BP, *InterfacesArr);
	}

	// Class Defaults
	const TArray<TSharedPtr<FJsonValue>>* ClassDefaultsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("class_defaults"), ClassDefaultsArr) && ClassDefaultsArr)
	{
		ImportClassDefaults_ImportBpy(BP, *ClassDefaultsArr, SourceBlueprintPath);
	}

	// Inherited Component Defaults
	const TArray<TSharedPtr<FJsonValue>>* InheritedComponentsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("inherited_components"), InheritedComponentsArr) && InheritedComponentsArr)
	{
		ImportInheritedComponents_ImportBpy(BP, *InheritedComponentsArr);
	}

	// Graphs
	const TArray<TSharedPtr<FJsonValue>>* GraphsArr;
	TArray<TSharedPtr<FJsonObject>> SortedGraphs;
	if (Root->TryGetArrayField(TEXT("graphs"), GraphsArr))
	{
		SortedGraphs.Reserve(GraphsArr->Num());
		for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
		{
			if (const TSharedPtr<FJsonObject> GraphObj = GraphValue->AsObject(); GraphObj.IsValid())
			{
				SortedGraphs.Add(GraphObj);
			}
		}

		SortedGraphs.Sort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
		{
			return GetGraphImportPriority_ImportBpy(A) < GetGraphImportPriority_ImportBpy(B);
		});

		for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
		{
			if (IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
			{
				continue;
			}

			UEdGraph* Graph = nullptr;
			FString GraphType;
			FString GraphName;
			if (!EnsureGraphExists_ImportBpy(BP, GraphObj, Graph, GraphType, GraphName, OutError))
			{
				return false;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		// Do not force skeleton regeneration mid-import for AnimBlueprints.
		// Reconstruct during import can invalidate nested anim graphs we just restored.

		const int32 PreSkeletonMaxPriority = 1;
		auto PopulateGraphStage = [&](bool bPreSkeletonStage) -> bool
		{
			for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
			{
				if (IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
				{
					continue;
				}

				const int32 GraphPriority = GetGraphImportPriority_ImportBpy(GraphObj);
				const bool bInStage =
					bPreSkeletonStage
						? (GraphPriority <= PreSkeletonMaxPriority)
						: (GraphPriority > PreSkeletonMaxPriority);
				if (!bInStage)
				{
					continue;
				}

				if (!CreateGraph(BP, GraphObj, OutError))
				{
					return false;
				}
			}

			return true;
		};

		if (!PopulateGraphStage(true))
		{
			return false;
		}

		if (bIsAnimBlueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			FKismetEditorUtilities::GenerateBlueprintSkeleton(BP, true);
		}

		if (!PopulateGraphStage(false))
		{
			return false;
		}

		if (!RebindUnresolvedSelfContextCallsAndReplaySerializedPins_ImportBpy(BP, SortedGraphs, OutError))
		{
			return false;
		}

		bool bReboundAnimNodeFunctionRefs = false;
		if (!RebindAnimNodeFunctionReferencesFromSerializedGraphs_ImportBpy(
				BP,
				SortedGraphs,
				bReboundAnimNodeFunctionRefs,
				OutError))
		{
			return false;
		}
		if (bReboundAnimNodeFunctionRefs)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}

	// Rebuild skeleton after all graph creation so function signatures exist and
	// metadata (thread_safe/category/pure) can be applied deterministically.
	if (SortedGraphs.Num() > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::GenerateBlueprintSkeleton(BP, true);
		ReplayFunctionGraphMetadataToSignature_ImportBpy(BP, SortedGraphs);

		// Skeleton regeneration can recreate node-owned anim subgraphs such as
		// BlendStack editor graphs. Replay the serialized owning-node metadata
		// immediately so nested graphs are restored before any compile/save path.
		if (bIsAnimBlueprint &&
			!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
		{
			return false;
		}
	}

	// Reconcile interface graph ownership from the implemented interface function
	// signatures. Some UE import paths leave correctly created graphs unattached
	// to FBPInterfaceDescription::Graphs, which breaks interface runtime calls.
	RepairImplementedInterfaceGraphsFromExistingGraphs_ImportBpy(BP);

	// Keep chooser references identical to the exported source asset.
	// Retargeting to duplicated chooser tables introduced runtime divergence in
	// cloned AnimBlueprint imports.
	constexpr bool bEnableChooserRetargeting_ImportBpy = true;
	bool bRetargetedChooserTables = false;
	if (bEnableChooserRetargeting_ImportBpy)
	{
		if (!RetargetEvaluateChooserTablesForCurrentBlueprint_ImportBpy(BP, bRetargetedChooserTables, OutError))
		{
			return false;
		}
		if (bRetargetedChooserTables)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}

	RepairEvaluateChooserSourceClassPinsForCurrentImport_ImportBpy(BP);

	if (bRetargetedChooserTables)
	{
		bool bReplayedChooserConnections = false;
		if (!ReplayTopLevelGraphSerializedConnectionsAfterCompile_ImportBpy(
				BP,
				SortedGraphs,
				true,
				bReplayedChooserConnections,
				OutError))
		{
			return false;
		}
		if (bReplayedChooserConnections)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}

	// Strict pre-compile parity checks: graph/function/delegate/event topology must
	// match the serialized export before any final compile-time reconstruction.
	if (bStrictImportMode)
	{
		if (!ValidateImportedBlueprintStructuralParityAgainstRootJson_ImportBpy(
				BP,
				Root,
				false,
				OutError))
		{
			return false;
		}

		if (!ValidateImportedInterfaceBindingsAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("pre_compile"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintPoseHistoryContractAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("pre_compile"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintStateMachineBindingContractAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("pre_compile"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintLinkedAnimLayerContractAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("pre_compile"),
				OutError))
		{
			return false;
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag] Non-strict import mode enabled (strict_mode=%s): skipped pre_compile strict contract validation gates."),
			*StrictMode);
	}

	if (bCompileBlueprint)
	{
		if (!CompileAndTrackWarnings(TEXT("initial")))
		{
			return false;
		}

		// AnimBlueprint compile reconstructs node-owned nested graphs such as
		// AnimGraphNode_BlendStack.BoundGraph. Post-compile replay must therefore
		// run by default, with an explicit kill-switch for emergency rollback.
		const FString DisableAnimPostCompileReplayEnv =
			FPlatformMisc::GetEnvironmentVariable(TEXT("EXPORTBPY_DISABLE_ANIM_POST_COMPILE_REPLAY"));
		const bool bEnableAnimPostCompileReplay =
			!bIsAnimBlueprint ||
			!(DisableAnimPostCompileReplayEnv.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
				DisableAnimPostCompileReplayEnv.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
				DisableAnimPostCompileReplayEnv.Equals(TEXT("yes"), ESearchCase::IgnoreCase) ||
				DisableAnimPostCompileReplayEnv.Equals(TEXT("on"), ESearchCase::IgnoreCase));

		if (!bEnableAnimPostCompileReplay)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[ExportBpy][ImportDiag] Skipping AnimBlueprint post-compile replay passes. Set EXPORTBPY_DISABLE_ANIM_POST_COMPILE_REPLAY=1 to disable only when debugging importer regressions."));
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[ExportBpy][ImportDiag] Running AnimBlueprint post-compile replay passes."));

			if (!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
			{
				return false;
			}
			LogAnimBlueprintBlendStackReplayState_ImportBpy(BP, SortedGraphs, TEXT("PostCompileCheck"));

			bool bReappliedComponentTemplateProps = false;
			if (ComponentsArr &&
				!ReplayComponentTemplatePropertiesAfterCompile_ImportBpy(
					BP,
					*ComponentsArr,
					&bReappliedComponentTemplateProps,
					OutError))
			{
				return false;
			}

			if (bReappliedComponentTemplateProps)
			{
				if (!CompileAndTrackWarnings(TEXT("component_replay")))
				{
					return false;
				}

				if (!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
				{
					return false;
				}

				if (!ReplayComponentTemplatePropertiesAfterCompile_ImportBpy(BP, *ComponentsArr, nullptr, OutError))
				{
					return false;
				}
			}

			bool bReplayedTopLevelConnections = false;
			if (!ReplayTopLevelGraphSerializedConnectionsAfterCompile_ImportBpy(
					BP,
					SortedGraphs,
					true,
					bReplayedTopLevelConnections,
					OutError))
			{
				return false;
			}

			if (bReplayedTopLevelConnections)
			{
				if (!CompileAndTrackWarnings(TEXT("connection_replay")))
				{
					return false;
				}

				if (!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
				{
					return false;
				}

				if (ComponentsArr &&
					!ReplayComponentTemplatePropertiesAfterCompile_ImportBpy(BP, *ComponentsArr, nullptr, OutError))
				{
					return false;
				}
			}

			bool bUnexpectedMissingConnections = false;
			if (!ReplayTopLevelGraphSerializedConnectionsAfterCompile_ImportBpy(
					BP,
					SortedGraphs,
					false,
					bUnexpectedMissingConnections,
					OutError))
			{
				return false;
			}

			int32 FinalBindingVisibilityChangedNodes = 0;
			EnforceAnimNodeBindingDrivenPinVisibility_ImportBpy(BP, FinalBindingVisibilityChangedNodes);
			if (FinalBindingVisibilityChangedNodes > 0)
			{
				if (!CompileAndTrackWarnings(TEXT("binding_visibility_final_pass")))
				{
					return false;
				}

				if (!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
				{
					return false;
				}

				if (ComponentsArr &&
					!ReplayComponentTemplatePropertiesAfterCompile_ImportBpy(BP, *ComponentsArr, nullptr, OutError))
				{
					return false;
				}

				bool bReplayedFinalPassConnections = false;
				if (!ReplayTopLevelGraphSerializedConnectionsAfterCompile_ImportBpy(
						BP,
						SortedGraphs,
						true,
						bReplayedFinalPassConnections,
						OutError))
				{
					return false;
				}

				if (bReplayedFinalPassConnections)
				{
					if (!CompileAndTrackWarnings(TEXT("binding_visibility_connection_replay")))
					{
						return false;
					}

					if (!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
					{
						return false;
					}

					if (ComponentsArr &&
						!ReplayComponentTemplatePropertiesAfterCompile_ImportBpy(BP, *ComponentsArr, nullptr, OutError))
					{
						return false;
					}
				}

				bool bFinalUnexpectedMissingConnections = false;
				if (!ReplayTopLevelGraphSerializedConnectionsAfterCompile_ImportBpy(
						BP,
						SortedGraphs,
						false,
						bFinalUnexpectedMissingConnections,
						OutError))
				{
					return false;
				}
				if (bFinalUnexpectedMissingConnections)
				{
					OutError = TEXT("Unexpected missing connections reported in strict final replay pass");
					return false;
				}
			}
		}
	}

	int32 RepairedStateEntryBindings = 0;
	if (!RepairAnimBlueprintStateMachineEntryBindings_ImportBpy(
			BP,
			RepairedStateEntryBindings,
			OutError,
			&Root))
	{
		return false;
	}
	if (RepairedStateEntryBindings > 0)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag] State entry bindings repaired deterministically (%d)."),
			RepairedStateEntryBindings);

		if (bCompileBlueprint)
		{
			if (!CompileAndTrackWarnings(TEXT("state_entry_binding_repair")))
			{
				return false;
			}

			int32 RepairedStateEntryBindingsAfterCompile = 0;
			if (!RepairAnimBlueprintStateMachineEntryBindings_ImportBpy(
					BP,
					RepairedStateEntryBindingsAfterCompile,
					OutError,
					&Root))
			{
				return false;
			}
			if (RepairedStateEntryBindingsAfterCompile > 0)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy][ImportDiag] State entry bindings repaired again after compile (%d)."),
					RepairedStateEntryBindingsAfterCompile);
			}
		}
	}
	LogAnimBlueprintStateMachineEntryBindings_ImportBpy(
		BP,
		TEXT("post_import_repair"));
	if (!ValidateAnimBlueprintStateMachineEntryBindingPresence_ImportBpy(
			BP,
			TEXT("post_import_repair"),
			OutError))
	{
		return false;
	}

	// Post-compile parity checks still enforce function/delegate/event class
	// counts, while relaxing root node-count drift that can be editor-generated.
	if (bStrictImportMode)
	{
		if (!ValidateImportedBlueprintStructuralParityAgainstRootJson_ImportBpy(
				BP,
				Root,
				true,
				OutError))
		{
			return false;
		}

		if (!ValidateImportedInterfaceBindingsAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("post_compile"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintPoseHistoryContractAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("post_compile"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintStateMachineBindingContractAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("post_compile"),
				OutError))
		{
			return false;
		}

		if (!ValidateAnimBlueprintLinkedAnimLayerContractAgainstRootJson_ImportBpy(
				BP,
				Root,
				TEXT("post_compile"),
				OutError))
		{
			return false;
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag] Non-strict import: skipped post_compile strict contract validation gates."));
	}

	if (bStrictImportMode)
	{
		if (!ValidateRoundtripAgainstRootJson_ImportBpy(
				BP,
				Root,
				ImportCompileWarnings,
				bCompileBlueprint ? TEXT("post_compile") : TEXT("post_import"),
				OutError))
		{
			return false;
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy][ImportDiag] Non-strict import: skipped post_compile roundtrip validation gate."));
	}

	if (bIsAnimBlueprint)
	{
		if (!ReplayAndValidateBlueprintDefaultsContract_ImportBpy(
				BP,
				Root,
				TEXT("post_compile"),
				OutError))
		{
			return false;
		}

		if (bStrictImportMode)
		{
			if (!ValidateImportedAnimBlueprintAgainstSourceAsset_ImportBpy(
					BP,
					SourceBlueprintPath,
					TEXT("post_compile_source_contract"),
					OutError))
			{
				return false;
			}
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ImportDiag] Non-strict import: skipped post_compile_source_contract validation."));
		}
	}

	{
		const FString EnableVerifyExportRoundtripEnv =
			FPlatformMisc::GetEnvironmentVariable(TEXT("EXPORTBPY_ENABLE_VERIFY_EXPORT_ROUNDTRIP"));
		const bool bEnableVerifyExportRoundtrip =
			EnableVerifyExportRoundtripEnv.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
			EnableVerifyExportRoundtripEnv.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			EnableVerifyExportRoundtripEnv.Equals(TEXT("yes"), ESearchCase::IgnoreCase) ||
			EnableVerifyExportRoundtripEnv.Equals(TEXT("on"), ESearchCase::IgnoreCase);

		if (bEnableVerifyExportRoundtrip)
		{
			FString VerifyRoundtripError;
			if (!VerifyExportRoundtripAgainstRootJson_ImportBpy(BP, Root, VerifyRoundtripError))
			{
				OutError = VerifyRoundtripError;
				return false;
			}
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[ExportBpy][ImportDiag] Skipping VerifyExportRoundtrip (set EXPORTBPY_ENABLE_VERIFY_EXPORT_ROUNDTRIP=1 to enable)."));
		}
	}

	if (bIsAnimBlueprint)
	{
		LogImportedAnimBlueprintReachableGraphInventory_ImportBpy(
			BP,
			TEXT("PreSaveFinal"));
		LogAnimBlueprintBlendStackReplayState_ImportBpy(
			BP,
			SortedGraphs,
			TEXT("PreSaveFinal"));
		LogSerializedAnimBlueprintBlendStackBindingState_ImportBpy(
			BP,
			SortedGraphs,
			TEXT("PreSaveFinal"));
		LogSerializedAnimNodeUidResolutionAudit_ImportBpy(
			BP,
			Root,
			TEXT("PreSaveFinal"));
	}

	if (!SaveBlueprint(BP, OutError))
	{
		return false;
	}

	if (bIsAnimBlueprint)
	{
		LogImportedAnimBlueprintReachableGraphInventory_ImportBpy(
			BP,
			TEXT("PostSaveBeforeReload"));
		LogAnimBlueprintBlendStackReplayState_ImportBpy(
			BP,
			SortedGraphs,
			TEXT("PostSaveBeforeReload"));
		LogSerializedAnimBlueprintBlendStackBindingState_ImportBpy(
			BP,
			SortedGraphs,
			TEXT("PostSaveBeforeReload"));
		LogSerializedAnimNodeUidResolutionAudit_ImportBpy(
			BP,
			Root,
			TEXT("PostSaveBeforeReload"));
	}

	if (!RunPostSaveReloadValidation_ImportBpy(
			TargetAssetPath,
			Root,
			SortedGraphs,
			ImportCompileWarnings,
			bStrictImportMode,
			BP,
			OutError))
	{
		return false;
	}

	return true;
}

FString UBPDirectImporter::ImportBlueprintFromJsonDetailed(
	const FString& JsonData,
	const FString& TargetAssetPath,
	bool bCompileBlueprint)
{
	FString OutError;
	const bool bSuccess = ImportBlueprintFromJson(JsonData, TargetAssetPath, bCompileBlueprint, OutError);

	TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), bSuccess);
	ResultObj->SetStringField(TEXT("error"), OutError);
	ResultObj->SetStringField(TEXT("asset_path"), TargetAssetPath);
	ResultObj->SetBoolField(TEXT("compiled"), bSuccess && bCompileBlueprint);

	FString ResultJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
	FJsonSerializer::Serialize(ResultObj, Writer);
	return ResultJson;
}

bool UBPDirectImporter::ValidateImportedBlueprintAgainstJson(
	const FString& JsonData,
	const FString& TargetAssetPath,
	FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Failed to parse JSON for validation");
		return false;
	}

	UBlueprint* const BP = LoadBlueprintAsset_ImportBpy(TargetAssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Unable to load blueprint for validation: %s"), *TargetAssetPath);
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> MissingGraphs;
	TArray<TSharedPtr<FJsonValue>> MissingDefaultKeys;
	TArray<TSharedPtr<FJsonValue>> DefaultMismatches;
	if (!ValidateBlueprintDefaultsAgainstRootJson_ImportBpy(
			BP,
			Root,
			MissingGraphs,
			MissingDefaultKeys,
			DefaultMismatches,
			OutError))
	{
		return false;
	}

	if (MissingGraphs.Num() > 0 || MissingDefaultKeys.Num() > 0 || DefaultMismatches.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("Import defaults validation failed: missing_graphs=%d missing_default_keys=%d default_mismatches=%d"),
			MissingGraphs.Num(),
			MissingDefaultKeys.Num(),
			DefaultMismatches.Num());
		return false;
	}

	return true;
}

FString UBPDirectImporter::ValidateImportedBlueprintAgainstJsonDetailed(
	const FString& JsonData,
	const FString& TargetAssetPath)
{
	FString OutError;
	TArray<TSharedPtr<FJsonValue>> MissingGraphs;
	TArray<TSharedPtr<FJsonValue>> MissingDefaultKeys;
	TArray<TSharedPtr<FJsonValue>> DefaultMismatches;

	bool bSuccess = false;
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Failed to parse JSON for validation");
		}
		else if (UBlueprint* BP = LoadBlueprintAsset_ImportBpy(TargetAssetPath))
		{
			const bool bValidated = ValidateBlueprintDefaultsAgainstRootJson_ImportBpy(
				BP,
				Root,
				MissingGraphs,
				MissingDefaultKeys,
				DefaultMismatches,
				OutError);
			bSuccess = bValidated &&
				MissingGraphs.Num() == 0 &&
				MissingDefaultKeys.Num() == 0 &&
				DefaultMismatches.Num() == 0;
			if (bValidated && !bSuccess && OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Import defaults validation failed: missing_graphs=%d missing_default_keys=%d default_mismatches=%d"),
					MissingGraphs.Num(),
					MissingDefaultKeys.Num(),
					DefaultMismatches.Num());
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Unable to load blueprint for validation: %s"), *TargetAssetPath);
		}
	}

	TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), bSuccess);
	ResultObj->SetStringField(TEXT("error"), OutError);
	ResultObj->SetStringField(TEXT("asset_path"), TargetAssetPath);
	ResultObj->SetArrayField(TEXT("missing_graphs"), MissingGraphs);
	ResultObj->SetArrayField(TEXT("missing_default_keys"), MissingDefaultKeys);
	ResultObj->SetArrayField(TEXT("default_mismatches"), DefaultMismatches);
	ResultObj->SetNumberField(TEXT("missing_graph_count"), MissingGraphs.Num());
	ResultObj->SetNumberField(TEXT("missing_default_key_count"), MissingDefaultKeys.Num());
	ResultObj->SetNumberField(TEXT("default_mismatch_count"), DefaultMismatches.Num());

	FString ResultJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
	FJsonSerializer::Serialize(ResultObj, Writer);
	return ResultJson;
}

bool UBPDirectImporter::ValidateRoundtrip(
	const FString& JsonData,
	const FString& TargetAssetPath,
	FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Failed to parse JSON for roundtrip validation");
			return false;
		}
	}

	UBlueprint* const BP = LoadBlueprintAsset_ImportBpy(TargetAssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Unable to load blueprint for roundtrip validation: %s"), *TargetAssetPath);
		return false;
	}

	return ValidateRoundtripAgainstRootJson_ImportBpy(BP, Root, TArray<FString>{}, TEXT("manual_validate"), OutError);
}

FString UBPDirectImporter::ValidateRoundtripDetailed(
	const FString& JsonData,
	const FString& TargetAssetPath)
{
	FString OutError;
	const bool bSuccess = ValidateRoundtrip(JsonData, TargetAssetPath, OutError);

	TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), bSuccess);
	ResultObj->SetStringField(TEXT("error"), OutError);
	ResultObj->SetStringField(TEXT("asset_path"), TargetAssetPath);

	FString ResultJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
	FJsonSerializer::Serialize(ResultObj, Writer);
	return ResultJson;
}

bool UBPDirectImporter::VerifyExportRoundtrip(
	const FString& JsonData,
	const FString& TargetAssetPath,
	FString& OutError)
{
	TSharedPtr<FJsonObject> SourceRoot;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
		if (!FJsonSerializer::Deserialize(Reader, SourceRoot) || !SourceRoot.IsValid())
		{
			OutError = TEXT("Failed to parse JSON for export roundtrip verification");
			return false;
		}
	}

	UBlueprint* const BP = LoadBlueprintAsset_ImportBpy(TargetAssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Unable to load blueprint for export roundtrip verification: %s"), *TargetAssetPath);
		return false;
	}

	return VerifyExportRoundtripAgainstRootJson_ImportBpy(BP, SourceRoot, OutError);
}

FString UBPDirectImporter::VerifyExportRoundtripDetailed(
	const FString& JsonData,
	const FString& TargetAssetPath)
{
	FString OutError;
	const bool bSuccess = VerifyExportRoundtrip(JsonData, TargetAssetPath, OutError);

	TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), bSuccess);
	ResultObj->SetStringField(TEXT("error"), OutError);
	ResultObj->SetStringField(TEXT("asset_path"), TargetAssetPath);

	FString ResultJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
	FJsonSerializer::Serialize(ResultObj, Writer);
	return ResultJson;
}

static bool RestoreCreateDelegatesFromRootJson_ImportBpy(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& Root,
	int32& OutDelegateNodeCount,
	int32& OutRestoredDelegateCount,
	FString& OutError)
{
	OutDelegateNodeCount = 0;
	OutRestoredDelegateCount = 0;

	if (!BP || !Root.IsValid())
	{
		OutError = TEXT("Invalid blueprint or root json while restoring CreateDelegate bindings");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (!GraphObj->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
		{
			continue;
		}

		TMap<FString, UEdGraphNode*> NodeMap;
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			FString NodeClassName;
			NodeObj->TryGetStringField(TEXT("node_class"), NodeClassName);
			if (NodeClassName != TEXT("K2Node_CreateDelegate"))
			{
				continue;
			}

			++OutDelegateNodeCount;

			FString NodeUid;
			if (!NodeObj->TryGetStringField(TEXT("uid"), NodeUid) || NodeUid.IsEmpty())
			{
				OutError = TEXT("CreateDelegate node is missing uid in import json");
				return false;
			}

			FGuid ParsedGuid;
			UEdGraphNode* FoundNode = FindImportedNodeBySerializedUid_ImportBpy(BP, NodeUid);
			if (!FoundNode && TryParseGuid_ImportBpy(NodeUid, ParsedGuid))
			{
				FoundNode = FindImportedNodeByGuidScan_ImportBpy(BP, ParsedGuid);
			}
			if (!FoundNode)
			{
				OutError = FString::Printf(
					TEXT("Cannot locate imported CreateDelegate node by uid/guid: %s"),
					*NodeUid);
				return false;
			}

			if (!Cast<UK2Node_CreateDelegate>(FoundNode))
			{
				OutError = FString::Printf(
					TEXT("Guid %s does not resolve to K2Node_CreateDelegate"),
					*NodeUid);
				return false;
			}

			NodeMap.Add(NodeUid, FoundNode);
		}

		if (NodeMap.Num() == 0)
		{
			continue;
		}

		if (!RestoreCreateDelegateNodesAfterConnections_ImportBpy(NodesArr, NodeMap, true, OutError))
		{
			return false;
		}

		for (const TPair<FString, UEdGraphNode*>& Pair : NodeMap)
		{
			if (const UK2Node_CreateDelegate* RestoredNode = Cast<UK2Node_CreateDelegate>(Pair.Value))
			{
				if (!RestoredNode->GetFunctionName().IsNone())
				{
					++OutRestoredDelegateCount;
				}
			}
		}
	}

	return true;
}

bool UBPDirectImporter::RestoreCreateDelegatesFromJson(
	const FString& JsonData,
	const FString& TargetAssetPath,
	FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Failed to parse JSON while restoring CreateDelegate bindings");
			return false;
		}
	}

	UBlueprint* const BP = LoadBlueprintAsset_ImportBpy(TargetAssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Unable to load blueprint asset for delegate restore: %s"), *TargetAssetPath);
		return false;
	}

	int32 DelegateNodeCount = 0;
	int32 RestoredDelegateCount = 0;
	if (!RestoreCreateDelegatesFromRootJson_ImportBpy(BP, Root, DelegateNodeCount, RestoredDelegateCount, OutError))
	{
		return false;
	}

	if (DelegateNodeCount > 0 && RestoredDelegateCount != DelegateNodeCount)
	{
		OutError = FString::Printf(
			TEXT("CreateDelegate restore mismatch: expected=%d restored=%d"),
			DelegateNodeCount,
			RestoredDelegateCount);
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->MarkPackageDirty();
	if (!SaveBlueprint(BP, OutError))
	{
		return false;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy] RestoreCreateDelegatesFromJson target=%s delegates=%d restored=%d"),
		*TargetAssetPath,
		DelegateNodeCount,
		RestoredDelegateCount);

	return true;
}

FString UBPDirectImporter::RestoreCreateDelegatesFromJsonDetailed(
	const FString& JsonData,
	const FString& TargetAssetPath)
{
	FString OutError;
	const bool bSuccess = RestoreCreateDelegatesFromJson(JsonData, TargetAssetPath, OutError);

	TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), bSuccess);
	ResultObj->SetStringField(TEXT("error"), OutError);
	ResultObj->SetStringField(TEXT("asset_path"), TargetAssetPath);

	FString ResultJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
	FJsonSerializer::Serialize(ResultObj, Writer);
	return ResultJson;
}

FString UBPDirectImporter::ImportStandaloneAssetFromJsonDetailed(
	const FString& AssetPath,
	const FString& PropertiesJson)
{
	FString OutError;
	const bool bSuccess = ImportStandaloneAssetFromJson(AssetPath, PropertiesJson, OutError);

	TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), bSuccess);
	ResultObj->SetStringField(TEXT("error"), OutError);
	ResultObj->SetStringField(TEXT("asset_path"), AssetPath);

	FString ResultJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
	FJsonSerializer::Serialize(ResultObj, Writer);
	return ResultJson;
}

// ─── CreateBlueprintAsset ─────────────────────────────────────────────────────

UBlueprint* UBPDirectImporter::CreateBlueprintAsset(
	const FString& AssetPath,
	UClass* ParentClass,
	FString& OutError)
{
	FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	FString AssetName   = FPaths::GetBaseFilename(AssetPath);

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Cannot create package: %s"), *PackageName);
		return nullptr;
	}

	if (UBlueprint* ExistingBlueprint = FindObject<UBlueprint>(Package, *AssetName))
	{
		return ExistingBlueprint;
	}

	if (UBlueprint* ExistingBlueprint = LoadBlueprintAsset_ImportBpy(AssetPath))
	{
		return ExistingBlueprint;
	}

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		*AssetName,
		BPTYPE_Normal,
		(ParentClass && ParentClass->IsChildOf(UAnimInstance::StaticClass()))
			? UAnimBlueprint::StaticClass()
			: UBlueprint::StaticClass(),
		(ParentClass && ParentClass->IsChildOf(UAnimInstance::StaticClass()))
			? UAnimBlueprintGeneratedClass::StaticClass()
			: UBlueprintGeneratedClass::StaticClass());

	if (!BP)
	{
		OutError = FString::Printf(TEXT("Cannot create blueprint: %s"), *AssetPath);
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(BP);
	Package->MarkPackageDirty();
	return BP;
}

// ─── CreateVariable ───────────────────────────────────────────────────────────

void UBPDirectImporter::CreateVariable(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& VarJson)
{
	if (!VarJson.IsValid()) return;

	FString VarName    = VarJson->GetStringField(TEXT("name"));
	FString TypeStr    = VarJson->GetStringField(TEXT("type"));
	FString DefaultVal = VarJson->GetStringField(TEXT("default"));
	FString Container  = TEXT("single");
	VarJson->TryGetStringField(TEXT("container"), Container);

	// Build pin type
	FEdGraphPinType PinType;
	ParsePinType(TypeStr, PinType);
	if (Container == TEXT("array"))
	{
		PinType.ContainerType = EPinContainerType::Array;
	}
	else if (Container == TEXT("set"))
	{
		PinType.ContainerType = EPinContainerType::Set;
	}
	else if (Container == TEXT("map"))
	{
		PinType.ContainerType = EPinContainerType::Map;
	}

	if (PinType.ContainerType == EPinContainerType::Map && PinType.PinValueType.TerminalCategory.IsNone())
	{
		FString ExplicitMapValueType;
		if (VarJson->TryGetStringField(TEXT("map_value_type"), ExplicitMapValueType) && !ExplicitMapValueType.IsEmpty())
		{
			FEdGraphPinType ExplicitMapValuePinType;
			ParsePinType(ExplicitMapValueType, ExplicitMapValuePinType);
			PinType.PinValueType = FEdGraphTerminalType::FromPinType(ExplicitMapValuePinType);

			bool bExplicitMapValueConst = false;
			bool bExplicitMapValueWeak = false;
			bool bExplicitMapValueWrapper = false;
			VarJson->TryGetBoolField(TEXT("map_value_const"), bExplicitMapValueConst);
			VarJson->TryGetBoolField(TEXT("map_value_weak"), bExplicitMapValueWeak);
			VarJson->TryGetBoolField(TEXT("map_value_wrapper"), bExplicitMapValueWrapper);
			PinType.PinValueType.bTerminalIsConst =
				PinType.PinValueType.bTerminalIsConst || bExplicitMapValueConst;
			PinType.PinValueType.bTerminalIsWeakPointer =
				PinType.PinValueType.bTerminalIsWeakPointer || bExplicitMapValueWeak;
			PinType.PinValueType.bTerminalIsUObjectWrapper =
				PinType.PinValueType.bTerminalIsUObjectWrapper || bExplicitMapValueWrapper;
		}
	}

	const FName VariableFName(*VarName);
	const bool bTraceMovementModeMap = (VarName == TEXT("MovementModeMap"));
	if (bTraceMovementModeMap)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy] CreateVariable raw %s type_str=%s"),
			*VarName,
			*TypeStr);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy] CreateVariable parsed %s type=%s default=%s"),
			*VarName,
			*DescribePinType_ImportBpy(PinType),
			*DefaultVal);
	}

	if (FBPVariableDescription* ExistingVariable =
			FindBlueprintVariableDescription_ImportBpy(BP, VariableFName))
	{
		BP->Modify();
		if (SyncBlueprintVariableDescriptionFromJson_ImportBpy(
				*ExistingVariable,
				VarJson,
				PinType,
				DefaultVal))
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			BP->MarkPackageDirty();
		}
		if (bTraceMovementModeMap)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy] CreateVariable updated %s stored_type=%s"),
				*VarName,
				*DescribePinType_ImportBpy(ExistingVariable->VarType));
		}
		return;
	}

	if (FBlueprintEditorUtils::AddMemberVariable(BP, VariableFName, PinType, DefaultVal))
	{
		if (FBPVariableDescription* CreatedVariable =
				FindBlueprintVariableDescription_ImportBpy(BP, VariableFName))
		{
			SyncBlueprintVariableDescriptionFromJson_ImportBpy(
				*CreatedVariable,
				VarJson,
				PinType,
				DefaultVal);
			if (bTraceMovementModeMap)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy] CreateVariable created %s stored_type=%s"),
					*VarName,
					*DescribePinType_ImportBpy(CreatedVariable->VarType));
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		BP->MarkPackageDirty();
	}
}

// ─── CreateGraph ─────────────────────────────────────────────────────────────

bool UBPDirectImporter::PopulateGraph(
	UBlueprint* BP,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& GraphJson,
	bool bPreserveTunnelNodes,
	FString& OutError)
{
	if (!BP || !Graph || !GraphJson.IsValid())
	{
		OutError = TEXT("Invalid graph import context");
		return false;
	}

	const FString GraphName = GraphJson->GetStringField(TEXT("name"));
	const FString GraphType = GraphJson->GetStringField(TEXT("graph_type"));
	const bool bIsBlendStackGraph = IsBlendStackGraphLike_ImportBpy(Graph);
	const FString EffectiveGraphType =
		(bIsBlendStackGraph && GraphType == TEXT("event_graph")) ? TEXT("blend_stack") : GraphType;
	FString SerializedGraphOuterKind;
	GraphJson->TryGetStringField(TEXT("graph_outer"), SerializedGraphOuterKind);
	FString SerializedGraphGuidText;
	if (GraphJson->TryGetStringField(TEXT("graph_guid"), SerializedGraphGuidText) &&
		!SerializedGraphGuidText.IsEmpty())
	{
		FGuid SerializedGraphGuid;
		if (TryParseGuid_ImportBpy(SerializedGraphGuidText, SerializedGraphGuid) &&
			Graph->GraphGuid != SerializedGraphGuid)
		{
			Graph->Modify();
			Graph->GraphGuid = SerializedGraphGuid;
		}
	}
	if (bIsBlendStackGraph && SerializedGraphOuterKind.Equals(TEXT("Node"), ESearchCase::IgnoreCase))
	{
		if (!Graph->GetOuter() || !Graph->GetOuter()->IsA<UEdGraphNode>())
		{
			OutError = FString::Printf(
				TEXT("BlendStack graph '%s' expects outer kind 'Node' from export, but resolved outer kind is '%s'."),
				*Graph->GetName(),
				*DescribeGraphOuterKind_ImportBpy(Graph));
			return false;
		}
	}
	const bool bTreatAsRegularFunctionGraph =
		(EffectiveGraphType == TEXT("function")) &&
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, Graph, GraphJson, EffectiveGraphType, GraphName);

	if (!GraphName.IsEmpty() && Graph->GetName() != GraphName)
	{
		const bool bIsNodeOwnedNestedGraph = Graph->GetOuter() && Graph->GetOuter()->IsA<UEdGraphNode>();
		if (bIsNodeOwnedNestedGraph)
		{
			// Avoid blueprint-level graph rename broadcasts while importing nested anim graphs.
			// UK2Node_AnimGetter listens to those events and can assert before ownership is fully restored.
			FEdGraphUtilities::RenameGraphToNameOrCloseToName(Graph, *GraphName);
		}
		else
		{
			FBlueprintEditorUtils::RenameGraph(Graph, GraphName);
		}
	}

	if (!RepairStateMachineSubgraphOwnershipBeforeClear_ImportBpy(Graph, OutError))
	{
		return false;
	}

	// Import is authoritative for a graph. Clear pre-existing/default nodes first so
	// re-imports do not accumulate stale nodes such as the template Event Tick.
	ClearGraphNodes_ImportBpy(BP, Graph, bPreserveTunnelNodes, bTreatAsRegularFunctionGraph);

	TArray<TPair<FString, FEdGraphPinType>> GraphInputs;
	TArray<TPair<FString, FEdGraphPinType>> GraphOutputs;
	ParseGraphPins_ImportBpy(GraphJson, TEXT("inputs"), GraphInputs);
	ParseGraphPins_ImportBpy(GraphJson, TEXT("outputs"), GraphOutputs);

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	GraphJson->TryGetArrayField(TEXT("nodes"), NodesArr);
	if (bPreserveTunnelNodes && NodesArr)
	{
		RecoverGraphPinContractsFromTunnelNodes_ImportBpy(NodesArr, GraphInputs, GraphOutputs);
	}

	if (bTreatAsRegularFunctionGraph)
	{
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		Graph->GetNodesOfClass(EntryNodes);
		if (EntryNodes.Num() == 0)
		{
			UK2Node_FunctionEntry* Entry = NewObject<UK2Node_FunctionEntry>(Graph);
			Entry->CreateNewGuid();
			Entry->PostPlacedNewNode();
			Entry->AllocateDefaultPins();
			Graph->AddNode(Entry, false, false);
			EntryNodes.Add(Entry);
		}
		ApplyFunctionGraphMetadata_ImportBpy(GraphJson, EntryNodes[0]);
		EnsureFunctionPins_ImportBpy(EntryNodes[0], GraphInputs);

		TArray<UK2Node_FunctionResult*> ResultNodes;
		Graph->GetNodesOfClass(ResultNodes);
		if (GraphOutputs.Num() > 0 && ResultNodes.Num() == 0)
		{
			UK2Node_FunctionResult* ResultNode = NewObject<UK2Node_FunctionResult>(Graph);
			ResultNode->CreateNewGuid();
			ResultNode->PostPlacedNewNode();
			ResultNode->AllocateDefaultPins();
			Graph->AddNode(ResultNode, false, false);
			ResultNodes.Add(ResultNode);
		}
		for (UK2Node_FunctionResult* ResultNode : ResultNodes)
		{
			EnsureFunctionPins_ImportBpy(ResultNode, GraphOutputs);
		}
	}

	UK2Node_Tunnel* EntryTunnel = nullptr;
	UK2Node_Tunnel* ExitTunnel = nullptr;
	if (bPreserveTunnelNodes)
	{
		TArray<UK2Node_Tunnel*> TunnelNodes;
		Graph->GetNodesOfClass(TunnelNodes);
		for (UK2Node_Tunnel* TunnelNode : TunnelNodes)
		{
			if (!TunnelNode)
			{
				continue;
			}

			if (TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs)
			{
				EntryTunnel = TunnelNode;
			}
			else if (TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs)
			{
				ExitTunnel = TunnelNode;
			}
		}

		if (GraphInputs.Num() > 0 && !EntryTunnel)
		{
			OutError = FString::Printf(TEXT("Graph '%s' is missing an entry tunnel"), *GraphName);
			return false;
		}
		if (GraphOutputs.Num() > 0 && !ExitTunnel)
		{
			OutError = FString::Printf(TEXT("Graph '%s' is missing an exit tunnel"), *GraphName);
			return false;
		}

		EnsureTunnelPins_ImportBpy(EntryTunnel, GraphInputs, EGPD_Output);
		EnsureTunnelPins_ImportBpy(ExitTunnel, GraphOutputs, EGPD_Input);
	}

	if (bTreatAsRegularFunctionGraph)
	{
		const TArray<TSharedPtr<FJsonValue>>* PreNodesArr = nullptr;
		if (GraphJson->TryGetArrayField(TEXT("nodes"), PreNodesArr))
		{
			TSet<FName> FunctionInputNames;
			for (const TPair<FString, FEdGraphPinType>& GraphInput : GraphInputs)
			{
				FunctionInputNames.Add(FName(*GraphInput.Key));
			}

			for (const TSharedPtr<FJsonValue>& NodeValue : *PreNodesArr)
			{
				const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
				if (!NodeObj.IsValid())
				{
					continue;
				}

				const FString NodeClass = NodeObj->GetStringField(TEXT("node_class"));
				if (NodeClass != TEXT("K2Node_VariableGet") && NodeClass != TEXT("K2Node_VariableSet"))
				{
					continue;
				}

				const FString VariableScope = GetNodePropString_ImportBpy(NodeObj, TEXT("VariableScope"));
				if (!VariableScope.Equals(TEXT("Local"), ESearchCase::IgnoreCase))
				{
					continue;
				}

				const FString VariableName = NodeObj->GetStringField(TEXT("member_name"));
				if (VariableName.IsEmpty() || FunctionInputNames.Contains(FName(*VariableName)))
				{
					continue;
				}

				if (FBlueprintEditorUtils::FindLocalVariable(BP, Graph, FName(*VariableName), nullptr))
				{
					continue;
				}

				const FString VariableTypeString = GetNodePropString_ImportBpy(NodeObj, TEXT("VariableType"));
				if (VariableTypeString.IsEmpty())
				{
					continue;
				}

				FEdGraphPinType LocalPinType;
				ParsePinType(VariableTypeString, LocalPinType);

				const FString VariableContainer = GetNodePropString_ImportBpy(NodeObj, TEXT("VariableContainer"));
				if (VariableContainer == TEXT("array"))
				{
					LocalPinType.ContainerType = EPinContainerType::Array;
				}
				else if (VariableContainer == TEXT("set"))
				{
					LocalPinType.ContainerType = EPinContainerType::Set;
				}
				else if (VariableContainer == TEXT("map"))
				{
					LocalPinType.ContainerType = EPinContainerType::Map;
				}

				FBlueprintEditorUtils::AddLocalVariable(BP, Graph, FName(*VariableName), LocalPinType);
			}
		}
	}

	TMap<FString, UEdGraphNode*> NodeMap;
	int32 ReusableResultNodeIndex = 0;
	TArray<UK2Node_FunctionResult*> ExistingResultNodes;
	if (bTreatAsRegularFunctionGraph)
	{
		Graph->GetNodesOfClass(ExistingResultNodes);
	}

	if (GraphJson->TryGetArrayField(TEXT("nodes"), NodesArr))
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString NodeClass = NodeObj->GetStringField(TEXT("node_class"));
			if (NodeClass != TEXT("K2Node_EvaluateChooser") && NodeClass != TEXT("K2Node_EvaluateChooser2"))
			{
				continue;
			}

			FString MissingChooserProperties;
			if (!HasEvaluateChooserMetadata_ImportBpy(NodeObj, MissingChooserProperties))
			{
				FString NodeUid;
				NodeObj->TryGetStringField(TEXT("uid"), NodeUid);
				OutError = FString::Printf(
					TEXT("Legacy chooser export detected in graph '%s': node '%s' (uid=%s) is missing required metadata [%s]. Re-export the source Blueprint with the latest ExportBpy and retry import."),
					*Graph->GetName(),
					*NodeClass,
					NodeUid.IsEmpty() ? TEXT("<none>") : *NodeUid,
					MissingChooserProperties.IsEmpty() ? TEXT("Chooser, Mode") : *MissingChooserProperties);
				return false;
			}
		}

	if (bPreserveTunnelNodes)
	{
		bool bMappedEntryTunnel = false;
		bool bMappedExitTunnel = false;

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid() || NodeObj->GetStringField(TEXT("node_class")) != TEXT("K2Node_Tunnel"))
				{
					continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			const ETunnelKind_ImportBpy TunnelKind = InferTunnelKind_ImportBpy(NodeObj);
			if (TunnelKind == ETunnelKind_ImportBpy::Entry && EntryTunnel && !bMappedEntryTunnel)
			{
				NodeMap.Add(Uid, EntryTunnel);
				RegisterImportedNodeUid_ImportBpy(BP, Uid, EntryTunnel);
				bMappedEntryTunnel = true;
			}
			else if (TunnelKind == ETunnelKind_ImportBpy::Exit && ExitTunnel && !bMappedExitTunnel)
			{
				NodeMap.Add(Uid, ExitTunnel);
				RegisterImportedNodeUid_ImportBpy(BP, Uid, ExitTunnel);
				bMappedExitTunnel = true;
			}
			else if (TunnelKind == ETunnelKind_ImportBpy::Unknown)
			{
				if (EntryTunnel && !bMappedEntryTunnel)
				{
					NodeMap.Add(Uid, EntryTunnel);
					RegisterImportedNodeUid_ImportBpy(BP, Uid, EntryTunnel);
					bMappedEntryTunnel = true;
				}
				else if (ExitTunnel && !bMappedExitTunnel)
				{
					NodeMap.Add(Uid, ExitTunnel);
					RegisterImportedNodeUid_ImportBpy(BP, Uid, ExitTunnel);
					bMappedExitTunnel = true;
				}
			}
		}
	}

		if (EffectiveGraphType == TEXT("event_graph"))
		{
			for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
			{
				const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
				if (!NodeObj.IsValid())
				{
					continue;
				}

				if (NodeObj->GetStringField(TEXT("node_class")) != TEXT("K2Node_CustomEvent"))
				{
					continue;
				}

				const FString Uid = NodeObj->GetStringField(TEXT("uid"));
				if (NodeMap.Contains(Uid))
				{
					continue;
				}

				UEdGraphNode* Node = CreateNode(Graph, NodeObj, OutError);
				if (Node)
				{
					NodeMap.Add(Uid, Node);
					RegisterImportedNodeUid_ImportBpy(BP, Uid, Node);
				}
			}

			if (NodeMap.Num() > 0)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			}
		}

		for (const TSharedPtr<FJsonValue>& NJ : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NJ->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			if (NodeMap.Contains(Uid))
			{
				continue;
			}

			UEdGraphNode* Node = nullptr;
			const FString NodeClass = NodeObj->GetStringField(TEXT("node_class"));
			if (NodeClass == TEXT("K2Node_FunctionEntry") && bTreatAsRegularFunctionGraph)
			{
				TArray<UK2Node_FunctionEntry*> EntryNodes;
				Graph->GetNodesOfClass(EntryNodes);
				Node = EntryNodes.Num() > 0 ? EntryNodes[0] : nullptr;
				if (!ApplyNodeJsonToNode_ImportBpy(Node, NodeObj, OutError, true))
				{
					return false;
				}
			}
			else if (NodeClass == TEXT("K2Node_FunctionResult") && bTreatAsRegularFunctionGraph)
			{
				if (ReusableResultNodeIndex < ExistingResultNodes.Num())
				{
					Node = ExistingResultNodes[ReusableResultNodeIndex++];
				}
				else
				{
					UK2Node_FunctionResult* ResultNode = NewObject<UK2Node_FunctionResult>(Graph);
					ResultNode->CreateNewGuid();
					ResultNode->PostPlacedNewNode();
					ResultNode->AllocateDefaultPins();
					Graph->AddNode(ResultNode, false, false);
					EnsureFunctionPins_ImportBpy(ResultNode, GraphOutputs);
					Node = ResultNode;
				}
				if (!ApplyNodeJsonToNode_ImportBpy(Node, NodeObj, OutError, true))
				{
					return false;
				}
			}
			else
			{
				Node = CreateNode(Graph, NodeObj, OutError);
			}

			if (!Node && !OutError.IsEmpty())
			{
				return false;
			}

			if (Node)
			{
				NodeMap.Add(Uid, Node);
				RegisterImportedNodeUid_ImportBpy(BP, Uid, Node);
			}
		}
	}

	if (NodesArr)
	{
		const bool bIsTopLevelGraphForStructuralRefresh =
			(Graph != nullptr) &&
			(Graph->GetOuter() == BP);
		if (bIsTopLevelGraphForStructuralRefresh)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
			if (!ExistingNode || !*ExistingNode)
			{
				continue;
			}

			UEdGraphNode* const ExistingNodePtr = *ExistingNode;
			const bool bOwnsNestedAnimGraph =
				Cast<UAnimGraphNode_StateMachineBase>(ExistingNodePtr) != nullptr ||
				Cast<UAnimStateNode>(ExistingNodePtr) != nullptr ||
				Cast<UAnimStateConduitNode>(ExistingNodePtr) != nullptr ||
				Cast<UAnimStateTransitionNode>(ExistingNodePtr) != nullptr;
			const bool bIsTopLevelAnimBlueprintGraphPass =
				bIsTopLevelGraphForStructuralRefresh &&
				Cast<UAnimBlueprint>(BP) != nullptr &&
				(Graph->IsA<UAnimationGraph>() ||
					(Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationGraphSchema>()));
			const bool bDeferNestedImportsForStateMachineNode =
				bIsTopLevelAnimBlueprintGraphPass &&
				Cast<UAnimGraphNode_StateMachineBase>(ExistingNodePtr) != nullptr;
			const UK2Node_AnimGetter* const AnimGetterNode = Cast<UK2Node_AnimGetter>(ExistingNodePtr);
			const bool bSkipPreApplyReconstructForAnimGetter =
				AnimGetterNode && !AnimGetterNode->HasValidBlueprint();
			const bool bSkipPreApplyReconstructForAnimGraphNode =
				Cast<UAnimBlueprint>(BP) != nullptr &&
				ExistingNodePtr->GetClass() &&
				ExistingNodePtr->GetClass()->GetName().StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive);
			const bool bCanReconstructNow =
				!bOwnsNestedAnimGraph &&
				!bSkipPreApplyReconstructForAnimGetter &&
				!bSkipPreApplyReconstructForAnimGraphNode &&
				(bIsTopLevelGraphForStructuralRefresh ||
				(FBlueprintEditorUtils::FindBlueprintForNode(ExistingNodePtr) != nullptr));
			if (bCanReconstructNow)
			{
				ExistingNodePtr->ReconstructNode();
			}
			if (!ApplyNodeJsonToNode_ImportBpy(
					ExistingNodePtr,
					NodeObj,
					OutError,
					bDeferNestedImportsForStateMachineNode))
			{
				return false;
			}
		}
	}

	if (!RestoreStateMachineAliasNodesAfterCreation_ImportBpy(BP, Graph, NodesArr, NodeMap, OutError))
	{
		return false;
	}
	if (!RestoreAnimReferenceNodesAfterCreation_ImportBpy(BP, NodesArr, NodeMap, OutError))
	{
		return false;
	}

	const bool bIsAnimationGraph =
		Graph->IsA<UAnimationGraph>() ||
		(Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationGraphSchema>());
	const bool bIsTopLevelBlueprintGraph =
		(Graph != nullptr) &&
		(Graph->GetOuter() == BP);
	if (NodesArr &&
		EffectiveGraphType == TEXT("event_graph") &&
		!bIsAnimationGraph &&
		!bIsBlendStackGraph &&
		!Cast<UAnimBlueprint>(BP))
	{
		FKismetEditorUtilities::GenerateBlueprintSkeleton(BP, true);
		RebindUnresolvedSelfContextCallNodes_ImportBpy(NodeMap);
	}
	if (NodesArr && Cast<UAnimBlueprint>(BP) && bIsAnimationGraph && bIsTopLevelBlueprintGraph)
	{
		RebindUnresolvedSelfContextCallNodes_ImportBpy(NodeMap);

		// Keep nested anim-node editor graphs stable during import. Skeleton rebuild here
		// can recreate state-machine / blend-stack subgraphs and invalidate restored links.
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
			if (!ExistingNode || !*ExistingNode)
			{
				continue;
			}

			UEdGraphNode* const ExistingNodePtr = *ExistingNode;
			const bool bIsStateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(ExistingNodePtr) != nullptr;
			const bool bIsBlendStackNode = ResolveBlendStackGraph_ImportBpy(ExistingNodePtr) != nullptr;
			if (!bIsStateMachineNode && !bIsBlendStackNode)
			{
				continue;
			}

			if (!ApplyNodeJsonToNode_ImportBpy(ExistingNodePtr, NodeObj, OutError, false))
			{
				return false;
			}
		}

		for (const TPair<FString, UEdGraphNode*>& NodePair : NodeMap)
		{
			if (UAnimGraphNode_LinkedAnimGraphBase* LinkedAnimNode = Cast<UAnimGraphNode_LinkedAnimGraphBase>(NodePair.Value))
			{
				LinkedAnimNode->ReconstructNode();
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
	GraphJson->TryGetArrayField(TEXT("connections"), ConnsArr);
	const bool bTraceConnectionAudit = IsConnectionTraceEnabled_ImportBpy();

	auto ResolveExistingConnectionPin = [](
		UEdGraphNode* Node,
		const FString& PinName,
		const FString& PinFullName,
		const FString& PinId,
		EEdGraphPinDirection Direction) -> UEdGraphPin*
	{
		if (!Node)
		{
			return nullptr;
		}

		UEdGraphPin* Pin = FindPinById_ImportBpy(Node, PinId);
		if (Pin && Pin->Direction != Direction)
		{
			Pin = nullptr;
		}

		if (!Pin && !PinFullName.IsEmpty())
		{
			Pin = FindExistingPinFlexible_ImportBpy(Node, PinFullName, Direction);
		}
		if (!Pin && !PinName.IsEmpty())
		{
			Pin = FindExistingPinFlexible_ImportBpy(Node, PinName, Direction);
		}

		return Pin;
	};

	auto AuditSerializedConnections = [&](const TCHAR* PhaseLabel, const bool bRequireAll) -> bool
	{
		if (!ConnsArr)
		{
			return true;
		}

		int32 PresentConnections = 0;
		int32 MissingConnections = 0;
		int32 TrackedPresent = 0;
		int32 TrackedMissing = 0;
		FString FirstMissingDetail;

		for (const TSharedPtr<FJsonValue>& ConnValue : *ConnsArr)
		{
			const TSharedPtr<FJsonObject> ConnObj = ConnValue.IsValid() ? ConnValue->AsObject() : nullptr;
			if (!ConnObj.IsValid())
			{
				continue;
			}

			FString SrcUid;
			FString DstUid;
			FString SrcPin;
			FString DstPin;
			FString SrcPinFull;
			FString DstPinFull;
			FString SrcPinId;
			FString DstPinId;
			ConnObj->TryGetStringField(TEXT("src_node"), SrcUid);
			ConnObj->TryGetStringField(TEXT("dst_node"), DstUid);
			ConnObj->TryGetStringField(TEXT("src_pin"), SrcPin);
			ConnObj->TryGetStringField(TEXT("dst_pin"), DstPin);
			ConnObj->TryGetStringField(TEXT("src_pin_full"), SrcPinFull);
			ConnObj->TryGetStringField(TEXT("dst_pin_full"), DstPinFull);
			ConnObj->TryGetStringField(TEXT("src_pin_id"), SrcPinId);
			ConnObj->TryGetStringField(TEXT("dst_pin_id"), DstPinId);

			const bool bTrackedConnection = IsTrackedTraversalConnection_ImportBpy(SrcUid, DstUid);

			UEdGraphNode* const* SrcNodePtr = NodeMap.Find(SrcUid);
			UEdGraphNode* const* DstNodePtr = NodeMap.Find(DstUid);
			const UEdGraphNode* SrcNode = (SrcNodePtr && *SrcNodePtr) ? *SrcNodePtr : nullptr;
			const UEdGraphNode* DstNode = (DstNodePtr && *DstNodePtr) ? *DstNodePtr : nullptr;

			UEdGraphPin* ResolvedSrcPin =
				ResolveExistingConnectionPin(const_cast<UEdGraphNode*>(SrcNode), SrcPin, SrcPinFull, SrcPinId, EGPD_Output);
			UEdGraphPin* ResolvedDstPin =
				ResolveExistingConnectionPin(const_cast<UEdGraphNode*>(DstNode), DstPin, DstPinFull, DstPinId, EGPD_Input);

			const bool bConnected =
				ResolvedSrcPin &&
				ResolvedDstPin &&
				ResolvedSrcPin->LinkedTo.Contains(ResolvedDstPin) &&
				ResolvedDstPin->LinkedTo.Contains(ResolvedSrcPin);

			if (bConnected)
			{
				++PresentConnections;
				if (bTrackedConnection)
				{
					++TrackedPresent;
				}
			}
			else
			{
				++MissingConnections;
				if (bTrackedConnection)
				{
					++TrackedMissing;
				}

				if (FirstMissingDetail.IsEmpty())
				{
					FirstMissingDetail = FString::Printf(
						TEXT("%s.%s -> %s.%s | src_resolved=%d dst_resolved=%d"),
						*SrcUid,
						*(!SrcPinFull.IsEmpty() ? SrcPinFull : SrcPin),
						*DstUid,
						*(!DstPinFull.IsEmpty() ? DstPinFull : DstPin),
						ResolvedSrcPin ? 1 : 0,
						ResolvedDstPin ? 1 : 0);
				}
			}

			if (bTraceConnectionAudit && bTrackedConnection)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy][ConnectionAudit][%s] tracked src=%s pin=%s dst=%s pin=%s connected=%d src_pin=%s dst_pin=%s src_links=%d dst_links=%d"),
					PhaseLabel ? PhaseLabel : TEXT("connections"),
					*SrcUid,
					*(!SrcPinFull.IsEmpty() ? SrcPinFull : SrcPin),
					*DstUid,
					*(!DstPinFull.IsEmpty() ? DstPinFull : DstPin),
					bConnected ? 1 : 0,
					ResolvedSrcPin ? *ResolvedSrcPin->GetName() : TEXT("<missing>"),
					ResolvedDstPin ? *ResolvedDstPin->GetName() : TEXT("<missing>"),
					ResolvedSrcPin ? ResolvedSrcPin->LinkedTo.Num() : -1,
					ResolvedDstPin ? ResolvedDstPin->LinkedTo.Num() : -1);
			}
		}

		if (bTraceConnectionAudit || TrackedMissing > 0)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ExportBpy][ConnectionAudit][%s] graph=%s present=%d missing=%d tracked_present=%d tracked_missing=%d"),
				PhaseLabel ? PhaseLabel : TEXT("connections"),
				*Graph->GetName(),
				PresentConnections,
				MissingConnections,
				TrackedPresent,
				TrackedMissing);
		}

		if (bRequireAll && MissingConnections > 0)
		{
			OutError = FString::Printf(
				TEXT("[%s] Serialized connection parity mismatch: graph=%s missing=%d first_missing=%s"),
				PhaseLabel ? PhaseLabel : TEXT("connections"),
				*Graph->GetName(),
				MissingConnections,
				FirstMissingDetail.IsEmpty() ? TEXT("<none>") : *FirstMissingDetail);
			return false;
		}

		return true;
	};

	auto ReplaySerializedConnections = [&](const TCHAR* PhaseLabel, const bool bAllowUnresolvedAtStall) -> bool
	{
		if (!ConnsArr)
		{
			return true;
		}

		TArray<TSharedPtr<FJsonObject>> PendingConnections;
		for (const TSharedPtr<FJsonValue>& CJ : *ConnsArr)
		{
			const TSharedPtr<FJsonObject> ConnObj = CJ->AsObject();
			if (!ConnObj.IsValid())
			{
				continue;
			}

			PendingConnections.Add(ConnObj);
		}

		while (PendingConnections.Num() > 0)
		{
			bool bMadeProgress = false;
			FString LastConnectError;
			TArray<TSharedPtr<FJsonObject>> RemainingConnections;
			RemainingConnections.Reserve(PendingConnections.Num());

			for (const TSharedPtr<FJsonObject>& ConnObj : PendingConnections)
			{
				if (!ConnObj.IsValid())
				{
					continue;
				}

				FString SrcUid = ConnObj->GetStringField(TEXT("src_node"));
				FString SrcPin = ConnObj->GetStringField(TEXT("src_pin"));
				FString DstUid = ConnObj->GetStringField(TEXT("dst_node"));
				FString DstPin = ConnObj->GetStringField(TEXT("dst_pin"));
				FString SrcPinFull;
				FString DstPinFull;
				FString SrcPinId;
				FString DstPinId;
				ConnObj->TryGetStringField(TEXT("src_pin_full"), SrcPinFull);
				ConnObj->TryGetStringField(TEXT("dst_pin_full"), DstPinFull);
				ConnObj->TryGetStringField(TEXT("src_pin_id"), SrcPinId);
				ConnObj->TryGetStringField(TEXT("dst_pin_id"), DstPinId);

				UEdGraphNode** SrcNodePtr = NodeMap.Find(SrcUid);
				UEdGraphNode** DstNodePtr = NodeMap.Find(DstUid);
				if (!SrcNodePtr || !DstNodePtr)
				{
					OutError = FString::Printf(
						TEXT("[%s] Connection references missing node(s): %s -> %s"),
						PhaseLabel ? PhaseLabel : TEXT("connections"),
						*SrcUid,
						*DstUid);
					return false;
				}

				FString ConnectError;
				if (ConnectPins(*SrcNodePtr, SrcPin, SrcPinFull, SrcPinId, *DstNodePtr, DstPin, DstPinFull, DstPinId, ConnectError))
				{
					bMadeProgress = true;
					continue;
				}

				LastConnectError = ConnectError;
				RemainingConnections.Add(ConnObj);
			}

			if (!bMadeProgress)
			{
				if (bAllowUnresolvedAtStall)
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[ExportBpy] [%s] Deferring unresolved connections (remaining=%d, last_error=%s)"),
						PhaseLabel ? PhaseLabel : TEXT("connections"),
						PendingConnections.Num(),
						LastConnectError.IsEmpty() ? TEXT("<none>") : *LastConnectError);
					return true;
				}

				if (LastConnectError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("[%s] Failed to resolve deferred graph connections"),
						PhaseLabel ? PhaseLabel : TEXT("connections"));
				}
				else
				{
					OutError = FString::Printf(
						TEXT("[%s] %s"),
						PhaseLabel ? PhaseLabel : TEXT("connections"),
						*LastConnectError);
				}
				return false;
			}

			PendingConnections = MoveTemp(RemainingConnections);
		}

		return true;
	};

	// Prime promotable operator literal defaults before any link replay. For round-tripped
	// .bp.py payloads we may not have explicit pin-type contracts, so wildcard operators
	// can lock into the wrong overload if links are restored before scalar defaults (e.g. +2).
	if (NodesArr)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
			if (!ExistingNode || !*ExistingNode)
			{
				continue;
			}

			UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(*ExistingNode);
			if (!CallNode)
			{
				continue;
			}

			const FString NodeClassName = CallNode->GetClass()->GetName();
			const bool bIsPromotableOperator =
				NodeClassName == TEXT("K2Node_PromotableOperator") ||
				NodeClassName == TEXT("K2Node_CommutativeAssociativeBinaryOperator");
			if (!bIsPromotableOperator)
			{
				continue;
			}

			if (!ApplyPinDefaults_ImportBpy(CallNode, NodeObj, OutError, true))
			{
				return false;
			}
		}
	}

	BreakAllGraphLinks_ImportBpy(Graph);
	if (!ReplaySerializedConnections(TEXT("initial"), true))
	{
		return false;
	}
	if (!AuditSerializedConnections(TEXT("after_initial_connections"), false))
	{
		return false;
	}
	LogTrackedPromotableNodesFromMap_ImportBpy(NodesArr, NodeMap, TEXT("after_initial_connections"));

	if (!RestoreCreateDelegateNodesAfterConnections_ImportBpy(NodesArr, NodeMap, false, OutError))
	{
		return false;
	}

	if (NodesArr)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
			if (!ExistingNode || !*ExistingNode)
			{
				continue;
			}

			if (!ApplyPinDefaults_ImportBpy(*ExistingNode, NodeObj, OutError, false))
			{
				return false;
			}
		}
	}

	if (Cast<UAnimBlueprint>(BP) && bIsAnimationGraph)
	{
		ResolveUseCachedPoseLinksInGraph_ImportBpy(Graph);
	}

	if (NodesArr)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
			if (!ExistingNode || !*ExistingNode)
			{
				continue;
			}

			// Some nodes recreate hidden or promoted pins after links are restored.
			// Replay serialized pin GUIDs once more against the final post-connection shape.
			if (!ApplyPinIds_ImportBpy(*ExistingNode, NodeObj, OutError))
			{
				return false;
			}
		}
	}

	if (!RestorePromotableOperatorBindingsAfterConnections_ImportBpy(
			NodesArr,
			NodeMap,
			TEXT("promotable_pass_1"),
			OutError))
	{
		return false;
	}
	LogTrackedPromotableNodesFromMap_ImportBpy(NodesArr, NodeMap, TEXT("after_promotable_pass_1"));

	if (!ReplaySerializedConnections(TEXT("post_promotable_rebind"), false))
	{
		return false;
	}
	if (!AuditSerializedConnections(TEXT("after_post_promotable_rebind"), true))
	{
		return false;
	}

	// Replaying links can re-promote wildcard operators based on transient pin state.
	// Run one final binding pass and stop reconnecting afterwards so exported function refs
	// remain identical to the source blueprint signatures.
	if (!RestorePromotableOperatorBindingsAfterConnections_ImportBpy(
			NodesArr,
			NodeMap,
			TEXT("promotable_pass_2"),
			OutError))
	{
		return false;
	}
	LogTrackedPromotableNodesFromMap_ImportBpy(NodesArr, NodeMap, TEXT("after_promotable_pass_2"));
	if (!AuditSerializedConnections(TEXT("after_promotable_pass_2"), false))
	{
		return false;
	}

	if (!ReplaySerializedConnections(TEXT("post_delegate_rebind"), false))
	{
		return false;
	}
	if (!AuditSerializedConnections(TEXT("after_post_delegate_rebind"), true))
	{
		return false;
	}

	// Keep CreateDelegate bindings as the final operation in graph population.
	// Additional connection replay can transiently clear SelectedFunctionName
	// when delegate pins are temporarily disconnected during node notifications.
	if (!RestoreCreateDelegateNodesAfterConnections_ImportBpy(NodesArr, NodeMap, true, OutError))
	{
		return false;
	}

	// Re-applying serialized graph links for delegate recovery can still trigger
	// late wildcard promotion on arithmetic operator nodes. Lock promotable operator
	// function/type bindings one last time before final defaults replay/validation.
	if (!RestorePromotableOperatorBindingsAfterConnections_ImportBpy(
			NodesArr,
			NodeMap,
			TEXT("promotable_pass_3"),
			OutError))
	{
		return false;
	}
	LogTrackedPromotableNodesFromMap_ImportBpy(NodesArr, NodeMap, TEXT("after_promotable_pass_3"));
	if (!AuditSerializedConnections(TEXT("after_promotable_pass_3"), false))
	{
		return false;
	}

	// Final stabilization pass: post-bind reconstructs can silently drop typed links
	// on promotable arithmetic nodes. Replaying serialized connections here restores
	// the exported topology before strict default validation and compile.
	if (!ReplaySerializedConnections(TEXT("final_connection_stabilize"), false))
	{
		return false;
	}
	if (!AuditSerializedConnections(TEXT("after_final_connection_stabilize"), true))
	{
		return false;
	}

	if (NodesArr && Cast<UAnimBlueprint>(BP) && bIsAnimationGraph && bIsTopLevelBlueprintGraph)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArr)
		{
			const TSharedPtr<FJsonObject> NodeObj = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			if (!NodeObj.IsValid())
			{
				continue;
			}

			const FString Uid = NodeObj->GetStringField(TEXT("uid"));
			UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
			UAnimGraphNode_StateMachineBase* const StateMachineNode =
				(ExistingNode && *ExistingNode) ? Cast<UAnimGraphNode_StateMachineBase>(*ExistingNode) : nullptr;
			if (!StateMachineNode)
			{
				continue;
			}

			const FString StateMachineGraphJsonText = GetSpecialNodePropString_ImportBpy(NodeObj, TEXT("StateMachineGraphJson"));
			if (StateMachineGraphJsonText.IsEmpty())
			{
				continue;
			}

			if (!ReplayStateMachineGraphFromJsonTextFinal_ImportBpy(
					BP,
					StateMachineNode,
					StateMachineGraphJsonText,
					TEXT("after_final_connection_stabilize"),
					OutError))
			{
				return false;
			}
		}
	}

	if (!ShouldSkipStrictDefaultValidation_ImportBpy())
	{
		LogTrackedPromotableNodesFromMap_ImportBpy(NodesArr, NodeMap, TEXT("before_strict_default_validation"));
		if (!ReplayAndValidateSerializedNodeDefaults_ImportBpy(NodesArr, NodeMap, Graph->GetName(), OutError))
		{
			return false;
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExportBpy] Skip strict default validation for graph %s (EXPORTBPY_SKIP_STRICT_DEFAULT_VALIDATION enabled)"),
			*Graph->GetName());
	}

	return true;
}

bool UBPDirectImporter::CreateGraph(
	UBlueprint* BP,
	const TSharedPtr<FJsonObject>& GraphJson,
	FString& OutError)
{
	if (!GraphJson.IsValid()) return false;

	FString GraphName = GraphJson->GetStringField(TEXT("name"));
	FString GraphType = GraphJson->GetStringField(TEXT("graph_type"));
	UEdGraph* Graph = nullptr;
	if (!EnsureGraphExists_ImportBpy(BP, GraphJson, Graph, GraphType, GraphName, OutError))
	{
		return false;
	}
	const bool bPreserveTunnelNodes = GraphType == TEXT("macro") || GraphType == TEXT("composite");
	return PopulateGraph(BP, Graph, GraphJson, bPreserveTunnelNodes, OutError);
}

// ─── CreateNode ──────────────────────────────────────────────────────────────

UEdGraphNode* UBPDirectImporter::CreateNode(
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError)
{
	if (!NodeJson.IsValid())
	{
		return nullptr;
	}

	FString NodeClass    = NodeJson->GetStringField(TEXT("node_class"));
	FString FunctionRef  = NodeJson->GetStringField(TEXT("function_ref"));
	FString MemberName   = NodeJson->GetStringField(TEXT("member_name"));
	FString TargetType;
	NodeJson->TryGetStringField(TEXT("target_type"), TargetType);

	UEdGraphNode* Result = nullptr;

	// ── Event ────────────────────────────────────────────────────────
	if (NodeClass == TEXT("K2Node_Event"))
	{
		Result = CreateEventNode(Graph, MemberName);
	}
	// ── Custom Event ─────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_CustomEvent"))
	{
		UK2Node_CustomEvent* CE = NewObject<UK2Node_CustomEvent>(Graph);
		CE->CustomFunctionName = FName(*MemberName);
		CE->CreateNewGuid();
		CE->PostPlacedNewNode();
		CE->AllocateDefaultPins();
		Graph->AddNode(CE, false, false);

		const TArray<TSharedPtr<FJsonValue>>* CustomParamsArr = nullptr;
		if (NodeJson->TryGetArrayField(TEXT("custom_params"), CustomParamsArr) && CustomParamsArr)
		{
			for (const TSharedPtr<FJsonValue>& ParamValue : *CustomParamsArr)
			{
				const TSharedPtr<FJsonObject> ParamObj = ParamValue.IsValid() ? ParamValue->AsObject() : nullptr;
				if (!ParamObj.IsValid())
				{
					continue;
				}

				FString ParamName;
				FString ParamTypeText;
				ParamObj->TryGetStringField(TEXT("name"), ParamName);
				ParamObj->TryGetStringField(TEXT("type"), ParamTypeText);
				if (ParamName.IsEmpty() || ParamTypeText.IsEmpty())
				{
					continue;
				}

				FEdGraphPinType ParamType;
				ParsePinTypeString_ImportBpy(ParamTypeText, ParamType);
				CE->CreateUserDefinedPin(FName(*ParamName), ParamType, EGPD_Output, false);
			}
		}

		if (const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
			NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) && NodePropsObj && NodePropsObj->IsValid())
		{
			FString NetFlagsText;
			if ((*NodePropsObj)->TryGetStringField(TEXT("NetFlags"), NetFlagsText) && !NetFlagsText.IsEmpty())
			{
				const uint64 ParsedNetFlags = FCString::Strtoui64(*NetFlagsText, nullptr, 10);
				CE->FunctionFlags &= ~FUNC_NetFuncFlags;
				CE->FunctionFlags |= static_cast<uint32>(ParsedNetFlags);
			}

			FString BoolText;
			if ((*NodePropsObj)->TryGetStringField(TEXT("CallInEditor"), BoolText))
			{
				CE->bCallInEditor = BoolText.Equals(TEXT("true"), ESearchCase::IgnoreCase) || BoolText == TEXT("1");
			}
			if ((*NodePropsObj)->TryGetStringField(TEXT("IsDeprecated"), BoolText))
			{
				CE->bIsDeprecated = BoolText.Equals(TEXT("true"), ESearchCase::IgnoreCase) || BoolText == TEXT("1");
			}

			FString DeprecationMessage;
			if ((*NodePropsObj)->TryGetStringField(TEXT("DeprecationMessage"), DeprecationMessage))
			{
				CE->DeprecationMessage = DeprecationMessage;
			}
		}
		Result = CE;
	}
	// ── Call Function ────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_CallFunction"))
	{
		Result = CreateCallFunctionNode(Graph, FunctionRef, NodeClass, NodeJson, OutError);
	}
	// ── Message ──────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_Message"))
	{
		Result = CreateMessageNode(Graph, FunctionRef, NodeJson, OutError);
	}
	// ── Variable Get ─────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_VariableGet"))
	{
		Result = CreateVariableNode(Graph, NodeJson, true, OutError);
	}
	// ── Variable Set ─────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_VariableSet"))
	{
		Result = CreateVariableNode(Graph, NodeJson, false, OutError);
	}
	// ── Branch ───────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_IfThenElse"))
	{
		Result = CreateBranchNode(Graph);
	}
	// ── Enhanced Input Action ───────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_EnhancedInputAction"))
	{
		Result = CreateEnhancedInputActionNode_ImportBpy(Graph, NodeJson, OutError);
	}
	// ── Input Key ────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_InputKey"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{
				TEXT("InputKey"),
				TEXT("bConsumeInput"),
				TEXT("bExecuteWhenPaused"),
				TEXT("bOverrideParentBinding"),
				TEXT("bControl"),
				TEXT("bAlt"),
				TEXT("bShift"),
				TEXT("bCommand"),
			},
			OutError);
	}
	// ── Get Input Action Value ───────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_GetInputActionValue"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("InputAction") },
			OutError);
	}
	// ── Sequence ─────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_ExecutionSequence"))
	{
		UK2Node_ExecutionSequence* Seq = NewObject<UK2Node_ExecutionSequence>(Graph);
		Seq->CreateNewGuid();
		Seq->PostPlacedNewNode();
		Seq->AllocateDefaultPins();
		Graph->AddNode(Seq, false, false);
		Result = Seq;
	}
	// ── Composite ────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_Composite"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Reroute / Knot ───────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_Knot"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Property Access ───────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_PropertyAccess"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("Path"), TEXT("ContextId") },
			OutError);
	}
	// ── Get Array Item ────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_GetArrayItem"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("bReturnByRefDesired") },
			OutError);
	}
	// ── Set By-Ref Variable ───────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_VariableSetRef"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Anim Node Reference ───────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_AnimNodeReference"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("Tag") },
			OutError);
	}
	// ── State Machine Graph Nodes ────────────────────────────────────
	else if (NodeClass == TEXT("AnimStateEntryNode") ||
		NodeClass == TEXT("K2Node_AnimStateEntryNode") ||
		NodeClass == TEXT("AnimStateNode") ||
		NodeClass == TEXT("K2Node_AnimStateNode") ||
		NodeClass == TEXT("AnimStateTransitionNode") ||
		NodeClass == TEXT("K2Node_AnimStateTransitionNode") ||
		NodeClass == TEXT("AnimStateAliasNode") ||
		NodeClass == TEXT("K2Node_AnimStateAliasNode") ||
		NodeClass == TEXT("AnimStateConduitNode") ||
		NodeClass == TEXT("K2Node_AnimStateConduitNode") ||
		NodeClass == TEXT("EdGraphNode_Comment"))
	{
		FString ResolvedStateMachineNodeClass = NodeClass;
		if (NodeClass == TEXT("K2Node_AnimStateEntryNode"))
		{
			ResolvedStateMachineNodeClass = TEXT("AnimStateEntryNode");
		}
		else if (NodeClass == TEXT("K2Node_AnimStateNode"))
		{
			ResolvedStateMachineNodeClass = TEXT("AnimStateNode");
		}
		else if (NodeClass == TEXT("K2Node_AnimStateTransitionNode"))
		{
			ResolvedStateMachineNodeClass = TEXT("AnimStateTransitionNode");
		}
		else if (NodeClass == TEXT("K2Node_AnimStateAliasNode"))
		{
			ResolvedStateMachineNodeClass = TEXT("AnimStateAliasNode");
		}
		else if (NodeClass == TEXT("K2Node_AnimStateConduitNode"))
		{
			ResolvedStateMachineNodeClass = TEXT("AnimStateConduitNode");
		}

		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, ResolvedStateMachineNodeClass, NodeJson, {}, OutError);
	}
	// ── Anim Graph Nodes ──────────────────────────────────────────────
	else if (NodeClass.StartsWith(TEXT("AnimGraphNode_")))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{
				TEXT("Node"),
				TEXT("ShowPinForProperties"),
				TEXT("CustomPinProperties"),
				TEXT("InitialUpdateFunction"),
				TEXT("BecomeRelevantFunction"),
				TEXT("UpdateFunction"),
				TEXT("OnMotionMatchingStateUpdatedFunction"),
			},
			OutError);
	}
	// ── Macro Instance ───────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_MacroInstance"))
	{
		Result = CreateMacroInstanceNode(Graph, TargetType, MemberName, OutError);
	}
	// ── Tunnel ───────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_Tunnel"))
	{
		const ETunnelKind_ImportBpy TunnelKind = InferTunnelKind_ImportBpy(NodeJson);

		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			UK2Node_Tunnel* const ExistingTunnel = Cast<UK2Node_Tunnel>(ExistingNode);
			if (!ExistingTunnel)
			{
				continue;
			}

			const bool bExistingIsEntry = ExistingTunnel->bCanHaveOutputs && !ExistingTunnel->bCanHaveInputs;
			const bool bExistingIsExit = ExistingTunnel->bCanHaveInputs && !ExistingTunnel->bCanHaveOutputs;
			if ((TunnelKind == ETunnelKind_ImportBpy::Entry && bExistingIsEntry) ||
				(TunnelKind == ETunnelKind_ImportBpy::Exit && bExistingIsExit))
			{
				Result = ExistingTunnel;
				break;
			}
		}

		if (Result)
		{
			// Reuse graph-owned entry/exit tunnels when present instead of creating duplicates.
			goto NodeCreationDone;
		}

		UK2Node_Tunnel* TunnelNode = NewObject<UK2Node_Tunnel>(Graph);
		if (TunnelKind == ETunnelKind_ImportBpy::Entry)
		{
			TunnelNode->bCanHaveInputs = false;
			TunnelNode->bCanHaveOutputs = true;
		}
		else if (TunnelKind == ETunnelKind_ImportBpy::Exit)
		{
			TunnelNode->bCanHaveInputs = true;
			TunnelNode->bCanHaveOutputs = false;
		}
		else
		{
			TunnelNode->bCanHaveInputs = true;
			TunnelNode->bCanHaveOutputs = true;
		}

		TunnelNode->CreateNewGuid();
		TunnelNode->PostPlacedNewNode();
		TunnelNode->AllocateDefaultPins();
		Graph->AddNode(TunnelNode, false, false);

		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			UK2Node_Tunnel* const ExistingTunnel = Cast<UK2Node_Tunnel>(ExistingNode);
			if (!ExistingTunnel || ExistingTunnel == TunnelNode)
			{
				continue;
			}

			const bool bNewIsEntry = TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs;
			const bool bNewIsExit = TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs;
			const bool bExistingIsEntry = ExistingTunnel->bCanHaveOutputs && !ExistingTunnel->bCanHaveInputs;
			const bool bExistingIsExit = ExistingTunnel->bCanHaveInputs && !ExistingTunnel->bCanHaveOutputs;

			if (bNewIsEntry && bExistingIsExit && !TunnelNode->OutputSourceNode && !ExistingTunnel->InputSinkNode)
			{
				TunnelNode->OutputSourceNode = ExistingTunnel;
				ExistingTunnel->InputSinkNode = TunnelNode;
				break;
			}
			if (bNewIsExit && bExistingIsEntry && !TunnelNode->InputSinkNode && !ExistingTunnel->OutputSourceNode)
			{
				TunnelNode->InputSinkNode = ExistingTunnel;
				ExistingTunnel->OutputSourceNode = TunnelNode;
				break;
			}
		}

		Result = TunnelNode;
	}
	// ── Dynamic Cast ─────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_DynamicCast"))
	{
		UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(Graph);
		CastNode->TargetType = ResolveNamedObject_ImportBpy<UClass>(TargetType);
		if (!TargetType.IsEmpty() && !CastNode->TargetType)
		{
			OutError = FString::Printf(TEXT("Cannot resolve dynamic cast target '%s'"), *TargetType);
			return nullptr;
		}
		CastNode->CreateNewGuid();
		CastNode->PostPlacedNewNode();
		Graph->AddNode(CastNode, false, false);
		CastNode->AllocateDefaultPins();
		if (CastNode->TargetType)
		{
			CastNode->ReconstructNode();
		}
		Result = CastNode;
	}
	// ── Select ───────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_Select"))
	{
		UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);
		SelectNode->CreateNewGuid();
		SelectNode->PostPlacedNewNode();
		SelectNode->AllocateDefaultPins();
		Graph->AddNode(SelectNode, false, false);
		Result = SelectNode;
	}
	else if (NodeClass == TEXT("K2Node_AnimGetter"))
	{
		UK2Node_AnimGetter* GetterNode = NewObject<UK2Node_AnimGetter>(Graph);
		if (UAnimBlueprint* OwningAnimBlueprint = ResolveOwningAnimBlueprintForGraph_ImportBpy(Graph))
		{
			GetterNode->SourceAnimBlueprint = OwningAnimBlueprint;
		}
		GetterNode->CreateNewGuid();
		GetterNode->PostPlacedNewNode();
		Graph->AddNode(GetterNode, false, false);

		// Anim getter nodes require a valid owning blueprint during pin allocation.
		if (!GetterNode->SourceAnimBlueprint)
		{
			if (UAnimBlueprint* OwningAnimBlueprint = ResolveOwningAnimBlueprintForGraph_ImportBpy(Graph))
			{
				GetterNode->SourceAnimBlueprint = OwningAnimBlueprint;
			}
		}

		FString GetterFunctionRef = FunctionRef;
		FString GetterFunctionName = GetterFunctionRef;
		FString GetterFunctionOwnerClass;
		UFunction* GetterFunction = nullptr;

		if (GetterFunctionRef.Split(TEXT("::"), &GetterFunctionOwnerClass, &GetterFunctionName))
		{
			if (UClass* ExplicitOwnerClass = ResolveNamedObject_ImportBpy<UClass>(GetterFunctionOwnerClass))
			{
				GetterFunction = ExplicitOwnerClass->FindFunctionByName(FName(*GetterFunctionName));
			}
		}
		else
		{
			const FString AnimGetterClassPath = GetNodePropString_ImportBpy(NodeJson, TEXT("AnimGetterClass"));
			if (!AnimGetterClassPath.IsEmpty())
			{
				if (UClass* AnimGetterClass = ResolveNamedObject_ImportBpy<UClass>(AnimGetterClassPath))
				{
					GetterFunction = AnimGetterClass->FindFunctionByName(FName(*GetterFunctionName));
				}
			}

			if (!GetterFunction)
			{
				GetterFunction = ResolveSelfContextFunction_ImportBpy(Graph, GetterFunctionName);
			}

			if (!GetterFunction && IsQualifiedFunctionReference_ImportBpy(GetterFunctionRef))
			{
				GetterFunction = ResolveNamedObject_ImportBpy<UFunction>(GetterFunctionRef);
			}
		}

		if (GetterFunction)
		{
			GetterNode->SetFromFunction(GetterFunction);
		}
		else if (!GetterFunctionName.IsEmpty())
		{
			GetterNode->FunctionReference.SetSelfMember(FName(*GetterFunctionName));
		}

		GetterNode->AllocateDefaultPins();
		Result = GetterNode;
	}
	else if (NodeClass == TEXT("K2Node_TransitionRuleGetter"))
	{
		UK2Node_TransitionRuleGetter* GetterNode = NewObject<UK2Node_TransitionRuleGetter>(Graph);
		GetterNode->CreateNewGuid();
		GetterNode->PostPlacedNewNode();
		Graph->AddNode(GetterNode, false, false);
		GetterNode->AllocateDefaultPins();
		Result = GetterNode;
	}
	// ── Enum Equality / Inequality ───────────────────────────────────
	else if (NodeClass == TEXT("K2Node_EnumEquality") || NodeClass == TEXT("K2Node_EnumInequality"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Enum Name Helpers ────────────────────────────────────────────
	else if (
		NodeClass == TEXT("K2Node_GetEnumeratorName") ||
		NodeClass == TEXT("K2Node_GetEnumeratorNameAsString"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Switch Enum ──────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_SwitchEnum"))
	{
		UK2Node_SwitchEnum* SwitchNode = NewObject<UK2Node_SwitchEnum>(Graph);
		if (UEnum* EnumObject = ResolveNamedObject_ImportBpy<UEnum>(TargetType))
		{
			SwitchNode->Enum = EnumObject;
		}
		else if (!TargetType.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Cannot resolve switch enum target '%s'"), *TargetType);
			return nullptr;
		}
		SwitchNode->CreateNewGuid();
		SwitchNode->PostPlacedNewNode();
		SwitchNode->AllocateDefaultPins();
		Graph->AddNode(SwitchNode, false, false);
		Result = SwitchNode;
	}
	// ── Switch Integer ───────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_SwitchInteger"))
	{
		UK2Node_SwitchInteger* SwitchNode = NewObject<UK2Node_SwitchInteger>(Graph);
		SwitchNode->CreateNewGuid();
		SwitchNode->PostPlacedNewNode();
		SwitchNode->AllocateDefaultPins();
		Graph->AddNode(SwitchNode, false, false);
		Result = SwitchNode;
	}
	// ── Set Fields In Struct ─────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_SetFieldsInStruct"))
	{
		UK2Node_SetFieldsInStruct* SetFieldsNode = NewObject<UK2Node_SetFieldsInStruct>(Graph);
		if (!SetFieldsNode)
		{
			OutError = TEXT("Failed to allocate K2Node_SetFieldsInStruct");
			return nullptr;
		}

		if (!TargetType.IsEmpty())
		{
			SetFieldsNode->StructType = ResolveNamedObject_ImportBpy<UScriptStruct>(TargetType);
			if (!SetFieldsNode->StructType)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve set-fields struct target '%s'"),
					*TargetType);
				return nullptr;
			}
		}

		SetFieldsNode->CreateNewGuid();
		SetFieldsNode->PostPlacedNewNode();
		Graph->AddNode(SetFieldsNode, false, false);
		SetFieldsNode->AllocateDefaultPins();

		Result = SetFieldsNode;
	}
	// ── Make Array ───────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_MakeArray"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Construct Object Nodes ───────────────────────────────────────
	else if (
		NodeClass == TEXT("K2Node_GenericCreateObject") ||
		NodeClass == TEXT("K2Node_ConstructObjectFromClass"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Break Struct ─────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_BreakStruct"))
	{
		UK2Node_BreakStruct* StructNode = NewObject<UK2Node_BreakStruct>(Graph);
		StructNode->StructType = ResolveNamedObject_ImportBpy<UScriptStruct>(TargetType);
		if (!TargetType.IsEmpty() && !StructNode->StructType)
		{
			OutError = FString::Printf(TEXT("Cannot resolve break struct target '%s'"), *TargetType);
			return nullptr;
		}
		StructNode->CreateNewGuid();
		StructNode->PostPlacedNewNode();
		StructNode->AllocateDefaultPins();
		Graph->AddNode(StructNode, false, false);
		Result = StructNode;
	}
	// ── Make Struct ──────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_MakeStruct"))
	{
		UK2Node_MakeStruct* StructNode = NewObject<UK2Node_MakeStruct>(Graph);
		StructNode->StructType = ResolveNamedObject_ImportBpy<UScriptStruct>(TargetType);
		if (!TargetType.IsEmpty() && !StructNode->StructType)
		{
			OutError = FString::Printf(TEXT("Cannot resolve make struct target '%s'"), *TargetType);
			return nullptr;
		}
		StructNode->CreateNewGuid();
		StructNode->PostPlacedNewNode();
		StructNode->AllocateDefaultPins();
		Graph->AddNode(StructNode, false, false);
		Result = StructNode;
	}
	// ── Self ─────────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_Self"))
	{
		UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
		SelfNode->CreateNewGuid();
		SelfNode->PostPlacedNewNode();
		SelfNode->AllocateDefaultPins();
		Graph->AddNode(SelfNode, false, false);
		Result = SelfNode;
	}
	// ── Get Subsystem Variants ───────────────────────────────────────
	else if (
		NodeClass == TEXT("K2Node_GetSubsystem") ||
		NodeClass == TEXT("K2Node_GetSubsystemFromPC") ||
		NodeClass == TEXT("K2Node_GetEngineSubsystem") ||
		NodeClass == TEXT("K2Node_GetEditorSubsystem"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("CustomClass") },
			OutError);
	}
	// ── Create Delegate ──────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_CreateDelegate"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("SelectedFunctionName"), TEXT("SelectedFunctionGuid") },
			OutError);
	}
	// ── Multicast Delegate Nodes ─────────────────────────────────────
	else if (
		NodeClass == TEXT("K2Node_AddDelegate") ||
		NodeClass == TEXT("K2Node_AssignDelegate") ||
		NodeClass == TEXT("K2Node_RemoveDelegate") ||
		NodeClass == TEXT("K2Node_CallDelegate"))
	{
		if (GetNodePropString_ImportBpy(NodeJson, TEXT("DelegateReference")).IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Delegate node '%s' is missing exported DelegateReference metadata"),
				*NodeClass);
			return nullptr;
		}

		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("DelegateReference") },
			OutError,
			NodeClass != TEXT("K2Node_AssignDelegate"));
	}
	// ── Chooser Nodes ────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_EvaluateChooser"))
	{
		FString MissingChooserProperties;
		if (!HasEvaluateChooserMetadata_ImportBpy(NodeJson, MissingChooserProperties))
		{
			FString NodeUid;
			NodeJson->TryGetStringField(TEXT("uid"), NodeUid);
			OutError = FString::Printf(
				TEXT("Chooser node '%s' (uid=%s) is missing required exported metadata [%s]. Re-export the source Blueprint with the latest ExportBpy and retry import."),
				*NodeClass,
				NodeUid.IsEmpty() ? TEXT("<none>") : *NodeUid,
				MissingChooserProperties.IsEmpty() ? TEXT("Chooser, Mode") : *MissingChooserProperties);
			return nullptr;
		}

		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("Chooser"), TEXT("Mode") },
			OutError);
	}
	else if (NodeClass == TEXT("K2Node_EvaluateChooser2"))
	{
		FString MissingChooserProperties;
		if (!HasEvaluateChooserMetadata_ImportBpy(NodeJson, MissingChooserProperties))
		{
			FString NodeUid;
			NodeJson->TryGetStringField(TEXT("uid"), NodeUid);
			OutError = FString::Printf(
				TEXT("Chooser node '%s' (uid=%s) is missing required exported metadata [%s]. Re-export the source Blueprint with the latest ExportBpy and retry import."),
				*NodeClass,
				NodeUid.IsEmpty() ? TEXT("<none>") : *NodeUid,
				MissingChooserProperties.IsEmpty() ? TEXT("Chooser, Mode") : *MissingChooserProperties);
			return nullptr;
		}

		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("Chooser"), TEXT("Mode"), TEXT("bReturnSoftObjectReference") },
			OutError);
	}
	// ── Mover Async Nodes ────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_PlayMontage"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	else if (NodeClass == TEXT("K2Node_PlayMontageOnMoverActor"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
	}
	// ── Function Entry ───────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_FunctionEntry"))
	{
		// Find existing entry node in graph
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N->IsA<UK2Node_FunctionEntry>())
			{
				Result = N;
				break;
			}
		}
		if (!Result)
		{
			UK2Node_FunctionEntry* Entry = NewObject<UK2Node_FunctionEntry>(Graph);
			Entry->CustomGeneratedFunctionName = Graph ? Graph->GetFName() : NAME_None;
			Entry->CreateNewGuid();
			Entry->PostPlacedNewNode();
			Entry->AllocateDefaultPins();
			Graph->AddNode(Entry, false, false);
			Result = Entry;
		}
	}
	// ── Function Result ──────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_FunctionResult"))
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N->IsA<UK2Node_FunctionResult>())
			{
				Result = N;
				break;
			}
		}
		if (!Result)
		{
			UK2Node_FunctionResult* Ret = NewObject<UK2Node_FunctionResult>(Graph);
			Ret->CreateNewGuid();
			Ret->PostPlacedNewNode();
			Ret->AllocateDefaultPins();
			Graph->AddNode(Ret, false, false);
			Result = Ret;
		}
	}
	else if (!FunctionRef.IsEmpty())
	{
		if (UClass* NodeUClass = ResolveNodeClass_ImportBpy(NodeClass))
		{
			if (NodeUClass->IsChildOf(UK2Node_Message::StaticClass()))
			{
				Result = CreateMessageNode(Graph, FunctionRef, NodeJson, OutError);
			}
			else if (NodeUClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
			{
				Result = CreateCallFunctionNode(Graph, FunctionRef, NodeClass, NodeJson, OutError);
			}
		}
	}
	// ── Fallback ─────────────────────────────────────────────────────
	else
	{
		OutError = FString::Printf(TEXT("Unsupported node class '%s'"), *NodeClass);
		return nullptr;
	}

NodeCreationDone:
	if (!Result) return nullptr;

	if (UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
	{
		FString SerializedUid;
		NodeJson->TryGetStringField(TEXT("uid"), SerializedUid);
		RegisterImportedNodeUid_ImportBpy(OwningBlueprint, SerializedUid, Result);
	}

	if (!ApplyNodeJsonToNode_ImportBpy(Result, NodeJson, OutError, true))
	{
		return nullptr;
	}

	return Result;
}

// ─── Specific node creators ───────────────────────────────────────────────────

UEdGraphNode* UBPDirectImporter::CreateEventNode(UEdGraph* Graph, const FString& EventName)
{
	const FString RequestedAggressive = NormalizeAggressiveFunctionName_ImportBpy(EventName);
	const bool bIsAnimNotifyEvent = IsAnimNotifyEventName_ImportBpy(EventName);

	// Search for existing event node with this name (e.g. ReceiveBeginPlay)
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (UK2Node_Event* Evt = Cast<UK2Node_Event>(N))
		{
			const FString ExistingName = Evt->EventReference.GetMemberName().ToString();
			const bool bExactMatch = ExistingName == EventName;
			const bool bAggressiveMatch =
				!RequestedAggressive.IsEmpty() &&
				(NormalizeAggressiveFunctionName_ImportBpy(ExistingName) == RequestedAggressive ||
				 NormalizeAggressiveFunctionName_ImportBpy(FName::NameToDisplayString(ExistingName, false)) == RequestedAggressive);
			if (bExactMatch || bAggressiveMatch)
			{
				if (bIsAnimNotifyEvent)
				{
					ConfigureAnimNotifyEventNodeReference_ImportBpy(Evt, EventName);
				}
				return Evt;
			}
		}
	}

	// Create standard event node via schema
	UK2Node_Event* NewEvt = NewObject<UK2Node_Event>(Graph);
	UFunction* EventFunc = ResolveFunctionOnBlueprintContext_ImportBpy(Graph, EventName);
	if (!EventFunc)
	{
		EventFunc = ResolveFunctionOnBlueprintContextByAggressiveName_ImportBpy(Graph, EventName);
	}
	if (!EventFunc)
	{
		EventFunc = ResolveSelfContextFunction_ImportBpy(Graph, EventName);
	}

	UClass* PreferredOwnerClass = nullptr;
	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
	{
		PreferredOwnerClass = Blueprint->SkeletonGeneratedClass
			? static_cast<UClass*>(Blueprint->SkeletonGeneratedClass)
			: (Blueprint->GeneratedClass
				? static_cast<UClass*>(Blueprint->GeneratedClass)
				: (Blueprint->ParentClass ? static_cast<UClass*>(Blueprint->ParentClass) : nullptr));
	}

	if (EventFunc)
	{
		NewEvt->EventReference.SetFromField<UFunction>(EventFunc, false);
		NewEvt->bOverrideFunction = true;
	}
	else if (PreferredOwnerClass)
	{
		// Keep override semantics even when the function is not resolvable yet.
		// ABP imports may create event nodes before all signatures are materialized.
		NewEvt->EventReference.SetExternalMember(FName(*EventName), PreferredOwnerClass);
		NewEvt->bOverrideFunction = true;
	}
	else
	{
		NewEvt->EventReference.SetExternalMember(FName(*EventName), UObject::StaticClass());
		NewEvt->bOverrideFunction = true;
	}

	if (!bIsAnimNotifyEvent)
	{
		NewEvt->bInternalEvent = false;
		NewEvt->CustomFunctionName = NAME_None;
	}

	if (bIsAnimNotifyEvent)
	{
		ConfigureAnimNotifyEventNodeReference_ImportBpy(NewEvt, EventName);
	}

	NewEvt->CreateNewGuid();
	NewEvt->PostPlacedNewNode();
	NewEvt->AllocateDefaultPins();
	Graph->AddNode(NewEvt, false, false);
	return NewEvt;
}

UEdGraphNode* UBPDirectImporter::CreateCallFunctionNode(
	UEdGraph* Graph,
	const FString& FunctionRef,
	const FString& NodeClassName,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError)
{
	const bool bJsonRequestsExecPins = NodeJsonRequestsExecPinsForCallNode_ImportBpy(NodeJson);

	// Parse "ClassName::FunctionName"
	FString ClassName, FuncName;
	if (!FunctionRef.Split(TEXT("::"), &ClassName, &FuncName))
	{
		FuncName = FunctionRef;
		ClassName = TEXT("");
	}

	UFunction* Func = nullptr;
	UClass* ExplicitOwnerClass = nullptr;
	const FString OwnerClassPath = GetNodePropString_ImportBpy(NodeJson, TEXT("FunctionOwnerClass"));
	if (!OwnerClassPath.IsEmpty())
	{
		ExplicitOwnerClass = ResolveNamedObject_ImportBpy<UClass>(OwnerClassPath);
		if (ExplicitOwnerClass)
		{
			Func = ExplicitOwnerClass->FindFunctionByName(FName(*FuncName));
		}
	}
	if (!Func && !ClassName.IsEmpty())
	{
		UClass* FuncClass = ResolveNamedObject_ImportBpy<UClass>(ClassName);
		ExplicitOwnerClass = FuncClass;
		if (FuncClass)
		{
			Func = FuncClass->FindFunctionByName(FName(*FuncName));
		}
	}
	if (!Func && OwnerClassPath.IsEmpty() && ClassName.IsEmpty())
	{
		Func = ResolveSelfContextFunction_ImportBpy(Graph, FuncName);
	}

	const bool bSelfContextCall = OwnerClassPath.IsEmpty() && ClassName.IsEmpty();
	if (!Func && (!bSelfContextCall || IsQualifiedFunctionReference_ImportBpy(FunctionRef)))
	{
		Func = ResolveNamedObject_ImportBpy<UFunction>(FunctionRef);
	}

	if (!Func)
	{
		if (!OwnerClassPath.IsEmpty() || !ClassName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Cannot resolve function '%s'"), *FunctionRef);
			return nullptr;
		}

		UE_LOG(LogTemp, Warning, TEXT("BPDirectImporter: cannot find function '%s'"), *FunctionRef);
	}

	UClass* DesiredNodeClass = ResolveNodeClass_ImportBpy(NodeClassName);
	if (!DesiredNodeClass || !DesiredNodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
	{
		DesiredNodeClass = UK2Node_CallFunction::StaticClass();
	}

	UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph, DesiredNodeClass);
	Node->SetFlags(RF_Transactional);
	Graph->AddNode(Node, false, false);

	const bool bIsPromotableOperatorNode =
		NodeClassName == TEXT("K2Node_PromotableOperator") ||
		NodeClassName == TEXT("K2Node_CommutativeAssociativeBinaryOperator");
	const bool bShouldBindCallNodeFromFunction =
		Func != nullptr;
	if (bShouldBindCallNodeFromFunction)
	{
		Node->SetFromFunction(Func);
	}

	if (bSelfContextCall)
	{
		// Keep self-context members explicitly marked as self. This is also required
		// for promotable nodes when SetFromFunction is intentionally skipped.
		Node->FunctionReference.SetSelfMember(FName(*FuncName));
	}
	else
	{
		// Prefer the external member reference over binding a resolved UFunction
		// directly. This matches how Blueprint call nodes preserve target class
		// context and avoids resolving the wrong runtime function variant for nodes
		// that must keep exec pins.
		Node->FunctionReference.SetExternalMember(
			FName(*FuncName),
			ExplicitOwnerClass ? ExplicitOwnerClass : (Func ? Func->GetOwnerClass() : UObject::StaticClass()));
	}

	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	EnsureCallFunctionPinsFromMatchingCustomEvent_ImportBpy(Node);
	if (bSelfContextCall && !Func)
	{
		RebindUnresolvedSelfContextCallNode_ImportBpy(Node);
	}
	if (bSelfContextCall && !Node->GetTargetFunction())
	{
		EnsureCallFunctionExecPins_ImportBpy(Node);
	}
	else if (bJsonRequestsExecPins)
	{
		EnsureCallFunctionExecPins_ImportBpy(Node);
	}
	LogTrackedPromotableNodeState_ImportBpy(TEXT("create_call_node"), Node, NodeJson);
	return Node;
}

UEdGraphNode* UBPDirectImporter::CreateMessageNode(
	UEdGraph* Graph,
	const FString& FunctionRef,
	const TSharedPtr<FJsonObject>& NodeJson,
	FString& OutError)
{
	FString ClassName;
	FString FuncName;
	if (!FunctionRef.Split(TEXT("::"), &ClassName, &FuncName))
	{
		OutError = FString::Printf(TEXT("Invalid message function ref: %s"), *FunctionRef);
		return nullptr;
	}

	const FString InterfaceClassPath = GetNodePropString_ImportBpy(NodeJson, TEXT("InterfaceClass"));
	UClass* InterfaceClass = InterfaceClassPath.IsEmpty()
		? ResolveNamedObject_ImportBpy<UClass>(ClassName)
		: ResolveNamedObject_ImportBpy<UClass>(InterfaceClassPath);
	UFunction* Func = InterfaceClass ? InterfaceClass->FindFunctionByName(FName(*FuncName)) : nullptr;
	if (!Func)
	{
		OutError = FString::Printf(TEXT("Cannot find interface function '%s'"), *FunctionRef);
		return nullptr;
	}

	UK2Node_Message* Node = NewObject<UK2Node_Message>(Graph);
	Node->FunctionReference.SetFromField<UFunction>(Func, false);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Graph->AddNode(Node, false, false);
	return Node;
}

UEdGraphNode* UBPDirectImporter::CreateMacroInstanceNode(
	UEdGraph* Graph,
	const FString& MacroGraphPath,
	const FString& MacroName,
	FString& OutError)
{
	UEdGraph* MacroGraph = ResolveMacroGraph_ImportBpy(Graph, MacroGraphPath, MacroName);
	if (!MacroGraph)
	{
		OutError = FString::Printf(TEXT("Cannot find macro graph '%s' (%s)"), *MacroName, *MacroGraphPath);
		return nullptr;
	}

	UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
	Node->SetMacroGraph(MacroGraph);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Graph->AddNode(Node, false, false);
	return Node;
}

UEdGraphNode* UBPDirectImporter::CreateVariableNode(
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	bool bIsGet,
	FString& OutError)
{
	if (!Graph || !NodeJson.IsValid())
	{
		return nullptr;
	}

	const FString VarName = NodeJson->GetStringField(TEXT("member_name"));
	const FString VariableScope = GetNodePropString_ImportBpy(NodeJson, TEXT("VariableScope"));
	const FString VariableScopeName = GetNodePropString_ImportBpy(NodeJson, TEXT("VariableScopeName"));
	const FString VariableOwnerClass = GetNodePropString_ImportBpy(NodeJson, TEXT("VariableOwnerClass"));
	const FString VariableGuidText = GetNodePropString_ImportBpy(NodeJson, TEXT("VariableGuid"));

	FGuid VariableGuid;
	const bool bHasVariableGuid = TryParseGuid_ImportBpy(VariableGuidText, VariableGuid);

	auto ConfigureVariableReference = [&](UK2Node_Variable* Node) -> bool
	{
		if (!Node)
		{
			return false;
		}

		if (VariableScope.Equals(TEXT("Local"), ESearchCase::IgnoreCase))
		{
			const FString ScopeName = !VariableScopeName.IsEmpty()
				? VariableScopeName
				: FBlueprintEditorUtils::GetTopLevelGraph(Graph)->GetName();
			Node->VariableReference.SetLocalMember(FName(*VarName), ScopeName, bHasVariableGuid ? VariableGuid : FGuid());
			return true;
		}

		if (VariableScope.Equals(TEXT("External"), ESearchCase::IgnoreCase))
		{
			if (UClass* OwnerClass = ResolveNamedObject_ImportBpy<UClass>(VariableOwnerClass))
			{
				if (bHasVariableGuid)
				{
					Node->VariableReference.SetExternalMember(FName(*VarName), OwnerClass, VariableGuid);
				}
				else
				{
					Node->VariableReference.SetExternalMember(FName(*VarName), OwnerClass);
				}
				return true;
			}

			OutError = FString::Printf(
				TEXT("Cannot resolve external variable owner '%s' for variable '%s'"),
				*VariableOwnerClass,
				*VarName);
			return false;
		}

		if (bHasVariableGuid)
		{
			Node->VariableReference.SetSelfMember(FName(*VarName), VariableGuid);
		}
		else
		{
			Node->VariableReference.SetSelfMember(FName(*VarName));
		}

		return true;
	};

	if (bIsGet)
	{
		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
		if (!ConfigureVariableReference(Node))
		{
			return nullptr;
		}
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Graph->AddNode(Node, false, false);

		if (ShouldRestoreImpureVariableGet_ImportBpy(NodeJson))
		{
			RestoreVariableGetPurity_ImportBpy(Node, false);
		}

		return Node;
	}
	else
	{
		UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(Graph);
		if (!ConfigureVariableReference(Node))
		{
			return nullptr;
		}
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Graph->AddNode(Node, false, false);
		return Node;
	}
}

UEdGraphNode* UBPDirectImporter::CreateBranchNode(UEdGraph* Graph)
{
	UK2Node_IfThenElse* Node = NewObject<UK2Node_IfThenElse>(Graph);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Graph->AddNode(Node, false, false);
	return Node;
}

// ─── ConnectPins ─────────────────────────────────────────────────────────────

bool UBPDirectImporter::ConnectPins(
	UEdGraphNode* SrcNode,
	const FString& SrcPinName,
	const FString& SrcPinFullName,
	const FString& SrcPinId,
	UEdGraphNode* DstNode,
	const FString& DstPinName,
	const FString& DstPinFullName,
	const FString& DstPinId,
	FString& OutError)
{
	UEdGraphPin* SrcPin = FindPinById_ImportBpy(SrcNode, SrcPinId);
	UEdGraphPin* DstPin = FindPinById_ImportBpy(DstNode, DstPinId);
	const FString RequestedSrcPin = !SrcPinFullName.IsEmpty() ? SrcPinFullName : SrcPinName;
	const FString RequestedDstPin = !DstPinFullName.IsEmpty() ? DstPinFullName : DstPinName;
	if (SrcPin && SrcPin->Direction != EGPD_Output)
	{
		// Some nodes (notably K2Node_EvaluateChooser2) expose multiple pins with the
		// same serialized name. When pin-id restoration resolves to the wrong side of
		// the node, fall back to directional name lookup instead of keeping a bad pin.
		SrcPin = nullptr;
	}
	if (SrcPin)
	{
		const FString LiveName = SrcPin->PinName.ToString();
		const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(SrcNode, RequestedSrcPin);
		const FString LiveNameNoGuid = StripGuidSuffix_ImportBpy(LiveName);
		const FString RequestedNoGuid = StripGuidSuffix_ImportBpy(RequestedSrcPin);
		const FString NormalizedNoGuid = StripGuidSuffix_ImportBpy(NormalizedRequested);
		if (!LiveName.Equals(RequestedSrcPin, ESearchCase::IgnoreCase) &&
			!LiveName.Equals(NormalizedRequested, ESearchCase::IgnoreCase) &&
			!LiveNameNoGuid.Equals(RequestedNoGuid, ESearchCase::IgnoreCase) &&
			!LiveNameNoGuid.Equals(NormalizedNoGuid, ESearchCase::IgnoreCase))
		{
			// PinId can drift across reconstruct/compile. Never trust id-only matches
			// when serialized pin names disagree, otherwise exec topology can be rewired.
			SrcPin = nullptr;
		}
	}
	if (DstPin && DstPin->Direction != EGPD_Input)
	{
		DstPin = nullptr;
	}
	if (UEdGraphPin* RetargetedChooserDstPin = FindEvaluateChooserContextPinForCurrentImport_ImportBpy(DstNode, RequestedDstPin, EGPD_Input))
	{
		DstPin = RetargetedChooserDstPin;
	}
	if (DstPin)
	{
		const FString LiveName = DstPin->PinName.ToString();
		const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(DstNode, RequestedDstPin);
		const FString LiveNameNoGuid = StripGuidSuffix_ImportBpy(LiveName);
		const FString RequestedNoGuid = StripGuidSuffix_ImportBpy(RequestedDstPin);
		const FString NormalizedNoGuid = StripGuidSuffix_ImportBpy(NormalizedRequested);
		if (!LiveName.Equals(RequestedDstPin, ESearchCase::IgnoreCase) &&
			!LiveName.Equals(NormalizedRequested, ESearchCase::IgnoreCase) &&
			!LiveNameNoGuid.Equals(RequestedNoGuid, ESearchCase::IgnoreCase) &&
			!LiveNameNoGuid.Equals(NormalizedNoGuid, ESearchCase::IgnoreCase))
		{
			DstPin = nullptr;
		}
	}
	if (!SrcPin)
	{
		SrcPin = SrcNode->FindPin(FName(*SrcPinFullName), EGPD_Output);
	}
	if (!SrcPin)
	{
		SrcPin = SrcNode->FindPin(FName(*SrcPinName), EGPD_Output);
	}
	if (!SrcPin)
	{
		SrcPin = FindPinFlexible_ImportBpy(SrcNode, !SrcPinFullName.IsEmpty() ? SrcPinFullName : SrcPinName, EGPD_Output);
	}

	if (!DstPin)
	{
		DstPin = FindPinFlexible_ImportBpy(DstNode, !DstPinFullName.IsEmpty() ? DstPinFullName : DstPinName, EGPD_Input);
	}

	const auto IsPromotableOperatorCallNode = [](const UK2Node_CallFunction* CallNode) -> bool
	{
		if (!CallNode)
		{
			return false;
		}

		const FString NodeClassName = CallNode->GetClass()->GetName();
		return NodeClassName == TEXT("K2Node_PromotableOperator") ||
			NodeClassName == TEXT("K2Node_CommutativeAssociativeBinaryOperator");
	};
	if (!SrcPin)
	{
		if (UK2Node_CallFunction* SrcCallNode = Cast<UK2Node_CallFunction>(SrcNode))
		{
			EnsureCallFunctionPinsFromMatchingCustomEvent_ImportBpy(SrcCallNode);
			const FString NormalizedSrcPin = NormalizeRequestedPinName_ImportBpy(SrcNode, RequestedSrcPin);
			if (NormalizedSrcPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
				NormalizedSrcPin.Equals(TEXT("then"), ESearchCase::IgnoreCase))
			{
				EnsureCallFunctionExecPins_ImportBpy(SrcCallNode);
			}
			SrcPin = FindPinFlexible_ImportBpy(SrcNode, RequestedSrcPin, EGPD_Output);
		}
	}
	if (!DstPin)
	{
		if (UK2Node_CallFunction* DstCallNode = Cast<UK2Node_CallFunction>(DstNode))
		{
			EnsureCallFunctionPinsFromMatchingCustomEvent_ImportBpy(DstCallNode);
			const FString NormalizedDstPin = NormalizeRequestedPinName_ImportBpy(DstNode, RequestedDstPin);
			if (NormalizedDstPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
				NormalizedDstPin.Equals(TEXT("then"), ESearchCase::IgnoreCase))
			{
				EnsureCallFunctionExecPins_ImportBpy(DstCallNode);
			}
			else if (
				SrcPin &&
				!NormalizedDstPin.IsEmpty() &&
				!DstCallNode->FindPin(FName(*NormalizedDstPin), EGPD_Input) &&
				!IsPromotableOperatorCallNode(DstCallNode) &&
				DstCallNode->GetTargetFunction() == nullptr)
			{
				DstCallNode->CreatePin(EGPD_Input, SrcPin->PinType, FName(*NormalizedDstPin));
			}
			DstPin = FindPinFlexible_ImportBpy(DstNode, RequestedDstPin, EGPD_Input);
		}
	}

	if (!SrcPin || !DstPin)
	{
		auto DescribePins = [](UEdGraphNode* Node, EEdGraphPinDirection Direction) -> FString
		{
			if (!Node)
			{
				return TEXT("<null-node>");
			}

			TArray<FString> PinNames;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != Direction)
				{
					continue;
				}
				PinNames.Add(Pin->PinName.ToString());
			}
			return PinNames.Num() > 0 ? FString::Join(PinNames, TEXT(", ")) : TEXT("<none>");
		};

		OutError = FString::Printf(
			TEXT("Cannot resolve connection pins: %s.%s -> %s.%s | src_pin=%s src_pin_full=%s src_pin_id=%s | dst_pin=%s dst_pin_full=%s dst_pin_id=%s | src_available=[%s] dst_available=[%s]"),
			*DescribeNode_ImportBpy(SrcNode),
			*(!SrcPinFullName.IsEmpty() ? SrcPinFullName : SrcPinName),
			*DescribeNode_ImportBpy(DstNode),
			*(!DstPinFullName.IsEmpty() ? DstPinFullName : DstPinName),
			*SrcPinName,
			*SrcPinFullName,
			*SrcPinId,
			*DstPinName,
			*DstPinFullName,
			*DstPinId,
			*DescribePins(SrcNode, EGPD_Output),
			*DescribePins(DstNode, EGPD_Input));
		return false;
	}

	auto EnsureReciprocalLink = [&](const TCHAR* FailurePrefix) -> bool
	{
		const bool bSrcHasDst = SrcPin->LinkedTo.Contains(DstPin);
		const bool bDstHasSrc = DstPin->LinkedTo.Contains(SrcPin);
		if (bSrcHasDst && bDstHasSrc)
		{
			return true;
		}

		SrcPin->Modify();
		DstPin->Modify();
		if (!bSrcHasDst)
		{
			SrcPin->LinkedTo.AddUnique(DstPin);
		}
		if (!bDstHasSrc)
		{
			DstPin->LinkedTo.AddUnique(SrcPin);
		}

		SrcNode->PinConnectionListChanged(SrcPin);
		DstNode->PinConnectionListChanged(DstPin);
		SrcNode->NodeConnectionListChanged();
		DstNode->NodeConnectionListChanged();
		if (UEdGraph* Graph = SrcNode->GetGraph())
		{
			Graph->NotifyGraphChanged();
		}

		if (SrcPin->LinkedTo.Contains(DstPin) && DstPin->LinkedTo.Contains(SrcPin))
		{
			return true;
		}

		OutError = FString::Printf(
			TEXT("%s: %s.%s -> %s.%s | reciprocal=%d/%d"),
			FailurePrefix,
			*DescribeNode_ImportBpy(SrcNode),
			*SrcPin->GetName(),
			*DescribeNode_ImportBpy(DstNode),
			*DstPin->GetName(),
			SrcPin->LinkedTo.Contains(DstPin) ? 1 : 0,
			DstPin->LinkedTo.Contains(SrcPin) ? 1 : 0);
		return false;
	};

	const bool bIsStateMachineGraph =
		SrcNode &&
		DstNode &&
		SrcNode->GetGraph() &&
		SrcNode->GetGraph() == DstNode->GetGraph() &&
		SrcNode->GetGraph()->IsA<UAnimationStateMachineGraph>();
	if (bIsStateMachineGraph)
	{
		SrcPin->Modify();
		DstPin->Modify();
		SrcPin->MakeLinkTo(DstPin);
		SrcNode->PinConnectionListChanged(SrcPin);
		DstNode->PinConnectionListChanged(DstPin);
		SrcNode->NodeConnectionListChanged();
		DstNode->NodeConnectionListChanged();

		if (UEdGraph* Graph = SrcNode->GetGraph())
		{
			Graph->NotifyGraphChanged();
		}

		return EnsureReciprocalLink(TEXT("Failed to create state machine link"));
	}

	const UEdGraphSchema* Schema = SrcPin->GetSchema();
	if (Schema && Schema->TryCreateConnection(SrcPin, DstPin))
	{
		return EnsureReciprocalLink(TEXT("Schema created non-reciprocal link"));
	}

	const bool bBothExecPins =
		SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
		DstPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	if (!bBothExecPins)
	{
		OutError = FString::Printf(
			TEXT("Schema rejected typed connection: %s.%s (%s) -> %s.%s (%s)"),
			*DescribeNode_ImportBpy(SrcNode),
			*SrcPin->GetName(),
			*DescribePinType_ImportBpy(SrcPin->PinType),
			*DescribeNode_ImportBpy(DstNode),
			*DstPin->GetName(),
			*DescribePinType_ImportBpy(DstPin->PinType));
		return false;
	}

	SrcPin->MakeLinkTo(DstPin);
	SrcNode->PinConnectionListChanged(SrcPin);
	DstNode->PinConnectionListChanged(DstPin);
	SrcNode->NodeConnectionListChanged();
	DstNode->NodeConnectionListChanged();

	if (UEdGraph* Graph = SrcNode->GetGraph())
	{
		Graph->NotifyGraphChanged();
	}

	return EnsureReciprocalLink(TEXT("Schema rejected exec connection"));
}

// ─── ParsePinType ─────────────────────────────────────────────────────────────

void UBPDirectImporter::ParsePinType(const FString& TypeStr, FEdGraphPinType& OutType)
{
	ParsePinTypeString_ImportBpy(TypeStr, OutType);
}

// ─── CompileBlueprint ────────────────────────────────────────────────────────

bool UBPDirectImporter::CompileBlueprint(
	UBlueprint* BP,
	TArray<FString>* OutWarnings,
	FString* OutError)
{
	if (!BP)
	{
		if (OutError)
		{
			*OutError = TEXT("Cannot compile a null Blueprint.");
		}
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FCompilerResultsLog ResultsLog;
	ResultsLog.bAnnotateMentionedNodes = false;
	FKismetEditorUtilities::CompileBlueprint(
		BP,
		EBlueprintCompileOptions::None,
		&ResultsLog);

	if (OutWarnings)
	{
		OutWarnings->Reset();
		for (const TSharedRef<FTokenizedMessage>& Message : ResultsLog.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Warning ||
				Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
			{
				OutWarnings->Add(Message->ToText().ToString());
			}
		}
	}

	const bool bHasErrors = ResultsLog.NumErrors > 0 || BP->Status == BS_Error;
	if (bHasErrors)
	{
		if (OutError)
		{
			TArray<FString> ErrorMessages;
			for (const TSharedRef<FTokenizedMessage>& Message : ResultsLog.Messages)
			{
				if (Message->GetSeverity() == EMessageSeverity::Error)
				{
					ErrorMessages.Add(Message->ToText().ToString());
				}
			}

			*OutError = ErrorMessages.Num() > 0
				? FString::Join(ErrorMessages, TEXT(" | "))
				: TEXT("Blueprint compile failed with BS_Error");
		}
		return false;
	}

	BP->MarkPackageDirty();
	return true;
}

bool UBPDirectImporter::SaveBlueprint(UBlueprint* BP, FString& OutError)
{
	if (!BP)
	{
		OutError = TEXT("Cannot save a null Blueprint.");
		return false;
	}

	UPackage* Package = BP->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Blueprint has no outer package to save.");
		return false;
	}

	const FString PackageName = Package->GetName();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;

	// Windows overwrite preflight can report a false external lock when the
	// current editor process already has the package loaded. SavePackage is the
	// authoritative check, so rely on its result instead.
	if (!UPackage::SavePackage(Package, BP, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save Blueprint package: %s"), *PackageFileName);
		return false;
	}

	return true;
}

// ─── ImportStandaloneAssetFromJson ────────────────────────────────────────────

bool UBPDirectImporter::ImportStandaloneAssetFromJson(
	const FString& AssetPath,
	const FString& PropertiesJson,
	FString& OutError)
{
	auto FailStandaloneStep = [&OutError](const TCHAR* StepName) -> bool
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Standalone import failed at step: %s"), StepName ? StepName : TEXT("Unknown"));
		}
		return false;
	};

	TSharedPtr<FJsonObject> PropsObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObj) || !PropsObj.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to parse PropertiesJson for asset: %s"), *AssetPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* StandalonePropertiesObj = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* StandaloneSubobjects = nullptr;
	const bool bHasStandaloneProperties = PropsObj->TryGetObjectField(TEXT("properties"), StandalonePropertiesObj);
	const bool bHasStandaloneSubobjects = PropsObj->TryGetArrayField(TEXT("subobjects"), StandaloneSubobjects);
	const bool bIsStandaloneMeta =
		bHasStandaloneProperties ||
		bHasStandaloneSubobjects ||
		PropsObj->HasField(TEXT("asset_class"));

	FString EffectiveAssetPath = NormalizeStandaloneAssetObjectPath_ImportBpy(AssetPath);
	if (EffectiveAssetPath.IsEmpty())
	{
		PropsObj->TryGetStringField(TEXT("asset"), EffectiveAssetPath);
		EffectiveAssetPath = NormalizeStandaloneAssetObjectPath_ImportBpy(EffectiveAssetPath);
	}

	if (EffectiveAssetPath.IsEmpty())
	{
		OutError = TEXT("Standalone asset import is missing a target asset path");
		return false;
	}

	if (!ValidateStandaloneChooserJsonPreflight_ImportBpy(EffectiveAssetPath, PropsObj, OutError))
	{
		return false;
	}

	UObject* Asset = nullptr;
	if (bIsStandaloneMeta)
	{
		FString AssetClassPath;
		PropsObj->TryGetStringField(TEXT("asset_class"), AssetClassPath);
		if (AssetClassPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Standalone asset meta is missing asset_class: %s"), *EffectiveAssetPath);
			return false;
		}

		if (!CreateOrReplaceStandaloneAsset_ImportBpy(
			EffectiveAssetPath,
			AssetClassPath,
			true,
			Asset,
			OutError))
		{
			return FailStandaloneStep(TEXT("CreateOrReplaceStandaloneAsset"));
		}

		Asset->Modify();
		if (!RecreateStandaloneAssetSubobjects_ImportBpy(Asset, StandaloneSubobjects, OutError))
		{
			return FailStandaloneStep(TEXT("RecreateStandaloneAssetSubobjects"));
		}

		if (!RestoreUserDefinedEnumEntries_ImportBpy(Asset, PropsObj, OutError))
		{
			return FailStandaloneStep(TEXT("RestoreUserDefinedEnumEntries"));
		}

		if (StandalonePropertiesObj && StandalonePropertiesObj->IsValid())
		{
			ApplyStandaloneAssetProperties_ImportBpy(Asset, *StandalonePropertiesObj);
			if (!RestoreInputMappingInstancedRefs_ImportBpy(Asset, StandalonePropertiesObj, OutError))
			{
				return FailStandaloneStep(TEXT("RestoreInputMappingInstancedRefs"));
			}
			if (!RestoreInputActionInstancedRefs_ImportBpy(Asset, StandalonePropertiesObj, OutError))
			{
				return FailStandaloneStep(TEXT("RestoreInputActionInstancedRefs"));
			}
		}

		if (!RestoreChooserTableData_ImportBpy(Asset, PropsObj, OutError))
		{
			return FailStandaloneStep(TEXT("RestoreChooserTableData"));
		}

		if (!CleanupUnexpectedStandaloneSubobjects_ImportBpy(Asset, StandaloneSubobjects, OutError))
		{
			return FailStandaloneStep(TEXT("CleanupUnexpectedStandaloneSubobjects"));
		}
	}
	else
	{
		Asset = LoadStandaloneAsset_ImportBpy(EffectiveAssetPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Cannot load asset: %s"), *EffectiveAssetPath);
			return false;
		}

		Asset->Modify();
		ApplyJsonObjectToObject_ImportBpy(Asset, PropsObj);
	}

	// Mark dirty and save
	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Asset has no outer package.");
		return false;
	}

	Asset->PostEditChange();
	Package->MarkPackageDirty();

	if (!Package->IsFullyLoaded())
	{
		Package->FullyLoad();
		if (!Package->IsFullyLoaded())
		{
			Package->MarkAsFullyLoaded();
		}
	}

	const FString PackageName     = Package->GetName();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags     = SAVE_None;

	// Windows overwrite preflight can report a false external lock when the
	// current editor process already has the package loaded. SavePackage is the
	// authoritative check, so rely on its result instead.
	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save asset package: %s"), *PackageFileName);
		return false;
	}

	return true;
}

