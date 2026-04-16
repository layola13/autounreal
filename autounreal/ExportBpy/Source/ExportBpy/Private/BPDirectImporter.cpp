// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "BPDirectImporter.h"

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
#include "KismetCompiler.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
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
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
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
	const TArray<TSharedPtr<FJsonValue>>* NodesArr,
	const TMap<FString, UEdGraphNode*>& NodeMap,
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

TMap<TWeakObjectPtr<UBlueprint>, TMap<FString, TWeakObjectPtr<UEdGraphNode>>> GImportedNodeUidRegistry_ImportBpy;

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
	if (VarJson->TryGetStringField(TEXT("category"), Category))
	{
		const FText CategoryText = FText::FromString(Category);
		if (!Variable.Category.EqualTo(CategoryText))
		{
			Variable.Category = CategoryText;
			bChanged = true;
		}
	}

	FString Tooltip;
	if (VarJson->TryGetStringField(TEXT("tooltip"), Tooltip))
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
	if (VarJson->TryGetBoolField(TEXT("replicated"), bReplicated))
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
	if (VarJson->TryGetStringField(TEXT("rep_notify"), RepNotify))
	{
		const FName RepNotifyName(*RepNotify);
		if (Variable.RepNotifyFunc != RepNotifyName)
		{
			Variable.RepNotifyFunc = RepNotifyName;
			bChanged = true;
		}
	}

	bool bInstanceEditable = false;
	if (VarJson->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable))
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

bool ObjectSerializedPropertyTextContains_ImportBpy(UObject* Object, const FString& Needle)
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

	if (SourceGeneratedClassPath.IsEmpty() ||
		TargetGeneratedClassPath.IsEmpty() ||
		SourceGeneratedClassPath.Equals(TargetGeneratedClassPath, ESearchCase::CaseSensitive))
	{
		return true;
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
				const bool bNeedsRetarget =
					ObjectSerializedPropertyTextContains_ImportBpy(ChooserAsset, SourceGeneratedClassPath);
				if (!bNeedsRetarget)
				{
					RetargetedChooserAsset = ChooserAsset;
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

					bool bChooserAssetChanged = false;
					if (!RemapObjectSerializedPropertyTextInPlace_ImportBpy(
							RetargetedChooserAsset,
							SourceGeneratedClassPath,
							TargetGeneratedClassPath,
							bChooserAssetChanged,
							OutError))
					{
						return false;
					}

					if (bChooserAssetChanged)
					{
						RetargetedChooserAsset->MarkPackageDirty();
						UEditorAssetLibrary::SaveAsset(RetargetedChooserAssetPath, false);
					}

					bCreatedOrUpdatedAnyChooserAsset = true;
				}

				SourceChooserAssetToRetargetedAsset.Add(SourceChooserAssetPath, RetargetedChooserAsset);
			}

			if (RetargetedChooserAsset && RetargetedChooserAsset != ChooserAsset)
			{
				Node->Modify();
				ChooserProperty->SetObjectPropertyValue_InContainer(Node, RetargetedChooserAsset);
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

void EnsureFunctionPins_ImportBpy(UK2Node_FunctionEntry* EntryNode, const TArray<TPair<FString, FEdGraphPinType>>& Inputs);
void EnsureFunctionPins_ImportBpy(UK2Node_FunctionResult* ResultNode, const TArray<TPair<FString, FEdGraphPinType>>& Outputs);
void ApplyFunctionGraphMetadata_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson, UK2Node_FunctionEntry* EntryNode);
void ParsePinTypeString_ImportBpy(const FString& TypeStr, FEdGraphPinType& OutType);
void ParseGraphPins_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson, const TCHAR* FieldName, TArray<TPair<FString, FEdGraphPinType>>& OutPins);
bool GraphJsonContainsAnimNodes_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson);

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

		// Anim layer / secondary animation graphs must exist before the primary
		// AnimGraph tries to materialize linked-layer input pins from them.
		if (bContainsAnimNodes && !bIsPrimaryAnimGraph)
		{
			return 1;
		}
		if (!bContainsAnimNodes)
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
	if (!InterfaceFunction)
	{
		return nullptr;
	}

	if (OutInterfaceFunction)
	{
		*OutInterfaceFunction = InterfaceFunction;
	}

	if (UClass* InterfaceClass = Cast<UClass>(InterfaceFunction->GetOuter()))
	{
		return InterfaceClass->GetAuthoritativeClass();
	}

	return nullptr;
}

