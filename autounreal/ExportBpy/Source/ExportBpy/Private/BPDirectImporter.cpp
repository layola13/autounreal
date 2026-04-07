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

template <typename TObject>
TObject* ResolveNamedObject_ImportBpy(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}

	if (TObject* Found = FindObject<TObject>(nullptr, *Name))
	{
		return Found;
	}
	if (TObject* Found = FindFirstObjectSafe<TObject>(*Name))
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

	if (TObject* Loaded = TryLoad(Name))
	{
		return Loaded;
	}

	FString PackagePath = Name;
	FString ObjectPath = Name;
	if (const int32 DotIndex = Name.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd); DotIndex != INDEX_NONE)
	{
		PackagePath = Name.Left(DotIndex);
	}
	else if (Name.StartsWith(TEXT("/")))
	{
		const FString AssetName = FPaths::GetBaseFilename(Name);
		ObjectPath = FString::Printf(TEXT("%s.%s"), *Name, *AssetName);
	}

	if (PackagePath != Name)
	{
		if (TObject* Loaded = TryLoad(PackagePath))
		{
			return Loaded;
		}
	}

	if (ObjectPath != Name)
	{
		if (TObject* Loaded = TryLoad(ObjectPath))
		{
			return Loaded;
		}
	}

	if (!Name.Contains(TEXT("/")) && !Name.Contains(TEXT(".")))
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

			const FName AssetName(*Name);
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

	AnimGetterNode->ReconstructNode();
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
				ResultAssetPaths.Add(AssetPath);
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

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
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
		if (!Existing)
		{
			UClass* OverrideFunctionClass =
				FBlueprintEditorUtils::GetOverrideFunctionClass(BP, FName(*OutGraphName));

			OutGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*OutGraphName),
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(
				BP,
				OutGraph,
				OverrideFunctionClass == nullptr,
				OverrideFunctionClass);
		}
		else
		{
			OutGraph = Existing;
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

void ApplyDefaultToPin_ImportBpy(UEdGraphPin* Pin, const TSharedPtr<FJsonValue>& Value)
{
	if (!Pin || !Value.IsValid())
	{
		return;
	}

	const FString DefaultValue = JsonValueToDefaultString_ImportBpy(Value);
	const UEdGraphSchema* Schema = Pin->GetSchema();
	const FName& PinCategory = Pin->PinType.PinCategory;

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
			Pin->AutogeneratedDefaultValue.Reset();

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
	}
	else
	{
		Pin->DefaultValue = DefaultValue;
	}
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

void EnsureDynamicPinsForRequest_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
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
		!CastField<FNameProperty>(Property))
	{
		const FString TextValue = Value->AsString();
		Property->ImportText_Direct(*TextValue, PropertyAddress, Object, PortFlags);
		return;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjectValue = ResolveNamedObject_ImportBpy<UObject>(Value->AsString());
		ObjectProperty->SetObjectPropertyValue(PropertyAddress, ObjectValue);
		return;
	}

	if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		UClass* ClassValue = ResolveNamedObject_ImportBpy<UClass>(Value->AsString());
		ClassProperty->SetObjectPropertyValue(PropertyAddress, ClassValue);
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
		UClass* Class = nullptr;
		TSharedPtr<FJsonObject> Json;
	};

	TArray<FStandaloneSubobjectImport_ImportBpy> ParsedSubobjects;
	ParsedSubobjects.Reserve(SubobjectValues->Num());

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

		FStandaloneSubobjectImport_ImportBpy& Parsed = ParsedSubobjects.AddDefaulted_GetRef();
		Parsed.Name = SubobjectName;
		Parsed.Class = SubobjectClass;
		Parsed.Json = SubobjectJson;

		DesiredSubobjectNames.Add(SubobjectName);
		DesiredSubobjectClasses.Add(SubobjectClass);
	}

	// Clean up stale import-managed subobjects of the same classes that are no
	// longer present in the desired set (common after repeated round-trips).
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

	for (const FStandaloneSubobjectImport_ImportBpy& ParsedSubobject : ParsedSubobjects)
	{
		UObject* Subobject = FindObject<UObject>(Asset, *ParsedSubobject.Name);
		if (Subobject && !Subobject->IsA(ParsedSubobject.Class))
		{
			// Name collision with a different class: move the old object aside and recreate.
			Subobject->Rename(
				nullptr,
				GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			Subobject = nullptr;
		}

		if (!Subobject)
		{
			Subobject = NewObject<UObject>(Asset, ParsedSubobject.Class, *ParsedSubobject.Name, RF_Public | RF_Transactional);
		}
		if (!Subobject)
		{
			OutError = FString::Printf(TEXT("Failed to create standalone subobject: %s"), *ParsedSubobject.Name);
			return false;
		}

		const TSharedPtr<FJsonObject>* PropertiesJson = nullptr;
		if (ParsedSubobject.Json->TryGetObjectField(TEXT("properties"), PropertiesJson) && PropertiesJson && PropertiesJson->IsValid())
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
					ApplyJsonObjectToObject_ImportBpy(ComponentNode->ComponentTemplate, *PropertiesObj);
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

bool ApplyNodeProps_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson, FString& OutError)
{
	if (!Node || !NodeJson.IsValid())
	{
		return true;
	}

	bool bNeedsReconstruct = false;
	bool bApplySelectIndexTypePostReconstruct = false;
	bool bApplySelectValueTypePostReconstruct = false;
	bool bApplySetFieldsVisiblePinsPostReconstruct = false;
	FEdGraphPinType SelectIndexPinType;
	FEdGraphPinType SelectValuePinType;
	TSet<FName> SetFieldsVisiblePins;

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

			BlendListByEnumNode->ReloadEnum(EnumObject);
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

	if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
	{
		FString BoundGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("BoundGraphJson"), BoundGraphJsonText) && !BoundGraphJsonText.IsEmpty())
		{
			if (!CompositeNode->BoundGraph)
			{
				OutError = FString::Printf(
					TEXT("Composite node %s does not have a bound graph"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			TSharedPtr<FJsonObject> BoundGraphJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BoundGraphJsonText);
			if (!FJsonSerializer::Deserialize(Reader, BoundGraphJson) || !BoundGraphJson.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Cannot parse BoundGraphJson on node %s"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(CompositeNode->BoundGraph);
			if (!OwningBlueprint)
			{
				OutError = FString::Printf(
					TEXT("Cannot resolve owning blueprint for composite node %s"),
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

			bNeedsReconstruct = true;
		}
	}

	if (UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
	{
		FString StateMachineGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("StateMachineGraphJson"), StateMachineGraphJsonText) &&
			!StateMachineGraphJsonText.IsEmpty())
		{
			if (!StateMachineNode->EditorStateMachineGraph)
			{
				OutError = FString::Printf(
					TEXT("State machine node %s does not have an editor graph"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					FBlueprintEditorUtils::FindBlueprintForNodeChecked(Node),
					StateMachineNode->EditorStateMachineGraph,
					StateMachineGraphJsonText,
					OutError))
			{
				return false;
			}
		}
	}

	if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
	{
		FString BoundGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("BoundGraphJson"), BoundGraphJsonText) && !BoundGraphJsonText.IsEmpty())
		{
			if (!StateNode->BoundGraph)
			{
				OutError = FString::Printf(
					TEXT("State node %s does not have a bound graph"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					FBlueprintEditorUtils::FindBlueprintForNodeChecked(Node),
					StateNode->BoundGraph,
					BoundGraphJsonText,
					OutError))
			{
				return false;
			}
		}
	}

	if (UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
	{
		FString BoundGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("BoundGraphJson"), BoundGraphJsonText) && !BoundGraphJsonText.IsEmpty())
		{
			if (!ConduitNode->BoundGraph)
			{
				OutError = FString::Printf(
					TEXT("Conduit node %s does not have a bound graph"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					FBlueprintEditorUtils::FindBlueprintForNodeChecked(Node),
					ConduitNode->BoundGraph,
					BoundGraphJsonText,
					OutError))
			{
				return false;
			}
		}
	}

	if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
	{
		FString BoundGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("BoundGraphJson"), BoundGraphJsonText) && !BoundGraphJsonText.IsEmpty())
		{
			if (!TransitionNode->GetBoundGraph())
			{
				OutError = FString::Printf(
					TEXT("Transition node %s does not have a bound graph"),
					*DescribeNode_ImportBpy(Node));
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					FBlueprintEditorUtils::FindBlueprintForNodeChecked(Node),
					TransitionNode->GetBoundGraph(),
					BoundGraphJsonText,
					OutError))
			{
				return false;
			}
		}

		FString CustomTransitionGraphJsonText;
		if ((*NodePropsObj)->TryGetStringField(TEXT("CustomTransitionGraphJson"), CustomTransitionGraphJsonText) &&
			!CustomTransitionGraphJsonText.IsEmpty())
		{
			if (!EnsureTransitionCustomGraphExists_ImportBpy(TransitionNode, OutError))
			{
				return false;
			}

			if (!PopulateNestedGraphFromJsonText_ImportBpy(
					FBlueprintEditorUtils::FindBlueprintForNodeChecked(Node),
					TransitionNode->GetCustomTransitionGraph(),
					CustomTransitionGraphJsonText,
					OutError))
			{
				return false;
			}
		}
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*NodePropsObj)->Values)
	{
		const FString& Key = Entry.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Entry.Value;
		if (!JsonValue.IsValid())
		{
			continue;
		}

		if (Key.StartsWith(TEXT("Variable")))
		{
			continue;
		}
		if (Key == TEXT("BoundGraphJson"))
		{
			continue;
		}
		if (Key == TEXT("StateMachineGraphJson") ||
			Key == TEXT("CustomTransitionGraphJson") ||
			Key == TEXT("AliasedStateUids"))
		{
			continue;
		}
		if (Cast<UK2Node_EnumEquality>(Node) && Key == TEXT("Enum"))
		{
			continue;
		}
		if (Cast<UAnimGraphNode_BlendListByEnum>(Node) && Key == TEXT("Enum"))
		{
			continue;
		}
		if ((Cast<UAnimGraphNode_SaveCachedPose>(Node) || Cast<UAnimGraphNode_UseCachedPose>(Node)) &&
			(Key == TEXT("CacheName") || Key == TEXT("CachePoseName")))
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

		if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
		{
			if (Key == TEXT("Enum") ||
				Key == TEXT("IndexType") ||
				Key == TEXT("IndexContainer") ||
				Key == TEXT("ValueType") ||
				Key == TEXT("ValueContainer"))
			{
				continue;
			}
		}

		if (Cast<UK2Node_SetFieldsInStruct>(Node) && Key == TEXT("VisiblePins"))
		{
			continue;
		}

		if (FProperty* Property = Node->GetClass()->FindPropertyByName(FName(*Key)))
		{
			ApplyJsonValueToProperty_ImportBpy(Node, Property, JsonValue);
			bNeedsReconstruct = true;
		}
	}

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

	if (bApplySetFieldsVisiblePinsPostReconstruct)
	{
		if (UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
		{
			ApplySetFieldsVisiblePins_ImportBpy(SetFieldsNode, SetFieldsVisiblePins);
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
		if (UEdGraphPin* Pin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Input))
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
			if (UEdGraphPin* RetriedPin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Input))
			{
				ApplyDefaultToPin_ImportBpy(RetriedPin, Entry.Value);
				continue;
			}

			OutError = FString::Printf(
				TEXT("Cannot resolve input pin '%s' while applying default on node %s"),
				*SerializedPinName,
				*DescribeNode_ImportBpy(Node));
			return false;
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
		UEdGraphPin* Pin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Input);
		if (!Pin)
		{
			Pin = FindSerializedPinOnNode_ImportBpy(Node, NodeJson, SerializedPinName, EGPD_Output);
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

bool RestoreCreateDelegateNodesAfterConnections_ImportBpy(
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

		const FString SelectedFunctionName = GetNodePropString_ImportBpy(NodeObj, TEXT("SelectedFunctionName"));
		if (SelectedFunctionName.IsEmpty())
		{
			continue;
		}

		CreateDelegateNode->SetFunction(FName(*SelectedFunctionName));
		CreateDelegateNode->HandleAnyChangeWithoutNotifying();

		if (CreateDelegateNode->GetFunctionName().IsNone())
		{
			OutError = FString::Printf(
				TEXT("Failed to restore CreateDelegate binding '%s' on node %s after connections"),
				*SelectedFunctionName,
				*DescribeNode_ImportBpy(CreateDelegateNode));
			return false;
		}
	}

	return true;
}