bool EnsureInterfaceGraphBinding_ImportBpy(UBlueprint* BP, UEdGraph* Graph, UClass* InterfaceClass, UFunction* InterfaceFunction)
{
	if (!BP || !Graph || !InterfaceClass || !InterfaceFunction)
	{
		return false;
	}

	bool bModified = false;

	Graph->bAllowDeletion = false;
	const FGuid InterfaceGuid = FBlueprintEditorUtils::FindInterfaceFunctionGuid(InterfaceFunction, InterfaceClass);
	if (Graph->InterfaceGuid != InterfaceGuid)
	{
		Graph->InterfaceGuid = InterfaceGuid;
		bModified = true;
	}

	if (BP->FunctionGraphs.Contains(Graph))
	{
		BP->FunctionGraphs.Remove(Graph);
		bModified = true;
	}

	for (FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface != InterfaceClass)
		{
			continue;
		}

		if (InterfaceDesc.Graphs.Find(Graph) == INDEX_NONE)
		{
			InterfaceDesc.Graphs.Add(Graph);
			bModified = true;
		}
		break;
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
			EnsureInterfaceGraphBinding_ImportBpy(BP, OutGraph, InterfaceClass, InterfaceFunction);
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

bool IsScalarLiteralPinCategory_ImportBpy(const FName& PinCategory)
{
	return PinCategory == UEdGraphSchema_K2::PC_Boolean ||
		PinCategory == UEdGraphSchema_K2::PC_Byte ||
		PinCategory == UEdGraphSchema_K2::PC_Int ||
		PinCategory == UEdGraphSchema_K2::PC_Int64 ||
		PinCategory == UEdGraphSchema_K2::PC_Real;
}

void ApplyDefaultToPin_ImportBpy(UEdGraphPin* Pin, const TSharedPtr<FJsonValue>& Value)
{
	if (!Pin || !Value.IsValid())
	{
		return;
	}

	const FString DefaultValue = JsonValueToDefaultString_ImportBpy(Value);
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

	if (IsObjectLikePinCategory_ImportBpy(PinCategory) && !DefaultValue.IsEmpty())
	{
		UObject* DefaultObject = nullptr;
		if (PinCategory == UEdGraphSchema_K2::PC_Class || PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			DefaultObject = ResolveNamedObject_ImportBpy<UClass>(DefaultValue);
		}
		else
		{
			DefaultObject = ResolveNamedObject_ImportBpy<UObject>(DefaultValue);
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
		const FText TextValue = FText::FromString(DefaultValue);
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
		Schema->TrySetDefaultValue(*Pin, DefaultValue, false);
		if (!bIsWildcardPin &&
			!bIsPromotableOperatorPin &&
			!Pin->DefaultValue.Equals(DefaultValue, ESearchCase::CaseSensitive))
		{
			Pin->DefaultValue = DefaultValue;
		}
	}
	else
	{
		if (!bIsWildcardPin && !bIsPromotableOperatorPin)
		{
			Pin->DefaultValue = DefaultValue;
		}
	}

	// Promotable operator pins can lose explicit numeric defaults (e.g. +2, -0.5)
	// after type promotion/rebind/reconstruct passes. If schema assignment did not
	// stick, restore the serialized literal on safe input pins.
	const bool bCanForcePromotableDefault =
		bIsPromotableOperatorPin &&
		Pin->Direction == EGPD_Input &&
		IsScalarLiteralPinCategory_ImportBpy(Pin->PinType.PinCategory) &&
		Pin->LinkedTo.Num() == 0 &&
		IsSimpleLiteralDefaultForPromotable_ImportBpy(DefaultValue) &&
		!Pin->DefaultValue.Equals(DefaultValue, ESearchCase::CaseSensitive);
	if (bCanForcePromotableDefault)
	{
		Pin->DefaultValue = DefaultValue;
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

	// UE 5.7 chooser nodes can expose runtime pin names without the legacy "S_" prefix.
	// Normalize exported legacy names so we bind to durable pins and avoid stale links.
	if (IsEvaluateChooserNode_ImportBpy(Node) &&
		RequestedPinName.StartsWith(TEXT("S_"), ESearchCase::IgnoreCase) &&
		RequestedPinName.Len() > 2)
	{
		return RequestedPinName.RightChop(2);
	}

	return RequestedPinName;
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

UEdGraphPin* FindExistingPinFlexible_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(Node, RequestedPinName);
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
		Normalized == TEXT("2C7F4536451C956EFBFE379EC7FE82FF");
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

	if (UAnimGraphNode_LinkedAnimLayer* const LinkedLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
	{
		if (LinkedLayerNode->Node.Layer != NAME_None)
		{
			if (UAnimBlueprint* CurrentBlueprint = Cast<UAnimBlueprint>(LinkedLayerNode->GetBlueprint()))
			{
				LinkedLayerNode->Node.Interface = nullptr;
				LinkedLayerNode->InterfaceGuid.Invalidate();
				const FName LayerName = LinkedLayerNode->Node.Layer;

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

				if (LinkedLayerNode->Node.Interface.Get() == nullptr)
				{
					LinkedLayerNode->Node.InstanceClass = nullptr;
				}
			}
		}
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

	const FString NodeClassName = Node->GetClass()->GetName();
	if (!NodeClassName.StartsWith(TEXT("AnimGraphNode_BlendStack")))
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
			if ((IsBlendStackGraphLike_ImportBpy(Graph) || FString(PropertyName).Equals(TEXT("BoundGraph"), ESearchCase::IgnoreCase)) &&
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
			PropertyName.Equals(TEXT("BoundGraph"), ESearchCase::IgnoreCase) ||
			PropertyName.Contains(TEXT("BlendStack"), ESearchCase::IgnoreCase) ||
			PropertyName.Contains(TEXT("BoundGraph"), ESearchCase::IgnoreCase);
		if (!bPropertyLooksRelevant && !IsBlendStackGraphLike_ImportBpy(Graph))
		{
			continue;
		}

		int32 Score = Graph->Nodes.Num();
		if (PropertyName.Equals(TEXT("BoundGraph"), ESearchCase::IgnoreCase))
		{
			Score += 1000;
		}
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
		FString CachePoseName;
		if (!(*NodePropsObj)->TryGetStringField(TEXT("CachePoseName"), CachePoseName) || CachePoseName.IsEmpty())
		{
			(*NodePropsObj)->TryGetStringField(TEXT("CacheName"), CachePoseName);
		}

		if (!CachePoseName.IsEmpty())
		{
			SaveCachedPoseNode->CacheName = CachePoseName;
			SaveCachedPoseNode->Node.CachePoseName = FName(*CachePoseName);
		}
	}

	if (UAnimGraphNode_UseCachedPose* UseCachedPoseNode = Cast<UAnimGraphNode_UseCachedPose>(Node))
	{
		FString CachePoseName;
		if (!(*NodePropsObj)->TryGetStringField(TEXT("CachePoseName"), CachePoseName) || CachePoseName.IsEmpty())
		{
			(*NodePropsObj)->TryGetStringField(TEXT("CacheName"), CachePoseName);
		}

		if (!CachePoseName.IsEmpty())
		{
			UseCachedPoseNode->Node.CachePoseName = FName(*CachePoseName);
			SetUseCachedPoseNameOfCache_ImportBpy(UseCachedPoseNode, CachePoseName);
		}
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
		}

		if (!BlendStackGraphJsonTextPostReconstruct.IsEmpty())
		{
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
			ApplyJsonValueToProperty_ImportBpy(Node, Property, JsonValue);
			bNeedsReconstruct = true;
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
		}

		if (!ReplayNestedGraphsPostReconstruct())
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

bool AreSerializedDefaultValuesEquivalent_ImportBpy(const FString& ExpectedValue, const FString& ActualValue)
{
	FString Expected = ExpectedValue;
	FString Actual = ActualValue;
	Expected.TrimStartAndEndInline();
	Actual.TrimStartAndEndInline();

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
	if (bIsPromotableOperator &&
		!bAllowPromotableFunctionRebind &&
		!bHasDefaultContractHint &&
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
		ExistingTargetFunction != ResolvedFunction;
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
	if (!ApplyPinDefaults_ImportBpy(Node, NodeJson, OutError, true))
	{
		return false;
	}
	RemapSourceGeneratedClassPinsToCurrentBlueprint_ImportBpy(Node);
	if (!ApplyPinIds_ImportBpy(Node, NodeJson, OutError))
	{
		return false;
	}

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

bool RestoreStateMachineAliasNodesAfterCreation_ImportBpy(
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
		if (!NodeObj.IsValid() || NodeObj->GetStringField(TEXT("node_class")) != TEXT("AnimStateAliasNode"))
		{
			continue;
		}

		const FString Uid = NodeObj->GetStringField(TEXT("uid"));
		UEdGraphNode* const* ExistingNode = NodeMap.Find(Uid);
		if (!ExistingNode || !*ExistingNode)
		{
			continue;
		}

		UAnimStateAliasNode* const AliasNode = Cast<UAnimStateAliasNode>(*ExistingNode);
		if (!AliasNode)
		{
			continue;
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
			UEdGraphNode* const* TargetNode = NodeMap.Find(AliasedUid);
			UAnimStateNodeBase* const TargetState = TargetNode ? Cast<UAnimStateNodeBase>(*TargetNode) : nullptr;
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
	}

	return true;
}
}

// ─── Public entry points ──────────────────────────────────────────────────────

void ImportInterfaces_ImportBpy(UBlueprint* BP, const TArray<TSharedPtr<FJsonValue>>& InterfacesArr)
{
	if (!BP) return;

	for (const TSharedPtr<FJsonValue>& Val : InterfacesArr)
	{
		if (!Val.IsValid()) continue;
		FString InterfacePath = Val->AsString();
		if (InterfacePath.IsEmpty()) continue;

		UClass* InterfaceClass = ResolveNamedObject_ImportBpy<UClass>(InterfacePath);
		if (!InterfaceClass) continue;

		// Skip if already implemented
		bool bAlreadyImplemented = false;
		for (const FBPInterfaceDescription& Existing : BP->ImplementedInterfaces)
		{
			if (Existing.Interface == InterfaceClass)
			{
				bAlreadyImplemented = true;
				break;
			}
		}
		if (bAlreadyImplemented) continue;

		FBlueprintEditorUtils::ImplementNewInterface(BP, InterfaceClass->GetFName());
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
			TEXT("[ExportBpy] Replaying top-level graph connections after compile: graph=%s missing_before=%d"),
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
			if (!StateMachineNode)
			{
				continue;
			}

			if (!ApplyNodeJsonToNode_ImportBpy(StateMachineNode, NodeObj, OutError, false))
			{
				return false;
			}
		}
	}

	return true;
}

bool UBPDirectImporter::ImportBlueprintFromJson(
	const FString& JsonData,
	const FString& TargetAssetPath,
	bool bCompileBlueprint,
	FString& OutError)
{
	ResetAllImportedNodeRegistries_ImportBpy();

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

		for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
		{
			if (IsNodeOwnedNestedGraphJson_ImportBpy(GraphObj))
			{
				continue;
			}

			if (!CreateGraph(BP, GraphObj, OutError))
			{
				return false;
			}
		}

		if (!RebindUnresolvedSelfContextCallsAndReplaySerializedPins_ImportBpy(BP, SortedGraphs, OutError))
		{
			return false;
		}
	}

	// Keep chooser references identical to the exported source asset.
	// Retargeting to *_For_* chooser copies introduces runtime divergence for cloned ABPs.
	constexpr bool bEnableChooserRetargeting_ImportBpy = false;
	if (bEnableChooserRetargeting_ImportBpy)
	{
		bool bRetargetedChooserTables = false;
		if (!RetargetEvaluateChooserTablesForCurrentBlueprint_ImportBpy(BP, bRetargetedChooserTables, OutError))
		{
			return false;
		}
		if (bRetargetedChooserTables)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}

	if (bCompileBlueprint)
	{
		CompileBlueprint(BP);

		if (!ReplayAnimBlueprintStateMachineGraphsAfterCompile_ImportBpy(BP, SortedGraphs, OutError))
		{
			return false;
		}

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
			CompileBlueprint(BP);

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
			CompileBlueprint(BP);

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
	}

	return SaveBlueprint(BP, OutError);
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

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
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
			const bool bCanReconstructNow =
				!bOwnsNestedAnimGraph &&
				!bSkipPreApplyReconstructForAnimGetter &&
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

	if (!RestoreStateMachineAliasNodesAfterCreation_ImportBpy(NodesArr, NodeMap, OutError))
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
		NodeClass == TEXT("AnimStateNode") ||
		NodeClass == TEXT("AnimStateTransitionNode") ||
		NodeClass == TEXT("AnimStateAliasNode") ||
		NodeClass == TEXT("AnimStateConduitNode") ||
		NodeClass == TEXT("EdGraphNode_Comment"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(Graph, NodeClass, NodeJson, {}, OutError);
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
	// Search for existing event node with this name (e.g. ReceiveBeginPlay)
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (UK2Node_Event* Evt = Cast<UK2Node_Event>(N))
		{
			if (Evt->EventReference.GetMemberName().ToString() == EventName)
				return Evt;
		}
	}

	// Create standard event node via schema
	UK2Node_Event* NewEvt = NewObject<UK2Node_Event>(Graph);
	if (UFunction* EventFunc = ResolveFunctionOnBlueprintContext_ImportBpy(Graph, EventName))
	{
		NewEvt->EventReference.SetFromField<UFunction>(EventFunc, false);
	}
	else if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
	{
		UClass* EventOwnerClass = Blueprint->ParentClass ? static_cast<UClass*>(Blueprint->ParentClass) : UObject::StaticClass();
		NewEvt->EventReference.SetExternalMember(FName(*EventName), EventOwnerClass);
	}
	else
	{
		NewEvt->EventReference.SetExternalMember(FName(*EventName), UObject::StaticClass());
	}
	NewEvt->bOverrideFunction = true;
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
	const bool bHasSerializedPinTypeContract = HasSerializedPinTypeContract_ImportBpy(NodeJson);
	const bool bShouldBindCallNodeFromFunction =
		Func != nullptr &&
		(!bIsPromotableOperatorNode || bHasSerializedPinTypeContract);
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
	if (SrcPin && SrcPin->Direction != EGPD_Output)
	{
		// Some nodes (notably K2Node_EvaluateChooser2) expose multiple pins with the
		// same serialized name. When pin-id restoration resolves to the wrong side of
		// the node, fall back to directional name lookup instead of keeping a bad pin.
		SrcPin = nullptr;
	}
	if (DstPin && DstPin->Direction != EGPD_Input)
	{
		DstPin = nullptr;
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

	const FString RequestedSrcPin = !SrcPinFullName.IsEmpty() ? SrcPinFullName : SrcPinName;
	const FString RequestedDstPin = !DstPinFullName.IsEmpty() ? DstPinFullName : DstPinName;
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

void UBPDirectImporter::CompileBlueprint(UBlueprint* BP)
{
	FKismetEditorUtilities::CompileBlueprint(BP,
		EBlueprintCompileOptions::SkipGarbageCollection);
	BP->MarkPackageDirty();
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