bool ApplyNodeJsonToNode_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson, FString& OutError)
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

	FString NodeGuidText;
	if (NodeJson->TryGetStringField(TEXT("node_guid"), NodeGuidText))
	{
		FGuid ParsedGuid;
		if (TryParseGuid_ImportBpy(NodeGuidText, ParsedGuid))
		{
			UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
			if (IsNodeGuidAlreadyUsedInBlueprint_ImportBpy(OwningBlueprint, ParsedGuid, Node))
			{
				Node->CreateNewGuid();
			}
			else
			{
				Node->NodeGuid = ParsedGuid;
			}
		}
	}

	if (!ApplyNodeProps_ImportBpy(Node, NodeJson, OutError))
	{
		return false;
	}
	if (!ApplyPinDefaults_ImportBpy(Node, NodeJson, OutError, true))
	{
		return false;
	}
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

void ClearGraphNodes_ImportBpy(UBlueprint* BP, UEdGraph* Graph, bool bPreserveTunnelNodes)
{
	if (!BP || !Graph)
	{
		return;
	}

	ResetAnimationGraphResultNode_ImportBpy(Graph);

	const bool bIsFunctionGraph =
		BP->FunctionGraphs.Contains(Graph) &&
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, Graph, TEXT("function"), Graph->GetName());
	TArray<UEdGraphNode*> ExistingNodes = Graph->Nodes;
	for (UEdGraphNode* Node : ExistingNodes)
	{
		if (!Node)
		{
			continue;
		}

		// Function graphs must keep their generated entry/result nodes so the
		// graph retains a valid function name/signature binding during compile.
		if (bIsFunctionGraph && (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>()))
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

	TSharedPtr<FJsonObject> GraphJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphJsonText);
	if (!FJsonSerializer::Deserialize(Reader, GraphJson) || !GraphJson.IsValid())
	{
		OutError = TEXT("Cannot parse nested graph json");
		return false;
	}

	FString SerializedGraphName;
	if (GraphJson->TryGetStringField(TEXT("name"), SerializedGraphName) &&
		!SerializedGraphName.IsEmpty() &&
		Graph->GetName() != SerializedGraphName)
	{
		FBlueprintEditorUtils::RenameGraph(Graph, SerializedGraphName);
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

bool IsNodeGuidAlreadyUsedInBlueprint_ImportBpy(UBlueprint* BP, const FGuid& Guid, const UEdGraphNode* IgnoreNode)
{
	if (!BP || !Guid.IsValid())
	{
		return false;
	}

	TArray<UObject*> NestedObjects;
	GetObjectsWithOuter(BP, NestedObjects, true);
	for (UObject* NestedObject : NestedObjects)
	{
		const UEdGraphNode* ExistingNode = Cast<UEdGraphNode>(NestedObject);
		if (!ExistingNode || ExistingNode == IgnoreNode)
		{
			continue;
		}

		if (ExistingNode->NodeGuid == Guid)
		{
			return true;
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
	if (!bIsStateGraphResult && !bIsTransitionGraphResult && !bIsCustomTransitionGraphResult)
	{
		return;
	}

	FObjectPropertyBase* ResultNodeProperty =
		FindFProperty<FObjectPropertyBase>(Graph->GetClass(), TEXT("MyResultNode"));
	if (ResultNodeProperty)
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
		const FString SourceBlueprintPath =
			Root->HasTypedField<EJson::String>(TEXT("path"))
				? Root->GetStringField(TEXT("path"))
				: FString();
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
	if (Root->TryGetArrayField(TEXT("graphs"), GraphsArr))
	{
		TArray<TSharedPtr<FJsonObject>> SortedGraphs;
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
			UEdGraph* Graph = nullptr;
			FString GraphType;
			FString GraphName;
			if (!EnsureGraphExists_ImportBpy(BP, GraphObj, Graph, GraphType, GraphName, OutError))
			{
				return false;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		if (Cast<UAnimBlueprint>(BP))
		{
			if (!FKismetEditorUtilities::GenerateBlueprintSkeleton(BP, true) && !BP->SkeletonGeneratedClass)
			{
				OutError = FString::Printf(
					TEXT("Failed to regenerate skeleton for anim blueprint '%s' before graph population"),
					*BP->GetPathName());
				return false;
			}
		}

		for (const TSharedPtr<FJsonObject>& GraphObj : SortedGraphs)
		{
			if (!CreateGraph(BP, GraphObj, OutError))
			{
				return false;
			}
		}
	}

	if (bCompileBlueprint)
	{
		CompileBlueprint(BP);
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
	const bool bTreatAsRegularFunctionGraph =
		(GraphType == TEXT("function")) &&
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, Graph, GraphJson, GraphType, GraphName);

	if (!GraphName.IsEmpty() && Graph->GetName() != GraphName)
	{
		FBlueprintEditorUtils::RenameGraph(Graph, GraphName);
	}

	// Import is authoritative for a graph. Clear pre-existing/default nodes first so
	// re-imports do not accumulate stale nodes such as the template Event Tick.
	ClearGraphNodes_ImportBpy(BP, Graph, bPreserveTunnelNodes);

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

		if (GraphType == TEXT("event_graph"))
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
				if (!ApplyNodeJsonToNode_ImportBpy(Node, NodeObj, OutError))
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
				if (!ApplyNodeJsonToNode_ImportBpy(Node, NodeObj, OutError))
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
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

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

			(*ExistingNode)->ReconstructNode();
			if (!ApplyNodeJsonToNode_ImportBpy(*ExistingNode, NodeObj, OutError))
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
	if (NodesArr && Cast<UAnimBlueprint>(BP) && bIsAnimationGraph)
	{
		FKismetEditorUtilities::GenerateBlueprintSkeleton(BP, true);

		for (const TPair<FString, UEdGraphNode*>& NodePair : NodeMap)
		{
			if (UAnimGraphNode_LinkedAnimGraphBase* LinkedAnimNode = Cast<UAnimGraphNode_LinkedAnimGraphBase>(NodePair.Value))
			{
				LinkedAnimNode->ReconstructNode();
			}
		}
	}

	BreakAllGraphLinks_ImportBpy(Graph);

	const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
	if (GraphJson->TryGetArrayField(TEXT("connections"), ConnsArr))
	{
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
					OutError = FString::Printf(TEXT("Connection references missing node(s): %s -> %s"), *SrcUid, *DstUid);
					return false;
				}

				const auto IsStateMachineStructuralNode = [](UEdGraphNode* Node) -> bool
				{
					return Node &&
						(Cast<UAnimStateNodeBase>(Node) ||
						 Cast<UAnimStateAliasNode>(Node) ||
						 Cast<UAnimStateConduitNode>(Node));
				};
				if (GraphType == TEXT("state_machine") &&
					!Cast<UAnimStateEntryNode>(*SrcNodePtr) &&
					IsStateMachineStructuralNode(*SrcNodePtr) &&
					IsStateMachineStructuralNode(*DstNodePtr))
				{
					// State machine exports include both the high-level state-to-state edge and
					// the explicit AnimStateTransitionNode wiring. Replaying the direct edge makes
					// AnimGraph auto-spawn duplicate empty transition nodes.
					bMadeProgress = true;
					continue;
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
				OutError = LastConnectError.IsEmpty()
					? TEXT("Failed to resolve deferred graph connections")
					: LastConnectError;
				return false;
			}

			PendingConnections = MoveTemp(RemainingConnections);
		}
	}

	if (!RestoreCreateDelegateNodesAfterConnections_ImportBpy(NodesArr, NodeMap, OutError))
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
	else if (NodeClass == TEXT("K2Node_TransitionRuleGetter"))
	{
		UK2Node_TransitionRuleGetter* GetterNode = NewObject<UK2Node_TransitionRuleGetter>(Graph);
		GetterNode->CreateNewGuid();
		GetterNode->PostPlacedNewNode();
		GetterNode->AllocateDefaultPins();
		Graph->AddNode(GetterNode, false, false);
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
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("StructType") },
			OutError);
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
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("Chooser"), TEXT("Mode") },
			OutError);
	}
	else if (NodeClass == TEXT("K2Node_EvaluateChooser2"))
	{
		Result = CreateResolvedNodeWithDefaultPins_ImportBpy(
			Graph,
			NodeClass,
			NodeJson,
			{ TEXT("Chooser"), TEXT("Mode"), TEXT("bReturnSoftObjectReference") },
			OutError);
	}
	// ── Mover Async Nodes ────────────────────────────────────────────
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

	if (!ApplyNodeJsonToNode_ImportBpy(Result, NodeJson, OutError))
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
		Func = ResolveFunctionOnBlueprintContext_ImportBpy(Graph, FuncName);
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

	if (Func)
	{
		Node->SetFromFunction(Func);
	}

	if (bSelfContextCall)
	{
		if (!Func)
		{
			// Keep unresolved bare calls as self members so later reconstruct/compile
			// can bind them against the current blueprint rather than some globally
			// discovered function on a different generated class.
			Node->FunctionReference.SetSelfMember(FName(*FuncName));
		}
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

	if (!SrcPin || !DstPin)
	{
		OutError = FString::Printf(
			TEXT("Cannot resolve connection pins: %s.%s -> %s.%s"),
			*DescribeNode_ImportBpy(SrcNode),
			*(!SrcPinFullName.IsEmpty() ? SrcPinFullName : SrcPinName),
			*DescribeNode_ImportBpy(DstNode),
			*(!DstPinFullName.IsEmpty() ? DstPinFullName : DstPinName));
		return false;
	}

	const UEdGraphSchema* Schema = SrcPin->GetSchema();
	if (Schema && Schema->TryCreateConnection(SrcPin, DstPin))
	{
		return true;
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

	if (!SrcPin->LinkedTo.Contains(DstPin))
	{
		OutError = FString::Printf(
			TEXT("Schema rejected connection: %s.%s -> %s.%s"),
			*DescribeNode_ImportBpy(SrcNode),
			*SrcPin->GetName(),
			*DescribeNode_ImportBpy(DstNode),
			*DstPin->GetName());
		return false;
	}

	return true;
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
